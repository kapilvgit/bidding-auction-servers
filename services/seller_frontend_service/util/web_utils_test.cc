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

#include "services/seller_frontend_service/util/web_utils.h"

#include <set>
#include <utility>
#include <vector>

#include <google/protobuf/util/message_differencer.h>
#include <include/gmock/gmock-actions.h>

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "glog/logging.h"
#include "services/common/compression/gzip.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/util/context_logger.h"
#include "services/common/util/error_accumulator.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/scoped_cbor.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline constexpr char kSamplePublisher[] = "foo_publisher";
inline constexpr char kSampleIgName[] = "foo_ig_name";
inline constexpr char kSampleBiddingSignalKey1[] = "bidding_signal_key_1";
inline constexpr char kSampleBiddingSignalKey2[] = "bidding_signal_key_2";
inline constexpr char kSampleAdRenderId1[] = "ad_render_id_1";
inline constexpr char kSampleAdRenderId2[] = "ad_render_id_2";
inline constexpr char kSampleAdComponentRenderId1[] =
    "ad_component_render_id_1";
inline constexpr char kSampleAdComponentRenderId2[] =
    "ad_component_render_id_2";
inline constexpr char kSampleUserBiddingSignals[] =
    R"(["this should be a", "JSON array", "that gets parsed", "into separate list values"])";
inline constexpr int kSampleJoinCount = 1;
inline constexpr int kSampleBidCount = 2;
inline constexpr int kSampleRecency = 3;
inline constexpr char kSampleIgOwner[] = "foo_owner";
inline constexpr char kSampleGenerationId[] =
    "6fa459ea-ee8a-3ca4-894e-db77e160355e";
inline constexpr char kSampleErrorMessage[] = "BadError";
inline constexpr int32_t kSampleErrorCode = 400;
inline constexpr char kTestEvent1[] = "click";
inline constexpr char kTestInteractionUrl1[] = "http://click.com";
inline constexpr char kTestEvent2[] = "scroll";
inline constexpr char kTestInteractionUrl2[] = "http://scroll.com";
inline constexpr char kTestEvent3[] = "close";
inline constexpr char kTestInteractionUrl3[] = "http://close.com";
inline constexpr char kTestReportResultUrl[] = "http://reportResult.com";
inline constexpr char kTestReportWinUrl[] = "http://reportWin.com";
inline constexpr char kConsentedDebugToken[] = "xyz";

using ErrorVisibility::CLIENT_VISIBLE;
using BiddingGroupMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using InteractionUrlMap = ::google::protobuf::Map<std::string, std::string>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;

cbor_pair BuildStringMapPair(absl::string_view key, absl::string_view value) {
  return {cbor_move(cbor_build_stringn(key.data(), key.size())),
          cbor_move(cbor_build_stringn(value.data(), value.size()))};
}

cbor_pair BuildBoolMapPair(absl::string_view key, bool value) {
  return {cbor_move(cbor_build_stringn(key.data(), key.size())),
          cbor_move(cbor_build_bool(value))};
}

cbor_pair BuildBytestringMapPair(absl::string_view key,
                                 absl::string_view value) {
  cbor_data data = reinterpret_cast<const unsigned char*>(value.data());
  cbor_item_t* bytestring = cbor_build_bytestring(data, value.size());
  return {cbor_move(cbor_build_stringn(key.data(), key.size())),
          cbor_move(bytestring)};
}

cbor_pair BuildIntMapPair(absl::string_view key, uint32_t value) {
  return {cbor_move(cbor_build_stringn(key.data(), key.size())),
          cbor_move(cbor_build_uint64(value))};
}

cbor_pair BuildStringArrayMapPair(
    absl::string_view key, const std::vector<absl::string_view>& values) {
  cbor_item_t* array = cbor_new_definite_array(values.size());
  for (auto const& value : values) {
    cbor_item_t* array_entry = cbor_build_stringn(value.data(), value.size());
    EXPECT_TRUE(cbor_array_push(array, cbor_move(array_entry)));
  }

  return {cbor_move(cbor_build_stringn(key.data(), key.size())),
          cbor_move(array)};
}

struct cbor_item_t* BuildSampleCborInterestGroup() {
  cbor_item_t* interest_group = cbor_new_definite_map(kNumInterestGroupKeys);
  EXPECT_TRUE(
      cbor_map_add(interest_group, BuildStringMapPair(kName, kSampleIgName)));
  EXPECT_TRUE(cbor_map_add(
      interest_group, BuildStringArrayMapPair(kBiddingSignalsKeys,
                                              {kSampleBiddingSignalKey1,
                                               kSampleBiddingSignalKey2})));
  EXPECT_TRUE(cbor_map_add(
      interest_group,
      BuildStringMapPair(kUserBiddingSignals, kSampleUserBiddingSignals)));
  EXPECT_TRUE(cbor_map_add(
      interest_group,
      BuildStringArrayMapPair(kAdComponents, {kSampleAdComponentRenderId1,
                                              kSampleAdComponentRenderId2})));
  EXPECT_TRUE(cbor_map_add(
      interest_group,
      BuildStringArrayMapPair(kAds, {kSampleAdRenderId1, kSampleAdRenderId2})));

  cbor_item_t* browser_signals = cbor_new_definite_map(kNumBrowserSignalKeys);
  EXPECT_TRUE(cbor_map_add(browser_signals,
                           BuildIntMapPair(kJoinCount, kSampleJoinCount)));
  EXPECT_TRUE(cbor_map_add(browser_signals,
                           BuildIntMapPair(kBidCount, kSampleBidCount)));
  EXPECT_TRUE(
      cbor_map_add(browser_signals, BuildIntMapPair(kRecency, kSampleRecency)));

  // Build prevWins arrays.
  cbor_item_t* child_arr_1 = cbor_new_definite_array(2);
  EXPECT_TRUE(cbor_array_push(child_arr_1, cbor_move(cbor_build_uint64(-20))));
  EXPECT_TRUE(cbor_array_push(
      child_arr_1, cbor_move(cbor_build_stringn(
                       kSampleAdRenderId1, sizeof(kSampleAdRenderId1) - 1))));
  cbor_item_t* child_arr_2 = cbor_new_definite_array(2);
  EXPECT_TRUE(cbor_array_push(child_arr_2, cbor_move(cbor_build_uint64(-100))));
  EXPECT_TRUE(cbor_array_push(
      child_arr_2, cbor_move(cbor_build_stringn(
                       kSampleAdRenderId2, sizeof(kSampleAdRenderId2) - 1))));

  cbor_item_t* parent_arr = cbor_new_definite_array(2);
  EXPECT_TRUE(cbor_array_push(parent_arr, cbor_move(child_arr_1)));
  EXPECT_TRUE(cbor_array_push(parent_arr, cbor_move(child_arr_2)));

  EXPECT_TRUE(cbor_map_add(
      browser_signals,
      {cbor_move(cbor_build_stringn(kPrevWins, sizeof(kPrevWins) - 1)),
       cbor_move(parent_arr)}));
  cbor_item_t* browser_signals_key =
      cbor_build_stringn(kBrowserSignals, sizeof(kBrowserSignals) - 1);
  EXPECT_TRUE(cbor_map_add(interest_group, {cbor_move(browser_signals_key),
                                            cbor_move(browser_signals)}));
  return cbor_move(interest_group);
}

cbor_item_t* CompressInterestGroups(const ScopedCbor& ig_array) {
  std::string serialized_ig_array = SerializeCbor(*ig_array);
  absl::StatusOr<std::string> compressed_ig = GzipCompress(serialized_ig_array);
  cbor_data my_cbor_data =
      reinterpret_cast<const unsigned char*>(compressed_ig->data());
  return cbor_build_bytestring(my_cbor_data, compressed_ig->size());
}

bool ContainsClientError(const ErrorAccumulator::ErrorMap& error_map,
                         absl::string_view target) {
  auto it = error_map.find(ErrorCode::CLIENT_SIDE);
  if (it == error_map.end()) {
    return false;
  }

  for (const auto& observed_err : it->second) {
    if (absl::StrContains(observed_err, target)) {
      return true;
    }
  }

  return false;
}

TEST(ChromeRequestUtils, Decode_Success) {
  ScopedCbor protected_auction_input(
      cbor_new_definite_map(kNumRequestRootKeys));
  EXPECT_TRUE(cbor_map_add(*protected_auction_input,
                           BuildStringMapPair(kPublisher, kSamplePublisher)));
  EXPECT_TRUE(
      cbor_map_add(*protected_auction_input,
                   BuildStringMapPair(kGenerationId, kSampleGenerationId)));
  EXPECT_TRUE(cbor_map_add(*protected_auction_input,
                           BuildBoolMapPair(kDebugReporting, true)));

  ScopedCbor ig_array(cbor_new_definite_array(1));
  EXPECT_TRUE(cbor_array_push(*ig_array, BuildSampleCborInterestGroup()));
  cbor_item_t* ig_bytestring = CompressInterestGroups(ig_array);

  cbor_item_t* interest_group_data_map = cbor_new_definite_map(1);
  EXPECT_TRUE(cbor_map_add(interest_group_data_map,
                           {cbor_move(cbor_build_stringn(
                                kSampleIgOwner, sizeof(kSampleIgOwner) - 1)),
                            cbor_move(ig_bytestring)}));
  EXPECT_TRUE(cbor_map_add(*protected_auction_input,
                           {cbor_move(cbor_build_stringn(
                                kInterestGroups, sizeof(kInterestGroups) - 1)),
                            cbor_move(interest_group_data_map)}));

  cbor_item_t* consented_debug_config_map =
      cbor_new_definite_map(kNumConsentedDebugConfigKeys);
  EXPECT_TRUE(cbor_map_add(consented_debug_config_map,
                           BuildBoolMapPair(kIsConsented, true)));
  EXPECT_TRUE(cbor_map_add(consented_debug_config_map,
                           BuildStringMapPair(kToken, kConsentedDebugToken)));
  EXPECT_TRUE(cbor_map_add(
      *protected_auction_input,
      {cbor_move(cbor_build_stringn(kConsentedDebugConfig,
                                    sizeof(kConsentedDebugConfig) - 1)),
       cbor_move(consented_debug_config_map)}));

  std::string serialized_cbor = SerializeCbor(*protected_auction_input);
  ContextLogger logger;
  ErrorAccumulator error_accumulator(&logger);
  ProtectedAuctionInput actual =
      Decode<ProtectedAuctionInput>(serialized_cbor, error_accumulator);
  ASSERT_FALSE(error_accumulator.HasErrors());

  ProtectedAuctionInput expected;
  expected.set_publisher_name(kSamplePublisher);
  expected.set_enable_debug_reporting(true);
  expected.set_generation_id(kSampleGenerationId);

  BuyerInput::InterestGroup expected_ig;
  expected_ig.set_name(kSampleIgName);
  expected_ig.add_bidding_signals_keys(kSampleBiddingSignalKey1);
  expected_ig.add_bidding_signals_keys(kSampleBiddingSignalKey2);
  expected_ig.add_ad_render_ids(kSampleAdRenderId1);
  expected_ig.add_ad_render_ids(kSampleAdRenderId2);
  expected_ig.add_component_ads(kSampleAdComponentRenderId1);
  expected_ig.add_component_ads(kSampleAdComponentRenderId2);
  expected_ig.set_user_bidding_signals(kSampleUserBiddingSignals);

  BrowserSignals* signals = expected_ig.mutable_browser_signals();
  signals->set_join_count(kSampleJoinCount);
  signals->set_bid_count(kSampleBidCount);
  signals->set_recency(kSampleRecency);
  std::string prev_wins_json_str = absl::StrFormat(
      R"([[-20,"%s"],[-100,"%s"]])", kSampleAdRenderId1, kSampleAdRenderId2);
  signals->set_prev_wins(prev_wins_json_str);

  BuyerInput buyer_input;
  *buyer_input.add_interest_groups() = expected_ig;
  expected_ig.mutable_browser_signals()->set_prev_wins(prev_wins_json_str);
  google::protobuf::Map<std::string, BuyerInput> buyer_inputs;
  buyer_inputs.emplace(kSampleIgOwner, std::move(buyer_input));

  absl::StatusOr<EncodedBuyerInputs> encoded_buyer_inputs =
      GetEncodedBuyerInputMap(buyer_inputs);
  ASSERT_TRUE(encoded_buyer_inputs.ok()) << encoded_buyer_inputs.status();
  *expected.mutable_buyer_input() = std::move(*encoded_buyer_inputs);

  ConsentedDebugConfiguration consented_debug_config;
  consented_debug_config.set_is_consented(true);
  consented_debug_config.set_token(kConsentedDebugToken);
  *expected.mutable_consented_debug_config() =
      std::move(consented_debug_config);

  std::string papi_differences;
  google::protobuf::util::MessageDifferencer papi_differencer;
  papi_differencer.ReportDifferencesToString(&papi_differences);
  // Note that this comparison is fragile because the CBOR encoding depends
  // on the order in which the data was created and if we have a difference
  // between the order in which the data items are added in the test vs how
  // they are added in the implementation, we will start to see failures.
  if (!papi_differencer.Compare(actual, expected)) {
    VLOG(1) << "Actual proto does not match expected proto";
    VLOG(1) << "\nExpected:\n" << expected.DebugString();
    VLOG(1) << "\nActual:\n" << actual.DebugString();
    VLOG(1) << "\nFound differences in ProtectedAuctionInput:\n"
            << papi_differences;

    auto expected_buyer_inputs =
        DecodeBuyerInputs(expected.buyer_input(), error_accumulator);
    ASSERT_FALSE(error_accumulator.HasErrors());
    ASSERT_EQ(expected_buyer_inputs.size(), 1);
    const auto& [expected_buyer, expected_buyer_input] =
        *expected_buyer_inputs.begin();

    std::string bi_differences;
    google::protobuf::util::MessageDifferencer bi_differencer;
    bi_differencer.ReportDifferencesToString(&bi_differences);
    VLOG(1) << "\nExpected BuyerInput:\n" << expected_buyer_input.DebugString();
    BuyerInput actual_buyer_input =
        DecodeBuyerInputs(actual.buyer_input(), error_accumulator)
            .begin()
            ->second;
    VLOG(1) << "\nActual BuyerInput:\n" << actual_buyer_input.DebugString();
    EXPECT_TRUE(
        !bi_differencer.Compare(actual_buyer_input, expected_buyer_input))
        << bi_differences;
    FAIL();
  }
}

TEST(ChromeRequestUtils, Decode_FailOnWrongType) {
  ScopedCbor root(cbor_build_stringn("string", 6));
  std::string serialized_cbor = SerializeCbor(*root);
  ContextLogger logger;
  ErrorAccumulator error_accumulator(&logger);
  ProtectedAuctionInput actual =
      Decode<ProtectedAuctionInput>(serialized_cbor, error_accumulator);
  ASSERT_TRUE(error_accumulator.HasErrors());

  const std::string expected_error =
      absl::StrFormat(kInvalidTypeError, kProtectedAuctionInput, kMap, kString);
  EXPECT_TRUE(ContainsClientError(error_accumulator.GetErrors(CLIENT_VISIBLE),
                                  expected_error));
}

TEST(ChromeRequestUtils, Decode_FailOnUnsupportedVersion) {
  ScopedCbor protected_auction_input(cbor_new_definite_map(1));
  EXPECT_TRUE(
      cbor_map_add(*protected_auction_input, BuildIntMapPair(kVersion, 999)));
  std::string serialized_cbor = SerializeCbor(*protected_auction_input);
  ContextLogger logger;
  ErrorAccumulator error_accumulator(&logger);
  ProtectedAuctionInput actual =
      Decode<ProtectedAuctionInput>(serialized_cbor, error_accumulator);
  ASSERT_TRUE(error_accumulator.HasErrors());

  const std::string expected_error =
      absl::StrFormat(kUnsupportedSchemaVersionError, 999);
  EXPECT_TRUE(ContainsClientError(error_accumulator.GetErrors(CLIENT_VISIBLE),
                                  expected_error));
}

TEST(ChromeRequestUtils, Decode_FailOnMalformedCompresedBytestring) {
  ScopedCbor protected_auction_input(cbor_new_definite_map(1));
  ScopedCbor ig_array(cbor_new_definite_array(1));
  EXPECT_TRUE(cbor_array_push(*ig_array, BuildSampleCborInterestGroup()));
  ScopedCbor ig_bytestring = ScopedCbor(CompressInterestGroups(ig_array));

  // Cut the compressed string in half and try that.
  std::string compressed_split(reinterpret_cast<char*>(ig_bytestring->data),
                               cbor_bytestring_length(*ig_bytestring) / 2);

  cbor_item_t* interest_group_data_map = cbor_new_definite_map(1);
  EXPECT_TRUE(
      cbor_map_add(interest_group_data_map,
                   BuildBytestringMapPair(kSampleIgOwner, compressed_split)));
  EXPECT_TRUE(cbor_map_add(*protected_auction_input,
                           {cbor_move(cbor_build_stringn(
                                kInterestGroups, sizeof(kInterestGroups) - 1)),
                            cbor_move(interest_group_data_map)}));

  std::string serialized_cbor = SerializeCbor(*protected_auction_input);
  ContextLogger logger;
  ErrorAccumulator error_accumulator(&logger);
  // The main decoding method for protected audience input doesn't decompress
  // and decode the BuyerInput. The latter is handled separately.
  ProtectedAuctionInput actual =
      Decode<ProtectedAuctionInput>(serialized_cbor, error_accumulator);
  ASSERT_FALSE(error_accumulator.HasErrors());

  absl::flat_hash_map<absl::string_view, BuyerInput> buyer_inputs =
      DecodeBuyerInputs(actual.buyer_input(), error_accumulator);
  ASSERT_TRUE(error_accumulator.HasErrors());
  EXPECT_TRUE(ContainsClientError(error_accumulator.GetErrors(CLIENT_VISIBLE),
                                  kMalformedCompressedBytestring));
}

TEST(ChromeResponseUtils, VerifyBiddingGroupBuyerOriginOrdering) {
  const std::string interest_group_owner_1 = "ig1";
  const std::string interest_group_owner_2 = "zi";
  const std::string interest_group_owner_3 = "ih1";
  AuctionResult::InterestGroupIndex interest_group_index;
  interest_group_index.add_index(1);
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  bidding_group_map.try_emplace(interest_group_owner_1, interest_group_index);
  bidding_group_map.try_emplace(interest_group_owner_2, interest_group_index);
  bidding_group_map.try_emplace(interest_group_owner_3, interest_group_index);

  // Convert the bidding group map to CBOR.
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumAuctionResultKeys));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](absl::string_view err_msg) {};
  auto result = CborSerializeBiddingGroups(bidding_group_map, err_handler,
                                           *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;

  // Decode the produced CBOR and collect the origins for verification.
  std::vector<std::string> observed_origins;
  absl::Span<struct cbor_pair> outer_map(cbor_map_handle(cbor_internal),
                                         cbor_map_size(cbor_internal));
  ASSERT_EQ(outer_map.size(), 1);
  auto& bidding_group_val = outer_map.begin()->value;
  ASSERT_TRUE(cbor_isa_map(bidding_group_val));
  absl::Span<struct cbor_pair> group_entries(cbor_map_handle(bidding_group_val),
                                             cbor_map_size(bidding_group_val));
  for (const auto& kv : group_entries) {
    ASSERT_TRUE(cbor_isa_string(kv.key)) << "Expected the key to be a string";
    observed_origins.emplace_back(
        reinterpret_cast<char*>(cbor_string_handle(kv.key)),
        cbor_string_length(kv.key));
  }

  // We expect the shorter length keys to be present first followed. Ties on
  // length are then broken by lexicographic order.
  ASSERT_EQ(observed_origins.size(), 3);
  EXPECT_EQ(observed_origins[0], interest_group_owner_2);
  EXPECT_EQ(observed_origins[1], interest_group_owner_1);
  EXPECT_EQ(observed_origins[2], interest_group_owner_3);
}
void TestReportAndInteractionUrls(const struct cbor_pair reporting_data,
                                  absl::string_view reporting_url_key,
                                  absl::string_view expected_report_url) {
  ASSERT_TRUE(cbor_isa_string(reporting_data.key))
      << "Expected the key to be a string";
  EXPECT_EQ(reporting_url_key, CborDecodeString(reporting_data.key));
  auto& outer_map = reporting_data.value;
  absl::Span<struct cbor_pair> reporting_urls_map(cbor_map_handle(outer_map),
                                                  cbor_map_size(outer_map));
  std::vector<std::string> observed_keys;
  for (const auto& kv : reporting_urls_map) {
    ASSERT_TRUE(cbor_isa_string(kv.key)) << "Expected the key to be a string";
    observed_keys.emplace_back(CborDecodeString(kv.key));
  }
  EXPECT_EQ(observed_keys[1], kInteractionReportingUrls);
  EXPECT_EQ(observed_keys[0], kReportingUrl);
  EXPECT_EQ(expected_report_url,
            CborDecodeString(reporting_urls_map.at(0).value));
  auto& interaction_map = reporting_urls_map.at(1).value;
  ASSERT_TRUE(cbor_isa_map(interaction_map));
  absl::Span<struct cbor_pair> interaction_urls(
      cbor_map_handle(interaction_map), cbor_map_size(interaction_map));
  ASSERT_EQ(interaction_urls.size(), 3);
  std::vector<std::string> observed_events;
  std::vector<std::string> observed_urls;
  for (const auto& kv : interaction_urls) {
    ASSERT_TRUE(cbor_isa_string(kv.key)) << "Expected the key to be a string";
    ASSERT_TRUE(cbor_isa_string(kv.value)) << "Expected the key to be a string";
    observed_events.emplace_back(CborDecodeString(kv.key));
    observed_urls.emplace_back(CborDecodeString(kv.value));
  }
  EXPECT_EQ(kTestEvent1, observed_events.at(0));
  EXPECT_EQ(kTestInteractionUrl1, observed_urls.at(0));
  EXPECT_EQ(kTestEvent3, observed_events.at(1));
  EXPECT_EQ(kTestInteractionUrl3, observed_urls.at(1));
  EXPECT_EQ(kTestEvent2, observed_events.at(2));
  EXPECT_EQ(kTestInteractionUrl2, observed_urls.at(2));
}

void TestReportingUrl(const struct cbor_pair reporting_data,
                      absl::string_view reporting_url_key,
                      absl::string_view expected_report_url) {
  ASSERT_TRUE(cbor_isa_string(reporting_data.key))
      << "Expected the key to be a string";
  EXPECT_EQ(reporting_url_key, CborDecodeString(reporting_data.key));
  auto& outer_map = reporting_data.value;
  absl::Span<struct cbor_pair> reporting_urls_map(cbor_map_handle(outer_map),
                                                  cbor_map_size(outer_map));
  std::vector<std::string> observed_keys;
  for (const auto& kv : reporting_urls_map) {
    ASSERT_TRUE(cbor_isa_string(kv.key)) << "Expected the key to be a string";
    observed_keys.emplace_back(CborDecodeString(kv.key));
  }
  EXPECT_EQ(observed_keys[0], kReportingUrl);
  EXPECT_EQ(expected_report_url,
            CborDecodeString(reporting_urls_map.at(0).value));
}

TEST(ChromeResponseUtils, CborSerializeWinReportingUrls) {
  InteractionUrlMap interaction_url_map;
  interaction_url_map.try_emplace(kTestEvent1, kTestInteractionUrl1);
  interaction_url_map.try_emplace(kTestEvent2, kTestInteractionUrl2);
  interaction_url_map.try_emplace(kTestEvent3, kTestInteractionUrl3);
  WinReportingUrls win_reporting_urls;
  win_reporting_urls.mutable_buyer_reporting_urls()->set_reporting_url(
      kTestReportWinUrl);
  win_reporting_urls.mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  win_reporting_urls.mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent2, kTestInteractionUrl2);
  win_reporting_urls.mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent3, kTestInteractionUrl3);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent2, kTestInteractionUrl2);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent3, kTestInteractionUrl3);

  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumWinReportingUrlsKeys));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](absl::string_view err_msg) {};
  auto result = CborSerializeWinReportingUrls(win_reporting_urls, err_handler,
                                              *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;

  // Decode the produced CBOR and collect the origins for verification.
  std::vector<std::string> observed_keys;
  absl::Span<struct cbor_pair> outer_map(cbor_map_handle(cbor_internal),
                                         cbor_map_size(cbor_internal));
  ASSERT_EQ(outer_map.size(), 1);
  auto& report_win_urls_map = outer_map.at(0).value;
  ASSERT_TRUE(cbor_isa_map(report_win_urls_map));
  absl::Span<struct cbor_pair> inner_map(cbor_map_handle(report_win_urls_map),
                                         cbor_map_size(report_win_urls_map));
  ASSERT_EQ(inner_map.size(), 2);
  TestReportAndInteractionUrls(inner_map.at(0), kBuyerReportingUrls,
                               kTestReportWinUrl);
  TestReportAndInteractionUrls(inner_map.at(1), kTopLevelSellerReportingUrls,
                               kTestReportResultUrl);
}

TEST(ChromeResponseUtils, NoCborGeneratedWithEmptyWinReportingUrl) {
  WinReportingUrls win_reporting_urls;
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumWinReportingUrlsKeys));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](absl::string_view err_msg) {};
  auto result = CborSerializeWinReportingUrls(win_reporting_urls, err_handler,
                                              *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;

  // Decode the produced CBOR and collect the origins for verification.
  std::vector<std::string> observed_keys;
  absl::Span<struct cbor_pair> outer_map(cbor_map_handle(cbor_internal),
                                         cbor_map_size(cbor_internal));
  ASSERT_EQ(outer_map.size(), 0);
}

TEST(ChromeResponseUtils, CborWithOnlySellerReprotingUrls) {
  InteractionUrlMap interaction_url_map;
  interaction_url_map.try_emplace(kTestEvent1, kTestInteractionUrl1);
  interaction_url_map.try_emplace(kTestEvent2, kTestInteractionUrl2);
  interaction_url_map.try_emplace(kTestEvent3, kTestInteractionUrl3);
  WinReportingUrls win_reporting_urls;
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent2, kTestInteractionUrl2);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent3, kTestInteractionUrl3);

  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumWinReportingUrlsKeys));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](absl::string_view err_msg) {};
  auto result = CborSerializeWinReportingUrls(win_reporting_urls, err_handler,
                                              *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;

  // Decode the produced CBOR and collect the origins for verification.
  std::vector<std::string> observed_keys;
  absl::Span<struct cbor_pair> outer_map(cbor_map_handle(cbor_internal),
                                         cbor_map_size(cbor_internal));
  ASSERT_EQ(outer_map.size(), 1);
  auto& report_win_urls_map = outer_map.at(0).value;
  ASSERT_TRUE(cbor_isa_map(report_win_urls_map));
  absl::Span<struct cbor_pair> inner_map(cbor_map_handle(report_win_urls_map),
                                         cbor_map_size(report_win_urls_map));
  ASSERT_EQ(inner_map.size(), 1);
  TestReportAndInteractionUrls(inner_map.at(0), kTopLevelSellerReportingUrls,
                               kTestReportResultUrl);
}

TEST(ChromeResponseUtils, CborWithNoInteractionReportingUrls) {
  WinReportingUrls win_reporting_urls;
  win_reporting_urls.mutable_buyer_reporting_urls()->set_reporting_url(
      kTestReportWinUrl);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumWinReportingUrlsKeys));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](absl::string_view err_msg) {};
  auto result = CborSerializeWinReportingUrls(win_reporting_urls, err_handler,
                                              *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;

  // Decode the produced CBOR and collect the origins for verification.
  std::vector<std::string> observed_keys;
  absl::Span<struct cbor_pair> outer_map(cbor_map_handle(cbor_internal),
                                         cbor_map_size(cbor_internal));
  ASSERT_EQ(outer_map.size(), 1);
  auto& report_win_urls_map = outer_map.at(0).value;
  ASSERT_TRUE(cbor_isa_map(report_win_urls_map));
  absl::Span<struct cbor_pair> inner_map(cbor_map_handle(report_win_urls_map),
                                         cbor_map_size(report_win_urls_map));
  ASSERT_EQ(inner_map.size(), 2);
  TestReportingUrl(inner_map.at(0), kBuyerReportingUrls, kTestReportWinUrl);
  TestReportingUrl(inner_map.at(1), kTopLevelSellerReportingUrls,
                   kTestReportResultUrl);
}

TEST(ChromeResponseUtils, VerifyCborEncoding) {
  // Setup a winning bid.
  const std::string interest_group = "interest_group";
  const std::string ad_render_url = "https://ad-found-here.com/ad-1";
  const std::string interest_group_owner = "https://ig-owner.com:1234";
  const int ig_index = 2;
  const float bid = 10.21;
  const float desirability = 2.35;
  ScoreAdsResponse::AdScore winner;
  winner.set_render(ad_render_url);
  winner.set_desirability(desirability);
  winner.set_buyer_bid(bid);
  winner.set_interest_group_name(interest_group);
  winner.set_interest_group_owner(interest_group_owner);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->set_reporting_url(kTestReportWinUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  // Setup a bidding group map.
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  AuctionResult::InterestGroupIndex ig_indices;
  ig_indices.add_index(ig_index);
  bidding_group_map.try_emplace(interest_group_owner, std::move(ig_indices));

  auto response_with_cbor =
      Encode(winner, std::move(bidding_group_map), /*error=*/std::nullopt,
             [](absl::string_view error) {});
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  VLOG(1) << "Encoded CBOR: " << absl::BytesToHexString(*response_with_cbor);
  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  // Verify that the decoded result has all the bidding groups correctly set.
  EXPECT_EQ(decoded_result->bidding_groups().size(), 1);
  const auto& [observed_owner, observed_ig_indices] =
      *decoded_result->bidding_groups().begin();
  EXPECT_EQ(observed_owner, interest_group_owner);
  EXPECT_EQ(observed_ig_indices.index_size(), 1);
  EXPECT_EQ(observed_ig_indices.index(0), ig_index);

  // Verify that the decoded result has the winning ad correctly set.
  EXPECT_EQ(decoded_result->ad_render_url(), ad_render_url);
  EXPECT_TRUE(AreFloatsEqual(decoded_result->bid(), bid))
      << " Actual: " << decoded_result->bid() << ", Expected: " << bid;
  EXPECT_TRUE(AreFloatsEqual(decoded_result->score(), desirability))
      << " Actual: " << decoded_result->score()
      << ", Expected: " << desirability;
  EXPECT_EQ(decoded_result->interest_group_name(), interest_group);
  EXPECT_EQ(decoded_result->interest_group_owner(), interest_group_owner);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .buyer_reporting_urls()
                .reporting_url(),
            kTestReportWinUrl);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestEvent1),
            kTestInteractionUrl1);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestEvent1),
            kTestInteractionUrl1);
}

TEST(ChromeResponseUtils, VerifyCBOREncodedError) {
  ScoreAdsResponse::AdScore winner;
  // Setup a bidding group map.
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  AuctionResult::InterestGroupIndex ig_indices;

  AuctionResult::Error error;
  error.set_message(kSampleErrorMessage);
  error.set_code(kSampleErrorCode);
  auto response_with_cbor = Encode(winner, std::move(bidding_group_map), error,
                                   [](absl::string_view error) {});

  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();
  EXPECT_EQ(decoded_result->error().message(), kSampleErrorMessage);
  EXPECT_EQ(decoded_result->error().code(), kSampleErrorCode);
}

std::string ErrStr(absl::string_view field_name,
                   absl::string_view expected_type,
                   absl::string_view observed_type) {
  return absl::StrFormat(kInvalidTypeError, field_name, expected_type,
                         observed_type);
}

TEST(WebRequestUtils, Decode_FailsAndGetsAllErrors) {
  ScopedCbor protected_auction_input(
      cbor_new_definite_map(kNumRequestRootKeys));

  std::set<std::string> expected_errors;

  // Malformed key type for publisher.
  cbor_pair publisher_kv = {
      cbor_move(cbor_build_bytestring(reinterpret_cast<cbor_data>(kPublisher),
                                      sizeof(kPublisher) - 1)),
      cbor_move(
          cbor_build_stringn(kSamplePublisher, sizeof(kSamplePublisher) - 1))};
  EXPECT_TRUE(cbor_map_add(*protected_auction_input, std::move(publisher_kv)));
  expected_errors.emplace(ErrStr(/*field_name=*/kRootCborKey,
                                 /*expected_type=*/kCborTypeString,
                                 /*observed_type=*/kCborTypeByteString));

  // Malformed value type for generation id.
  cbor_pair gen_id_kv = {
      cbor_move(cbor_build_stringn(kGenerationId, sizeof(kGenerationId) - 1)),
      cbor_move(cbor_build_bytestring(
          reinterpret_cast<cbor_data>(kSampleGenerationId),
          sizeof(kSampleGenerationId) - 1))};
  EXPECT_TRUE(cbor_map_add(*protected_auction_input, std::move(gen_id_kv)));
  expected_errors.emplace(ErrStr(/*field_name=*/kGenerationId,
                                 /*expected_type=*/kCborTypeString,
                                 /*observed_type=*/kCborTypeByteString));

  ScopedCbor ig_array(cbor_new_definite_array(1));
  // Malformed interest group fields.
  cbor_item_t* interest_group = cbor_new_definite_map(kNumInterestGroupKeys);
  // Malformed interest group name field.
  cbor_pair ig_name_kv = {
      cbor_move(cbor_build_stringn(kName, sizeof(kName) - 1)),
      cbor_move(cbor_build_uint32(1))};
  EXPECT_TRUE(cbor_map_add(interest_group, std::move(ig_name_kv)));
  expected_errors.emplace(ErrStr(/*field_name=*/kIgName,
                                 /*expected_type=*/kCborTypeString,
                                 /*observed_type=*/kCborTypePositiveInt));
  // Malformed browser signals.
  cbor_item_t* browser_signals_array = cbor_new_definite_array(1);
  EXPECT_TRUE(
      cbor_array_push(browser_signals_array, cbor_move(cbor_build_uint32(1))));
  cbor_pair browser_signals_kv = {
      cbor_move(cbor_build_stringn(kBrowserSignalsKey,
                                   sizeof(kBrowserSignalsKey) - 1)),
      cbor_move(browser_signals_array)};
  EXPECT_TRUE(cbor_map_add(interest_group, std::move(browser_signals_kv)));
  expected_errors.emplace(ErrStr(/*field_name=*/kBrowserSignalsKey,
                                 /*expected_type=*/kCborTypeString,
                                 /*observed_type=*/kCborTypePositiveInt));
  // Malformed bidding signals.
  cbor_item_t* bidding_signals_array = cbor_new_definite_array(1);
  EXPECT_TRUE(
      cbor_array_push(bidding_signals_array, cbor_move(cbor_build_uint32(1))));
  cbor_pair bidding_signals_kv = {
      cbor_move(cbor_build_stringn(kBiddingSignalsKeys,
                                   sizeof(kBiddingSignalsKeys) - 1)),
      cbor_move(bidding_signals_array)};
  EXPECT_TRUE(cbor_map_add(interest_group, std::move(bidding_signals_kv)));
  expected_errors.emplace(ErrStr(/*field_name=*/kIgBiddingSignalKeysEntry,
                                 /*expected_type=*/kCborTypeString,
                                 /*observed_type=*/kCborTypePositiveInt));

  EXPECT_TRUE(cbor_array_push(*ig_array, cbor_move(interest_group)));
  cbor_item_t* ig_bytestring = CompressInterestGroups(ig_array);

  cbor_item_t* interest_group_data_map = cbor_new_definite_map(1);
  EXPECT_TRUE(cbor_map_add(interest_group_data_map,
                           {cbor_move(cbor_build_stringn(
                                kSampleIgOwner, sizeof(kSampleIgOwner) - 1)),
                            cbor_move(ig_bytestring)}));
  EXPECT_TRUE(cbor_map_add(*protected_auction_input,
                           {cbor_move(cbor_build_stringn(
                                kInterestGroups, sizeof(kInterestGroups) - 1)),
                            cbor_move(interest_group_data_map)}));

  std::string serialized_cbor = SerializeCbor(*protected_auction_input);
  ContextLogger logger;
  ErrorAccumulator error_accumulator(&logger);
  ProtectedAuctionInput decoded_protected_auction_input =
      Decode<ProtectedAuctionInput>(serialized_cbor, error_accumulator,
                                    /*fail_fast=*/false);
  ASSERT_TRUE(error_accumulator.HasErrors());
  VLOG(0) << "Decoded protected audience input:\n"
          << decoded_protected_auction_input.DebugString();

  // Verify all the errors were reported to the error accumulator.
  const auto& client_visible_errors =
      error_accumulator.GetErrors(ErrorVisibility::CLIENT_VISIBLE);
  ASSERT_FALSE(client_visible_errors.empty());
  auto observed_client_side_errors_it =
      client_visible_errors.find(ErrorCode::CLIENT_SIDE);
  ASSERT_NE(observed_client_side_errors_it, client_visible_errors.end());
  const auto& observed_client_side_errors =
      observed_client_side_errors_it->second;
  ASSERT_FALSE(observed_client_side_errors.empty());
  std::set<std::string> unexpected_errors;
  absl::c_set_difference(
      observed_client_side_errors, expected_errors,
      std::inserter(unexpected_errors, unexpected_errors.begin()));
  EXPECT_TRUE(unexpected_errors.empty())
      << "Found following unexpected errors were observed:\n"
      << absl::StrJoin(unexpected_errors, "\n");
}

absl::StatusOr<std::string> SerializeToCbor(cbor_item_t* cbor_data_root) {
  // Serialize the payload to CBOR.
  const size_t cbor_serialized_data_size = cbor_serialized_size(cbor_data_root);
  if (!cbor_serialized_data_size) {
    return absl::InternalError("Unable to serialize (data too large)");
  }

  std::vector<unsigned char> byte_string(cbor_serialized_data_size);
  if (cbor_serialize(cbor_data_root, byte_string.data(),
                     cbor_serialized_data_size) == 0) {
    return absl::InternalError("Failed to serialize to CBOR");
  }
  std::string out;
  for (const auto& val : byte_string) {
    out.append(absl::StrCat(absl::Hex(val, absl::kZeroPad2)));
  }
  return out;
}

TEST(ChromeResponseUtils, UintsAreCompactlyCborEncoded) {
  ScopedCbor single_byte_cbor(cbor_build_uint(23));
  auto single_byte = SerializeToCbor(*single_byte_cbor);
  ASSERT_TRUE(single_byte.ok()) << single_byte.status();
  EXPECT_EQ(*single_byte, "17");

  ScopedCbor two_bytes_cbor(cbor_build_uint(255));
  auto two_bytes = SerializeToCbor(*two_bytes_cbor);
  ASSERT_TRUE(two_bytes.ok()) << two_bytes.status();
  EXPECT_EQ(*two_bytes, "18ff");

  ScopedCbor three_bytes_cbor(cbor_build_uint(65535));
  auto three_bytes = SerializeToCbor(*three_bytes_cbor);
  ASSERT_TRUE(three_bytes.ok()) << three_bytes.status();
  EXPECT_EQ(*three_bytes, "19ffff");

  ScopedCbor five_bytes_cbor(cbor_build_uint(4294967295));
  auto five_bytes = SerializeToCbor(*five_bytes_cbor);
  ASSERT_TRUE(five_bytes.ok()) << five_bytes.status();
  EXPECT_EQ(*five_bytes, "1affffffff");
}

TEST(ChromeResponseUtils, FloatsAreCompactlyCborEncoded) {
  auto half_precision_cbor = cbor_build_float(0.0);
  ASSERT_TRUE(half_precision_cbor.ok()) << half_precision_cbor.status();
  auto half_precision = SerializeToCbor(*half_precision_cbor);
  cbor_decref(&*half_precision_cbor);
  ASSERT_TRUE(half_precision.ok()) << half_precision.status();
  EXPECT_EQ(*half_precision, "f90000");

  auto single_precision_cbor = cbor_build_float(std::pow(2.0, -24));
  ASSERT_TRUE(single_precision_cbor.ok()) << single_precision_cbor.status();
  auto single_precision = SerializeToCbor(*single_precision_cbor);
  cbor_decref(&*single_precision_cbor);
  ASSERT_TRUE(single_precision.ok()) << single_precision.status();
  EXPECT_EQ(*single_precision, "f90001");

  auto fine_precision_cbor = cbor_build_float(std::pow(2.0, -32));
  ASSERT_TRUE(fine_precision_cbor.ok()) << fine_precision_cbor.status();
  auto fine_precision = SerializeToCbor(*fine_precision_cbor);
  cbor_decref(&*fine_precision_cbor);
  EXPECT_EQ(*fine_precision, "fa2f800000");

  auto double_precision_cbor = cbor_build_float(0.16);
  ASSERT_TRUE(double_precision_cbor.ok()) << double_precision_cbor.status();
  auto double_precision = SerializeToCbor(*double_precision_cbor);
  cbor_decref(&*double_precision_cbor);
  ASSERT_TRUE(double_precision.ok()) << double_precision.status();
  EXPECT_EQ(*double_precision, "fb3fc47ae147ae147b");

  auto rand_cbor_1 = cbor_build_float(10.21);
  ASSERT_TRUE(rand_cbor_1.ok()) << rand_cbor_1.status();
  auto rand_1 = SerializeToCbor(*rand_cbor_1);
  cbor_decref(&*rand_cbor_1);
  ASSERT_TRUE(rand_1.ok()) << rand_1.status();
  EXPECT_EQ(*rand_1, "fb40246b851eb851ec");

  auto rand_cbor_2 = cbor_build_float(2.35);
  ASSERT_TRUE(rand_cbor_2.ok()) << rand_cbor_2.status();
  auto rand_2 = SerializeToCbor(*rand_cbor_2);
  cbor_decref(&*rand_cbor_2);
  ASSERT_TRUE(rand_2.ok()) << rand_2.status();
  EXPECT_EQ(*rand_2, "fb4002cccccccccccd");
}

TEST(ChromeResponseUtils, VerifyMinimalResponseEncoding) {
  ScoreAdsResponse::AdScore winner;
  winner.set_interest_group_owner("https://adtech.com");
  winner.set_interest_group_name("ig1");
  winner.set_desirability(156671.781);
  winner.set_buyer_bid(0.195839122);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->set_reporting_url(kTestReportWinUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  const std::string interest_group_owner_1 = "ig1";
  const std::string interest_group_owner_2 = "zi";
  const std::string interest_group_owner_3 = "ih1";
  AuctionResult::InterestGroupIndex indices;
  indices.add_index(7);
  indices.add_index(2);
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  bidding_group_map.try_emplace(interest_group_owner_1, indices);
  bidding_group_map.try_emplace(interest_group_owner_2, indices);
  bidding_group_map.try_emplace(interest_group_owner_3, indices);
  bidding_group_map.try_emplace("owner1", std::move(indices));

  auto ret = Encode(std::move(winner), std::move(bidding_group_map),
                    std::nullopt, [](auto error) {});
  ASSERT_TRUE(ret.ok()) << ret.status();
  // Conversion can be verified at: https://cbor.me/
  EXPECT_EQ(
      absl::BytesToHexString(*ret),
      "a963626964fa3e488a0d6573636f7265fa4818fff26769734368616666f46a636f6d706f"
      "6e656e7473806b616452656e64657255524c606d62696464696e6747726f757073a4627a"
      "698207026369673182070263696831820702666f776e6572318207027077696e5265706f"
      "7274696e6755524c73a27262757965725265706f7274696e6755524c73a26c7265706f72"
      "74696e6755524c74687474703a2f2f7265706f727457696e2e636f6d7818696e74657261"
      "6374696f6e5265706f7274696e6755524c73a165636c69636b70687474703a2f2f636c69"
      "636b2e636f6d781b746f704c6576656c53656c6c65725265706f7274696e6755524c73a2"
      "6c7265706f7274696e6755524c77687474703a2f2f7265706f7274526573756c742e636f"
      "6d7818696e746572616374696f6e5265706f7274696e6755524c73a165636c69636b7068"
      "7474703a2f2f636c69636b2e636f6d71696e74657265737447726f75704e616d65636967"
      "3172696e74657265737447726f75704f776e65727268747470733a2f2f6164746563682e"
      "636f6d");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
