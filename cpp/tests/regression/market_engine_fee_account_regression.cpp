#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/snapshot_state.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

using namespace lobx_test;

namespace {

constexpr lobx::UserId kFeeAccount = std::numeric_limits<lobx::UserId>::max();

void force_fee_credit_overflow(SpotEngineFixture& f) {
  const auto deposit = f.ledger.deposit(kFeeAccount, f.quote_asset, std::numeric_limits<lobx::Amount>::max());
  EXPECT_TRUE_MSG(deposit.ok, "setup fee account max balance reason=" + deposit.reason);
}

int trade_event_count(const lobx::EventStore& events) {
  int count = 0;
  for (const auto& event : events.records()) {
    if (event.type == "trade") ++count;
  }
  return count;
}

} // namespace

TEST(MarketEngineFeeAccountRegression, FeeCreditGoesToDedicatedFeeAccount) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  EXPECT_TRUE(f.submit(f.alice, 42001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto buy = f.submit(f.bob, 42002, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE_MSG(buy.accepted, "taker buy should settle reason=" + buy.reason);
  EXPECT_EQ_MSG(f.ledger.balance(kFeeAccount, f.quote_asset).total, 1,
                "fee account should receive taker fee");
}

TEST(MarketEngineFeeAccountRegression, RegularUserZeroCannotAccidentallyReceiveFees) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  constexpr lobx::UserId user_zero = 0;
  EXPECT_TRUE(f.ledger.deposit(user_zero, f.base_asset, 1000).ok);
  const auto user_zero_quote_before = f.ledger.balance(user_zero, f.quote_asset).total;
  EXPECT_TRUE(f.submit(f.alice, 42011, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto buy = f.submit(f.bob, 42012, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE(buy.accepted);
  EXPECT_EQ_MSG(f.ledger.balance(user_zero, f.quote_asset).total, user_zero_quote_before,
                "ordinary user id 0 balance must not be indistinguishable from fee account revenue");
  EXPECT_EQ_MSG(f.ledger.balance(kFeeAccount, f.quote_asset).total, 1,
                "dedicated fee account should receive the taker fee");
}

TEST(MarketEngineFeeAccountRegression, FeeCreditFailureRollsBackSpotSettlement) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  force_fee_credit_overflow(f);
  EXPECT_TRUE(f.submit(f.alice, 42021, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto before = EngineSnapshot::capture(f);

  auto buy = f.submit(f.bob, 42022, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_FALSE_MSG(buy.accepted, "fee account credit overflow should reject the order");
  const auto after = EngineSnapshot::capture(f);
  EXPECT_TRUE_MSG(same_book_and_open(before, after), "fee credit failure must not mutate book/open");
  EXPECT_TRUE_MSG(same_balances(before, after), "fee credit failure must roll back buyer/seller/fee ledger changes");
}

TEST(MarketEngineFeeAccountRegression, FeeCreditFailureDoesNotAppendTradeEvent) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  force_fee_credit_overflow(f);
  EXPECT_TRUE(f.submit(f.alice, 42031, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const int before = trade_event_count(f.events);

  auto buy = f.submit(f.bob, 42032, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_FALSE(buy.accepted);
  EXPECT_EQ_MSG(trade_event_count(f.events), before, "fee credit failure must not append committed trade event");
}

TEST(MarketEngineFeeAccountRegression, FeeCreditFailureDoesNotLeaveBuyerBaseCredited) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  force_fee_credit_overflow(f);
  EXPECT_TRUE(f.submit(f.alice, 42041, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto before = f.ledger.balance(f.bob, f.base_asset).total;

  EXPECT_FALSE(f.submit(f.bob, 42042, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.base_asset).total, before,
                "fee credit failure must not leave buyer base credited");
}

TEST(MarketEngineFeeAccountRegression, FeeCreditFailureDoesNotLeaveSellerQuoteCredited) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  force_fee_credit_overflow(f);
  EXPECT_TRUE(f.submit(f.alice, 42051, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto before = f.ledger.balance(f.alice, f.quote_asset).total;

  EXPECT_FALSE(f.submit(f.bob, 42052, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  EXPECT_EQ_MSG(f.ledger.balance(f.alice, f.quote_asset).total, before,
                "fee credit failure must not leave seller quote credited");
}

TEST(MarketEngineFeeAccountRegression, FeeAccountingConservesAssetsIncludingFeeAccount) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  EXPECT_TRUE(f.submit(f.alice, 42061, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto total_before = f.ledger.balance(f.alice, f.quote_asset).total +
                            f.ledger.balance(f.bob, f.quote_asset).total +
                            f.ledger.balance(kFeeAccount, f.quote_asset).total;

  auto buy = f.submit(f.bob, 42062, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE(buy.accepted);
  const auto total_after = f.ledger.balance(f.alice, f.quote_asset).total +
                           f.ledger.balance(f.bob, f.quote_asset).total +
                           f.ledger.balance(kFeeAccount, f.quote_asset).total;
  EXPECT_EQ_MSG(total_after, total_before, "quote asset should be conserved when fee account is included");
}

TEST(MarketEngineFeeAccountRegression, TakerFeeRoundedDownConsistently) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/1);
  EXPECT_TRUE(f.submit(f.alice, 42071, lob::Side::Ask, 9999, 1, lob::POST_ONLY, 1).accepted);

  auto buy = f.submit(f.bob, 42072, lob::Side::Bid, 9999, 1, lob::IOC, 2);

  EXPECT_TRUE(buy.accepted);
  EXPECT_EQ_MSG(f.ledger.balance(kFeeAccount, f.quote_asset).total, 0,
                "9999 * 1bps should round down to zero fee with integer floor");
}

TEST(MarketEngineFeeAccountRegression, FeeOverflowRejectsOrderInsteadOfZeroFee) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/std::numeric_limits<int>::max());

  auto order = f.submit(f.bob, 42081, lob::Side::Bid, 100000000000LL, 1000000LL, lob::POST_ONLY, 1);

  EXPECT_FALSE_MSG(order.accepted, "fee overflow should reject instead of silently producing zero fee");
  EXPECT_EQ(order.code, lobx::RejectCode::InvalidNotional);
}
