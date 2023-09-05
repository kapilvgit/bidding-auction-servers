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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AD_REACTOR_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AD_REACTOR_H_

#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/flags/flag.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/blocking_counter.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "include/grpcpp/impl/codegen/server_callback.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"
#include "services/common/loggers/build_input_process_response_benchmarking_logger.h"
#include "services/common/loggers/no_ops_logger.h"
#include "services/common/metric/server_definition.h"
#include "services/common/util/bid_stats.h"
#include "services/common/util/context_logger.h"
#include "services/common/util/error_accumulator.h"
#include "services/common/util/error_reporter.h"
#include "services/common/util/request_metadata.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/seller_frontend_service.h"

namespace privacy_sandbox::bidding_auction_servers {
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 3>
    kBuyerMetadataKeysMap = {{{"x-accept-language", "x-accept-language"},
                              {"x-user-agent", "x-user-agent"},
                              {"x-bna-client-ip", "x-bna-client-ip"}}};

// Constants for user errors.
inline constexpr char kEmptyProtectedAuctionCiphertextError[] =
    "protected_auction_ciphertext must be non-null.";
inline constexpr char kInvalidOhttpKeyIdError[] =
    "Invalid key ID provided in OHTTP encapsulated request for "
    "protected_audience_ciphertext.";
inline constexpr char kMissingPrivateKey[] =
    "Unable to get private key for the key ID in OHTTP encapsulated request.";
inline constexpr char kUnsupportedClientType[] = "Unsupported client type.";
inline constexpr char kMalformedEncapsulatedRequest[] =
    "Malformed OHTTP encapsulated request provided for "
    "protected_audience_ciphertext: "
    "%s";

// Constants for bad Ad server provided inputs.
inline constexpr char kEmptySellerSignals[] =
    "Seller signals missing in auction config";
inline constexpr char kEmptyAuctionSignals[] =
    "Auction signals missing in auction config";
inline constexpr char kEmptyBuyerList[] = "No buyers specified";
inline constexpr char kEmptySeller[] =
    "Seller origin missing in auction config";
inline constexpr char kEmptyBuyerSignals[] =
    "Buyer signals missing in auction config for buyer: %s";
inline constexpr char kUnknownClientType[] =
    "Unknown client type in SelectAdRequest";
inline constexpr char kWrongSellerDomain[] =
    "Seller domain passed in request does not match this server's domain";
inline constexpr char kEmptyBuyerInPerBuyerConfig[] =
    "One or more buyer keys are empty in per buyer config map";

inline constexpr char kNoBidsReceived[] = "No bids received.";

// Struct for any objects needed throughout the lifecycle of the request.
struct RequestContext {
  // Key ID used to encrypt the request.
  google::scp::cpio::PublicPrivateKeyPairId key_id;

  // OHTTP context generated during request decryption.
  std::unique_ptr<quiche::ObliviousHttpRequest::Context> context;

  server_common::PrivateKey private_key;
};

// This is a gRPC reactor that serves a single GenerateBidsRequest.
// It stores state relevant to the request and after the
// response is finished being served, SelectAdReactor cleans up all
// necessary state and grpc releases the reactor from memory.
class SelectAdReactor : public grpc::ServerUnaryReactor {
 public:
  explicit SelectAdReactor(grpc::CallbackServerContext* context,
                           const SelectAdRequest* request,
                           SelectAdResponse* response,
                           const ClientRegistry& clients,
                           const TrustedServersConfigClient& config_client,
                           bool fail_fast = true);

  // Initiate the asynchronous execution of the SelectingWinningAdRequest.
  virtual void Execute();

 protected:
  using ErrorHandlerSignature = const std::function<void(absl::string_view)>&;

  // Gets a string representing the response to be returned to the client. This
  // data will be encrypted before it is sent back to the client.
  virtual absl::StatusOr<std::string> GetNonEncryptedResponse(
      const std::optional<ScoreAdsResponse::AdScore>& high_score,
      const google::protobuf::Map<
          std::string, AuctionResult::InterestGroupIndex>& bidding_group_map,
      const std::optional<AuctionResult::Error>& error) = 0;

  // Decodes the plaintext payload and returns a `ProtectedAudienceInput` proto.
  // Any errors while decoding are reported to error accumulator object.
  [[deprecated]] virtual ProtectedAudienceInput
  GetDecodedProtectedAudienceInput(absl::string_view encoded_data) = 0;

  // Decodes the plaintext payload and returns a `ProtectedAuctionInput` proto.
  // Any errors while decoding are reported to error accumulator object.
  virtual ProtectedAuctionInput GetDecodedProtectedAuctionInput(
      absl::string_view encoded_data) = 0;

  // Returns the decoded BuyerInput from the encoded/compressed BuyerInput.
  // Any errors while decoding are reported to error accumulator object.
  virtual absl::flat_hash_map<absl::string_view, BuyerInput>
  GetDecodedBuyerinputs(const google::protobuf::Map<std::string, std::string>&
                            encoded_buyer_inputs) = 0;

  virtual std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
  CreateGetBidsRequest(absl::string_view seller,
                       const std::string& buyer_ig_owner,
                       const BuyerInput& buyer_input);

  virtual std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
  CreateScoreAdsRequest();

  // Checks if any client visible errors have been observed.
  bool HaveClientVisibleErrors();

  // Checks if any ad server visible errors have been observed.
  bool HaveAdServerVisibleErrors();

  // Finishes the RPC call while reporting the error.
  void FinishWithInternalError(absl::string_view error);

  // Finishes the RPC call when call is cancelled/aborted.
  void FinishWithAborted();

  // Validates the mandatory fields in the request. Reports any errors to the
  // error accumulator.
  template <typename T>
  void ValidateProtectedAuctionInput(const T& protected_auction_input) {
    if (protected_auction_input.generation_id().empty()) {
      ReportError(ErrorVisibility::CLIENT_VISIBLE, kMissingGenerationId,
                  ErrorCode::CLIENT_SIDE);
    }

    if (protected_auction_input.publisher_name().empty()) {
      ReportError(ErrorVisibility::CLIENT_VISIBLE, kMissingPublisherName,
                  ErrorCode::CLIENT_SIDE);
    }

    // Validate Buyer Inputs.
    if (buyer_inputs_->empty()) {
      ReportError(ErrorVisibility::CLIENT_VISIBLE, kMissingBuyerInputs,
                  ErrorCode::CLIENT_SIDE);
    } else {
      bool is_any_buyer_input_valid = false;
      std::set<std::string> observed_errors;
      for (const auto& [buyer, buyer_input] : *buyer_inputs_) {
        bool any_error = false;
        if (buyer.empty()) {
          observed_errors.insert(kEmptyInterestGroupOwner);
          any_error = true;
        }
        if (buyer_input.interest_groups().empty()) {
          observed_errors.insert(
              absl::StrFormat(kMissingInterestGroups, buyer));
          any_error = true;
        }
        if (any_error) {
          continue;
        }
        is_any_buyer_input_valid = true;
      }
      // Buyer inputs have keys but none of the key/value pairs are usable to
      // get bids from buyers.
      if (!is_any_buyer_input_valid) {
        std::string error =
            absl::StrFormat(kNonEmptyBuyerInputMalformed,
                            absl::StrJoin(observed_errors, kErrorDelimiter));
        ReportError(ErrorVisibility::CLIENT_VISIBLE, error,
                    ErrorCode::CLIENT_SIDE);
      } else {
        // Log but don't report the errors for malformed buyer inputs because we
        // have found at least one buyer input that is well formed.
        for (const auto& observed_error : observed_errors) {
          logger_.vlog(2, observed_error);
        }
      }
    }
  }

  // Logs the decoded buyer inputs if available.
  void MayLogBuyerInput();

  // Populates the errors that need to be sent to the client (in the encrypted
  // response).
  void MayPopulateClientVisibleErrors();

  // Populates the errors related to bad inputs from ad server.
  void MayPopulateAdServerVisibleErrors();

  // Gets a string of all errors caused by bad inputs to the SFE.
  std::string GetAccumulatedErrorString(ErrorVisibility error_visibility);

  // Decrypts the ProtectedAudienceInput in the request object and returns
  // whether decryption was successful.
  bool DecryptRequest();

  // Fetches the bids from a single buyer by initiating an asynchronous GetBids
  // rpc.
  //
  // buyer: a string representing the buyer, identified as an IG owner.
  // buyer_input: input for bidding.
  void FetchBid(const std::string& buyer_ig_owner,
                const BuyerInput& buyer_input, absl::string_view seller);
  // Handles recording the fetched bid to state.
  // This is called by the grpc buyer client when the request is finished,
  // and will subsequently call update pending bids state which will update how
  // many bids are still pending and finally fetch the scoring signals once all
  // bids are done.
  //
  // response: an error status or response from the GetBid request.
  // buyer_hostname: the hostname of the buyer
  void OnFetchBidsDone(
      absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
          response,
      const std::string& buyer_hostname);

  // Calls FetchScoringSignals or calls Finish on the reactor depending on
  // if there were any successful bids or if the request was cancelled by the
  // client.
  void OnAllBidsDone(bool any_successful_bids);

  // Initiates the asynchronous grpc request to fetch scoring signals
  // from the key value server. The ad_render_url in the GetBid response from
  // each Buyer is used as a key for the Seller Key-Value lookup.
  void FetchScoringSignals();

  // Handles recording the fetched scoring signals to state.
  // If the code blob is already fetched, this function initiates scoring the
  // auction.
  //
  // result: the status or the  GetValuesClientOutput, which contains the
  // scoring signals that will be used by the auction service.
  void OnFetchScoringSignalsDone(
      absl::StatusOr<std::unique_ptr<ScoringSignals>> result);

  // Initiates an asynchronous rpc to the auction service. This request includes
  // all signals and bids.
  void ScoreAds();

  // Handles the auction result and writes the winning ad to
  // the SelectAdResponse, thus finishing the SelectAdRequest.
  // This function is called by the auction service client as a done callback.
  //
  // status: the status of the grpc request if failed or the ScoreAdsResponse,
  // which contains the scores for each ad in the auction.
  void OnScoreAdsDone(
      absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
          status);

  // Gets the bidding groups after scoring is done.
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
  GetBiddingGroups();

  // Sends debug reporting pings to buyers for the interest groups.
  void PerformDebugReporting(
      const std::optional<ScoreAdsResponse::AdScore>& high_score);

  // Encrypts the AuctionResult and sets the ciphertext field in the response.
  // Returns whether encryption was successful.
  bool EncryptResponse(std::string plaintext_response);

  // Cleans up and deletes the SelectAdReactor. Called by the grpc library after
  // the response has finished.
  void OnDone() override;

  // Abandons the entire SelectAdRequest. Called by the grpc library if the
  // client cancels the request.
  void OnCancel() override;

  // Finishes the RPC call with an OK status.
  void FinishWithOkStatus();

  // Populates the logging context needed for request tracing. For the case when
  // encrypting is enabled, this method should be called after decrypting
  // and decoding the request.
  ContextLogger::ContextMap GetLoggingContext();

  // Reports an error to the error accumulator object.
  void ReportError(
      ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
      const std::string& msg, ErrorCode error_code);

  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata BuildAdWithBidMetadata(
      const AdWithBid& input, absl::string_view interest_group_owner);

  // Initialization
  grpc::CallbackServerContext* context_;
  const SelectAdRequest* request_;
  std::variant<ProtectedAudienceInput, ProtectedAuctionInput>
      protected_auction_input_;
  SelectAdResponse* response_;
  AuctionResult::Error error_;
  const ClientRegistry& clients_;
  const TrustedServersConfigClient& config_client_;

  // Key Value Fetch Result.
  std::unique_ptr<ScoringSignals> scoring_signals_;

  // Metadata to be sent to buyers.
  RequestMetadata buyer_metadata_;

  // Get Bid Results
  // Multiple threads can be writing buyer bid responses so this map
  // gets locked when bid_stats_ updates the state of pending bids.
  // The map can be freely used without a lock after all the bids have
  // completed.
  BuyerBidsResponseMap shared_buyer_bids_map_;

  // Benchmarking Logger to benchmark the service
  std::unique_ptr<BenchmarkingLogger> benchmarking_logger_;

  // Request context needed throughout the lifecycle of the request.
  RequestContext request_context_;
  // Logger that logs enough context around a request so that it can be traced
  // through B&A services.
  ContextLogger logger_;

  // Decompressed and decoded buyer inputs.
  absl::StatusOr<absl::flat_hash_map<absl::string_view, BuyerInput>>
      buyer_inputs_;

  // Used to log metric, same life time as reactor.
  std::unique_ptr<metric::SfeContext> metric_context_;

  // Object that accumulates all the errors and aggregates them based on their
  // intended visibility.
  ErrorAccumulator error_accumulator_;
  // Bool indicating whether or not the reactor should bail out on first input
  // validation failure. Setting this to false can help reduce the debugging
  // time on the client side since clients will get a holistic report of what is
  // wrong with each input field.
  const bool fail_fast_;

  // Indicates whether the request is using newer request field.
  // This can be removed once all the clients start using this new field.
  bool is_protected_auction_request_;

  // Indicates whether or not the protected app signals feature is enabled or
  // not.
  const bool is_pas_enabled_;

 private:
  // Keeps track of how many buyer bids were expected initially and how many
  // were erroneous. If all bids ended up in an error state then that should be
  // flagged as an error eventually.
  BidStats bid_stats_;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_SELECT_WINNING_AD_REACTOR_H_
