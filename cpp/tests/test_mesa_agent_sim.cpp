#include "lobx/simulation/mesa_agent_sim.hpp"

#include "test_helpers/test_framework.hpp"

#include <map>
#include <vector>

using namespace lobx::sim;

namespace {

MesaAgentSimConfig standard_config(uint64_t seed = 42, int steps = 20) {
  MesaAgentSimConfig config{};
  config.seed = seed;
  config.steps = steps;
  config.agents = MesaAgentCounts{4, 6, 2, 2, 1};
  return config;
}

bool legal_book(const MesaAgentSimulation& sim) {
  const auto bids = sim.bids(1);
  const auto asks = sim.asks(1);
  return bids.empty() || asks.empty() || bids.front().first <= asks.front().first;
}

struct OrderTrace {
  int step{0};
  lobx::UserId user{0};
  std::string agent_type;
  lob::Side side{lob::Side::Bid};
  lob::Tick price{0};
  lob::Quantity qty{0};
  uint32_t flags{lob::NONE};
};

std::vector<OrderTrace> trace_orders(MesaAgentSimulation& sim, int steps) {
  std::vector<OrderTrace> out;
  for (int i = 0; i < steps; ++i) {
    const MesaStepEvents events = sim.step();
    for (const auto& order : events.orders) {
      out.push_back(OrderTrace{events.step, order.user, order.agent_type, order.side, order.price, order.qty, order.flags});
    }
  }
  return out;
}

void expect_summary_eq(const MesaAgentSimSummary& actual,
                       int steps,
                       int agents,
                       int accepted,
                       int rejected,
                       int trades,
                       lob::Tick bid,
                       lob::Tick ask) {
  EXPECT_EQ(actual.steps, steps);
  EXPECT_EQ(actual.agent_count, agents);
  EXPECT_EQ(actual.accepted_orders, accepted);
  EXPECT_EQ(actual.rejected_orders, rejected);
  EXPECT_EQ(actual.trade_count, trades);
  EXPECT_EQ(actual.final_best_bid, bid);
  EXPECT_EQ(actual.final_best_ask, ask);
}

} // namespace

TEST(MesaAgentSimulationFlow, SIM001InitializationCreatesExpectedAgentsAndEmptyBook) {
  MesaAgentSimulation sim(standard_config(42, 0));
  const MesaAgentSimSummary summary = sim.summary();

  EXPECT_EQ(summary.agent_count, 15);
  EXPECT_EQ(summary.agent_types.at("market_maker"), 4);
  EXPECT_EQ(summary.agent_types.at("noise_trader"), 6);
  EXPECT_EQ(summary.agent_types.at("momentum"), 2);
  EXPECT_EQ(summary.agent_types.at("mean_reversion"), 2);
  EXPECT_EQ(summary.agent_types.at("whale_sweeper"), 1);
  EXPECT_TRUE(sim.bids(1).empty());
  EXPECT_TRUE(sim.asks(1).empty());
  EXPECT_TRUE(sim.accounting_invariant_ok());
}

TEST(MesaAgentSimulationFlow, SIM002SIM005EachStepUsesAndPublishesCurrentBookStats) {
  MesaAgentSimConfig config = standard_config(7, 0);
  config.agents = MesaAgentCounts{2, 0, 0, 0, 0};
  MesaAgentSimulation sim(config);

  for (int step = 1; step <= 5; ++step) {
    const MesaStepEvents events = sim.step();
    EXPECT_EQ(events.step, step);
    EXPECT_EQ(events.stats.step, step);
    EXPECT_EQ(events.orders.size(), 4UL);
    EXPECT_TRUE(events.stats.best_bid > 0);
    EXPECT_TRUE(events.stats.best_ask > 0);
    EXPECT_TRUE(events.stats.best_bid <= events.stats.best_ask);
    EXPECT_EQ(events.stats.spread, events.stats.best_ask - events.stats.best_bid);
    EXPECT_TRUE(legal_book(sim));
  }
}

TEST(MesaAgentSimulationFlow, SIM003ShuffleOrderIsReproducibleForFixedSeed) {
  MesaAgentSimulation a(standard_config(99, 0));
  MesaAgentSimulation b(standard_config(99, 0));

  const auto ta = trace_orders(a, 24);
  const auto tb = trace_orders(b, 24);

  EXPECT_EQ(ta.size(), tb.size());
  for (size_t i = 0; i < ta.size(); ++i) {
    EXPECT_EQ(ta[i].step, tb[i].step);
    EXPECT_EQ(ta[i].user, tb[i].user);
    EXPECT_EQ(ta[i].agent_type, tb[i].agent_type);
    EXPECT_EQ(ta[i].side, tb[i].side);
    EXPECT_EQ(ta[i].price, tb[i].price);
    EXPECT_EQ(ta[i].qty, tb[i].qty);
    EXPECT_EQ(ta[i].flags, tb[i].flags);
  }
}

TEST(MesaAgentSimulationFlow, SIM004AlwaysActingAgentsSubmitExpectedOrdersEveryStep) {
  MesaAgentSimConfig config = standard_config(12, 0);
  config.agents = MesaAgentCounts{3, 5, 0, 0, 0};
  MesaAgentSimulation sim(config);

  for (int i = 0; i < 30; ++i) {
    const MesaStepEvents events = sim.step();
    EXPECT_EQ(events.orders.size(), 11UL);
    int maker_orders = 0;
    int noise_orders = 0;
    for (const auto& order : events.orders) {
      if (order.agent_type == "market_maker") ++maker_orders;
      if (order.agent_type == "noise_trader") ++noise_orders;
    }
    EXPECT_EQ(maker_orders, 6);
    EXPECT_EQ(noise_orders, 5);
  }
}

TEST(MesaAgentSimulationFlow, SIM006SIM007StatsSeriesAndTradeCountAreConsistent) {
  MesaAgentSimulation sim(standard_config(8, 0));
  std::vector<MesaStepStats> stats;
  int cumulative_trades = 0;

  for (int i = 0; i < 40; ++i) {
    const MesaStepEvents events = sim.step();
    stats.push_back(events.stats);
    cumulative_trades += static_cast<int>(events.trades.size());
    EXPECT_EQ(events.stats.trade_count, cumulative_trades);
    EXPECT_TRUE(events.stats.spread >= 0);
  }

  EXPECT_EQ(stats.size(), 40UL);
  EXPECT_EQ(sim.summary().trade_count, cumulative_trades);
  EXPECT_TRUE(sim.accounting_invariant_ok());
}

TEST(MesaAgentSimulationFlow, SIM008SIM009ZeroAndOneAgentDoNotCrash) {
  MesaAgentSimConfig zero = standard_config(1, 5);
  zero.agents = MesaAgentCounts{0, 0, 0, 0, 0};
  MesaAgentSimulation zero_sim(zero);
  const MesaAgentSimSummary zero_summary = zero_sim.run();
  EXPECT_EQ(zero_summary.agent_count, 0);
  EXPECT_EQ(zero_summary.accepted_orders, 0);
  EXPECT_EQ(zero_summary.trade_count, 0);
  EXPECT_TRUE(zero_sim.accounting_invariant_ok());

  MesaAgentSimConfig one = standard_config(1, 5);
  one.agents = MesaAgentCounts{1, 0, 0, 0, 0};
  MesaAgentSimulation one_sim(one);
  const MesaAgentSimSummary one_summary = one_sim.run();
  EXPECT_EQ(one_summary.agent_count, 1);
  EXPECT_TRUE(one_summary.accepted_orders > 0);
  EXPECT_TRUE(one_sim.accounting_invariant_ok());
}

TEST(MesaAgentSimulationFlow, SIM010FifteenAgentsTwentyStepsSmokeAndGoldenBaseline) {
  MesaAgentSimulation sim(standard_config(42, 20));
  const MesaAgentSimSummary summary = sim.run();

  EXPECT_TRUE(summary.accepted_orders > 0);
  EXPECT_TRUE(summary.rejected_orders >= 0);
  EXPECT_TRUE(summary.trade_count > 0);
  EXPECT_TRUE(legal_book(sim));
  EXPECT_TRUE(sim.accounting_invariant_ok());
  expect_summary_eq(summary, 20, 15, 270, 34, 88, 100, 102);
}

TEST(MesaAgentSimulationFlow, SIM011HundredAgentsHundredStepsSmoke) {
  MesaAgentSimConfig config = standard_config(42, 100);
  config.agents = MesaAgentCounts{25, 50, 10, 10, 5};
  MesaAgentSimulation sim(config);
  const MesaAgentSimSummary summary = sim.run();

  EXPECT_EQ(summary.steps, 100);
  EXPECT_EQ(summary.agent_count, 100);
  EXPECT_TRUE(summary.accepted_orders > 0);
  EXPECT_TRUE(summary.rejected_orders >= 0);
  EXPECT_TRUE(summary.trade_count > 0);
  EXPECT_TRUE(legal_book(sim));
  EXPECT_TRUE(sim.accounting_invariant_ok());
}

TEST(MesaAgentSimulationFlow, SIM012SameSeedRerunProducesSameOutput) {
  MesaAgentSimulation a(standard_config(42, 100));
  MesaAgentSimulation b(standard_config(42, 100));

  const MesaAgentSimSummary sa = a.run();
  const MesaAgentSimSummary sb = b.run();

  EXPECT_EQ(sa.accepted_orders, sb.accepted_orders);
  EXPECT_EQ(sa.rejected_orders, sb.rejected_orders);
  EXPECT_EQ(sa.trade_count, sb.trade_count);
  EXPECT_EQ(sa.final_best_bid, sb.final_best_bid);
  EXPECT_EQ(sa.final_best_ask, sb.final_best_ask);
  EXPECT_TRUE(a.accounting_invariant_ok());
  EXPECT_TRUE(b.accounting_invariant_ok());
}

TEST(MesaAgentSimulationFlow, GoldenBaselinesForSeed42At100And1000Steps) {
  MesaAgentSimulation sim100(standard_config(42, 100));
  expect_summary_eq(sim100.run(), 100, 15, 1319, 177, 411, 98, 100);

  MesaAgentSimulation sim1000(standard_config(42, 1000));
  expect_summary_eq(sim1000.run(), 1000, 15, 13009, 1791, 3933, 99, 101);
}
