#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

void open_long(PerpEngineFixture& f, lob::Quantity qty = 100) {
  EXPECT_TRUE(f.submit(f.bob, 73001, lob::Side::Ask, 100, qty, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 73002, lob::Side::Bid, 100, qty, lob::IOC, 2).accepted);
}

int event_count(const lobx::EventStore& events, const std::string& type) {
  int count = 0;
  for (const auto& record : events.records()) {
    if (record.type == type) ++count;
  }
  return count;
}

std::vector<lobx::PerpRiskTier> liquidation_tiers() {
  return {lobx::PerpRiskTier{0, 0, 1000, 10000, 10}};
}

} // namespace

TEST(PerpFees, PERP_FEE_001ZeroFeeConfigNoWalletMutation) {
  PerpEngineFixture f;
  const auto alice_before = f.ledger.balance(f.alice, f.margin_asset);
  const auto bob_before = f.ledger.balance(f.bob, f.margin_asset);

  open_long(f, 10);

  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, alice_before.total);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).total, bob_before.total);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), 0);
  EXPECT_EQ(f.engine.account_fee_total(f.bob), 0);
}

TEST(PerpFees, PERP_FEE_002TakerFeeChargedOnOpen) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{10, 20, 0}).ok);

  open_long(f);

  EXPECT_EQ(f.engine.account_fee_total(f.alice), 20);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, 999980);
}

TEST(PerpFees, PERP_FEE_003MakerFeeChargedOnFill) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{10, 20, 0}).ok);

  open_long(f);

  EXPECT_EQ(f.engine.account_fee_total(f.bob), 10);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).total, 999990);
}

TEST(PerpFees, PERP_FEE_004FeeReducesAccountEquity) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{0, 20, 0}).ok);
  EXPECT_TRUE(f.engine.set_index_price(100).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  const lobx::Amount equity_before = f.engine.account_equity(f.alice);

  open_long(f);

  EXPECT_EQ(f.engine.account_equity(f.alice), equity_before - 20);
}

TEST(PerpFees, PERP_FEE_005FeeCanMakeAccountCloserToLiquidation) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{0, 20, 0}).ok);
  EXPECT_TRUE(f.engine.set_risk_tiers({lobx::PerpRiskTier{0, 0, 1000, 500, 10}}).ok);
  EXPECT_TRUE(f.engine.set_index_price(100).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  const lobx::Amount equity_before = f.engine.account_equity(f.alice);

  open_long(f);

  EXPECT_TRUE(f.engine.account_equity(f.alice) < equity_before);
  EXPECT_FALSE(f.engine.is_liquidatable(f.alice));
}

TEST(PerpFees, PERP_FEE_006FeeAccountingDoesNotDoubleCountRealizedPnl) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{10, 20, 0}).ok);
  EXPECT_TRUE(f.submit(f.bob, 73011, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 73012, lob::Side::Bid, 100, 10, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.submit(f.carol, 73013, lob::Side::Bid, 110, 10, lob::POST_ONLY, 3).accepted);

  auto close = f.submit(f.alice, 73014, lob::Side::Ask, 110, 10, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);

  EXPECT_TRUE_MSG(close.accepted, close.reason);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).realized_pnl, 100);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), 4);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, 1000096);
}

TEST(PerpFees, PERP_FEE_007LiquidationFeeBehaviorDeterministic) {
  PerpEngineFixture f(10000);
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{0, 0, 100}).ok);
  EXPECT_TRUE(f.engine.set_risk_tiers(liquidation_tiers()).ok);
  open_long(f);
  EXPECT_TRUE(f.engine.set_index_price(1).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  EXPECT_TRUE(f.engine.is_liquidatable(f.alice));

  auto result = f.engine.liquidate_position(f.alice, f.carol, 10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), 1);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, 99);
}

TEST(PerpFees, PERP_FEE_008FeeRollbackRestoresWalletPositionEventsAndFeeTotals) {
  PerpEngineFixture f(1000);
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{10000, 10000, 0}).ok);
  EXPECT_TRUE(f.submit(f.bob, 73021, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);
  const auto alice_before = f.ledger.balance(f.alice, f.margin_asset);
  const auto bob_before = f.ledger.balance(f.bob, f.margin_asset);
  const int trades_before = event_count(f.events, "trade");

  auto rejected = f.submit(f.alice, 73022, lob::Side::Bid, 100, 10, lob::IOC, 2);

  EXPECT_FALSE(rejected.accepted);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, alice_before.total);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).free, alice_before.free);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).total, bob_before.total);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).locked, bob_before.locked);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), 0);
  EXPECT_EQ(f.engine.account_fee_total(f.bob), 0);
  EXPECT_EQ(event_count(f.events, "trade"), trades_before);
  EXPECT_EQ(event_count(f.events, "perp.fee_charged"), 0);
}

TEST(PerpFees, PERP_FEE_009RejectedOrderNoFee) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{10, 20, 0}).ok);
  EXPECT_TRUE(f.submit(f.bob, 73031, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);

  auto rejected = f.submit(f.alice, 73032, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2);

  EXPECT_FALSE(rejected.accepted);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), 0);
  EXPECT_EQ(f.engine.account_fee_total(f.bob), 0);
  EXPECT_EQ(event_count(f.events, "perp.fee_charged"), 0);
}

TEST(PerpFees, PERP_FEE_010ReduceOnlyFillChargesFee) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{10, 20, 0}).ok);
  EXPECT_TRUE(f.submit(f.bob, 73041, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 73042, lob::Side::Bid, 100, 10, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.submit(f.carol, 73043, lob::Side::Bid, 110, 10, lob::POST_ONLY, 3).accepted);

  auto reduce = f.submit(f.alice, 73044, lob::Side::Ask, 110, 10, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);

  EXPECT_TRUE_MSG(reduce.accepted, reduce.reason);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), 4);
  EXPECT_EQ(event_count(f.events, "perp.fee_charged"), 4);
}

TEST(PerpFees, PERP_FEE_011FeeEventRecorded) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{10, 20, 0}).ok);
  const int before = event_count(f.events, "perp.fee_charged");

  open_long(f);

  EXPECT_EQ(event_count(f.events, "perp.fee_charged"), before + 2);
}
