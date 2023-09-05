//  Copyright 2023 Google LLC
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
//  limitations under the License
#include "services/auction_service/reporting/reporting_helper.h"

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.pb.h"
#include "rapidjson/document.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/util/context_logger.h"
#include "services/common/util/json_util.h"
#include "services/common/util/post_auction_signals.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<ReportingResponse> ParseAndGetReportingResponse(
    bool enable_adtech_code_logging, const std::string& response) {
  ReportingResponse reporting_response{};
  PS_ASSIGN_OR_RETURN(rapidjson::Document document, ParseJsonString(response));
  if (!document.HasMember(kReportResultResponse)) {
    return reporting_response;
  }
  rapidjson::Value& response_obj = document[kReportResultResponse];
  reporting_response.report_result_response.report_result_url =
      response_obj[kReportResultUrl].GetString();
  reporting_response.report_result_response.send_report_to_invoked =
      response_obj[kSendReportToInvoked].GetBool();
  reporting_response.report_result_response.register_ad_beacon_invoked =
      response_obj[kRegisterAdBeaconInvoked].GetBool();

  rapidjson::Value interaction_reporting_urls_map =
      response_obj[kInteractionReportingUrlsWrapperResponse].GetObject();
  for (rapidjson::Value::MemberIterator it =
           interaction_reporting_urls_map.MemberBegin();
       it != interaction_reporting_urls_map.MemberEnd(); ++it) {
    const auto& [key, value] = *it;
    reporting_response.report_result_response.interaction_reporting_urls
        .try_emplace(key.GetString(), value.GetString());
  }
  if (enable_adtech_code_logging) {
    const rapidjson::Value& logs = document[kSellerLogs];
    for (const auto& log : logs.GetArray()) {
      reporting_response.seller_logs.emplace_back(log.GetString());
    }
  }

  if (!document.HasMember(kReportWinResponse)) {
    return reporting_response;
  }
  rapidjson::Value& report_win_obj = document[kReportWinResponse];
  reporting_response.report_win_response.report_win_url =
      report_win_obj[kReportWinUrl].GetString();

  reporting_response.report_win_response.send_report_to_invoked =
      report_win_obj[kSendReportToInvoked].GetBool();

  reporting_response.report_win_response.register_ad_beacon_invoked =
      report_win_obj[kRegisterAdBeaconInvoked].GetBool();

  rapidjson::Value report_win_interaction_urls_map =
      report_win_obj[kInteractionReportingUrlsWrapperResponse].GetObject();

  for (rapidjson::Value::MemberIterator it =
           report_win_interaction_urls_map.MemberBegin();
       it != report_win_interaction_urls_map.MemberEnd(); ++it) {
    const auto& [key, value] = *it;
    reporting_response.report_win_response.interaction_reporting_urls
        .try_emplace(key.GetString(), value.GetString());
  }
  return reporting_response;
}

absl::StatusOr<std::string> GetSellerReportingSignals(
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const std::string& publisher_hostname) {
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(winning_ad_score);

  rapidjson::Document document;
  document.SetObject();
  // Convert the std::string to a rapidjson::Value object.
  rapidjson::Value hostname_value;
  hostname_value.SetString(publisher_hostname.c_str(), document.GetAllocator());
  rapidjson::Value render_URL_value;
  render_URL_value.SetString(post_auction_signals.winning_ad_render_url.c_str(),
                             document.GetAllocator());
  rapidjson::Value render_url_value;
  render_url_value.SetString(post_auction_signals.winning_ad_render_url.c_str(),
                             document.GetAllocator());

  rapidjson::Value interest_group_owner;
  interest_group_owner.SetString(post_auction_signals.winning_ig_owner.c_str(),
                                 document.GetAllocator());

  document.AddMember(kTopWindowHostname, hostname_value,
                     document.GetAllocator());
  document.AddMember(kInterestGroupOwner, interest_group_owner,
                     document.GetAllocator());
  document.AddMember(kRenderURL, render_URL_value, document.GetAllocator());
  document.AddMember(kRenderUrl, render_url_value, document.GetAllocator());
  document.AddMember(kBid, post_auction_signals.winning_bid,
                     document.GetAllocator());
  document.AddMember(kDesirability, post_auction_signals.winning_score,
                     document.GetAllocator());
  document.AddMember(kHighestScoringOtherBid,
                     post_auction_signals.highest_scoring_other_bid,
                     document.GetAllocator());
  return SerializeJsonDoc(document);
}

std::string GetBuyerMetadataJson(
    const BuyerReportingMetadata& buyer_reporting_metadata,
    const ContextLogger& logger,
    const ScoreAdsResponse::AdScore& winning_ad_score) {
  if (!buyer_reporting_metadata.enable_report_win_url_generation) {
    return kDefaultBuyerReportingMetadata;
  }
  rapidjson::Document buyer_reporting_signals_obj;
  buyer_reporting_signals_obj.SetObject();
  buyer_reporting_signals_obj.AddMember(
      kEnableReportWinUrlGeneration,
      buyer_reporting_metadata.enable_report_win_url_generation,
      buyer_reporting_signals_obj.GetAllocator());
  absl::StatusOr<rapidjson::Document> buyer_signals_obj;
  if (!buyer_reporting_metadata.buyer_signals.empty()) {
    buyer_signals_obj = ParseJsonString(buyer_reporting_metadata.buyer_signals);
    if (!buyer_signals_obj.ok()) {
      logger.vlog(1, "Error parsing buyer signals to Json object");
    } else {
      buyer_reporting_signals_obj.AddMember(
          kBuyerSignals, buyer_signals_obj.value(),
          buyer_reporting_signals_obj.GetAllocator());
    }
  }
  rapidjson::Value buyer_origin;
  buyer_origin.SetString(winning_ad_score.interest_group_owner().c_str(),
                         buyer_reporting_signals_obj.GetAllocator());
  buyer_reporting_signals_obj.AddMember(
      kBuyerOrigin, buyer_origin, buyer_reporting_signals_obj.GetAllocator());
  bool made_highest_scoring_other_bid = false;
  if (winning_ad_score.ig_owner_highest_scoring_other_bids_map().size() == 1 &&
      winning_ad_score.ig_owner_highest_scoring_other_bids_map().contains(
          winning_ad_score.interest_group_owner())) {
    made_highest_scoring_other_bid = true;
  }
  buyer_reporting_signals_obj.AddMember(
      kMadeHighestScoringOtherBid, made_highest_scoring_other_bid,
      buyer_reporting_signals_obj.GetAllocator());
  if (buyer_reporting_metadata.join_count.has_value()) {
    buyer_reporting_signals_obj.AddMember(
        kJoinCount, buyer_reporting_metadata.join_count.value(),
        buyer_reporting_signals_obj.GetAllocator());
  }
  if (buyer_reporting_metadata.recency.has_value()) {
    buyer_reporting_signals_obj.AddMember(
        kRecency, buyer_reporting_metadata.recency.value(),
        buyer_reporting_signals_obj.GetAllocator());
  }
  if (buyer_reporting_metadata.modeling_signals.has_value()) {
    buyer_reporting_signals_obj.AddMember(
        kModelingSignals, buyer_reporting_metadata.modeling_signals.value(),
        buyer_reporting_signals_obj.GetAllocator());
  }
  absl::StatusOr<std::string> buyer_reporting_metadata_json =
      SerializeJsonDoc(buyer_reporting_signals_obj);
  if (!buyer_reporting_metadata_json.ok()) {
    logger.vlog(2,
                "Error constructing buyer_reporting_metadata_input for "
                "reportingEntryFunction");
    return kDefaultBuyerReportingMetadata;
  }
  return buyer_reporting_metadata_json.value();
}

std::vector<std::shared_ptr<std::string>> GetReportingInput(
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const std::string& publisher_hostname, bool enable_adtech_code_logging,
    std::shared_ptr<std::string> auction_config, const ContextLogger& logger,
    const BuyerReportingMetadata& buyer_reporting_metadata) {
  absl::StatusOr<std::string> seller_reporting_signals =
      GetSellerReportingSignals(winning_ad_score, publisher_hostname);
  if (!seller_reporting_signals.ok()) {
    logger.vlog(
        2, "Error generating Seller Reporting Signals for Reporting input");
    return {};
  }
  std::vector<std::shared_ptr<std::string>> input(
      kReportingArgSize);  // ReportingArgs size

  input[ReportingArgIndex(ReportingArgs::kAuctionConfig)] = auction_config;
  input[ReportingArgIndex(ReportingArgs::kSellerReportingSignals)] =
      std::make_shared<std::string>(seller_reporting_signals.value());
  // This is only added to prevent errors in the reporting ad script, and
  // will always be an empty object.
  input[ReportingArgIndex(ReportingArgs::kDirectFromSellerSignals)] =
      std::make_shared<std::string>("{}");
  input[ReportingArgIndex(ReportingArgs::kEnableAdTechCodeLogging)] =
      std::make_shared<std::string>(
          absl::StrFormat("%v", enable_adtech_code_logging));
  std::string buyer_reporting_metadata_json =
      GetBuyerMetadataJson(buyer_reporting_metadata, logger, winning_ad_score);
  input[ReportingArgIndex(ReportingArgs::kBuyerReportingMetadata)] =
      std::make_shared<std::string>(buyer_reporting_metadata_json);

  if (VLOG_IS_ON(2)) {
    logger.vlog(
        2, "\n\nReporting Input Args:", "\nAuction Config:\n",
        *(input[ReportingArgIndex(ReportingArgs::kAuctionConfig)]),
        "\nSeller Reporting Signals:\n",
        *(input[ReportingArgIndex(ReportingArgs::kSellerReportingSignals)]),
        "\nEnable AdTech Code Logging:\n",
        *(input[ReportingArgIndex(ReportingArgs::kEnableAdTechCodeLogging)]),
        "\nDirect from Seller Signals:\n",
        *(input[ReportingArgIndex(ReportingArgs::kDirectFromSellerSignals)]),
        "\nBuyer Reporting Metadata:\n",
        *(input[ReportingArgIndex(ReportingArgs::kBuyerReportingMetadata)]));
  }
  return input;
}

DispatchRequest GetReportingDispatchRequest(
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const std::string& publisher_hostname, bool enable_adtech_code_logging,
    std::shared_ptr<std::string> auction_config, const ContextLogger& logger,
    const BuyerReportingMetadata& buyer_reporting_metadata) {
  // Construct the wrapper struct for our V8 Dispatch Request.
  return {
      .id = winning_ad_score.render(),
      .version_num = kDispatchRequestVersionNumber,
      .handler_name = kReportingDispatchHandlerFunctionName,
      .input = GetReportingInput(winning_ad_score, publisher_hostname,
                                 enable_adtech_code_logging, auction_config,
                                 logger, buyer_reporting_metadata),
  };
}

}  // namespace privacy_sandbox::bidding_auction_servers
