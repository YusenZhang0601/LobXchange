#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "lobx/agents/agent.hpp"
#include "lobx/exchange.hpp"
#include "lobx/simulation/action_queue.hpp"
#include "lobx/simulation/agent_state_store.hpp"
#include "lobx/simulation/market_view.hpp"
#include "lobx/simulation/simulation_event.hpp"

namespace lobx::simulation {

struct AgentRuntimeConfig {
  std::string symbol{"BTC-USDT"};
  std::string base_asset{"BTC"};
  std::string quote_asset{"USDT"};
  lobx::agents::Price reference_price{100};
  int steps{0};
  int book_levels{10};
  int recent_trade_limit{128};
  lobx::Amount initial_quote{1000000000LL};
  lobx::Amount initial_base{1000000LL};
  lobx::agents::Timestamp action_latency{0};
  lobx::agents::EnvironmentView environment;
  bool retain_action_trace{true};
  bool retain_events{true};
  bool enable_scheduler{false};
  int default_decision_interval_steps{1};
  std::unordered_map<lobx::agents::AgentType, int> decision_interval_by_type;
};

struct AgentRuntimeSummary {
  int steps{0};
  int agent_count{0};
  int action_count{0};
  int accepted_orders{0};
  int rejected_orders{0};
  int canceled_orders{0};
  int trade_count{0};
  int event_count{0};
  lobx::agents::Quantity cum_volume{0};
  lobx::agents::Price final_best_bid{0};
  lobx::agents::Price final_best_ask{0};
};

struct AgentEquitySnapshot {
  lobx::agents::AgentId agent_id{0};
  lobx::agents::AgentType agent_type{lobx::agents::AgentType::Unknown};
  double initial_cash{0.0};
  double initial_inventory{0.0};
  double initial_equity{0.0};
  double final_equity{0.0};
  double pnl{0.0};

  double total_cash{0.0};
  double locked_cash{0.0};
  double free_cash{0.0};
  double total_inventory{0.0};
  double locked_inventory{0.0};
  double free_inventory{0.0};
  int open_order_count{0};
  int trade_count{0};
  double buy_volume{0.0};
  double sell_volume{0.0};
  double fees_paid{0.0};
};

struct AccountingSummary {
  double sum_initial_equity{0.0};
  double sum_final_equity{0.0};
  double sum_agent_pnl{0.0};
  double exchange_fee_revenue{0.0};
  double house_account_pnl{0.0};
  double insurance_fund_pnl{0.0};
  double system_pnl_residual{0.0};
  int negative_pnl_agent_count{0};
  int positive_pnl_agent_count{0};
  int zero_pnl_agent_count{0};
  int agent_count{0};
  double sum_cash_free{0.0};
  double sum_cash_locked{0.0};
  double sum_cash_total{0.0};
  double sum_inventory_free{0.0};
  double sum_inventory_locked{0.0};
  double sum_inventory_total{0.0};
  double mark_price{0.0};
  std::string mark_price_policy{"mid_price"};
};

struct AgentRuntimeActionCounts {
  int total_actions{0};
  int submit_limit_order{0};
  int submit_market_order{0};
  int cancel_order{0};
  int replace_order{0};
  int cancel_all_orders{0};
  int sleep_until{0};
};

struct AgentRuntimeTradeStepStats {
  int trade_count{0};
  double volume{0.0};
  double buy_aggressor_volume{0.0};
  double sell_aggressor_volume{0.0};
  double notional{0.0};
  double vwap{0.0};
  double min_price{0.0};
  double max_price{0.0};
  double last_trade_price{0.0};
};

struct AgentRuntimeStepStats {
  lobx::agents::Timestamp ts{0};
  AgentRuntimeActionCounts actions;
  std::unordered_map<lobx::agents::AgentType, AgentRuntimeActionCounts> actions_by_type;
  AgentRuntimeTradeStepStats trades;
  int accepted_orders{0};
  int rejected_orders{0};
  int canceled_orders{0};
  int agents_due_count{0};
  int agents_skipped_count{0};
  int agent_decisions_count{0};
  double agent_decide_ms{0.0};
  double action_schedule_ms{0.0};
  double exchange_apply_ms{0.0};
  double state_update_ms{0.0};
  double book_sample_ms{0.0};
};

struct AgentRuntimeOpenOrderSummary {
  int total_open_orders{0};
  int open_bid_orders{0};
  int open_ask_orders{0};
  double mean_open_orders_per_agent{0.0};
  int max_open_orders_per_agent{0};
  int stale_order_count{0};
};

// New agent simulation runtime. It is the only layer that converts AgentAction
// into Exchange commands and applies latency.
class AgentRuntime {
public:
  explicit AgentRuntime(AgentRuntimeConfig config = {});

  void add_agent(std::unique_ptr<lobx::agents::IAgent> agent);
  void run();
  void run_steps(int steps);
  void step();

  const AgentRuntimeSummary& summary() const { return summary_; }
  const std::vector<lobx::agents::AgentAction>& action_trace() const { return action_trace_; }
  const std::vector<SimulationEvent>& events() const { return events_; }
  const MarketView& market_view() const { return market_view_; }
  const AgentRuntimeStepStats& last_step_stats() const { return last_step_stats_; }
  int open_order_count() const;
  AgentRuntimeOpenOrderSummary open_order_summary(int stale_after_steps = 0) const;
  std::vector<AgentEquitySnapshot> agent_equity_snapshots(double mark_price) const;
  AccountingSummary accounting_summary(double mark_price) const;
  double total_agent_equity(double mark_price) const;

  std::string action_trace_jsonl() const;
  std::string event_trace_jsonl() const;
  bool write_action_trace_jsonl(const std::string& path) const;
  bool write_event_trace_jsonl(const std::string& path) const;

private:
  struct OrderRef {
    lobx::agents::AgentId agent_id{0};
    lobx::agents::ClientOrderId client_order_id{0};
    lobx::agents::Side side{lob::Side::Bid};
    std::string symbol;
  };

  void bootstrap_exchange();
  void update_market_view();
  void schedule_actions(const std::vector<lobx::agents::AgentAction>& actions);
  void execute_due_actions();
  void execute_action(const ScheduledAction& scheduled);
  void execute_submit_limit(const ScheduledAction& scheduled, const lobx::agents::SubmitLimitOrder& order);
  void execute_submit_market(const ScheduledAction& scheduled, const lobx::agents::SubmitMarketOrder& order);
  void execute_cancel(const ScheduledAction& scheduled, const lobx::agents::CancelOrder& cancel);
  void execute_replace(const ScheduledAction& scheduled, const lobx::agents::ReplaceOrder& replace);
  void execute_cancel_all(const ScheduledAction& scheduled, const lobx::agents::CancelAllOrders& cancel_all);
  void account_submit_result(const ScheduledAction& scheduled,
                             lobx::OrderId exchange_order_id,
                             lobx::agents::ClientOrderId client_order_id,
                             lobx::agents::Side side,
                             lobx::agents::Price price,
                             const std::string& symbol,
                             const lobx::SubmitResult& result);
  void account_trade(const lobx::TradeEvent& trade);
  void record_event(SimulationEvent event);
  void record_book_snapshot();

  lobx::agents::ClientOrderId client_order_id_for(const lobx::agents::AgentAction& action,
                                                   lobx::agents::ClientOrderId explicit_id) const;
  lobx::agents::Price protection_price(lobx::agents::Side side) const;
  std::uint32_t flags_for(const lobx::agents::SubmitLimitOrder& order) const;
  lobx::OrderId next_exchange_order_id();
  void count_action(lobx::agents::AgentType agent_type, const lobx::agents::AgentActionPayload& payload);
  int decision_interval_for(lobx::agents::AgentType type) const;

  AgentRuntimeConfig config_;
  lobx::Exchange exchange_;
  std::vector<std::unique_ptr<lobx::agents::IAgent>> agents_;
  ActionQueue action_queue_;
  AgentStateStore state_store_;

  lobx::agents::Timestamp now_{0};
  std::uint64_t next_queue_seq_{1};
  std::uint64_t next_event_seq_{1};
  lobx::OrderId next_order_id_{1};

  std::vector<BookLevelView> bid_cache_;
  std::vector<BookLevelView> ask_cache_;
  std::vector<TradePrintView> recent_trades_;
  MarketView market_view_;

  std::unordered_map<lobx::OrderId, OrderRef> by_exchange_order_id_;
  std::unordered_map<lobx::agents::AgentId, std::unordered_map<lobx::agents::ClientOrderId, lobx::OrderId>> client_to_exchange_;

  std::vector<lobx::agents::AgentAction> action_trace_;
  std::vector<SimulationEvent> events_;
  AgentRuntimeSummary summary_;
  AgentRuntimeStepStats last_step_stats_;
  std::unordered_map<lobx::agents::AgentId, lobx::agents::Timestamp> next_decision_step_;

  struct CumulativeAgentStats {
    int trade_count{0};
    double buy_volume{0.0};
    double sell_volume{0.0};
    double fees_paid{0.0};
  };
  std::unordered_map<lobx::agents::AgentId, CumulativeAgentStats> cumulative_agent_stats_;
};

} // namespace lobx::simulation
