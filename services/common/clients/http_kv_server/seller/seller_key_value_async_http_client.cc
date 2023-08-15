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

#include "services/common/clients/http_kv_server/seller/seller_key_value_async_http_client.h"

#include "glog/logging.h"
#include "services/common/clients/http_kv_server/util/generate_url.h"
#include "services/common/util/request_metadata.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

// Builds Seller KV Value lookup https request url.
HTTPRequest BuildSellerKeyValueRequest(
    absl::string_view kv_server_host_domain, const RequestMetadata& metadata,
    std::unique_ptr<GetSellerValuesInput> client_input) {
  HTTPRequest request;
  ClearAndMakeStartOfUrl(kv_server_host_domain, &request.url);

  if (!client_input->render_urls.empty()) {
    AddListItemsAsQueryParamsToUrl(&request.url, "renderUrls",
                                   client_input->render_urls, true);
  }
  if (!client_input->ad_component_render_urls.empty()) {
    AddListItemsAsQueryParamsToUrl(&request.url, "adComponentRenderUrls",
                                   client_input->ad_component_render_urls,
                                   true);
  }
  request.headers = RequestMetadataToHttpHeaders(metadata);
  return request;
}
}  // namespace

absl::Status SellerKeyValueAsyncHttpClient::Execute(
    std::unique_ptr<GetSellerValuesInput> keys, const RequestMetadata& metadata,
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
        on_done,
    absl::Duration timeout) const {
  HTTPRequest request = BuildSellerKeyValueRequest(kv_server_base_address_,
                                                   metadata, std::move(keys));
  VLOG(2) << "SellerKeyValueAsyncHttpClient Request: " << request.url;
  VLOG(2) << "\nSellerKeyValueAsyncHttpClient Headers:\n";
  for (const auto& header : request.headers) {
    VLOG(2) << header;
  }
  auto done_callback = [on_done = std::move(on_done)](
                           absl::StatusOr<std::string> resultStr) mutable {
    if (resultStr.ok()) {
      VLOG(2) << "SellerKeyValueAsyncHttpClient Response: "
              << resultStr.value();
      std::unique_ptr<GetSellerValuesOutput> resultUPtr =
          std::make_unique<GetSellerValuesOutput>(
              GetSellerValuesOutput({std::move(resultStr.value())}));
      std::move(on_done)(std::move(resultUPtr));
    } else {
      VLOG(2) << "SellerKeyValueAsyncHttpClients Response: "
              << resultStr.status();
      std::move(on_done)(resultStr.status());
    }
  };
  http_fetcher_async_->FetchUrl(
      request, static_cast<int>(absl::ToInt64Milliseconds(timeout)),
      std::move(done_callback));
  return absl::OkStatus();
}

SellerKeyValueAsyncHttpClient::SellerKeyValueAsyncHttpClient(
    absl::string_view kv_server_base_address,
    std::unique_ptr<HttpFetcherAsync> http_fetcher_async, bool pre_warm)
    : http_fetcher_async_(std::move(http_fetcher_async)),
      kv_server_base_address_(kv_server_base_address) {
  if (pre_warm) {
    auto request = std::make_unique<GetSellerValuesInput>();
    Execute(
        std::move(request), {},
        [](absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>
               seller_kv_output) mutable {
          if (!seller_kv_output.ok()) {
            VLOG(1) << "SellerKeyValueAsyncHttpClient pre-warm returned status:"
                    << seller_kv_output.status().message();
          }
        },
        // Longer timeout for first request
        absl::Milliseconds(60000));
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
