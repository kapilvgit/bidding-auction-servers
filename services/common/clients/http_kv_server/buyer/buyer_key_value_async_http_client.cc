//   Copyright 2022 Google LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#include "services/common/clients/http_kv_server/buyer/buyer_key_value_async_http_client.h"

#include "glog/logging.h"
#include "services/common/clients/http_kv_server/util/generate_url.h"
#include "services/common/util/request_metadata.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr auto kEnableEncodeParams = true;

// Builds Buyer KV Value lookup Request.
HTTPRequest BuildBuyerKeyValueRequest(
    absl::string_view kv_server_host_domain, const RequestMetadata& metadata,
    std::unique_ptr<GetBuyerValuesInput> client_input) {
  HTTPRequest request;
  ClearAndMakeStartOfUrl(kv_server_host_domain, &request.url);
  if (!(client_input->hostname.empty())) {
    AddAmpersandIfNotFirstQueryParam(&request.url);
    absl::StrAppend(&request.url, "hostname=", client_input->hostname);
  }
  if (!client_input->keys.empty()) {
    AddListItemsAsQueryParamsToUrl(&request.url, "keys", client_input->keys,
                                   kEnableEncodeParams);
  }

  request.headers = RequestMetadataToHttpHeaders(metadata, kMandatoryHeaders);
  return request;
}
}  // namespace

absl::Status BuyerKeyValueAsyncHttpClient::Execute(
    std::unique_ptr<GetBuyerValuesInput> keys, const RequestMetadata& metadata,
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
        on_done,
    absl::Duration timeout) const {
  HTTPRequest request = BuildBuyerKeyValueRequest(kv_server_base_address_,
                                                  metadata, std::move(keys));
  VLOG(2) << "BuyerKeyValueAsyncHttpClient Request: " << request.url;
  auto done_callback = [on_done = std::move(on_done)](
                           absl::StatusOr<std::string> resultStr) mutable {
    if (resultStr.ok()) {
      VLOG(2) << "\n\nBuyerKeyValueAsyncHttpClient Success Response:\n"
              << resultStr.value() << "\n";
      std::unique_ptr<GetBuyerValuesOutput> resultUPtr =
          std::make_unique<GetBuyerValuesOutput>(
              GetBuyerValuesOutput({std::move(resultStr.value())}));
      std::move(on_done)(std::move(resultUPtr));
    } else {
      VLOG(2) << "BuyerKeyValueAsyncHttpClient Failure Response: "
              << resultStr.status();
      std::move(on_done)(resultStr.status());
    }
  };
  VLOG(2) << "\n\nBTS Request Url:\n" << request.url << "\nHeaders:\n";
  for (const auto& header : request.headers) {
    VLOG(2) << header;
  }
  http_fetcher_async_->FetchUrl(
      request, static_cast<int>(absl::ToInt64Milliseconds(timeout)),
      std::move(done_callback));
  return absl::OkStatus();
}

BuyerKeyValueAsyncHttpClient::BuyerKeyValueAsyncHttpClient(
    absl::string_view kv_server_base_address,
    std::unique_ptr<HttpFetcherAsync> http_fetcher_async, bool pre_warm)
    : http_fetcher_async_(std::move(http_fetcher_async)),
      kv_server_base_address_(kv_server_base_address) {
  if (pre_warm) {
    auto request = std::make_unique<GetBuyerValuesInput>();
    Execute(
        std::move(request), {},
        [](absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>
               buyer_kv_output) mutable {
          if (!buyer_kv_output.ok()) {
            VLOG(1) << "BuyerKeyValueAsyncHttpClient pre-warm returned status:"
                    << buyer_kv_output.status().message();
          }
        },
        // Longer timeout for first request
        absl::Milliseconds(60000));
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
