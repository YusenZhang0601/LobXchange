#include "lobx/simulation/research_cli.hpp"

#include "lobx/simulation/config_loader.hpp"
#include "lobx/simulation/file_export.hpp"
#include "lobx/simulation/multi_seed_evaluation.hpp"
#include "lobx/simulation/parameter_sweep.hpp"
#include "lobx/simulation/research_runner.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace lobx::sim {

namespace {

ResearchCliResult ok_result(std::string stdout_text = {}) {
  return ResearchCliResult{0, std::move(stdout_text), {}};
}

ResearchCliResult error_result(const std::string& message) {
  return ResearchCliResult{1, {}, "error: " + message + "\n"};
}

bool params_equal(const std::map<std::string, double>& a, const std::map<std::string, double>& b) {
  return a == b;
}

const BotConfig* find_bot(const ScenarioConfig& config, const std::string& name) {
  for (const BotConfig& bot : config.bots) {
    if (bot.name == name) return &bot;
  }
  return nullptr;
}

bool find_ranked_config(const std::vector<SweepRun>& runs,
                        const RankedStrategyResult& ranked,
                        ScenarioConfig* out) {
  for (const SweepRun& run : runs) {
    const BotConfig* bot = find_bot(run.config, ranked.bot_name);
    if (bot != nullptr && bot->user == ranked.user && params_equal(bot->params, ranked.params)) {
      *out = run.config;
      return true;
    }
  }
  return false;
}

bool parse_non_negative_int(const std::string& text, int* out) {
  if (text.empty()) return false;
  size_t start = 0;
  if (text[0] == '+') start = 1;
  if (start == text.size()) return false;
  for (size_t i = start; i < text.size(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
  }
  try {
    size_t consumed = 0;
    const long long value = std::stoll(text, &consumed, 10);
    if (consumed != text.size() || value < 0 || value > static_cast<long long>(std::numeric_limits<int>::max())) {
      return false;
    }
    *out = static_cast<int>(value);
    return true;
  } catch (...) {
    return false;
  }
}

std::string metric_name(RankingMetric metric) {
  switch (metric) {
    case RankingMetric::NetPnl: return "net_pnl";
    case RankingMetric::GrossPnl: return "gross_pnl";
    case RankingMetric::FeesPaidInverse: return "fees_paid_inverse";
  }
  return "net_pnl";
}

std::vector<RankedStrategyResult> truncate_ranked(std::vector<RankedStrategyResult> ranked, int top_n) {
  if (top_n > 0 && ranked.size() > static_cast<size_t>(top_n)) {
    ranked.resize(static_cast<size_t>(top_n));
  }
  return ranked;
}

} // namespace

bool parse_ranking_metric(const std::string& s, RankingMetric* out) {
  if (s == "net_pnl") {
    if (out != nullptr) *out = RankingMetric::NetPnl;
    return true;
  }
  if (s == "gross_pnl") {
    if (out != nullptr) *out = RankingMetric::GrossPnl;
    return true;
  }
  if (s == "fees_paid_inverse") {
    if (out != nullptr) *out = RankingMetric::FeesPaidInverse;
    return true;
  }
  return false;
}

std::string research_cli_usage() {
  return
      "usage: lobx_research_runner "
      "--scenario <scenario.json> "
      "--sweep <sweep.json> "
      "--seeds <seeds.json> "
      "--rank-bot <bot_name> "
      "--metric <net_pnl|gross_pnl|fees_paid_inverse> "
      "--out <output_dir> "
      "[--top-n <N>] "
      "[--verbose]\n";
}

ResearchCliResult parse_research_cli_args(const std::vector<std::string>& args,
                                          ResearchCliOptions* out) {
  if (out == nullptr) return error_result("internal error: null options");
  *out = ResearchCliOptions{};

  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    auto require_value = [&](std::string* target) -> bool {
      if (i + 1 >= args.size()) return false;
      *target = args[++i];
      return true;
    };

    if (arg == "--help") {
      out->help = true;
    } else if (arg == "--verbose") {
      out->verbose = true;
    } else if (arg == "--scenario") {
      if (!require_value(&out->scenario_path)) return error_result("missing value for --scenario");
    } else if (arg == "--sweep") {
      if (!require_value(&out->sweep_path)) return error_result("missing value for --sweep");
    } else if (arg == "--seeds") {
      if (!require_value(&out->seeds_path)) return error_result("missing value for --seeds");
    } else if (arg == "--rank-bot") {
      if (!require_value(&out->rank_bot)) return error_result("missing value for --rank-bot");
    } else if (arg == "--metric") {
      if (!require_value(&out->metric)) return error_result("missing value for --metric");
    } else if (arg == "--out") {
      if (!require_value(&out->output_dir)) return error_result("missing value for --out");
    } else if (arg == "--top-n") {
      std::string value;
      if (!require_value(&value)) return error_result("missing value for --top-n");
      if (!parse_non_negative_int(value, &out->top_n)) return error_result("invalid --top-n: " + value);
    } else {
      return error_result("unknown argument: " + arg);
    }
  }

  if (out->help) return ok_result(research_cli_usage());
  if (out->scenario_path.empty()) return error_result("missing required argument --scenario");
  if (out->sweep_path.empty()) return error_result("missing required argument --sweep");
  if (out->seeds_path.empty()) return error_result("missing required argument --seeds");
  if (out->rank_bot.empty()) return error_result("missing required argument --rank-bot");
  if (out->metric.empty()) return error_result("missing required argument --metric");
  if (out->output_dir.empty()) return error_result("missing required argument --out");
  return ok_result();
}

ResearchCliResult run_research_cli(const std::vector<std::string>& args) {
  ResearchCliOptions options;
  ResearchCliResult parsed = parse_research_cli_args(args, &options);
  if (parsed.exit_code != 0 || options.help) return parsed;

  RankingMetric metric = RankingMetric::NetPnl;
  if (!parse_ranking_metric(options.metric, &metric)) {
    return error_result("unknown ranking metric: " + options.metric);
  }

  const ScenarioConfigLoadResult scenario = load_scenario_config_from_json_file(options.scenario_path);
  if (!scenario.ok) return error_result("failed to load scenario: " + scenario.reason);

  const SweepConfigLoadResult sweep = load_sweep_config_from_json_file(options.sweep_path);
  if (!sweep.ok) return error_result("failed to load sweep: " + sweep.reason);

  const MultiSeedConfigLoadResult seeds = load_seed_config_from_json_file(options.seeds_path);
  if (!seeds.ok) return error_result("failed to load seeds: " + seeds.reason);

  const ValidationResult validation = validate_scenario_config(scenario.config);
  if (!validation.ok) return error_result("invalid scenario: " + validation.reason);

  ResearchRunner runner;
  const std::vector<SweepRun> sweep_runs = ::lobx::sim::run_parameter_sweep(runner, scenario.config, sweep.params);
  if (sweep_runs.empty()) return error_result("parameter sweep produced no runs");

  const std::vector<RankedStrategyResult> full_ranked =
      rank_sweep_results(sweep_runs, options.rank_bot, metric);
  if (full_ranked.empty()) return error_result("ranking produced no results for bot: " + options.rank_bot);

  ScenarioConfig top_config;
  if (!find_ranked_config(sweep_runs, full_ranked.front(), &top_config)) {
    return error_result("failed to locate top ranked scenario config");
  }

  const std::vector<SeedEvaluationRun> multi_seed =
      run_multi_seed_evaluation(runner, top_config, seeds.seeds);
  if (multi_seed.empty()) return error_result("multi-seed evaluation produced no runs");

  const AggregatedStrategyStats aggregated = aggregate_strategy_stats(multi_seed, options.rank_bot);
  if (aggregated.runs == 0) return error_result("failed to aggregate stats for bot: " + options.rank_bot);

  const std::vector<RankedStrategyResult> ranked = truncate_ranked(full_ranked, options.top_n);
  const FileWriteResult write =
      write_research_bundle(options.output_dir, ranked, {aggregated}, multi_seed.front().result);
  if (!write.ok) return error_result("failed to write output bundle: " + write.reason);

  std::ostringstream out;
  out << "lobx research runner completed\n"
      << "sweep_runs=" << sweep_runs.size() << "\n"
      << "ranked=" << ranked.size() << "\n"
      << "multi_seed_runs=" << multi_seed.size() << "\n"
      << "output_dir=" << options.output_dir << "\n";
  if (options.verbose) {
    out << "scenario=" << options.scenario_path << "\n"
        << "sweep=" << options.sweep_path << "\n"
        << "seeds=" << options.seeds_path << "\n"
        << "rank_bot=" << options.rank_bot << "\n"
        << "metric=" << metric_name(metric) << "\n"
        << "top_score=" << static_cast<double>(full_ranked.front().score) << "\n";
  }
  return ok_result(out.str());
}

} // namespace lobx::sim
