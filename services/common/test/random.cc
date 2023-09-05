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

#include "services/common/test/random.h"

#include <string>

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kTestIgWithTwoAds[] =
    R"json({"ad_render_ids":["adg_id=134256827445&cr_id=594352291621&cv_id=4", "adg_id=134256827445&cr_id=605048329089&cv_id=2"],"browser_signals":{"joinCount":1,"bidCount":100,"prevWins":"[[1472425.0,{\"metadata\":[134256827485.0,594352291615.0,null,16996677067.0]}],[1475389.0,{\"metadata\":[134256827445.0,594352291621.0,null,16996677067.0]}],[1487572.0,{\"metadata\":[134256827445.0,605490292974.0,null,16996677067.0]}],[1451707.0,{\"metadata\":[134256827445.0,605048329092.0,null,16996677067.0]}],[1485996.0,{\"metadata\":[134256827445.0,605048329089.0,null,16996677067.0]}],[1450931.0,{\"metadata\":[134256827485.0,605490292980.0,null,16996677067.0]}],[1473069.0,{\"metadata\":[134256827485.0,605048328957.0,null,16996677067.0]}],[1461197.0,{\"metadata\":[134256827485.0,605048329080.0,null,16996677067.0]}]]"},"name":"1j1043317685"})json";

std::string MakeARandomString() {
  return std::to_string(ToUnixNanos(absl::Now()));
}

std::string MakeARandomUrl() {
  return absl::StrCat("https://", MakeARandomString(), ".com");
}

absl::flat_hash_map<std::string, std::string> MakeARandomMap(int entries) {
  absl::flat_hash_map<std::string, std::string> output;
  for (int i = 0; i < entries; i++) {
    output.try_emplace(MakeARandomString(), MakeARandomString());
  }
  return output;
}

int MakeARandomInt(int min, int max) {
  std::default_random_engine curr_time_generator(ToUnixMillis(absl::Now()));
  return std::uniform_int_distribution<int>(min, max)(curr_time_generator);
}

std::unique_ptr<google::protobuf::Struct> MakeARandomStruct(int num_fields) {
  auto a_struct = std::make_unique<google::protobuf::Struct>();
  auto& map = *(a_struct->mutable_fields());
  for (int i = 0; i < num_fields; i++) {
    map[MakeARandomString()].set_string_value(MakeARandomString());
  }
  return a_struct;
}

void ProtoToJson(const google::protobuf::Message& proto,
                 std::string* json_output) {
  auto options = google::protobuf::util::JsonPrintOptions();
  options.preserve_proto_field_names = true;
  google::protobuf::util::MessageToJsonString(proto, json_output, options);
}

std::unique_ptr<std::string> MakeARandomStructJsonString(int num_fields) {
  std::unique_ptr<std::string> json_output = std::make_unique<std::string>();
  std::unique_ptr<google::protobuf::Struct> map = MakeARandomStruct(num_fields);
  ProtoToJson(*map, json_output.get());
  return json_output;
}

google::protobuf::Struct MakeAnAd(std::string render_url,
                                  std::string metadata_key,
                                  int metadata_value) {
  google::protobuf::Struct ad;
  google::protobuf::Value ad_render_url;
  ad_render_url.set_string_value(render_url);
  ad.mutable_fields()->try_emplace("renderUrl", ad_render_url);

  auto metadata_obj = MakeARandomStruct(0);
  google::protobuf::Value metadata_v;
  metadata_v.set_number_value(metadata_value);
  metadata_obj->mutable_fields()->try_emplace(metadata_key, metadata_v);

  google::protobuf::Value metadata_obj_val;
  metadata_obj_val.set_allocated_struct_value(metadata_obj.release());
  ad.mutable_fields()->try_emplace("metadata", metadata_obj_val);
  return ad;
}

// Consistent to aid latency benchmarking.
std::string MakeAFixedSetOfUserBiddingSignals(int num_ads) {
  std::string output = "{";
  for (int i = 0; i < num_ads; i++) {
    absl::StrAppend(&output, "\"userBiddingSignal", i + 1, "\": \"someValue\"");
    if (i < num_ads - 1) {
      absl::StrAppend(&output, ",");
    }
  }
  absl::StrAppend(&output, "}");
  return output;
}

google::protobuf::ListValue MakeARandomListOfStrings() {
  google::protobuf::ListValue output;
  for (int i = 0; i < MakeARandomInt(1, 10); i++) {
    auto* v = output.mutable_values()->Add();
    v->set_string_value(MakeARandomString());
  }
  return output;
}

google::protobuf::ListValue MakeARandomListOfNumbers() {
  google::protobuf::ListValue output;
  for (int i = 0; i < MakeARandomInt(1, 10); i++) {
    float number = MakeARandomNumber<float>(1, 10);
    output.add_values()->set_number_value(number);
  }
  return output;
}

std::string MakeRandomPreviousWins(
    const google::protobuf::RepeatedPtrField<std::string>& ad_render_ids,
    bool set_times_to_one) {
  std::string previous_wins = "[";
  for (int i = 0; i < ad_render_ids.size(); i++) {
    int time_val = 1;
    if (!set_times_to_one) {
      time_val = (unsigned)time(NULL);
    }
    absl::StrAppend(&previous_wins, "[", time_val, ",\"", ad_render_ids.at(i),
                    "\"]");
    if (i < ad_render_ids.size() - 1) {
      absl::StrAppend(&previous_wins, ",");
    }
  }
  absl::StrAppend(&previous_wins, "]");
  return previous_wins;
}

BrowserSignals MakeRandomBrowserSignalsForIG(
    const google::protobuf::RepeatedPtrField<std::string>& ad_render_ids) {
  BrowserSignals browser_signals;
  browser_signals.set_join_count(MakeARandomInt(0, 10));
  browser_signals.set_bid_count(MakeARandomInt(0, 50));
  browser_signals.set_recency((unsigned long)std::time(NULL));
  browser_signals.set_prev_wins(MakeRandomPreviousWins(ad_render_ids));
  return browser_signals;
}

// Must manually delete/take ownership of underlying pointer
std::unique_ptr<BuyerInput::InterestGroup> MakeAnInterestGroupSentFromDevice() {
  // Parse an IG in the form sent from the device.
  BuyerInput::InterestGroup ig_with_two_ads;
  auto result = google::protobuf::util::JsonStringToMessage(kTestIgWithTwoAds,
                                                            &ig_with_two_ads);
  std::unique_ptr<BuyerInput::InterestGroup> u_ptr_to_ig =
      std::make_unique<BuyerInput::InterestGroup>(ig_with_two_ads);
  u_ptr_to_ig->mutable_bidding_signals_keys()->Add(MakeARandomString());
  google::protobuf::RepeatedPtrField<std::string> ad_render_ids;
  ad_render_ids.Add(MakeARandomString());
  u_ptr_to_ig->mutable_browser_signals()->CopyFrom(
      MakeRandomBrowserSignalsForIG(ad_render_ids));
  return u_ptr_to_ig;
}

InterestGroupForBidding MakeAnInterestGroupForBiddingSentFromDevice() {
  std::unique_ptr<BuyerInput::InterestGroup> ig_with_two_ads =
      MakeAnInterestGroupSentFromDevice();
  InterestGroupForBidding ig_for_bidding_from_device;

  ig_for_bidding_from_device.set_name(ig_with_two_ads->name());
  ig_for_bidding_from_device.mutable_browser_signals()->CopyFrom(
      ig_with_two_ads->browser_signals());
  ig_for_bidding_from_device.mutable_ad_render_ids()->MergeFrom(
      ig_with_two_ads->ad_render_ids());
  ig_for_bidding_from_device.mutable_trusted_bidding_signals_keys()->Add(
      MakeARandomString());
  return ig_for_bidding_from_device;
}

std::string MakeRandomTrustedBiddingSignals(
    const GenerateBidsRequest::GenerateBidsRawRequest& raw_request) {
  std::string signals_dest;
  absl::StrAppend(&signals_dest, "{\"keys\":{");
  for (int i = 0; i < raw_request.interest_group_for_bidding_size(); i++) {
    auto interest_group = raw_request.interest_group_for_bidding().at(i);
    std::string raw_random_signals;
    google::protobuf::util::MessageToJsonString(MakeARandomListOfStrings(),
                                                &raw_random_signals);
    absl::StrAppend(&signals_dest, "\"", interest_group.name(),
                    "\":", raw_random_signals);
    if (i < raw_request.interest_group_for_bidding_size() - 1) {
      absl::StrAppend(&signals_dest, ",");
    }
  }
  absl::StrAppend(&signals_dest, "}}");
  return signals_dest;
}

InterestGroupForBidding MakeARandomInterestGroupForBidding(
    bool build_android_signals, bool set_user_bidding_signals_to_empty_struct) {
  InterestGroupForBidding ig_for_bidding;
  ig_for_bidding.set_name(MakeARandomString());
  ig_for_bidding.mutable_trusted_bidding_signals_keys()->Add(
      MakeARandomString());
  if (!set_user_bidding_signals_to_empty_struct) {
    // Takes ownership of pointer.
    ig_for_bidding.set_user_bidding_signals(
        R"JSON({"years": [1776, 1868], "name": "winston", "someId": 1789})JSON");
  }
  int ad_render_ids_to_generate = MakeARandomInt(1, 10);
  for (int i = 0; i < ad_render_ids_to_generate; i++) {
    *ig_for_bidding.mutable_ad_render_ids()->Add() =
        absl::StrCat("ad_render_id_", MakeARandomString());
  }
  if (build_android_signals) {
    // Empty message for now.
    ig_for_bidding.mutable_android_signals();
  } else {
    ig_for_bidding.mutable_browser_signals()->CopyFrom(
        MakeRandomBrowserSignalsForIG(ig_for_bidding.ad_render_ids()));
  }
  return ig_for_bidding;
}

InterestGroupForBidding MakeALargeInterestGroupForBiddingForLatencyTesting() {
  int num_ads = 10, num_bidding_signals_keys = 10, num_ad_render_ids = 10,
      num_ad_component_render_ids = 10, num_user_bidding_signals = 10;
  InterestGroupForBidding ig_for_bidding;
  // Name.
  ig_for_bidding.set_name("HandbagShoppers");
  // Ad render IDs.
  for (int i = 0; i < num_ad_render_ids; i++) {
    ig_for_bidding.mutable_ad_render_ids()->Add(
        absl::StrCat("adRenderId", i + 1));
  }
  // Ad component render IDs.
  for (int i = 0; i < num_ad_component_render_ids; i++) {
    ig_for_bidding.mutable_ad_component_render_ids()->Add(
        absl::StrCat("adComponentRenderId", i + 1));
  }
  // Bidding signals keys.
  for (int i = 0; i < num_bidding_signals_keys; i++) {
    ig_for_bidding.mutable_trusted_bidding_signals_keys()->Add(
        absl::StrCat("biddingSignalsKey", i + 1));
  }
  // User bidding signals.
  ig_for_bidding.set_user_bidding_signals(
      MakeAFixedSetOfUserBiddingSignals(num_user_bidding_signals));
  // Device signals.
  ig_for_bidding.mutable_browser_signals()->CopyFrom(
      MakeRandomBrowserSignalsForIG(ig_for_bidding.ad_render_ids()));
  return ig_for_bidding;
}

InterestGroupForBidding MakeARandomInterestGroupForBiddingFromAndroid() {
  return MakeARandomInterestGroupForBidding(true);
}

InterestGroupForBidding MakeARandomInterestGroupForBiddingFromBrowser() {
  return MakeARandomInterestGroupForBidding(false);
}

GenerateBidsRequest::GenerateBidsRawRequest
MakeARandomGenerateBidsRawRequestForAndroid() {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  *raw_request.mutable_interest_group_for_bidding()->Add() =
      MakeARandomInterestGroupForBiddingFromAndroid();
  *raw_request.mutable_interest_group_for_bidding()->Add() =
      MakeARandomInterestGroupForBiddingFromAndroid();
  raw_request.set_allocated_auction_signals(
      std::move(MakeARandomStructJsonString(MakeARandomInt(0, 100))).release());
  raw_request.set_allocated_buyer_signals(
      std::move(MakeARandomStructJsonString(MakeARandomInt(0, 100))).release());
  raw_request.set_bidding_signals(MakeARandomString());

  return raw_request;
}

GenerateBidsRequest::GenerateBidsRawRequest
MakeARandomGenerateBidsRequestForBrowser() {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  *raw_request.mutable_interest_group_for_bidding()->Add() =
      MakeARandomInterestGroupForBiddingFromBrowser();
  *raw_request.mutable_interest_group_for_bidding()->Add() =
      MakeARandomInterestGroupForBiddingFromBrowser();
  raw_request.set_allocated_auction_signals(
      std::move(MakeARandomStructJsonString(MakeARandomInt(0, 100))).release());
  raw_request.set_allocated_buyer_signals(
      std::move(MakeARandomStructJsonString(MakeARandomInt(0, 10))).release());
  raw_request.set_bidding_signals(MakeARandomString());
  raw_request.set_seller(MakeARandomString());
  raw_request.set_publisher_name(MakeARandomString());

  return raw_request;
}

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
MakeARandomAdWithBidMetadata(float min_bid, float max_bid,
                             int num_ad_components) {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata ad_with_bid;

  ad_with_bid.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(MakeARandomString(), MakeARandomString(), 2));
  ad_with_bid.set_interest_group_name(MakeARandomString());
  ad_with_bid.set_render(MakeARandomString());
  ad_with_bid.set_bid(MakeARandomNumber<float>(min_bid, max_bid));
  ad_with_bid.set_interest_group_owner(MakeARandomString());

  for (int i = 0; i < num_ad_components; i++) {
    ad_with_bid.add_ad_components(absl::StrCat("adComponent.com/id=", i));
  }
  ad_with_bid.set_ad_cost(MakeARandomNumber<double>(0.0, 2.0));
  ad_with_bid.set_modeling_signals(MakeARandomInt(0, 100));
  return ad_with_bid;
}

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
MakeARandomAdWithBidMetadataWithRejectionReason(float min_bid, float max_bid,
                                                int num_ad_components,
                                                int rejection_reason_index) {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata ad_with_bid;
  ad_with_bid.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(MakeARandomString(), "rejectReason", rejection_reason_index));
  ad_with_bid.set_interest_group_name(MakeARandomString());
  ad_with_bid.set_render(MakeARandomString());
  ad_with_bid.set_bid(MakeARandomNumber<float>(min_bid, max_bid));
  ad_with_bid.set_interest_group_owner(MakeARandomString());

  for (int i = 0; i < num_ad_components; i++) {
    ad_with_bid.add_ad_components(absl::StrCat("adComponent.com/id=", i));
  }

  return ad_with_bid;
}

DebugReportUrls MakeARandomDebugReportUrls() {
  DebugReportUrls debug_report_urls;
  debug_report_urls.set_auction_debug_win_url(MakeARandomUrl());
  debug_report_urls.set_auction_debug_loss_url(MakeARandomUrl());
  return debug_report_urls;
}

WinReportingUrls::ReportingUrls MakeARandomReportingUrls(
    int intraction_entries) {
  WinReportingUrls::ReportingUrls reporting_urls;
  reporting_urls.set_reporting_url(MakeARandomUrl());
  for (int index = 0; index < intraction_entries; index++) {
    reporting_urls.mutable_interaction_reporting_urls()->try_emplace(
        MakeARandomString(), MakeARandomUrl());
  }
  return reporting_urls;
}

WinReportingUrls MakeARandomWinReportingUrls() {
  WinReportingUrls win_reporting_urls;
  *win_reporting_urls.mutable_buyer_reporting_urls() =
      MakeARandomReportingUrls();
  *win_reporting_urls.mutable_component_seller_reporting_urls() =
      MakeARandomReportingUrls();
  *win_reporting_urls.mutable_top_level_seller_reporting_urls() =
      MakeARandomReportingUrls();
  return win_reporting_urls;
}

AdWithBid MakeARandomAdWithBid(float min_bid, float max_bid,
                               int num_ad_components) {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  AdWithBid ad_with_bid;

  ad_with_bid.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(MakeARandomString(), MakeARandomString(), 2));
  ad_with_bid.set_interest_group_name(MakeARandomString());
  ad_with_bid.set_render(MakeARandomString());
  ad_with_bid.set_bid(MakeARandomNumber<float>(min_bid, max_bid));
  *ad_with_bid.mutable_debug_report_urls() = MakeARandomDebugReportUrls();
  for (int i = 0; i < num_ad_components; i++) {
    ad_with_bid.add_ad_components(absl::StrCat("adComponent.com/id=", i));
  }
  ad_with_bid.set_bid_currency("USD");
  ad_with_bid.set_ad_cost(MakeARandomNumber<double>(0.0, 2.0));
  ad_with_bid.set_modeling_signals(MakeARandomInt(0, 100));
  return ad_with_bid;
}

GenerateBidsResponse::GenerateBidsRawResponse
MakeARandomGenerateBidsRawResponse() {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.mutable_bids()->Add(MakeARandomAdWithBid(0, 10));
  return raw_response;
}

ScoreAdsResponse::AdScore MakeARandomAdScore(
    int hob_buyer_entries, int rejection_reason_ig_owners,
    int rejection_reason_ig_per_owner) {
  ScoreAdsResponse::AdScore ad_score;
  float bid = MakeARandomNumber<float>(1, 2.5);
  float score = MakeARandomNumber<float>(1, 2.5);
  ad_score.set_desirability(score);
  ad_score.set_render(MakeARandomString());
  ad_score.set_interest_group_name(MakeARandomString());
  ad_score.set_buyer_bid(bid);
  ad_score.set_interest_group_owner(MakeARandomString());
  ad_score.set_ad_metadata(MakeARandomString());
  ad_score.set_allow_component_auction(false);
  ad_score.set_bid(bid);
  *ad_score.mutable_debug_report_urls() = MakeARandomDebugReportUrls();
  *ad_score.mutable_win_reporting_urls() = MakeARandomWinReportingUrls();
  for (int index = 0; index < hob_buyer_entries; index++) {
    ad_score.mutable_ig_owner_highest_scoring_other_bids_map()->try_emplace(
        MakeARandomString(), MakeARandomListOfNumbers());
  }
  std::vector<ScoreAdsResponse::AdScore::AdRejectionReason>
      ad_rejection_reasons;
  for (int index = 0; index < rejection_reason_ig_owners; index++) {
    std::string interest_group_owner = MakeARandomString();
    for (int j = 0; j < rejection_reason_ig_per_owner; j++) {
      ScoreAdsResponse::AdScore::AdRejectionReason ad_rejection_reason;
      ad_rejection_reason.set_interest_group_name(MakeARandomString());
      ad_rejection_reason.set_interest_group_owner(interest_group_owner);
      int rejection_reason_value =
          MakeARandomInt(1, 7);  // based on number of rejection reasons.
      ad_rejection_reason.set_rejection_reason(
          static_cast<SellerRejectionReason>(rejection_reason_value));
      ad_rejection_reasons.push_back(ad_rejection_reason);
    }
  }
  *ad_score.mutable_ad_rejection_reasons() = {ad_rejection_reasons.begin(),
                                              ad_rejection_reasons.end()};
  return ad_score;
}

// Must manually delete/take ownership of underlying pointer
// build_android_signals: If false, will build browser signals instead.
std::unique_ptr<BuyerInput::InterestGroup> MakeARandomInterestGroup(
    bool build_android_signals) {
  auto interest_group = std::make_unique<BuyerInput::InterestGroup>();
  interest_group->set_name(MakeARandomString());  // 1
  interest_group->mutable_bidding_signals_keys()->Add(
      MakeARandomString());  // 2
  interest_group->mutable_bidding_signals_keys()->Add(
      MakeARandomString());  // 2
  interest_group->set_allocated_user_bidding_signals(
      MakeARandomStructJsonString(5).release());
  int ad_render_ids_to_generate = MakeARandomInt(1, 10);
  for (int i = 0; i < ad_render_ids_to_generate; i++) {
    *interest_group->mutable_ad_render_ids()->Add() =
        absl::StrCat("ad_render_id_", MakeARandomString());
  }
  if (build_android_signals) {
    // Empty field right now.
    interest_group->mutable_android_signals();
  } else {
    interest_group->mutable_browser_signals()->CopyFrom(
        MakeRandomBrowserSignalsForIG(interest_group->ad_render_ids()));
  }
  return interest_group;
}

std::unique_ptr<BuyerInput::InterestGroup>
MakeARandomInterestGroupFromAndroid() {
  return MakeARandomInterestGroup(true);
}

std::unique_ptr<BuyerInput::InterestGroup>
MakeARandomInterestGroupFromBrowser() {
  return MakeARandomInterestGroup(false);
}

GetBidsRequest::GetBidsRawRequest MakeARandomGetBidsRawRequest() {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  GetBidsRequest::GetBidsRawRequest raw_request;
  raw_request.set_publisher_name("publisher_name");
  raw_request.set_allocated_auction_signals(
      std::move(MakeARandomStructJsonString(1).release()));
  return raw_request;
}

GetBidsRequest MakeARandomGetBidsRequest() {
  GetBidsRequest request;
  GetBidsRequest::GetBidsRawRequest raw_request =
      MakeARandomGetBidsRawRequest();
  request.set_request_ciphertext(raw_request.SerializeAsString());
  request.set_key_id(MakeARandomString());
  return request;
}

google::protobuf::Value MakeAStringValue(const std::string& v) {
  google::protobuf::Value obj;
  obj.set_string_value(v);
  return obj;
}

google::protobuf::Value MakeANullValue() {
  google::protobuf::Value obj;
  obj.set_null_value(google::protobuf::NULL_VALUE);
  return obj;
}

google::protobuf::Value MakeAListValue(
    const std::vector<google::protobuf::Value>& vec) {
  google::protobuf::Value obj;
  for (auto& val : vec) {
    obj.mutable_list_value()->add_values()->MergeFrom(val);
  }
  return obj;
}

BuyerInput MakeARandomBuyerInput() {
  BuyerInput buyer_input;
  buyer_input.mutable_interest_groups()->AddAllocated(
      MakeARandomInterestGroup(false).release());
  return buyer_input;
}

ProtectedAuctionInput MakeARandomProtectedAuctionInput(
    SelectAdRequest::ClientType client_type) {
  ProtectedAuctionInput input;
  input.set_publisher_name(MakeARandomString());
  input.set_generation_id(MakeARandomString());
  google::protobuf::Map<std::string, BuyerInput> buyer_inputs;
  buyer_inputs.emplace(MakeARandomString(), MakeARandomBuyerInput());
  absl::StatusOr<EncodedBuyerInputs> encoded_buyer_input;
  switch (client_type) {
    case SelectAdRequest::BROWSER:
      encoded_buyer_input = GetEncodedBuyerInputMap(buyer_inputs);
      break;
    case SelectAdRequest::ANDROID:
      encoded_buyer_input = GetProtoEncodedBuyerInputs(buyer_inputs);
      break;
    default:
      break;
  }
  GetEncodedBuyerInputMap(buyer_inputs);
  *input.mutable_buyer_input() = *std::move(encoded_buyer_input);
  input.set_enable_debug_reporting(true);
  return input;
}

AuctionResult MakeARandomAuctionResult() {
  AuctionResult result;
  result.set_ad_render_url(MakeARandomString());
  result.set_bid(MakeARandomNumber<float>(0.0, 1.0));
  result.set_interest_group_name(MakeARandomString());
  result.set_interest_group_owner(MakeARandomString());
  result.set_score(MakeARandomNumber<float>(0.0, 1.0));

  AuctionResult::InterestGroupIndex ig_indices;
  ig_indices.add_index(MakeARandomInt(1, 5));
  result.mutable_bidding_groups()->try_emplace(MakeARandomString(),
                                               std::move(ig_indices));
  // TODO(b/287074572): Add reporting URLs and other reporting fields here
  // when adding support for reporting.
  return result;
}

}  // namespace privacy_sandbox::bidding_auction_servers
