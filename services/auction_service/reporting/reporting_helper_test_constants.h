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

#ifndef SERVICES_AUCTION_SERVICE_REPORTING_REPORTING_HELPER_TEST_CONSTANTS_H_
#define SERVICES_AUCTION_SERVICE_REPORTING_REPORTING_HELPER_TEST_CONSTANTS_H_

namespace privacy_sandbox::bidding_auction_servers {
constexpr char kTestReportResultUrl[] = "http://reportResultUrl.com";
constexpr char kTestReportWinUrl[] = "http://reportWinUrl.com";
constexpr char kTestLog[] = "testLog";
constexpr bool kSendReportToInvokedTrue = true;
constexpr bool kRegisterAdBeaconInvokedTrue = true;
constexpr char kTestInteractionEvent[] = "click";
constexpr char kTestInteractionUrl[] = "http://event.com";
constexpr char kTestPublisherHostName[] = "publisherName";
constexpr char kTestAuctionConfig[] = "testAuctionConfig";
constexpr char kTestSellerReportingSignals[] =
    R"({"topWindowHostname":"publisherName","interestGroupOwner":"testOwner","renderURL":"http://testurl.com","renderUrl":"http://testurl.com","bid":1.0,"desirability":2.0,"highestScoringOtherBid":0.5})";
constexpr char kTestInterestGroupOwner[] = "testOwner";
constexpr char kTestInterestGroupName[] = "testInterestGroupName";
constexpr char kTestRender[] = "http://testurl.com";
constexpr float kTestBuyerBid = 1.0;
constexpr float kTestDesirability = 2.0;
constexpr float kTestHighestScoringOtherBid = 0.5;
constexpr char kEnableAdtechCodeLoggingTrue[] = "true";
constexpr int kTestJoinCount = 1;
constexpr float kTestRecency = 2.1;
constexpr int kTestModelingSignals = 3;
constexpr char kTestBuyerMetadata[] =
    R"({"enableReportWinUrlGeneration":true,"perBuyerSignals":{"testkey":"testvalue"},"buyerOrigin":"testOwner","madeHighestScoringOtherBid":true,"joinCount":1,"recency":2,"modelingSignals":3})";
constexpr char kTestBuyerSignals[] = "{\"testkey\":\"testvalue\"}";
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_REPORTING_REPORTING_HELPER_TEST_CONSTANTS_H_
