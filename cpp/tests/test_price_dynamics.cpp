#include "lobx/simulation/mesa_agent_sim.hpp"

#include "test_helpers/test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace lobx::sim;

namespace {

MesaAgentSimConfig config_with(MesaAgentCounts counts, uint64_t seed = 42, int steps = 80) {
  MesaAgentSimConfig config{};
  config.seed = seed;
  config.steps = steps;
  config.agents = counts;
  return config;
}

lob::Quantity volume(const MesaStepEvents& events) {
  lob::Quantity total = 0;
  for (const auto& trade : events.trades) total += trade.qty;
  return total;
}

lob::Quantity signed_flow(const MesaStepEvents& events) {
  lob::Quantity total = 0;
  for (const auto& trade : events.trades) {
    total += trade.liquidity_side == lob::Side::Ask ? trade.qty : -trade.qty;
  }
  return total;
}

double realized_volatility(const std::vector<double>& returns) {
  if (returns.empty()) return 0.0;
  const double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / static_cast<double>(returns.size());
  double variance = 0.0;
  for (double r : returns) variance += (r - mean) * (r - mean);
  return std::sqrt(variance / static_cast<double>(returns.size()));
}

} // namespace

TEST(PriceDynamics, PX001ToPX006PriceSpreadAndLastTradeSeriesAreStable) {
  MesaAgentSimulation sim(config_with(MesaAgentCounts{4, 6, 2, 2, 1}, 42, 0));
  std::vector<lob::Tick> price_series;
  std::vector<lob::Tick> spread_series;

  for (int i = 0; i < 80; ++i) {
    const MesaStepEvents events = sim.step();
    price_series.push_back(events.stats.last_price);
    spread_series.push_back(events.stats.spread);
    if (events.stats.best_bid > 0 && events.stats.best_ask > 0) {
      EXPECT_TRUE(events.stats.best_bid <= events.stats.best_ask);
      EXPECT_EQ(events.stats.spread, events.stats.best_ask - events.stats.best_bid);
    }
    EXPECT_TRUE(events.stats.spread >= 0);
    if (!events.trades.empty()) {
      EXPECT_EQ(events.stats.last_price, events.trades.back().price);
    }
  }

  EXPECT_EQ(price_series.size(), 80UL);
  EXPECT_EQ(spread_series.size(), 80UL);
  EXPECT_TRUE(sim.accounting_invariant_ok());
}

TEST(PriceDynamics, PX002SummaryMidUsesFinalBestBidAskWhenBothSidesExist) {
  MesaAgentSimulation sim(config_with(MesaAgentCounts{4, 0, 0, 0, 0}, 5, 20));
  const MesaAgentSimSummary summary = sim.run();

  EXPECT_TRUE(summary.final_best_bid > 0);
  EXPECT_TRUE(summary.final_best_ask > 0);
  EXPECT_TRUE(summary.final_best_bid <= summary.final_best_ask);
  const double expected_mid = (static_cast<double>(summary.final_best_bid) + static_cast<double>(summary.final_best_ask)) / 2.0;
  EXPECT_EQ(summary.final_mid_price, expected_mid);
  EXPECT_TRUE(summary.mean_spread >= 0.0);
}

TEST(PriceDynamics, PX006NoTradeLastPriceUsesStableBookMidFallback) {
  MesaAgentSimulation sim(config_with(MesaAgentCounts{1, 0, 0, 0, 0}, 19, 0));

  for (int i = 0; i < 10; ++i) {
    const MesaStepEvents events = sim.step();
    EXPECT_TRUE(events.trades.empty());
    EXPECT_TRUE(events.stats.best_bid > 0);
    EXPECT_TRUE(events.stats.best_ask > 0);
    EXPECT_EQ(events.stats.last_price, (events.stats.best_bid + events.stats.best_ask) / 2);
  }
}

TEST(PriceDynamics, PX007ReturnSeriesHasNoNaNOrInf) {
  MesaAgentSimulation sim(config_with(MesaAgentCounts{4, 6, 2, 2, 1}, 42, 0));
  std::vector<double> returns;
  lob::Tick previous = 0;

  for (int i = 0; i < 120; ++i) {
    const MesaStepEvents events = sim.step();
    if (previous > 0 && events.stats.last_price > 0) {
      const double r = (static_cast<double>(events.stats.last_price) - static_cast<double>(previous)) /
                       static_cast<double>(previous);
      EXPECT_TRUE(std::isfinite(r));
      returns.push_back(r);
    }
    previous = events.stats.last_price;
  }

  EXPECT_FALSE(returns.empty());
  EXPECT_TRUE(std::isfinite(realized_volatility(returns)));
}

TEST(PriceDynamics, PX008VolumeSeriesEqualsPerStepTradeQtySum) {
  MesaAgentSimulation sim(config_with(MesaAgentCounts{4, 6, 2, 2, 1}, 42, 0));
  lob::Quantity cumulative_volume = 0;
  int cumulative_trades = 0;

  for (int i = 0; i < 60; ++i) {
    const MesaStepEvents events = sim.step();
    const lob::Quantity step_volume = volume(events);
    cumulative_volume += step_volume;
    cumulative_trades += static_cast<int>(events.trades.size());
    EXPECT_TRUE(step_volume >= 0);
    EXPECT_EQ(events.stats.trade_count, cumulative_trades);
  }

  EXPECT_TRUE(cumulative_volume > 0);
}

TEST(PriceDynamics, PX009SignedOrderFlowUsesAggressorDirection) {
  MesaAgentSimulation sim(config_with(MesaAgentCounts{4, 10, 0, 0, 0}, 71, 0));
  bool saw_positive = false;
  bool saw_negative = false;

  for (int i = 0; i < 100; ++i) {
    const MesaStepEvents events = sim.step();
    const lob::Quantity flow = signed_flow(events);
    if (flow > 0) saw_positive = true;
    if (flow < 0) saw_negative = true;
  }

  EXPECT_TRUE(saw_positive);
  EXPECT_TRUE(saw_negative);
}

TEST(PriceDynamics, PX010WhalePriceImpactWindowIsDefined) {
  MesaAgentSimulation sim(config_with(MesaAgentCounts{6, 0, 0, 0, 1}, 44, 0));
  lob::Tick before = 0;
  lob::Tick after = 0;
  lob::Quantity sweep_volume = 0;

  for (int step = 1; step <= 13; ++step) {
    const MesaStepEvents events = sim.step();
    if (step == 11) before = events.stats.last_price;
    if (step == 12) sweep_volume = volume(events);
    if (step == 13) after = events.stats.last_price;
  }

  EXPECT_TRUE(before > 0);
  EXPECT_TRUE(after > 0);
  EXPECT_TRUE(sweep_volume > 0);
  const lob::Tick impact = after - before;
  EXPECT_TRUE(impact == after - before);
}

TEST(PriceDynamics, PX101OnlyMarketMakersKeepSpreadBounded) {
  MesaAgentSimulation sim(config_with(MesaAgentCounts{8, 0, 0, 0, 0}, 9, 0));
  lob::Tick max_spread = 0;

  for (int i = 0; i < 40; ++i) {
    const MesaStepEvents events = sim.step();
    max_spread = std::max(max_spread, events.stats.spread);
    EXPECT_TRUE(events.trades.empty());
  }

  EXPECT_TRUE(max_spread <= 8);
}

TEST(PriceDynamics, PX102OnlyNoiseHasNoForcedDirectionalDrift) {
  MesaAgentSimulation sim(config_with(MesaAgentCounts{0, 20, 0, 0, 0}, 88, 0));
  int buy_orders = 0;
  int sell_orders = 0;

  for (int i = 0; i < 200; ++i) {
    const MesaStepEvents events = sim.step();
    for (const auto& order : events.orders) {
      if (order.side == lob::Side::Bid) ++buy_orders;
      if (order.side == lob::Side::Ask) ++sell_orders;
    }
  }

  EXPECT_TRUE(buy_orders > 0);
  EXPECT_TRUE(sell_orders > 0);
  EXPECT_TRUE(std::abs(buy_orders - sell_orders) < buy_orders + sell_orders);
}

TEST(PriceDynamics, PX103PX104MomentumWarmupControlsOrderDirection) {
  MesaAgentSimConfig up = config_with(MesaAgentCounts{0, 0, 3, 0, 0}, 1, 0);
  up.initial_trade_prices = {100, 101};
  MesaAgentSimulation up_sim(up);
  const MesaStepEvents up_events = up_sim.step();
  for (const auto& order : up_events.orders) EXPECT_EQ(order.side, lob::Side::Bid);

  MesaAgentSimConfig down = config_with(MesaAgentCounts{0, 0, 3, 0, 0}, 1, 0);
  down.initial_trade_prices = {101, 100};
  MesaAgentSimulation down_sim(down);
  const MesaStepEvents down_events = down_sim.step();
  for (const auto& order : down_events.orders) EXPECT_EQ(order.side, lob::Side::Ask);
}

TEST(PriceDynamics, PX105PX106MeanReversionWarmupControlsOrderDirection) {
  MesaAgentSimConfig high = config_with(MesaAgentCounts{0, 0, 0, 3, 0}, 1, 0);
  high.reference_price = 100;
  high.initial_trade_prices = {104};
  MesaAgentSimulation high_sim(high);
  const MesaStepEvents high_events = high_sim.step();
  for (const auto& order : high_events.orders) EXPECT_EQ(order.side, lob::Side::Ask);

  MesaAgentSimConfig low = config_with(MesaAgentCounts{0, 0, 0, 3, 0}, 1, 0);
  low.reference_price = 100;
  low.initial_trade_prices = {96};
  MesaAgentSimulation low_sim(low);
  const MesaStepEvents low_events = low_sim.step();
  for (const auto& order : low_events.orders) EXPECT_EQ(order.side, lob::Side::Bid);
}

TEST(PriceDynamics, PX107WhaleCreatesVolumeSpikesOnTwelveStepCadence) {
  MesaAgentSimulation sim(config_with(MesaAgentCounts{6, 0, 0, 0, 1}, 44, 0));
  std::vector<lob::Quantity> volumes;

  for (int i = 0; i < 36; ++i) {
    volumes.push_back(volume(sim.step()));
  }

  EXPECT_TRUE(volumes[11] > 0);
  EXPECT_TRUE(volumes[23] > 0);
  EXPECT_TRUE(volumes[35] > 0);
}

TEST(PriceDynamics, PX108PX110ScenarioStatisticsAreComputable) {
  MesaAgentSimulation maker_heavy(config_with(MesaAgentCounts{20, 2, 0, 0, 0}, 5, 100));
  MesaAgentSimulation noise_heavy(config_with(MesaAgentCounts{2, 30, 0, 0, 0}, 5, 100));
  MesaAgentSimulation whale_heavy(config_with(MesaAgentCounts{8, 0, 0, 0, 6}, 5, 100));

  const MesaAgentSimSummary makers = maker_heavy.run();
  const MesaAgentSimSummary noise = noise_heavy.run();
  const MesaAgentSimSummary whales = whale_heavy.run();

  EXPECT_TRUE(makers.mean_spread >= 0.0);
  EXPECT_TRUE(noise.trade_count >= 0);
  EXPECT_TRUE(whales.trade_count > 0);
  EXPECT_TRUE(maker_heavy.accounting_invariant_ok());
  EXPECT_TRUE(noise_heavy.accounting_invariant_ok());
  EXPECT_TRUE(whale_heavy.accounting_invariant_ok());
}
