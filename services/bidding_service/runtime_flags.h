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

#ifndef FLEDGE_SERVICES_BIDDING_SERVICE_RUNTIME_FLAGS_H_
#define FLEDGE_SERVICES_BIDDING_SERVICE_RUNTIME_FLAGS_H_

#include <vector>

#include "absl/strings/string_view.h"
#include "services/common/constants/common_service_flags.h"

namespace privacy_sandbox::bidding_auction_servers {

// Define runtime flag names.
inline constexpr char PORT[] = "BIDDING_PORT";
inline constexpr char ENABLE_BIDDING_SERVICE_BENCHMARK[] =
    "ENABLE_BIDDING_SERVICE_BENCHMARK";
inline constexpr char BUYER_CODE_FETCH_CONFIG[] = "BUYER_CODE_FETCH_CONFIG";
inline constexpr char JS_NUM_WORKERS[] = "JS_NUM_WORKERS";
inline constexpr char JS_WORKER_QUEUE_LEN[] = "JS_WORKER_QUEUE_LEN";

inline constexpr absl::string_view kFlags[] = {
    PORT, ENABLE_BIDDING_SERVICE_BENCHMARK, BUYER_CODE_FETCH_CONFIG,
    JS_NUM_WORKERS, JS_WORKER_QUEUE_LEN};

inline std::vector<absl::string_view> GetServiceFlags() {
  int size = sizeof(kFlags) / sizeof(kFlags[0]);
  std::vector<absl::string_view> flags(kFlags, kFlags + size);

  for (absl::string_view flag : kCommonServiceFlags) {
    flags.push_back(flag);
  }

  return flags;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_BIDDING_SERVICE_RUNTIME_FLAGS_H_
