/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_SELECT_AD_REACTOR_TEST_UTILS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_SELECT_AD_REACTOR_TEST_UTILS_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/test/utils/ohttp_utils.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/seller_frontend_service.h"
#include "services/seller_frontend_service/test/app_test_utils.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/cpp/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kAuctionHost[] = "auction-server.com";
constexpr char kSellerOriginDomain[] = "seller.com";
constexpr double kAdCost = 1.0;
constexpr int kModelingSignals = 0;
constexpr int kDefaultNumAdComponents = 3;
constexpr absl::string_view kSampleInterestGroupName = "interest_group";
constexpr absl::string_view kEmptyBuyer = "";
constexpr absl::string_view kSampleBuyer = "https://ad_tech_A.com";
constexpr absl::string_view kSampleBuyer2 = "https://ad_tech_B.com";
constexpr absl::string_view kSampleBuyer3 = "https://ad_tech_C.com";
constexpr absl::string_view kSampleGenerationId = "a-standard-uuid";
constexpr absl::string_view kSampleSellerDebugId = "sample-seller-debug-id";
constexpr absl::string_view kSampleBuyerDebugId = "sample-buyer-debug-id";
constexpr absl::string_view kSampleBuyerSignals = "[]";
constexpr float kNonZeroBidValue = 1.0;
constexpr float kZeroBidValue = 0.0;
constexpr int kNumAdComponentRenderUrl = 1;
constexpr int kNonZeroDesirability = 1;
constexpr bool kIsConsentedDebug = true;
constexpr absl::string_view kConsentedDebugToken = "test";
inline constexpr char kTestEvent[] = "click";
inline constexpr char kTestInteractionUrl[] = "http://click.com";
inline constexpr char kTestTopLevelSellerReportingUrl[] =
    "http://reportResult.com";
inline constexpr char kTestBuyerReportingUrl[] = "http://reportWin.com";

template <typename T>
struct EncryptedSelectAdRequestWithContext {
  // Clear text protected auction input.
  T protected_auction_input;
  // Request containing the ciphertext blob of protected audience input.
  SelectAdRequest select_ad_request;
  // OHTTP request context used to encrypt the plain text protected audience
  // input. (Useful for decoding the response).
  quiche::ObliviousHttpRequest::Context context;
};

absl::flat_hash_map<std::string, std::string> BuildBuyerWinningAdUrlMap(
    const SelectAdRequest& request);

void SetupBuyerClientMock(
    absl::string_view hostname,
    const BuyerFrontEndAsyncClientFactoryMock& buyer_clients,
    const std::optional<GetBidsResponse::GetBidsRawResponse>& bid,
    bool repeated_get_allowed = false);

void BuildAdWithBidFromAdWithBidMetadata(
    const ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata& input,
    AdWithBid* result);

AdWithBid BuildNewAdWithBid(
    const std::string& ad_url,
    absl::optional<absl::string_view> interest_group = absl::nullopt,
    absl::optional<float> bid_value = absl::nullopt,
    const bool enable_event_level_debug_reporting = false,
    int number_ad_component_render_urls = kDefaultNumAdComponents);

void SetupScoringProviderMock(
    const MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>& provider,
    const BuyerBidsResponseMap& expected_buyer_bids,
    const std::optional<std::string>& ad_render_urls,
    bool repeated_get_allowed = false);

TrustedServersConfigClient CreateConfig();

template <class T>
SelectAdResponse RunRequest(const TrustedServersConfigClient& config_client,
                            const ClientRegistry& clients,
                            const SelectAdRequest& request) {
  grpc::CallbackServerContext context;
  SelectAdResponse response;
  T reactor(&context, &request, &response, clients, config_client);
  reactor.Execute();
  return response;
}

server_common::PrivateKey GetPrivateKey();

template <typename T>
BuyerBidsResponseMap GetBuyerClientsAndBidsForReactor(
    const SelectAdRequest& request, const T& protected_auction_input,
    const BuyerFrontEndAsyncClientFactoryMock& buyer_clients) {
  BuyerBidsResponseMap buyer_bids;
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request);

  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    const std::string& url = buyer_to_ad_url.at(local_buyer);
    AdWithBid bid =
        BuildNewAdWithBid(url, kSampleInterestGroupName, kNonZeroBidValue,
                          kNumAdComponentRenderUrl);
    GetBidsResponse::GetBidsRawResponse response;
    auto mutable_bids = response.mutable_bids();
    mutable_bids->Add(std::move(bid));

    SetupBuyerClientMock(local_buyer, buyer_clients, response,
                         /*repeated_get_allowed=*/true);
    buyer_bids.try_emplace(
        local_buyer,
        std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  return buyer_bids;
}

std::pair<std::string, quiche::ObliviousHttpRequest::Context>
GetFramedInputAndOhttpContext(absl::string_view encoded_request);

template <typename T>
std::pair<std::string, quiche::ObliviousHttpRequest::Context>
GetCborEncodedEncryptedInputAndOhttpContext(const T& protected_auction_input) {
  absl::StatusOr<std::string> encoded_request =
      CborEncodeProtectedAuctionProto(protected_auction_input);
  EXPECT_TRUE(encoded_request.ok()) << encoded_request.status();
  return GetFramedInputAndOhttpContext(*encoded_request);
}

// Gets the encoded and encrypted request as well as the OHTTP context used
// for encrypting the request.
template <typename T>
std::pair<std::string, quiche::ObliviousHttpRequest::Context>
GetProtoEncodedEncryptedInputAndOhttpContext(const T& protected_auction_input) {
  return GetFramedInputAndOhttpContext(
      protected_auction_input.SerializeAsString());
}

template <typename T>
EncryptedSelectAdRequestWithContext<T> GetSampleSelectAdRequest(
    SelectAdRequest::ClientType client_type,
    absl::string_view seller_origin_domain, bool is_consented_debug = false) {
  BuyerInput buyer_input;
  auto* interest_group = buyer_input.mutable_interest_groups()->Add();
  interest_group->set_name(kSampleInterestGroupName);
  *interest_group->mutable_bidding_signals_keys()->Add() = "[]";

  google::protobuf::Map<std::string, BuyerInput> decoded_buyer_inputs;
  decoded_buyer_inputs.emplace(kSampleBuyer, buyer_input);
  google::protobuf::Map<std::string, std::string> encoded_buyer_inputs;
  switch (client_type) {
    case SelectAdRequest::BROWSER:
      encoded_buyer_inputs = *GetEncodedBuyerInputMap(decoded_buyer_inputs);
      break;
    case SelectAdRequest::ANDROID:
      encoded_buyer_inputs = GetProtoEncodedBuyerInputs(decoded_buyer_inputs);
      break;
    default:
      EXPECT_TRUE(false)
          << "Test configuration error, unsupported client type: "
          << client_type;
      break;
  }

  SelectAdRequest request;
  T protected_auction_input;
  protected_auction_input.set_generation_id(kSampleGenerationId);
  if (is_consented_debug) {
    auto* consented_debug_config =
        protected_auction_input.mutable_consented_debug_config();
    consented_debug_config->set_is_consented(kIsConsentedDebug);
    consented_debug_config->set_token(kConsentedDebugToken);
  }

  *protected_auction_input.mutable_buyer_input() =
      std::move(encoded_buyer_inputs);
  request.mutable_auction_config()->set_seller_signals(
      absl::StrCat("{\"seller_signal\": \"", MakeARandomString(), "\"}"));
  request.mutable_auction_config()->set_auction_signals(
      absl::StrCat("{\"auction_signal\": \"", MakeARandomString(), "\"}"));
  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    *request.mutable_auction_config()->mutable_buyer_list()->Add() =
        local_buyer;
  }
  protected_auction_input.set_publisher_name(MakeARandomString());
  request.mutable_auction_config()->set_seller(seller_origin_domain);
  request.set_client_type(client_type);

  const auto* descriptor = protected_auction_input.GetDescriptor();
  const bool is_protected_auction_input =
      descriptor->name() == kProtectedAuctionInput;

  switch (client_type) {
    case SelectAdRequest::ANDROID: {
      auto [encrypted_request, context] =
          GetProtoEncodedEncryptedInputAndOhttpContext(protected_auction_input);
      if (is_protected_auction_input) {
        *request.mutable_protected_auction_ciphertext() =
            std::move(encrypted_request);
      } else {
        *request.mutable_protected_audience_ciphertext() =
            std::move(encrypted_request);
      }
      return {std::move(protected_auction_input), std::move(request),
              std::move(context)};
    }
    case SelectAdRequest::BROWSER: {
      auto [encrypted_request, context] =
          GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
      if (is_protected_auction_input) {
        *request.mutable_protected_auction_ciphertext() =
            std::move(encrypted_request);
      } else {
        *request.mutable_protected_audience_ciphertext() =
            std::move(encrypted_request);
      }
      return {std::move(protected_auction_input), std::move(request),
              std::move(context)};
    }
    default:
      break;
  }
}

template <typename T>
std::pair<EncryptedSelectAdRequestWithContext<T>, ClientRegistry>
GetSelectAdRequestAndClientRegistryForTest(
    SelectAdRequest::ClientType client_type, std::optional<float> buyer_bid,
    const MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>&
        scoring_signals_provider,
    const ScoringAsyncClientMock& scoring_client,
    const BuyerFrontEndAsyncClientFactoryMock&
        buyer_front_end_async_client_factory_mock,
    server_common::MockKeyFetcherManager* mock_key_fetcher_manager,
    BuyerBidsResponseMap& expected_buyer_bids,
    absl::string_view seller_origin_domain) {
  auto encrypted_request_with_context =
      GetSampleSelectAdRequest<T>(client_type, seller_origin_domain);

  // Sets up buyer client while populating the expected buyer bids that can then
  // be used to setup the scoring signals provider.
  expected_buyer_bids = GetBuyerClientsAndBidsForReactor(
      encrypted_request_with_context.select_ad_request,
      encrypted_request_with_context.protected_auction_input,
      buyer_front_end_async_client_factory_mock);

  // Scoring signals provider
  std::string ad_render_urls = "test scoring signals";
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls, /*repeated_get_allowed=*/true);

  float bid_value = kNonZeroBidValue;
  if (buyer_bid.has_value()) {
    bid_value = *buyer_bid;
  }
  using ScoreAdsDoneCallback =
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                  ScoreAdsResponse::ScoreAdsRawResponse>>) &&>;
  // Sets up scoring Client
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillRepeatedly(
          [bid_value, client_type](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              const RequestMetadata& metadata, ScoreAdsDoneCallback on_done,
              absl::Duration timeout) {
            for (const auto& bid : request->ad_bids()) {
              auto response =
                  std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>();
              ScoreAdsResponse::AdScore* score = response->mutable_ad_score();
              EXPECT_FALSE(bid.render().empty());
              score->set_render(bid.render());
              score->mutable_component_renders()->CopyFrom(bid.ad_components());
              EXPECT_EQ(bid.ad_components_size(), kDefaultNumAdComponents);
              score->set_desirability(kNonZeroDesirability);
              score->set_buyer_bid(bid_value);
              score->set_interest_group_name(bid.interest_group_name());
              score->set_interest_group_owner(kSampleBuyer);
              score->mutable_win_reporting_urls()
                  ->mutable_top_level_seller_reporting_urls()
                  ->set_reporting_url(kTestTopLevelSellerReportingUrl);
              score->mutable_win_reporting_urls()
                  ->mutable_top_level_seller_reporting_urls()
                  ->mutable_interaction_reporting_urls()
                  ->try_emplace(kTestEvent, kTestInteractionUrl);
              score->mutable_win_reporting_urls()
                  ->mutable_buyer_reporting_urls()
                  ->set_reporting_url(kTestBuyerReportingUrl);
              score->mutable_win_reporting_urls()
                  ->mutable_buyer_reporting_urls()
                  ->mutable_interaction_reporting_urls()
                  ->try_emplace(kTestEvent, kTestInteractionUrl);
              if (client_type == SelectAdRequest::ANDROID) {
                score->set_ad_type(AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);
              }
              std::move(on_done)(std::move(response));
              // Expect only one bid.
              break;
            }
            return absl::OkStatus();
          });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Sets up client registry
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_front_end_async_client_factory_mock,
                         *mock_key_fetcher_manager, std::move(async_reporter)};

  return {std::move(encrypted_request_with_context), std::move(clients)};
}

template <typename T>
SelectAdResponse RunReactorRequest(
    const TrustedServersConfigClient& config_client,
    const ClientRegistry& clients, const SelectAdRequest& request,
    bool fail_fast = false) {
  metric::SfeContextMap()->Get(&request);
  grpc::CallbackServerContext context;
  SelectAdResponse response;
  T reactor(&context, &request, &response, clients, config_client, fail_fast);
  reactor.Execute();
  return response;
}

AuctionResult DecryptAppProtoAuctionResult(
    absl::string_view auction_result_ciphertext,
    quiche::ObliviousHttpRequest::Context& context);

AuctionResult DecryptBrowserAuctionResult(
    absl::string_view auction_result_ciphertext,
    quiche::ObliviousHttpRequest::Context& context);

absl::StatusOr<std::string> UnframeAndDecompressAuctionResult(
    absl::string_view framed_response);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_SELECT_AD_REACTOR_TEST_UTILS_H_
