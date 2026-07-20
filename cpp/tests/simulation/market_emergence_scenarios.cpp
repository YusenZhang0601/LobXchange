#include "lobx/simulation/emergence_runner.hpp"

#include "test_helpers/test_framework.hpp"

using namespace lobx::sim;

namespace {

EmergenceConfig scenario(int makers, int initial_qty, const std::string& extra_strategy = "noise_trader") {
  MarketEnvironmentConfig env{};
  env.market_symbol = "BTC-USDT";
  env.reference_price = 100;
  env.ticks = 60;
  env.warmup_ticks = 5;
  env.initial_book = {InitialBookLevel{lob::Side::Bid, 99, initial_qty}, InitialBookLevel{lob::Side::Ask, 101, initial_qty}};

  AgentGroupConfig maker{};
  maker.strategy_type = "market_maker";
  maker.count = makers;
  maker.name_prefix = "mm";
  maker.latency_range = LatencyRangeConfig{0, 0, 0, 0, 1, 1, 1, 1};
  maker.param_ranges = {{"bid_px", DoubleRange{98, 99}}, {"ask_px", DoubleRange{101, 102}}, {"qty", DoubleRange{1, 2}}};

  AgentGroupConfig extra{};
  extra.strategy_type = extra_strategy;
  extra.count = 2;
  extra.name_prefix = "extra";
  extra.latency_range = LatencyRangeConfig{0, 1, 0, 1, 1, 1, 1, 1};
  extra.param_ranges = {{"seed", DoubleRange{1, 100}},
                        {"side", DoubleRange{0, 0}},
                        {"target_qty", DoubleRange{1, 1}},
                        {"limit_price", DoubleRange{101, 101}},
                        {"max_avg_price", DoubleRange{101, 101}},
                        {"bid_px", DoubleRange{98, 99}},
                        {"ask_px", DoubleRange{101, 102}},
                        {"qty", DoubleRange{1, 1}}};

  return EmergenceConfig{env, AgentPopulationConfig{99, 10, {maker, extra}}};
}

} // namespace

TEST(MarketEmergenceScenarios, BalancedMarketMaintainsNonEmptySpreadSamples) {
  const EmergenceRunResult result = EmergenceRunner().run(scenario(3, 10));

  EXPECT_TRUE_MSG(result.ok, result.reason);
  EXPECT_TRUE(result.metrics.measured_ticks > 0);
  EXPECT_TRUE(result.metrics.median_spread >= 0.0L);
}

TEST(MarketEmergenceScenarios, MoreMarketMakersIncreaseAverageTopDepth) {
  const EmergenceRunResult few = EmergenceRunner().run(scenario(1, 5));
  const EmergenceRunResult many = EmergenceRunner().run(scenario(6, 5));

  EXPECT_TRUE_MSG(few.ok, few.reason);
  EXPECT_TRUE_MSG(many.ok, many.reason);
  EXPECT_TRUE(many.metrics.mean_depth_top5 >= few.metrics.mean_depth_top5);
}

TEST(MarketEmergenceScenarios, ComplexStrategiesAreNoLongerSilentlyMappedToResearchFallbacks) {
  const EmergenceRunResult thin = EmergenceRunner().run(scenario(1, 1, "adversarial_sweeper"));

  EXPECT_FALSE(thin.ok);
  EXPECT_TRUE(thin.reason.find("unknown strategy_type: adversarial_sweeper") != std::string::npos);
}

TEST(MarketEmergenceScenarios, AdversarialSweeperRequiresAgentRuntimePath) {
  const EmergenceRunResult result = EmergenceRunner().run(scenario(1, 1, "adversarial_sweeper"));

  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(result.reason.find("unknown strategy_type: adversarial_sweeper") != std::string::npos);
}

TEST(MarketEmergenceScenarios, MomentumRequiresAgentRuntimePath) {
  const EmergenceRunResult momentum = EmergenceRunner().run(scenario(2, 5, "momentum"));

  EXPECT_FALSE(momentum.ok);
  EXPECT_TRUE(momentum.reason.find("unknown strategy_type: momentum") != std::string::npos);
}

TEST(MarketEmergenceScenarios, MeanReversionRequiresAgentRuntimePath) {
  const EmergenceRunResult mean_reversion = EmergenceRunner().run(scenario(2, 5, "mean_reversion"));

  EXPECT_FALSE(mean_reversion.ok);
  EXPECT_TRUE(mean_reversion.reason.find("unknown strategy_type: mean_reversion") != std::string::npos);
}
