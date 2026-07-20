#include "lobx/simulation/diagnostic_recorder.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_map>

#include "lobx/agents/agent_factory.hpp"

namespace lobx::simulation {

namespace {

std::string json_escape(const std::string& s) {
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

std::string bool_json(bool value) {
  return value ? "true" : "false";
}

double median(std::vector<double> values) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const std::size_t mid = values.size() / 2;
  if ((values.size() % 2) != 0) return values[mid];
  return (values[mid - 1] + values[mid]) / 2.0;
}

std::int64_t elapsed_ms_since(std::chrono::steady_clock::time_point started) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
}

double elapsed_ms_between(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
  return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
}

std::uint64_t fnv1a_file_hash(const std::filesystem::path& path) {
  constexpr std::uint64_t kOffset = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::ifstream in(path, std::ios::binary);
  std::uint64_t hash = kOffset;
  char ch = 0;
  while (in.get(ch)) {
    hash ^= static_cast<unsigned char>(ch);
    hash *= kPrime;
  }
  return hash;
}

std::string hex_hash(std::uint64_t value) {
  std::ostringstream os;
  os << std::hex << std::setw(16) << std::setfill('0') << value;
  return os.str();
}

void write_action_counts(std::ostream& os, const AgentRuntimeActionCounts& c) {
  os << c.total_actions << ','
     << c.submit_limit_order << ','
     << c.submit_market_order << ','
     << c.cancel_order << ','
     << c.replace_order << ','
     << c.cancel_all_orders << ','
     << c.sleep_until;
}

} // namespace

DiagnosticRecorder::DiagnosticRecorder(DiagnosticScenario scenario,
                                       DiagnosticOutputOptions options,
                                       std::filesystem::path output_dir)
    : scenario_(std::move(scenario)), options_(options), output_dir_(std::move(output_dir)) {}

DiagnosticRecorder::~DiagnosticRecorder() = default;

bool DiagnosticRecorder::open() {
  std::filesystem::create_directories(output_dir_);
  started_ = std::chrono::steady_clock::now();

  price_series_.open(output_dir_ / "price_series.csv");
  book_samples_.open(output_dir_ / "book_samples.csv");
  agent_state_samples_.open(output_dir_ / "agent_state_samples.csv");
  actions_by_step_.open(output_dir_ / "actions_by_step.csv");
  actions_by_type_.open(output_dir_ / "actions_by_type.csv");
  trades_by_step_.open(output_dir_ / "trades_by_step.csv");
  runtime_metrics_.open(output_dir_ / "runtime_metrics.csv");
  open_order_growth_.open(output_dir_ / "open_order_growth.csv");
  agent_type_pnl_timeseries_.open(output_dir_ / "agent_type_pnl_timeseries.csv");
  if (options_.jsonl_events) {
    agent_actions_jsonl_.open(output_dir_ / "agent_actions.jsonl");
    simulation_events_jsonl_.open(output_dir_ / "simulation_events.jsonl");
  }
  return is_open();
}

bool DiagnosticRecorder::is_open() const {
  return price_series_.is_open() && book_samples_.is_open() && agent_state_samples_.is_open() &&
         actions_by_step_.is_open() && actions_by_type_.is_open() && trades_by_step_.is_open() &&
         runtime_metrics_.is_open() && open_order_growth_.is_open() && agent_type_pnl_timeseries_.is_open();
}

void DiagnosticRecorder::write_metadata() {
  std::ofstream out(output_dir_ / "run_metadata.json");
  out << "{"
      << "\"scenario\":\"" << json_escape(scenario_.name) << "\","
      << "\"seed\":" << scenario_.seed << ','
      << "\"agent_count\":" << scenario_.agent_count << ','
      << "\"steps\":" << scenario_.steps << ','
      << "\"sample_interval\":" << scenario_.sample_interval << ','
      << "\"book_depth\":" << scenario_.book_depth << ','
      << "\"created_at_unix_ms\":0,"
      << "\"binary\":\"lobx_agent_long_diagnostic_tests\","
      << "\"git_commit\":\"unknown\","
      << "\"build_type\":\"unknown\","
      << "\"compiler\":\"unknown\","
      << "\"config\":{"
      << "\"static_market_maker_ratio\":" << scenario_.static_market_maker_ratio << ','
      << "\"noise_trader_ratio\":" << scenario_.noise_trader_ratio << ','
      << "\"momentum_follower_ratio\":" << scenario_.momentum_follower_ratio << ','
      << "\"mean_reverter_ratio\":" << scenario_.mean_reverter_ratio << ','
      << "\"whale_sweeper_ratio\":" << scenario_.whale_sweeper_ratio
      << ",\"bounded_quotes\":" << bool_json(scenario_.bounded_quotes)
      << ",\"max_open_orders_per_side\":" << scenario_.max_open_orders_per_side
      << ",\"quote_refresh_interval_steps\":" << scenario_.quote_refresh_interval_steps
      << ",\"quote_ttl_steps\":" << scenario_.quote_ttl_steps
      << ",\"cancel_stale_quotes\":" << bool_json(scenario_.cancel_stale_quotes)
      << ",\"enable_scheduler\":" << bool_json(scenario_.enable_scheduler)
      << "},"
      << "\"enabled_outputs\":{"
      << "\"jsonl_events\":" << bool_json(options_.jsonl_events) << ','
      << "\"agent_snapshots\":" << bool_json(options_.agent_snapshots) << ','
      << "\"order_trace\":" << bool_json(options_.order_trace) << ','
      << "\"impact_windows\":" << bool_json(options_.impact_windows)
      << "}"
      << "}\n";
}

void DiagnosticRecorder::write_headers() {
  price_series_ << "step,ts,best_bid,best_ask,mid_price,spread,spread_bps,last_trade_price,cum_volume,trade_count,step_trade_count,step_volume,step_return_bps,drawdown_bps\n";
  book_samples_ << "step,ts,side,level,price,quantity\n";
  agent_state_samples_ << "step,ts,agent_id,agent_type,cash_free,cash_locked,cash_total,inventory_free,inventory_locked,inventory_total,equity,unrealized_pnl,total_pnl,open_order_count,trade_count,buy_volume,sell_volume,fees_paid\n";
  actions_by_step_ << "step,ts,total_actions,submit_limit_order,submit_market_order,cancel_order,replace_order,cancel_all_orders,sleep_until\n";
  actions_by_type_ << "step,ts,agent_type,total_actions,submit_limit_order,submit_market_order,cancel_order,replace_order,cancel_all_orders,sleep_until\n";
  trades_by_step_ << "step,ts,trade_count,volume,buy_aggressor_volume,sell_aggressor_volume,notional,vwap,min_price,max_price,last_trade_price\n";
  runtime_metrics_ << "step,ts,elapsed_ms,agent_decide_ms,action_schedule_ms,exchange_apply_ms,state_update_ms,recorder_ms,book_sample_ms,total_actions,total_orders,total_trades,total_cancels,total_rejections,open_order_count,agents_due_count,agents_skipped_count,agent_decisions_count,price_samples_written,agent_samples_written,jsonl_events_written\n";
  open_order_growth_ << "step,ts,total_open_orders,open_bid_orders,open_ask_orders,mean_open_orders_per_agent,max_open_orders_per_agent,stale_order_count\n";
  agent_type_pnl_timeseries_ << "step,ts,agent_type,agent_count,mean_pnl,median_pnl,sum_pnl,min_pnl,max_pnl,mean_equity,mean_inventory,mean_open_order_count\n";
}

double DiagnosticRecorder::current_mid(const AgentRuntime& runtime) const {
  const double mid = runtime.market_view().mid_price;
  return std::isfinite(mid) && mid > 0.0 ? mid : 100.0;
}

double DiagnosticRecorder::step_return_bps(double mid) {
  if (previous_mid_ <= 0.0) {
    previous_mid_ = mid;
    return 0.0;
  }
  const double out = (mid / previous_mid_ - 1.0) * 10000.0;
  previous_mid_ = mid;
  return std::isfinite(out) ? out : 0.0;
}

double DiagnosticRecorder::drawdown_bps(double mid) {
  if (running_peak_ <= 0.0) running_peak_ = mid;
  running_peak_ = std::max(running_peak_, mid);
  if (running_peak_ <= 0.0) return 0.0;
  const double out = (mid / running_peak_ - 1.0) * 10000.0;
  return std::isfinite(out) ? out : 0.0;
}

void DiagnosticRecorder::record_step(const AgentRuntime& runtime, int step) {
  accumulate_runtime_stats(runtime);
  write_action_samples(runtime, step);
  write_trade_sample(runtime, step);
  if (options_.jsonl_events) write_jsonl_events(runtime);

  const int interval = std::max(1, scenario_.sample_interval);
  if ((step % interval) == 0 || step == scenario_.steps) {
    const auto recorder_start = std::chrono::steady_clock::now();
    write_price_sample(runtime, step);
    write_book_sample(runtime, step);
    if (options_.agent_snapshots) write_agent_samples(runtime, step);
    write_open_order_growth(runtime, step);
    write_agent_type_pnl_timeseries(runtime, step);
    const auto recorder_end = std::chrono::steady_clock::now();
    interval_recorder_ms_ += elapsed_ms_between(recorder_start, recorder_end);
    total_recorder_ms_ += elapsed_ms_between(recorder_start, recorder_end);
    write_runtime_metrics(runtime, step);
    reset_interval_stats();
  }
}

void DiagnosticRecorder::write_price_sample(const AgentRuntime& runtime, int step) {
  const MarketView& view = runtime.market_view();
  const AgentRuntimeSummary& summary = runtime.summary();
  const AgentRuntimeStepStats& step_stats = runtime.last_step_stats();
  const double mid = current_mid(runtime);
  const double spread = view.best_bid > 0.0 && view.best_ask > 0.0 && view.best_ask >= view.best_bid
                            ? view.best_ask - view.best_bid
                            : 0.0;
  const double last_trade_price = view.recent_trades.empty() ? 0.0 : view.recent_trades.back().price;
  price_series_ << std::setprecision(12)
                << step << ',' << view.exchange_ts << ','
                << view.best_bid << ',' << view.best_ask << ','
                << mid << ',' << spread << ',' << view.spread_bps << ','
                << last_trade_price << ','
                << static_cast<double>(summary.cum_volume) << ','
                << summary.trade_count << ','
                << step_stats.trades.trade_count << ','
                << step_stats.trades.volume << ','
                << step_return_bps(mid) << ','
                << drawdown_bps(mid) << '\n';
  ++price_samples_written_;
}

void DiagnosticRecorder::write_book_sample(const AgentRuntime& runtime, int step) {
  const MarketView& view = runtime.market_view();
  int level = 1;
  for (const BookLevelView& bid : view.bids) {
    if (level > scenario_.book_depth) break;
    book_samples_ << step << ',' << view.exchange_ts << ",bid," << level++ << ',' << bid.price << ',' << bid.quantity << '\n';
  }
  level = 1;
  for (const BookLevelView& ask : view.asks) {
    if (level > scenario_.book_depth) break;
    book_samples_ << step << ',' << view.exchange_ts << ",ask," << level++ << ',' << ask.price << ',' << ask.quantity << '\n';
  }
}

void DiagnosticRecorder::write_agent_samples(const AgentRuntime& runtime, int step) {
  const double mark = current_mid(runtime);
  for (const AgentEquitySnapshot& s : runtime.agent_equity_snapshots(mark)) {
    agent_state_samples_ << std::setprecision(12)
                         << step << ',' << runtime.market_view().exchange_ts << ','
                         << s.agent_id << ',' << lobx::agents::agent_type_name(s.agent_type) << ','
                         << s.free_cash << ',' << s.locked_cash << ',' << s.total_cash << ','
                         << s.free_inventory << ',' << s.locked_inventory << ',' << s.total_inventory << ','
                         << s.final_equity << ',' << 0.0 << ',' << s.pnl << ','
                         << s.open_order_count << ',' << s.trade_count << ','
                         << s.buy_volume << ',' << s.sell_volume << ',' << s.fees_paid << '\n';
    ++agent_samples_written_;
  }
}

void DiagnosticRecorder::write_action_samples(const AgentRuntime& runtime, int step) {
  const AgentRuntimeStepStats& stats = runtime.last_step_stats();
  actions_by_step_ << step << ',' << stats.ts << ',';
  write_action_counts(actions_by_step_, stats.actions);
  actions_by_step_ << '\n';

  for (const auto& [type, counts] : stats.actions_by_type) {
    actions_by_type_ << step << ',' << stats.ts << ',' << lobx::agents::agent_type_name(type) << ',';
    write_action_counts(actions_by_type_, counts);
    actions_by_type_ << '\n';
  }
}

void DiagnosticRecorder::write_trade_sample(const AgentRuntime& runtime, int step) {
  const AgentRuntimeStepStats& stats = runtime.last_step_stats();
  const AgentRuntimeTradeStepStats& t = stats.trades;
  trades_by_step_ << std::setprecision(12)
                  << step << ',' << stats.ts << ','
                  << t.trade_count << ',' << t.volume << ','
                  << t.buy_aggressor_volume << ',' << t.sell_aggressor_volume << ','
                  << t.notional << ',' << t.vwap << ','
                  << t.min_price << ',' << t.max_price << ',' << t.last_trade_price << '\n';
}

void DiagnosticRecorder::write_runtime_metrics(const AgentRuntime& runtime, int step) {
  const AgentRuntimeSummary& summary = runtime.summary();
  const AgentRuntimeStepStats& stats = runtime.last_step_stats();
  runtime_metrics_ << step << ',' << stats.ts << ','
                   << elapsed_ms_since(started_) << ','
                   << interval_agent_decide_ms_ << ','
                   << interval_action_schedule_ms_ << ','
                   << interval_exchange_apply_ms_ << ','
                   << interval_state_update_ms_ << ','
                   << interval_recorder_ms_ << ','
                   << interval_book_sample_ms_ << ','
                   << summary.action_count << ','
                   << summary.accepted_orders << ','
                   << summary.trade_count << ','
                   << summary.canceled_orders << ','
                   << summary.rejected_orders << ','
                   << runtime.open_order_count() << ','
                   << interval_agents_due_ << ','
                   << interval_agents_skipped_ << ','
                   << interval_agent_decisions_ << ','
                   << price_samples_written_ << ','
                   << agent_samples_written_ << ','
                   << jsonl_events_written_ << '\n';
}

void DiagnosticRecorder::write_open_order_growth(const AgentRuntime& runtime, int step) {
  const AgentRuntimeOpenOrderSummary summary = runtime.open_order_summary(scenario_.quote_ttl_steps);
  max_open_orders_ = std::max(max_open_orders_, summary.total_open_orders);
  max_open_orders_per_agent_ = std::max(max_open_orders_per_agent_, summary.max_open_orders_per_agent);
  open_order_growth_ << std::setprecision(12)
                     << step << ',' << runtime.market_view().exchange_ts << ','
                     << summary.total_open_orders << ','
                     << summary.open_bid_orders << ','
                     << summary.open_ask_orders << ','
                     << summary.mean_open_orders_per_agent << ','
                     << summary.max_open_orders_per_agent << ','
                     << summary.stale_order_count << '\n';
}

void DiagnosticRecorder::write_agent_type_pnl_timeseries(const AgentRuntime& runtime, int step) {
  const double mark = current_mid(runtime);
  std::unordered_map<lobx::agents::AgentType, TypeAggregate> by_type;
  for (const AgentEquitySnapshot& s : runtime.agent_equity_snapshots(mark)) {
    TypeAggregate& agg = by_type[s.agent_type];
    ++agg.count;
    agg.pnl.push_back(s.pnl);
    agg.sum_pnl += s.pnl;
    agg.sum_equity += s.final_equity;
    agg.sum_inventory += s.total_inventory;
    agg.sum_open_order_count += s.open_order_count;
  }

  for (auto& [type, agg] : by_type) {
    const double count = static_cast<double>(std::max(1, agg.count));
    const auto [min_it, max_it] = std::minmax_element(agg.pnl.begin(), agg.pnl.end());
    agent_type_pnl_timeseries_ << std::setprecision(12)
                               << step << ',' << runtime.market_view().exchange_ts << ','
                               << lobx::agents::agent_type_name(type) << ','
                               << agg.count << ','
                               << agg.sum_pnl / count << ','
                               << median(agg.pnl) << ','
                               << agg.sum_pnl << ','
                               << (min_it == agg.pnl.end() ? 0.0 : *min_it) << ','
                               << (max_it == agg.pnl.end() ? 0.0 : *max_it) << ','
                               << agg.sum_equity / count << ','
                               << agg.sum_inventory / count << ','
                               << agg.sum_open_order_count / count << '\n';
  }
}

void DiagnosticRecorder::write_jsonl_events(const AgentRuntime& runtime) {
  const auto& actions = runtime.action_trace();
  for (; last_action_trace_index_ < actions.size(); ++last_action_trace_index_) {
    const auto& action = actions[last_action_trace_index_];
    agent_actions_jsonl_ << "{\"step\":" << runtime.summary().steps
                         << ",\"ts\":" << action.decision_ts
                         << ",\"agent_id\":" << action.agent_id
                         << ",\"agent_type\":\"" << lobx::agents::agent_type_name(action.agent_type) << "\""
                         << ",\"group_id\":" << action.group_id
                         << ",\"client_action_id\":" << action.client_action_id
                         << ",\"action_type\":\"" << lobx::agents::action_payload_name(action.payload) << "\""
                         << ",\"decision_ts\":" << action.decision_ts
                         << ",\"arrival_ts\":" << action.decision_ts
                         << ",\"reason_tag\":\"" << json_escape(action.reason_tag) << "\"}\n";
    ++jsonl_events_written_;
  }
  const auto& events = runtime.events();
  for (; last_event_trace_index_ < events.size(); ++last_event_trace_index_) {
    simulation_events_jsonl_ << "{\"step\":" << runtime.summary().steps
                             << ",\"ts\":" << events[last_event_trace_index_].ts
                             << ",\"seq\":" << events[last_event_trace_index_].seq
                             << ",\"event_type\":\"" << simulation_event_payload_name(events[last_event_trace_index_].payload)
                             << "\"}\n";
    ++jsonl_events_written_;
  }
}

void DiagnosticRecorder::finalize(const AgentRuntime& runtime, const PriceImpactSummary& summary) {
  price_series_.flush();
  book_samples_.flush();
  agent_state_samples_.flush();
  actions_by_step_.flush();
  actions_by_type_.flush();
  trades_by_step_.flush();
  runtime_metrics_.flush();
  open_order_growth_.flush();
  agent_type_pnl_timeseries_.flush();
  write_summary(summary);
  write_accounting_summary(runtime, summary.final_mid_price > 0.0 ? summary.final_mid_price : current_mid(runtime));
  write_final_agent_state(runtime, summary.final_mid_price > 0.0 ? summary.final_mid_price : current_mid(runtime));
  write_agent_type_summary(runtime, summary.final_mid_price > 0.0 ? summary.final_mid_price : current_mid(runtime));
  write_inventory_consistency(runtime, summary.final_mid_price > 0.0 ? summary.final_mid_price : current_mid(runtime));
  write_perf_summary(runtime);
  write_unit_sanity_summary(runtime);
  write_empty_optional_files();
  write_warnings();
  write_run_hash(runtime);
}

void DiagnosticRecorder::write_summary(const PriceImpactSummary& summary) {
  std::ofstream out(output_dir_ / "summary.json");
  out << price_impact_summary_json(summary) << '\n';
}

void DiagnosticRecorder::write_inventory_consistency(const AgentRuntime& runtime, double mark_price) {
  (void)mark_price;
  std::ofstream by_agent(output_dir_ / "inventory_consistency_by_agent.csv");
  by_agent << "agent_id,agent_type,initial_inventory,buy_volume,sell_volume,expected_final_inventory,final_inventory_total,inventory_residual\n";

  double max_abs = 0.0;
  double sum_abs = 0.0;
  int count = 0;
  int failed = 0;
  constexpr double kTolerance = 1e-9;
  for (const AgentEquitySnapshot& s : runtime.agent_equity_snapshots(current_mid(runtime))) {
    const double expected = s.initial_inventory + s.buy_volume - s.sell_volume;
    const double residual = s.total_inventory - expected;
    const double abs_residual = std::abs(residual);
    max_abs = std::max(max_abs, abs_residual);
    sum_abs += abs_residual;
    if (abs_residual > kTolerance) ++failed;
    ++count;
    by_agent << std::setprecision(12)
             << s.agent_id << ',' << lobx::agents::agent_type_name(s.agent_type) << ','
             << s.initial_inventory << ',' << s.buy_volume << ',' << s.sell_volume << ','
             << expected << ',' << s.total_inventory << ',' << residual << '\n';
  }

  std::ofstream summary(output_dir_ / "inventory_consistency_summary.json");
  summary << "{"
          << "\"max_abs_inventory_residual\":" << max_abs << ','
          << "\"mean_abs_inventory_residual\":" << (count == 0 ? 0.0 : sum_abs / static_cast<double>(count)) << ','
          << "\"agent_count\":" << count << ','
          << "\"failed_agent_count\":" << failed << ','
          << "\"available\":true"
          << "}\n";
}

void DiagnosticRecorder::write_perf_summary(const AgentRuntime& runtime) {
  const AgentRuntimeSummary& summary = runtime.summary();
  const double total_elapsed_ms = static_cast<double>(elapsed_ms_since(started_));
  const double elapsed_sec = total_elapsed_ms > 0.0 ? total_elapsed_ms / 1000.0 : 0.0;
  std::ofstream out(output_dir_ / "perf_summary.json");
  out << std::setprecision(12)
      << "{"
      << "\"total_elapsed_ms\":" << total_elapsed_ms << ','
      << "\"agent_decide_ms\":" << total_agent_decide_ms_ << ','
      << "\"action_schedule_ms\":" << total_action_schedule_ms_ << ','
      << "\"exchange_apply_ms\":" << total_exchange_apply_ms_ << ','
      << "\"state_update_ms\":" << total_state_update_ms_ << ','
      << "\"recorder_ms\":" << total_recorder_ms_ << ','
      << "\"book_sample_ms\":" << total_book_sample_ms_ << ','
      << "\"actions_per_sec\":" << (elapsed_sec > 0.0 ? static_cast<double>(summary.action_count) / elapsed_sec : 0.0) << ','
      << "\"orders_per_sec\":" << (elapsed_sec > 0.0 ? static_cast<double>(summary.accepted_orders + summary.rejected_orders) / elapsed_sec : 0.0) << ','
      << "\"trades_per_sec\":" << (elapsed_sec > 0.0 ? static_cast<double>(summary.trade_count) / elapsed_sec : 0.0) << ','
      << "\"max_open_orders\":" << max_open_orders_ << ','
      << "\"final_open_orders\":" << runtime.open_order_count() << ','
      << "\"max_open_orders_per_agent\":" << max_open_orders_per_agent_ << ','
      << "\"agent_count\":" << summary.agent_count << ','
      << "\"steps\":" << summary.steps
      << "}\n";
}

void DiagnosticRecorder::write_run_hash(const AgentRuntime& runtime) {
  price_series_.flush();
  agent_state_samples_.flush();
  runtime_metrics_.flush();
  open_order_growth_.flush();
  agent_type_pnl_timeseries_.flush();

  std::ofstream out(output_dir_ / "run_hash.json");
  out << "{"
      << "\"price_series_hash\":\"" << hex_hash(fnv1a_file_hash(output_dir_ / "price_series.csv")) << "\","
      << "\"agent_final_state_hash\":\"" << hex_hash(fnv1a_file_hash(output_dir_ / "agent_final_state.csv")) << "\","
      << "\"accounting_summary_hash\":\"" << hex_hash(fnv1a_file_hash(output_dir_ / "accounting_summary.json")) << "\","
      << "\"inventory_consistency_hash\":\"" << hex_hash(fnv1a_file_hash(output_dir_ / "inventory_consistency_by_agent.csv")) << "\","
      << "\"event_count\":" << runtime.summary().event_count << ','
      << "\"hash_excludes\":[\"created_at_unix_ms\",\"elapsed_ms\",\"output_dir\",\"wall_clock_time\"]"
      << "}\n";
}

void DiagnosticRecorder::write_unit_sanity_summary(const AgentRuntime& runtime) {
  std::vector<double> abs_inventory;
  std::vector<double> abs_cash;
  double max_abs_inventory = 0.0;
  double max_abs_cash = 0.0;
  for (const AgentEquitySnapshot& s : runtime.agent_equity_snapshots(current_mid(runtime))) {
    const double inv = std::abs(s.total_inventory);
    const double cash = std::abs(s.total_cash);
    abs_inventory.push_back(inv);
    abs_cash.push_back(cash);
    max_abs_inventory = std::max(max_abs_inventory, inv);
    max_abs_cash = std::max(max_abs_cash, cash);
  }
  const double median_abs_inventory = median(abs_inventory);
  const bool suspected_scale = median_abs_inventory > 100000.0;
  if (suspected_scale) {
    warnings_.push_back("UNIT_SCALE_SUSPECTED: Median absolute inventory is large; verify quantity units and fixed-point scaling.");
  }

  std::ofstream out(output_dir_ / "unit_sanity_summary.json");
  out << std::setprecision(12)
      << "{"
      << "\"max_abs_inventory\":" << max_abs_inventory << ','
      << "\"median_abs_inventory\":" << median_abs_inventory << ','
      << "\"max_abs_cash\":" << max_abs_cash << ','
      << "\"median_order_quantity\":0.0,"
      << "\"median_trade_quantity\":0.0,"
      << "\"max_order_quantity\":0.0,"
      << "\"max_trade_quantity\":0.0,"
      << "\"suspected_fixed_point_quantity_scale\":" << bool_json(suspected_scale) << ','
      << "\"notes\":[";
  if (suspected_scale) out << "\"Large inventory values may reflect fixed-point/base-unit accounting.\"";
  out << "]}\n";
}

void DiagnosticRecorder::write_accounting_summary(const AgentRuntime& runtime, double mark_price) {
  std::ofstream out(output_dir_ / "accounting_summary.json");
  out << accounting_summary_json(runtime.accounting_summary(mark_price)) << '\n';
}

void DiagnosticRecorder::write_final_agent_state(const AgentRuntime& runtime, double mark_price) {
  std::ofstream out(output_dir_ / "agent_final_state.csv");
  out << "agent_id,agent_type,initial_cash,initial_inventory,initial_equity,final_cash_free,final_cash_locked,final_cash_total,final_inventory_free,final_inventory_locked,final_inventory_total,final_equity,total_pnl,realized_pnl,unrealized_pnl,fees_paid,trade_count,buy_volume,sell_volume,open_order_count\n";
  for (const AgentEquitySnapshot& s : runtime.agent_equity_snapshots(mark_price)) {
    out << std::setprecision(12)
        << s.agent_id << ',' << lobx::agents::agent_type_name(s.agent_type) << ','
        << s.initial_cash << ',' << s.initial_inventory << ',' << s.initial_equity << ','
        << s.free_cash << ',' << s.locked_cash << ',' << s.total_cash << ','
        << s.free_inventory << ',' << s.locked_inventory << ',' << s.total_inventory << ','
        << s.final_equity << ',' << s.pnl << ','
        << 0.0 << ',' << 0.0 << ',' << s.fees_paid << ','
        << s.trade_count << ',' << s.buy_volume << ',' << s.sell_volume << ',' << s.open_order_count << '\n';
  }
}

void DiagnosticRecorder::write_agent_type_summary(const AgentRuntime& runtime, double mark_price) {
  std::unordered_map<lobx::agents::AgentType, TypeAggregate> by_type;
  for (const AgentEquitySnapshot& s : runtime.agent_equity_snapshots(mark_price)) {
    TypeAggregate& agg = by_type[s.agent_type];
    ++agg.count;
    agg.pnl.push_back(s.pnl);
    agg.sum_pnl += s.pnl;
    agg.sum_equity += s.final_equity;
    agg.sum_inventory += s.total_inventory;
    agg.total_buy_volume += s.buy_volume;
    agg.total_sell_volume += s.sell_volume;
    agg.total_trade_count += s.trade_count;
    agg.sum_open_order_count += s.open_order_count;
    if (s.pnl < 0.0) ++agg.negative_count;
    else if (s.pnl > 0.0) ++agg.positive_count;
    else ++agg.zero_count;
  }
  std::ofstream out(output_dir_ / "agent_type_summary.csv");
  out << "agent_type,agent_count,mean_pnl,median_pnl,min_pnl,max_pnl,sum_pnl,mean_equity,mean_inventory,total_buy_volume,total_sell_volume,total_trade_count,mean_open_order_count,negative_pnl_count,positive_pnl_count,zero_pnl_count\n";
  for (auto& [type, agg] : by_type) {
    const double count = static_cast<double>(std::max(1, agg.count));
    const auto [min_it, max_it] = std::minmax_element(agg.pnl.begin(), agg.pnl.end());
    out << lobx::agents::agent_type_name(type) << ','
        << agg.count << ','
        << agg.sum_pnl / count << ','
        << median(agg.pnl) << ','
        << (min_it == agg.pnl.end() ? 0.0 : *min_it) << ','
        << (max_it == agg.pnl.end() ? 0.0 : *max_it) << ','
        << agg.sum_pnl << ','
        << agg.sum_equity / count << ','
        << agg.sum_inventory / count << ','
        << agg.total_buy_volume << ','
        << agg.total_sell_volume << ','
        << agg.total_trade_count << ','
        << agg.sum_open_order_count / count << ','
        << agg.negative_count << ','
        << agg.positive_count << ','
        << agg.zero_count << '\n';
  }
}

void DiagnosticRecorder::write_empty_optional_files() {
  for (const char* file : {"orders.jsonl", "trades.jsonl", "cancels.jsonl", "rejections.jsonl", "impact_windows.csv"}) {
    std::ofstream out(output_dir_ / file);
    if (std::string(file) == "impact_windows.csv") {
      out << "scenario,seed,event_step,event_ts,event_type,agent_id,agent_type,pre_mid,post_mid,impact_bps,pre_spread_bps,post_spread_bps,pre_depth_bid,pre_depth_ask,post_depth_bid,post_depth_ask\n";
    }
  }
  if (!options_.order_trace) warnings_.push_back("ORDER_TRACE_UNAVAILABLE: Order JSONL trace is not yet emitted by AgentRuntime diagnostic recorder.");
  if (!options_.impact_windows) warnings_.push_back("IMPACT_WINDOWS_DISABLED: impact_windows.csv is written with header only.");
}

void DiagnosticRecorder::write_warnings() {
  std::ofstream out(output_dir_ / "diagnostic_warnings.json");
  out << "{\"warnings\":[";
  for (std::size_t i = 0; i < warnings_.size(); ++i) {
    const std::string& warning = warnings_[i];
    const std::size_t pos = warning.find(':');
    const std::string code = pos == std::string::npos ? warning : warning.substr(0, pos);
    const std::string message = pos == std::string::npos ? warning : warning.substr(pos + 1);
    if (i > 0) out << ',';
    out << "{\"code\":\"" << json_escape(code) << "\",\"message\":\"" << json_escape(message) << "\"}";
  }
  out << "]}\n";
}

void DiagnosticRecorder::accumulate_runtime_stats(const AgentRuntime& runtime) {
  const AgentRuntimeStepStats& stats = runtime.last_step_stats();
  interval_agent_decide_ms_ += stats.agent_decide_ms;
  interval_action_schedule_ms_ += stats.action_schedule_ms;
  interval_exchange_apply_ms_ += stats.exchange_apply_ms;
  interval_state_update_ms_ += stats.state_update_ms;
  interval_book_sample_ms_ += stats.book_sample_ms;
  interval_agents_due_ += stats.agents_due_count;
  interval_agents_skipped_ += stats.agents_skipped_count;
  interval_agent_decisions_ += stats.agent_decisions_count;

  total_agent_decide_ms_ += stats.agent_decide_ms;
  total_action_schedule_ms_ += stats.action_schedule_ms;
  total_exchange_apply_ms_ += stats.exchange_apply_ms;
  total_state_update_ms_ += stats.state_update_ms;
  total_book_sample_ms_ += stats.book_sample_ms;
}

void DiagnosticRecorder::reset_interval_stats() {
  interval_agent_decide_ms_ = 0.0;
  interval_action_schedule_ms_ = 0.0;
  interval_exchange_apply_ms_ = 0.0;
  interval_state_update_ms_ = 0.0;
  interval_recorder_ms_ = 0.0;
  interval_book_sample_ms_ = 0.0;
  interval_agents_due_ = 0;
  interval_agents_skipped_ = 0;
  interval_agent_decisions_ = 0;
}

} // namespace lobx::simulation
