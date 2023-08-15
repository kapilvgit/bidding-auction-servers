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
//  limitations under the License.

#ifndef FLEDGE_SERVICES_SELLER_CODE_WRAPPER_H_
#define FLEDGE_SERVICES_SELLER_CODE_WRAPPER_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kFeatureLogging[] = "enable_logging";
constexpr char kFeatureDebugUrlGeneration[] = "enable_debug_url_generation";

constexpr char kFeatureDisabled[] = "false";
constexpr char kFeatureEnabled[] = "true";

inline constexpr char kReportWinWrapperNamePlaceholder[] =
    "$reportWinWrapperName";
inline constexpr char kReportWinCodePlaceholder[] = "$reportWinCode";
inline constexpr char kReportWinWrapperFunctionName[] = "reportWinWrapper";

// The function that will be called first by Roma.
// The dispatch function name will be scoreAdEntryFunction.
// This wrapper supports the features below:
//- Exporting logs to Auction Service using console.log
constexpr absl::string_view kEntryFunction = R"JS_CODE(
    const forDebuggingOnly = {}
    forDebuggingOnly.auction_win_url = undefined;
    forDebuggingOnly.auction_loss_url = undefined;

    forDebuggingOnly.reportAdAuctionLoss = (url) => {
      forDebuggingOnly.auction_loss_url = url;
    }

    forDebuggingOnly.reportAdAuctionWin = (url) => {
      forDebuggingOnly.auction_win_url = url;
    }

    function scoreAdEntryFunction(adMetadata, bid, auctionConfig, trustedScoringSignals,
                                browserSignals, directFromSellerSignals, featureFlags){
      var ps_logs = [];
      var ps_errors = [];
      var ps_warns = [];
      if(featureFlags.enable_logging){
        console.log = function(...args) {
          ps_logs.push(JSON.stringify(args))
        }
        console.error = function(...args) {
          ps_errors.push(JSON.stringify(args))
        }
        console.warn = function(...args) {
          ps_warns.push(JSON.stringify(args))
        }
      }

      var scoreAdResponse = {};
      try {
        scoreAdResponse = scoreAd(adMetadata, bid, auctionConfig,
              trustedScoringSignals, browserSignals, directFromSellerSignals);
      } catch({error, message}) {
          console.error("[Error: " + error + "; Message: " + message + "]");
      } finally {
        if( featureFlags.enable_debug_url_generation &&
              (forDebuggingOnly.auction_win_url
                  || forDebuggingOnly.auction_loss_url)) {
          scoreAdResponse.debugReportUrls = {
            auctionDebugLossUrl: forDebuggingOnly.auction_loss_url,
            auctionDebugWinUrl: forDebuggingOnly.auction_win_url
          }
        }
      }
      return {
        response: scoreAdResponse,
        logs: ps_logs,
        errors: ps_errors,
        warnings: ps_warns
      }
    }
)JS_CODE";

// The function that will be called by Roma to generate reporting urls.
// The dispatch function name will be reportingEntryFunction.
// This wrapper supports the features below:
//- Event level and fenced frame reporting for seller: reportResult() function
// execution
//- Event level and fenced frame reporting for buyer: reportWin() function
// execution
//- Exporting console.logs from the AdTech script execution
inline constexpr absl::string_view kReportingEntryFunction =
    R"JSCODE(
    //Handler method to call adTech provided reportResult method and wrap the
    // response with reportResult url and interaction reporting urls.
    function reportingEntryFunction(auctionConfig, sellerReportingSignals, directFromSellerSignals, enable_logging, buyerReportingMetadata) {
      var ps_report_result_response = {
        reportResultUrl : "",
        signalsForWinner : "",
        interactionReportingUrls : "",
        sendReportToInvoked : false,
        registerAdBeaconInvoked : false,
      }
      var ps_logs = [];
      var ps_errors = [];
      var ps_warns = [];
      if(enable_logging){
        console.log = function(...args) {
          ps_logs.push(JSON.stringify(args))
        }
        console.error = function(...args) {
          ps_errors.push(JSON.stringify(args))
        }
        console.warn = function(...args) {
          ps_warns.push(JSON.stringify(args))
        }
      }
      globalThis.sendReportTo = function sendReportTo(url){
        if(ps_report_result_response.sendReportToInvoked) {
          throw new Error("sendReportTo function invoked more than once");
        }
        ps_report_result_response.reportResultUrl = url;
        ps_report_result_response.sendReportToInvoked = true;
      }
      globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap){
        if(ps_report_result_response.registerAdBeaconInvoked) {
          throw new Error("registerAdBeaconInvoked function invoked more than once");
        }
        ps_report_result_response.interactionReportingUrls=eventUrlMap;
        ps_report_result_response.registerAdBeaconInvoked = true;
      }
      ps_report_result_response.signalsForWinner = reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals);
      try{
      if(buyerReportingMetadata.enableReportWinUrlGeneration){
        var buyerOrigin = buyerReportingMetadata.buyerOrigin
        var buyerPrefix = buyerOrigin.replace(/[^a-zA-Z0-9 ]/g, "")
        var auctionSignals = auctionConfig.auctionSignals
        var buyerReportingSignals = sellerReportingSignals
        buyerReportingSignals.interestGroupName = buyerReportingMetadata.interestGroupName
        buyerReportingSignals.madeHighestScoringOtherBid = buyerReportingMetadata.madeHighestScoringOtherBid
        buyerReportingSignals.joinCount = buyerReportingMetadata.joinCount
        buyerReportingSignals.recency = buyerReportingMetadata.recency
        buyerReportingSignals.modelingSignals = buyerReportingMetadata.modelingSignals
        perBuyerSignals = buyerReportingMetadata.perBuyerSignals
        signalsForWinner = ps_report_result_response.signalsForWinner
        var reportWinFunction = "reportWinWrapper"+buyerPrefix+"(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,"+
                              "directFromSellerSignals, enable_logging)"
        var reportWinResponse = eval(reportWinFunction)
        return {
          reportResultResponse: ps_report_result_response,
          sellerLogs: ps_logs,
          sellerErrors: ps_errors,
          sellerWarnings: ps_warns,
          reportWinResponse: reportWinResponse.response,
          buyerLogs: reportWinResponse.logs
      }
      }
      } catch(ex){
        console.error(ex.message)
      }
      return {
        reportResultResponse: ps_report_result_response,
        sellerLogs: ps_logs,
        sellerErrors: ps_errors,
        sellerWarnings: ps_warns,
      }
    }
)JSCODE";

inline constexpr absl::string_view kReportingWinWrapperTemplate =
    R"JSCODE(
    // Handler method to call adTech provided reportWin method and wrap the
    // response with reportWin url and interaction reporting urls.
    function $reportWinWrapperName(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals, enable_logging) {
      var ps_report_win_response = {
        reportWinUrl : "",
        interactionReportingUrls : "",
        sendReportToInvoked : false,
        registerAdBeaconInvoked : false,
      }
      var ps_logs = [];
      if(enable_logging){
        console.log = function(...args) {
          ps_logs.push(JSON.stringify(args))
        }
      }
      globalThis.sendReportTo = function sendReportTo(url){
        if(ps_report_win_response.sendReportToInvoked) {
          throw new Error("sendReportTo function invoked more than once");
        }
        ps_report_win_response.reportWinUrl = url;
        ps_report_win_response.sendReportToInvoked = true;
      }
      globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap){
        if(ps_report_win_response.registerAdBeaconInvoked) {
          throw new Error("registerAdBeaconInvoked function invoked more than once");
        }
        ps_report_win_response.interactionReportingUrls = eventUrlMap;
        ps_report_win_response.registerAdBeaconInvoked = true;
      }
ps_report_win_code = $reportWinCode
      try{
      reportWin(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals)
      } catch(ex){
        console.error(ex.message)
      }
      return {
        response: ps_report_win_response,
        logs: ps_logs,
      }
    }
)JSCODE";

// Returns the complete wrapped code for Seller.
// The function adds wrappers to the Seller provided script. This enables:
// - Generation of event level reporting urls for Seller
// - Generation of event level reporting urls for all the Buyers
// - Generation of event level debug reporting
// - Exporting console.logs from the AdTech execution.
std::string GetSellerWrappedCode(
    absl::string_view seller_js_code, bool enable_report_result_url_generation,
    bool enable_report_win_url_generation,
    const absl::flat_hash_map<std::string, std::string>& buyer_origin_code_map);

// Returns a JSON string for feature flags to be used by the wrapper script.
std::string GetFeatureFlagJson(bool enable_logging,
                               bool enable_debug_url_generation);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_SELLER_CODE_WRAPPER_H_
