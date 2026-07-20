#include "lobx/simulation/agent_population.hpp"

#include <limits>
#include <random>
#include <set>
#include <utility>

namespace lobx::sim {

namespace {

constexpr UserId kReservedFeeAccount = std::numeric_limits<UserId>::max();

bool is_supported_population_strategy(const std::string& type) {
  return type == "market_maker" ||
         type == "noise_trader" ||
         type == "taker_sweep" ||
         type == "momentum" ||
         type == "mean_reversion" ||
         type == "liquidity_sniper" ||
         type == "adversarial_sweeper";
}

bool is_runtime_param(const std::string& strategy, const std::string& name) {
  if (strategy == "market_maker") return name == "bid_px" || name == "ask_px" || name == "qty";
  if (strategy == "noise_trader") return name == "seed";
  if (strategy == "taker_sweep") {
    return name == "side" || name == "target_qty" || name == "limit_price" || name == "max_avg_price";
  }
  if (strategy == "momentum" || strategy == "adversarial_sweeper" || strategy == "liquidity_sniper") {
    return name == "side" || name == "target_qty" || name == "limit_price" || name == "max_avg_price" ||
           name == "seed" || name == "interval" || name == "quantity";
  }
  if (strategy == "mean_reversion") {
    return name == "bid_px" || name == "ask_px" || name == "qty" || name == "reference_price" || name == "seed";
  }
  return false;
}

bool latency_range_valid(const LatencyRangeConfig& latency) {
  return latency.order_min >= 0 && latency.cancel_min >= 0 &&
         latency.market_data_min >= 0 && latency.private_data_min >= 0 &&
         latency.order_min <= latency.order_max &&
         latency.cancel_min <= latency.cancel_max &&
         latency.market_data_min <= latency.market_data_max &&
         latency.private_data_min <= latency.private_data_max;
}

int draw_int(std::mt19937& rng, int min, int max) {
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng);
}

double draw_double(std::mt19937& rng, const DoubleRange& range) {
  std::uniform_real_distribution<double> dist(range.min, range.max);
  return dist(rng);
}

double range_or(std::mt19937& rng,
                const std::map<std::string, DoubleRange>& ranges,
                const std::string& name,
                double fallback) {
  const auto it = ranges.find(name);
  return it == ranges.end() ? fallback : draw_double(rng, it->second);
}

} // namespace

AgentPopulationValidation validate_agent_population(const AgentPopulationConfig& config) {
  auto fail = [](std::string reason) {
    return AgentPopulationValidation{false, std::move(reason)};
  };

  if (config.first_user_id == 0) return fail("first_user_id must be positive");
  if (config.first_user_id == kReservedFeeAccount) return fail("first_user_id cannot be reserved fee account");
  if (config.groups.empty()) return fail("agent population must contain at least one group");

  uint64_t total = 0;
  for (const AgentGroupConfig& group : config.groups) {
    if (!is_supported_population_strategy(group.strategy_type)) {
      return fail("unknown strategy_type: " + group.strategy_type);
    }
    if (group.count <= 0) return fail("group count must be positive");
    if (group.name_prefix.empty()) return fail("name_prefix must not be empty");
    if (!latency_range_valid(group.latency_range)) return fail("invalid latency range");
    for (const auto& [name, range] : group.param_ranges) {
      (void)name;
      if (range.min > range.max) return fail("invalid parameter range");
    }
    total += static_cast<uint64_t>(group.count);
  }

  if (total == 0) return fail("agent population must not be empty");
  if (config.first_user_id > kReservedFeeAccount - total) {
    return fail("generated users would overlap reserved fee account");
  }
  return AgentPopulationValidation{true, {}};
}

std::vector<BotConfig> generate_agent_population(const AgentPopulationConfig& config) {
  if (!validate_agent_population(config).ok) return {};

  std::vector<BotConfig> out;
  std::mt19937 rng(static_cast<uint32_t>(config.seed));
  UserId next_user = config.first_user_id;

  for (const AgentGroupConfig& group : config.groups) {
    for (int i = 0; i < group.count; ++i) {
      BotConfig bot{};
      bot.user = next_user++;
      bot.name = group.name_prefix + std::to_string(i);
      bot.strategy_type = group.strategy_type;
      bot.latency = LatencyConfig{
          static_cast<lob::Timestamp>(draw_int(rng, group.latency_range.order_min, group.latency_range.order_max)),
          static_cast<lob::Timestamp>(draw_int(rng, group.latency_range.cancel_min, group.latency_range.cancel_max)),
          static_cast<lob::Timestamp>(draw_int(rng, group.latency_range.market_data_min, group.latency_range.market_data_max)),
          static_cast<lob::Timestamp>(draw_int(rng, group.latency_range.private_data_min, group.latency_range.private_data_max))};

      for (const auto& [name, range] : group.param_ranges) {
        if (is_runtime_param(bot.strategy_type, name)) bot.params[name] = draw_double(rng, range);
      }

      if (bot.strategy_type == "market_maker") {
        bot.params["bid_px"] = range_or(rng, group.param_ranges, "bid_px", 99.0);
        bot.params["ask_px"] = range_or(rng, group.param_ranges, "ask_px", 101.0);
        bot.params["qty"] = range_or(rng, group.param_ranges, "qty", 1.0);
      } else if (bot.strategy_type == "noise_trader") {
        bot.params["seed"] = range_or(rng, group.param_ranges, "seed", static_cast<double>(config.seed + bot.user));
      } else if (bot.strategy_type == "taker_sweep" ||
                 bot.strategy_type == "momentum" ||
                 bot.strategy_type == "liquidity_sniper" ||
                 bot.strategy_type == "adversarial_sweeper") {
        bot.params["side"] = range_or(rng, group.param_ranges, "side", 0.0) >= 0.5 ? 1.0 : 0.0;
        bot.params["target_qty"] = range_or(rng, group.param_ranges, "target_qty", 1.0);
        bot.params["limit_price"] = range_or(rng, group.param_ranges, "limit_price", 101.0);
        bot.params["max_avg_price"] = range_or(rng, group.param_ranges, "max_avg_price", bot.params["limit_price"]);
      } else if (bot.strategy_type == "mean_reversion") {
        bot.params["bid_px"] = range_or(rng, group.param_ranges, "bid_px", 99.0);
        bot.params["ask_px"] = range_or(rng, group.param_ranges, "ask_px", 101.0);
        bot.params["qty"] = range_or(rng, group.param_ranges, "qty", 1.0);
        bot.params["reference_price"] = range_or(rng, group.param_ranges, "reference_price", 100.0);
      }
      out.push_back(std::move(bot));
    }
  }
  return out;
}

} // namespace lobx::sim
