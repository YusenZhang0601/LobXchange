#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

std::vector<lobx::PerpRiskTier> liquidation_tiers() {
  return {lobx::PerpRiskTier{0, 0, 1000, 500, 10}};
}

std::vector<lobx::PerpRiskTier> full_margin_liquidation_tiers() {
  return {lobx::PerpRiskTier{0, 0, 1000, 10000, 10}};
}

int event_count(const lobx::EventStore& events, const std::string& type) {
  int count = 0;
  for (const auto& record : events.records()) {
    if (record.type == type) ++count;
  }
  return count;
}

int event_total(const lobx::EventStore& events) {
  return static_cast<int>(events.records().size());
}

void open_long(PerpEngineFixture& f, lob::Quantity qty = 100) {
  EXPECT_TRUE(f.submit(f.bob, 72001, lob::Side::Ask, 100, qty, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 72002, lob::Side::Bid, 100, qty, lob::IOC, 2).accepted);
}

void open_short(PerpEngineFixture& f, lob::Quantity qty = 100) {
  EXPECT_TRUE(f.submit(f.bob, 72011, lob::Side::Bid, 100, qty, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 72012, lob::Side::Ask, 100, qty, lob::IOC, 2).accepted);
}

} // namespace

TEST(PerpLiquidation, PERP_LIQ_001LongPriceDropTriggersLiquidatable) {
  PerpEngineFixture f(2100);
  EXPECT_TRUE(f.engine.set_risk_tiers(liquidation_tiers()).ok);
  open_long(f);
  EXPECT_TRUE(f.engine.set_index_price(1).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);

  EXPECT_TRUE(f.engine.is_liquidatable(f.alice));
}

TEST(PerpLiquidation, PERP_LIQ_002ShortPriceRiseTriggersLiquidatable) {
  PerpEngineFixture f(2100);
  EXPECT_TRUE(f.engine.set_risk_tiers(liquidation_tiers()).ok);
  open_short(f);
  EXPECT_TRUE(f.engine.set_index_price(200).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);

  EXPECT_TRUE(f.engine.is_liquidatable(f.alice));
}

TEST(PerpLiquidation, PERP_LIQ_003NoPositionIsNotLiquidatable) {
  PerpEngineFixture f(2100);
  EXPECT_TRUE(f.engine.set_risk_tiers(liquidation_tiers()).ok);
  EXPECT_TRUE(f.engine.set_index_price(1).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);

  EXPECT_FALSE(f.engine.is_liquidatable(f.alice));
}

TEST(PerpLiquidation, PERP_LIQ_004PriceRecoveryClearsLiquidatable) {
  PerpEngineFixture f(2100);
  EXPECT_TRUE(f.engine.set_risk_tiers(liquidation_tiers()).ok);
  open_long(f);
  EXPECT_TRUE(f.engine.set_index_price(1).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  EXPECT_TRUE(f.engine.is_liquidatable(f.alice));

  EXPECT_TRUE(f.engine.set_index_price(100).ok);

  EXPECT_FALSE(f.engine.is_liquidatable(f.alice));
}

TEST(PerpLiquidation, PERP_LIQ_005FullLiquidationClearsPositionAndReleasesMargin) {
  PerpEngineFixture f(10000);
  EXPECT_TRUE(f.engine.set_risk_tiers(full_margin_liquidation_tiers()).ok);
  open_long(f);
  EXPECT_TRUE(f.engine.set_index_price(1).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  EXPECT_TRUE(f.engine.is_liquidatable(f.alice));

  auto result = f.engine.liquidate_position(f.alice, f.carol, 10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
  EXPECT_EQ(f.ledger.locked(f.alice, f.margin_asset), 0);
}

TEST(PerpLiquidation, PERP_LIQ_006LiquidationEventRecorded) {
  PerpEngineFixture f(10000);
  EXPECT_TRUE(f.engine.set_risk_tiers(full_margin_liquidation_tiers()).ok);
  open_long(f);
  EXPECT_TRUE(f.engine.set_index_price(1).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  EXPECT_TRUE(f.engine.is_liquidatable(f.alice));
  const int before = event_count(f.events, "liquidation");

  auto result = f.engine.liquidate_position(f.alice, f.carol, 10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(event_count(f.events, "liquidation"), before + 1);
}

TEST(PerpLiquidation, PERP_LIQ_007LiquidationSettlementFailureRollsBack) {
  PerpEngineFixture f(1000);
  EXPECT_TRUE(f.submit(f.bob, 72031, lob::Side::Ask, 100, 20, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 72032, lob::Side::Bid, 100, 20, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.engine.set_index_price(1).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{0, 0, 10000}).ok);
  const auto position_before = f.positions.position(f.alice, f.market.id);
  const auto balance_before = f.ledger.balance(f.alice, f.margin_asset);
  const auto bad_debt_before = f.engine.bad_debt();
  const int events_before = event_count(f.events, "liquidation");

  auto result = f.engine.liquidate_position(f.alice, f.carol, 10);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, position_before.signed_qty);
  const auto balance_after = f.ledger.balance(f.alice, f.margin_asset);
  EXPECT_EQ(balance_after.total, balance_before.total);
  EXPECT_EQ(balance_after.free, balance_before.free);
  EXPECT_EQ(balance_after.locked, balance_before.locked);
  EXPECT_EQ(f.engine.bad_debt(), bad_debt_before);
  EXPECT_EQ(event_count(f.events, "liquidation"), events_before);
}

TEST(PerpLiquidation, PERP_LIQ_010HealthyAccountLiquidationRejected) {
  PerpEngineFixture f(10000);
  EXPECT_TRUE(f.engine.set_risk_tiers(liquidation_tiers()).ok);
  open_long(f, 10);
  EXPECT_TRUE(f.engine.set_index_price(90).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  EXPECT_FALSE(f.engine.is_liquidatable(f.alice));

  auto result = f.engine.liquidate_position(f.alice, f.carol, 10);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.code, lobx::RejectCode::InvalidNotional);
}

TEST(PerpLiquidation, PERP_LIQ_011LiquidatableAccountLiquidationSucceeds) {
  PerpEngineFixture f(10000);
  EXPECT_TRUE(f.engine.set_risk_tiers(full_margin_liquidation_tiers()).ok);
  open_long(f);
  EXPECT_TRUE(f.engine.set_index_price(1).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  EXPECT_TRUE(f.engine.is_liquidatable(f.alice));

  auto result = f.engine.liquidate_position(f.alice, f.carol, 10);

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
}

TEST(PerpLiquidation, PERP_LIQ_012LiquidationRejectionHasNoSideEffects) {
  PerpEngineFixture f(10000);
  EXPECT_TRUE(f.engine.set_risk_tiers(liquidation_tiers()).ok);
  open_long(f, 10);
  EXPECT_TRUE(f.engine.set_index_price(90).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  const auto position_before = f.positions.position(f.alice, f.market.id);
  const auto balance_before = f.ledger.balance(f.alice, f.margin_asset);
  const int events_before = event_total(f.events);

  auto result = f.engine.liquidate_position(f.alice, f.carol, 10);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, position_before.signed_qty);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).entry_price, position_before.entry_price);
  const auto balance_after = f.ledger.balance(f.alice, f.margin_asset);
  EXPECT_EQ(balance_after.total, balance_before.total);
  EXPECT_EQ(balance_after.free, balance_before.free);
  EXPECT_EQ(balance_after.locked, balance_before.locked);
  EXPECT_EQ(event_total(f.events), events_before);
}

TEST(PerpLiquidation, PERP_LIQ_008BankruptcyPriceLong) {
  PerpEngineFixture f(10000);
  open_long(f, 10);

  EXPECT_EQ(f.engine.bankruptcy_price(f.alice), 80);
}

TEST(PerpLiquidation, PERP_LIQ_009BankruptcyPriceShort) {
  PerpEngineFixture f(10000);
  open_short(f, 10);

  EXPECT_EQ(f.engine.bankruptcy_price(f.alice), 120);
}
