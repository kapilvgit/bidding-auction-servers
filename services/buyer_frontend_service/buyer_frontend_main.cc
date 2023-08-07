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

#include <memory>
#include <string>

#include <aws/core/Aws.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "glog/logging.h"
#include "grpcpp/ext/proto_server_reflection_plugin.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/health_check_service_interface.h"
#include "opentelemetry/metrics/provider.h"
#include "public/cpio/interface/cpio.h"
#include "services/buyer_frontend_service/buyer_frontend_service.h"
#include "services/buyer_frontend_service/providers/http_bidding_signals_async_provider.h"
#include "services/buyer_frontend_service/runtime_flags.h"
#include "services/common/clients/bidding_server/bidding_async_client.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/metric/server_definition.h"
#include "services/common/telemetry/configure_telemetry.h"
#include "services/common/util/status_macros.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/cpp/concurrent/event_engine_executor.h"
#include "src/cpp/encryption/key_fetcher/src/key_fetcher_manager.h"

ABSL_FLAG(std::optional<uint16_t>, port, std::nullopt,
          "Port the server is listening on.");
ABSL_FLAG(std::optional<std::string>, bidding_server_addr, std::nullopt,
          "Bidding Server Address");
ABSL_FLAG(std::optional<std::string>, buyer_kv_server_addr, std::nullopt,
          "Buyer KV Server Address");
// Added for performance/benchmark testing of both types of http clients.
ABSL_FLAG(std::optional<int>, generate_bid_timeout_ms, std::nullopt,
          "Max time to wait for generate bid request to finish.");
ABSL_FLAG(std::optional<int>, bidding_signals_load_timeout_ms, std::nullopt,
          "Max time to wait for fetching bidding signals to finish.");
ABSL_FLAG(std::optional<bool>, enable_buyer_frontend_benchmarking, std::nullopt,
          "Enable benchmarking the BuyerFrontEnd Server.");
ABSL_FLAG(
    std::optional<bool>, create_new_event_engine, std::nullopt,
    "Share the event engine with gprc when false , otherwise create new one");
ABSL_FLAG(std::optional<bool>, enable_bidding_compression, true,
          "Flag to enable bidding client compression. True by default.");
ABSL_FLAG(std::optional<bool>, bfe_ingress_tls, std::nullopt,
          "If true, frontend gRPC service terminates TLS");
ABSL_FLAG(std::optional<std::string>, bfe_tls_key, std::nullopt,
          "TLS key string. Required if bfe_ingress_tls=true.");
ABSL_FLAG(std::optional<std::string>, bfe_tls_cert, std::nullopt,
          "TLS cert string. Required if bfe_ingress_tls=true.");
ABSL_FLAG(std::optional<bool>, bidding_egress_tls, std::nullopt,
          "If true, bidding service gRPC client uses TLS.");
ABSL_FLAG(
    bool, init_config_client, false,
    "Initialize config client to fetch any runtime flags not supplied from"
    " command line from cloud metadata store. False by default.");
namespace privacy_sandbox::bidding_auction_servers {

using ::google::scp::cpio::Cpio;
using ::google::scp::cpio::CpioOptions;
using ::google::scp::cpio::LogOption;
using ::grpc::Server;
using ::grpc::ServerBuilder;

absl::StatusOr<TrustedServersConfigClient> GetConfigClient(
    std::string config_param_prefix) {
  TrustedServersConfigClient config_client(GetServiceFlags());
  config_client.SetFlag(FLAGS_port, PORT);
  config_client.SetFlag(FLAGS_bidding_server_addr, BIDDING_SERVER_ADDR);
  config_client.SetFlag(FLAGS_buyer_kv_server_addr, BUYER_KV_SERVER_ADDR);
  config_client.SetFlag(FLAGS_generate_bid_timeout_ms, GENERATE_BID_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_bidding_signals_load_timeout_ms,
                        BIDDING_SIGNALS_LOAD_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_enable_buyer_frontend_benchmarking,
                        ENABLE_BUYER_FRONTEND_BENCHMARKING);
  config_client.SetFlag(FLAGS_create_new_event_engine, CREATE_NEW_EVENT_ENGINE);
  config_client.SetFlag(FLAGS_enable_bidding_compression,
                        ENABLE_BIDDING_COMPRESSION);
  config_client.SetFlag(FLAGS_bfe_ingress_tls, BFE_INGRESS_TLS);
  config_client.SetFlag(FLAGS_bfe_tls_key, BFE_TLS_KEY);
  config_client.SetFlag(FLAGS_bfe_tls_cert, BFE_TLS_CERT);
  config_client.SetFlag(FLAGS_bidding_egress_tls, BIDDING_EGRESS_TLS);
  config_client.SetFlag(FLAGS_enable_encryption, ENABLE_ENCRYPTION);
  config_client.SetFlag(FLAGS_test_mode, TEST_MODE);
  config_client.SetFlag(FLAGS_public_key_endpoint, PUBLIC_KEY_ENDPOINT);
  config_client.SetFlag(FLAGS_primary_coordinator_private_key_endpoint,
                        PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT);
  config_client.SetFlag(FLAGS_secondary_coordinator_private_key_endpoint,
                        SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT);
  config_client.SetFlag(FLAGS_primary_coordinator_account_identity,
                        PRIMARY_COORDINATOR_ACCOUNT_IDENTITY);
  config_client.SetFlag(FLAGS_secondary_coordinator_account_identity,
                        SECONDARY_COORDINATOR_ACCOUNT_IDENTITY);
  config_client.SetFlag(FLAGS_gcp_primary_workload_identity_pool_provider,
                        GCP_PRIMARY_WORKLOAD_IDENTITY_POOL_PROVIDER);
  config_client.SetFlag(FLAGS_gcp_secondary_workload_identity_pool_provider,
                        GCP_SECONDARY_WORKLOAD_IDENTITY_POOL_PROVIDER);
  config_client.SetFlag(FLAGS_gcp_primary_key_service_cloud_function_url,
                        GCP_PRIMARY_KEY_SERVICE_CLOUD_FUNCTION_URL);
  config_client.SetFlag(FLAGS_gcp_secondary_key_service_cloud_function_url,
                        GCP_SECONDARY_KEY_SERVICE_CLOUD_FUNCTION_URL);
  config_client.SetFlag(FLAGS_primary_coordinator_region,
                        PRIMARY_COORDINATOR_REGION);
  config_client.SetFlag(FLAGS_secondary_coordinator_region,
                        SECONDARY_COORDINATOR_REGION);
  config_client.SetFlag(FLAGS_private_key_cache_ttl_seconds,
                        PRIVATE_KEY_CACHE_TTL_SECONDS);
  config_client.SetFlag(FLAGS_key_refresh_flow_run_frequency_seconds,
                        KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS);
  config_client.SetFlag(FLAGS_telemetry_config, TELEMETRY_CONFIG);
  config_client.SetFlag(FLAGS_consented_debug_token, CONSENTED_DEBUG_TOKEN);
  config_client.SetFlag(FLAGS_enable_otel_based_logging,
                        ENABLE_OTEL_BASED_LOGGING);

  if (absl::GetFlag(FLAGS_init_config_client)) {
    PS_RETURN_IF_ERROR(config_client.Init(config_param_prefix)).LogError()
        << "Config client failed to initialize.";
  }

  VLOG(1) << "Successfully constructed the config client.\n";
  return config_client;
}

// Brings up the gRPC async BuyerFrontEndService on FLAGS_port.
absl::Status RunServer() {
  TrustedServerConfigUtil config_util(absl::GetFlag(FLAGS_init_config_client));
  PS_ASSIGN_OR_RETURN(TrustedServersConfigClient config_client,
                      GetConfigClient(config_util.GetConfigParameterPrefix()));

  int port = config_client.GetIntParameter(PORT);
  std::string bidding_server_addr =
      std::string(config_client.GetStringParameter(BIDDING_SERVER_ADDR));
  std::string buyer_kv_server_addr =
      std::string(config_client.GetStringParameter(BUYER_KV_SERVER_ADDR));
  bool enable_buyer_frontend_benchmarking =
      config_client.GetBooleanParameter(ENABLE_BUYER_FRONTEND_BENCHMARKING);
  bool enable_bidding_compression =
      config_client.GetBooleanParameter(ENABLE_BIDDING_COMPRESSION);

  if (bidding_server_addr.empty()) {
    return absl::InvalidArgumentError("Missing: Bidding server address");
  }
  if (buyer_kv_server_addr.empty()) {
    return absl::InvalidArgumentError("Missing: Buyer KV server address");
  }

  server_common::GrpcInit gprc_init;
  auto executor = std::make_unique<server_common::EventEngineExecutor>(
      config_client.GetBooleanParameter(CREATE_NEW_EVENT_ENGINE)
          ? grpc_event_engine::experimental::CreateEventEngine()
          : grpc_event_engine::experimental::GetDefaultEventEngine());
  std::unique_ptr<BuyerKeyValueAsyncHttpClient> buyer_kv_async_http_client;
  buyer_kv_async_http_client = std::make_unique<BuyerKeyValueAsyncHttpClient>(
      buyer_kv_server_addr,
      std::make_unique<MultiCurlHttpFetcherAsync>(executor.get()), true);

  server_common::BuildDependentConfig telemetry_config(
      config_client
          .GetCustomParameter<server_common::TelemetryFlag>(TELEMETRY_CONFIG)
          .server_config);
  std::string collector_endpoint =
      config_client.GetStringParameter(COLLECTOR_ENDPOINT).data();
  server_common::InitTelemetry(
      config_util.GetService(), kOpenTelemetryVersion.data(),
      telemetry_config.TraceAllowed(), telemetry_config.MetricAllowed(),
      config_client.GetBooleanParameter(ENABLE_OTEL_BASED_LOGGING));
  server_common::ConfigureMetrics(CreateSharedAttributes(&config_util),
                                  CreateMetricsOptions(), collector_endpoint);
  server_common::ConfigureTracer(CreateSharedAttributes(&config_util),
                                 collector_endpoint);
  server_common::ConfigureLogger(CreateSharedAttributes(&config_util),
                                 collector_endpoint);
  AddSystemMetric(metric::BfeContextMap(
      std::move(telemetry_config),
      opentelemetry::metrics::Provider::GetMeterProvider()
          ->GetMeter(config_util.GetService(), kOpenTelemetryVersion.data())
          .get()));

  BuyerFrontEndService buyer_frontend_service(
      std::make_unique<HttpBiddingSignalsAsyncProvider>(
          std::move(buyer_kv_async_http_client)),
      BiddingServiceClientConfig{
          .server_addr = bidding_server_addr,
          .compression = enable_bidding_compression,
          .secure_client =
              config_client.GetBooleanParameter(BIDDING_EGRESS_TLS),
          .encryption_enabled =
              config_client.GetBooleanParameter(ENABLE_ENCRYPTION)},
      CreateKeyFetcherManager(config_client), CreateCryptoClient(),
      GetBidsConfig{
          config_client.GetIntParameter(GENERATE_BID_TIMEOUT_MS),
          config_client.GetIntParameter(BIDDING_SIGNALS_LOAD_TIMEOUT_MS),
          config_client.GetBooleanParameter(ENABLE_ENCRYPTION)},
      enable_buyer_frontend_benchmarking);

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  std::string server_address = absl::StrCat("0.0.0.0:", port);
  if (config_client.GetBooleanParameter(BFE_INGRESS_TLS)) {
    std::vector<grpc::experimental::IdentityKeyCertPair> key_cert_pairs{{
        .private_key =
            std::string(config_client.GetStringParameter(BFE_TLS_KEY)),
        .certificate_chain =
            std::string(config_client.GetStringParameter(BFE_TLS_CERT)),
    }};
    auto certificate_provider =
        std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
            key_cert_pairs);
    grpc::experimental::TlsServerCredentialsOptions options(
        certificate_provider);
    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("buyer_frontend");
    builder.AddListeningPort(server_address,
                             grpc::experimental::TlsServerCredentials(options));
  } else {
    // Listen on the given address without any authentication mechanism.
    // This server is expected to accept insecure connections as it will be
    // deployed behind an HTTPS load balancer that terminates TLS.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  }
  builder.RegisterService(&buyer_frontend_service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    return absl::UnavailableError("Error starting Server.");
  }
  VLOG(1) << "Server listening on " << server_address;

  // Wait for the server to shut down. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
  return absl::OkStatus();
}
}  // namespace privacy_sandbox::bidding_auction_servers

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  google::InitGoogleLogging(argv[0]);

  google::scp::cpio::CpioOptions cpio_options;

  bool init_config_client = absl::GetFlag(FLAGS_init_config_client);
  if (init_config_client) {
    cpio_options.log_option = google::scp::cpio::LogOption::kConsoleLog;
    CHECK(google::scp::cpio::Cpio::InitCpio(cpio_options).Successful())
        << "Failed to initialize CPIO library";
  }

  CHECK_OK(privacy_sandbox::bidding_auction_servers::RunServer())
      << "Failed to run server ";

  if (init_config_client) {
    google::scp::cpio::Cpio::ShutdownCpio(cpio_options);
  }

  return 0;
}
