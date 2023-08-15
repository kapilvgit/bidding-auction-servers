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

#include "services/auction_service/auction_service.h"

#include <memory>
#include <string>

#include <grpcpp/server.h>

#include <gmock/gmock-matchers.h>

#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "api/bidding_auction_servers.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/score_ads_reactor.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kKeyId[] = "key_id";
constexpr char kSecret[] = "secret";

using ::testing::AnyNumber;

struct LocalAuctionStartResult {
  int port;
  std::unique_ptr<grpc::Server> server;

  // Shutdown the server when the test is done.
  ~LocalAuctionStartResult() {
    if (server) {
      server->Shutdown();
    }
  }
};

LocalAuctionStartResult StartLocalAuction(AuctionService* auction_service) {
  grpc::ServerBuilder builder;
  int port;
  builder.AddListeningPort("[::]:0",
                           grpc::experimental::LocalServerCredentials(
                               grpc_local_connect_type::LOCAL_TCP),
                           &port);
  builder.RegisterService(auction_service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  return {port, std::move(server)};
}

std::unique_ptr<Auction::StubInterface> CreateAuctionStub(int port) {
  std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(
      absl::StrFormat("localhost:%d", port),
      grpc::experimental::LocalCredentials(grpc_local_connect_type::LOCAL_TCP));
  return Auction::NewStub(channel);
}

class AuctionServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_common::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::TelemetryConfig::PROD);
    metric::AuctionContextMap(
        server_common::BuildDependentConfig(config_proto));
    SetupMockCryptoClientWrapper();
  }

  void SetupMockCryptoClientWrapper() {
    // Mock the HpkeDecrypt() call on the crypto_client_. This is used by the
    // service to decrypt the incoming request_.
    EXPECT_CALL(*crypto_client_, HpkeDecrypt)
        .Times(AnyNumber())
        .WillRepeatedly([](const server_common::PrivateKey& private_key,
                           const std::string& ciphertext) {
          google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
              hpke_decrypt_response;
          hpke_decrypt_response.set_payload(ciphertext);
          hpke_decrypt_response.set_secret(kSecret);
          return hpke_decrypt_response;
        });

    // Mock the AeadEncrypt() call on the crypto_client_. This is used to
    // encrypt the response_ coming back from the service.
    EXPECT_CALL(*crypto_client_, AeadEncrypt)
        .Times(AnyNumber())
        .WillOnce([](const std::string& plaintext_payload,
                     const std::string& secret) {
          google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
          data.set_ciphertext(plaintext_payload);
          google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
              aead_encrypt_response;
          *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
          return aead_encrypt_response;
        });
  }

  MockCodeDispatchClient dispatcher_;
  ScoreAdsRequest request_;
  ScoreAdsResponse response_;
  std::unique_ptr<MockCryptoClientWrapper> crypto_client_ =
      std::make_unique<MockCryptoClientWrapper>();
  TrustedServersConfigClient config_client_{{}};
};

TEST_F(AuctionServiceTest, InstantiatesScoreAdsReactor) {
  absl::BlockingCounter init_pending(1);
  auto score_ads_reactor_factory =
      [this, &init_pending](
          const ScoreAdsRequest* request_, ScoreAdsResponse* response_,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client_,
          const AuctionServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarkingLogger =
            std::make_unique<ScoreAdsNoOpLogger>();
        std::unique_ptr<MockAsyncReporter> async_reporter =
            std::make_unique<MockAsyncReporter>(
                std::make_unique<MockHttpFetcherAsync>());
        auto mock = std::make_unique<MockScoreAdsReactor>(
            dispatcher_, request_, response_, key_fetcher_manager,
            crypto_client_, runtime_config, std::move(benchmarkingLogger),
            std::move(async_reporter), "");
        EXPECT_CALL(*mock, Execute).Times(1);
        init_pending.DecrementCount();
        return mock;
      };
  config_client_.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config_client_.SetFlagForTest(kTrue, TEST_MODE);
  auto key_fetcher_manager = CreateKeyFetcherManager(config_client_);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  auction_service_runtime_config.encryption_enabled = true;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client_), auction_service_runtime_config);
  grpc::CallbackServerContext context;
  auto mock = service.ScoreAds(&context, &request_, &response_);
  init_pending.Wait();
  delete mock;
}

TEST_F(AuctionServiceTest, AbortsIfMissingAds) {
  auto score_ads_reactor_factory =
      [this](const ScoreAdsRequest* request_, ScoreAdsResponse* response_,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client_,
             const AuctionServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<MockAsyncReporter> async_reporter =
            std::make_unique<MockAsyncReporter>(
                std::make_unique<MockHttpFetcherAsync>());
        return std::make_unique<ScoreAdsReactor>(
            dispatcher_, request_, response_,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client_, std::move(async_reporter), runtime_config);
      };
  config_client_.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config_client_.SetFlagForTest(kTrue, TEST_MODE);
  auto key_fetcher_manager = CreateKeyFetcherManager(config_client_);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  auction_service_runtime_config.encryption_enabled = true;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client_), auction_service_runtime_config);

  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);

  grpc::ClientContext context;
  grpc::Status status = stub->ScoreAds(&context, request_, &response_);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_EQ(status.error_message(), kNoAdsToScore);
}

TEST_F(AuctionServiceTest, AbortsIfMissingScoringSignals) {
  auto score_ads_reactor_factory =
      [this](const ScoreAdsRequest* request_, ScoreAdsResponse* response_,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client_,
             const AuctionServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<MockAsyncReporter> async_reporter =
            std::make_unique<MockAsyncReporter>(
                std::make_unique<MockHttpFetcherAsync>());
        return std::make_unique<ScoreAdsReactor>(
            dispatcher_, request_, response_,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client_, std::move(async_reporter), runtime_config);
      };
  config_client_.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config_client_.SetFlagForTest(kTrue, TEST_MODE);
  auto key_fetcher_manager = CreateKeyFetcherManager(config_client_);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  auction_service_runtime_config.encryption_enabled = true;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client_), auction_service_runtime_config);

  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);

  grpc::ClientContext context;
  request_.set_key_id(kKeyId);
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  *raw_request.mutable_ad_bids()->Add() = MakeARandomAdWithBidMetadata(1, 10);
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  grpc::Status status = stub->ScoreAds(&context, request_, &response_);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_EQ(status.error_message(), kNoTrustedScoringSignals);
}

TEST_F(AuctionServiceTest, AbortsIfMissingDispatchRequests) {
  auto score_ads_reactor_factory =
      [this](const ScoreAdsRequest* request_, ScoreAdsResponse* response_,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client_,
             const AuctionServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<MockAsyncReporter> async_reporter =
            std::make_unique<MockAsyncReporter>(
                std::make_unique<MockHttpFetcherAsync>());
        return std::make_unique<ScoreAdsReactor>(
            dispatcher_, request_, response_,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client_, std::move(async_reporter), runtime_config);
      };
  config_client_.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config_client_.SetFlagForTest(kTrue, TEST_MODE);
  auto key_fetcher_manager = CreateKeyFetcherManager(config_client_);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  auction_service_runtime_config.encryption_enabled = true;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client_), auction_service_runtime_config);

  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);

  grpc::ClientContext context;
  request_.set_key_id(kKeyId);
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  *raw_request.mutable_ad_bids()->Add() = MakeARandomAdWithBidMetadata(1, 10);
  raw_request.set_scoring_signals(
      R"json({"renderUrls":{"placeholder_url":[123]}})json");
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  grpc::Status status = stub->ScoreAds(&context, request_, &response_);

  ASSERT_EQ(status.error_message(), kNoAdsWithValidScoringSignals);
  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
