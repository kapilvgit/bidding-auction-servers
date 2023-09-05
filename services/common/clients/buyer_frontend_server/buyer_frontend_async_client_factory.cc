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

#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client_factory.h"

#include <utility>

#include "absl/container/flat_hash_map.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "glog/logging.h"
#include "services/common/clients/async_client.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client.h"
#include "services/common/concurrent/static_local_cache.h"

namespace privacy_sandbox::bidding_auction_servers {

BuyerFrontEndAsyncClientFactory::BuyerFrontEndAsyncClientFactory(
    const absl::flat_hash_map<std::string, std::string>&
        buyer_ig_owner_to_bfe_addr_map,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const BuyerServiceClientConfig& client_config) {
  absl::flat_hash_map<std::string,
                      std::shared_ptr<const BuyerFrontEndAsyncClient>>
      bfe_addr_client_map;
  auto static_client_map = std::make_unique<absl::flat_hash_map<
      std::string, std::shared_ptr<const BuyerFrontEndAsyncClient>>>();
  for (const auto& [ig_owner, bfe_host_addr] : buyer_ig_owner_to_bfe_addr_map) {
    // Can perform additional validations here.
    if (ig_owner.empty() || bfe_host_addr.empty()) {
      continue;
    }

    if (auto it = bfe_addr_client_map.find(bfe_host_addr);
        it != bfe_addr_client_map.end()) {
      static_client_map->try_emplace(ig_owner, it->second);
    } else {
      BuyerServiceClientConfig client_config_copy = client_config;
      client_config_copy.server_addr = bfe_host_addr;
      auto bfe_client_ptr =
          std::make_shared<const BuyerFrontEndAsyncGrpcClient>(
              key_fetcher_manager, crypto_client,
              std::move(client_config_copy));
      bfe_addr_client_map.insert({bfe_host_addr, bfe_client_ptr});
      static_client_map->try_emplace(ig_owner, bfe_client_ptr);
    }
  }
  client_cache_ = std::make_unique<
      StaticLocalCache<std::string, const BuyerFrontEndAsyncClient>>(
      std::move(static_client_map));
}

std::shared_ptr<const BuyerFrontEndAsyncClient>
BuyerFrontEndAsyncClientFactory::Get(absl::string_view ig_owner) const {
  std::string ig_owner_str = absl::StrCat(ig_owner);
  return client_cache_->LookUp(ig_owner_str);
}

}  // namespace privacy_sandbox::bidding_auction_servers
