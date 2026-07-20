#include "lobx/agents/agent_factory.hpp"

#include "lobx/agents/builtins/funding_arbitrage_agent.hpp"
#include "lobx/agents/builtins/grid_bot_agent.hpp"
#include "lobx/agents/builtins/hawkes_panic_agent.hpp"
#include "lobx/agents/builtins/liquidation_sniper_agent.hpp"
#include "lobx/agents/builtins/mean_reverter_agent.hpp"
#include "lobx/agents/builtins/momentum_follower_agent.hpp"
#include "lobx/agents/builtins/noise_trader_agent.hpp"
#include "lobx/agents/builtins/ofi_momentum_agent.hpp"
#include "lobx/agents/builtins/static_market_maker_agent.hpp"
#include "lobx/agents/builtins/whale_sweeper_agent.hpp"

#include <array>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace lobx::agents {

namespace {

struct AgentNameSpec {
  AgentType type;
  std::string_view name;
  bool canonical;
};

inline constexpr std::array<AgentNameSpec, 19> kAgentNameSpecs{{
    {AgentType::StaticMarketMaker, "static_market_maker", true},
    {AgentType::StaticMarketMaker, "market_maker", false},
    {AgentType::NoiseTrader, "noise_trader", true},
    {AgentType::MomentumFollower, "momentum_follower", true},
    {AgentType::MomentumFollower, "momentum", false},
    {AgentType::MeanReverter, "mean_reverter", true},
    {AgentType::MeanReverter, "mean_reversion", false},
    {AgentType::WhaleSweeper, "whale_sweeper", true},
    {AgentType::WhaleSweeper, "whale", false},
    {AgentType::DynamicMarketMaker, "dynamic_market_maker", true},
    {AgentType::LiquidityTaker, "liquidity_taker", true},
    {AgentType::LiquidityTaker, "taker_sweep", false},
    {AgentType::AdversarialSweeper, "adversarial_sweeper", true},
    {AgentType::LiquidityWithdrawer, "liquidity_withdrawer", true},
    {AgentType::GridBot, "grid_bot", true},
    {AgentType::FundingArbitrageur, "funding_arbitrageur", true},
    {AgentType::LiquidationSniper, "liquidation_sniper", true},
    {AgentType::OfiMomentum, "ofi_momentum", true},
    {AgentType::HawkesPanic, "hawkes_panic", true},
}};

template <typename AgentT>
struct AgentClassTraits;

template <>
struct AgentClassTraits<StaticMarketMakerAgent> {
  static constexpr AgentType type = AgentType::StaticMarketMaker;
};

template <>
struct AgentClassTraits<NoiseTraderAgent> {
  static constexpr AgentType type = AgentType::NoiseTrader;
};

template <>
struct AgentClassTraits<MomentumFollowerAgent> {
  static constexpr AgentType type = AgentType::MomentumFollower;
};

template <>
struct AgentClassTraits<MeanReverterAgent> {
  static constexpr AgentType type = AgentType::MeanReverter;
};

template <>
struct AgentClassTraits<WhaleSweeperAgent> {
  static constexpr AgentType type = AgentType::WhaleSweeper;
};

template <>
struct AgentClassTraits<GridBotAgent> {
  static constexpr AgentType type = AgentType::GridBot;
};

template <>
struct AgentClassTraits<FundingArbitrageAgent> {
  static constexpr AgentType type = AgentType::FundingArbitrageur;
};

template <>
struct AgentClassTraits<LiquidationSniperAgent> {
  static constexpr AgentType type = AgentType::LiquidationSniper;
};

template <>
struct AgentClassTraits<OfiMomentumAgent> {
  static constexpr AgentType type = AgentType::OfiMomentum;
};

template <>
struct AgentClassTraits<HawkesPanicAgent> {
  static constexpr AgentType type = AgentType::HawkesPanic;
};

template <typename AgentT>
void register_agent_class(AgentFactoryRegistry& registry) {
  constexpr AgentType type = AgentClassTraits<AgentT>::type;
  auto factory = [](AgentId id, const AgentConfig& config) -> std::unique_ptr<IAgent> {
    return std::make_unique<AgentT>(id, config);
  };

  for (const AgentNameSpec& spec : kAgentNameSpecs) {
    if (spec.type == type) registry.register_factory(std::string(spec.name), factory);
  }
}

template <typename T>
struct ActionPayloadTraits;

template <>
struct ActionPayloadTraits<SubmitLimitOrder> {
  static constexpr std::string_view name = "submit_limit_order";
};

template <>
struct ActionPayloadTraits<SubmitMarketOrder> {
  static constexpr std::string_view name = "submit_market_order";
};

template <>
struct ActionPayloadTraits<CancelOrder> {
  static constexpr std::string_view name = "cancel_order";
};

template <>
struct ActionPayloadTraits<ReplaceOrder> {
  static constexpr std::string_view name = "replace_order";
};

template <>
struct ActionPayloadTraits<CancelAllOrders> {
  static constexpr std::string_view name = "cancel_all_orders";
};

template <>
struct ActionPayloadTraits<SleepUntil> {
  static constexpr std::string_view name = "sleep_until";
};

} // namespace

const char* agent_type_name(AgentType type) {
  for (const AgentNameSpec& spec : kAgentNameSpecs) {
    if (spec.type == type && spec.canonical) return spec.name.data();
  }
  return "unknown";
}

AgentType agent_type_from_name(const std::string& name) {
  for (const AgentNameSpec& spec : kAgentNameSpecs) {
    if (spec.name == name) return spec.type;
  }
  return AgentType::Unknown;
}

const char* side_name(Side side) {
  return side == Side::Bid ? "bid" : "ask";
}

const char* action_payload_name(const AgentActionPayload& payload) {
  return std::visit([](const auto& value) -> const char* {
    using T = std::decay_t<decltype(value)>;
    return ActionPayloadTraits<T>::name.data();
  }, payload);
}

void AgentFactoryRegistry::register_factory(const std::string& type, FactoryFn fn) {
  if (type.empty()) throw std::invalid_argument("agent factory type must not be empty");
  if (!fn) throw std::invalid_argument("agent factory fn must not be empty");
  factories_[type] = std::move(fn);
}

std::unique_ptr<IAgent> AgentFactoryRegistry::create(const std::string& type,
                                                     AgentId id,
                                                     const AgentConfig& config) const {
  const auto it = factories_.find(type);
  if (it == factories_.end()) throw std::runtime_error("Unknown agent type: " + type);
  return it->second(id, config);
}

bool AgentFactoryRegistry::contains(const std::string& type) const {
  return factories_.find(type) != factories_.end();
}

void register_builtin_agents(AgentFactoryRegistry& registry) {
  register_agent_class<StaticMarketMakerAgent>(registry);
  register_agent_class<NoiseTraderAgent>(registry);
  register_agent_class<MomentumFollowerAgent>(registry);
  register_agent_class<MeanReverterAgent>(registry);
  register_agent_class<WhaleSweeperAgent>(registry);
  register_agent_class<GridBotAgent>(registry);
  register_agent_class<FundingArbitrageAgent>(registry);
  register_agent_class<LiquidationSniperAgent>(registry);
  register_agent_class<OfiMomentumAgent>(registry);
  register_agent_class<HawkesPanicAgent>(registry);
}

} // namespace lobx::agents
