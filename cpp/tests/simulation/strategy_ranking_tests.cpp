#include "test_helpers/strategy_ranking.hpp"
#include "test_helpers/test_framework.hpp"

#include <map>
#include <string>
#include <vector>

using namespace lobx_test;

namespace {

SweepRun fake_run(std::string bot_name,
                  lobx::UserId user,
                  std::map<std::string, double> params,
                  long double gross_pnl,
                  lobx::Amount fees_paid,
                  long double net_pnl) {
  StrategyMetrics metrics{};
  metrics.user = user;
  metrics.gross_pnl = gross_pnl;
  metrics.fees_paid = fees_paid;
  metrics.net_pnl = net_pnl;

  BotRunResult result{};
  result.metrics[user] = metrics;
  return SweepRun{ScenarioConfig{1,
                                 1,
                                 "BTC-USDT",
                                 {BotConfig{user,
                                            std::move(bot_name),
                                            "market_maker",
                                            TestLatencyModel{0, 0, 0},
                                            std::move(params)}}},
                  result};
}

} // namespace

TEST(StrategyRankingTests, RankingByNetPnlOrdersResultsDescending) {
  const std::vector<SweepRun> runs{
      fake_run("mm", 10, {{"case", 1}}, 10, 0, 10),
      fake_run("mm", 10, {{"case", 2}}, 5, 0, 5),
      fake_run("mm", 10, {{"case", 3}}, -1, 0, -1)};

  const auto ranked = rank_sweep_results(runs, "mm", RankingMetric::NetPnl);

  EXPECT_EQ(ranked.size(), 3UL);
  EXPECT_EQ(static_cast<int>(ranked[0].score), 10);
  EXPECT_EQ(static_cast<int>(ranked[1].score), 5);
  EXPECT_EQ(static_cast<int>(ranked[2].score), -1);
  EXPECT_EQ(ranked[0].rank, 1);
  EXPECT_EQ(ranked[2].rank, 3);
}

TEST(StrategyRankingTests, RankingTieBreaksDeterministically) {
  const std::vector<SweepRun> runs{
      fake_run("mm", 10, {{"bid_px", 99}}, 10, 0, 5),
      fake_run("mm", 10, {{"bid_px", 98}}, 10, 0, 5)};

  const auto ranked = rank_sweep_results(runs, "mm", RankingMetric::NetPnl);

  EXPECT_EQ(ranked.size(), 2UL);
  EXPECT_EQ(ranked[0].params.at("bid_px"), 98.0);
  EXPECT_EQ(ranked[1].params.at("bid_px"), 99.0);
}

TEST(StrategyRankingTests, RankingIncludesFeesInNetPnl) {
  const std::vector<SweepRun> runs{
      fake_run("mm", 10, {{"case", 1}}, 10, 3, 7),
      fake_run("mm", 10, {{"case", 2}}, 8, 0, 8)};

  const auto ranked = rank_sweep_results(runs, "mm", RankingMetric::NetPnl);

  EXPECT_EQ(ranked.size(), 2UL);
  EXPECT_EQ(ranked[0].params.at("case"), 2.0);
  EXPECT_EQ(static_cast<int>(ranked[0].score), 8);
}

TEST(StrategyRankingTests, RankingStableAcrossSameSeedReplay) {
  const std::vector<SweepRun> runs{
      fake_run("mm", 10, {{"case", 1}}, 10, 0, 10),
      fake_run("mm", 10, {{"case", 2}}, 6, 0, 6)};

  const auto a = rank_sweep_results(runs, "mm", RankingMetric::NetPnl);
  const auto b = rank_sweep_results(runs, "mm", RankingMetric::NetPnl);

  EXPECT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].rank, b[i].rank);
    EXPECT_EQ(a[i].score, b[i].score);
    EXPECT_EQ(serialize_params(a[i].params), serialize_params(b[i].params));
  }
}

TEST(StrategyRankingTests, RankingCanSelectTargetBotByName) {
  StrategyMetrics mm_metrics{};
  mm_metrics.user = 10;
  mm_metrics.net_pnl = 1;
  StrategyMetrics taker_metrics{};
  taker_metrics.user = 20;
  taker_metrics.net_pnl = 9;

  BotRunResult result{};
  result.metrics[10] = mm_metrics;
  result.metrics[20] = taker_metrics;
  SweepRun run{ScenarioConfig{1,
                              1,
                              "BTC-USDT",
                              {BotConfig{10, "mm", "market_maker", TestLatencyModel{0, 0, 0}, {}},
                               BotConfig{20, "taker", "taker_sweep", TestLatencyModel{0, 0, 0}, {}}}},
               result};

  const auto ranked = rank_sweep_results({run}, "taker", RankingMetric::NetPnl);

  EXPECT_EQ(ranked.size(), 1UL);
  EXPECT_EQ(ranked[0].bot_name, std::string("taker"));
  EXPECT_EQ(ranked[0].user, 20ULL);
  EXPECT_EQ(static_cast<int>(ranked[0].score), 9);
}

TEST(StrategyRankingTests, RankingRejectsMissingBotMetrics) {
  BotRunResult result{};
  SweepRun run{ScenarioConfig{1,
                              1,
                              "BTC-USDT",
                              {BotConfig{10, "mm", "market_maker", TestLatencyModel{0, 0, 0}, {}}}},
               result};

  const auto ranked = rank_sweep_results({run}, "mm", RankingMetric::NetPnl);

  EXPECT_TRUE(ranked.empty());
}
