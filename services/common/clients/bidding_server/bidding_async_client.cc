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

#include "services/common/clients/bidding_server/bidding_async_client.h"

#include "cc/public/cpio/interface/crypto_client/crypto_client_interface.h"
#include "glog/logging.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::cmrt::sdk::public_key_service::v1::PublicKey;
using GenerateProtectedAppSignalsBidsRawResponse =
    GenerateProtectedAppSignalsBidsResponse::
        GenerateProtectedAppSignalsBidsRawResponse;

namespace {

template <typename Request, typename Response, typename RawResponse>
void OnRpcDone(
    const grpc::Status& status,
    RawClientParams<Request, Response, RawResponse>* params,
    std::function<absl::StatusOr<std::unique_ptr<RawResponse>>(Response*)>
        decrypt_response) {
  if (!status.ok()) {
    VLOG(1) << "SendRPC completion status not ok: " << ToAbslStatus(status);
    params->OnDone(status);
    return;
  }
  VLOG(6) << "SendRPC completion status ok";
  auto decrypted_response = decrypt_response(params->ResponseRef());
  if (!decrypted_response.ok()) {
    VLOG(1) << "BiddingAsyncGrpcClient failed to decrypt response";
    params->OnDone(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                decrypted_response.status().ToString()));
    return;
  }

  params->SetRawResponse(*std::move(decrypted_response));
  VLOG(6) << "Returning the decrypted response via callback";
  params->OnDone(status);
}

}  // namespace

BiddingAsyncGrpcClient::BiddingAsyncGrpcClient(
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const BiddingServiceClientConfig& client_config, Bidding::Stub* stub)
    : DefaultAsyncGrpcClient(key_fetcher_manager, crypto_client,
                             client_config.encryption_enabled),
      stub_(stub) {}

void BiddingAsyncGrpcClient::SendRpc(
    const std::string& hpke_secret,
    RawClientParams<GenerateBidsRequest, GenerateBidsResponse,
                    GenerateBidsResponse::GenerateBidsRawResponse>* params)
    const {
  VLOG(5) << "BiddingAsyncGrpcClient SendRpc invoked ...";
  stub_->async()->GenerateBids(
      params->ContextRef(), params->RequestRef(), params->ResponseRef(),
      [this, params, hpke_secret](grpc::Status status) {
        DCHECK(encryption_enabled_);
        OnRpcDone<GenerateBidsRequest, GenerateBidsResponse,
                  GenerateBidsResponse::GenerateBidsRawResponse>(
            status, params,
            [this, &hpke_secret](GenerateBidsResponse* response) {
              return DecryptResponse(hpke_secret, response);
            });
      });
}

ProtectedAppSignalsBiddingAsyncGrpcClient::
    ProtectedAppSignalsBiddingAsyncGrpcClient(
        server_common::KeyFetcherManagerInterface* key_fetcher_manager,
        CryptoClientWrapperInterface* crypto_client,
        const BiddingServiceClientConfig& client_config, Bidding::Stub* stub)
    : DefaultAsyncGrpcClient(key_fetcher_manager, crypto_client,
                             client_config.encryption_enabled),
      stub_(stub) {}

void ProtectedAppSignalsBiddingAsyncGrpcClient::SendRpc(
    const std::string& hpke_secret,
    RawClientParams<GenerateProtectedAppSignalsBidsRequest,
                    GenerateProtectedAppSignalsBidsResponse,
                    GenerateProtectedAppSignalsBidsRawResponse>* params) const {
  VLOG(5) << "ProtectedAppSignalsBiddingAsyncGrpcClient SendRpc invoked ...";
  stub_->async()->GenerateProtectedAppSignalsBids(
      params->ContextRef(), params->RequestRef(), params->ResponseRef(),
      [this, params, hpke_secret](grpc::Status status) {
        DCHECK(encryption_enabled_);
        OnRpcDone<GenerateProtectedAppSignalsBidsRequest,
                  GenerateProtectedAppSignalsBidsResponse,
                  GenerateProtectedAppSignalsBidsRawResponse>(
            status, params,
            [this,
             &hpke_secret](GenerateProtectedAppSignalsBidsResponse* response) {
              return DecryptResponse(hpke_secret, response);
            });
      });
}

}  // namespace privacy_sandbox::bidding_auction_servers
