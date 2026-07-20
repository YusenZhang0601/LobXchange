#include "lobx/simulation/mesa_agent_sim.hpp"

#include "test_helpers/test_framework.hpp"

#include <map>
#include <string>

using namespace lobx::sim;

namespace {

MesaAgentSimConfig active_config(uint64_t seed = 12, int steps = 40) {
  MesaAgentSimConfig config{};
  config.seed = seed;
  config.steps = steps;
  config.agents.market_makers = 2;
  config.agents.noise_traders = 4;
  config.agents.momentum = 1;
  config.agents.mean_reversion = 1;
  config.agents.whale_sweepers = 1;
  return config;
}

} // namespace

TEST(MesaAgentSimTests, CxxMesaAgentPopulationMatchesPythonMesaKinds) {
  MesaAgentSimulation sim(active_config());
  const MesaAgentSimSummary summary = sim.summary();

  EXPECT_EQ(summary.agent_count, 9);
  EXPECT_EQ(summary.agent_types.at("market_maker"), 2);
  EXPECT_EQ(summary.agent_types.at("noise_trader"), 4);
  EXPECT_EQ(summary.agent_types.at("momentum"), 1);
  EXPECT_EQ(summary.agent_types.at("mean_reversion"), 1);
  EXPECT_EQ(summary.agent_types.at("whale_sweeper"), 1);
}

TEST(MesaAgentSimTests, CxxMesaAgentsProduceOrdersTradesAndCandles) {
  MesaAgentSimulation sim(active_config(7, 60));
  MesaAgentSimSummary summary{};
  bool saw_candle = false;
  for (int i = 0; i < 60; ++i) {
    const MesaStepEvents events = sim.step();
    saw_candle = saw_candle || !events.candles.empty();
  }
  summary = sim.summary();

  EXPECT_TRUE(summary.accepted_orders > 0);
  EXPECT_TRUE(summary.trade_count > 0);
  EXPECT_TRUE(summary.final_best_bid > 0);
  EXPECT_TRUE(summary.final_best_ask > 0);
  EXPECT_TRUE(saw_candle);
}

TEST(MesaAgentSimTests, CxxMesaAgentRunsAreDeterministicForSameSeed) {
  MesaAgentSimulation a(active_config(99, 50));
  MesaAgentSimulation b(active_config(99, 50));

  const MesaAgentSimSummary sa = a.run();
  const MesaAgentSimSummary sb = b.run();

  EXPECT_EQ(sa.accepted_orders, sb.accepted_orders);
  EXPECT_EQ(sa.rejected_orders, sb.rejected_orders);
  EXPECT_EQ(sa.trade_count, sb.trade_count);
  EXPECT_EQ(sa.final_best_bid, sb.final_best_bid);
  EXPECT_EQ(sa.final_best_ask, sb.final_best_ask);
  EXPECT_EQ(a.trades().size(), b.trades().size());
  for (size_t i = 0; i < a.trades().size(); ++i) {
    EXPECT_EQ(a.trades()[i].price, b.trades()[i].price);
    EXPECT_EQ(a.trades()[i].qty, b.trades()[i].qty);
    EXPECT_EQ(a.trades()[i].buyer, b.trades()[i].buyer);
    EXPECT_EQ(a.trades()[i].seller, b.trades()[i].seller);
  }
}

TEST(MesaAgentSimTests, CxxMesaAgentSummaryJsonMatchesMesaSmokeShape) {
  MesaAgentSimulation sim(active_config(3, 10));
  const MesaAgentSimSummary summary = sim.run();
  const std::string json = mesa_agent_summary_json(summary);

  EXPECT_TRUE(json.find("\"accepted_orders\"") != std::string::npos);
  EXPECT_TRUE(json.find("\"agent_types\"") != std::string::npos);
  EXPECT_TRUE(json.find("\"market_maker\"") != std::string::npos);
  EXPECT_TRUE(json.find("\"trade_count\"") != std::string::npos);
}

TEST(MesaAgentSimTests, CxxMesaAgentSimulationAllowsZeroAgents) {
  MesaAgentSimConfig config{};
  config.steps = 5;
  config.agents = MesaAgentCounts{0, 0, 0, 0, 0};

  MesaAgentSimulation sim(config);
  const MesaAgentSimSummary summary = sim.run();

  EXPECT_EQ(summary.agent_count, 0);
  EXPECT_EQ(summary.accepted_orders, 0);
  EXPECT_EQ(summary.trade_count, 0);
  EXPECT_EQ(summary.final_mid_price, 100.0);
}
