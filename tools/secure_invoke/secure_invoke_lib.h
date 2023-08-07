// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TOOLS_INVOKE_SECURE_INVOKE_LIB_H_
#define TOOLS_INVOKE_SECURE_INVOKE_LIB_H_

#include <memory>
#include <string>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.grpc.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr absl::Duration timeout = absl::Milliseconds(120000);

struct RequestOptions {
  std::string client_ip;
  std::string user_agent;
  std::string accept_language;
  std::string host_addr;
  bool insecure;
};

// Sends a request to SFE. The parameters used for the request are retrieved
// from absl flags that are used to run the script.
absl::Status SendRequestToSfe(SelectAdRequest::ClientType client_type);

// Sends a request to BFE. The parameters used for the request are retrieved
// from absl flags that are used to run the script.
absl::Status SendRequestToBfe(
    std::unique_ptr<BuyerFrontEnd::StubInterface> stub = nullptr);

// Gets contents of the provided file path.
std::string LoadFile(absl::string_view file_path);

// Returns a JSON string of the OHTTP encrypted of the input GetBidsRawRequest
// to the secure invoke tool.
std::string PackagePlainTextGetBidsRequestToJson();

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // TOOLS_INVOKE_SECURE_INVOKE_LIB_H_
