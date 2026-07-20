#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

std::vector<lobx::PerpRiskTier> basic_tiers() {
  return {
      lobx::PerpRiskTier{0, 1000, 1000, 500, 10},
      lobx::PerpRiskTier{1000, 0, 5000, 1000, 2},
  };
}

std::vector<lobx::PerpRiskTier> high_floor_tiers() {
  return {
      lobx::PerpRiskTier{1000, 2000, 1000, 500, 4},
      lobx::PerpRiskTier{2000, 0, 5000, 1000, 2},
  };
}

} // namespace

TEST(PerpRiskTiers, PERP_RISK_001MaintenanceMarginLong) {
  auto f = ExchangeFixture::Perp();
  EXPECT_TRUE(f.exchange.set_perp_risk_tiers(f.perp_market_id, basic_tiers()).ok);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 71001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 71002, lob::Side::Bid, 100, 10, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.exchange.set_index_price(f.perp_market_id, 100).ok);
  EXPECT_TRUE(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::IndexPrice).ok);

  EXPECT_EQ(f.exchange.maintenance_margin(f.alice, f.perp_symbol), 100);
}

TEST(PerpRiskTiers, PERP_RISK_002MaintenanceMarginShort) {
  auto f = ExchangeFixture::Perp();
  EXPECT_TRUE(f.exchange.set_perp_risk_tiers(f.perp_market_id, basic_tiers()).ok);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 71011, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 71012, lob::Side::Bid, 100, 10, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.exchange.set_index_price(f.perp_market_id, 110).ok);
  EXPECT_TRUE(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::IndexPrice).ok);

  EXPECT_EQ(f.exchange.maintenance_margin(f.bob, f.perp_symbol), 110);
}

TEST(PerpRiskTiers, PERP_RISK_003NoPositionMaintenanceMarginIsZero) {
  auto f = ExchangeFixture::Perp();
  EXPECT_TRUE(f.exchange.set_perp_risk_tiers(f.perp_market_id, basic_tiers()).ok);
  EXPECT_TRUE(f.exchange.set_index_price(f.perp_market_id, 100).ok);

  EXPECT_EQ(f.exchange.maintenance_margin(f.alice, f.perp_symbol), 0);
}

TEST(PerpRiskTiers, PERP_RISK_004NotionalHitsTier) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_risk_tiers(basic_tiers()).ok);
  EXPECT_EQ(f.engine.effective_max_leverage(f.alice, 999), 10);
  EXPECT_EQ(f.engine.effective_max_leverage(f.alice, 1000), 2);
}

TEST(PerpRiskTiers, PERP_RISK_005LargePositionMaxLeverageIsLower) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_risk_tiers(basic_tiers()).ok);

  EXPECT_EQ(f.engine.effective_max_leverage(f.alice, 5000), 2);
}

TEST(PerpRiskTiers, PERP_RISK_006SetLeverageIsClampedByTier) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_risk_tiers(basic_tiers()).ok);
  EXPECT_TRUE(f.submit(f.bob, 71021, lob::Side::Ask, 100, 20, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 71022, lob::Side::Bid, 100, 20, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.engine.set_index_price(100).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);

  f.engine.set_user_leverage(f.alice, 10);

  EXPECT_EQ(f.positions.position(f.alice, f.market.id).leverage, 2);
}

TEST(PerpRiskTiers, PERP_RISK_007CrossTierRiskCheckUsesNewTier) {
  PerpEngineFixture f(1000);
  EXPECT_TRUE(f.engine.set_risk_tiers(basic_tiers()).ok);
  f.positions.set_leverage(f.alice, f.market.id, 10);
  f.positions.set_leverage(f.bob, f.market.id, 2);
  EXPECT_TRUE(f.submit(f.bob, 71031, lob::Side::Ask, 100, 20, lob::POST_ONLY, 1).accepted);

  auto open = f.submit(f.alice, 71032, lob::Side::Bid, 100, 20, lob::IOC, 2);

  EXPECT_TRUE_MSG(open.accepted, "cross-tier open should use max leverage 2 and exact margin: " + open.reason);
  EXPECT_EQ(f.ledger.locked(f.alice, f.margin_asset), 1000);
}

TEST(PerpRiskTiers, PERP_RISK_008RejectedOrderHasNoSideEffects) {
  PerpEngineFixture f(1000);
  EXPECT_TRUE(f.engine.set_risk_tiers(basic_tiers()).ok);
  f.positions.set_leverage(f.alice, f.market.id, 10);
  f.positions.set_leverage(f.bob, f.market.id, 2);
  EXPECT_TRUE(f.submit(f.bob, 71041, lob::Side::Ask, 100, 20, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.ledger.withdraw(f.alice, f.margin_asset, 1).ok);
  const auto before = f.ledger.balance(f.alice, f.margin_asset);

  auto rejected = f.submit(f.alice, 71042, lob::Side::Bid, 100, 20, lob::IOC, 2);

  EXPECT_FALSE(rejected.accepted);
  EXPECT_EQ(rejected.code, lobx::RejectCode::InsufficientBalance);
  const auto after = f.ledger.balance(f.alice, f.margin_asset);
  EXPECT_EQ(after.total, before.total);
  EXPECT_EQ(after.free, before.free);
  EXPECT_EQ(after.locked, before.locked);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
}

TEST(PerpRiskTiers, PERP_RISK_009NotionalBelowFirstTierFloorUsesDefaultRisk) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_risk_tiers(high_floor_tiers()).ok);

  EXPECT_EQ(f.engine.effective_max_leverage(f.alice, 999), 10);
}

TEST(PerpRiskTiers, PERP_RISK_010NotionalExactlyAtTierFloorMatchesTier) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_risk_tiers(high_floor_tiers()).ok);

  EXPECT_EQ(f.engine.effective_max_leverage(f.alice, 1000), 4);
}

TEST(PerpRiskTiers, PERP_RISK_011NotionalExactlyAtCappedBoundaryUsesNextTier) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_risk_tiers(high_floor_tiers()).ok);

  EXPECT_EQ(f.engine.effective_max_leverage(f.alice, 2000), 2);
}

TEST(PerpRiskTiers, PERP_RISK_012NotionalAboveFinalOpenEndedFloorMatchesFinalTier) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_risk_tiers(high_floor_tiers()).ok);

  EXPECT_EQ(f.engine.effective_max_leverage(f.alice, 5000), 2);
}
