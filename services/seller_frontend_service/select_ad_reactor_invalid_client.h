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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AD_REACTOR_INVALID_CLIENT_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AD_REACTOR_INVALID_CLIENT_H_

#include <string>

#include <grpcpp/grpcpp.h>

#include "absl/status/statusor.h"
#include "include/grpcpp/impl/codegen/server_callback.h"
#include "services/seller_frontend_service/select_ad_reactor.h"

namespace privacy_sandbox::bidding_auction_servers {

// A reactor to finish the client's RPC call with an error when an invalid
// client type is specified in the request.
class SelectAdReactorInvalidClient : public SelectAdReactor {
 public:
  explicit SelectAdReactorInvalidClient(
      grpc::CallbackServerContext* context, const SelectAdRequest* request,
      SelectAdResponse* response, const ClientRegistry& clients,
      const TrustedServersConfigClient& config_client);
  virtual ~SelectAdReactorInvalidClient() = default;

  // SelectAdReactorInvalidClient is neither copyable nor movable.
  SelectAdReactorInvalidClient(const SelectAdReactorInvalidClient&) = delete;
  SelectAdReactorInvalidClient& operator=(const SelectAdReactorInvalidClient&) =
      delete;

  void Execute() override;

 private:
  absl::StatusOr<std::string> GetNonEncryptedResponse(
      const std::optional<ScoreAdsResponse::AdScore>& high_score,
      const google::protobuf::Map<
          std::string, AuctionResult::InterestGroupIndex>& bidding_group_map,
      const std::optional<AuctionResult::Error>& error) override;

  [[deprecated]] ProtectedAudienceInput GetDecodedProtectedAudienceInput(
      absl::string_view encoded_data) override;

  ProtectedAuctionInput GetDecodedProtectedAuctionInput(
      absl::string_view encoded_data) override;

  absl::flat_hash_map<absl::string_view, BuyerInput> GetDecodedBuyerinputs(
      const google::protobuf::Map<std::string, std::string>&
          encoded_buyer_inputs) override;

  SelectAdRequest::ClientType client_type_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AD_REACTOR_INVALID_CLIENT_H_
