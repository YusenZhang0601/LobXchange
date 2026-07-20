#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

void set_free_quote(SpotEngineFixture& f, lobx::UserId user, lobx::Amount target_free) {
  const auto current = f.ledger.balance(user, f.quote_asset).free;
  if (current > target_free) {
    auto withdraw = f.ledger.withdraw(user, f.quote_asset, current - target_free);
    EXPECT_TRUE_MSG(withdraw.ok, "setup set free quote user=" + std::to_string(user) + " reason=" + withdraw.reason);
  }
}

} // namespace

TEST(MarketEngineSpotFeeRegression, SpotBuyTakerFeeDoesNotFailWhenQuoteIsLocked) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_free_quote(f, f.bob, 110);

  EXPECT_TRUE(f.submit(f.alice, 31001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto buy = f.submit(f.bob, 31002, lob::Side::Bid, 110, 1, lob::IOC, 2);

  EXPECT_TRUE_MSG(buy.accepted,
                  "buyer has enough locked quote including price improvement to pay taker fee reason=" + buy.reason);
  EXPECT_EQ(buy.exec.filled, 1);
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.base_asset).total, 1000001LL, f.ledger_summary(f.bob));
}

TEST(MarketEngineSpotFeeRegression, SpotExactNotionalWithoutFeeRejectedBeforeBookMutation) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_free_quote(f, f.bob, 100);

  EXPECT_TRUE(f.submit(f.alice, 31011, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto buy = f.submit(f.bob, 31012, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_FALSE_MSG(buy.accepted, "setup should reject exact-notional-without-fee before matching");
  EXPECT_EQ_MSG(buy.code, lobx::RejectCode::InsufficientBalance,
                "exact-notional-without-fee should be pre-trade risk rejection");
  auto asks = f.engine.topN(lob::Side::Ask, 10);
  EXPECT_FALSE_MSG(asks.empty(), "pre-trade rejection must not mutate book");
  EXPECT_EQ_MSG(asks[0].second, 1, "pre-trade rejection must preserve ask qty");
}

TEST(MarketEngineSpotFeeRegression, SpotBuyTakerPriceImprovementReleasesExcessQuoteAfterFees) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_free_quote(f, f.bob, 111);

  EXPECT_TRUE(f.submit(f.alice, 31021, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto buy = f.submit(f.bob, 31022, lob::Side::Bid, 110, 1, lob::IOC, 2);

  EXPECT_TRUE_MSG(buy.accepted, "user=bob symbol=BTC-USDT order_id=31022 side=BUY price=100 qty=1 flags=IOC reason=" + buy.reason);
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.quote_asset).locked, 0, f.ledger_summary(f.bob));
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.quote_asset).total, 10,
                "buyer should pay execution notional 100 and fee 1 from starting 111");
}

TEST(MarketEngineSpotFeeRegression, SpotSellTakerFeeChargedAfterQuoteCredit) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_free_quote(f, f.alice, 0);

  EXPECT_TRUE(f.submit(f.bob, 31031, lob::Side::Bid, 100, 1, lob::POST_ONLY, 1).accepted);
  auto sell = f.submit(f.alice, 31032, lob::Side::Ask, 100, 1, lob::IOC, 2);

  EXPECT_TRUE_MSG(sell.accepted, "user=alice symbol=BTC-USDT order_id=31032 side=SELL price=100 qty=1 flags=IOC reason=" + sell.reason);
  EXPECT_EQ_MSG(f.ledger.balance(f.alice, f.quote_asset).total, 99,
                "seller taker should receive 100 quote then pay 1 quote fee");
  EXPECT_TRUE_MSG(f.ledger.invariant_ok(), f.ledger_summary(f.alice));
}

TEST(MarketEngineSpotFeeRegression, SpotMakerDoesNotPayTakerFee) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_free_quote(f, f.bob, 101);

  EXPECT_TRUE(f.submit(f.alice, 31041, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto buy = f.submit(f.bob, 31042, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE(buy.accepted);
  EXPECT_EQ_MSG(f.ledger.balance(f.alice, f.quote_asset).total, 1000100LL,
                "maker seller should receive notional and no taker fee");
}

TEST(MarketEngineSpotFeeRegression, SpotIOCRemainingReleasesLockedQuote) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/0);
  set_free_quote(f, f.bob, 500);

  EXPECT_TRUE(f.submit(f.alice, 31051, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  auto buy = f.submit(f.bob, 31052, lob::Side::Bid, 100, 5, lob::IOC, 2);

  EXPECT_TRUE(buy.accepted);
  EXPECT_EQ(buy.exec.filled, 2);
  EXPECT_EQ(buy.exec.remaining, 3);
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.quote_asset).locked, 0, f.ledger_summary(f.bob));
  EXPECT_TRUE_MSG(f.ledger.invariant_ok(), f.ledger_summary(f.bob));
}

TEST(MarketEngineSpotFeeRegression, SpotFOKInsufficientLiquidityDoesNotLockOrTrade) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/0);
  set_free_quote(f, f.bob, 500);

  EXPECT_TRUE(f.submit(f.alice, 31061, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  const auto buyer_quote_before = f.ledger.balance(f.bob, f.quote_asset);
  auto buy = f.submit(f.bob, 31062, lob::Side::Bid, 100, 5, lob::FOK, 2);

  EXPECT_TRUE(buy.accepted);
  EXPECT_EQ(buy.exec.filled, 0);
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.quote_asset).total, buyer_quote_before.total, f.ledger_summary(f.bob));
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.quote_asset).locked, buyer_quote_before.locked, f.ledger_summary(f.bob));
}
