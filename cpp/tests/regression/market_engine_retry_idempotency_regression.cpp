#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/snapshot_state.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

using namespace lobx_test;

namespace {

void force_fee_account_credit_failure(SpotEngineFixture& f) {
  const auto deposit = f.ledger.deposit(std::numeric_limits<lobx::UserId>::max(), f.quote_asset,
                                        std::numeric_limits<lobx::Amount>::max());
  EXPECT_TRUE_MSG(deposit.ok, "setup fee account overflow guard reason=" + deposit.reason);
}

} // namespace

TEST(MarketEngineRetryIdempotencyRegression, SettlementFailureRetrySameOrderIdDoesNotDoubleCreditBuyer) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  force_fee_account_credit_failure(f);
  EXPECT_TRUE(f.submit(f.alice, 41001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 41002, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);
  const auto before = f.ledger.balance(f.bob, f.base_asset).total;

  auto first = f.submit(f.bob, 41003, lob::Side::Bid, 100, 1, lob::IOC, 3);
  EXPECT_FALSE(first.accepted);
  auto retry = f.submit(f.bob, 41003, lob::Side::Bid, 100, 1, lob::IOC, 4);
  EXPECT_FALSE(retry.accepted);

  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.base_asset).total, before,
                "failed settlement retry with same order id must not credit buyer base repeatedly");
}

TEST(MarketEngineRetryIdempotencyRegression, SettlementFailureRetrySameOrderIdDoesNotDoubleCreditSeller) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  force_fee_account_credit_failure(f);
  EXPECT_TRUE(f.submit(f.alice, 41101, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 41102, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);
  const auto alice_quote_before = f.ledger.balance(f.alice, f.quote_asset).total;
  const auto carol_quote_before = f.ledger.balance(f.carol, f.quote_asset).total;

  EXPECT_FALSE(f.submit(f.bob, 41103, lob::Side::Bid, 100, 1, lob::IOC, 3).accepted);
  EXPECT_FALSE(f.submit(f.bob, 41103, lob::Side::Bid, 100, 1, lob::IOC, 4).accepted);

  EXPECT_EQ_MSG(f.ledger.balance(f.alice, f.quote_asset).total, alice_quote_before,
                "first failed settlement must not credit maker quote");
  EXPECT_EQ_MSG(f.ledger.balance(f.carol, f.quote_asset).total, carol_quote_before,
                "retry failed settlement must not credit second maker quote");
}

TEST(MarketEngineRetryIdempotencyRegression, SettlementFailureRetrySameOrderIdDoesNotDoubleRemoveBookQty) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  force_fee_account_credit_failure(f);
  EXPECT_TRUE(f.submit(f.alice, 41201, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 41202, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);

  EXPECT_FALSE(f.submit(f.bob, 41203, lob::Side::Bid, 100, 1, lob::IOC, 3).accepted);
  EXPECT_FALSE(f.submit(f.bob, 41203, lob::Side::Bid, 100, 1, lob::IOC, 4).accepted);

  auto asks = f.engine.topN(lob::Side::Ask, 10);
  EXPECT_FALSE_MSG(asks.empty(), "failed settlement retry must leave original ask liquidity on book");
  EXPECT_EQ_MSG(asks[0].second, 2, "failed settlement retry must not remove book qty twice");
}

TEST(MarketEngineRetryIdempotencyRegression, SettlementFailureRetrySameOrderIdDoesNotCreateDuplicateOpenOrder) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  force_fee_account_credit_failure(f);
  EXPECT_TRUE(f.submit(f.alice, 41301, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_FALSE(f.submit(f.bob, 41302, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);
  EXPECT_FALSE(f.submit(f.bob, 41302, lob::Side::Bid, 100, 1, lob::IOC, 3).accepted);

  int matching_ids = 0;
  for (const auto& order : f.engine.open_orders()) {
    if (order.id == 41302) ++matching_ids;
  }
  EXPECT_EQ_MSG(matching_ids, 0, "failed IOC retry should not leave duplicate open order id");
}

TEST(MarketEngineRetryIdempotencyRegression, DuplicateOrderIdRejectedAfterSuccessfulAcceptedOrder) {
  SpotEngineFixture f;
  auto first = f.submit(f.alice, 41401, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1);
  EXPECT_TRUE(first.accepted);

  auto duplicate = f.submit(f.alice, 41401, lob::Side::Ask, 101, 1, lob::POST_ONLY, 2);
  EXPECT_FALSE(duplicate.accepted);
  EXPECT_EQ(duplicate.code, lobx::RejectCode::DuplicateOrderId);
}

TEST(MarketEngineRetryIdempotencyRegression, DuplicateOrderIdRejectedAfterExpiredFOKOrder) {
  SpotEngineFixture f;
  auto expired = f.submit(f.bob, 41411, lob::Side::Bid, 100, 1, lob::FOK, 1);
  EXPECT_TRUE(expired.accepted);
  EXPECT_EQ(expired.exec.filled, 0);

  auto duplicate = f.submit(f.bob, 41411, lob::Side::Bid, 100, 1, lob::FOK, 2);
  EXPECT_FALSE(duplicate.accepted);
  EXPECT_EQ(duplicate.code, lobx::RejectCode::DuplicateOrderId);
}

TEST(MarketEngineRetryIdempotencyRegression, DuplicateOrderIdPolicyAfterRejectedRiskOrderIsExplicit) {
  SpotEngineFixture f;
  auto rejected = f.submit(f.alice, 41421, lob::Side::Ask, 100, 1, 1u << 30, 1);
  EXPECT_FALSE(rejected.accepted);

  auto retry = f.submit(f.alice, 41421, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2);
  EXPECT_TRUE_MSG(retry.accepted, "risk-rejected order ids should be reusable because they were never accepted");
}
