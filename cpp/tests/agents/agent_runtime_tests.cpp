#include "lobx/agents/agent.hpp"
#include "lobx/agents/agent_factory.hpp"
#include "lobx/agents/builtins/mean_reverter_agent.hpp"
#include "lobx/agents/builtins/momentum_follower_agent.hpp"
#include "lobx/agents/builtins/noise_trader_agent.hpp"
#include "lobx/agents/builtins/static_market_maker_agent.hpp"
#include "lobx/agents/builtins/whale_sweeper_agent.hpp"
#include "lobx/simulation/agent_runtime.hpp"

#include "test_helpers/test_framework.hpp"

#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lobx::agents;
using namespace lobx::simulation;

namespace {

class DummyAgent final : public IAgent {
public:
  explicit DummyAgent(AgentId id) : id_(id) {}
  AgentId id() const override { return id_; }
  AgentType type() const override { return AgentType::Unknown; }
  std::vector<AgentAction> decide(const AgentContext&) override { return {}; }

private:
  AgentId id_{0};
};

class ScriptedAgent final : public IAgent {
public:
  explicit ScriptedAgent(AgentId id, std::vector<AgentAction> actions)
      : id_(id), actions_(std::move(actions)) {}

  AgentId id() const override { return id_; }
  AgentType type() const override { return AgentType::Unknown; }

  std::vector<AgentAction> decide(const AgentContext& ctx) override {
    std::vector<AgentAction> out;
    for (AgentAction& action : actions_) {
      if (action.decision_ts == ctx.now) out.push_back(action);
    }
    return out;
  }

private:
  AgentId id_{0};
  std::vector<AgentAction> actions_;
};

MarketView market_view(std::vector<BookLevelView>& bids,
                       std::vector<BookLevelView>& asks,
                       std::vector<TradePrintView>& trades,
                       double mid = 100.0,
                       Timestamp ts = 1) {
  return MarketView{"BTC-USDT",
                    std::span<const BookLevelView>(bids.data(), bids.size()),
                    std::span<const BookLevelView>(asks.data(), asks.size()),
                    std::span<const TradePrintView>(trades.data(), trades.size()),
                    bids.empty() ? 0.0 : bids.front().price,
                    asks.empty() ? 0.0 : asks.front().price,
                    mid,
                    200.0,
                    ts,
                    ts};
}

PrivateAgentState private_state(AgentId id) {
  static const std::vector<AgentOrderView> orders;
  static const std::vector<AgentFillView> fills;
  return PrivateAgentState{id,
                           0.0,
                           0.0,
                           0.0,
                           0,
                           0,
                           0.0,
                           std::span<const AgentOrderView>(orders.data(), orders.size()),
                           std::span<const AgentFillView>(fills.data(), fills.size()),
                           0,
                           0};
}

AgentContext context_for(AgentId id,
                         MarketView& view,
                         Timestamp now = 1) {
  static const EnvironmentView env{};
  static const PrivateAgentState state = private_state(id);
  return AgentContext{now, view, state, env, {}};
}

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool contains_file_text(const std::string& path, const std::string& needle) {
  return read_file(path).find(needle) != std::string::npos;
}

} // namespace

TEST(AgentFactoryRegistryTests, CreateRegisteredAgent) {
  AgentFactoryRegistry registry;
  registry.register_factory("dummy", [](AgentId id, const AgentConfig&) {
    return std::make_unique<DummyAgent>(id);
  });

  AgentConfig config{};
  config.type = "dummy";
  std::unique_ptr<IAgent> agent = registry.create("dummy", 42, config);

  EXPECT_TRUE(registry.contains("dummy"));
  EXPECT_EQ(agent->id(), static_cast<AgentId>(42));
}

TEST(AgentFactoryRegistryTests, UnknownTypeThrowsInsteadOfFallback) {
  AgentFactoryRegistry registry;
  register_builtin_agents(registry);

  bool threw = false;
  try {
    AgentConfig config{};
    (void)registry.create("not_registered", 1, config);
  } catch (const std::runtime_error& e) {
    threw = std::string(e.what()).find("Unknown agent type: not_registered") != std::string::npos;
  }
  EXPECT_TRUE(threw);
}

TEST(AgentFactoryRegistryTests, AgentTypeCanonicalNamesUseTable) {
  EXPECT_EQ(std::string(agent_type_name(AgentType::StaticMarketMaker)), std::string("static_market_maker"));
  EXPECT_EQ(std::string(agent_type_name(AgentType::NoiseTrader)), std::string("noise_trader"));
  EXPECT_EQ(std::string(agent_type_name(AgentType::MomentumFollower)), std::string("momentum_follower"));
  EXPECT_EQ(std::string(agent_type_name(AgentType::MeanReverter)), std::string("mean_reverter"));
  EXPECT_EQ(std::string(agent_type_name(AgentType::WhaleSweeper)), std::string("whale_sweeper"));
  EXPECT_EQ(std::string(agent_type_name(AgentType::DynamicMarketMaker)), std::string("dynamic_market_maker"));
  EXPECT_EQ(std::string(agent_type_name(AgentType::LiquidityTaker)), std::string("liquidity_taker"));
  EXPECT_EQ(std::string(agent_type_name(AgentType::AdversarialSweeper)), std::string("adversarial_sweeper"));
  EXPECT_EQ(std::string(agent_type_name(AgentType::LiquidityWithdrawer)), std::string("liquidity_withdrawer"));
  EXPECT_EQ(std::string(agent_type_name(AgentType::Unknown)), std::string("unknown"));
}

TEST(AgentFactoryRegistryTests, AgentTypeAliasesParseThroughTable) {
  EXPECT_EQ(agent_type_from_name("static_market_maker"), AgentType::StaticMarketMaker);
  EXPECT_EQ(agent_type_from_name("market_maker"), AgentType::StaticMarketMaker);
  EXPECT_EQ(agent_type_from_name("noise_trader"), AgentType::NoiseTrader);
  EXPECT_EQ(agent_type_from_name("momentum_follower"), AgentType::MomentumFollower);
  EXPECT_EQ(agent_type_from_name("momentum"), AgentType::MomentumFollower);
  EXPECT_EQ(agent_type_from_name("mean_reverter"), AgentType::MeanReverter);
  EXPECT_EQ(agent_type_from_name("mean_reversion"), AgentType::MeanReverter);
  EXPECT_EQ(agent_type_from_name("whale_sweeper"), AgentType::WhaleSweeper);
  EXPECT_EQ(agent_type_from_name("whale"), AgentType::WhaleSweeper);
  EXPECT_EQ(agent_type_from_name("dynamic_market_maker"), AgentType::DynamicMarketMaker);
  EXPECT_EQ(agent_type_from_name("liquidity_taker"), AgentType::LiquidityTaker);
  EXPECT_EQ(agent_type_from_name("taker_sweep"), AgentType::LiquidityTaker);
  EXPECT_EQ(agent_type_from_name("adversarial_sweeper"), AgentType::AdversarialSweeper);
  EXPECT_EQ(agent_type_from_name("liquidity_withdrawer"), AgentType::LiquidityWithdrawer);
  EXPECT_EQ(agent_type_from_name("unknown_xyz"), AgentType::Unknown);
}

TEST(AgentFactoryRegistryTests, BuiltinRegistrationIncludesAliasesButNotFutureTypes) {
  AgentFactoryRegistry registry;
  register_builtin_agents(registry);

  EXPECT_TRUE(registry.contains("static_market_maker"));
  EXPECT_TRUE(registry.contains("market_maker"));
  EXPECT_TRUE(registry.contains("noise_trader"));
  EXPECT_TRUE(registry.contains("momentum_follower"));
  EXPECT_TRUE(registry.contains("momentum"));
  EXPECT_TRUE(registry.contains("mean_reverter"));
  EXPECT_TRUE(registry.contains("mean_reversion"));
  EXPECT_TRUE(registry.contains("whale_sweeper"));
  EXPECT_TRUE(registry.contains("whale"));

  EXPECT_FALSE(registry.contains("dynamic_market_maker"));
  EXPECT_FALSE(registry.contains("liquidity_taker"));
  EXPECT_FALSE(registry.contains("taker_sweep"));
  EXPECT_FALSE(registry.contains("adversarial_sweeper"));
  EXPECT_FALSE(registry.contains("liquidity_withdrawer"));
}

TEST(AgentFactoryRegistryTests, FutureTypeCreateStillThrowsWhenNotImplemented) {
  AgentFactoryRegistry registry;
  register_builtin_agents(registry);

  bool threw = false;
  try {
    AgentConfig config{};
    (void)registry.create("adversarial_sweeper", 9, config);
  } catch (const std::runtime_error& e) {
    threw = std::string(e.what()).find("Unknown agent type: adversarial_sweeper") != std::string::npos;
  }
  EXPECT_TRUE(threw);
}

TEST(AgentFactoryRegistryTests, ActionPayloadNamesUseTraits) {
  EXPECT_EQ(std::string(action_payload_name(AgentActionPayload{SubmitLimitOrder{}})), std::string("submit_limit_order"));
  EXPECT_EQ(std::string(action_payload_name(AgentActionPayload{SubmitMarketOrder{}})), std::string("submit_market_order"));
  EXPECT_EQ(std::string(action_payload_name(AgentActionPayload{CancelOrder{}})), std::string("cancel_order"));
  EXPECT_EQ(std::string(action_payload_name(AgentActionPayload{ReplaceOrder{}})), std::string("replace_order"));
  EXPECT_EQ(std::string(action_payload_name(AgentActionPayload{CancelAllOrders{}})), std::string("cancel_all_orders"));
  EXPECT_EQ(std::string(action_payload_name(AgentActionPayload{SleepUntil{}})), std::string("sleep_until"));
}

TEST(BuiltinAgentTests, StaticMarketMakerReturnsBidAndAskLimitOrders) {
  AgentConfig config{};
  config.group_id = 7;
  StaticMarketMakerAgent agent(1, config);
  std::vector<BookLevelView> bids{{99, 5}};
  std::vector<BookLevelView> asks{{101, 5}};
  std::vector<TradePrintView> trades;
  MarketView view = market_view(bids, asks, trades);

  const std::vector<AgentAction> actions = agent.decide(context_for(1, view));

  EXPECT_EQ(actions.size(), static_cast<size_t>(2));
  EXPECT_TRUE(std::holds_alternative<SubmitLimitOrder>(actions[0].payload));
  EXPECT_TRUE(std::holds_alternative<SubmitLimitOrder>(actions[1].payload));
  EXPECT_EQ(actions[0].reason_tag, std::string("static_quote"));
}

TEST(BuiltinAgentTests, BoundedStaticMarketMakerEmitsCancelForStaleQuote) {
  AgentConfig config{};
  config.group_id = 7;
  config.numeric_params["bounded_quotes"] = 1.0;
  config.numeric_params["quote_ttl_steps"] = 5.0;
  config.numeric_params["quote_refresh_interval_steps"] = 5.0;
  StaticMarketMakerAgent agent(1, config);
  std::vector<BookLevelView> bids{{99, 5}};
  std::vector<BookLevelView> asks{{101, 5}};
  std::vector<TradePrintView> trades;
  MarketView view = market_view(bids, asks, trades, 100.0, 10);
  std::vector<AgentOrderView> open_orders{{42, "BTC-USDT", Side::Bid, 98, 2, 1},
                                          {43, "BTC-USDT", Side::Ask, 102, 2, 1}};
  std::vector<AgentFillView> fills;
  PrivateAgentState state{1,
                          0.0,
                          0.0,
                          0.0,
                          0,
                          0,
                          0.0,
                          std::span<const AgentOrderView>(open_orders.data(), open_orders.size()),
                          std::span<const AgentFillView>(fills.data(), fills.size()),
                          0,
                          0};
  const EnvironmentView env{};
  AgentContext ctx{10, view, state, env, {}};

  const auto actions = agent.decide(ctx);

  int cancels = 0;
  int submits = 0;
  for (const AgentAction& action : actions) {
    if (std::holds_alternative<CancelOrder>(action.payload)) ++cancels;
    if (std::holds_alternative<SubmitLimitOrder>(action.payload)) ++submits;
  }
  EXPECT_EQ(cancels, 2);
  EXPECT_EQ(submits, 2);
}

TEST(BuiltinAgentTests, NoiseTraderDeterministicForFixedSeed) {
  AgentConfig config{};
  config.numeric_params["seed"] = 99;
  NoiseTraderAgent a(1, config);
  NoiseTraderAgent b(1, config);
  std::vector<BookLevelView> bids{{99, 5}};
  std::vector<BookLevelView> asks{{101, 5}};
  std::vector<TradePrintView> trades;
  MarketView view = market_view(bids, asks, trades);

  const auto aa = a.decide(context_for(1, view));
  const auto bb = b.decide(context_for(1, view));
  const auto& pa = std::get<SubmitLimitOrder>(aa.front().payload);
  const auto& pb = std::get<SubmitLimitOrder>(bb.front().payload);

  EXPECT_EQ(pa.side, pb.side);
  EXPECT_EQ(pa.price, pb.price);
  EXPECT_EQ(pa.tif, pb.tif);
}

TEST(BuiltinAgentTests, MomentumFollowerBuysAfterUpTick) {
  AgentConfig config{};
  MomentumFollowerAgent agent(1, config);
  std::vector<BookLevelView> bids{{99, 5}};
  std::vector<BookLevelView> asks{{101, 5}};
  std::vector<TradePrintView> trades{{100, 1, 1, 1}, {101, 1, 1, 2}};
  MarketView view = market_view(bids, asks, trades);

  const auto actions = agent.decide(context_for(1, view, 2));

  EXPECT_EQ(actions.size(), static_cast<size_t>(1));
  EXPECT_EQ(std::get<SubmitLimitOrder>(actions.front().payload).side, Side::Bid);
  EXPECT_EQ(actions.front().reason_tag, std::string("momentum_follow"));
}

TEST(BuiltinAgentTests, MeanReverterSellsAboveReference) {
  AgentConfig config{};
  config.numeric_params["reference_price"] = 100;
  MeanReverterAgent agent(1, config);
  std::vector<BookLevelView> bids{{103, 5}};
  std::vector<BookLevelView> asks{{105, 5}};
  std::vector<TradePrintView> trades{{104, 1, 1, 1}};
  MarketView view = market_view(bids, asks, trades, 104.0);

  const auto actions = agent.decide(context_for(1, view));

  EXPECT_EQ(actions.size(), static_cast<size_t>(1));
  EXPECT_EQ(std::get<SubmitLimitOrder>(actions.front().payload).side, Side::Ask);
  EXPECT_EQ(actions.front().reason_tag, std::string("mean_reversion"));
}

TEST(BuiltinAgentTests, WhaleSweeperTriggersOnConfiguredInterval) {
  AgentConfig config{};
  config.numeric_params["seed"] = 7;
  config.numeric_params["interval"] = 12;
  WhaleSweeperAgent agent(1, config);
  std::vector<BookLevelView> bids{{99, 5}};
  std::vector<BookLevelView> asks{{101, 5}};
  std::vector<TradePrintView> trades;
  MarketView view = market_view(bids, asks, trades);

  EXPECT_TRUE(agent.decide(context_for(1, view, 11)).empty());
  const auto actions = agent.decide(context_for(1, view, 12));

  EXPECT_EQ(actions.size(), static_cast<size_t>(1));
  EXPECT_TRUE(std::holds_alternative<SubmitMarketOrder>(actions.front().payload));
  EXPECT_EQ(actions.front().reason_tag, std::string("whale_sweep"));
}

TEST(AgentRuntimeTests, RunsSmallAgentSimulationAndRecordsTrace) {
  AgentRuntimeConfig config{};
  config.steps = 30;
  config.reference_price = 100;
  AgentRuntime runtime(config);
  AgentFactoryRegistry registry;
  register_builtin_agents(registry);

  AgentConfig maker{};
  maker.type = "static_market_maker";
  runtime.add_agent(registry.create("static_market_maker", 100, maker));
  runtime.add_agent(registry.create("static_market_maker", 101, maker));

  AgentConfig noise{};
  noise.type = "noise_trader";
  noise.numeric_params["seed"] = 42;
  runtime.add_agent(registry.create("noise_trader", 120, noise));
  noise.numeric_params["seed"] = 43;
  runtime.add_agent(registry.create("noise_trader", 121, noise));

  AgentConfig whale{};
  whale.type = "whale_sweeper";
  whale.numeric_params["seed"] = 9;
  runtime.add_agent(registry.create("whale_sweeper", 200, whale));

  runtime.run();
  const AgentRuntimeSummary& summary = runtime.summary();

  EXPECT_EQ(summary.agent_count, 5);
  EXPECT_TRUE(summary.action_count > 0);
  EXPECT_TRUE(summary.accepted_orders > 0);
  EXPECT_TRUE(summary.event_count > 0);
  EXPECT_TRUE(runtime.action_trace_jsonl().find("static_quote") != std::string::npos);
  EXPECT_TRUE(runtime.event_trace_jsonl().find("order_accepted") != std::string::npos);
}

TEST(AgentRuntimeTests, SubmitLimitOrderCanConvertToExchangeCommand) {
  AgentRuntimeConfig config{};
  config.steps = 1;
  AgentRuntime runtime(config);

  SubmitLimitOrder order{};
  order.client_order_id = 77;
  order.symbol = "BTC-USDT";
  order.side = Side::Bid;
  order.price = 99;
  order.quantity = 1;
  order.post_only = true;

  AgentAction action{};
  action.agent_id = 300;
  action.agent_type = AgentType::Unknown;
  action.decision_ts = 1;
  action.client_action_id = 1;
  action.payload = order;
  action.reason_tag = "script_submit_limit";

  runtime.add_agent(std::make_unique<ScriptedAgent>(300, std::vector<AgentAction>{action}));
  runtime.run();

  EXPECT_EQ(runtime.summary().accepted_orders, 1);
  EXPECT_TRUE(runtime.event_trace_jsonl().find("order_accepted") != std::string::npos);
}

TEST(AgentRuntimeTests, SubmitMarketOrderCanConvertToExchangeCommand) {
  AgentRuntimeConfig config{};
  config.steps = 15;
  AgentRuntime runtime(config);
  AgentFactoryRegistry registry;
  register_builtin_agents(registry);

  AgentConfig maker{};
  runtime.add_agent(registry.create("static_market_maker", 100, maker));
  runtime.add_agent(registry.create("static_market_maker", 101, maker));
  AgentConfig whale{};
  whale.numeric_params["seed"] = 1;
  whale.numeric_params["interval"] = 1;
  whale.numeric_params["quantity"] = 1;
  runtime.add_agent(registry.create("whale_sweeper", 200, whale));

  runtime.run();

  EXPECT_TRUE(runtime.summary().accepted_orders > 0);
  EXPECT_TRUE(runtime.action_trace_jsonl().find("submit_market_order") != std::string::npos);
}

TEST(AgentRuntimeTests, ReplaceOrderConvertsToCancelAndSubmit) {
  AgentRuntimeConfig config{};
  config.steps = 2;
  AgentRuntime runtime(config);

  SubmitLimitOrder initial{};
  initial.client_order_id = 77;
  initial.symbol = "BTC-USDT";
  initial.side = Side::Ask;
  initial.price = 105;
  initial.quantity = 1;
  initial.post_only = true;

  AgentAction submit{};
  submit.agent_id = 300;
  submit.agent_type = AgentType::Unknown;
  submit.decision_ts = 1;
  submit.client_action_id = 1;
  submit.payload = initial;
  submit.reason_tag = "script_submit_limit";

  ReplaceOrder replace{};
  replace.old_client_order_id = 77;
  replace.new_price = 106;
  replace.new_quantity = 1;

  AgentAction replace_action{};
  replace_action.agent_id = 300;
  replace_action.agent_type = AgentType::Unknown;
  replace_action.decision_ts = 2;
  replace_action.client_action_id = 2;
  replace_action.payload = replace;
  replace_action.reason_tag = "script_replace";

  runtime.add_agent(std::make_unique<ScriptedAgent>(300, std::vector<AgentAction>{submit, replace_action}));
  runtime.run();

  EXPECT_EQ(runtime.summary().accepted_orders, 2);
  EXPECT_EQ(runtime.summary().canceled_orders, 1);
  EXPECT_TRUE(runtime.event_trace_jsonl().find("order_canceled") != std::string::npos);
}

TEST(AgentDecouplingTests, AgentHeadersDoNotIncludeExchangeOrMarketEngine) {
  EXPECT_FALSE(contains_file_text("cpp/include/lobx/agents/agent.hpp", "exchange.hpp"));
  EXPECT_FALSE(contains_file_text("cpp/include/lobx/agents/agent.hpp", "market_engine.hpp"));
  EXPECT_FALSE(contains_file_text("cpp/include/lobx/agents/builtins/static_market_maker_agent.hpp", "exchange.hpp"));
  EXPECT_FALSE(contains_file_text("cpp/include/lobx/agents/builtins/noise_trader_agent.hpp", "market_engine.hpp"));
}
