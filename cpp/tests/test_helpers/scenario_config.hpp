#pragma once

#include "test_helpers/bot_test_agents.hpp"

#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace lobx_test {

struct BotConfig {
  lobx::UserId user{0};
  std::string name;
  std::string strategy_type;
  TestLatencyModel latency{1, 1, 1};
  std::map<std::string, double> params;
};

struct ScenarioConfig {
  uint64_t seed{0};
  int ticks{0};
  std::string market_symbol{"BTC-USDT"};
  std::vector<BotConfig> bots;
};

struct ScenarioBuildResult {
  bool ok{false};
  std::string reason;
  BotRunResult result;
};

inline double scenario_param_or(const BotConfig& bot, const std::string& name, double fallback) {
  const auto it = bot.params.find(name);
  return it == bot.params.end() ? fallback : it->second;
}

inline bool is_supported_strategy_type(const std::string& type) {
  return type == "market_maker" || type == "taker_sweep" ||
         type == "noise_trader" || type == "user_script";
}

inline const BotConfig* find_bot_config_by_name(const ScenarioConfig& config, const std::string& name) {
  for (const BotConfig& bot : config.bots) {
    if (bot.name == name) return &bot;
  }
  return nullptr;
}

inline bool is_known_strategy_param(const std::string& strategy_type, const std::string& param_name) {
  if (strategy_type == "market_maker") {
    return param_name == "bid_px" || param_name == "ask_px" || param_name == "qty";
  }
  if (strategy_type == "taker_sweep") {
    return param_name == "side" || param_name == "target_qty" ||
           param_name == "limit_price" || param_name == "max_avg_price";
  }
  if (strategy_type == "noise_trader") {
    return param_name == "seed";
  }
  if (strategy_type == "user_script") {
    return param_name == "side" || param_name == "price" || param_name == "qty" ||
           param_name == "flags" || param_name == "decision_ts" || param_name == "order_id";
  }
  return false;
}

inline bool validate_scenario_config(const ScenarioConfig& config, std::string* reason) {
  auto fail = [&](std::string message) {
    if (reason != nullptr) *reason = std::move(message);
    return false;
  };

  if (config.ticks <= 0) return fail("ticks must be positive");
  if (config.market_symbol.empty()) return fail("market symbol must not be empty");
  if (config.market_symbol != "BTC-USDT") return fail("unknown market symbol: " + config.market_symbol);
  if (config.bots.empty()) return fail("scenario must contain at least one bot");

  std::set<lobx::UserId> users;
  for (const BotConfig& bot : config.bots) {
    if (bot.user == dedicated_fee_account()) return fail("reserved fee account cannot be a bot");
    if (!users.insert(bot.user).second) return fail("duplicate bot user id: " + std::to_string(bot.user));
    if (!is_supported_strategy_type(bot.strategy_type)) return fail("unknown strategy type: " + bot.strategy_type);
    if (bot.latency.order_latency < 0 || bot.latency.cancel_latency < 0 ||
        bot.latency.market_data_latency < 0) {
      return fail("latency values must be non-negative");
    }
    for (const auto& [param_name, _] : bot.params) {
      if (!is_known_strategy_param(bot.strategy_type, param_name)) {
        return fail("unknown parameter for strategy " + bot.strategy_type + ": " + param_name);
      }
    }
  }
  if (reason != nullptr) reason->clear();
  return true;
}

inline BotAction scenario_script_action(const BotConfig& bot, lobx::OrderId fallback_order_id) {
  const int side_value = static_cast<int>(scenario_param_or(bot, "side", 0));
  const lob::Side side = side_value == 1 ? lob::Side::Ask : lob::Side::Bid;
  const lob::Tick price = static_cast<lob::Tick>(scenario_param_or(bot, "price", side == lob::Side::Bid ? 100 : 101));
  const lob::Quantity qty = static_cast<lob::Quantity>(scenario_param_or(bot, "qty", 1));
  const uint32_t flags = static_cast<uint32_t>(scenario_param_or(bot, "flags", lob::IOC));
  const lob::Timestamp decision_ts = static_cast<lob::Timestamp>(scenario_param_or(bot, "decision_ts", 1));
  const lobx::OrderId order_id = static_cast<lobx::OrderId>(scenario_param_or(bot, "order_id", fallback_order_id));
  BotAction action{bot.user, order_id, side, price, qty, flags};
  action.decision_ts = decision_ts;
  return action;
}

inline std::unique_ptr<Strategy> make_strategy_from_config(const BotConfig& bot,
                                                           uint64_t scenario_seed,
                                                           size_t bot_index) {
  const lobx::OrderId first_order_id =
      static_cast<lobx::OrderId>(800000 + static_cast<int64_t>(bot_index) * 10000);

  if (bot.strategy_type == "market_maker") {
    return std::make_unique<MarketMakerStrategy>(
        static_cast<lob::Tick>(scenario_param_or(bot, "bid_px", 99)),
        static_cast<lob::Tick>(scenario_param_or(bot, "ask_px", 101)),
        static_cast<lob::Quantity>(scenario_param_or(bot, "qty", 1)),
        first_order_id);
  }
  if (bot.strategy_type == "taker_sweep") {
    const int side_value = static_cast<int>(scenario_param_or(bot, "side", 0));
    return std::make_unique<TakerSweepStrategy>(
        side_value == 1 ? lob::Side::Ask : lob::Side::Bid,
        static_cast<lob::Quantity>(scenario_param_or(bot, "target_qty", 1)),
        static_cast<lob::Tick>(scenario_param_or(bot, "limit_price", 101)),
        static_cast<long double>(scenario_param_or(bot, "max_avg_price", 101)),
        first_order_id);
  }
  if (bot.strategy_type == "noise_trader") {
    const uint64_t strategy_seed =
        bot.params.find("seed") == bot.params.end()
            ? scenario_seed * 131 + bot.user * 17 + static_cast<uint64_t>(bot_index)
            : static_cast<uint64_t>(scenario_param_or(bot, "seed", 0));
    return std::make_unique<NoiseTraderStrategy>(strategy_seed, first_order_id);
  }

  return std::make_unique<UserStrategyStub>(
      std::vector<BotAction>{scenario_script_action(bot, first_order_id)});
}

inline ScenarioBuildResult run_scenario_config(const ScenarioConfig& config) {
  ScenarioBuildResult out{};
  if (!validate_scenario_config(config, &out.reason)) return out;

  BotSimulationRunner runner(config.seed);
  for (size_t i = 0; i < config.bots.size(); ++i) {
    const BotConfig& bot = config.bots[i];
    if (!runner.add_bot(BotInstance{bot.user, bot.name, bot.latency,
                                    make_strategy_from_config(bot, config.seed, i)})) {
      out.reason = "failed to add bot: " + bot.name;
      return out;
    }
  }

  out.result = runner.run(config.ticks);
  out.ok = true;
  return out;
}

} // namespace lobx_test
