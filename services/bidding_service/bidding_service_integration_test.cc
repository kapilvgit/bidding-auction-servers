//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include <thread>

#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/blocking_counter.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/bidding_service/benchmarking/bidding_benchmarking_logger.h"
#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/bidding_service/bidding_service.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper_test_constants.h"
#include "services/common/clients/code_dispatcher/code_dispatch_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using ::google::protobuf::TextFormat;
using ::testing::AnyNumber;

// Must be ample time for generateBid() to complete, otherwise we risk
// flakiness. In production, generateBid() should run in no more than a few
// hundred milliseconds.
constexpr int kGenerateBidExecutionTimeSeconds = 2;
constexpr char kKeyId[] = "key_id";
constexpr char kSecret[] = "secret";
constexpr char kAdRenderUrlPrefixForTest[] = "https://advertising.net/ad?";

// While Roma demands JSON input and enforces it strictly, we follow the
// javascript style guide for returning objects here, so object keys are
// unquoted on output even though they MUST be quoted on input.
constexpr absl::string_view js_code_template = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_requiring_user_bidding_signals_template =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      // Test that user bidding signals are present
      let length = interest_group.userBiddingSignals.length;

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view
    js_code_requiring_parsed_user_bidding_signals_template =
        R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      let ubs = interest_group.userBiddingSignals;
      if ((ubs.someId === 1789) && (ubs.name === "winston")
          && ((ubs.years[0] === 1776) && (ubs.years[1] === 1868))) {
        // Reshaped into an AdWithBid.
        return {
          render: "%s" + interest_group.adRenderIds[0],
          ad: {"arbitraryMetadataField": 1},
          bid: bid,
          allowComponentAuction: false
        };
      }
    }
  )JS_CODE";

constexpr absl::string_view js_code_requiring_trusted_bidding_signals_template =
    R"JS_CODE(
    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      const bid = Math.floor(Math.random() * 10 + 1);

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"tbsLength": Object.keys(trusted_bidding_signals).length},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view
    js_code_requiring_trusted_bidding_signals_keys_template =
        R"JS_CODE(
    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      const bid = Math.floor(Math.random() * 10 + 1);

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"tbskLength": interest_group.trustedBiddingSignalsKeys.length},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_debug_urls_template = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      forDebuggingOnly.reportAdAuctionLoss("https://example-dsp.com/debugLoss");
      forDebuggingOnly.reportAdAuctionWin("https://example-dsp.com/debugWin");

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_throws_exception = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));
      throw new Error('Exception message');
    }
  )JS_CODE";

constexpr absl::string_view js_code_throws_exception_with_debug_urls =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      forDebuggingOnly.reportAdAuctionLoss("https://example-dsp.com/debugLoss");
      forDebuggingOnly.reportAdAuctionWin("https://example-dsp.com/debugWin");
      throw new Error('Exception message');
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_logs_template = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));
      console.log("Logging from generateBid");
      console.warn("Warning from generateBid");
      console.error("Error from generateBid");
      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

void SetupMockCryptoClientWrapper(MockCryptoClientWrapper& crypto_client) {
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .Times(testing::AnyNumber())
      .WillRepeatedly(
          [](const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
             const std::string& plaintext_payload) {
            google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
                hpke_encrypt_response;
            hpke_encrypt_response.set_secret(kSecret);
            hpke_encrypt_response.mutable_encrypted_data()->set_key_id(kKeyId);
            hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
                plaintext_payload);
            return hpke_encrypt_response;
          });

  // Mock the HpkeDecrypt() call on the crypto_client. This is used by the
  // service to decrypt the incoming request.
  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .Times(AnyNumber())
      .WillRepeatedly([](const server_common::PrivateKey& private_key,
                         const std::string& ciphertext) {
        google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
            hpke_decrypt_response;
        *hpke_decrypt_response.mutable_payload() = ciphertext;
        hpke_decrypt_response.set_secret(kSecret);
        return hpke_decrypt_response;
      });

  // Mock the AeadEncrypt() call on the crypto_client. This is used to encrypt
  // the response coming back from the service.
  EXPECT_CALL(crypto_client, AeadEncrypt)
      .Times(AnyNumber())
      .WillRepeatedly(
          [](const std::string& plaintext_payload, const std::string& secret) {
            google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
            *data.mutable_ciphertext() = plaintext_payload;
            google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
                aead_encrypt_response;
            *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
            return aead_encrypt_response;
          });
}

// The following is a base64 encoded string of wasm binary output
// that exports a function with the following definition:
// int plusOne(int x)
constexpr absl::string_view base64_wasm_plus_one =
    "AGFzbQEAAAABhoCAgAABYAF/"
    "AX8DgoCAgAABAASEgICAAAFwAAAFg4CAgAABAAEGgYCAgAAAB5SAgIAAAgZtZW1vcnkCAAdwbH"
    "VzT25lAAAKjYCAgAABh4CAgAAAIABBAWoL";
constexpr absl::string_view js_code_runs_wasm_helper = R"JS_CODE(
function generateBid( interest_group,
                      auction_signals,
                      buyer_signals,
                      trusted_bidding_signals,
                      device_signals) {
  const instance = new WebAssembly.Instance(device_signals.wasmHelper);

  //Reshaped into an AdWithBid.
  return {
    render: "%s" + interest_group.adRenderIds[0],
    ad: {"tbsLength": Object.keys(trusted_bidding_signals).length},
    bid: instance.exports.plusOne(0),
    allowComponentAuction: false
  };
}
)JS_CODE";

void SetupV8Dispatcher(V8Dispatcher* dispatcher, absl::string_view adtech_js,
                       absl::string_view adtech_wasm = "",
                       bool enable_buyer_code_wrapper = false) {
  DispatchConfig config;
  ASSERT_TRUE(dispatcher->Init(config).ok());
  std::string wrapper_blob = std::string(adtech_js);
  if (enable_buyer_code_wrapper) {
    wrapper_blob = GetBuyerWrappedCode(adtech_js, adtech_wasm);
  }
  ASSERT_TRUE(dispatcher->LoadSync(1, wrapper_blob).ok());
}

GenerateBidsRequest::GenerateBidsRawRequest BuildGenerateBidsRequestFromBrowser(
    absl::flat_hash_map<std::string, std::vector<std::string>>*
        interest_group_to_ad,
    int desired_bid_count = 5, bool set_enable_debug_reporting = false) {
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  raw_request.set_enable_debug_reporting(set_enable_debug_reporting);
  for (int i = 0; i < desired_bid_count; i++) {
    auto interest_group = MakeARandomInterestGroupForBiddingFromBrowser();
    interest_group_to_ad->try_emplace(
        interest_group.name(),
        std::vector<std::string>(interest_group.ad_render_ids().begin(),
                                 interest_group.ad_render_ids().end()));
    *raw_request.mutable_interest_group_for_bidding()->Add() =
        std::move(interest_group);
  }
  raw_request.set_bidding_signals(MakeRandomTrustedBiddingSignals(raw_request));
  return raw_request;
}

class GenerateBidsReactorIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    server_common::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::TelemetryConfig::PROD);
    metric::BiddingContextMap(
        server_common::BuildDependentConfig(config_proto));

    TrustedServersConfigClient config_client({});
    config_client.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
    config_client.SetFlagForTest(kTrue, TEST_MODE);
    key_fetcher_manager_ = CreateKeyFetcherManager(config_client);
    SetupMockCryptoClientWrapper(*crypto_client_);
  }

  std::unique_ptr<MockCryptoClientWrapper> crypto_client_ =
      std::make_unique<MockCryptoClientWrapper>();
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  BiddingServiceRuntimeConfig bidding_service_runtime_config_ = {
      .encryption_enabled = true};
};

TEST_F(GenerateBidsReactorIntegrationTest, GeneratesBidsByInterestGroupCode) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, absl::StrFormat(js_code_template,
                                                 kAdRenderUrlPrefixForTest));

  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  auto raw_request = BuildGenerateBidsRequestFromBrowser(&interest_group_to_ad);
  request.set_request_ciphertext(raw_request.SerializeAsString());
  GenerateBidsResponse response;

  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        // You can manually flip this flag to turn benchmarking logging on or
        // off
        bool enable_benchmarking = true;
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger;
        if (enable_benchmarking) {
          benchmarking_logger = std::make_unique<BiddingBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarking_logger = std::make_unique<BiddingNoOpLogger>();
        }
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };

  BiddingService service(
      std::move(generate_bids_reactor_factory), std::move(key_fetcher_manager_),
      std::move(crypto_client_), bidding_service_runtime_config_);
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    std::string expected_render_url =
        kAdRenderUrlPrefixForTest +
        interest_group_to_ad.at(ad_with_bid.interest_group_name()).at(0);
    EXPECT_GT(ad_with_bid.render().length(), 0);
    EXPECT_EQ(ad_with_bid.render(), expected_render_url);
    // Expected false because it is expected to be present and was manually set
    // to false.
    EXPECT_FALSE(ad_with_bid.allow_component_auction());
    ASSERT_TRUE(ad_with_bid.ad().has_struct_value());
    EXPECT_EQ(ad_with_bid.ad().struct_value().fields_size(), 1);
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("arbitraryMetadataField")
                  .number_value(),
              1.0);
  }
  EXPECT_TRUE(dispatcher.Stop().ok());
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsWithParsedUserBiddingSignals) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(
      &dispatcher,
      absl::StrFormat(js_code_requiring_parsed_user_bidding_signals_template,
                      kAdRenderUrlPrefixForTest));

  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  auto raw_request = BuildGenerateBidsRequestFromBrowser(&interest_group_to_ad);
  request.set_request_ciphertext(raw_request.SerializeAsString());
  GenerateBidsResponse response;
  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        // You can manually flip this flag to turn benchmarking logging on or
        // off
        bool enable_benchmarking = true;
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger;
        if (enable_benchmarking) {
          benchmarking_logger = std::make_unique<BiddingBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarking_logger = std::make_unique<BiddingNoOpLogger>();
        }
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };

  BiddingService service(
      std::move(generate_bids_reactor_factory), std::move(key_fetcher_manager_),
      std::move(crypto_client_), bidding_service_runtime_config_);
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    std::string expected_render_url =
        kAdRenderUrlPrefixForTest +
        interest_group_to_ad.at(ad_with_bid.interest_group_name()).at(0);
    EXPECT_GT(ad_with_bid.render().length(), 0);
    EXPECT_EQ(ad_with_bid.render(), expected_render_url);
    // Expected false because it is expected to be present and was manually set
    // to false.
    EXPECT_FALSE(ad_with_bid.allow_component_auction());
    ASSERT_TRUE(ad_with_bid.ad().has_struct_value());
    EXPECT_EQ(ad_with_bid.ad().struct_value().fields_size(), 1);
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("arbitraryMetadataField")
                  .number_value(),
              1.0);
  }
  EXPECT_TRUE(dispatcher.Stop().ok());
}

TEST_F(GenerateBidsReactorIntegrationTest, ReceivesTrustedBiddingSignals) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(
      &dispatcher,
      absl::StrFormat(js_code_requiring_trusted_bidding_signals_template,
                      kAdRenderUrlPrefixForTest));

  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  auto raw_request = BuildGenerateBidsRequestFromBrowser(&interest_group_to_ad);
  request.set_request_ciphertext(raw_request.SerializeAsString());
  ASSERT_GT(raw_request.bidding_signals().length(), 0);

  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger =
            std::make_unique<BiddingNoOpLogger>();
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };
  GenerateBidsResponse response;
  BiddingService service(
      std::move(generate_bids_reactor_factory), std::move(key_fetcher_manager_),
      std::move(crypto_client_), bidding_service_runtime_config_);
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    ASSERT_TRUE(ad_with_bid.ad().struct_value().fields().find("tbsLength") !=
                ad_with_bid.ad().struct_value().fields().end());
    // One signal key per IG.
    EXPECT_EQ(
        ad_with_bid.ad().struct_value().fields().at("tbsLength").number_value(),
        1);
  }
  EXPECT_TRUE(dispatcher.Stop().ok());
}

TEST_F(GenerateBidsReactorIntegrationTest, ReceivesTrustedBiddingSignalsKeys) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(
      &dispatcher,
      absl::StrFormat(js_code_requiring_trusted_bidding_signals_keys_template,
                      kAdRenderUrlPrefixForTest));

  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  GenerateBidsRequest::GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRequestFromBrowser(&interest_group_to_ad);
  request.set_request_ciphertext(raw_request.SerializeAsString());
  ASSERT_GT(raw_request.interest_group_for_bidding(0)
                .trusted_bidding_signals_keys_size(),
            0);

  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger =
            std::make_unique<BiddingNoOpLogger>();
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };
  GenerateBidsResponse response;
  BiddingService service(
      std::move(generate_bids_reactor_factory), std::move(key_fetcher_manager_),
      std::move(crypto_client_), bidding_service_runtime_config_);
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    ASSERT_TRUE(ad_with_bid.ad().struct_value().fields().find("tbskLength") !=
                ad_with_bid.ad().struct_value().fields().end());
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("tbskLength")
                  .number_value(),
              raw_request.interest_group_for_bidding(0)
                  .trusted_bidding_signals_keys_size());
  }
  EXPECT_TRUE(dispatcher.Stop().ok());
}

/*
 * This test exists to demonstrate that if an AdTech's script expects a
 * property to be present in the interest group, but that property is set to a
 * value which the protobuf serializer serializes to an empty string, then that
 * property WILL BE OMITTED from the serialized interest_group passed to the
 * generateBid() script, and the script will CRASH.
 * It so happens that the SideLoad Data provider provided such a value for
 * interestGroup.userBiddingSignals when userBiddingSignals are not present,
 * and the generateBid() script with which we test requires the
 * .userBiddingSignals property to be present and crashes when it is absent.
 */
TEST_F(GenerateBidsReactorIntegrationTest,
       FailsToGenerateBidsWhenMissingUserBiddingSignals) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(
      &dispatcher,
      absl::StrFormat(js_code_requiring_user_bidding_signals_template,
                      kAdRenderUrlPrefixForTest));
  int desired_bid_count = 5;
  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  for (int i = 0; i < desired_bid_count; i++) {
    auto interest_group = MakeARandomInterestGroupForBidding(false, true);
    interest_group_to_ad.try_emplace(
        interest_group.name(),
        std::vector<std::string>(interest_group.ad_render_ids().begin(),
                                 interest_group.ad_render_ids().end()));

    ASSERT_TRUE(interest_group.user_bidding_signals().empty());
    *raw_request.mutable_interest_group_for_bidding()->Add() =
        std::move(interest_group);
  }
  GenerateBidsResponse response;
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();
  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        // You can manually flip this flag to turn benchmarking logging on or
        // off
        bool enable_benchmarking = true;
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger;
        if (enable_benchmarking) {
          benchmarking_logger = std::make_unique<BiddingBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarking_logger = std::make_unique<BiddingNoOpLogger>();
        }
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };
  BiddingService service(
      std::move(generate_bids_reactor_factory), std::move(key_fetcher_manager_),
      std::move(crypto_client_), bidding_service_runtime_config_);
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));

  ASSERT_TRUE(response.IsInitialized());
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  ASSERT_TRUE(raw_response.IsInitialized());

  // All instances of the script should have crashed; no bids should have been
  // generated.
  ASSERT_EQ(raw_response.bids_size(), 0);
  EXPECT_TRUE(dispatcher.Stop().ok());
}

TEST_F(GenerateBidsReactorIntegrationTest, GeneratesBidsFromDevice) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, absl::StrFormat(js_code_template,
                                                 kAdRenderUrlPrefixForTest));
  int desired_bid_count = 1;
  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  for (int i = 0; i < desired_bid_count; i++) {
    auto interest_group = MakeAnInterestGroupForBiddingSentFromDevice();
    ASSERT_EQ(interest_group.ad_render_ids_size(), 2);
    interest_group_to_ad.try_emplace(
        interest_group.name(),
        std::vector<std::string>(interest_group.ad_render_ids().begin(),
                                 interest_group.ad_render_ids().end()));
    *raw_request.mutable_interest_group_for_bidding()->Add() =
        std::move(interest_group);
    // This fails in production, the user Bidding Signals are not being set.
    // use logging to figure out why.
  }
  raw_request.set_bidding_signals(MakeRandomTrustedBiddingSignals(raw_request));
  GenerateBidsResponse response;
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();
  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        // You can manually flip this flag to turn benchmarking logging on or
        // off
        bool enable_benchmarking = true;
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger;
        if (enable_benchmarking) {
          benchmarking_logger = std::make_unique<BiddingBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarking_logger = std::make_unique<BiddingNoOpLogger>();
        }
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };
  BiddingService service(
      std::move(generate_bids_reactor_factory), std::move(key_fetcher_manager_),
      std::move(crypto_client_), bidding_service_runtime_config_);
  service.GenerateBids(&context, &request, &response);

  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 1);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    std::string expected_render_url = absl::StrCat(
        kAdRenderUrlPrefixForTest,
        interest_group_to_ad.at(ad_with_bid.interest_group_name()).at(0));
    EXPECT_GT(ad_with_bid.render().length(), 0);
    EXPECT_EQ(ad_with_bid.render(), expected_render_url);
    // Expected false because it is expected to be present and was manually set
    // to false.
    EXPECT_FALSE(ad_with_bid.allow_component_auction());
    ASSERT_TRUE(ad_with_bid.ad().has_struct_value());
    EXPECT_EQ(ad_with_bid.ad().struct_value().fields_size(), 1);
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("arbitraryMetadataField")
                  .number_value(),
              1.0);
  }
  EXPECT_TRUE(dispatcher.Stop().ok());
}

void GenerateBidCodeWrapperTestHelper(GenerateBidsResponse* response,
                                      absl::string_view js_blob,
                                      bool enable_debug_reporting,
                                      bool enable_buyer_debug_url_generation,
                                      bool enable_buyer_code_wrapper = true,
                                      bool enable_adtech_code_logging = false,
                                      absl::string_view wasm_blob = "") {
  int desired_bid_count = 5;
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, js_blob, wasm_blob, enable_buyer_code_wrapper);
  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  auto raw_request = BuildGenerateBidsRequestFromBrowser(
      &interest_group_to_ad, desired_bid_count, enable_debug_reporting);
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();

  auto generate_bids_reactor_factory =
      [&client](const GenerateBidsRequest* request,
                GenerateBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                const BiddingServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger;
        benchmarking_logger = std::make_unique<BiddingBenchmarkingLogger>(
            FormatTime(absl::Now()));
        return new GenerateBidsReactor(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, runtime_config);
      };

  std::unique_ptr<MockCryptoClientWrapper> crypto_client =
      std::make_unique<MockCryptoClientWrapper>();
  TrustedServersConfigClient config_client({});
  config_client.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config_client.SetFlagForTest(kTrue, TEST_MODE);
  SetupMockCryptoClientWrapper(*crypto_client);
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager = CreateKeyFetcherManager(config_client);

  const BiddingServiceRuntimeConfig& runtime_config = {
      .encryption_enabled = true,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
      .enable_buyer_code_wrapper = enable_buyer_code_wrapper,
      .enable_adtech_code_logging = enable_adtech_code_logging};
  BiddingService service(std::move(generate_bids_reactor_factory),
                         std::move(key_fetcher_manager),
                         std::move(crypto_client), std::move(runtime_config));
  service.GenerateBids(&context, &request, response);
  std::this_thread::sleep_for(
      absl::ToChronoSeconds(absl::Seconds(kGenerateBidExecutionTimeSeconds)));
  EXPECT_TRUE(dispatcher.Stop().ok());
}

TEST_F(GenerateBidsReactorIntegrationTest, BuyerDebugUrlGenerationDisabled) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = false;
  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(js_code_with_debug_urls_template,
                      kAdRenderUrlPrefixForTest),
      enable_debug_reporting, enable_buyer_debug_url_generation);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& adWithBid : raw_response.bids()) {
    EXPECT_GT(adWithBid.bid(), 0);
    EXPECT_FALSE(adWithBid.has_debug_report_urls());
  }
}

TEST_F(GenerateBidsReactorIntegrationTest, EventLevelDebugReportingDisabled) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = false;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(js_code_with_debug_urls_template,
                      kAdRenderUrlPrefixForTest),
      enable_debug_reporting, enable_buyer_debug_url_generation);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& adWithBid : raw_response.bids()) {
    EXPECT_GT(adWithBid.bid(), 0);
    EXPECT_FALSE(adWithBid.has_debug_report_urls());
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsReturnDebugReportingUrls) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(js_code_with_debug_urls_template,
                      kAdRenderUrlPrefixForTest),
      enable_debug_reporting, enable_buyer_debug_url_generation);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& adWithBid : raw_response.bids()) {
    EXPECT_GT(adWithBid.bid(), 0);
    EXPECT_EQ(adWithBid.debug_report_urls().auction_debug_win_url(),
              "https://example-dsp.com/debugWin");
    EXPECT_EQ(adWithBid.debug_report_urls().auction_debug_loss_url(),
              "https://example-dsp.com/debugLoss");
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsReturnDebugReportingUrlsWhenScriptCrashes) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidCodeWrapperTestHelper(
      &response, js_code_throws_exception_with_debug_urls,
      enable_debug_reporting, enable_buyer_debug_url_generation);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& adWithBid : raw_response.bids()) {
    EXPECT_EQ(adWithBid.bid(), 0);
    EXPECT_EQ(adWithBid.debug_report_urls().auction_debug_win_url(),
              "https://example-dsp.com/debugWin");
    EXPECT_EQ(adWithBid.debug_report_urls().auction_debug_loss_url(),
              "https://example-dsp.com/debugLoss");
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       NoGenerateBidsResponseIfNoDebugUrlsAndScriptCrashes) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidCodeWrapperTestHelper(&response, js_code_throws_exception,
                                   enable_debug_reporting,
                                   enable_buyer_debug_url_generation);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 0);
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GenerateBidsReturnsSuccessFullyWithLoggingEnabled) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = false;
  bool enable_buyer_debug_url_generation = false;
  bool enable_buyer_code_wrapper = true;
  bool enable_adtech_code_logging = true;
  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(js_code_with_logs_template, kAdRenderUrlPrefixForTest),
      enable_debug_reporting, enable_buyer_debug_url_generation,
      enable_buyer_code_wrapper, enable_adtech_code_logging);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GenerateBidsReturnsSuccessWithWasmHelperCall) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = false;
  bool enable_buyer_debug_url_generation = false;
  bool enable_buyer_code_wrapper = true;
  bool enable_adtech_code_logging = true;

  std::string raw_wasm_bytes;
  ASSERT_TRUE(absl::Base64Unescape(base64_wasm_plus_one, &raw_wasm_bytes));
  GenerateBidCodeWrapperTestHelper(
      &response, js_code_runs_wasm_helper, enable_debug_reporting,
      enable_buyer_debug_url_generation, enable_buyer_code_wrapper,
      enable_adtech_code_logging, raw_wasm_bytes);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& adWithBid : raw_response.bids()) {
    EXPECT_EQ(adWithBid.bid(), 1);
  }
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
