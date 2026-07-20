#include "lobx/simulation/market_phenomenon.hpp"
#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace lobx_test;

namespace {

using lobx::sim::MarketPhenomenon;
using lobx::sim::MarketPhenomenonType;
using lobx::sim::CrowdedPosition;
using lobx::sim::MarketObservation;
using lobx::sim::PhenomenonEvent;
using lobx::sim::PhenomenonTrade;

void expect_ok(const lobx::Result& result) {
  EXPECT_TRUE_MSG(result.ok, result.reason);
}

void expect_ok(const lobx::SubmitResult& result) {
  EXPECT_TRUE_MSG(result.accepted, result.reason);
}

bool has_type(const std::vector<MarketPhenomenon>& phenomena, MarketPhenomenonType type) {
  for (const auto& phenomenon : phenomena) {
    if (phenomenon.type == type) return true;
  }
  return false;
}

const char* type_name(MarketPhenomenonType type) {
  switch (type) {
    case MarketPhenomenonType::RepeatedRangeSweep: return "RepeatedRangeSweep";
    case MarketPhenomenonType::LongShortStopHunt: return "LongShortStopHunt";
    case MarketPhenomenonType::WickSpike: return "WickSpike";
    case MarketPhenomenonType::LiquidationCascade: return "LiquidationCascade";
    case MarketPhenomenonType::LiquidityVacuum: return "LiquidityVacuum";
    case MarketPhenomenonType::FalseBreakout: return "FalseBreakout";
    case MarketPhenomenonType::OrderBookImbalance: return "OrderBookImbalance";
    case MarketPhenomenonType::StopRun: return "StopRun";
    case MarketPhenomenonType::SpreadWidening: return "SpreadWidening";
    case MarketPhenomenonType::CrowdedTrade: return "CrowdedTrade";
    case MarketPhenomenonType::MeanReversionTrap: return "MeanReversionTrap";
    case MarketPhenomenonType::MomentumIgnition: return "MomentumIgnition";
    case MarketPhenomenonType::QuoteStuffing: return "QuoteStuffing";
    case MarketPhenomenonType::SpoofingLikeBehavior: return "SpoofingLikeBehavior";
    case MarketPhenomenonType::ChoppyMeanReversion: return "ChoppyMeanReversion";
  }
  return "Unknown";
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

MarketObservation observation(lob::Timestamp ts,
                              lob::Tick price,
                              lob::Tick best_bid,
                              lob::Tick best_ask,
                              lobx::Amount bid_depth,
                              lobx::Amount ask_depth,
                              lob::Quantity qty = 1,
                              lob::Side side = lob::Side::Bid) {
  MarketObservation obs{};
  obs.ts = ts;
  obs.price = price;
  obs.qty = qty;
  obs.aggressor_side = side;
  obs.best_bid = best_bid;
  obs.best_ask = best_ask;
  obs.bid_depth = bid_depth;
  obs.ask_depth = ask_depth;
  obs.trade_count = qty > 0 ? 1U : 0U;
  return obs;
}

std::vector<lobx::PerpRiskTier> high_maintenance_tiers() {
  return {lobx::PerpRiskTier{0, 0, 1000, 10000, 10}};
}

void maybe_write_market_scenario_artifacts(const std::string& name,
                                           const std::vector<PhenomenonTrade>& trades,
                                           const std::vector<MarketPhenomenon>& phenomena) {
  if (std::getenv("LOBX_WRITE_MARKET_SCENARIO_ARTIFACTS") == nullptr) return;
  const std::filesystem::path dir{"artifacts/market_scenarios"};
  std::filesystem::create_directories(dir);

  {
    std::ofstream csv(dir / (name + "_price_curve.csv"));
    csv << "step,timestamp,side,price,qty,label\n";
    int step = 1;
    for (const auto& trade : trades) {
      csv << step++ << ',' << trade.ts << ','
          << (trade.aggressor_side == lob::Side::Bid ? "buy" : "sell") << ','
          << trade.price << ',' << trade.qty << ',' << name << "\n";
    }
  }

  {
    std::ofstream json(dir / (name + ".json"));
    json << "{\n  \"name\":\"" << name << "\",\n  \"phenomena\":[";
    for (std::size_t i = 0; i < phenomena.size(); ++i) {
      if (i > 0) json << ',';
      json << "\n    {\"type\":\"" << type_name(phenomena[i].type)
           << "\",\"start_ts\":" << phenomena[i].start_ts
           << ",\"end_ts\":" << phenomena[i].end_ts
           << ",\"low\":" << phenomena[i].low
           << ",\"high\":" << phenomena[i].high
           << ",\"trade_count\":" << phenomena[i].trade_count
           << ",\"explanation\":\"" << phenomena[i].explanation << "\"}";
    }
    json << "\n  ]\n}\n";
  }

  {
    lob::Tick low = trades.empty() ? 0 : trades.front().price;
    lob::Tick high = low;
    for (const auto& trade : trades) {
      low = std::min(low, trade.price);
      high = std::max(high, trade.price);
    }
    const int width = 640;
    const int height = 260;
    const int pad = 36;
    const lob::Tick range = std::max<lob::Tick>(1, high - low);
    std::ostringstream points;
    for (std::size_t i = 0; i < trades.size(); ++i) {
      const double x = pad + (trades.size() <= 1 ? 0.0 : (static_cast<double>(i) / static_cast<double>(trades.size() - 1)) * (width - 2 * pad));
      const double y = height - pad - (static_cast<double>(trades[i].price - low) / static_cast<double>(range)) * (height - 2 * pad);
      if (i > 0) points << ' ';
      points << x << ',' << y;
    }
    std::ofstream svg(dir / (name + ".svg"));
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height << "\">\n"
        << "<title>" << name << " price curve</title>\n"
        << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n"
        << "<text x=\"16\" y=\"22\" font-size=\"16\">" << name << " price curve</text>\n"
        << "<text x=\"16\" y=\"244\" font-size=\"11\">x-axis step</text>\n"
        << "<text x=\"8\" y=\"135\" font-size=\"11\" transform=\"rotate(-90 8,135)\">y-axis price</text>\n"
        << "<polyline fill=\"none\" stroke=\"#2563eb\" stroke-width=\"2\" points=\"" << points.str() << "\"/>\n";
    for (std::size_t i = 0; i < trades.size(); ++i) {
      const double x = pad + (trades.size() <= 1 ? 0.0 : (static_cast<double>(i) / static_cast<double>(trades.size() - 1)) * (width - 2 * pad));
      const double y = height - pad - (static_cast<double>(trades[i].price - low) / static_cast<double>(range)) * (height - 2 * pad);
      svg << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"3\" fill=\"#111827\"/>\n";
    }
    svg << "<text x=\"420\" y=\"22\" font-size=\"11\">low=" << low << " high=" << high
        << " phenomena=" << phenomena.size() << "</text>\n</svg>\n";
  }
}

void maybe_write_market_scenario_artifacts(const std::string& name,
                                           const std::vector<MarketObservation>& observations,
                                           const std::vector<MarketPhenomenon>& phenomena) {
  if (std::getenv("LOBX_WRITE_MARKET_SCENARIO_ARTIFACTS") == nullptr) return;
  const std::filesystem::path dir{"artifacts/market_scenarios"};
  std::filesystem::create_directories(dir);

  {
    std::ofstream csv(dir / (name + "_price_curve.csv"));
    csv << "step,timestamp,price,qty,side,best_bid,best_ask,bid_depth,ask_depth,spread,trigger_count,liquidation_count,label\n";
    int step = 1;
    for (const auto& obs : observations) {
      const lob::Tick spread = obs.best_ask > obs.best_bid ? obs.best_ask - obs.best_bid : 0;
      csv << step++ << ',' << obs.ts << ',' << obs.price << ',' << obs.qty << ','
          << (obs.aggressor_side == lob::Side::Bid ? "buy" : "sell") << ','
          << obs.best_bid << ',' << obs.best_ask << ','
          << obs.bid_depth << ',' << obs.ask_depth << ',' << spread << ','
          << obs.trigger_fired_count << ',' << obs.liquidation_count << ',' << name << "\n";
    }
  }

  auto write_json = [&](const std::filesystem::path& path) {
    std::ofstream json(path);
    json << "{\n  \"scenario\":\"" << name << "\",\n  \"phenomena\":[";
    for (std::size_t i = 0; i < phenomena.size(); ++i) {
      if (i > 0) json << ',';
      json << "\n    {\"type\":\"" << type_name(phenomena[i].type)
           << "\",\"start_ts\":" << phenomena[i].start_ts
           << ",\"end_ts\":" << phenomena[i].end_ts
           << ",\"score\":" << static_cast<double>(phenomena[i].score)
           << ",\"price_impact\":" << phenomena[i].price_impact
           << ",\"explanation\":\"" << phenomena[i].explanation << "\"}";
    }
    json << "\n  ],\n  \"series\":[";
    for (std::size_t i = 0; i < observations.size(); ++i) {
      if (i > 0) json << ',';
      json << "\n    {\"step\":" << (i + 1)
           << ",\"timestamp\":" << observations[i].ts
           << ",\"price\":" << observations[i].price
           << ",\"qty\":" << observations[i].qty
           << ",\"best_bid\":" << observations[i].best_bid
           << ",\"best_ask\":" << observations[i].best_ask
           << ",\"bid_depth\":" << observations[i].bid_depth
           << ",\"ask_depth\":" << observations[i].ask_depth
           << ",\"trigger_count\":" << observations[i].trigger_fired_count
           << ",\"liquidation_count\":" << observations[i].liquidation_count << "}";
    }
    json << "\n  ]\n}\n";
  };
  write_json(dir / (name + "_price_curve.json"));
  write_json(dir / (name + ".json"));

  {
    lob::Tick low = observations.empty() ? 0 : observations.front().price;
    lob::Tick high = low;
    for (const auto& obs : observations) {
      low = std::min(low, obs.price);
      high = std::max(high, obs.price);
    }
    const int width = 680;
    const int height = 280;
    const int pad = 40;
    const lob::Tick range = std::max<lob::Tick>(1, high - low);
    std::ostringstream points;
    for (std::size_t i = 0; i < observations.size(); ++i) {
      const double x = pad + (observations.size() <= 1 ? 0.0 : (static_cast<double>(i) / static_cast<double>(observations.size() - 1)) * (width - 2 * pad));
      const double y = height - pad - (static_cast<double>(observations[i].price - low) / static_cast<double>(range)) * (height - 2 * pad);
      if (i > 0) points << ' ';
      points << x << ',' << y;
    }
    std::ofstream svg(dir / (name + "_price_curve.svg"));
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height << "\">\n"
        << "<title>" << name << " market scenario</title>\n"
        << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n"
        << "<text x=\"16\" y=\"24\" font-size=\"16\">" << name << " market scenario</text>\n"
        << "<text x=\"18\" y=\"262\" font-size=\"11\">x-axis step</text>\n"
        << "<text x=\"10\" y=\"150\" font-size=\"11\" transform=\"rotate(-90 10,150)\">y-axis price</text>\n"
        << "<polyline fill=\"none\" stroke=\"#0f766e\" stroke-width=\"2\" points=\"" << points.str() << "\"/>\n";
    for (std::size_t i = 0; i < observations.size(); ++i) {
      const double x = pad + (observations.size() <= 1 ? 0.0 : (static_cast<double>(i) / static_cast<double>(observations.size() - 1)) * (width - 2 * pad));
      const double y = height - pad - (static_cast<double>(observations[i].price - low) / static_cast<double>(range)) * (height - 2 * pad);
      svg << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"3\" fill=\"#111827\"/>\n";
      if (observations[i].trigger_fired_count > 0 || observations[i].liquidation_count > 0) {
        svg << "<text x=\"" << (x + 5) << "\" y=\"" << (y - 7) << "\" font-size=\"10\">"
            << "T" << observations[i].trigger_fired_count
            << "/L" << observations[i].liquidation_count << "</text>\n";
      }
    }
    svg << "<text x=\"430\" y=\"24\" font-size=\"11\">low=" << low << " high=" << high
        << " phenomena=" << phenomena.size() << "</text>\n</svg>\n";
  }
}

void create_last_price_stop_hunt(ExchangeFixture& f) {
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, 970101, lob::Side::Ask,
                                    106, 2, lob::POST_ONLY, 1));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.carol, 970102, lob::Side::Bid,
                                    94, 2, lob::POST_ONLY, 2));
  expect_ok(f.exchange.create_trigger_order(f.perp_symbol, f.alice, 970110,
                                            lob::Side::Bid, 1, 105,
                                            lobx::TriggerPriceType::Last,
                                            lobx::TriggerCondition::AboveOrEqual,
                                            lobx::TriggerChildOrderType::Market,
                                            0, 120, lob::NONE, 3));
  expect_ok(f.exchange.create_trigger_order(f.perp_symbol, f.alice, 970111,
                                            lob::Side::Ask, 1, 95,
                                            lobx::TriggerPriceType::Last,
                                            lobx::TriggerCondition::BelowOrEqual,
                                            lobx::TriggerChildOrderType::Market,
                                            0, 90, lob::NONE, 4));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.alice, 970120, lob::Side::Bid,
                                    106, 1, lob::IOC, 5));
  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Last, 6), 1);
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.alice, 970121, lob::Side::Ask,
                                    94, 1, lob::IOC, 7));
  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Last, 8), 1);
}

void configure_liquidation_cluster(ExchangeFixture& f) {
  expect_ok(f.exchange.set_perp_risk_tiers(f.perp_market_id, high_maintenance_tiers()));
  expect_ok(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::IndexPrice));
  lobx::LiquidationOptions options;
  options.mode = lobx::LiquidationMode::InfiniteInsurance;
  f.exchange.set_liquidation_options(options);
  f.deposit(f.bob, "USDT", 3000000);
  f.deposit(40, "USDT", 1000000);
  for (lobx::UserId user : {f.alice, f.bob, f.carol, static_cast<lobx::UserId>(40)}) {
    f.exchange.set_leverage(user, f.perp_symbol, 10);
  }
}

} // namespace

TEST(RobotMarketScenarios, RangeSweepBotsCreateRepeatedRangeSweep) {
  auto f = ExchangeFixture::Spot();
  for (int i = 0; i < 3; ++i) {
    const lobx::OrderId base = 970000 + i * 10;
    expect_ok(f.exchange.submit_limit(f.spot_symbol, f.bob, base + 1, lob::Side::Ask,
                                      101, 1, lob::POST_ONLY, base + 1));
    expect_ok(f.exchange.submit_limit(f.spot_symbol, f.alice, base + 2, lob::Side::Bid,
                                      101, 1, lob::IOC, base + 2));
    expect_ok(f.exchange.submit_limit(f.spot_symbol, f.bob, base + 3, lob::Side::Bid,
                                      100, 1, lob::POST_ONLY, base + 3));
    expect_ok(f.exchange.submit_limit(f.spot_symbol, f.carol, base + 4, lob::Side::Ask,
                                      100, 1, lob::IOC, base + 4));
  }

  const auto trades = exchange_trades(f);
  const auto phenomena = lobx::sim::detect_market_phenomena(trades, exchange_events(f));
  maybe_write_market_scenario_artifacts("range_sweep", trades, phenomena);

  EXPECT_EQ(trades.size(), 6UL);
  EXPECT_TRUE(has_type(phenomena, MarketPhenomenonType::RepeatedRangeSweep));
}

TEST(RobotMarketScenarios, LeverageClusterCreatesLiquidationCascade) {
  auto f = ExchangeFixture::Perp();
  configure_liquidation_cluster(f);
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, 970201, lob::Side::Ask,
                                    100, 300000, lob::POST_ONLY, 1));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.alice, 970202, lob::Side::Bid,
                                    100, 100000, lob::IOC, 2));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.carol, 970203, lob::Side::Bid,
                                    100, 100000, lob::IOC, 3));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, 40, 970204, lob::Side::Bid,
                                    100, 100000, lob::IOC, 4));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 1));
  expect_ok(f.exchange.liquidate_if_needed(f.perp_symbol, f.alice, 10));
  expect_ok(f.exchange.liquidate_if_needed(f.perp_symbol, f.carol, 11));
  expect_ok(f.exchange.liquidate_if_needed(f.perp_symbol, 40, 12));

  std::vector<PhenomenonTrade> price_path{{1, 100, 1, lob::Side::Bid},
                                          {2, 60, 1, lob::Side::Ask},
                                          {3, 20, 1, lob::Side::Ask},
                                          {4, 1, 1, lob::Side::Ask}};
  const auto phenomena = lobx::sim::detect_market_phenomena(price_path, exchange_events(f));
  maybe_write_market_scenario_artifacts("liquidation_cascade", price_path, phenomena);

  EXPECT_TRUE(has_type(phenomena, MarketPhenomenonType::LiquidationCascade));
  EXPECT_EQ(f.exchange.bad_debt(f.perp_symbol), 0);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 0);
  EXPECT_EQ(f.exchange.position(f.carol, f.perp_symbol).signed_qty, 0);
  EXPECT_EQ(f.exchange.position(40, f.perp_symbol).signed_qty, 0);
}

TEST(RobotMarketScenarios, StopHuntBotsTriggerBothSides) {
  auto f = ExchangeFixture::Perp();
  create_last_price_stop_hunt(f);

  const auto trades = exchange_trades(f);
  const auto phenomena = lobx::sim::detect_market_phenomena(trades, exchange_events(f));
  maybe_write_market_scenario_artifacts("stop_hunt", trades, phenomena);

  EXPECT_TRUE(has_type(phenomena, MarketPhenomenonType::LongShortStopHunt));
}

TEST(RobotMarketScenariosBatch2, ThinBookCreatesLiquidityVacuum) {
  auto f = ExchangeFixture::Spot();
  f.deposit(40, "BTC", 1000000);
  expect_ok(f.exchange.submit_limit(f.spot_symbol, f.bob, 980001, lob::Side::Ask,
                                    100, 1, lob::POST_ONLY, 1));
  expect_ok(f.exchange.submit_limit(f.spot_symbol, f.carol, 980002, lob::Side::Ask,
                                    110, 1, lob::POST_ONLY, 2));
  expect_ok(f.exchange.submit_limit(f.spot_symbol, 40, 980003, lob::Side::Ask,
                                    130, 1, lob::POST_ONLY, 3));
  expect_ok(f.exchange.submit_limit(f.spot_symbol, f.alice, 980004, lob::Side::Bid,
                                    130, 3, lob::IOC, 4));

  const auto trades = exchange_trades(f);
  const std::vector<MarketObservation> observations{
      observation(1, 100, 99, 100, 100, 3),
      observation(2, 110, 99, 110, 100, 1),
      observation(3, 130, 99, 130, 100, 0),
  };
  const auto phenomena = lobx::sim::detect_market_phenomena(trades, observations, exchange_events(f));
  maybe_write_market_scenario_artifacts("liquidity_vacuum", observations, phenomena);

  EXPECT_EQ(trades.size(), 3UL);
  EXPECT_TRUE(has_type(phenomena, MarketPhenomenonType::LiquidityVacuum));
}

TEST(RobotMarketScenariosBatch2, FalseBreakoutTrapsMomentumBots) {
  const std::vector<MarketObservation> observations{
      observation(1, 100, 99, 101, 400, 400),
      observation(2, 104, 103, 105, 350, 350),
      observation(3, 105, 104, 106, 330, 330),
      observation(4, 107, 106, 108, 220, 180),
      observation(5, 104, 103, 105, 360, 360, 1, lob::Side::Ask),
      observation(6, 103, 102, 104, 380, 380, 1, lob::Side::Ask),
  };

  const auto phenomena = lobx::sim::detect_market_phenomena(observations);
  maybe_write_market_scenario_artifacts("false_breakout", observations, phenomena);

  EXPECT_TRUE(has_type(phenomena, MarketPhenomenonType::FalseBreakout));
  EXPECT_FALSE(has_type(phenomena, MarketPhenomenonType::WickSpike));
}

TEST(RobotMarketScenariosBatch2, StopClusterCreatesStopRun) {
  const std::vector<MarketObservation> observations{
      observation(1, 104, 103, 105, 500, 500),
      observation(2, 105, 104, 106, 480, 400),
      observation(3, 108, 107, 109, 420, 280),
      observation(4, 110, 109, 111, 380, 240),
  };
  std::vector<PhenomenonEvent> events{
      PhenomenonEvent{2, "trigger.fired", "side=BID trigger=980101"},
      PhenomenonEvent{3, "trigger.fired", "side=BID trigger=980102"},
      PhenomenonEvent{4, "trigger.child_order", "side=BID child=980103"},
  };

  const auto phenomena = lobx::sim::detect_market_phenomena(observations, events);
  maybe_write_market_scenario_artifacts("stop_run", observations, phenomena);

  EXPECT_TRUE(has_type(phenomena, MarketPhenomenonType::StopRun));
}

TEST(RobotMarketScenariosBatch2, MakerWithdrawalCreatesSpreadWidening) {
  auto f = ExchangeFixture::Spot();
  expect_ok(f.exchange.submit_limit(f.spot_symbol, f.bob, 980201, lob::Side::Bid,
                                    99, 10, lob::POST_ONLY, 1));
  expect_ok(f.exchange.submit_limit(f.spot_symbol, f.carol, 980202, lob::Side::Ask,
                                    101, 10, lob::POST_ONLY, 2));
  expect_ok(f.exchange.submit_limit(f.spot_symbol, f.bob, 980203, lob::Side::Bid,
                                    90, 10, lob::POST_ONLY, 3));
  expect_ok(f.exchange.submit_limit(f.spot_symbol, f.carol, 980204, lob::Side::Ask,
                                    110, 10, lob::POST_ONLY, 4));
  EXPECT_TRUE(f.exchange.cancel(f.spot_symbol, f.bob, 980201, 5));
  EXPECT_TRUE(f.exchange.cancel(f.spot_symbol, f.carol, 980202, 6));

  const std::vector<MarketObservation> observations{
      observation(1, 100, 99, 101, 10, 10, 0),
      observation(2, 100, 90, 110, 1, 1, 0),
  };
  const auto phenomena = lobx::sim::detect_market_phenomena(observations, exchange_events(f));
  maybe_write_market_scenario_artifacts("spread_widening", observations, phenomena);

  EXPECT_TRUE(has_type(phenomena, MarketPhenomenonType::SpreadWidening));
  EXPECT_FALSE(has_type(phenomena, MarketPhenomenonType::LiquidityVacuum));
}

TEST(RobotMarketScenariosBatch2, CrowdedLongsBecomeCascadeRisk) {
  auto f = ExchangeFixture::Perp();
  expect_ok(f.exchange.set_perp_risk_tiers(f.perp_market_id, high_maintenance_tiers()));
  expect_ok(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::IndexPrice));
  lobx::LiquidationOptions options;
  options.mode = lobx::LiquidationMode::InfiniteInsurance;
  f.exchange.set_liquidation_options(options);

  const std::vector<lobx::UserId> longs{f.alice, f.carol, 40, 41, 42, 43};
  f.deposit(f.bob, "USDT", 3000000);
  for (lobx::UserId user : longs) {
    f.deposit(user, "USDT", 1000000);
    f.exchange.set_leverage(user, f.perp_symbol, 10);
  }
  f.exchange.set_leverage(f.bob, f.perp_symbol, 10);

  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, 980301, lob::Side::Ask,
                                    100, 300000, lob::POST_ONLY, 1));
  lobx::OrderId order_id = 980302;
  lob::Timestamp ts = 2;
  std::vector<CrowdedPosition> positions;
  for (lobx::UserId user : longs) {
    expect_ok(f.exchange.submit_limit(f.perp_symbol, user, order_id++, lob::Side::Bid,
                                      100, 50000, lob::IOC, ts++));
    positions.push_back(CrowdedPosition{user, lob::Side::Bid, 100, 50000, 10});
  }

  expect_ok(f.exchange.set_index_price(f.perp_market_id, 1));
  std::size_t liquidation_step = 7;
  for (lobx::UserId user : longs) {
    expect_ok(f.exchange.liquidate_if_needed(f.perp_symbol, user,
                                             static_cast<lob::Timestamp>(liquidation_step++)));
  }

  std::vector<MarketObservation> observations{
      observation(1, 100, 99, 101, 1000, 1000),
      observation(2, 80, 79, 81, 700, 300, 1, lob::Side::Ask),
      observation(3, 40, 39, 41, 300, 100, 1, lob::Side::Ask),
      observation(4, 1, 0, 2, 100, 20, 1, lob::Side::Ask),
  };
  observations[2].liquidation_count = 3;
  observations[3].liquidation_count = 3;

  const auto phenomena = lobx::sim::detect_market_phenomena(
      observations, exchange_events(f), positions);
  maybe_write_market_scenario_artifacts("crowded_trade", observations, phenomena);

  EXPECT_TRUE(has_type(phenomena, MarketPhenomenonType::CrowdedTrade));
  EXPECT_TRUE(has_type(phenomena, MarketPhenomenonType::LiquidationCascade));
  for (lobx::UserId user : longs) {
    EXPECT_EQ(f.exchange.position(user, f.perp_symbol).signed_qty, 0);
  }
}
