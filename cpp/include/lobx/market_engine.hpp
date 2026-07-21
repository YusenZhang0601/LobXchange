#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "lob/book_core.hpp"
#include "lob/price_levels.hpp"
#include "lobx/account_ledger.hpp"
#include "lobx/event_store.hpp"
#include "lobx/market_registry.hpp"
#include "lobx/position_engine.hpp"
#include "lobx/risk_engine.hpp"

namespace lobx {

struct RawFill {
  lob::Tick price{0};
  lob::Quantity qty{0};
  lob::Side liquidity_side{lob::Side::Ask};
  OrderId passive_order_id{0};
  OrderId taker_order_id{0};
  lob::Timestamp ts{0};
};

class CollectingLogger final : public lob::IEventLogger {
public:
  void clear() { fills.clear(); cancels.clear(); }
  void log_new(const lob::NewOrder&, bool, lob::Tick, lob::Timestamp) override {}
  void log_fill(lob::Tick px, lob::Quantity qty, lob::Side liquidity_side, lob::OrderId passive_id, lob::OrderId taker_id, lob::Timestamp ts) override {
    fills.push_back(RawFill{px, qty, liquidity_side, passive_id, taker_id, ts});
  }
  void log_cancel(lob::OrderId id, lob::Timestamp) override { cancels.push_back(id); }
  std::vector<RawFill> fills;
  std::vector<lob::OrderId> cancels;
};

struct OpenOrder {
  OrderId id{0};
  UserId user{0};
  lob::SeqNo seq{0};
  lob::Side side{lob::Side::Bid};
  lob::Tick limit_price{0};
  lob::Quantity leaves_qty{0};
  lob::Timestamp ts{0};
  AssetId locked_asset{0};
  Amount locked_remaining{0};
  int leverage{1};
  uint32_t flags{lob::NONE};
};

struct SimulatedFillLeg {
  lob::Tick price{0};
  lob::Quantity qty{0};
  UserId maker_account{0};
  UserId taker_account{0};
  OrderId maker_order_id{0};
  lob::Side taker_side{lob::Side::Bid};
  Amount notional{0};
  Amount fee_estimate{0};
};

struct SimulatedFill {
  bool supported{true};
  bool would_accept{false};
  bool crosses{false};
  bool would_rest{false};
  bool fok_would_fill{false};

  lob::Quantity requested_qty{0};
  lob::Quantity fillable_qty{0};
  lob::Quantity estimated_filled_qty{0};
  lob::Quantity self_liquidity_skipped{0};

  Amount notional{0};
  Amount estimated_notional{0};
  lob::Tick worst_price{0};
  long double avg_price{0.0L};
  int levels_consumed{0};

  Amount estimated_taker_fee{0};
  Amount estimated_fee{0};
  Amount estimated_required_margin{0};
  Amount estimated_realized_pnl{0};
  Amount estimated_unrealized_pnl_after{0};
  Amount wallet_delta{0};
  Amount margin_delta{0};
  lob::Quantity position_qty_before{0};
  lob::Quantity position_qty_after{0};
  lob::Tick entry_price_before{0};
  lob::Tick entry_price_after{0};
  AssetId lock_asset{0};
  Amount required_lock{0};
  std::vector<SimulatedFillLeg> fills;

  RejectCode code{RejectCode::None};
  std::string reason;
};

struct AdlCandidate {
  UserId account_id{0};
  lob::Quantity signed_qty{0};
  Amount notional{0};
  Amount unrealized_pnl{0};
  long double pnl_ratio{0.0L};
  long double effective_leverage{0.0L};
  int rank{0};
};

enum class TriggerPriceType : uint8_t { Mark = 0, Last = 1, Index = 2 };
enum class TriggerCondition : uint8_t { AboveOrEqual = 0, BelowOrEqual = 1 };
enum class TriggerChildOrderType : uint8_t { Market = 0, Limit = 1 };
enum class TriggerOrderStatus : uint8_t { Active = 0, Triggered = 1, Cancelled = 2, Failed = 3 };

struct TriggerOrderRequest {
  MarketId market_id{0};
  UserId user{0};
  OrderId trigger_order_id{0};
  lob::Side side{lob::Side::Bid};
  lob::Quantity qty{0};
  lob::Tick trigger_price{0};
  TriggerPriceType trigger_price_type{TriggerPriceType::Mark};
  TriggerCondition trigger_condition{TriggerCondition::AboveOrEqual};
  TriggerChildOrderType child_order_type{TriggerChildOrderType::Market};
  lob::Tick child_limit_price{0};
  lob::Tick protection_price{0};
  uint32_t flags{lob::NONE};
  lob::Timestamp ts{0};
};

struct TriggerOrder {
  TriggerOrderRequest request;
  TriggerOrderStatus status{TriggerOrderStatus::Active};
  OrderId child_order_id{0};
  std::string failure_reason;
};

#ifdef LOBX_TESTING
enum class MarketEngineFaultPoint : uint8_t {
  None = 0,
  AfterBookSubmit = 1,
  AfterFeeCharge = 2,
  AfterPositionApply = 3,
  AfterReleaseAndErase = 4,
  AfterAdjustRestingLock = 5,
  BeforeInvariantCheck = 6,
  ForceLedgerInvariantFailure = 7,
};
#endif

class MarketEngine {
  enum class FaultPoint : uint8_t {
    None = 0,
    AfterBookSubmit = 1,
    AfterFeeCharge = 2,
    AfterPositionApply = 3,
    AfterReleaseAndErase = 4,
    AfterAdjustRestingLock = 5,
    BeforeInvariantCheck = 6,
    ForceLedgerInvariantFailure = 7,
  };

public:
  MarketEngine(Market market, AccountLedger& ledger, RiskEngine& risk, PositionEngine* positions = nullptr,
               EventStore* events = nullptr, const RuntimeRetentionOptions* retention_options = nullptr);

#ifdef LOBX_TESTING
  void set_fault_point_for_testing(MarketEngineFaultPoint point) {
    fault_point_ = static_cast<FaultPoint>(static_cast<uint8_t>(point));
  }
  void clear_fault_point_for_testing() { fault_point_ = FaultPoint::None; }
#endif

  const Market& market() const { return market_; }
  SubmitResult submit_limit(const OrderRequest& req);
  SubmitResult submit_market(const OrderRequest& req, lob::Tick protection_price);
  SimulatedFill simulate_fill(UserId user, lob::Side side, lob::Tick limit_price, lob::Quantity qty, uint32_t flags) const;
  SimulatedFill simulate_fill(const OrderRequest& req) const;
  bool cancel(OrderId order_id, std::optional<UserId> owner = std::nullopt, lob::Timestamp ts = 0);
  std::vector<std::pair<lob::Tick, lob::Quantity>> topN(lob::Side side, int levels);
  std::vector<OpenOrder> open_orders() const;
  lob::Tick best_bid() const { return bids_.best_bid(); }
  lob::Tick best_ask() const { return asks_.best_ask(); }
  Result set_index_price(lob::Tick price);
  lob::Tick index_price() const { return index_price_; }
  Result set_mark_price_mode(MarkPriceMode mode);
  MarkPriceMode mark_price_mode() const { return mark_price_mode_; }
  lob::Tick mark_price() const;
  lob::Tick last_trade_price() const { return last_trade_price_; }
  Amount unrealized_pnl(UserId user) const;
  Amount maintenance_margin(UserId user) const;
  Amount account_equity(UserId user) const;
  bool is_liquidatable(UserId user) const;
  Result set_risk_tiers(std::vector<PerpRiskTier> tiers);
  const std::vector<PerpRiskTier>& risk_tiers() const { return risk_tiers_; }
  Result set_fee_config(PerpFeeConfig config);
  PerpFeeConfig fee_config() const { return fee_config_; }
  Amount account_fee_total(UserId user) const;
  Result set_funding_rate(int funding_rate_bps);
  int funding_rate() const { return funding_config_.funding_rate_bps; }
  Result settle_funding(lob::Timestamp ts);
  lob::Timestamp next_funding_time() const { return funding_config_.next_funding_time; }
  Amount account_funding_total(UserId user) const;
  void set_user_leverage(UserId user, int leverage);
  int effective_max_leverage(UserId user, Amount projected_notional = 0) const;
  // Diagnostic-only bankruptcy estimate for tests/logging. This is not a
  // production liquidation routing price and ignores insurance fund, ADL,
  // partial liquidation, and funding.
  lob::Tick bankruptcy_price(UserId user) const;
  // Requires the account to be liquidatable. The current simplified model
  // performs full-position liquidation at mark price and restores ledger,
  // book/open orders, positions, and retained position margin on failure.
  Result liquidate_position(UserId user, UserId liquidator, lob::Timestamp ts = 0,
                            const LiquidationOptions* options = nullptr);
  Result credit_insurance_fund(Amount amount, lob::Timestamp ts = 0, const std::string& reason = "manual_credit");
  Amount insurance_fund_balance() const { return insurance_fund_balance_; }
  Amount bad_debt() const { return bad_debt_; }
  std::vector<AdlCandidate> rank_adl_candidates() const;
  Result create_trigger_order(const TriggerOrderRequest& req);
  bool cancel_trigger_order(OrderId trigger_order_id, std::optional<UserId> owner = std::nullopt, lob::Timestamp ts = 0);
  std::vector<TriggerOrder> trigger_orders() const;
  int evaluate_triggers(TriggerPriceType price_type, lob::Timestamp ts = 0);

private:
  struct BookRestingOrder {
    OrderId id{0};
    UserId user{0};
    lob::SeqNo seq{0};
    lob::Side side{lob::Side::Bid};
    lob::Tick price{0};
    lob::Quantity qty{0};
    lob::Timestamp ts{0};
  };

  struct PendingEvent {
    lob::Timestamp ts{0};
    std::string type;
    std::string payload;
  };

  struct Snapshot {
    std::vector<BookRestingOrder> book_orders;
    std::unordered_map<OrderId, OpenOrder> open;
    std::unordered_map<UserId, Amount> position_margin;
    std::unordered_map<UserId, Amount> fee_totals;
    std::unordered_map<UserId, Amount> funding_totals;
    Amount insurance_fund_balance{0};
    Amount bad_debt{0};
    std::unordered_map<OrderId, TriggerOrder> trigger_orders;
    AccountLedger::Snapshot ledger;
    PositionEngine::Snapshot positions;
  };

  struct SubmitSnapshot {
    std::vector<BookRestingOrder> book_orders;
    std::unordered_map<OrderId, OpenOrder> open;
    AccountLedger::Snapshot ledger;
    PositionEngine::Snapshot positions;
  };

  struct ScalarMapUndoEntry {
    UserId user{0};
    bool existed{false};
    Amount old_value{0};
  };

  struct ScalarMapUndo {
    std::vector<ScalarMapUndoEntry> position_margin;
    std::vector<ScalarMapUndoEntry> fee_totals;
    std::vector<ScalarMapUndoEntry> funding_totals;
  };

  bool settle_fill(const RawFill& fill, const OrderRequest& taker, TradeEvent& out, std::vector<PendingEvent>& pending_events);
  bool settle_spot_fill(const RawFill& fill, TradeEvent& out);
  bool settle_perpetual_fill(const RawFill& fill, TradeEvent& out, std::vector<PendingEvent>& pending_events);
  bool can_settle_perpetual_participant(const OpenOrder& order, lob::Side fill_side, lob::Tick price, lob::Quantity qty, Amount fee = 0) const;
  bool settle_perpetual_participant(OpenOrder& order, lob::Side fill_side, lob::Tick price, lob::Quantity qty);
  bool has_loss_capacity(const OrderRequest& req, int leverage) const;
  bool adjust_resting_lock(OrderId order_id);
  bool release_and_erase(OrderId order_id);
  bool cancel_order(OrderId order_id, std::optional<UserId> owner, lob::Timestamp ts, PendingEvent* pending_event);
  lob::Quantity available_to_fill(const OrderRequest& req) const;
  bool target_lock_for(const OpenOrder& order, Amount& out) const;
  bool margin_for(lob::Tick price, lob::Quantity qty, int leverage, Amount& out) const;
  bool fee_for(Amount notional, int fee_bps, Amount& out) const;
  UserId fee_account_user() const;
  lob::Tick fallback_mark_price() const;
  Amount position_notional(UserId user, lob::Tick price) const;
  Amount projected_notional_after(const OrderRequest& req) const;
  const PerpRiskTier* tier_for_notional(Amount notional) const;
  int maintenance_margin_bps_for(Amount notional) const;
  bool charge_perp_fee(UserId user, Amount fee, bool liquidation_fee, lob::Timestamp ts,
                       Amount notional, int fee_bps, std::vector<PendingEvent>* pending_events);
  bool record_funding(UserId user, Amount payment);
  bool debit_insurance_fund(Amount amount, lob::Timestamp ts, const std::string& reason, std::vector<PendingEvent>& pending_events);
  bool record_infinite_insurance_absorption(UserId user, Amount amount, lob::Timestamp ts,
                                            const std::string& reason, std::vector<PendingEvent>& pending_events) const;
  bool record_bad_debt(UserId user, Amount amount, lob::Timestamp ts, const std::string& reason, std::vector<PendingEvent>& pending_events);
  void append_adl_required_event(UserId user, Amount bad_debt_amount, Amount insurance_paid, lob::Timestamp ts, std::vector<PendingEvent>& pending_events) const;
  bool release_user_orders(UserId user);
  bool can_release_order_lock(OrderId order_id) const;
  bool purge_invalid_reduce_only_orders(const OrderRequest& req, std::vector<PendingEvent>& pending_events, const std::function<void()>& ensure_snapshot_fn = nullptr);
  Snapshot make_snapshot() const;
  void restore_snapshot(const Snapshot& snapshot);
  SubmitSnapshot make_submit_snapshot() const;
  void restore_submit_snapshot(const SubmitSnapshot& snapshot);
  static void remember_scalar_value(std::unordered_map<UserId, Amount>& map,
                                    std::vector<ScalarMapUndoEntry>& undo,
                                    UserId user);
  static void restore_scalar_values(std::unordered_map<UserId, Amount>& map,
                                    const std::vector<ScalarMapUndoEntry>& undo);
  void restore_scalar_map_undo(const ScalarMapUndo& undo);
  void remember_position_margin(UserId user);
  void remember_fee_total(UserId user);
  void remember_funding_total(UserId user);
  bool build_event_payloads() const;
  bool fault_active(FaultPoint point) const { return fault_point_ == point; }
  void reset_book();
  void rebuild_book(const std::vector<BookRestingOrder>& orders);
  void append_event(lob::Timestamp ts, const std::string& type, const std::string& payload);
  void append_reject_event(const OrderRequest& req, RejectCode code, const std::string& reason);
  void append_trigger_event(lob::Timestamp ts, const std::string& type, const TriggerOrder& trigger, const std::string& extra = "");
  std::string order_payload(const OrderRequest& req) const;
  std::string trade_payload(const TradeEvent& trade) const;
  lob::Tick trigger_reference_price(TriggerPriceType price_type) const;
  bool trigger_condition_met(const TriggerOrder& trigger, lob::Tick reference) const;

  Market market_;
  AccountLedger& ledger_;
  RiskEngine& risk_;
  PositionEngine* positions_{nullptr};
  EventStore* events_{nullptr};
  const RuntimeRetentionOptions* retention_options_{nullptr};
  lob::PriceLevelsSparse bids_;
  lob::PriceLevelsSparse asks_;
  CollectingLogger logger_;
  std::unique_ptr<lob::BookCore> book_;
  std::unordered_map<OrderId, OpenOrder> open_;
  std::unordered_set<OrderId> seen_order_ids_;
  std::unordered_map<UserId, Amount> position_margin_;
  lob::Tick index_price_{0};
  lob::Tick last_trade_price_{0};
  MarkPriceMode mark_price_mode_{MarkPriceMode::LastTrade};
  std::vector<PerpRiskTier> risk_tiers_;
  PerpFeeConfig fee_config_;
  PerpFundingConfig funding_config_;
  std::unordered_map<UserId, Amount> fee_totals_;
  std::unordered_map<UserId, Amount> funding_totals_;
  Amount insurance_fund_balance_{0};
  Amount bad_debt_{0};
  std::unordered_map<OrderId, TriggerOrder> trigger_orders_;
  ScalarMapUndo* active_scalar_undo_{nullptr};
  FaultPoint fault_point_{FaultPoint::None};
};

} // namespace lobx
