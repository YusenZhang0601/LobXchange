#include "lobx/simulation/scenario_config.hpp"

#include <limits>
#include <set>
#include <utility>

namespace lobx::sim {

namespace {

constexpr UserId kReservedFeeAccount = std::numeric_limits<UserId>::max();

bool is_supported_strategy(const std::string& type) {
  return type == "market_maker" || type == "taker_sweep" ||
         type == "noise_trader" || type == "user_script";
}

bool is_known_param(const std::string& strategy_type, const std::string& param_name) {
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

double param_or(const BotConfig& bot, const std::string& name, double fallback) {
  const auto it = bot.params.find(name);
  return it == bot.params.end() ? fallback : it->second;
}

bool trade_events_equal(const std::vector<TradeEvent>& a, const std::vector<TradeEvent>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].market_id != b[i].market_id ||
        a[i].ts != b[i].ts ||
        a[i].price != b[i].price ||
        a[i].qty != b[i].qty ||
        a[i].buyer != b[i].buyer ||
        a[i].seller != b[i].seller ||
        a[i].buyer_order_id != b[i].buyer_order_id ||
        a[i].seller_order_id != b[i].seller_order_id ||
        a[i].liquidity_side != b[i].liquidity_side) {
      return false;
    }
  }
  return true;
}

bool balances_equal(const std::vector<WalletBalance>& a, const std::vector<WalletBalance>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].user != b[i].user ||
        a[i].asset != b[i].asset ||
        a[i].total != b[i].total ||
        a[i].locked != b[i].locked ||
        a[i].free != b[i].free) {
      return false;
    }
  }
  return true;
}

} // namespace

ValidationResult validate_scenario_config(const ScenarioConfig& config) {
  auto fail = [](std::string reason) {
    return ValidationResult{false, std::move(reason)};
  };

  if (config.ticks <= 0) return fail("ticks must be positive");
  if (config.market_symbol.empty()) return fail("market_symbol must not be empty");
  if (config.market_symbol != "BTC-USDT") return fail("unsupported market_symbol: " + config.market_symbol);
  if (config.bots.empty()) return fail("scenario must contain at least one bot");

  std::set<UserId> users;
  for (const BotConfig& bot : config.bots) {
    if (bot.user == kReservedFeeAccount) return fail("reserved fee account cannot be a bot");
    if (!users.insert(bot.user).second) return fail("duplicate bot user id: " + std::to_string(bot.user));
    if (bot.name.empty()) return fail("bot name must not be empty");
    if (!is_supported_strategy(bot.strategy_type)) return fail("unknown strategy_type: " + bot.strategy_type);
    if (bot.latency.order_latency < 0 || bot.latency.cancel_latency < 0 ||
        bot.latency.market_data_latency < 0 || bot.latency.private_data_latency < 0) {
      return fail("latency values must be non-negative");
    }
    for (const auto& [param_name, _] : bot.params) {
      if (!is_known_param(bot.strategy_type, param_name)) {
        return fail("unknown parameter for " + bot.strategy_type + ": " + param_name);
      }
    }
    if (bot.strategy_type == "market_maker") {
      if (param_or(bot, "qty", 1) <= 0) return fail("market_maker qty must be positive");
      if (param_or(bot, "bid_px", 99) <= 0 || param_or(bot, "ask_px", 101) <= 0) {
        return fail("market_maker prices must be positive");
      }
    } else if (bot.strategy_type == "taker_sweep") {
      if (param_or(bot, "target_qty", 1) <= 0) return fail("taker_sweep target_qty must be positive");
      if (param_or(bot, "limit_price", 101) <= 0) return fail("taker_sweep limit_price must be positive");
      const int side = static_cast<int>(param_or(bot, "side", 0));
      if (side != 0 && side != 1) return fail("taker_sweep side must be 0 or 1");
    } else if (bot.strategy_type == "user_script") {
      if (param_or(bot, "qty", 1) <= 0) return fail("user_script qty must be positive");
      if (param_or(bot, "price", 100) <= 0) return fail("user_script price must be positive");
    }
  }

  return ValidationResult{true, {}};
}

bool same_research_result(const ResearchRunResult& a, const ResearchRunResult& b) {
  return a.action_trace == b.action_trace &&
         trade_events_equal(a.trades, b.trades) &&
         a.event_types == b.event_types &&
         balances_equal(a.balances, b.balances) &&
         a.bids == b.bids &&
         a.asks == b.asks &&
         a.metrics == b.metrics &&
         a.ledger_invariant_ok == b.ledger_invariant_ok &&
         a.book_open_consistency_ok == b.book_open_consistency_ok &&
         a.no_private_data_leak == b.no_private_data_leak &&
         a.no_future_public_data_leak == b.no_future_public_data_leak;
}

} // namespace lobx::sim
