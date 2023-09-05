// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SERVICES_BUYER_FRONTEND_SERVICE_DATA_GET_BIDS_CONFIG_H_
#define SERVICES_BUYER_FRONTEND_SERVICE_DATA_GET_BIDS_CONFIG_H_

#include <string>

namespace privacy_sandbox::bidding_auction_servers {

struct GetBidsConfig {
  // The max time to wait for generate bid request to finish.
  int generate_bid_timeout_ms;
  // The max time to wait for fetching bidding signals to finish.
  int bidding_signals_load_timeout_ms;
  // Enables encryption when communicating with clients and other servers.
  bool encryption_enabled;
  // The max time to wait for protected app signals generate bid request to
  // finish.
  int protected_app_signals_generate_bid_timeout_ms;
  // Indicates whether Protected App Signals support is enabled or not.
  bool is_protected_app_signals_enabled;
  // Whether the OpenTelemetry Logs is enabled. It's used for consented
  // debugging.
  bool enable_otel_based_logging;
  // The secret token for AdTech consented debugging.
  std::string consented_debug_token;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BUYER_FRONTEND_SERVICE_DATA_GET_BIDS_CONFIG_H_
