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

#include "services/buyer_frontend_service/get_bids_unary_reactor.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "glog/logging.h"
#include "services/buyer_frontend_service/util/proto_factory.h"
#include "services/common/constants/user_error_strings.h"
#include "services/common/loggers/build_input_process_response_benchmarking_logger.h"
#include "services/common/loggers/no_ops_logger.h"
#include "services/common/util/consented_debugging_logger.h"
#include "services/common/util/request_metadata.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse;

bool GetBidsUnaryReactor::DecryptRequest() {
  if (request_->key_id().empty()) {
    VLOG(1) << kEmptyKeyIdError;
    Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, kEmptyKeyIdError));
    return false;
  }

  if (request_->request_ciphertext().empty()) {
    VLOG(1) << kEmptyCiphertextError;
    Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        kEmptyCiphertextError));
    return false;
  }

  std::optional<server_common::PrivateKey> private_key =
      key_fetcher_manager_->GetPrivateKey(request_->key_id());
  if (!private_key.has_value()) {
    VLOG(1) << kInvalidKeyIdError;
    Finish(
        grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, kInvalidKeyIdError));
    return false;
  }

  absl::StatusOr<HpkeDecryptResponse> decrypt_response =
      crypto_client_->HpkeDecrypt(*private_key, request_->request_ciphertext());
  if (!decrypt_response.ok()) {
    VLOG(1) << kMalformedCiphertext;
    Finish(
        grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, kMalformedCiphertext));
    return false;
  }

  hpke_secret_ = std::move(decrypt_response->secret());
  if (!raw_request_.ParseFromString(decrypt_response->payload())) {
    VLOG(1) << "Unable to parse proto from the decrypted request: "
            << kMalformedCiphertext;
    Finish(
        grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, kMalformedCiphertext));
    return false;
  }

  return true;
}

void GetBidsUnaryReactor::Execute() {
  benchmarking_logger_->Begin();
  DCHECK(config_.encryption_enabled);
  if (!DecryptRequest()) {
    VLOG(1) << "Decrypting the request failed";
    return;
  }
  VLOG(5) << "Successfully decrypted the request";

  GetProtectedAudienceBids();
}

void GetBidsUnaryReactor::GetProtectedAudienceBids() {
  // Logger for consented debugging.
  // TODO(b/279955398): Refactor ConsentedDebuggingLogger to create right next
  // to ContextLogger, and use a common helper function.
  if (config_.enable_otel_based_logging) {
    if (absl::string_view token = config_.consented_debug_token;
        !token.empty()) {
      ConsentedDebuggingLogger debug_logger(GetLoggingContext(), token);
      debug_logger.vlog(
          0, absl::StrCat("GetBidsRawRequest: ", raw_request_.DebugString()));
    }
  }

  BiddingSignalsRequest bidding_signals_request(raw_request_, kv_metadata_);
  auto kv_request =
      metric::MakeInitiatedRequest(metric::kKv, metric_context_.get(), 0);
  // Get Bidding Signals.
  bidding_signals_async_provider_->Get(
      bidding_signals_request,
      [this, kv_request = std::move(kv_request)](
          absl::StatusOr<std::unique_ptr<BiddingSignals>> response) mutable {
        {  // destruct kv_request, destructor measures request time
          auto not_used = std::move(kv_request);
        }
        if (!response.ok()) {
          LogIfError(metric_context_->AccumulateMetric<
                     server_common::metric::kInitiatedRequestErrorCount>(1));
          // Return error to client.
          logger_.vlog(1, "GetBiddingSignals request failed with status:",
                       response.status());
          Finish(grpc::Status(
              static_cast<grpc::StatusCode>(response.status().code()),
              std::string(response.status().message())));
          return;
        }
        // Final callback needs to check status of others and send bidding
        // request.
        PrepareAndGenerateProtectedAudienceBid(std::move(response.value()));
      },
      absl::Milliseconds(config_.bidding_signals_load_timeout_ms));
}

// Process Outputs from Actions to prepare bidding request.
// All Preload actions must have completed before this is invoked.
void GetBidsUnaryReactor::PrepareAndGenerateProtectedAudienceBid(
    std::unique_ptr<BiddingSignals> bidding_signals) {
  const auto& log_context = raw_request_.log_context();
  std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
      raw_bidding_input = ProtoFactory::CreateGenerateBidsRawRequest(
          raw_request_, raw_request_.buyer_input(), std::move(bidding_signals),
          log_context);

  logger_.vlog(2, "GenerateBidsRequest:\n", raw_bidding_input->DebugString());
  auto bidding_request = metric::MakeInitiatedRequest(
      metric::kBs, metric_context_.get(), raw_bidding_input->ByteSizeLong());
  absl::Status execute_result = bidding_async_client_->ExecuteInternal(
      std::move(raw_bidding_input), {},
      [this, bidding_request = std::move(bidding_request)](
          absl::StatusOr<
              std::unique_ptr<GenerateBidsResponse::GenerateBidsRawResponse>>
              raw_response) mutable {
        {  // destruct bidding_request, destructor measures request time
          auto not_used = std::move(bidding_request);
        }
        if (!raw_response.ok()) {
          LogIfError(metric_context_->AccumulateMetric<
                     server_common::metric::kInitiatedRequestErrorCount>(1));
          const std::string err_msg = absl::StrCat(
              "Execution of GenerateBids request failed with status: ",
              raw_response.status().message());
          // Return error to client.
          logger_.vlog(1, err_msg);
          benchmarking_logger_->End();
          Finish(grpc::Status(
              static_cast<grpc::StatusCode>(raw_response.status().code()),
              std::move(err_msg)));
          return;
        }
        logger_.vlog(2, "Raw response received by bidding async client:\n",
                     (*raw_response)->DebugString());

        // Parse and convert response.
        get_bids_raw_response_ =
            ProtoFactory::CreateGetBidsRawResponse(*std::move(raw_response));
        logger_.vlog(2, "GetBidsRawResponse:\n",
                     get_bids_raw_response_->DebugString());

        if (!EncryptResponse()) {
          return;
        }

        logger_.vlog(3, "GetBidsResponse:\n",
                     get_bids_response_->DebugString());
        benchmarking_logger_->End();
        FinishWithOkStatus();
      },
      absl::Milliseconds(config_.generate_bid_timeout_ms));
  if (!execute_result.ok()) {
    logger_.error(
        absl::StrFormat("Failed to make async GenerateBids call: (error: %s)",
                        execute_result.ToString()));
    Finish(grpc::Status(grpc::INTERNAL, kInternalServerError));
  }
}

bool GetBidsUnaryReactor::EncryptResponse() {
  std::string payload = get_bids_raw_response_->SerializeAsString();
  absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse>
      aead_encrypt = crypto_client_->AeadEncrypt(payload, hpke_secret_);
  if (!aead_encrypt.ok()) {
    logger_.vlog(1, "Failed to encrypt response");
    Finish(grpc::Status(grpc::StatusCode::INTERNAL,
                        aead_encrypt.status().ToString()));
    return false;
  }

  get_bids_response_->set_response_ciphertext(
      aead_encrypt->encrypted_data().ciphertext());
  return true;
}

ContextLogger::ContextMap GetBidsUnaryReactor::GetLoggingContext() {
  const auto& log_context = raw_request_.log_context();
  ContextLogger::ContextMap context_map = {
      {kGenerationId, log_context.generation_id()},
      {kBuyerDebugId, log_context.adtech_debug_id()}};
  if (raw_request_.has_consented_debug_config()) {
    MaybeAddConsentedDebugConfig(raw_request_.consented_debug_config(),
                                 context_map);
  }
  return context_map;
}

GetBidsUnaryReactor::GetBidsUnaryReactor(
    grpc::CallbackServerContext& context,
    const GetBidsRequest& get_bids_request, GetBidsResponse& get_bids_response,
    const BiddingSignalsAsyncProvider& bidding_signals_async_provider,
    const BiddingAsyncClient& bidding_async_client, const GetBidsConfig& config,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client, bool enable_benchmarking)
    : context_(&context),
      request_(&get_bids_request),
      get_bids_response_(&get_bids_response),
      // TODO(b/278039901): Add integration test for metadata forwarding.
      kv_metadata_(GrpcMetadataToRequestMetadata(context.client_metadata(),
                                                 kBuyerKVMetadata)),
      bidding_signals_async_provider_(&bidding_signals_async_provider),
      bidding_async_client_(&bidding_async_client),
      config_(config),
      key_fetcher_manager_(key_fetcher_manager),
      crypto_client_(crypto_client),
      logger_(GetLoggingContext()) {
  if (enable_benchmarking) {
    std::string request_id = FormatTime(absl::Now());
    benchmarking_logger_ =
        std::make_unique<BuildInputProcessResponseBenchmarkingLogger>(
            request_id);
  } else {
    benchmarking_logger_ = std::make_unique<NoOpsLogger>();
  }
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::BfeContextMap()->Remove(request_));
    return absl::OkStatus();
  }()) << "BfeContextMap()->Get(request) should have been called";
}

void GetBidsUnaryReactor::FinishWithOkStatus() {
  metric_context_->SetRequestSuccessful();
  Finish(grpc::Status::OK);
}

// Deletes all data related to this object.
void GetBidsUnaryReactor::OnDone() { delete this; }

}  // namespace privacy_sandbox::bidding_auction_servers
