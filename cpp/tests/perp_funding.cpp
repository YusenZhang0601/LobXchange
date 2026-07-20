#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

using namespace lobx_test;

namespace {

void open_long_short(PerpEngineFixture& f, lob::Quantity qty = 100) {
  EXPECT_TRUE(f.submit(f.bob, 74001, lob::Side::Ask, 100, qty, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 74002, lob::Side::Bid, 100, qty, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.engine.set_index_price(100).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
}

int event_count(const lobx::EventStore& events, const std::string& type) {
  int count = 0;
  for (const auto& record : events.records()) {
    if (record.type == type) ++count;
  }
  return count;
}

} // namespace

TEST(PerpFunding, PERP_FUND_001ZeroPositionNoFundingPayment) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_index_price(100).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  EXPECT_TRUE(f.engine.set_funding_rate(100).ok);

  auto result = f.engine.settle_funding(10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(f.engine.account_funding_total(f.alice), 0);
  EXPECT_EQ(event_count(f.events, "funding.settled"), 0);
}

TEST(PerpFunding, PERP_FUND_002ZeroFundingRateNoWalletMutation) {
  PerpEngineFixture f;
  open_long_short(f);
  const auto alice_before = f.ledger.balance(f.alice, f.margin_asset);
  const auto bob_before = f.ledger.balance(f.bob, f.margin_asset);
  EXPECT_TRUE(f.engine.set_funding_rate(0).ok);

  auto result = f.engine.settle_funding(10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, alice_before.total);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).total, bob_before.total);
  EXPECT_EQ(f.engine.account_funding_total(f.alice), 0);
  EXPECT_EQ(f.engine.account_funding_total(f.bob), 0);
}

TEST(PerpFunding, PERP_FUND_003PositiveFundingLongPaysShortReceives) {
  PerpEngineFixture f;
  open_long_short(f);
  EXPECT_TRUE(f.engine.set_funding_rate(100).ok);

  auto result = f.engine.settle_funding(10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(f.engine.account_funding_total(f.alice), -100);
  EXPECT_EQ(f.engine.account_funding_total(f.bob), 100);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, 999900);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).total, 1000100);
}

TEST(PerpFunding, PERP_FUND_004NegativeFundingShortPaysLongReceives) {
  PerpEngineFixture f;
  open_long_short(f);
  EXPECT_TRUE(f.engine.set_funding_rate(-100).ok);

  auto result = f.engine.settle_funding(10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(f.engine.account_funding_total(f.alice), 100);
  EXPECT_EQ(f.engine.account_funding_total(f.bob), -100);
}

TEST(PerpFunding, PERP_FUND_005FundingChangesAccountEquity) {
  PerpEngineFixture f;
  open_long_short(f);
  const lobx::Amount equity_before = f.engine.account_equity(f.alice);
  EXPECT_TRUE(f.engine.set_funding_rate(100).ok);

  auto result = f.engine.settle_funding(10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(f.engine.account_equity(f.alice), equity_before - 100);
}

TEST(PerpFunding, PERP_FUND_006FundingDoesNotMutateUnrealizedPnlDirectly) {
  PerpEngineFixture f;
  open_long_short(f);
  const lobx::Amount pnl_before = f.engine.unrealized_pnl(f.alice);
  EXPECT_TRUE(f.engine.set_funding_rate(100).ok);

  auto result = f.engine.settle_funding(10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(f.engine.unrealized_pnl(f.alice), pnl_before);
}

TEST(PerpFunding, PERP_FUND_007FundingCanMakeAccountLiquidatable) {
  PerpEngineFixture f(2200);
  EXPECT_TRUE(f.engine.set_risk_tiers({lobx::PerpRiskTier{0, 0, 1000, 2000, 10}}).ok);
  open_long_short(f);
  EXPECT_FALSE(f.engine.is_liquidatable(f.alice));
  EXPECT_TRUE(f.engine.set_funding_rate(200).ok);

  auto result = f.engine.settle_funding(10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_TRUE(f.engine.is_liquidatable(f.alice));
}

TEST(PerpFunding, PERP_FUND_008FundingEventEmittedExactlyOnce) {
  PerpEngineFixture f;
  open_long_short(f);
  EXPECT_TRUE(f.engine.set_funding_rate(100).ok);
  const int before = event_count(f.events, "funding.settled");

  auto result = f.engine.settle_funding(10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(event_count(f.events, "funding.settled"), before + 1);
  EXPECT_EQ(event_count(f.events, "funding.payment"), 2);
}

TEST(PerpFunding, PERP_FUND_009FundingRollbackRestoresWalletTotalsAndEvents) {
  PerpEngineFixture f(2100);
  open_long_short(f);
  EXPECT_TRUE(f.engine.set_funding_rate(200).ok);
  const auto alice_before = f.ledger.balance(f.alice, f.margin_asset);
  const auto bob_before = f.ledger.balance(f.bob, f.margin_asset);
  const int events_before = event_count(f.events, "funding.settled");

  auto result = f.engine.settle_funding(10);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, alice_before.total);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).free, alice_before.free);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).total, bob_before.total);
  EXPECT_EQ(f.engine.account_funding_total(f.alice), 0);
  EXPECT_EQ(f.engine.account_funding_total(f.bob), 0);
  EXPECT_EQ(event_count(f.events, "funding.settled"), events_before);
  EXPECT_EQ(event_count(f.events, "funding.payment"), 0);
}

TEST(PerpFunding, PERP_FUND_010OneSidedPositiveFundingBehaviorPinned) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.submit(f.bob, 74011, lob::Side::Ask, 100, 100, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 74012, lob::Side::Bid, 100, 100, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.submit(f.carol, 74013, lob::Side::Ask, 100, 100, lob::POST_ONLY, 3).accepted);
  EXPECT_TRUE(f.submit(f.bob, 74014, lob::Side::Bid, 100, 100, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4).accepted);
  EXPECT_TRUE(f.engine.set_index_price(100).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  EXPECT_TRUE(f.engine.set_funding_rate(100).ok);

  auto result = f.engine.settle_funding(10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(f.engine.account_funding_total(f.alice), -100);
  EXPECT_EQ(f.engine.account_funding_total(f.bob), 0);
}

TEST(PerpFunding, PERP_FUND_011OneSidedNegativeFundingBehaviorPinned) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.submit(f.bob, 74021, lob::Side::Ask, 100, 100, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 74022, lob::Side::Bid, 100, 100, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.submit(f.carol, 74023, lob::Side::Ask, 100, 100, lob::POST_ONLY, 3).accepted);
  EXPECT_TRUE(f.submit(f.bob, 74024, lob::Side::Bid, 100, 100, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4).accepted);
  EXPECT_TRUE(f.engine.set_index_price(100).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  EXPECT_TRUE(f.engine.set_funding_rate(-100).ok);

  auto result = f.engine.settle_funding(10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(f.engine.account_funding_total(f.alice), 100);
  EXPECT_EQ(f.engine.account_funding_total(f.bob), 0);
}

TEST(PerpFunding, PERP_FUND_012FundingRateIntMinRejected) {
  PerpEngineFixture f;

  auto result = f.engine.set_funding_rate(std::numeric_limits<int>::min());

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.code, lobx::RejectCode::InvalidQuantity);
}
