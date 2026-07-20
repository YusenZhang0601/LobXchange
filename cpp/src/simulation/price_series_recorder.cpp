#include "lobx/simulation/price_series_recorder.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <filesystem>
#include <sstream>

namespace lobx::simulation {

namespace {

bool valid_price(double value) {
  return std::isfinite(value) && value > 0.0;
}

std::string escape_json(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char ch : s) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += ch; break;
    }
  }
  return out;
}

double stddev(const std::vector<double>& values) {
  if (values.size() < 2) return 0.0;
  const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
  double sum_sq = 0.0;
  for (double value : values) {
    const double d = value - mean;
    sum_sq += d * d;
  }
  return std::sqrt(sum_sq / static_cast<double>(values.size() - 1));
}

} // namespace

void PriceSeriesRecorder::record(int step, const AgentRuntime& runtime) {
  const MarketView& view = runtime.market_view();
  const AgentRuntimeSummary& summary = runtime.summary();
  const double spread = view.best_bid > 0.0 && view.best_ask > 0.0 && view.best_ask >= view.best_bid
                            ? view.best_ask - view.best_bid
                            : 0.0;
  const double last_trade_price =
      view.recent_trades.empty() ? std::numeric_limits<double>::quiet_NaN() : view.recent_trades.back().price;
  samples_.push_back(PriceSeriesSample{step,
                                       view.exchange_ts,
                                       view.best_bid,
                                       view.best_ask,
                                       view.mid_price,
                                       spread,
                                       view.spread_bps,
                                       last_trade_price,
                                       static_cast<double>(summary.cum_volume),
                                       summary.trade_count});
}

PriceImpactSummary PriceSeriesRecorder::summarize(const std::string& scenario,
                                                  std::uint64_t seed,
                                                  int agent_count,
                                                  int steps) const {
  PriceImpactSummary out{};
  out.scenario = scenario;
  out.seed = seed;
  out.agent_count = agent_count;
  out.steps = steps;
  out.price_samples_count = static_cast<int>(samples_.size());
  if (samples_.empty()) return out;
  out.trade_count = samples_.back().trade_count;
  out.cum_volume = samples_.back().cum_volume;

  std::vector<double> mids;
  mids.reserve(samples_.size());
  for (const PriceSeriesSample& sample : samples_) {
    if (valid_price(sample.mid_price)) mids.push_back(sample.mid_price);
  }
  if (mids.empty()) return out;
  out.initial_mid_price = mids.front();
  out.final_mid_price = mids.back();
  if (valid_price(out.initial_mid_price)) {
    out.total_return_bps = (out.final_mid_price / out.initial_mid_price - 1.0) * 10000.0;
  }

  std::vector<double> returns;
  returns.reserve(mids.size() > 0 ? mids.size() - 1 : 0);
  for (size_t i = 1; i < mids.size(); ++i) {
    if (!valid_price(mids[i - 1]) || !valid_price(mids[i])) continue;
    const double ret = (mids[i] / mids[i - 1] - 1.0) * 10000.0;
    if (!std::isfinite(ret)) continue;
    returns.push_back(ret);
    out.largest_abs_return_bps = std::max(out.largest_abs_return_bps, std::abs(ret));
  }
  out.realized_vol_bps = stddev(returns);

  double peak = mids.front();
  double min_drawdown = 0.0;
  for (double mid : mids) {
    peak = std::max(peak, mid);
    if (valid_price(peak)) {
      min_drawdown = std::min(min_drawdown, (mid / peak - 1.0) * 10000.0);
    }
  }
  out.max_drawdown_bps = std::abs(min_drawdown);
  return out;
}

std::string price_series_csv(const std::vector<PriceSeriesSample>& samples) {
  std::ostringstream os;
  os << std::setprecision(12);
  os << "step,ts,best_bid,best_ask,mid_price,spread,spread_bps,last_trade_price,cum_volume,trade_count\n";
  for (const PriceSeriesSample& s : samples) {
    os << s.step << ','
       << s.ts << ','
       << s.best_bid << ','
       << s.best_ask << ','
       << s.mid_price << ','
       << s.spread << ','
       << s.spread_bps << ','
       << s.last_trade_price << ','
       << s.cum_volume << ','
       << s.trade_count << '\n';
  }
  return os.str();
}

std::string price_impact_summary_json(const PriceImpactSummary& summary) {
  std::ostringstream os;
  os << std::setprecision(12);
  os << "{"
     << "\"scenario\":\"" << escape_json(summary.scenario) << "\","
     << "\"seed\":" << summary.seed << ','
     << "\"agent_count\":" << summary.agent_count << ','
     << "\"steps\":" << summary.steps << ','
     << "\"initial_mid_price\":" << summary.initial_mid_price << ','
     << "\"final_mid_price\":" << summary.final_mid_price << ','
     << "\"total_return_bps\":" << summary.total_return_bps << ','
     << "\"realized_vol_bps\":" << summary.realized_vol_bps << ','
     << "\"max_drawdown_bps\":" << summary.max_drawdown_bps << ','
     << "\"largest_abs_return_bps\":" << summary.largest_abs_return_bps << ','
     << "\"trade_count\":" << summary.trade_count << ','
     << "\"cum_volume\":" << summary.cum_volume << ','
     << "\"price_samples_count\":" << summary.price_samples_count
     << "}";
  return os.str();
}

bool PriceSeriesRecorder::write_price_series_csv(const std::string& path) const {
  std::ofstream out(path);
  if (!out.is_open()) return false;
  out << price_series_csv(samples_);
  return out.good();
}

bool PriceSeriesRecorder::write_summary_json(const std::string& path, const PriceImpactSummary& summary) const {
  std::ofstream out(path);
  if (!out.is_open()) return false;
  out << price_impact_summary_json(summary) << '\n';
  return out.good();
}

std::string accounting_summary_json(const AccountingSummary& summary) {
  std::ostringstream os;
  os << std::setprecision(12);
  os << "{"
     << "\"sum_initial_equity\":" << summary.sum_initial_equity << ','
     << "\"sum_final_equity\":" << summary.sum_final_equity << ','
     << "\"sum_agent_pnl\":" << summary.sum_agent_pnl << ','
     << "\"exchange_fee_revenue\":" << summary.exchange_fee_revenue << ','
     << "\"house_account_pnl\":" << summary.house_account_pnl << ','
     << "\"insurance_fund_pnl\":" << summary.insurance_fund_pnl << ','
     << "\"system_pnl_residual\":" << summary.system_pnl_residual << ','
     << "\"negative_pnl_agent_count\":" << summary.negative_pnl_agent_count << ','
     << "\"positive_pnl_agent_count\":" << summary.positive_pnl_agent_count << ','
     << "\"zero_pnl_agent_count\":" << summary.zero_pnl_agent_count << ','
     << "\"agent_count\":" << summary.agent_count << ','
     << "\"sum_cash_free\":" << summary.sum_cash_free << ','
     << "\"sum_cash_locked\":" << summary.sum_cash_locked << ','
     << "\"sum_cash_total\":" << summary.sum_cash_total << ','
     << "\"sum_inventory_free\":" << summary.sum_inventory_free << ','
     << "\"sum_inventory_locked\":" << summary.sum_inventory_locked << ','
     << "\"sum_inventory_total\":" << summary.sum_inventory_total << ','
     << "\"mark_price\":" << summary.mark_price << ','
     << "\"mark_price_policy\":\"" << escape_json(summary.mark_price_policy) << "\""
     << "}";
  return os.str();
}

bool PriceSeriesRecorder::write_summary_json(const std::string& path,
                                             const PriceImpactSummary& summary,
                                             const AccountingSummary& accounting) const {
  if (!write_summary_json(path, summary)) return false;
  const std::filesystem::path summary_path(path);
  const std::filesystem::path accounting_path = summary_path.parent_path() / "accounting_summary.json";
  std::ofstream out(accounting_path);
  if (!out.is_open()) return false;
  out << accounting_summary_json(accounting) << '\n';
  return out.good();
}

std::string scenario_comparison_csv(const std::vector<PriceImpactSummary>& summaries) {
  std::ostringstream os;
  os << std::setprecision(12);
  os << "scenario,seed,agent_count,steps,initial_mid_price,final_mid_price,total_return_bps,realized_vol_bps,max_drawdown_bps,largest_abs_return_bps,trade_count,cum_volume,price_samples_count\n";
  for (const PriceImpactSummary& s : summaries) {
    os << s.scenario << ','
       << s.seed << ','
       << s.agent_count << ','
       << s.steps << ','
       << s.initial_mid_price << ','
       << s.final_mid_price << ','
       << s.total_return_bps << ','
       << s.realized_vol_bps << ','
       << s.max_drawdown_bps << ','
       << s.largest_abs_return_bps << ','
       << s.trade_count << ','
       << s.cum_volume << ','
       << s.price_samples_count << '\n';
  }
  return os.str();
}

bool write_scenario_comparison_csv(const std::string& path, const std::vector<PriceImpactSummary>& summaries) {
  std::ofstream out(path);
  if (!out.is_open()) return false;
  out << scenario_comparison_csv(summaries);
  return out.good();
}

} // namespace lobx::simulation
