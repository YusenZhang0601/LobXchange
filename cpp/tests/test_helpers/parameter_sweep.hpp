#pragma once

#include "test_helpers/scenario_config.hpp"

#include <string>
#include <vector>

namespace lobx_test {

struct SweepParam {
  std::string bot_name;
  std::string param_name;
  std::vector<double> values;
};

struct SweepRun {
  ScenarioConfig config;
  BotRunResult result;
};

inline BotConfig* find_bot_config(ScenarioConfig& config, const std::string& name) {
  for (BotConfig& bot : config.bots) {
    if (bot.name == name) return &bot;
  }
  return nullptr;
}

inline const BotConfig* find_bot_config(const ScenarioConfig& config, const std::string& name) {
  for (const BotConfig& bot : config.bots) {
    if (bot.name == name) return &bot;
  }
  return nullptr;
}

inline bool validate_sweep_params(const ScenarioConfig& base, const std::vector<SweepParam>& params) {
  for (const SweepParam& param : params) {
    const BotConfig* bot = find_bot_config(base, param.bot_name);
    if (bot == nullptr) return false;
    if (param.values.empty()) return false;
    if (!is_known_strategy_param(bot->strategy_type, param.param_name)) return false;
  }
  return true;
}

inline void expand_parameter_sweep_at(const ScenarioConfig& current,
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
    BotConfig* bot = find_bot_config(next, param.bot_name);
    if (bot == nullptr) return;
    bot->params[param.param_name] = value;
    expand_parameter_sweep_at(next, params, index + 1, out);
  }
}

inline std::vector<ScenarioConfig> expand_parameter_sweep(const ScenarioConfig& base,
                                                          const std::vector<SweepParam>& params) {
  if (!validate_sweep_params(base, params)) return {};
  std::vector<ScenarioConfig> out;
  expand_parameter_sweep_at(base, params, 0, out);
  return out;
}

inline std::vector<SweepRun> run_parameter_sweep(const ScenarioConfig& base,
                                                const std::vector<SweepParam>& params) {
  std::vector<SweepRun> out;
  for (const ScenarioConfig& config : expand_parameter_sweep(base, params)) {
    ScenarioBuildResult run = run_scenario_config(config);
    if (!run.ok) return {};
    out.push_back(SweepRun{config, run.result});
  }
  return out;
}

} // namespace lobx_test
