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

#ifndef SERVICES_COMMON_TEST_RANDOM_H_
#define SERVICES_COMMON_TEST_RANDOM_H_

#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "absl/container/flat_hash_map.h"
#include "absl/time/time.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/seller_frontend_service/test/app_test_utils.h"

// helper functions to generate random objects for testing
namespace privacy_sandbox::bidding_auction_servers {

using EncodedBuyerInputs = google::protobuf::Map<std::string, std::string>;
using InterestGroupForBidding =
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding;

std::string MakeARandomString();

std::string MakeARandomUrl();

absl::flat_hash_map<std::string, std::string> MakeARandomMap(int entries = 2);

int MakeARandomInt(int min, int max);

template <typename num_type>
num_type MakeARandomNumber(num_type min, num_type max) {
  std::default_random_engine curr_time_generator(ToUnixMillis(absl::Now()));
  return std::uniform_real_distribution<num_type>(min,
                                                  max)(curr_time_generator);
}

std::unique_ptr<google::protobuf::Struct> MakeARandomStruct(int num_fields);

void ProtoToJson(const google::protobuf::Message& proto,
                 std::string* json_output);

std::unique_ptr<std::string> MakeARandomStructJsonString(int num_fields);

google::protobuf::Struct MakeAnAd(std::string render_url,
                                  std::string metadata_key, int metadata_value);

// Consistent to aid latency benchmarking.
std::string MakeAFixedSetOfUserBiddingSignals(int num_ads);

google::protobuf::ListValue MakeARandomListOfStrings();

google::protobuf::ListValue MakeARandomListOfNumbers();

std::string MakeRandomPreviousWins(
    const google::protobuf::RepeatedPtrField<std::string>& ad_render_ids,
    bool set_times_to_one = false);

BrowserSignals MakeRandomBrowserSignalsForIG(
    const google::protobuf::RepeatedPtrField<std::string>& ad_render_ids);

// Must manually delete/take ownership of underlying pointer
std::unique_ptr<BuyerInput::InterestGroup> MakeAnInterestGroupSentFromDevice();

InterestGroupForBidding MakeAnInterestGroupForBiddingSentFromDevice();

// Build random trusted bidding signals with interest group names.
std::string MakeRandomTrustedBiddingSignals(
    const GenerateBidsRequest::GenerateBidsRawRequest& raw_request);

// build_android_signals: If false, will insert random values into
// browser signals, otherwise will insert random values into android signals.
InterestGroupForBidding MakeARandomInterestGroupForBidding(
    bool build_android_signals,
    bool set_user_bidding_signals_to_empty_struct = false);

InterestGroupForBidding MakeALargeInterestGroupForBiddingForLatencyTesting();

InterestGroupForBidding MakeARandomInterestGroupForBiddingFromAndroid();

InterestGroupForBidding MakeARandomInterestGroupForBiddingFromBrowser();

GenerateBidsRequest::GenerateBidsRawRequest
MakeARandomGenerateBidsRawRequestForAndroid();

GenerateBidsRequest::GenerateBidsRawRequest
MakeARandomGenerateBidsRequestForBrowser();

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
MakeARandomAdWithBidMetadata(float min_bid, float max_bid,
                             int num_ad_components = 5);

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
MakeARandomAdWithBidMetadataWithRejectionReason(float min_bid, float max_bid,
                                                int num_ad_components = 5,
                                                int rejection_reason_index = 0);

DebugReportUrls MakeARandomDebugReportUrls();

WinReportingUrls::ReportingUrls MakeARandomReportingUrls(
    int intraction_entries = 10);

WinReportingUrls MakeARandomWinReportingUrls();

AdWithBid MakeARandomAdWithBid(float min_bid, float max_bid,
                               int num_ad_components = 5);

GenerateBidsResponse::GenerateBidsRawResponse
MakeARandomGenerateBidsRawResponse();

ScoreAdsResponse::AdScore MakeARandomAdScore(
    int hob_buyer_entries = 0, int rejection_reason_ig_owners = 0,
    int rejection_reason_ig_per_owner = 0);

// Must manually delete/take ownership of underlying pointer
// build_android_signals: If false, will build browser signals instead.
std::unique_ptr<BuyerInput::InterestGroup> MakeARandomInterestGroup(
    bool build_android_signals);

std::unique_ptr<BuyerInput::InterestGroup>
MakeARandomInterestGroupFromAndroid();

std::unique_ptr<BuyerInput::InterestGroup>
MakeARandomInterestGroupFromBrowser();

GetBidsRequest::GetBidsRawRequest MakeARandomGetBidsRawRequest();

GetBidsRequest MakeARandomGetBidsRequest();

template <typename T>
T MakeARandomProtectedAuctionInput() {
  BuyerInput buyer_input_1;
  BuyerInput buyer_input_2;
  auto ig_with_two_ads_1 = MakeAnInterestGroupSentFromDevice();
  auto ig_with_two_ads_2 = MakeAnInterestGroupSentFromDevice();
  buyer_input_1.mutable_interest_groups()->AddAllocated(
      ig_with_two_ads_1.release());
  buyer_input_2.mutable_interest_groups()->AddAllocated(
      ig_with_two_ads_2.release());
  google::protobuf::Map<std::string, BuyerInput> buyer_inputs;
  buyer_inputs.emplace("ad_tech_A.com", buyer_input_1);
  buyer_inputs.emplace("ad_tech_B.com", buyer_input_2);
  absl::StatusOr<EncodedBuyerInputs> encoded_buyer_input =
      GetEncodedBuyerInputMap(buyer_inputs);
  T protected_auction_input;
  protected_auction_input.set_generation_id(MakeARandomString());
  *protected_auction_input.mutable_buyer_input() =
      *std::move(encoded_buyer_input);
  protected_auction_input.set_publisher_name(MakeARandomString());
  return protected_auction_input;
}

template <typename T>
SelectAdRequest MakeARandomSelectAdRequest(
    absl::string_view seller_domain_origin, const T& protected_auction_input) {
  SelectAdRequest request;
  request.mutable_auction_config()->set_seller_signals(absl::StrCat(
      "{\"seller_signal\": \"", MakeARandomString(), "\"}"));  // 3.

  request.mutable_auction_config()->set_auction_signals(
      absl::StrCat("{\"auction_signal\": \"", MakeARandomString(), "\"}"));

  request.mutable_auction_config()->set_seller(MakeARandomString());
  request.mutable_auction_config()->set_buyer_timeout_ms(1000);

  for (auto& buyer_input_pair : protected_auction_input.buyer_input()) {
    *request.mutable_auction_config()->mutable_buyer_list()->Add() =
        buyer_input_pair.first;
    SelectAdRequest::AuctionConfig::PerBuyerConfig per_buyer_config = {};
    per_buyer_config.set_buyer_signals(MakeARandomString());
    request.mutable_auction_config()->mutable_per_buyer_config()->insert(
        {buyer_input_pair.first, per_buyer_config});
  }

  request.mutable_auction_config()->set_seller(seller_domain_origin);
  request.set_client_type(SelectAdRequest::BROWSER);
  return request;
}

google::protobuf::Value MakeAStringValue(const std::string& v);

google::protobuf::Value MakeANullValue();

google::protobuf::Value MakeAListValue(
    const std::vector<google::protobuf::Value>& vec);

BuyerInput MakeARandomBuyerInput();

ProtectedAuctionInput MakeARandomProtectedAuctionInput(
    SelectAdRequest::ClientType client_type);

AuctionResult MakeARandomAuctionResult();

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_TEST_RANDOM_H_
