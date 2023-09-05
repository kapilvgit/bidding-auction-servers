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

#include "services/common/clients/async_grpc/default_async_grpc_client.h"

#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kKeyId[] = "key_id";
constexpr char kSecret[] = "secret";
using ::testing::AnyNumber;

void SetupMockCryptoClientWrapper(MockCryptoClientWrapper& crypto_client) {
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .Times(testing::AnyNumber())
      .WillOnce(
          [](const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
             const std::string& plaintext_payload) {
            google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
                hpke_encrypt_response;
            hpke_encrypt_response.set_secret(kSecret);
            hpke_encrypt_response.mutable_encrypted_data()->set_key_id(kKeyId);
            hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
                plaintext_payload);
            return hpke_encrypt_response;
          });

  // Mock the HpkeDecrypt() call on the crypto_client. This is used by the
  // service to decrypt the incoming request.
  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .Times(AnyNumber())
      .WillRepeatedly([](const server_common::PrivateKey& private_key,
                         const std::string& ciphertext) {
        google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
            hpke_decrypt_response;
        hpke_decrypt_response.set_payload(ciphertext);
        hpke_decrypt_response.set_secret(kSecret);
        return hpke_decrypt_response;
      });

  // Mock the AeadEncrypt() call on the crypto_client. This is used to encrypt
  // the response coming back from the service.
  EXPECT_CALL(crypto_client, AeadEncrypt)
      .Times(AnyNumber())
      .WillOnce(
          [](const std::string& plaintext_payload, const std::string& secret) {
            google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
            data.set_ciphertext(plaintext_payload);
            google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
                aead_encrypt_response;
            *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
            return aead_encrypt_response;
          });
}

// The structs below have some definitions to mimic protos.
// default_async_grpc_client expects these fields to exist on the template types
// provided during instantiation.
struct MockRawRequest {
  std::string value;
  std::string SerializeAsString() { return value; }
  std::string DebugString() { return value; }
};

struct MockRequest {
  std::string request_ciphertext() const { return request_ciphertext_; }
  void set_request_ciphertext(absl::string_view cipher) {
    request_ciphertext_ = cipher;
  }
  void set_key_id(absl::string_view key_id) {}

 private:
  std::string request_ciphertext_;
};

struct MockRawResponse {
  void ParseFromString(absl::string_view) {}
};

class MockResponse {
 public:
  int value;
  MockRawResponse raw_response;
  std::string ciphertext;

  MockRawResponse* mutable_raw_response() { return &raw_response; }
  void clear_response_ciphertext() {}
  std::string& response_ciphertext() { return ciphertext; }
};

class TestDefaultAsyncGrpcClient
    : public DefaultAsyncGrpcClient<MockRequest, MockResponse, MockRawRequest,
                                    MockRawResponse> {
 public:
  TestDefaultAsyncGrpcClient(
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client, bool encryption_enabled,
      absl::Notification& notification, MockRequest req,
      absl::Duration expected_timeout)
      : DefaultAsyncGrpcClient(key_fetcher_manager, crypto_client,
                               encryption_enabled),
        notification_(notification),
        req_(req),
        expected_timeout_(expected_timeout) {}

  void SendRpc(const std::string& hpke_secret,
               RawClientParams<MockRequest, MockResponse, MockRawResponse>*
                   params) const override {
    VLOG(1) << "SendRpc invoked";
    EXPECT_EQ(params->RequestRef()->request_ciphertext(),
              req_.request_ciphertext());
    absl::Duration actual_timeout =
        absl::FromChrono(params->ContextRef()->deadline()) - absl::Now();

    int64_t time_difference_ms =
        ToInt64Milliseconds(actual_timeout - expected_timeout_);
    // Time diff in ms is expected due to different invocations of absl::Now(),
    // but should within a small range.
    EXPECT_GE(time_difference_ms, 0);
    EXPECT_LE(time_difference_ms, 100);
    notification_.Notify();
    params->OnDone(grpc::Status::OK);
  }
  absl::Notification& notification_;
  MockRequest req_;
  absl::Duration expected_timeout_;
};

TEST(TestDefaultAsyncGrpcClient, SendsMessageWithCorrectParams) {
  absl::Notification notification;
  MockRequest req;
  RequestMetadata metadata = MakeARandomMap();
  MockRawRequest raw_request;
  raw_request.value = "test";
  req.set_request_ciphertext(raw_request.SerializeAsString());

  absl::Duration timeout_ms = absl::Milliseconds(100);
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  SetupMockCryptoClientWrapper(*crypto_client);
  TrustedServersConfigClient config_client({});
  config_client.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config_client.SetFlagForTest(kTrue, TEST_MODE);
  auto key_fetcher_manager = CreateKeyFetcherManager(config_client);
  TestDefaultAsyncGrpcClient client(key_fetcher_manager.get(),
                                    crypto_client.get(), true, notification,
                                    req, timeout_ms);

  client.ExecuteInternal(
      std::make_unique<MockRawRequest>(raw_request), metadata,
      [](absl::StatusOr<std::unique_ptr<MockRawResponse>> result) {},
      timeout_ms);
  notification.WaitForNotification();
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
