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

#include "services/seller_frontend_service/select_ad_reactor.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/numeric/bits.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "glog/logging.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"
#include "services/common/constants/user_error_strings.h"
#include "services/common/reporters/async_reporter.h"
#include "services/common/util/consented_debugging_logger.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/cpp/communication/ohttp_utils.h"
#include "src/cpp/encryption/key_fetcher/src/key_fetcher_manager.h"
#include "src/cpp/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ScoreAdsRawRequest = ScoreAdsRequest::ScoreAdsRawRequest;
using AdScore = ScoreAdsResponse::AdScore;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using BiddingGroupMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using ErrorVisibility::CLIENT_VISIBLE;

}  // namespace

SelectAdReactor::SelectAdReactor(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client, bool fail_fast)
    : context_(context),
      request_(request),
      response_(response),
      clients_(clients),
      config_client_(config_client),
      // TODO(b/278039901): Add integration test for metadata forwarding.
      buyer_metadata_(GrpcMetadataToRequestMetadata(context->client_metadata(),
                                                    kBuyerMetadataKeysMap)),
      error_accumulator_(&logger_),
      fail_fast_(fail_fast),
      bid_stats_(request->auction_config().buyer_list_size(), logger_,
                 [this](bool successful) { OnAllBidsDone(successful); }),
      is_protected_auction_request_(false),
      is_pas_enabled_(
          config_client_.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS)) {
  if (config_client_.GetBooleanParameter(ENABLE_SELLER_FRONTEND_BENCHMARKING)) {
    benchmarking_logger_ =
        std::make_unique<BuildInputProcessResponseBenchmarkingLogger>(
            FormatTime(absl::Now()));
  } else {
    benchmarking_logger_ = std::make_unique<NoOpsLogger>();
  }
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::SfeContextMap()->Remove(request_));
    return absl::OkStatus();
  }()) << "SfeContextMap()->Get(request) should have been called";
}

AdWithBidMetadata SelectAdReactor::BuildAdWithBidMetadata(
    const AdWithBid& input, absl::string_view interest_group_owner) {
  AdWithBidMetadata result;
  if (input.has_ad()) {
    *result.mutable_ad() = input.ad();
  }
  result.set_bid(input.bid());
  result.set_render(input.render());
  result.set_allow_component_auction(input.allow_component_auction());
  result.mutable_ad_components()->CopyFrom(input.ad_components());
  result.set_interest_group_name(input.interest_group_name());
  result.set_interest_group_owner(interest_group_owner);
  result.set_ad_cost(input.ad_cost());
  result.set_modeling_signals(input.modeling_signals());
  const BuyerInput& buyer_input =
      buyer_inputs_->find(interest_group_owner)->second;
  for (const auto& interest_group : buyer_input.interest_groups()) {
    if (std::strcmp(interest_group.name().c_str(),
                    result.interest_group_name().c_str())) {
      if (request_->client_type() == SelectAdRequest::BROWSER) {
        result.set_join_count(interest_group.browser_signals().join_count());
        result.set_recency(interest_group.browser_signals().recency());
      }
      break;
    }
  }
  return result;
}

bool SelectAdReactor::HaveClientVisibleErrors() {
  return !error_accumulator_.GetErrors(ErrorVisibility::CLIENT_VISIBLE).empty();
}

bool SelectAdReactor::HaveAdServerVisibleErrors() {
  return !error_accumulator_.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE)
              .empty();
}

void SelectAdReactor::MayPopulateClientVisibleErrors() {
  if (!HaveClientVisibleErrors()) {
    return;
  }

  error_.set_code(static_cast<int>(ErrorCode::CLIENT_SIDE));
  error_.set_message(
      GetAccumulatedErrorString(ErrorVisibility::CLIENT_VISIBLE));
}

bool SelectAdReactor::DecryptRequest() {
  if (request_->protected_auction_ciphertext().empty() &&
      request_->protected_audience_ciphertext().empty()) {
    Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        kEmptyProtectedAuctionCiphertextError));
    return false;
  }
  is_protected_auction_request_ =
      !request_->protected_auction_ciphertext().empty();

  absl::string_view encapsulated_req;
  if (is_protected_auction_request_) {
    encapsulated_req = request_->protected_auction_ciphertext();
  } else {
    encapsulated_req = request_->protected_audience_ciphertext();
  }
  if (VLOG_IS_ON(5)) {
    logger_.vlog(5, "Protected ",
                 is_protected_auction_request_ ? "auction" : "audience",
                 " ciphertext: ", absl::Base64Escape(encapsulated_req));
  }

  // Parse the encapsulated request for the key ID.
  absl::StatusOr<uint8_t> key_id = server_common::ParseKeyId(encapsulated_req);
  if (!key_id.ok()) {
    logger_.vlog(2, "Parsed key id error status: ", key_id.status().message());
    Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        kInvalidOhttpKeyIdError));
    return false;
  }
  logger_.vlog(5, "Key Id parsed correctly");

  std::string str_key_id = std::to_string(*key_id);
  std::optional<server_common::PrivateKey> private_key =
      clients_.key_fetcher_manager_.GetPrivateKey(str_key_id);

  if (!private_key.has_value()) {
    logger_.vlog(2, "Unable to retrieve private key for key ID: ", str_key_id);
    Finish(
        grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, kMissingPrivateKey));
    return false;
  }

  logger_.vlog(3, "Private Key Id: ", private_key->key_id,
               ", Key Hex: ", absl::BytesToHexString(private_key->private_key));
  // Decrypt the ciphertext.
  absl::StatusOr<quiche::ObliviousHttpRequest> ohttp_request =
      server_common::DecryptEncapsulatedRequest(*private_key, encapsulated_req);
  if (!ohttp_request.ok()) {
    logger_.vlog(2, "Unable to decrypt the ciphertext. Reason: ",
                 ohttp_request.status().message());
    Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        absl::StrFormat(kMalformedEncapsulatedRequest,
                                        ohttp_request.status().message())));
    return false;
  }

  logger_.vlog(5, "Successfully decrypted the protected ",
               is_protected_auction_request_ ? "auction" : "audience",
               " input ciphertext");
  quiche::ObliviousHttpRequest::Context ohttp_context =
      std::move(ohttp_request.value()).ReleaseContext();
  RequestContext request = {
      str_key_id,
      std::make_unique<quiche::ObliviousHttpRequest::Context>(
          std::move(ohttp_context)),
      *private_key};
  request_context_ = std::move(request);
  if (is_protected_auction_request_) {
    protected_auction_input_ =
        GetDecodedProtectedAuctionInput(ohttp_request->GetPlaintextData());
  } else {
    protected_auction_input_ =
        GetDecodedProtectedAudienceInput(ohttp_request->GetPlaintextData());
  }
  std::visit(
      [this](const auto& protected_auction_input) {
        buyer_inputs_ =
            GetDecodedBuyerinputs(protected_auction_input.buyer_input());
      },
      protected_auction_input_);
  return true;
}

ContextLogger::ContextMap SelectAdReactor::GetLoggingContext() {
  ContextLogger::ContextMap context_map;
  std::visit(
      [&context_map, this](const auto& protected_auction_input) {
        context_map = {
            {kGenerationId, protected_auction_input.generation_id()},
            {kSellerDebugId, request_->auction_config().seller_debug_id()}};
        if (protected_auction_input.has_consented_debug_config() &&
            protected_auction_input.consented_debug_config().is_consented()) {
          absl::string_view token =
              protected_auction_input.consented_debug_config().token();
          if (!token.empty()) {
            context_map[kToken] = std::string(token);
          }
        }
      },
      protected_auction_input_);
  return context_map;
}

void SelectAdReactor::MayPopulateAdServerVisibleErrors() {
  if (request_->auction_config().seller_signals().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptySellerSignals,
                ErrorCode::CLIENT_SIDE);
  }

  if (request_->auction_config().auction_signals().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptyAuctionSignals,
                ErrorCode::CLIENT_SIDE);
  }

  if (request_->auction_config().buyer_list().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptyBuyerList,
                ErrorCode::CLIENT_SIDE);
  }

  if (request_->auction_config().seller().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptySeller,
                ErrorCode::CLIENT_SIDE);
  }

  if (config_client_.GetStringParameter(SELLER_ORIGIN_DOMAIN) !=
      request_->auction_config().seller()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kWrongSellerDomain,
                ErrorCode::CLIENT_SIDE);
  }

  for (const auto& [buyer, per_buyer_config] :
       request_->auction_config().per_buyer_config()) {
    if (buyer.empty()) {
      ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                  kEmptyBuyerInPerBuyerConfig, ErrorCode::CLIENT_SIDE);
    }
    if (per_buyer_config.buyer_signals().empty()) {
      ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                  absl::StrFormat(kEmptyBuyerSignals, buyer),
                  ErrorCode::CLIENT_SIDE);
    }
  }

  if (request_->client_type() == SelectAdRequest::UNKNOWN) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kUnknownClientType,
                ErrorCode::CLIENT_SIDE);
  }
}

void SelectAdReactor::MayLogBuyerInput() {
  if (!buyer_inputs_.ok()) {
    logger_.vlog(1, "Failed to decode buyer inputs");
  } else if (VLOG_IS_ON(6)) {
    for (const auto& [buyer, buyer_input] : *buyer_inputs_) {
      logger_.vlog(6, "Decoded buyer input for buyer: ", buyer,
                   " is: ", buyer_input.DebugString());
    }
  }
}

void SelectAdReactor::Execute() {
  if (!config_client_.GetBooleanParameter(ENABLE_ENCRYPTION)) {
    LOG(DFATAL) << "Expected encryption to be enabled";
  }
  if (!DecryptRequest()) {
    VLOG(1) << "SelectAdRequest decryption failed";
    return;
  }
  logger_.Configure(GetLoggingContext());
  MayLogBuyerInput();
  MayPopulateAdServerVisibleErrors();
  if (HaveAdServerVisibleErrors()) {
    logger_.vlog(1, "AdServerVisible errors found, failing now");
    // Finish the GRPC request if we have received bad data from the ad tech
    // server.
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }

  // Validate mandatory fields if decoding went through fine.
  if (!HaveClientVisibleErrors()) {
    logger_.vlog(5, "No ClientVisible errors found, validating input now");
    std::visit(
        [this](const auto& protected_auction_input) {
          ValidateProtectedAuctionInput(protected_auction_input);
        },
        protected_auction_input_);
  }

  // Populate errors on the response immediately after decoding and input
  // validation so that when we stop processing the request due to errors, we
  // have correct errors set in the response.
  MayPopulateClientVisibleErrors();

  if (error_accumulator_.HasErrors()) {
    logger_.vlog(1, "Some errors found, failing now");
    // Finish the GRPC request now.
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }
  logger_.vlog(1, "No client / Adtech server errors found");

  auto span = server_common::GetTracer()->StartSpan("SelectAdReactor_Execute");
  auto scope = opentelemetry::trace::Scope(span);
  // Logger for consented debugging.
  // TODO(b/279955398): Move the object to a member variable.
  if (config_client_.GetBooleanParameter(ENABLE_OTEL_BASED_LOGGING)) {
    if (absl::string_view token =
            config_client_.GetStringParameter(CONSENTED_DEBUG_TOKEN);
        !token.empty()) {
      ConsentedDebuggingLogger debug_logger(GetLoggingContext(), token);
      std::visit(
          [&debug_logger](const auto& protected_auction_input) {
            debug_logger.vlog(
                0, absl::StrCat("ProtectedAudienceInput: ",
                                protected_auction_input.DebugString()));
          },
          protected_auction_input_);
      if (!buyer_inputs_.ok()) {
        debug_logger.vlog(
            0, absl::StrCat("Failed to decode buyer inputs. Reason: ",
                            buyer_inputs_.status().ToString()));
      } else if (buyer_inputs_->empty()) {
        debug_logger.vlog(0, "buyer inputs are missing.");
      } else {
        for (const auto& [buyer, buyer_input] : *buyer_inputs_) {
          debug_logger.vlog(0, absl::StrCat("buyer_input[", buyer,
                                            "]: ", buyer_input.DebugString()));
        }
      }
    }
  }

  benchmarking_logger_->Begin();

  logger_.vlog(
      6, "Buyer list size: ", request_->auction_config().buyer_list().size());
  for (const std::string& buyer_ig_owner :
       request_->auction_config().buyer_list()) {
    const auto& buyer_input_iterator = buyer_inputs_->find(buyer_ig_owner);
    if (buyer_input_iterator != buyer_inputs_->end()) {
      FetchBid(buyer_ig_owner, buyer_input_iterator->second,
               request_->auction_config().seller());
    } else {
      logger_.vlog(2, "No buyer input found for buyer: ", buyer_ig_owner);

      // Pending bids count is set on reactor construction to buyer_list_size().
      // If no BuyerInput is found for a buyer in buyer_list, must decrement
      // pending bids count.
      bid_stats_.BidCompleted(CompletedBidState::SKIPPED);
    }
  }
  logger_.vlog(5, "Finishing execute call, response may be available later");
}

std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
SelectAdReactor::CreateGetBidsRequest(absl::string_view seller,
                                      const std::string& buyer_ig_owner,
                                      const BuyerInput& buyer_input) {
  auto get_bids_request = std::make_unique<GetBidsRequest::GetBidsRawRequest>();
  get_bids_request->set_is_chaff(false);
  get_bids_request->set_seller(seller);
  get_bids_request->set_auction_signals(
      request_->auction_config().auction_signals());
  std::string buyer_debug_id;
  const auto& per_buyer_config_itr =
      request_->auction_config().per_buyer_config().find(buyer_ig_owner);
  if (per_buyer_config_itr !=
      request_->auction_config().per_buyer_config().end()) {
    buyer_debug_id = per_buyer_config_itr->second.buyer_debug_id();
    if (!per_buyer_config_itr->second.buyer_signals().empty()) {
      get_bids_request->set_buyer_signals(
          per_buyer_config_itr->second.buyer_signals());
    }
  }
  *get_bids_request->mutable_buyer_input() = buyer_input;
  std::visit(
      [&get_bids_request,
       &buyer_debug_id](const auto& protected_auction_input) {
        get_bids_request->set_publisher_name(
            protected_auction_input.publisher_name());
        get_bids_request->set_enable_debug_reporting(
            protected_auction_input.enable_debug_reporting());
        auto* log_context = get_bids_request->mutable_log_context();
        log_context->set_generation_id(protected_auction_input.generation_id());
        log_context->set_adtech_debug_id(buyer_debug_id);
        if (protected_auction_input.has_consented_debug_config()) {
          *get_bids_request->mutable_consented_debug_config() =
              protected_auction_input.consented_debug_config();
        }
      },
      protected_auction_input_);
  return get_bids_request;
}

void SelectAdReactor::FetchBid(const std::string& buyer_ig_owner,
                               const BuyerInput& buyer_input,
                               absl::string_view seller) {
  auto scope = opentelemetry::trace::Scope(
      server_common::GetTracer()->StartSpan("FetchBid"));
  auto buyer_client = clients_.buyer_factory.Get(buyer_ig_owner);
  if (buyer_client == nullptr) {
    logger_.vlog(2, "No buyer client found for buyer: ", buyer_ig_owner);
    bid_stats_.BidCompleted(CompletedBidState::SKIPPED);
  } else {
    VLOG(6) << "Getting bid from a BFE";
    absl::Duration timeout = absl::Milliseconds(
        config_client_.GetIntParameter(GET_BID_RPC_TIMEOUT_MS));
    if (request_->auction_config().buyer_timeout_ms() > 0) {
      timeout =
          absl::Milliseconds(request_->auction_config().buyer_timeout_ms());
    }
    auto get_bids_request =
        CreateGetBidsRequest(seller, buyer_ig_owner, buyer_input);
    auto bfe_request =
        metric::MakeInitiatedRequest(metric::kBfe, metric_context_.get(), 0);
    absl::Status execute_result = buyer_client->ExecuteInternal(
        std::move(get_bids_request), buyer_metadata_,
        [buyer_ig_owner, this, bfe_request = std::move(bfe_request)](
            absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
                response) mutable {
          {  // destruct bfe_request, destructor measures request time
            auto not_used = std::move(bfe_request);
          }
          VLOG(6) << "Received a bid response from a BFE";
          OnFetchBidsDone(std::move(response), buyer_ig_owner);
        },
        timeout);
    if (!execute_result.ok()) {
      logger_.error(
          absl::StrFormat("Failed to make async GetBids call: (buyer: %s, "
                          "seller: %s, error: "
                          "%s)",
                          buyer_ig_owner, seller, execute_result.ToString()));
      bid_stats_.BidCompleted(CompletedBidState::ERROR);
    }
  }
}

void SelectAdReactor::OnFetchBidsDone(
    absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
        response,
    const std::string& buyer_ig_owner) {
  VLOG(5) << "Received response from a BFE ... ";
  if (response.ok()) {
    auto& found_response = *response;
    logger_.vlog(2, "\nGetBidsResponse:\n", found_response->DebugString());
    if (found_response->bids().empty() &&
        (!is_pas_enabled_ ||
         found_response->protected_app_signals_bids().empty())) {
      logger_.vlog(2, "Skipping buyer ", buyer_ig_owner,
                   " due to empty GetBidsResponse.");
      bid_stats_.BidCompleted(CompletedBidState::EMPTY_RESPONSE);
    } else {
      bid_stats_.BidCompleted(
          CompletedBidState::SUCCESS,
          [this, &buyer_ig_owner, response = *std::move(response)]() mutable {
            shared_buyer_bids_map_.try_emplace(buyer_ig_owner,
                                               std::move(response));
          });
    }
  } else {
    LogIfError(metric_context_->AccumulateMetric<
               server_common::metric::kInitiatedRequestErrorCount>(1));
    logger_.vlog(1, "GetBidsRequest failed for buyer ", buyer_ig_owner,
                 "\nresponse status: ", response.status());
    bid_stats_.BidCompleted(CompletedBidState::ERROR);
  }
}

void SelectAdReactor::OnAllBidsDone(bool any_successful_bids) {
  if (this->context_->IsCancelled()) {
    // Early return if request is cancelled. DO NOT move to next step.
    FinishWithAborted();
    return;
  }

  // No successful bids received.
  if (shared_buyer_bids_map_.empty()) {
    logger_.vlog(2, kNoBidsReceived);
    if (!any_successful_bids) {
      logger_.vlog(3, "Finishing the SelectAdRequest RPC with an error");
      FinishWithInternalError(kInternalError);
      return;
    }
    // Since no buyers have returned bids, we would still finish the call RPC
    // call here and send a chaff back.
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }
  FetchScoringSignals();
}

void SelectAdReactor::FetchScoringSignals() {
  ScoringSignalsRequest scoring_signals_request(shared_buyer_bids_map_,
                                                buyer_metadata_);
  auto kv_request =
      metric::MakeInitiatedRequest(metric::kKv, metric_context_.get(), 0);
  clients_.scoring_signals_async_provider.Get(
      scoring_signals_request,
      [this, kv_request = std::move(kv_request)](
          absl::StatusOr<std::unique_ptr<ScoringSignals>> result) mutable {
        {  // destruct kv_request, destructor measures request time
          auto not_used = std::move(kv_request);
        }
        OnFetchScoringSignalsDone(std::move(result));
      },
      absl::Milliseconds(config_client_.GetIntParameter(
          KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS)));
}

void SelectAdReactor::OnFetchScoringSignalsDone(
    absl::StatusOr<std::unique_ptr<ScoringSignals>> result) {
  if (result.ok()) {
    scoring_signals_ = std::move(result.value());
  } else {
    LogIfError(metric_context_->AccumulateMetric<
               server_common::metric::kInitiatedRequestErrorCount>(1));
    // TODO(b/245982466): Handle early abort and errors.
    logger_.vlog(1, "Scoring signals fetch from key-value server failed: ",
                 result.status());
  }
  ScoreAds();
}

std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
SelectAdReactor::CreateScoreAdsRequest() {
  auto raw_request = std::make_unique<ScoreAdsRequest::ScoreAdsRawRequest>();
  for (const auto& [buyer, get_bid_response] : shared_buyer_bids_map_) {
    for (int i = 0; i < get_bid_response->bids_size(); i++) {
      AdWithBidMetadata ad_with_bid_metadata =
          BuildAdWithBidMetadata(get_bid_response->bids().at(i), buyer);
      raw_request->mutable_ad_bids()->Add(std::move(ad_with_bid_metadata));
    }
  }
  *raw_request->mutable_auction_signals() =
      request_->auction_config().auction_signals();
  *raw_request->mutable_seller_signals() =
      request_->auction_config().seller_signals();
  if (scoring_signals_ != nullptr) {
    // Ad scoring signals cannot be used after this.
    raw_request->set_allocated_scoring_signals(
        scoring_signals_->scoring_signals.release());
  }
  std::visit(
      [&raw_request, this](const auto& protected_auction_input) {
        raw_request->set_publisher_hostname(
            protected_auction_input.publisher_name());
        raw_request->set_enable_debug_reporting(
            protected_auction_input.enable_debug_reporting());
        auto* log_context = raw_request->mutable_log_context();
        log_context->set_generation_id(protected_auction_input.generation_id());
        log_context->set_adtech_debug_id(
            request_->auction_config().seller_debug_id());
        if (protected_auction_input.has_consented_debug_config()) {
          *raw_request->mutable_consented_debug_config() =
              protected_auction_input.consented_debug_config();
        }
      },
      protected_auction_input_);

  for (const auto& [buyer, per_buyer_config] :
       request_->auction_config().per_buyer_config()) {
    raw_request->mutable_per_buyer_signals()->try_emplace(
        buyer, per_buyer_config.buyer_signals());
  }
  return raw_request;
}

void SelectAdReactor::ScoreAds() {
  auto raw_request = CreateScoreAdsRequest();
  logger_.vlog(2, "\nScoreAdsRawRequest:\n", raw_request->DebugString());
  auto auction_request = metric::MakeInitiatedRequest(
      metric::kAs, metric_context_.get(), raw_request->ByteSizeLong());
  auto on_scoring_done =
      [this, auction_request = std::move(auction_request)](
          absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
              result) mutable {
        {  // destruct auction_request, destructor measures request time
          auto not_used = std::move(auction_request);
        }
        OnScoreAdsDone(std::move(result));
      };
  absl::Status execute_result = clients_.scoring.ExecuteInternal(
      std::move(raw_request), {}, std::move(on_scoring_done),
      absl::Milliseconds(
          config_client_.GetIntParameter(SCORE_ADS_RPC_TIMEOUT_MS)));
  if (!execute_result.ok()) {
    logger_.error(
        absl::StrFormat("Failed to make async ScoreAds call: (error: %s)",
                        execute_result.ToString()));
    Finish(grpc::Status(grpc::INTERNAL, kInternalServerError));
  }
}

BiddingGroupMap SelectAdReactor::GetBiddingGroups() {
  BiddingGroupMap bidding_groups;
  for (const auto& [buyer, ad_with_bids] : shared_buyer_bids_map_) {
    // Mapping from buyer to interest groups that are associated with non-zero
    // bids.
    absl::flat_hash_set<absl::string_view> buyer_interest_groups;
    for (const auto& ad_with_bid : ad_with_bids->bids()) {
      if (ad_with_bid.bid() > 0) {
        buyer_interest_groups.insert(ad_with_bid.interest_group_name());
      }
    }
    const auto& buyer_input = buyer_inputs_->at(buyer);
    AuctionResult::InterestGroupIndex ar_interest_group_index;
    int ig_index = 0;
    for (const auto& interest_group : buyer_input.interest_groups()) {
      // If the interest group name is one of the groups returned by the bidding
      // service then record its index.
      if (buyer_interest_groups.contains(interest_group.name())) {
        ar_interest_group_index.add_index(ig_index);
      }
      ig_index++;
    }
    bidding_groups.try_emplace(buyer, std::move(ar_interest_group_index));
  }
  return bidding_groups;
}

void SelectAdReactor::FinishWithOkStatus() {
  benchmarking_logger_->End();
  metric_context_->SetRequestSuccessful();
  Finish(grpc::Status::OK);
}

void SelectAdReactor::FinishWithInternalError(absl::string_view error) {
  logger_.error("RPC failed: ", error);
  benchmarking_logger_->End();
  Finish(grpc::Status(grpc::INTERNAL, kInternalServerError));
}

void SelectAdReactor::FinishWithAborted() {
  logger_.error("RPC aborted: Request Cancelled by Client.");
  benchmarking_logger_->End();
  Finish(grpc::Status(grpc::ABORTED, kRequestCancelled));
}

std::string SelectAdReactor::GetAccumulatedErrorString(
    ErrorVisibility error_visibility) {
  const ErrorAccumulator::ErrorMap& error_map =
      error_accumulator_.GetErrors(error_visibility);
  auto it = error_map.find(ErrorCode::CLIENT_SIDE);
  if (it == error_map.end()) {
    return "";
  }

  return absl::StrJoin(it->second, kErrorDelimiter);
}

void SelectAdReactor::OnScoreAdsDone(
    absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
        response) {
  if (HaveAdServerVisibleErrors()) {
    logger_.vlog(
        3, "Finishing the SelectAdRequest RPC with ad server visible error");
    Finish(grpc::Status(
        grpc::StatusCode::INVALID_ARGUMENT,
        GetAccumulatedErrorString(ErrorVisibility::AD_SERVER_VISIBLE)));
    return;
  }

  logger_.vlog(2, "ScoreAdsResponse status:", response.status());
  if (!response.ok()) {
    LogIfError(metric_context_->AccumulateMetric<
               server_common::metric::kInitiatedRequestErrorCount>(1));
    benchmarking_logger_->End();
    Finish(grpc::Status(static_cast<grpc::StatusCode>(response.status().code()),
                        std::string(response.status().message())));
    return;
  }

  std::optional<AdScore> high_score;
  const auto& found_response = *response;
  if (found_response->has_ad_score() &&
      found_response->ad_score().buyer_bid() > 0) {
    high_score = found_response->ad_score();
  }
  BiddingGroupMap bidding_group_map = GetBiddingGroups();
  PerformDebugReporting(high_score);

  std::optional<AuctionResult::Error> error;
  if (HaveClientVisibleErrors()) {
    error = std::move(error_);
  }
  absl::StatusOr<std::string> non_encrypted_response = GetNonEncryptedResponse(
      high_score, std::move(bidding_group_map), std::move(error));
  if (!non_encrypted_response.ok()) {
    return;
  }

  std::string plaintext_response = std::move(*non_encrypted_response);
  if (!EncryptResponse(std::move(plaintext_response))) {
    return;
  }

  logger_.vlog(2, "\nSelectAdResponse:\n", response_->DebugString());
  FinishWithOkStatus();
}

DecodedBuyerInputs SelectAdReactor::GetDecodedBuyerinputs(
    const EncodedBuyerInputs& encoded_buyer_inputs) {
  return DecodeBuyerInputs(encoded_buyer_inputs, error_accumulator_,
                           fail_fast_);
}

bool SelectAdReactor::EncryptResponse(std::string plaintext_response) {
  std::optional<server_common::PrivateKey> private_key =
      clients_.key_fetcher_manager_.GetPrivateKey(request_context_.key_id);
  if (!private_key.has_value()) {
    logger_.vlog(
        4,
        absl::StrFormat(
            "Encryption key not found during response encryption: (key ID: %s)",
            request_context_.key_id));
    Finish(grpc::Status(grpc::StatusCode::INTERNAL, kInternalServerError));
    return false;
  }

  absl::StatusOr<std::string> encapsulated_response =
      server_common::EncryptAndEncapsulateResponse(
          std::move(plaintext_response), request_context_.private_key,
          *request_context_.context);
  if (!encapsulated_response.ok()) {
    logger_.vlog(
        4, absl::StrFormat("Error during response encryption/encapsulation: %s",
                           encapsulated_response.status().message()));
    Finish(grpc::Status(grpc::StatusCode::INTERNAL, kInternalServerError));
    return false;
  }

  response_->mutable_auction_result_ciphertext()->assign(
      std::move(*encapsulated_response));
  return true;
}

void SelectAdReactor::PerformDebugReporting(
    const std::optional<AdScore>& high_score) {
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(high_score);
  for (const auto& [buyer, get_bid_response] : shared_buyer_bids_map_) {
    std::string ig_owner = buyer;
    for (int i = 0; i < get_bid_response->bids_size(); i++) {
      AdWithBid adWithBid = get_bid_response->bids().at(i);
      std::string ig_name = adWithBid.interest_group_name();
      if (adWithBid.has_debug_report_urls()) {
        auto done_cb = [&logger_ = logger_, ig_owner,
                        ig_name](absl::StatusOr<absl::string_view> result) {
          if (result.ok()) {
            logger_.vlog(2, "Performed debug reporting for:", ig_owner,
                         ", interest_group: ", ig_name);
          } else {
            logger_.vlog(
                1, "Error while performing debug reporting for: ", ig_owner,
                ",  interest_group: ", ig_name, " ,status:", result.status());
          }
        };
        std::string debug_url;
        if (post_auction_signals.winning_ig_owner == buyer &&
            adWithBid.interest_group_name() ==
                post_auction_signals.winning_ig_name) {
          debug_url = adWithBid.debug_report_urls().auction_debug_win_url();
        } else {
          debug_url = adWithBid.debug_report_urls().auction_debug_loss_url();
        }
        HTTPRequest http_request = CreateDebugReportingHttpRequest(
            debug_url, GetPlaceholderDataForInterestGroup(
                           ig_owner, ig_name, post_auction_signals));
        clients_.reporting->DoReport(http_request, done_cb);
      }
    }
  }
}

void SelectAdReactor::OnDone() { delete this; }

void SelectAdReactor::OnCancel() {
  // TODO(b/245982466): Handle early abort and errors.
}

void SelectAdReactor::ReportError(
    ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
    const std::string& msg, ErrorCode error_code) {
  const auto& location = error_visibility_with_loc.location;
  ErrorVisibility error_visibility = error_visibility_with_loc.mandatory_param;
  error_accumulator_.ReportError(location, error_visibility, msg, error_code);
}

}  // namespace privacy_sandbox::bidding_auction_servers
