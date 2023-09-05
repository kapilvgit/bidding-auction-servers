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

#ifndef FLEDGE_SERVICES_COMMON_TEST_MOCKS_H_
#define FLEDGE_SERVICES_COMMON_TEST_MOCKS_H_

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "gmock/gmock.h"
#include "include/grpc/event_engine/event_engine.h"
#include "services/auction_service/score_ads_reactor.h"
#include "services/bidding_service/generate_bids_reactor.h"
#include "services/common/clients/auction_server/scoring_async_client.h"
#include "services/common/clients/bidding_server/bidding_async_client.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client_factory.h"
#include "services/common/clients/client_factory.h"
#include "services/common/clients/code_dispatcher/code_dispatch_client.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/concurrent/local_cache.h"
#include "services/common/providers/async_provider.h"
#include "services/common/reporters/async_reporter.h"
#include "src/cpp/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

class MockBuyerFrontEnd : public BuyerFrontEnd::CallbackService {
 public:
  MockBuyerFrontEnd() {}

  MOCK_METHOD(grpc::ServerUnaryReactor*, GetBids,
              (grpc::CallbackServerContext * context,
               const GetBidsRequest* request, GetBidsResponse* response),
              (override));
};

class MockEventEngine : public grpc_event_engine::experimental::EventEngine {
 public:
  MOCK_METHOD(void, Run, (absl::AnyInvocable<void()> closure), (override));
  MOCK_METHOD(void, Run,
              (grpc_event_engine::experimental::EventEngine::Closure * closure),
              (override));
  MOCK_METHOD(grpc_event_engine::experimental::EventEngine::TaskHandle,
              RunAfter,
              (grpc_event_engine::experimental::EventEngine::Duration when,
               absl::AnyInvocable<void()> closure),
              (override));
  MOCK_METHOD(grpc_event_engine::experimental::EventEngine::TaskHandle,
              RunAfter,
              (grpc_event_engine::experimental::EventEngine::Duration when,
               grpc_event_engine::experimental::EventEngine::Closure* closure),
              (override));
  MOCK_METHOD(
      absl::StatusOr<std::unique_ptr<Listener>>, CreateListener,
      (Listener::AcceptCallback on_accept,
       absl::AnyInvocable<void(absl::Status)> on_shutdown,
       const grpc_event_engine::experimental::EndpointConfig& config,
       std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory>
           memory_allocator_factory),
      (override));
  MOCK_METHOD(
      grpc_event_engine::experimental::EventEngine::ConnectionHandle, Connect,
      (grpc_event_engine::experimental::EventEngine::OnConnectCallback
           on_connect,
       const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
           addr,
       const grpc_event_engine::experimental::EndpointConfig& args,
       grpc_event_engine::experimental::MemoryAllocator memory_allocator,
       grpc_event_engine::experimental::EventEngine::Duration timeout),
      (override));
  MOCK_METHOD(bool, Cancel,
              (grpc_event_engine::experimental::EventEngine::TaskHandle handle),
              (override));
  MOCK_METHOD(
      bool, CancelConnect,
      (grpc_event_engine::experimental::EventEngine::ConnectionHandle handle),
      (override));
  MOCK_METHOD(bool, IsWorkerThread, (), (override));
  MOCK_METHOD(std::unique_ptr<
                  grpc_event_engine::experimental::EventEngine::DNSResolver>,
              GetDNSResolver,
              (const grpc_event_engine::experimental::EventEngine::DNSResolver::
                   ResolverOptions& options),
              (override));
};

class MockExecutor : public server_common::Executor {
 public:
  MOCK_METHOD(void, Run, (absl::AnyInvocable<void()> closure), (override));
  MOCK_METHOD(server_common::TaskId, RunAfter,
              (absl::Duration duration, absl::AnyInvocable<void()> closure),
              (override));
  MOCK_METHOD(bool, Cancel, (server_common::TaskId task_id), (override));
};

template <typename Request, typename Response, typename RawRequest,
          typename RawResponse>
class AsyncClientMock
    : public AsyncClient<Request, Response, RawRequest, RawResponse> {
 public:
  MOCK_METHOD(
      absl::Status, Execute,
      (std::unique_ptr<Request> request, const RequestMetadata& metadata,
       absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Response>>) &&>
           on_done,
       absl::Duration timeout),
      (const, override));

  MOCK_METHOD(
      absl::Status, ExecuteInternal,
      (std::unique_ptr<RawRequest> raw_request, const RequestMetadata& metadata,
       absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<RawResponse>>) &&>
           on_done,
       absl::Duration timeout),
      (const, override));
};

using BuyerFrontEndAsyncClientMock =
    AsyncClientMock<GetBidsRequest, GetBidsResponse,
                    GetBidsRequest::GetBidsRawRequest,
                    GetBidsResponse::GetBidsRawResponse>;
using ScoringAsyncClientMock =
    AsyncClientMock<ScoreAdsRequest, ScoreAdsResponse,
                    ScoreAdsRequest::ScoreAdsRawRequest,
                    ScoreAdsResponse::ScoreAdsRawResponse>;
using BiddingAsyncClientMock =
    AsyncClientMock<GenerateBidsRequest, GenerateBidsResponse,
                    GenerateBidsRequest::GenerateBidsRawRequest,
                    GenerateBidsResponse::GenerateBidsRawResponse>;

// Utility class to be used by anything that relies on an HttpFetcherAsync.
class MockHttpFetcherAsync : public HttpFetcherAsync {
 public:
  MOCK_METHOD(void, FetchUrl,
              (const HTTPRequest& http_request, int timeout_ms,
               OnDoneFetchUrl done_callback),
              (override));
  MOCK_METHOD(void, FetchUrls,
              (const std::vector<HTTPRequest>& requests, absl::Duration timeout,
               OnDoneFetchUrls done_callback),
              (override));
};

// Utility class to be used to mock AsyncReporter.
class MockAsyncReporter : public AsyncReporter {
 public:
  explicit MockAsyncReporter(
      std::unique_ptr<HttpFetcherAsync> http_fetcher_async)
      : AsyncReporter(std::move(http_fetcher_async)) {}

  MOCK_METHOD(void, DoReport,
              (const HTTPRequest& reporting_request,
               absl::AnyInvocable<void(absl::StatusOr<absl::string_view>) &&>
                   done_callback),
              (override));
};

// Dummy server in leu of no support for mocking async stubs
class SellerFrontEndServiceMock : public SellerFrontEnd::CallbackService {
 public:
  explicit SellerFrontEndServiceMock(
      std::function<grpc::ServerUnaryReactor*(grpc::CallbackServerContext*,
                                              const SelectAdRequest*,
                                              SelectAdResponse*)>
          rpc_method)
      : server_rpc_(std::move(rpc_method)) {}

  grpc::ServerUnaryReactor* SelectAd(grpc::CallbackServerContext* ctxt,
                                     const SelectAdRequest* req,
                                     SelectAdResponse* resp) override {
    return server_rpc_(ctxt, req, resp);
  }

 private:
  std::function<grpc::ServerUnaryReactor*(
      grpc::CallbackServerContext*, const SelectAdRequest*, SelectAdResponse*)>
      server_rpc_;
};

// Dummy server in leu of no support for mocking async stubs
class BiddingServiceMock : public Bidding::CallbackService {
 public:
  explicit BiddingServiceMock(
      std::function<grpc::ServerUnaryReactor*(grpc::CallbackServerContext*,
                                              const GenerateBidsRequest*,
                                              GenerateBidsResponse*)>
          rpc_method)
      : server_rpc_(std::move(rpc_method)) {}

  explicit BiddingServiceMock(std::function<grpc::ServerUnaryReactor*(
                                  grpc::CallbackServerContext*,
                                  const GenerateProtectedAppSignalsBidsRequest*,
                                  GenerateProtectedAppSignalsBidsResponse*)>
                                  rpc_method)
      : server_pas_rpc_(std::move(rpc_method)) {}

  grpc::ServerUnaryReactor* GenerateBids(grpc::CallbackServerContext* ctxt,
                                         const GenerateBidsRequest* req,
                                         GenerateBidsResponse* resp) override {
    return server_rpc_(ctxt, req, resp);
  }

  grpc::ServerUnaryReactor* GenerateProtectedAppSignalsBids(
      grpc::CallbackServerContext* ctxt,
      const GenerateProtectedAppSignalsBidsRequest* req,
      GenerateProtectedAppSignalsBidsResponse* resp) override {
    return server_pas_rpc_(ctxt, req, resp);
  }

 private:
  std::function<grpc::ServerUnaryReactor*(grpc::CallbackServerContext*,
                                          const GenerateBidsRequest*,
                                          GenerateBidsResponse*)>
      server_rpc_;

  std::function<grpc::ServerUnaryReactor*(
      grpc::CallbackServerContext*,
      const GenerateProtectedAppSignalsBidsRequest*,
      GenerateProtectedAppSignalsBidsResponse*)>
      server_pas_rpc_;
};

// Dummy server in leu of no support for mocking async stubs
class AuctionServiceMock : public Auction::CallbackService {
 public:
  explicit AuctionServiceMock(std::function<grpc::ServerUnaryReactor*(
                                  grpc::CallbackServerContext*,
                                  const ScoreAdsRequest*, ScoreAdsResponse*)>
                                  rpc_method)
      : server_rpc_(std::move(rpc_method)) {}

  grpc::ServerUnaryReactor* ScoreAds(grpc::CallbackServerContext* ctxt,
                                     const ScoreAdsRequest* req,
                                     ScoreAdsResponse* resp) override {
    return server_rpc_(ctxt, req, resp);
  }

 private:
  std::function<grpc::ServerUnaryReactor*(
      grpc::CallbackServerContext*, const ScoreAdsRequest*, ScoreAdsResponse*)>
      server_rpc_;
};

// Dummy server in lieu of no support for mocking async stubs.
class BuyerFrontEndServiceMock : public BuyerFrontEnd::CallbackService {
 public:
  explicit BuyerFrontEndServiceMock(
      std::function<grpc::ServerUnaryReactor*(grpc::CallbackServerContext*,
                                              const GetBidsRequest*,
                                              GetBidsResponse*)>
          rpc_method)
      : server_rpc_(std::move(rpc_method)) {}

  grpc::ServerUnaryReactor* GetBids(grpc::CallbackServerContext* ctxt,
                                    const GetBidsRequest* req,
                                    GetBidsResponse* resp) override {
    return server_rpc_(ctxt, req, resp);
  }

 private:
  std::function<grpc::ServerUnaryReactor*(
      grpc::CallbackServerContext*, const GetBidsRequest*, GetBidsResponse*)>
      server_rpc_;
};

class BuyerFrontEndAsyncClientFactoryMock
    : public ClientFactory<BuyerFrontEndAsyncClient, absl::string_view> {
 public:
  MOCK_METHOD(std::shared_ptr<const BuyerFrontEndAsyncClient>, Get,
              (absl::string_view), (const, override));
};

class MockV8Dispatcher : public V8Dispatcher {
 public:
  MOCK_METHOD(absl::Status, Init, (DispatchConfig config), (const));
  MOCK_METHOD(absl::Status, Stop, (), (const));
  MOCK_METHOD(absl::Status, LoadSync, (int version, absl::string_view js),
              (const));
  MOCK_METHOD(absl::Status, BatchExecute,
              (std::vector<DispatchRequest> & batch,
               BatchDispatchDoneCallback batch_callback),
              (const));
  MOCK_METHOD(absl::Status, Execute,
              (std::unique_ptr<DispatchRequest> request,
               DispatchDoneCallback done_callback),
              (const));
};

class MockCodeDispatchClient : public CodeDispatchClient {
 public:
  MockCodeDispatchClient() : CodeDispatchClient(MockV8Dispatcher()) {}
  MOCK_METHOD(absl::Status, BatchExecute,
              (std::vector<DispatchRequest> & batch,
               BatchDispatchDoneCallback batch_callback),
              (const));
};

template <class Key, class Value>
class LocalCacheMock : public LocalCache<Key, Value> {
 public:
  MOCK_METHOD(Value, LookUp, (Key key), (override));
};

class MockScoreAdsReactor : public ScoreAdsReactor {
 public:
  MockScoreAdsReactor(
      const CodeDispatchClient& dispatcher, const ScoreAdsRequest* request,
      ScoreAdsResponse* response,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      const AuctionServiceRuntimeConfig& runtime_config,
      std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger,
      std::unique_ptr<AsyncReporter> async_reporter, absl::string_view js)
      : ScoreAdsReactor(dispatcher, request, response,
                        std::move(benchmarking_logger), key_fetcher_manager,
                        crypto_client, std::move(async_reporter),
                        std::move(runtime_config)) {}
  MOCK_METHOD(void, Execute, (), (override));
};

class MockGenerateBidsReactor : public GenerateBidsReactor {
 public:
  MockGenerateBidsReactor(
      const CodeDispatchClient& dispatcher, const GenerateBidsRequest* request,
      GenerateBidsResponse* response, absl::string_view js,
      std::unique_ptr<BiddingBenchmarkingLogger> benchmarkingLogger,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      const BiddingServiceRuntimeConfig& runtime_config)
      : GenerateBidsReactor(dispatcher, request, response,
                            std::move(benchmarkingLogger), key_fetcher_manager,
                            crypto_client_, std::move(runtime_config)) {}
  MOCK_METHOD(void, Execute, (), (override));
};

template <class Client, class ClientKey>
class ClientFactoryMock : public ClientFactory<Client, ClientKey> {
 public:
  MOCK_METHOD(std::shared_ptr<const Client>, Get, (ClientKey),
              (const, override));
};

template <class ServiceMock, class RawRequest, class Response>
class MockServerThread {
 public:
  explicit MockServerThread(
      std::function<grpc::ServerUnaryReactor*(grpc::CallbackServerContext*,
                                              const RawRequest*, Response*)>
          rpc_method) {
    // Setup server
    mock_server_address_ = "localhost";
    service_ = std::make_unique<ServiceMock>(std::move(rpc_method));
    grpc::ServerBuilder builder;

    auto certificate_provider =
        std::make_shared<grpc::experimental::FileWatcherCertificateProvider>(
            ca_private_key_path, ca_private_cert_path, ca_root_cert_path, 1);

    setenv(ca_root_path_env_key.c_str(), ca_root_cert_path.c_str(), 1);

    grpc::experimental::TlsServerCredentialsOptions options(
        certificate_provider);
    options.watch_root_certs();
    options.set_root_cert_name("root_cert_name");

    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("identity_cert_name");

    builder.AddListeningPort(absl::StrCat(mock_server_address_, ":0"),
                             grpc::experimental::TlsServerCredentials(options),
                             &chosen_port_);
    builder.RegisterService(service_.get());
    server_ = builder.BuildAndStart();
    server_thread_ = std::thread(&MockServerThread::RunServerLoop, this);
  }

  ~MockServerThread() {
    server_->Shutdown();
    unsetenv(ca_root_path_env_key.c_str());
    server_thread_.join();
  }

  std::string GetServerAddr() {
    std::string addr = absl::StrCat(mock_server_address_, ":", chosen_port_);
    return addr;
  }

 private:
  void RunServerLoop() { server_->Wait(); }

  std::string ca_root_path_env_key = "GRPC_DEFAULT_SSL_ROOTS_FILE_PATH";
  std::string ca_root_cert_path =
      "services/common/test/artifacts/grpc_tls/root_certificate_authority.pem";
  std::string ca_private_key_path =
      "services/common/test/artifacts/grpc_tls/localhost.key";
  std::string ca_private_cert_path =
      "services/common/test/artifacts/grpc_tls/localhost.pem";
  std::unique_ptr<ServiceMock> service_;
  std::unique_ptr<grpc::Server> server_;
  std::thread server_thread_;
  absl::string_view mock_server_address_;
  int chosen_port_;
};

template <typename Params, typename Provision>
class MockAsyncProvider : public AsyncProvider<Params, Provision> {
 public:
  MOCK_METHOD(
      void, Get,
      (const Params& params,
       absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Provision>>) &&>
           on_done,
       absl::Duration timeout),
      (const, override));
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_COMMON_TEST_MOCKS_H_
