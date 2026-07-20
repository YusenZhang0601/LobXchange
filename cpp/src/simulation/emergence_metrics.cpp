#include "lobx/simulation/emergence_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>

namespace lobx::sim {

namespace {

long double median(std::vector<long double> values) {
  if (values.empty()) return 0.0L;
  std::sort(values.begin(), values.end());
  const size_t mid = values.size() / 2;
  if ((values.size() % 2) == 0) return (values[mid - 1] + values[mid]) / 2.0L;
  return values[mid];
}

long double mean(const std::vector<long double>& values) {
  if (values.empty()) return 0.0L;
  return std::accumulate(values.begin(), values.end(), 0.0L) / static_cast<long double>(values.size());
}

std::string format_decimal(long double value) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(6) << static_cast<double>(value);
  return os.str();
}

lob::Quantity sum_depth(const std::vector<std::pair<lob::Tick, lob::Quantity>>& depth, size_t levels) {
  lob::Quantity out = 0;
  for (size_t i = 0; i < depth.size() && i < levels; ++i) out += depth[i].second;
  return out;
}

lob::Tick last_trade_price_at_or_before(const ResearchRunResult& result, int tick) {
  lob::Tick last = 0;
  for (const TradeEvent& trade : result.trades) {
    if (trade.ts <= tick) last = trade.price;
  }
  return last;
}

std::vector<StrategyMetrics> sorted_metrics(const std::map<UserId, StrategyMetrics>& metrics) {
  std::vector<StrategyMetrics> out;
  for (const auto& [_, value] : metrics) out.push_back(value);
  std::sort(out.begin(), out.end(), [](const StrategyMetrics& a, const StrategyMetrics& b) {
    if (a.user != b.user) return a.user < b.user;
    return a.bot_name < b.bot_name;
  });
  return out;
}

} // namespace

EmergenceMetricsCollector::EmergenceMetricsCollector(int warmup_ticks)
    : warmup_ticks_(warmup_ticks) {}

void EmergenceMetricsCollector::record_tick(int tick, const ResearchRunResult& state) {
  const lob::Tick best_bid = state.bids.empty() ? 0 : state.bids.front().first;
  const lob::Tick best_ask = state.asks.empty() ? 0 : state.asks.front().first;
  lob::Tick mid = 0;
  if (best_bid > 0 && best_ask > 0) {
    mid = static_cast<lob::Tick>((best_bid + best_ask) / 2);
  } else {
    mid = last_trade_price_at_or_before(state, tick);
  }

  lob::Quantity trade_volume = 0;
  Amount quote_volume = 0;
  for (const TradeEvent& trade : state.trades) {
    if (trade.ts != tick) continue;
    trade_volume += trade.qty;
    Amount notional = 0;
    if (mul_amount(trade.price, trade.qty, notional)) quote_volume += notional;
  }

  samples_.push_back(MarketTickSample{tick,
                                      mid,
                                      best_bid,
                                      best_ask,
                                      state.bids.empty() ? 0 : state.bids.front().second,
                                      state.asks.empty() ? 0 : state.asks.front().second,
                                      sum_depth(state.bids, 5),
                                      sum_depth(state.asks, 5),
                                      trade_volume,
                                      quote_volume});
  agent_metrics_ = sorted_metrics(state.metrics);
}

EmergenceMetrics EmergenceMetricsCollector::finalize() const {
  return summarize_market_samples(warmup_ticks_, samples_, agent_metrics_);
}

EmergenceMetrics summarize_market_samples(int warmup_ticks,
                                          const std::vector<MarketTickSample>& samples,
                                          const std::vector<StrategyMetrics>& agent_metrics) {
  EmergenceMetrics out{};
  out.ticks = static_cast<int>(samples.size());
  out.warmup_ticks = warmup_ticks;
  out.samples.clear();
  out.agent_metrics = agent_metrics;

  std::vector<long double> spreads;
  std::vector<long double> depth_top1;
  std::vector<long double> depth_top5;
  std::vector<long double> mids;

  for (const MarketTickSample& sample : samples) {
    if (sample.tick <= warmup_ticks) continue;
    out.samples.push_back(sample);
    ++out.measured_ticks;
    if (sample.trade_volume > 0) ++out.trade_count;
    out.total_volume += static_cast<long double>(sample.trade_volume);
    out.total_quote_volume += static_cast<long double>(sample.quote_volume);

    if (sample.best_bid > 0 && sample.best_ask > 0 && sample.best_ask >= sample.best_bid) {
      spreads.push_back(static_cast<long double>(sample.best_ask - sample.best_bid));
    }
    depth_top1.push_back(static_cast<long double>(sample.bid_depth_top1 + sample.ask_depth_top1));
    depth_top5.push_back(static_cast<long double>(sample.bid_depth_top5 + sample.ask_depth_top5));
    if (sample.mid_price > 0) mids.push_back(static_cast<long double>(sample.mid_price));
  }

  out.mean_spread = mean(spreads);
  out.median_spread = median(spreads);
  out.mean_depth_top1 = mean(depth_top1);
  out.mean_depth_top5 = mean(depth_top5);

  std::vector<long double> returns;
  for (size_t i = 1; i < mids.size(); ++i) {
    if (mids[i - 1] == 0.0L) continue;
    returns.push_back((mids[i] - mids[i - 1]) / mids[i - 1]);
  }
  long double return_square_sum = 0.0L;
  for (long double value : returns) return_square_sum += value * value;
  out.realized_volatility = std::sqrt(return_square_sum);

  if (returns.size() >= 2) {
    std::vector<long double> a;
    std::vector<long double> b;
    for (size_t i = 1; i < returns.size(); ++i) {
      a.push_back(returns[i]);
      b.push_back(returns[i - 1]);
    }
    const long double mean_a = mean(a);
    const long double mean_b = mean(b);
    long double cov = 0.0L;
    long double var_a = 0.0L;
    long double var_b = 0.0L;
    for (size_t i = 0; i < a.size(); ++i) {
      cov += (a[i] - mean_a) * (b[i] - mean_b);
      var_a += (a[i] - mean_a) * (a[i] - mean_a);
      var_b += (b[i] - mean_b) * (b[i] - mean_b);
    }
    if (var_a > 0.0L && var_b > 0.0L) out.return_autocorrelation = cov / std::sqrt(var_a * var_b);
  }

  if (!mids.empty()) {
    long double peak = mids.front();
    for (long double mid : mids) {
      if (mid > peak) peak = mid;
      if (peak > 0.0L) out.max_drawdown = std::max(out.max_drawdown, (peak - mid) / peak);
    }
  }

  for (long double value : returns) {
    out.max_price_impact = std::max(out.max_price_impact, std::abs(value));
    out.average_slippage += std::abs(value);
  }
  if (!returns.empty()) out.average_slippage /= static_cast<long double>(returns.size());

  const long double median_depth_top5 = median(depth_top5);
  const long double spread_threshold = out.median_spread * 3.0L;
  for (long double value : depth_top5) {
    if (median_depth_top5 > 0.0L && value < median_depth_top5 * 0.2L) ++out.liquidity_crashes;
  }
  for (long double value : spreads) {
    if (out.median_spread > 0.0L && value > spread_threshold) ++out.spread_spikes;
  }

  std::vector<long double> abs_returns;
  for (long double value : returns) abs_returns.push_back(std::abs(value));
  const long double median_abs_return = median(abs_returns);
  int consecutive = 0;
  for (long double value : abs_returns) {
    if (median_abs_return > 0.0L && value > 2.0L * median_abs_return) {
      ++consecutive;
      if (consecutive == 3) ++out.volatility_clusters;
    } else {
      consecutive = 0;
    }
  }

  return out;
}

std::string export_emergence_summary_json(const EmergenceMetrics& metrics) {
  std::ostringstream os;
  os << '{'
     << "\"ticks\":" << metrics.ticks << ','
     << "\"warmup_ticks\":" << metrics.warmup_ticks << ','
     << "\"measured_ticks\":" << metrics.measured_ticks << ','
     << "\"trade_count\":" << metrics.trade_count << ','
     << "\"total_volume\":" << format_decimal(metrics.total_volume) << ','
     << "\"total_quote_volume\":" << format_decimal(metrics.total_quote_volume) << ','
     << "\"mean_spread\":" << format_decimal(metrics.mean_spread) << ','
     << "\"median_spread\":" << format_decimal(metrics.median_spread) << ','
     << "\"mean_depth_top1\":" << format_decimal(metrics.mean_depth_top1) << ','
     << "\"mean_depth_top5\":" << format_decimal(metrics.mean_depth_top5) << ','
     << "\"realized_volatility\":" << format_decimal(metrics.realized_volatility) << ','
     << "\"return_autocorrelation\":" << format_decimal(metrics.return_autocorrelation) << ','
     << "\"max_drawdown\":" << format_decimal(metrics.max_drawdown) << ','
     << "\"max_price_impact\":" << format_decimal(metrics.max_price_impact) << ','
     << "\"average_slippage\":" << format_decimal(metrics.average_slippage) << ','
     << "\"liquidity_crashes\":" << metrics.liquidity_crashes << ','
     << "\"spread_spikes\":" << metrics.spread_spikes << ','
     << "\"volatility_clusters\":" << metrics.volatility_clusters
     << '}';
  return os.str();
}

std::string export_price_series_csv(const EmergenceMetrics& metrics) {
  std::ostringstream os;
  os << "tick,mid_price,last_trade_volume,quote_volume\n";
  for (const MarketTickSample& sample : metrics.samples) {
    os << sample.tick << ',' << sample.mid_price << ',' << sample.trade_volume << ',' << sample.quote_volume << '\n';
  }
  return os.str();
}

std::string export_spread_series_csv(const EmergenceMetrics& metrics) {
  std::ostringstream os;
  os << "tick,best_bid,best_ask,spread\n";
  for (const MarketTickSample& sample : metrics.samples) {
    const lob::Tick spread = (sample.best_bid > 0 && sample.best_ask > 0 && sample.best_ask >= sample.best_bid)
                                 ? sample.best_ask - sample.best_bid
                                 : 0;
    os << sample.tick << ',' << sample.best_bid << ',' << sample.best_ask << ',' << spread << '\n';
  }
  return os.str();
}

std::string export_depth_series_csv(const EmergenceMetrics& metrics) {
  std::ostringstream os;
  os << "tick,bid_depth_top1,ask_depth_top1,bid_depth_top5,ask_depth_top5\n";
  for (const MarketTickSample& sample : metrics.samples) {
    os << sample.tick << ','
       << sample.bid_depth_top1 << ','
       << sample.ask_depth_top1 << ','
       << sample.bid_depth_top5 << ','
       << sample.ask_depth_top5 << '\n';
  }
  return os.str();
}

std::string export_agent_metrics_csv(const EmergenceMetrics& metrics) {
  std::ostringstream os;
  os << "user,bot_name,net_pnl,gross_pnl,fees_paid,inventory,fills,accepted_orders,rejected_orders\n";
  for (const StrategyMetrics& row : metrics.agent_metrics) {
    os << row.user << ','
       << row.bot_name << ','
       << format_decimal(row.net_pnl) << ','
       << format_decimal(row.gross_pnl) << ','
       << row.fees_paid << ','
       << row.inventory << ','
       << row.fills << ','
       << row.accepted_orders << ','
       << row.rejected_orders << '\n';
  }
  return os.str();
}

} // namespace lobx::sim
