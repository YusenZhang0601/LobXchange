#include "lobx/simulation/market_phenomenon.hpp"
#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <string>
#include <vector>

using namespace lobx_test;

namespace {

using lobx::sim::MarketPhenomenonType;
using lobx::sim::PhenomenonEvent;
using lobx::sim::PhenomenonTrade;

void expect_ok(const lobx::Result& result) {
  EXPECT_TRUE_MSG(result.ok, result.reason);
}

void expect_ok(const lobx::SubmitResult& result) {
  EXPECT_TRUE_MSG(result.accepted, result.reason);
}

bool has_type(const std::vector<lobx::sim::MarketPhenomenon>& phenomena, MarketPhenomenonType type) {
  for (const auto& phenomenon : phenomena) {
    if (phenomenon.type == type) return true;
  }
  return false;
}

std::string explanation_for(const std::vector<lobx::sim::MarketPhenomenon>& phenomena,
                            MarketPhenomenonType type) {
  for (const auto& phenomenon : phenomena) {
    if (phenomenon.type == type) return phenomenon.explanation;
  }
  return {};
}

std::vector<PhenomenonTrade> trades(std::initializer_list<lob::Tick> prices) {
  std::vector<PhenomenonTrade> out;
  int step = 1;
  for (const lob::Tick price : prices) {
    out.push_back(PhenomenonTrade{step, price, 1, step % 2 == 0 ? lob::Side::Ask : lob::Side::Bid});
    ++step;
  }
  return out;
}

std::vector<PhenomenonTrade> exchange_trades(const ExchangeFixture& f) {
  std::vector<PhenomenonTrade> out;
  for (const auto& trade : f.exchange.trades()) {
    const lob::Side aggressor = trade.liquidity_side == lob::Side::Ask ? lob::Side::Bid : lob::Side::Ask;
    out.push_back(PhenomenonTrade{trade.ts, trade.price, trade.qty, aggressor});
  }
  return out;
}

std::vector<PhenomenonEvent> exchange_events(const ExchangeFixture& f) {
  std::vector<PhenomenonEvent> out;
  for (const auto& event : f.exchange.events().records()) {
    out.push_back(PhenomenonEvent{event.ts, event.type, event.payload});
  }
  return out;
}

std::vector<lobx::PerpRiskTier> high_maintenance_tiers() {
  return {lobx::PerpRiskTier{0, 0, 1000, 10000, 10}};
}

void configure_perp_for_liquidation(ExchangeFixture& f) {
  expect_ok(f.exchange.set_perp_risk_tiers(f.perp_market_id, high_maintenance_tiers()));
  expect_ok(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::IndexPrice));
  lobx::LiquidationOptions options;
  options.mode = lobx::LiquidationMode::InfiniteInsurance;
  f.exchange.set_liquidation_options(options);
  f.exchange.set_leverage(f.alice, f.perp_symbol, 10);
  f.exchange.set_leverage(f.bob, f.perp_symbol, 10);
  f.exchange.set_leverage(f.carol, f.perp_symbol, 10);
}

void create_last_price_stop_hunt(ExchangeFixture& f) {
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, 960001, lob::Side::Ask,
                                    106, 2, lob::POST_ONLY, 1));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.carol, 960002, lob::Side::Bid,
                                    94, 2, lob::POST_ONLY, 2));
  expect_ok(f.exchange.create_trigger_order(f.perp_symbol, f.alice, 960010,
                                            lob::Side::Bid, 1, 105,
                                            lobx::TriggerPriceType::Last,
                                            lobx::TriggerCondition::AboveOrEqual,
                                            lobx::TriggerChildOrderType::Market,
                                            0, 120, lob::NONE, 3));
  expect_ok(f.exchange.create_trigger_order(f.perp_symbol, f.alice, 960011,
                                            lob::Side::Ask, 1, 95,
                                            lobx::TriggerPriceType::Last,
                                            lobx::TriggerCondition::BelowOrEqual,
                                            lobx::TriggerChildOrderType::Market,
                                            0, 90, lob::NONE, 4));

  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.alice, 960020, lob::Side::Bid,
                                    106, 1, lob::IOC, 5));
  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Last, 6), 1);
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.alice, 960021, lob::Side::Ask,
                                    94, 1, lob::IOC, 7));
  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Last, 8), 1);
}

void create_liquidation_cascade(ExchangeFixture& f) {
  configure_perp_for_liquidation(f);
  f.deposit(f.bob, "USDT", 3000000);
  f.deposit(40, "USDT", 1000000);
  f.exchange.set_leverage(40, f.perp_symbol, 10);
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, 960101, lob::Side::Ask,
                                    100, 300000, lob::POST_ONLY, 1));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.alice, 960102, lob::Side::Bid,
                                    100, 100000, lob::IOC, 2));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.carol, 960103, lob::Side::Bid,
                                    100, 100000, lob::IOC, 3));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, 40, 960104, lob::Side::Bid,
                                    100, 100000, lob::IOC, 4));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 1));
  expect_ok(f.exchange.liquidate_if_needed(f.perp_symbol, f.alice, 10));
  expect_ok(f.exchange.liquidate_if_needed(f.perp_symbol, f.carol, 11));
  expect_ok(f.exchange.liquidate_if_needed(f.perp_symbol, 40, 12));
}

} // namespace

TEST(MarketPhenomenaDetection, DetectsRepeatedRangeSweep) {
  const auto result = lobx::sim::detect_market_phenomena(
      trades({100, 101, 100, 101, 100, 101}));

  EXPECT_TRUE(has_type(result, MarketPhenomenonType::RepeatedRangeSweep));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::WickSpike));
}

TEST(MarketPhenomenaDetection, DetectsWickSpikeUpAndRevert) {
  const auto result = lobx::sim::detect_market_phenomena(
      trades({100, 101, 120, 102, 101}));

  EXPECT_TRUE(has_type(result, MarketPhenomenonType::WickSpike));
  const std::string explanation = explanation_for(result, MarketPhenomenonType::WickSpike);
  EXPECT_TRUE_MSG(explanation.find("high=120") != std::string::npos, explanation);
  EXPECT_TRUE_MSG(explanation.find("close=101") != std::string::npos, explanation);
  EXPECT_TRUE_MSG(explanation.find("wick_ratio=") != std::string::npos, explanation);
}

TEST(MarketPhenomenaDetection, DetectsWickSpikeDownAndRevert) {
  const auto result = lobx::sim::detect_market_phenomena(
      trades({100, 99, 80, 98, 100}));

  EXPECT_TRUE(has_type(result, MarketPhenomenonType::WickSpike));
  const std::string explanation = explanation_for(result, MarketPhenomenonType::WickSpike);
  EXPECT_TRUE_MSG(explanation.find("direction=down") != std::string::npos, explanation);
  EXPECT_TRUE_MSG(explanation.find("low=80") != std::string::npos, explanation);
}

TEST(MarketPhenomenaDetection, DetectsLongShortStopHuntUsingTriggers) {
  auto f = ExchangeFixture::Perp();
  create_last_price_stop_hunt(f);

  const auto result = lobx::sim::detect_market_phenomena(exchange_trades(f), exchange_events(f));

  EXPECT_TRUE(has_type(result, MarketPhenomenonType::LongShortStopHunt));
}

TEST(MarketPhenomenaDetection, DetectsLiquidationCascadeInInfiniteInsuranceMode) {
  auto f = ExchangeFixture::Perp();
  create_liquidation_cascade(f);
  auto price_path = trades({100, 80, 40, 1});

  const auto result = lobx::sim::detect_market_phenomena(price_path, exchange_events(f));

  EXPECT_TRUE(has_type(result, MarketPhenomenonType::LiquidationCascade));
  EXPECT_EQ(f.exchange.bad_debt(f.perp_symbol), 0);
}

TEST(MarketPhenomenaDetection, DoesNotFalsePositiveOnNormalTrend) {
  const auto result = lobx::sim::detect_market_phenomena(
      trades({100, 101, 102, 103, 104}));

  EXPECT_FALSE(has_type(result, MarketPhenomenonType::RepeatedRangeSweep));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::LongShortStopHunt));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::WickSpike));
}
