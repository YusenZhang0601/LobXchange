#include "lobx/simulation/market_phenomenon.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace lobx::sim {

namespace {

struct TradeSummary {
  lob::Timestamp start_ts{0};
  lob::Timestamp end_ts{0};
  lob::Tick low{0};
  lob::Tick high{0};
  lob::Tick open{0};
  lob::Tick close{0};
  Amount volume{0};
};

TradeSummary summarize(const std::vector<PhenomenonTrade>& trades) {
  TradeSummary s{};
  if (trades.empty()) return s;
  s.start_ts = trades.front().ts;
  s.end_ts = trades.back().ts;
  s.low = trades.front().price;
  s.high = trades.front().price;
  s.open = trades.front().price;
  s.close = trades.back().price;
  for (const PhenomenonTrade& trade : trades) {
    s.low = std::min(s.low, trade.price);
    s.high = std::max(s.high, trade.price);
    if (trade.qty > 0 && s.volume <= std::numeric_limits<Amount>::max() - trade.qty) {
      s.volume += trade.qty;
    }
  }
  return s;
}

std::vector<PhenomenonTrade> trades_from_observations(const std::vector<MarketObservation>& observations) {
  std::vector<PhenomenonTrade> trades;
  trades.reserve(observations.size());
  for (const MarketObservation& obs : observations) {
    if (obs.trade_count == 0 && obs.qty == 0) continue;
    trades.push_back(PhenomenonTrade{obs.ts, obs.price, obs.qty, obs.aggressor_side});
  }
  return trades;
}

MarketPhenomenon make_phenomenon(MarketPhenomenonType type,
                                 const std::vector<PhenomenonTrade>& trades,
                                 const std::string& explanation) {
  const TradeSummary s = summarize(trades);
  MarketPhenomenon p{};
  p.type = type;
  p.start_ts = s.start_ts;
  p.end_ts = s.end_ts;
  p.low = s.low;
  p.high = s.high;
  p.open = s.open;
  p.close = s.close;
  p.volume = s.volume;
  p.trade_count = trades.size();
  p.explanation = explanation;
  return p;
}

int count_events(const std::vector<PhenomenonEvent>& events, const std::string& type) {
  int count = 0;
  for (const PhenomenonEvent& event : events) {
    if (event.type == type) ++count;
  }
  return count;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

bool mostly_one_direction(const std::vector<PhenomenonTrade>& trades) {
  if (trades.size() < 2) return false;
  int up = 0;
  int down = 0;
  for (std::size_t i = 1; i < trades.size(); ++i) {
    if (trades[i].price > trades[i - 1].price) ++up;
    if (trades[i].price < trades[i - 1].price) ++down;
  }
  return up == 0 || down == 0 || std::max(up, down) >= static_cast<int>(trades.size() - 1);
}

Amount total_depth(const MarketObservation& obs) {
  Amount depth = 0;
  if (obs.bid_depth > 0 && depth <= std::numeric_limits<Amount>::max() - obs.bid_depth) {
    depth += obs.bid_depth;
  }
  if (obs.ask_depth > 0 && depth <= std::numeric_limits<Amount>::max() - obs.ask_depth) {
    depth += obs.ask_depth;
  }
  return depth;
}

lob::Tick spread(const MarketObservation& obs) {
  if (obs.best_ask <= obs.best_bid) return 0;
  return obs.best_ask - obs.best_bid;
}

void detect_trade_path_phenomena(std::vector<MarketPhenomenon>& out,
                                 const std::vector<PhenomenonTrade>& trades,
                                 const std::vector<PhenomenonEvent>& events,
                                 MarketPhenomenonConfig config) {
  if (trades.empty()) return;

  const TradeSummary s = summarize(trades);
  const lob::Tick range = s.high - s.low;

  if (trades.size() >= config.min_range_sweep_trades &&
      range <= config.range_threshold &&
      s.volume >= config.min_range_sweep_volume) {
    int low_touches = 0;
    int high_touches = 0;
    int side_changes = 0;
    for (std::size_t i = 0; i < trades.size(); ++i) {
      if (trades[i].price == s.low) ++low_touches;
      if (trades[i].price == s.high) ++high_touches;
      if (i > 0 && trades[i].aggressor_side != trades[i - 1].aggressor_side) ++side_changes;
    }
    if (low_touches >= 2 && high_touches >= 2 && side_changes >= 3) {
      std::ostringstream os;
      os << "range=" << range << ",low_touches=" << low_touches
         << ",high_touches=" << high_touches << ",side_changes=" << side_changes;
      MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::RepeatedRangeSweep, trades, os.str());
      p.score = static_cast<long double>(side_changes);
      out.push_back(p);
    }
  }

  const lob::Tick body = std::max<lob::Tick>(1, s.open > s.close ? s.open - s.close : s.close - s.open);
  const lob::Tick upper_wick = s.high - std::max(s.open, s.close);
  const lob::Tick lower_wick = std::min(s.open, s.close) - s.low;
  if (upper_wick > 0 &&
      static_cast<long double>(upper_wick) / static_cast<long double>(body) >= config.wick_ratio_threshold) {
    std::ostringstream os;
    os << "direction=up,high=" << s.high << ",close=" << s.close
       << ",upper_wick=" << upper_wick << ",body=" << body
       << ",wick_ratio=" << static_cast<long double>(upper_wick) / static_cast<long double>(body);
    MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::WickSpike, trades, os.str());
    p.price_impact = upper_wick;
    p.score = static_cast<long double>(upper_wick) / static_cast<long double>(body);
    out.push_back(p);
  }
  if (lower_wick > 0 &&
      static_cast<long double>(lower_wick) / static_cast<long double>(body) >= config.wick_ratio_threshold) {
    std::ostringstream os;
    os << "direction=down,low=" << s.low << ",close=" << s.close
       << ",lower_wick=" << lower_wick << ",body=" << body
       << ",wick_ratio=" << static_cast<long double>(lower_wick) / static_cast<long double>(body);
    MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::WickSpike, trades, os.str());
    p.price_impact = lower_wick;
    p.score = static_cast<long double>(lower_wick) / static_cast<long double>(body);
    out.push_back(p);
  }

  int fired_bid = 0;
  int fired_ask = 0;
  for (const PhenomenonEvent& event : events) {
    if (event.type != "trigger.fired" && event.type != "trigger.child_order") continue;
    if (contains(event.payload, "side=BID")) ++fired_bid;
    if (contains(event.payload, "side=ASK")) ++fired_ask;
  }
  if (fired_bid > 0 && fired_ask > 0 && range >= config.min_stop_hunt_range) {
    std::ostringstream os;
    os << "trigger_bid=" << fired_bid << ",trigger_ask=" << fired_ask
       << ",price_range=" << range;
    MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::LongShortStopHunt, trades, os.str());
    p.trigger_count = static_cast<std::size_t>(fired_bid + fired_ask);
    p.event_count = p.trigger_count;
    p.price_impact = range;
    p.score = static_cast<long double>(range);
    out.push_back(p);
  }

  const int liquidation_count = count_events(events, "liquidation");
  const int insurance_absorbed = count_events(events, "insurance_fund.absorbed_loss") +
                                 count_events(events, "insurance_fund.debited") +
                                 count_events(events, "perp.bad_debt_recorded");
  if (liquidation_count >= static_cast<int>(config.min_liquidation_count) && insurance_absorbed > 0) {
    std::ostringstream os;
    os << "liquidations=" << liquidation_count << ",insurance_events=" << insurance_absorbed
       << ",price_range=" << range;
    MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::LiquidationCascade, trades, os.str());
    p.liquidation_count = static_cast<std::size_t>(liquidation_count);
    p.event_count = static_cast<std::size_t>(liquidation_count + insurance_absorbed);
    p.price_impact = range;
    p.score = static_cast<long double>(liquidation_count);
    out.push_back(p);
  }

  if (range >= config.min_liquidity_vacuum_move &&
      s.volume > 0 &&
      s.volume <= config.max_liquidity_vacuum_volume &&
      mostly_one_direction(trades)) {
    const long double impact = static_cast<long double>(range) / static_cast<long double>(std::max<Amount>(1, s.volume));
    if (impact >= config.min_liquidity_vacuum_impact) {
      std::ostringstream os;
      os << "price impact=" << impact << ",move=" << range
         << ",volume=" << s.volume << ",levels swept=" << trades.size();
      MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::LiquidityVacuum, trades, os.str());
      p.price_impact = range;
      p.score = impact;
      out.push_back(p);
    }
  }

  for (std::size_t i = 2; i + 1 < trades.size(); ++i) {
    lob::Tick prior_low = trades.front().price;
    lob::Tick prior_high = trades.front().price;
    for (std::size_t j = 0; j < i; ++j) {
      prior_low = std::min(prior_low, trades[j].price);
      prior_high = std::max(prior_high, trades[j].price);
    }
    const bool broke_up = trades[i].price >= prior_high + config.false_breakout_min_ticks;
    const bool broke_down = trades[i].price + config.false_breakout_min_ticks <= prior_low;
    if (!broke_up && !broke_down) continue;

    const std::size_t end = std::min(trades.size() - 1, i + config.false_breakout_max_return_steps);
    for (std::size_t j = i + 1; j <= end; ++j) {
      const bool returned_inside = (broke_up && trades[j].price <= prior_high) ||
                                   (broke_down && trades[j].price >= prior_low);
      if (!returned_inside) continue;
      std::ostringstream os;
      os << "breakout level=" << (broke_up ? prior_high : prior_low)
         << ",breakout_price=" << trades[i].price
         << ",return_step=" << (j - i)
         << ",direction=" << (broke_up ? "up" : "down");
      MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::FalseBreakout, trades, os.str());
      p.price_impact = broke_up ? trades[i].price - prior_high : prior_low - trades[i].price;
      p.score = static_cast<long double>(p.price_impact);
      out.push_back(p);
      return;
    }
  }

  if (fired_bid >= static_cast<int>(config.min_stop_run_triggers) &&
      s.close >= s.open + config.min_stop_run_follow_through) {
    std::ostringstream os;
    os << "trigger_count=" << fired_bid << ",direction=up,follow_through=" << (s.close - s.open);
    MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::StopRun, trades, os.str());
    p.trigger_count = static_cast<std::size_t>(fired_bid);
    p.event_count = p.trigger_count;
    p.price_impact = s.close - s.open;
    p.score = static_cast<long double>(fired_bid);
    out.push_back(p);
  }
  if (fired_ask >= static_cast<int>(config.min_stop_run_triggers) &&
      s.open >= s.close + config.min_stop_run_follow_through) {
    std::ostringstream os;
    os << "trigger_count=" << fired_ask << ",direction=down,follow_through=" << (s.open - s.close);
    MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::StopRun, trades, os.str());
    p.trigger_count = static_cast<std::size_t>(fired_ask);
    p.event_count = p.trigger_count;
    p.price_impact = s.open - s.close;
    p.score = static_cast<long double>(fired_ask);
    out.push_back(p);
  }
}

void detect_observation_phenomena(std::vector<MarketPhenomenon>& out,
                                  const std::vector<PhenomenonTrade>& trades,
                                  const std::vector<MarketObservation>& observations,
                                  MarketPhenomenonConfig config) {
  if (observations.size() < 2) return;

  for (std::size_t i = 0; i + 1 < observations.size(); ++i) {
    const Amount total = total_depth(observations[i]);
    if (total <= 0) continue;
    const long double bid_ratio = static_cast<long double>(observations[i].bid_depth) / static_cast<long double>(total);
    const lob::Tick start_price = observations[i].price;
    for (std::size_t j = i + 1; j < observations.size(); ++j) {
      const lob::Tick end_price = observations[j].price;
      const bool up = bid_ratio >= config.order_book_imbalance_threshold &&
                      end_price >= start_price + config.min_imbalance_follow_through;
      const bool down = bid_ratio <= (1.0L - config.order_book_imbalance_threshold) &&
                        start_price >= end_price + config.min_imbalance_follow_through;
      if (!up && !down) continue;
      std::ostringstream os;
      os << "imbalance ratio=" << bid_ratio << ",direction=" << (up ? "up" : "down")
         << ",bid_depth=" << observations[i].bid_depth
         << ",ask_depth=" << observations[i].ask_depth;
      MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::OrderBookImbalance, trades, os.str());
      p.start_ts = observations[i].ts;
      p.end_ts = observations[j].ts;
      p.depth_before = total;
      p.depth_after = total_depth(observations[j]);
      p.price_impact = up ? end_price - start_price : start_price - end_price;
      p.score = std::abs(bid_ratio - 0.5L) * 2.0L;
      out.push_back(p);
      break;
    }
    if (!out.empty() && out.back().type == MarketPhenomenonType::OrderBookImbalance) break;
  }

  for (std::size_t i = 0; i + 1 < observations.size(); ++i) {
    const lob::Tick before_spread = spread(observations[i]);
    if (before_spread <= 0) continue;
    for (std::size_t j = i + 1; j < observations.size(); ++j) {
      const lob::Tick after_spread = spread(observations[j]);
      const Amount depth_before = total_depth(observations[i]);
      const Amount depth_after = total_depth(observations[j]);
      if (after_spread < before_spread + config.min_spread_widening_ticks) continue;
      if (static_cast<long double>(after_spread) <
          static_cast<long double>(before_spread) * config.spread_widening_multiplier) continue;
      if (depth_before > 0 && depth_after >= depth_before) continue;

      std::ostringstream os;
      os << "spread_before=" << before_spread << ",spread_after=" << after_spread
         << ",depth_before=" << depth_before << ",depth_after=" << depth_after;
      MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::SpreadWidening, trades, os.str());
      p.start_ts = observations[i].ts;
      p.end_ts = observations[j].ts;
      p.depth_before = depth_before;
      p.depth_after = depth_after;
      p.price_impact = after_spread - before_spread;
      p.score = static_cast<long double>(after_spread) / static_cast<long double>(std::max<lob::Tick>(1, before_spread));
      out.push_back(p);
      return;
    }
  }

  if (!trades.empty()) {
    const TradeSummary s = summarize(trades);
    const Amount depth_before = total_depth(observations.front());
    const Amount depth_after = total_depth(observations.back());
    const lob::Tick range = s.high - s.low;
    bool depth_drop = false;
    if (depth_before > 0) {
      const long double ratio = static_cast<long double>(depth_after) / static_cast<long double>(depth_before);
      depth_drop = ratio <= config.max_liquidity_vacuum_depth_ratio;
    }
    if (range >= config.min_liquidity_vacuum_move && depth_drop) {
      std::ostringstream os;
      os << "price impact=" << range << ",thin depth,depth_before=" << depth_before
         << ",depth_after=" << depth_after << ",levels swept=" << trades.size();
      MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::LiquidityVacuum, trades, os.str());
      p.depth_before = depth_before;
      p.depth_after = depth_after;
      p.price_impact = range;
      p.score = depth_after == 0 ? static_cast<long double>(range)
                                 : static_cast<long double>(range) / static_cast<long double>(depth_after);
      out.push_back(p);
    }
  }
}

void detect_crowded_positions(std::vector<MarketPhenomenon>& out,
                              const std::vector<PhenomenonTrade>& trades,
                              const std::vector<CrowdedPosition>& positions,
                              MarketPhenomenonConfig config) {
  for (const lob::Side side : {lob::Side::Bid, lob::Side::Ask}) {
    std::vector<CrowdedPosition> same_side;
    for (const CrowdedPosition& position : positions) {
      if (position.side == side && position.qty > 0) same_side.push_back(position);
    }
    if (same_side.size() < config.crowded_trade_min_users) continue;

    lob::Tick low_entry = same_side.front().entry_price;
    lob::Tick high_entry = same_side.front().entry_price;
    long double leverage_sum = 0.0L;
    Amount qty_sum = 0;
    for (const CrowdedPosition& position : same_side) {
      low_entry = std::min(low_entry, position.entry_price);
      high_entry = std::max(high_entry, position.entry_price);
      leverage_sum += position.leverage;
      if (position.qty > 0 && qty_sum <= std::numeric_limits<Amount>::max() - position.qty) {
        qty_sum += position.qty;
      }
    }
    const lob::Tick entry_range = high_entry - low_entry;
    const long double avg_leverage = leverage_sum / static_cast<long double>(same_side.size());
    const long double score = (static_cast<long double>(same_side.size()) /
                               static_cast<long double>(config.crowded_trade_min_users)) *
                              (avg_leverage / static_cast<long double>(config.crowded_trade_min_leverage));
    if (entry_range > config.crowded_trade_max_entry_range ||
        avg_leverage < static_cast<long double>(config.crowded_trade_min_leverage) ||
        score < config.crowded_trade_min_score) {
      continue;
    }

    std::ostringstream os;
    os << "side=" << (side == lob::Side::Bid ? "long" : "short")
       << ",user_count=" << same_side.size()
       << ",entry_range=" << low_entry << '-' << high_entry
       << ",average leverage=" << avg_leverage
       << ",qty=" << qty_sum;
    MarketPhenomenon p = make_phenomenon(MarketPhenomenonType::CrowdedTrade, trades, os.str());
    p.volume = qty_sum;
    p.score = score;
    out.push_back(p);
  }
}

} // namespace

std::vector<MarketPhenomenon> detect_market_phenomena(const std::vector<PhenomenonTrade>& trades,
                                                      const std::vector<PhenomenonEvent>& events,
                                                      MarketPhenomenonConfig config) {
  std::vector<MarketPhenomenon> out;
  detect_trade_path_phenomena(out, trades, events, config);
  return out;
}

std::vector<MarketPhenomenon> detect_market_phenomena(const std::vector<MarketObservation>& observations,
                                                      const std::vector<PhenomenonEvent>& events,
                                                      const std::vector<CrowdedPosition>& positions,
                                                      MarketPhenomenonConfig config) {
  const std::vector<PhenomenonTrade> trades = trades_from_observations(observations);
  return detect_market_phenomena(trades, observations, events, positions, config);
}

std::vector<MarketPhenomenon> detect_market_phenomena(const std::vector<PhenomenonTrade>& trades,
                                                      const std::vector<MarketObservation>& observations,
                                                      const std::vector<PhenomenonEvent>& events,
                                                      const std::vector<CrowdedPosition>& positions,
                                                      MarketPhenomenonConfig config) {
  std::vector<MarketPhenomenon> out;
  detect_trade_path_phenomena(out, trades, events, config);
  detect_observation_phenomena(out, trades, observations, config);
  detect_crowded_positions(out, trades, positions, config);
  return out;
}

} // namespace lobx::sim
