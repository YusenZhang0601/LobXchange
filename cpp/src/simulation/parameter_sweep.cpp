#include "lobx/simulation/parameter_sweep.hpp"

#include "lobx/simulation/research_runner.hpp"

#include <utility>

namespace lobx::sim {

namespace {

BotConfig* find_bot(ScenarioConfig& config, const std::string& name) {
  for (BotConfig& bot : config.bots) {
    if (bot.name == name) return &bot;
  }
  return nullptr;
}

const BotConfig* find_bot(const ScenarioConfig& config, const std::string& name) {
  for (const BotConfig& bot : config.bots) {
    if (bot.name == name) return &bot;
  }
  return nullptr;
}

bool is_known_param(const std::string& strategy_type, const std::string& param_name) {
  if (strategy_type == "market_maker") {
    return param_name == "bid_px" || param_name == "ask_px" || param_name == "qty";
  }
  if (strategy_type == "taker_sweep") {
    return param_name == "side" || param_name == "target_qty" ||
           param_name == "limit_price" || param_name == "max_avg_price";
  }
  if (strategy_type == "noise_trader") return param_name == "seed";
  if (strategy_type == "user_script") {
    return param_name == "side" || param_name == "price" || param_name == "qty" ||
           param_name == "flags" || param_name == "decision_ts" || param_name == "order_id";
  }
  return false;
}

bool validate_sweep(const ScenarioConfig& base, const std::vector<SweepParam>& params) {
  for (const SweepParam& param : params) {
    const BotConfig* bot = find_bot(base, param.bot_name);
    if (bot == nullptr || param.values.empty()) return false;
    if (!is_known_param(bot->strategy_type, param.param_name)) return false;
  }
  return true;
}

void expand_at(const ScenarioConfig& current,
               const std::vector<SweepParam>& params,
               size_t index,
               std::vector<ScenarioConfig>& out) {
  if (index == params.size()) {
    out.push_back(current);
    return;
  }
  const SweepParam& param = params[index];
  for (double value : param.values) {
    ScenarioConfig next = current;
    BotConfig* bot = find_bot(next, param.bot_name);
    if (bot == nullptr) return;
    bot->params[param.param_name] = value;
    expand_at(next, params, index + 1, out);
  }
}

} // namespace

std::vector<ScenarioConfig> expand_parameter_sweep(const ScenarioConfig& base,
                                                   const std::vector<SweepParam>& params) {
  if (!validate_sweep(base, params)) return {};
  std::vector<ScenarioConfig> out;
  expand_at(base, params, 0, out);
  return out;
}

std::vector<SweepRun> run_parameter_sweep(ResearchRunner& runner,
                                          const ScenarioConfig& base,
                                          const std::vector<SweepParam>& params) {
  std::vector<SweepRun> out;
  for (const ScenarioConfig& config : expand_parameter_sweep(base, params)) {
    ResearchRunResult result = runner.run_scenario(config);
    if (!result.ledger_invariant_ok) return {};
    out.push_back(SweepRun{config, std::move(result)});
  }
  return out;
}

} // namespace lobx::sim
