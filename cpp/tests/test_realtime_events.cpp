#include "lobx/simulation/mesa_agent_sim.hpp"

#include "test_helpers/test_framework.hpp"

#include <string>
#include <vector>

using namespace lobx::sim;

namespace {

MesaAgentSimConfig realtime_config(uint64_t seed = 42, int steps = 10) {
  MesaAgentSimConfig config{};
  config.seed = seed;
  config.steps = steps;
  config.agents = MesaAgentCounts{4, 6, 2, 2, 1};
  return config;
}

bool contains(const std::string& s, const std::string& needle) {
  return s.find(needle) != std::string::npos;
}

std::vector<std::string> event_stream(uint64_t seed, int steps) {
  MesaAgentSimulation sim(realtime_config(seed, 0));
  std::vector<std::string> out;
  out.push_back(mesa_agent_mix_json(sim.summary()));
  for (int i = 0; i < steps; ++i) {
    const MesaStepEvents events = sim.step();
    for (const auto& trade : events.trades) out.push_back(mesa_trade_json(trade, events.step));
    for (const auto& candle : events.candles) out.push_back(mesa_step_candle_json(candle));
    out.push_back(mesa_step_stats_json(events.stats));
  }
  return out;
}

} // namespace

TEST(RealtimeEvents, RT005SSEDataFrameFormatIsLegal) {
  MesaAgentSimulation sim(realtime_config(42, 0));
  const MesaStepEvents events = sim.step();
  const std::string frame = "data: " + mesa_step_stats_json(events.stats) + "\n\n";

  EXPECT_TRUE(frame.rfind("data: ", 0) == 0);
  EXPECT_TRUE(frame.size() >= 2);
  EXPECT_EQ(frame.substr(frame.size() - 2), std::string("\n\n"));
  EXPECT_TRUE(contains(frame, "\"type\":\"stats\""));
}

TEST(RealtimeEvents, RT006StatsEventContainsBookTradesSpreadAndStepFields) {
  MesaAgentSimulation sim(realtime_config(7, 0));
  const MesaStepEvents events = sim.step();
  const std::string json = mesa_step_stats_json(events.stats);

  EXPECT_TRUE(contains(json, "\"step\":1"));
  EXPECT_TRUE(contains(json, "\"best_bid\""));
  EXPECT_TRUE(contains(json, "\"best_ask\""));
  EXPECT_TRUE(contains(json, "\"spread\""));
  EXPECT_TRUE(contains(json, "\"trade_count\""));
  EXPECT_TRUE(contains(json, "\"agent_count\""));
}

TEST(RealtimeEvents, RT006TradeAndCandleEventsContainRequiredFields) {
  MesaAgentSimulation sim(realtime_config(42, 0));
  bool saw_trade = false;
  bool saw_candle = false;

  for (int i = 0; i < 40 && (!saw_trade || !saw_candle); ++i) {
    const MesaStepEvents events = sim.step();
    for (const auto& trade : events.trades) {
      const std::string json = mesa_trade_json(trade, events.step);
      EXPECT_TRUE(contains(json, "\"type\":\"trade\""));
      EXPECT_TRUE(contains(json, "\"price\""));
      EXPECT_TRUE(contains(json, "\"qty\""));
      EXPECT_TRUE(contains(json, "\"buyer\""));
      EXPECT_TRUE(contains(json, "\"seller\""));
      saw_trade = true;
    }
    for (const auto& candle : events.candles) {
      const std::string json = mesa_step_candle_json(candle);
      EXPECT_TRUE(contains(json, "\"type\":\"candle\""));
      EXPECT_TRUE(contains(json, "\"open\""));
      EXPECT_TRUE(contains(json, "\"high\""));
      EXPECT_TRUE(contains(json, "\"low\""));
      EXPECT_TRUE(contains(json, "\"close\""));
      EXPECT_TRUE(contains(json, "\"volume\""));
      saw_candle = true;
    }
  }

  EXPECT_TRUE(saw_trade);
  EXPECT_TRUE(saw_candle);
}

TEST(RealtimeEvents, RT007LongCppEventStreamDoesNotHang) {
  MesaAgentSimulation sim(realtime_config(17, 0));
  size_t bytes = 0;

  for (int i = 0; i < 1000; ++i) {
    const MesaStepEvents events = sim.step();
    bytes += mesa_step_stats_json(events.stats).size();
    for (const auto& trade : events.trades) bytes += mesa_trade_json(trade, events.step).size();
    for (const auto& candle : events.candles) bytes += mesa_step_candle_json(candle).size();
  }

  EXPECT_TRUE(bytes > 0);
  EXPECT_TRUE(sim.accounting_invariant_ok());
}

TEST(RealtimeEvents, RT010FixedSeedEventSequenceIsReproducible) {
  const auto a = event_stream(123, 50);
  const auto b = event_stream(123, 50);

  EXPECT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) EXPECT_EQ(a[i], b[i]);
}

TEST(RealtimeEvents, SummaryJsonCarriesCppAgentMixForEndpointBatch) {
  MesaAgentSimulation sim(realtime_config(42, 0));
  const std::string mix = mesa_agent_mix_json(sim.summary());
  const std::string summary = mesa_agent_summary_json(sim.run(), false);

  EXPECT_TRUE(contains(mix, "\"type\":\"agent_mix\""));
  EXPECT_TRUE(contains(mix, "\"market_maker\""));
  EXPECT_TRUE(contains(mix, "\"noise_trader\""));
  EXPECT_TRUE(contains(summary, "\"accepted_orders\""));
  EXPECT_TRUE(contains(summary, "\"trade_count\""));
}
