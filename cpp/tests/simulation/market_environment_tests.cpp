#include "lobx/simulation/config_loader.hpp"
#include "lobx/simulation/emergence_runner.hpp"

#include "test_helpers/test_framework.hpp"

using namespace lobx::sim;

namespace {

std::string market_json() {
  return R"JSON({
    "market_environment": {
      "market_symbol": "BTC-USDT",
      "reference_price": 100,
      "ticks": 20,
      "warmup_ticks": 5,
      "initial_book": [
        {"side": "bid", "price": 99, "qty": 2},
        {"side": "ask", "price": 101, "qty": 2}
      ],
      "noise_intensity": 0.4,
      "liquidity_scale": 1.0,
      "volatility_regime": 1.0,
      "spread_regime": 1.0
    }
  })JSON";
}

AgentPopulationConfig one_agent_population() {
  AgentGroupConfig group{};
  group.strategy_type = "market_maker";
  group.count = 1;
  group.name_prefix = "mm";
  group.latency_range = LatencyRangeConfig{0, 0, 0, 0, 1, 1, 1, 1};
  group.param_ranges = {{"bid_px", DoubleRange{98, 98}}, {"ask_px", DoubleRange{102, 102}}, {"qty", DoubleRange{1, 1}}};
  return AgentPopulationConfig{42, 10, {group}};
}

} // namespace

TEST(MarketEnvironmentTests, MarketEnvironmentLoadsFromJson) {
  const MarketEnvironmentConfigLoadResult loaded = load_market_environment_from_json_string(market_json());

  EXPECT_TRUE_MSG(loaded.ok, loaded.reason);
  EXPECT_EQ(loaded.config.market_symbol, std::string("BTC-USDT"));
  EXPECT_EQ(loaded.config.reference_price, 100);
  EXPECT_EQ(loaded.config.initial_book.size(), 2UL);
}

TEST(MarketEnvironmentTests, MarketEnvironmentRoundTripDeterministic) {
  const MarketEnvironmentConfigLoadResult loaded = load_market_environment_from_json_string(market_json());
  EXPECT_TRUE_MSG(loaded.ok, loaded.reason);

  const std::string first = market_environment_to_json(loaded.config);
  const MarketEnvironmentConfigLoadResult reloaded = load_market_environment_from_json_string(first);
  const std::string second = market_environment_to_json(reloaded.config);

  EXPECT_TRUE_MSG(reloaded.ok, reloaded.reason);
  EXPECT_EQ(first, second);
}

TEST(MarketEnvironmentTests, MarketEnvironmentRejectsNonPositiveReferencePrice) {
  MarketEnvironmentConfig config{};
  config.reference_price = 0;
  config.ticks = 10;

  EXPECT_FALSE(validate_market_environment(config).ok);
}

TEST(MarketEnvironmentTests, MarketEnvironmentRejectsInvalidWarmupTicks) {
  MarketEnvironmentConfig config{};
  config.reference_price = 100;
  config.ticks = 10;
  config.warmup_ticks = 10;

  EXPECT_FALSE(validate_market_environment(config).ok);
}

TEST(MarketEnvironmentTests, MarketEnvironmentRejectsInvalidInitialBookLevel) {
  MarketEnvironmentConfig config{};
  config.reference_price = 100;
  config.ticks = 10;
  config.initial_book.push_back(InitialBookLevel{lob::Side::Bid, 0, 1});

  EXPECT_FALSE(validate_market_environment(config).ok);
}

TEST(MarketEnvironmentTests, InitialBookSeedsDepth) {
  MarketEnvironmentConfig env = load_market_environment_from_json_string(market_json()).config;
  EmergenceRunner runner;
  const EmergenceRunResult result = runner.run(EmergenceConfig{env, one_agent_population()});

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_TRUE(!result.research_result.bids.empty());
  EXPECT_TRUE(!result.research_result.asks.empty());
}

TEST(MarketEnvironmentTests, ReferencePriceUsedForInventoryMarking) {
  MarketEnvironmentConfig env = load_market_environment_from_json_string(market_json()).config;
  env.reference_price = 123;
  EmergenceRunner runner;
  const EmergenceRunResult result = runner.run(EmergenceConfig{env, one_agent_population()});

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_EQ(result.config.market_environment.reference_price, 123);
  EXPECT_TRUE(!result.metrics.samples.empty());
}
