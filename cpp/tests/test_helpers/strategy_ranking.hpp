#pragma once

#include "test_helpers/parameter_sweep.hpp"
#include "test_helpers/strategy_metrics.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace lobx_test {

struct RankedStrategyResult {
  int rank{0};
  std::string bot_name;
  lobx::UserId user{0};
  std::map<std::string, double> params;
  long double score{0.0L};
  StrategyMetrics metrics;
};

enum class RankingMetric {
  NetPnl,
  GrossPnl,
  FeesPaidInverse
};

inline std::string serialize_params(const std::map<std::string, double>& params) {
  std::ostringstream os;
  for (const auto& [key, value] : params) {
    os << key << '=' << value << ';';
  }
  return os.str();
}

inline long double score_for_metric(const StrategyMetrics& metrics, RankingMetric metric) {
  switch (metric) {
    case RankingMetric::NetPnl: return metrics.net_pnl;
    case RankingMetric::GrossPnl: return metrics.gross_pnl;
    case RankingMetric::FeesPaidInverse: return -static_cast<long double>(metrics.fees_paid);
  }
  return metrics.net_pnl;
}

inline std::vector<RankedStrategyResult> rank_sweep_results(const std::vector<SweepRun>& runs,
                                                            const std::string& bot_name,
                                                            RankingMetric metric) {
  std::vector<RankedStrategyResult> out;
  for (const SweepRun& run : runs) {
    const BotConfig* bot = find_bot_config(run.config, bot_name);
    if (bot == nullptr) return {};
    const auto metrics_it = run.result.metrics.find(bot->user);
    if (metrics_it == run.result.metrics.end()) return {};
    out.push_back(RankedStrategyResult{0,
                                       bot->name,
                                       bot->user,
                                       bot->params,
                                       score_for_metric(metrics_it->second, metric),
                                       metrics_it->second});
  }

  std::sort(out.begin(), out.end(), [](const RankedStrategyResult& a, const RankedStrategyResult& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.bot_name != b.bot_name) return a.bot_name < b.bot_name;
    if (a.user != b.user) return a.user < b.user;
    return serialize_params(a.params) < serialize_params(b.params);
  });
  for (size_t i = 0; i < out.size(); ++i) out[i].rank = static_cast<int>(i + 1);
  return out;
}

} // namespace lobx_test
