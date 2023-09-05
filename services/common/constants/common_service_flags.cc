// Copyright 2023 Google LLC
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

#include "services/common/constants/common_service_flags.h"

#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"

ABSL_FLAG(
    std::optional<bool>, enable_encryption, false,
    "Enable the use of encryption on the server requests/responses. False by "
    "default.");
ABSL_FLAG(std::optional<std::string>, public_key_endpoint, std::nullopt,
          "Endpoint serving set of public keys used for encryption");
ABSL_FLAG(std::optional<std::string>, primary_coordinator_private_key_endpoint,
          std::nullopt,
          "Primary coordinator's private key vending service endpoint");
ABSL_FLAG(std::optional<std::string>,
          secondary_coordinator_private_key_endpoint, std::nullopt,
          "Secondary coordinator's Private Key Vending Service endpoint");
ABSL_FLAG(std::optional<std::string>, primary_coordinator_account_identity,
          std::nullopt,
          "The identity used to communicate with the primary coordinator's "
          "Private Key Vending Service");
ABSL_FLAG(std::optional<std::string>, secondary_coordinator_account_identity,
          std::nullopt,
          "The identity used to communicate with the secondary coordinator's "
          "Private Key Vending Service");
ABSL_FLAG(
    std::optional<std::string>, primary_coordinator_region, std::nullopt,
    "The region of the primary coordinator's Private Key Vending Service");
ABSL_FLAG(
    std::optional<std::string>, secondary_coordinator_region, std::nullopt,
    "The region of the secondary coordinator's Private Key Vending Service");
ABSL_FLAG(std::optional<std::string>,
          gcp_primary_workload_identity_pool_provider, std::nullopt,
          "The GCP primary workload identity pool provider resource name.");
ABSL_FLAG(std::optional<std::string>,
          gcp_secondary_workload_identity_pool_provider, std::nullopt,
          "The GCP secondary workload identity pool provider resource name.");

ABSL_FLAG(std::optional<std::string>,
          gcp_primary_key_service_cloud_function_url, std::nullopt,
          "GCP primary private key vending service cloud function URL.");
ABSL_FLAG(std::optional<std::string>,
          gcp_secondary_key_service_cloud_function_url, std::nullopt,
          "GCP secondary private key vending service cloud function URL.");
ABSL_FLAG(std::optional<int>, private_key_cache_ttl_seconds,
          3888000,  // 45 days
          "The duration of how long encryption keys are cached in memory");
ABSL_FLAG(std::optional<int>, key_refresh_flow_run_frequency_seconds,
          10800,  // 3 hours
          "The frequency at which the encryption key refresh flow should run.");
// Master flag for controlling all the features that needs to turned on for
// testing.
ABSL_FLAG(std::optional<bool>, test_mode, std::nullopt, "Enable test mode");
ABSL_FLAG(std::optional<privacy_sandbox::server_common::TelemetryFlag>,
          telemetry_config, std::nullopt, "configure telemetry.");
ABSL_FLAG(std::optional<std::string>, roma_timeout_ms, std::nullopt,
          "The timeout used by Roma for dispatch requests");
ABSL_FLAG(std::optional<std::string>, collector_endpoint, std::nullopt,
          "The endpoint of the OpenTelemetry Collector");
ABSL_FLAG(std::optional<std::string>, consented_debug_token, std::nullopt,
          "The secret token for AdTech consented debugging. The server will "
          "enable logs only for the request with the matching token. It should "
          "not be publicly shared.");
ABSL_FLAG(std::optional<bool>, enable_otel_based_logging, false,
          "Whether the OpenTelemetry Logs is enabled. It's used for consented "
          "debugging.");
ABSL_FLAG(std::optional<bool>, enable_protected_app_signals, false,
          "Enables the protected app signals support.");
