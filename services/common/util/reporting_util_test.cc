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

#include "services/common/util/reporting_util.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "include/gtest/gtest.h"
#include "services/common/test/random.h"
#include "services/common/util/post_auction_signals.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline constexpr char kEmptyString[] = "";
inline constexpr char kTestIgOwner[] = "test_ig_owner";
inline constexpr char kTestIgName[] = "test_ig_name";
inline constexpr char kWinningIgName[] = "winning_ig_name";
inline constexpr char kWinningIgOwner[] = "winning_ig_owner";

TEST(PostAuctionSignalsTest, HasAllSignals) {
  std::optional<ScoreAdsResponse::AdScore> ad_score =
      std::make_optional(MakeARandomAdScore(2, 2, 2));
  PostAuctionSignals signals = GeneratePostAuctionSignals(ad_score);
  EXPECT_EQ(ad_score->interest_group_owner(), signals.winning_ig_owner);
  EXPECT_EQ(ad_score->interest_group_name(), signals.winning_ig_name);
  EXPECT_EQ(ad_score->buyer_bid(), signals.winning_bid);
  EXPECT_EQ(ad_score->desirability(), signals.winning_score);
  EXPECT_EQ(ad_score->render(), signals.winning_ad_render_url);
  EXPECT_TRUE(signals.has_highest_scoring_other_bid);
  EXPECT_NE(signals.highest_scoring_other_bid_ig_owner, "");
  EXPECT_GT(signals.highest_scoring_other_bid, 0.0);
  EXPECT_GT(signals.rejection_reason_map.size(), 0);
}

TEST(PostAuctionSignalsTest, HasWinningBidSignals) {
  std::optional<ScoreAdsResponse::AdScore> ad_score =
      std::make_optional(MakeARandomAdScore(0));
  PostAuctionSignals signals = GeneratePostAuctionSignals(ad_score);
  EXPECT_EQ(ad_score->interest_group_owner(), signals.winning_ig_owner);
  EXPECT_EQ(ad_score->interest_group_name(), signals.winning_ig_name);
  EXPECT_EQ(ad_score->buyer_bid(), signals.winning_bid);
  EXPECT_EQ(ad_score->desirability(), signals.winning_score);
  EXPECT_EQ(ad_score->render(), signals.winning_ad_render_url);
}

TEST(PostAuctionSignalsTest, HasHighestOtherBidSignals) {
  std::optional<ScoreAdsResponse::AdScore> ad_score =
      std::make_optional(MakeARandomAdScore(2));
  PostAuctionSignals signals = GeneratePostAuctionSignals(ad_score);
  EXPECT_TRUE(signals.has_highest_scoring_other_bid);
  EXPECT_NE(signals.highest_scoring_other_bid_ig_owner, "");
  EXPECT_GT(signals.highest_scoring_other_bid, 0.0);
}

TEST(PostAuctionSignalsTest, HasRejectionReasons) {
  std::optional<ScoreAdsResponse::AdScore> ad_score =
      std::make_optional(MakeARandomAdScore(0, 2, 2));
  PostAuctionSignals signals = GeneratePostAuctionSignals(ad_score);
  EXPECT_GT(signals.rejection_reason_map.size(), 0);
}

TEST(PostAuctionSignalsTest, DoesNotHaveAnySignal) {
  PostAuctionSignals signals = GeneratePostAuctionSignals(std::nullopt);
  EXPECT_EQ(signals.winning_ig_owner, "");
  EXPECT_EQ(signals.winning_ig_name, "");
  EXPECT_EQ(signals.winning_bid, 0.0);
  EXPECT_EQ(signals.winning_score, 0.0);
  EXPECT_EQ(signals.winning_ad_render_url, "");
  EXPECT_FALSE(signals.has_highest_scoring_other_bid);
  EXPECT_EQ(signals.highest_scoring_other_bid_ig_owner, "");
  EXPECT_EQ(signals.highest_scoring_other_bid, 0.0);
  EXPECT_EQ(signals.rejection_reason_map.size(), 0);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithWinningBidSuccess) {
  absl::string_view url =
      "https://wikipedia.org?wb=${winningBid}&m_wb=${madeWinningBid}";
  std::unique_ptr<DebugReportingPlaceholder> placeholder_1 =
      std::make_unique<DebugReportingPlaceholder>(
          1.9, false, 0.0, false,
          SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
  absl::string_view expected_url = "https://wikipedia.org?wb=1.9&m_wb=false";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder_1));
  EXPECT_EQ(request.url, expected_url);

  std::unique_ptr<DebugReportingPlaceholder> placeholder_2 =
      std::make_unique<DebugReportingPlaceholder>(
          1.9, true, 0.0, false,
          SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
  expected_url = "https://wikipedia.org?wb=1.9&m_wb=true";
  request = CreateDebugReportingHttpRequest(url, std::move(placeholder_2));
  EXPECT_EQ(request.url, expected_url);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithWinningBidAsZero) {
  absl::string_view url =
      "https://wikipedia.org?wb=${winningBid}&m_wb=${madeWinningBid}";
  std::unique_ptr<DebugReportingPlaceholder> placeholder_1 =
      std::make_unique<DebugReportingPlaceholder>(
          0.0, false, 0.0, false,
          SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
  absl::string_view expected_url = "https://wikipedia.org?wb=0&m_wb=false";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder_1));
  EXPECT_EQ(request.url, expected_url);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithHighestOtherBidSuccess) {
  absl::string_view url =
      "https://"
      "wikipedia.org?hob=${highestScoringOtherBid}&m_hob=${"
      "madeHighestScoringOtherBid}";
  std::unique_ptr<DebugReportingPlaceholder> placeholder_1 =
      std::make_unique<DebugReportingPlaceholder>(
          1.9, false, 2.18, true,
          SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
  absl::string_view expected_url = "https://wikipedia.org?hob=2.18&m_hob=true";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder_1));
  EXPECT_EQ(request.url, expected_url);

  std::unique_ptr<DebugReportingPlaceholder> placeholder_2 =
      std::make_unique<DebugReportingPlaceholder>(
          1.9, false, 2.18, false,
          SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
  expected_url = "https://wikipedia.org?hob=2.18&m_hob=false";
  request = CreateDebugReportingHttpRequest(url, std::move(placeholder_2));
  EXPECT_EQ(request.url, expected_url);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithHighestOtherBidAsZero) {
  absl::string_view url =
      "https://"
      "wikipedia.org?hob=${highestScoringOtherBid}&m_hob=${"
      "madeHighestScoringOtherBid}";
  std::unique_ptr<DebugReportingPlaceholder> placeholder_1 =
      std::make_unique<DebugReportingPlaceholder>(
          1.9, false, 0.0, false,
          SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
  absl::string_view expected_url = "https://wikipedia.org?hob=0&m_hob=false";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder_1));
  EXPECT_EQ(request.url, expected_url);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithRejectionReasonSuccess) {
  absl::string_view url =
      "https://wikipedia.org?seller_rejection_reason=${rejectReason}";
  std::unique_ptr<DebugReportingPlaceholder> placeholder_1 =
      std::make_unique<DebugReportingPlaceholder>(
          1.9, false, 2.18, true, SellerRejectionReason::INVALID_BID);
  absl::string_view expected_url =
      "https://wikipedia.org?seller_rejection_reason=invalid-bid";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder_1));
  EXPECT_EQ(request.url, expected_url);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithRejectionReasonNotAvailable) {
  absl::string_view url =
      "https://wikipedia.org?seller_rejection_reason=${rejectReason}";
  std::unique_ptr<DebugReportingPlaceholder> placeholder_1 =
      std::make_unique<DebugReportingPlaceholder>(
          1.9, false, 0.0, false,
          SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
  absl::string_view expected_url =
      "https://wikipedia.org?seller_rejection_reason=not-available";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder_1));
  EXPECT_EQ(request.url, expected_url);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithNoPlaceholder) {
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      std::make_unique<DebugReportingPlaceholder>(
          1.9, false, 2.18, true,
          SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
  absl::string_view url = "https://wikipedia.org";
  absl::string_view expected_url = "https://wikipedia.org";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder));
  EXPECT_EQ(request.url, expected_url);
}

TEST(GetPlaceholderDataForInterestGroupOwnerTest, IgOwnerIsNone) {
  absl::flat_hash_map<std::string,
                      absl::flat_hash_map<std::string, SellerRejectionReason>>
      rejection_reason_map;
  PostAuctionSignals signals = {
      .winning_ig_name = kEmptyString,
      .winning_ig_owner = kEmptyString,
      .winning_bid = 0.0,
      .highest_scoring_other_bid = 0.0,
      .highest_scoring_other_bid_ig_owner = kEmptyString,
      .has_highest_scoring_other_bid = false,
      .winning_score = 0.0,
      .winning_ad_render_url = kEmptyString,
      .rejection_reason_map = std::move(rejection_reason_map)};
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      GetPlaceholderDataForInterestGroup(kTestIgOwner, kTestIgName, signals);
  EXPECT_FALSE(placeholder->made_winning_bid);
  EXPECT_FALSE(placeholder->made_highest_scoring_other_bid);
  EXPECT_EQ(placeholder->winning_bid, signals.winning_bid);
  EXPECT_EQ(placeholder->highest_scoring_other_bid,
            signals.highest_scoring_other_bid);
  EXPECT_EQ(placeholder->rejection_reason,
            SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
}

TEST(GetPlaceholderDataForInterestGroupOwnerTest, IgOwnerIsWinner) {
  float winning_bid = 1.9;
  absl::flat_hash_map<std::string,
                      absl::flat_hash_map<std::string, SellerRejectionReason>>
      rejection_reason_map;
  PostAuctionSignals signals = {
      .winning_ig_name = kTestIgName,
      .winning_ig_owner = kTestIgOwner,
      .winning_bid = winning_bid,
      .highest_scoring_other_bid = 0.0,
      .highest_scoring_other_bid_ig_owner = kEmptyString,
      .has_highest_scoring_other_bid = false,
      .winning_score = 0.0,
      .winning_ad_render_url = kEmptyString,
      .rejection_reason_map = std::move(rejection_reason_map)};
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      GetPlaceholderDataForInterestGroup(kTestIgOwner, kTestIgName, signals);

  EXPECT_TRUE(placeholder->made_winning_bid);
  EXPECT_EQ(placeholder->winning_bid, signals.winning_bid);
}

TEST(GetPlaceholderDataForInterestGroupOwnerTest, IgOwnerMadeHighestOtherBid) {
  float winning_bid = 1.9;
  float highest_scoring_other_bid = 2.18;
  absl::flat_hash_map<std::string,
                      absl::flat_hash_map<std::string, SellerRejectionReason>>
      rejection_reason_map;
  PostAuctionSignals signals = {
      .winning_ig_name = kWinningIgName,
      .winning_ig_owner = kWinningIgOwner,
      .winning_bid = winning_bid,
      .highest_scoring_other_bid = highest_scoring_other_bid,
      .highest_scoring_other_bid_ig_owner = kTestIgOwner,
      .has_highest_scoring_other_bid = true,
      .winning_score = 0.0,
      .winning_ad_render_url = kEmptyString,
      .rejection_reason_map = std::move(rejection_reason_map)};
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      GetPlaceholderDataForInterestGroup(kTestIgOwner, kWinningIgName, signals);

  EXPECT_TRUE(placeholder->made_highest_scoring_other_bid);
  EXPECT_EQ(placeholder->highest_scoring_other_bid,
            signals.highest_scoring_other_bid);
}

TEST(GetPlaceholderDataForInterestGroupOwnerTest, IgOwnerIsBoth) {
  float winning_bid = 1.9;
  float highest_scoring_other_bid = 2.18;
  absl::flat_hash_map<std::string,
                      absl::flat_hash_map<std::string, SellerRejectionReason>>
      rejection_reason_map;
  PostAuctionSignals signals = {
      .winning_ig_name = kWinningIgName,
      .winning_ig_owner = kWinningIgOwner,
      .winning_bid = winning_bid,
      .highest_scoring_other_bid = highest_scoring_other_bid,
      .highest_scoring_other_bid_ig_owner = kWinningIgOwner,
      .has_highest_scoring_other_bid = true,
      .winning_score = 0.0,
      .winning_ad_render_url = kEmptyString,
      .rejection_reason_map = std::move(rejection_reason_map)};

  absl::string_view test_ig_name = "test_ig_name";
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      GetPlaceholderDataForInterestGroup(kWinningIgOwner, test_ig_name,
                                         signals);

  EXPECT_TRUE(placeholder->made_winning_bid);
  EXPECT_TRUE(placeholder->made_highest_scoring_other_bid);
  EXPECT_EQ(placeholder->winning_bid, signals.winning_bid);
  EXPECT_EQ(placeholder->highest_scoring_other_bid,
            signals.highest_scoring_other_bid);
}

TEST(GetPlaceholderDataForInterestGroupOwnerTest, IgHasRejectionReason) {
  float winning_bid = 1.9;
  absl::flat_hash_map<std::string, SellerRejectionReason>
      ig_name_rejection_reason_map;
  ig_name_rejection_reason_map.emplace(
      kWinningIgName, SellerRejectionReason::BID_BELOW_AUCTION_FLOOR);
  absl::flat_hash_map<std::string,
                      absl::flat_hash_map<std::string, SellerRejectionReason>>
      rejection_reason_map;
  rejection_reason_map.emplace(kWinningIgOwner, ig_name_rejection_reason_map);
  PostAuctionSignals signals = {
      .winning_ig_name = kWinningIgName,
      .winning_ig_owner = kWinningIgOwner,
      .winning_bid = winning_bid,
      .highest_scoring_other_bid = 0.0,
      .highest_scoring_other_bid_ig_owner = kEmptyString,
      .has_highest_scoring_other_bid = false,
      .winning_score = 0.0,
      .winning_ad_render_url = kEmptyString,
      .rejection_reason_map = std::move(rejection_reason_map)};
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      GetPlaceholderDataForInterestGroup(kWinningIgOwner, kWinningIgName,
                                         signals);

  EXPECT_EQ(placeholder->rejection_reason,
            SellerRejectionReason::BID_BELOW_AUCTION_FLOOR);
}

void ToSellerRejectionReasonAndCompare(absl::string_view rejection_reason_str,
                                       SellerRejectionReason expected_reason) {
  SellerRejectionReason rejection_reason =
      ToSellerRejectionReason(rejection_reason_str);
  EXPECT_EQ(rejection_reason, expected_reason);
}

void ToSellerRejectionReasonStringAndCompare(
    SellerRejectionReason rejection_reason, absl::string_view expected_str) {
  absl::string_view rejection_reason_str =
      ToSellerRejectionReasonString(rejection_reason);
  EXPECT_EQ(rejection_reason_str, expected_str);
}

TEST(SellerRejectionReasonTest, ToSellerRejectionReasonEmptyString) {
  ToSellerRejectionReasonAndCompare(
      "", SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
}

TEST(SellerRejectionReasonTest, ToSellerRejectionReasonNullString) {
  absl::string_view rejection_reason_str;
  ToSellerRejectionReasonAndCompare(
      rejection_reason_str,
      SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
}

TEST(SellerRejectionReasonTest, ToSellerRejectionReasonRandomString) {
  ToSellerRejectionReasonAndCompare(
      "a_random_rejection_reason",
      SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
}

TEST(SellerRejectionReasonTest, ToSellerRejectionReasonSuccess) {
  ToSellerRejectionReasonAndCompare(
      "not-available",
      SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
  ToSellerRejectionReasonAndCompare("invalid-bid",
                                    SellerRejectionReason::INVALID_BID);
  ToSellerRejectionReasonAndCompare(
      "bid-below-auction-floor",
      SellerRejectionReason::BID_BELOW_AUCTION_FLOOR);
  ToSellerRejectionReasonAndCompare(
      "pending-approval-by-exchange",
      SellerRejectionReason::PENDING_APPROVAL_BY_EXCHANGE);
  ToSellerRejectionReasonAndCompare(
      "disapproved-by-exchange",
      SellerRejectionReason::DISAPPROVED_BY_EXCHANGE);
  ToSellerRejectionReasonAndCompare(
      "blocked-by-publisher", SellerRejectionReason::BLOCKED_BY_PUBLISHER);
  ToSellerRejectionReasonAndCompare("language-exclusions",
                                    SellerRejectionReason::LANGUAGE_EXCLUSIONS);
  ToSellerRejectionReasonAndCompare("category-exclusions",
                                    SellerRejectionReason::CATEGORY_EXCLUSIONS);
}

TEST(SellerRejectionReasonTest, ToSellerRejectionReasonStringSuccess) {
  ToSellerRejectionReasonStringAndCompare(
      SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE,
      "not-available");
  ToSellerRejectionReasonStringAndCompare(SellerRejectionReason::INVALID_BID,
                                          "invalid-bid");
  ToSellerRejectionReasonStringAndCompare(
      SellerRejectionReason::BID_BELOW_AUCTION_FLOOR,
      "bid-below-auction-floor");
  ToSellerRejectionReasonStringAndCompare(
      SellerRejectionReason::PENDING_APPROVAL_BY_EXCHANGE,
      "pending-approval-by-exchange");
  ToSellerRejectionReasonStringAndCompare(
      SellerRejectionReason::DISAPPROVED_BY_EXCHANGE,
      "disapproved-by-exchange");
  ToSellerRejectionReasonStringAndCompare(
      SellerRejectionReason::BLOCKED_BY_PUBLISHER, "blocked-by-publisher");
  ToSellerRejectionReasonStringAndCompare(
      SellerRejectionReason::LANGUAGE_EXCLUSIONS, "language-exclusions");
  ToSellerRejectionReasonStringAndCompare(
      SellerRejectionReason::CATEGORY_EXCLUSIONS, "category-exclusions");
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
