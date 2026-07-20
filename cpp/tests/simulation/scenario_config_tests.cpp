#include "test_helpers/scenario_config.hpp"
#include "test_helpers/test_framework.hpp"

#include <string>

using namespace lobx_test;

namespace {

ScenarioConfig base_scenario(uint64_t seed = 42) {
  return ScenarioConfig{seed,
                        30,
                        "BTC-USDT",
                        {BotConfig{10,
                                   "mm",
                                   "market_maker",
                                   TestLatencyModel{0, 0, 1},
                                   {{"bid_px", 99}, {"ask_px", 101}, {"qty", 1}}},
                         BotConfig{20,
                                   "taker",
                                   "taker_sweep",
                                   TestLatencyModel{0, 0, 1},
                                   {{"side", 0}, {"target_qty", 1}, {"limit_price", 101}, {"max_avg_price", 101}}},
                         BotConfig{40,
                                   "noise",
                                   "noise_trader",
                                   TestLatencyModel{1, 0, 1},
                                   {}}}};
}

} // namespace

TEST(ScenarioConfigTests, ScenarioConfigBuildsDeterministicRunner) {
  const ScenarioBuildResult run = run_scenario_config(base_scenario());

  EXPECT_TRUE_MSG(run.ok, run.reason);
  EXPECT_TRUE(run.result.ledger_invariant_ok);
  EXPECT_TRUE(run.result.book_open_consistency_ok);
  EXPECT_TRUE(run.result.metrics.find(10) != run.result.metrics.end());
}

TEST(ScenarioConfigTests, ScenarioSameConfigProducesSameResult) {
  const ScenarioConfig config = base_scenario(77);
  const ScenarioBuildResult a = run_scenario_config(config);
  const ScenarioBuildResult b = run_scenario_config(config);

  EXPECT_TRUE_MSG(a.ok, a.reason);
  EXPECT_TRUE_MSG(b.ok, b.reason);
  EXPECT_TRUE(same_bot_run_result(a.result, b.result));
}

TEST(ScenarioConfigTests, ScenarioDifferentSeedProducesDifferentActionTrace) {
  ScenarioConfig a = base_scenario(42);
  ScenarioConfig b = base_scenario(43);

  const ScenarioBuildResult run_a = run_scenario_config(a);
  const ScenarioBuildResult run_b = run_scenario_config(b);

  EXPECT_TRUE_MSG(run_a.ok, run_a.reason);
  EXPECT_TRUE_MSG(run_b.ok, run_b.reason);
  EXPECT_TRUE_MSG(run_a.result.action_trace != run_b.result.action_trace,
                  "different scenario seeds must diverge in action trace");
}

TEST(ScenarioConfigTests, ScenarioRejectsDuplicateBotUsers) {
  ScenarioConfig config = base_scenario();
  config.bots[1].user = config.bots[0].user;
  std::string reason;

  EXPECT_FALSE(validate_scenario_config(config, &reason));
  EXPECT_TRUE(reason.find("duplicate") != std::string::npos);
}

TEST(ScenarioConfigTests, ScenarioRejectsReservedFeeAccountBot) {
  ScenarioConfig config = base_scenario();
  config.bots[0].user = dedicated_fee_account();
  std::string reason;

  EXPECT_FALSE(validate_scenario_config(config, &reason));
  EXPECT_TRUE(reason.find("reserved") != std::string::npos);
}

TEST(ScenarioConfigTests, ScenarioRejectsUnknownStrategyType) {
  ScenarioConfig config = base_scenario();
  config.bots[0].strategy_type = "oracle_peeker";
  std::string reason;

  EXPECT_FALSE(validate_scenario_config(config, &reason));
  EXPECT_TRUE(reason.find("unknown strategy") != std::string::npos);
}

TEST(ScenarioConfigTests, ScenarioRejectsNonPositiveTicks) {
  ScenarioConfig config = base_scenario();
  config.ticks = 0;
  std::string reason;

  EXPECT_FALSE(validate_scenario_config(config, &reason));
  EXPECT_TRUE(reason.find("ticks") != std::string::npos);
}

TEST(ScenarioConfigTests, ScenarioRecordsMetricsForEachBot) {
  const ScenarioBuildResult run = run_scenario_config(base_scenario());

  EXPECT_TRUE_MSG(run.ok, run.reason);
  EXPECT_TRUE(run.result.metrics.find(10) != run.result.metrics.end());
  EXPECT_TRUE(run.result.metrics.find(20) != run.result.metrics.end());
  EXPECT_TRUE(run.result.metrics.find(40) != run.result.metrics.end());
}
