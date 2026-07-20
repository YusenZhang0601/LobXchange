#include "lobx/simulation/mesa_agent_sim.hpp"

#include "test_helpers/test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace lobx::sim;

namespace {

MesaAgentSimConfig only_agents(MesaAgentCounts counts, uint64_t seed = 42) {
  MesaAgentSimConfig config{};
  config.seed = seed;
  config.steps = 0;
  config.agents = counts;
  return config;
}

lob::Quantity step_volume(const MesaStepEvents& events) {
  lob::Quantity volume = 0;
  for (const auto& trade : events.trades) volume += trade.qty;
  return volume;
}

std::vector<MesaOrderEvent> collect_orders(MesaAgentSimulation& sim, int steps) {
  std::vector<MesaOrderEvent> orders;
  for (int i = 0; i < steps; ++i) {
    MesaStepEvents events = sim.step();
    orders.insert(orders.end(), events.orders.begin(), events.orders.end());
  }
  return orders;
}

} // namespace

TEST(MesaAgents, MM001ToMM006MarketMakerPlacesPredictablePostOnlyTwoSidedQuotes) {
  MesaAgentSimulation sim(only_agents(MesaAgentCounts{1, 0, 0, 0, 0}, 7));

  const MesaStepEvents events = sim.step();

  EXPECT_EQ(events.orders.size(), 2UL);
  EXPECT_EQ(events.orders[0].agent_type, std::string("market_maker"));
  EXPECT_EQ(events.orders[1].agent_type, std::string("market_maker"));
  EXPECT_EQ(events.orders[0].side, lob::Side::Bid);
  EXPECT_EQ(events.orders[1].side, lob::Side::Ask);
  EXPECT_EQ(events.orders[0].flags, lob::POST_ONLY);
  EXPECT_EQ(events.orders[1].flags, lob::POST_ONLY);
  EXPECT_EQ(events.orders[0].price + events.orders[1].price, 200);
  EXPECT_TRUE(events.orders[0].price < 100);
  EXPECT_TRUE(events.orders[1].price > 100);
  EXPECT_TRUE(events.orders[0].qty >= 2 && events.orders[0].qty <= 4);
  EXPECT_EQ(events.orders[0].qty, events.orders[1].qty);
  EXPECT_TRUE(events.trades.empty());
  EXPECT_FALSE(sim.bids(1).empty());
  EXPECT_FALSE(sim.asks(1).empty());
}

TEST(MesaAgents, MM007PostOnlyMarketMakerDoesNotActivelyTradeOnEmptyBook) {
  MesaAgentSimulation sim(only_agents(MesaAgentCounts{4, 0, 0, 0, 0}, 11));

  for (int i = 0; i < 20; ++i) {
    const MesaStepEvents events = sim.step();
    for (const auto& order : events.orders) {
      EXPECT_EQ(order.flags, lob::POST_ONLY);
      EXPECT_EQ(order.trade_count, 0);
    }
    EXPECT_TRUE(events.trades.empty());
  }
}

TEST(MesaAgents, MM008MarketMakerFixedSeedIsReproducible) {
  MesaAgentSimulation a(only_agents(MesaAgentCounts{1, 0, 0, 0, 0}, 99));
  MesaAgentSimulation b(only_agents(MesaAgentCounts{1, 0, 0, 0, 0}, 99));

  const auto oa = collect_orders(a, 12);
  const auto ob = collect_orders(b, 12);

  EXPECT_EQ(oa.size(), ob.size());
  for (size_t i = 0; i < oa.size(); ++i) {
    EXPECT_EQ(oa[i].side, ob[i].side);
    EXPECT_EQ(oa[i].price, ob[i].price);
    EXPECT_EQ(oa[i].qty, ob[i].qty);
    EXPECT_EQ(oa[i].flags, ob[i].flags);
  }
}

TEST(MesaAgents, NT001NT003NoiseTraderDirectionModeAndSeedAreReproducible) {
  MesaAgentSimulation a(only_agents(MesaAgentCounts{0, 1, 0, 0, 0}, 123));
  MesaAgentSimulation b(only_agents(MesaAgentCounts{0, 1, 0, 0, 0}, 123));

  const auto oa = collect_orders(a, 64);
  const auto ob = collect_orders(b, 64);

  EXPECT_EQ(oa.size(), 64UL);
  EXPECT_EQ(oa.size(), ob.size());
  for (size_t i = 0; i < oa.size(); ++i) {
    EXPECT_EQ(oa[i].agent_type, std::string("noise_trader"));
    EXPECT_TRUE(oa[i].side == lob::Side::Bid || oa[i].side == lob::Side::Ask);
    EXPECT_TRUE(oa[i].flags == lob::IOC || oa[i].flags == lob::POST_ONLY);
    EXPECT_EQ(oa[i].qty, 1);
    EXPECT_EQ(oa[i].side, ob[i].side);
    EXPECT_EQ(oa[i].price, ob[i].price);
    EXPECT_EQ(oa[i].flags, ob[i].flags);
  }
}

TEST(MesaAgents, NT002NoiseTraderIOCProbabilityIsNearThirtyFivePercent) {
  MesaAgentSimulation sim(only_agents(MesaAgentCounts{0, 1, 0, 0, 0}, 777));
  int ioc = 0;
  int post_only = 0;
  constexpr int n = 2000;

  for (int i = 0; i < n; ++i) {
    const MesaStepEvents events = sim.step();
    EXPECT_EQ(events.orders.size(), 1UL);
    if (events.orders.front().flags == lob::IOC) ++ioc;
    if (events.orders.front().flags == lob::POST_ONLY) ++post_only;
  }

  const double ratio = static_cast<double>(ioc) / static_cast<double>(n);
  EXPECT_TRUE_MSG(ratio >= 0.32 && ratio <= 0.38, "IOC ratio=" + std::to_string(ratio));
  EXPECT_EQ(ioc + post_only, n);
}

TEST(MesaAgents, NT004NT005NoiseTraderIOCCanTradeAndPostOnlyCanRest) {
  MesaAgentSimConfig config = only_agents(MesaAgentCounts{4, 20, 0, 0, 0}, 202);
  MesaAgentSimulation sim(config);
  bool saw_ioc_trade = false;
  bool saw_post_only_rest = false;

  for (int i = 0; i < 80; ++i) {
    const MesaStepEvents events = sim.step();
    for (const auto& order : events.orders) {
      if (order.agent_type == "noise_trader" && order.flags == lob::IOC && order.trade_count > 0) {
        saw_ioc_trade = true;
      }
      if (order.agent_type == "noise_trader" && order.flags == lob::POST_ONLY && order.accepted && order.trade_count == 0) {
        saw_post_only_rest = true;
      }
    }
  }

  EXPECT_TRUE(saw_ioc_trade);
  EXPECT_TRUE(saw_post_only_rest);
}

TEST(MesaAgents, MO001ToMO007MomentumUsesLastTwoTradePrices) {
  MesaAgentSimConfig up = only_agents(MesaAgentCounts{0, 0, 1, 0, 0}, 1);
  up.initial_trade_prices = {100, 101};
  MesaAgentSimulation up_sim(up);
  const MesaStepEvents up_events = up_sim.step();
  EXPECT_EQ(up_events.orders.size(), 1UL);
  EXPECT_EQ(up_events.orders.front().agent_type, std::string("momentum"));
  EXPECT_EQ(up_events.orders.front().side, lob::Side::Bid);
  EXPECT_EQ(up_events.orders.front().flags, lob::IOC);
  EXPECT_EQ(up_events.orders.front().price, 113);
  EXPECT_EQ(up_events.orders.front().qty, 2);

  MesaAgentSimConfig down = only_agents(MesaAgentCounts{0, 0, 1, 0, 0}, 1);
  down.initial_trade_prices = {101, 100};
  MesaAgentSimulation down_sim(down);
  const MesaStepEvents down_events = down_sim.step();
  EXPECT_EQ(down_events.orders.size(), 1UL);
  EXPECT_EQ(down_events.orders.front().side, lob::Side::Ask);
  EXPECT_EQ(down_events.orders.front().flags, lob::IOC);
  EXPECT_EQ(down_events.orders.front().price, 88);

  MesaAgentSimConfig flat = only_agents(MesaAgentCounts{0, 0, 1, 0, 0}, 1);
  flat.initial_trade_prices = {100, 100};
  MesaAgentSimulation flat_sim(flat);
  EXPECT_TRUE(flat_sim.step().orders.empty());

  MesaAgentSimulation cold_sim(only_agents(MesaAgentCounts{0, 0, 1, 0, 0}, 1));
  EXPECT_TRUE(cold_sim.step().orders.empty());
}

TEST(MesaAgents, MR001ToMR007MeanReversionTradesAwayFromThreeTickDeviation) {
  MesaAgentSimConfig high = only_agents(MesaAgentCounts{0, 0, 0, 1, 0}, 1);
  high.reference_price = 100;
  high.initial_trade_prices = {103};
  MesaAgentSimulation high_sim(high);
  const MesaStepEvents high_events = high_sim.step();
  EXPECT_EQ(high_events.orders.size(), 1UL);
  EXPECT_EQ(high_events.orders.front().agent_type, std::string("mean_reversion"));
  EXPECT_EQ(high_events.orders.front().side, lob::Side::Ask);
  EXPECT_EQ(high_events.orders.front().flags, lob::IOC);
  EXPECT_EQ(high_events.orders.front().price, 99);

  MesaAgentSimConfig low = only_agents(MesaAgentCounts{0, 0, 0, 1, 0}, 1);
  low.reference_price = 100;
  low.initial_trade_prices = {97};
  MesaAgentSimulation low_sim(low);
  const MesaStepEvents low_events = low_sim.step();
  EXPECT_EQ(low_events.orders.size(), 1UL);
  EXPECT_EQ(low_events.orders.front().side, lob::Side::Bid);
  EXPECT_EQ(low_events.orders.front().flags, lob::IOC);
  EXPECT_EQ(low_events.orders.front().price, 101);

  MesaAgentSimConfig near = only_agents(MesaAgentCounts{0, 0, 0, 1, 0}, 1);
  near.reference_price = 100;
  near.initial_trade_prices = {102};
  MesaAgentSimulation near_sim(near);
  EXPECT_TRUE(near_sim.step().orders.empty());

  MesaAgentSimulation missing_price_sim(only_agents(MesaAgentCounts{0, 0, 0, 1, 0}, 1));
  EXPECT_TRUE(missing_price_sim.step().orders.empty());
}

TEST(MesaAgents, WH001ToWH004WhaleOnlyActsOnMultiplesOfTwelveWithFixedQtyIOC) {
  MesaAgentSimulation sim(only_agents(MesaAgentCounts{0, 0, 0, 0, 1}, 42));
  for (int step = 1; step < 12; ++step) {
    EXPECT_TRUE(sim.step().orders.empty());
  }

  const MesaStepEvents events = sim.step();

  EXPECT_EQ(events.step, 12);
  EXPECT_EQ(events.orders.size(), 1UL);
  EXPECT_EQ(events.orders.front().agent_type, std::string("whale_sweeper"));
  EXPECT_EQ(events.orders.front().qty, 20);
  EXPECT_EQ(events.orders.front().flags, lob::IOC);
  EXPECT_FALSE(events.orders.front().accepted == false && events.orders.front().code == lobx::RejectCode::PostOnlyWouldCross);
}

TEST(MesaAgents, WH005ToWH007WhaleSweepsDepthAndPartialRemainderDoesNotRest) {
  MesaAgentSimulation deep(only_agents(MesaAgentCounts{6, 0, 0, 0, 1}, 44));
  MesaStepEvents sweep{};
  for (int step = 1; step <= 12; ++step) sweep = deep.step();
  EXPECT_TRUE(step_volume(sweep) > 0);
  for (const auto& order : sweep.orders) {
    if (order.agent_type == "whale_sweeper") {
      EXPECT_TRUE(order.trade_count > 0);
      EXPECT_TRUE(order.filled > 0);
      EXPECT_FALSE(order.remaining > 0 && order.flags != lob::IOC);
    }
  }

  MesaAgentSimulation shallow(only_agents(MesaAgentCounts{0, 0, 0, 0, 1}, 44));
  for (int step = 1; step <= 11; ++step) shallow.step();
  const MesaStepEvents no_depth = shallow.step();
  EXPECT_EQ(no_depth.trades.size(), 0UL);
  EXPECT_EQ(no_depth.orders.size(), 1UL);
  EXPECT_EQ(no_depth.orders.front().filled, 0);
  EXPECT_TRUE(shallow.bids(100).empty());
  EXPECT_TRUE(shallow.asks(100).empty());
}

TEST(MesaAgents, WH008WhaleFixedSeedIsReproducible) {
  MesaAgentSimulation a(only_agents(MesaAgentCounts{0, 0, 0, 0, 1}, 1234));
  MesaAgentSimulation b(only_agents(MesaAgentCounts{0, 0, 0, 0, 1}, 1234));
  MesaStepEvents ea{};
  MesaStepEvents eb{};
  for (int step = 1; step <= 24; ++step) {
    ea = a.step();
    eb = b.step();
    EXPECT_EQ(ea.orders.size(), eb.orders.size());
    for (size_t i = 0; i < ea.orders.size(); ++i) {
      EXPECT_EQ(ea.orders[i].side, eb.orders[i].side);
      EXPECT_EQ(ea.orders[i].price, eb.orders[i].price);
      EXPECT_EQ(ea.orders[i].qty, eb.orders[i].qty);
      EXPECT_EQ(ea.orders[i].flags, eb.orders[i].flags);
    }
  }
}
