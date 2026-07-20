#pragma once

#include <memory>
#include <unordered_map>

#include "lobx/asset_registry.hpp"
#include "lobx/event_store.hpp"
#include "lobx/kline_aggregator.hpp"
#include "lobx/market_engine.hpp"
#include "lobx/position_engine.hpp"

namespace lobx {

class Exchange {
public:
  Exchange();

  void set_retention_options(RuntimeRetentionOptions options);
  const RuntimeRetentionOptions& retention_options() const { return *retention_options_; }
  void set_liquidation_options(LiquidationOptions options);
  const LiquidationOptions& liquidation_options() const { return liquidation_options_; }

#ifdef LOBX_TESTING
  void set_market_fault_point_for_testing(const std::string& market_symbol, MarketEngineFaultPoint point) {
    if (MarketEngine* engine = engine_for_symbol(market_symbol)) {
      engine->set_fault_point_for_testing(point);
    }
  }
  void clear_market_fault_point_for_testing(const std::string& market_symbol) {
    if (MarketEngine* engine = engine_for_symbol(market_symbol)) {
      engine->clear_fault_point_for_testing();
    }
  }
#endif

  Result issue_asset(const std::string& symbol, uint8_t decimals, Amount max_supply, UserId issuer, Amount initial_supply, AssetId* out_id = nullptr);
  Result create_spot_market(const std::string& symbol, const std::string& base_symbol, const std::string& quote_symbol,
                            lob::Tick tick_size, lob::Quantity lot_size, lob::Quantity min_qty, Amount min_notional,
                            MarketId* out_id = nullptr);
  Result create_perpetual_market(const std::string& symbol, const std::string& base_symbol, const std::string& quote_symbol,
                                  const std::string& margin_symbol, lob::Tick tick_size, lob::Quantity lot_size,
                                  lob::Quantity min_qty, Amount min_notional, int max_leverage,
                                  MarketId* out_id = nullptr);
  Result deposit(UserId user, const std::string& symbol, Amount amount);
  SubmitResult submit_limit(const std::string& market_symbol, UserId user, OrderId order_id, lob::Side side,
                            lob::Tick price, lob::Quantity qty, uint32_t flags = lob::NONE, lob::Timestamp ts = 0);
  SubmitResult submit_market(const std::string& market_symbol, UserId user, OrderId order_id, lob::Side side,
                             lob::Quantity qty, lob::Tick protection_price, uint32_t flags = lob::NONE, lob::Timestamp ts = 0);
  SimulatedFill simulate_fill(const std::string& market_symbol, UserId user, lob::Side side,
                              lob::Tick limit_price, lob::Quantity qty, uint32_t flags = lob::NONE) const;
  bool cancel(const std::string& market_symbol, OrderId order_id);
  bool cancel(const std::string& market_symbol, UserId user, OrderId order_id, lob::Timestamp ts = 0);

  void set_leverage(UserId user, const std::string& market_symbol, int leverage);
  Result set_index_price(MarketId market_id, lob::Tick price);
  lob::Tick get_index_price(MarketId market_id) const;
  Result set_mark_price_mode(MarketId market_id, MarkPriceMode mode);
  lob::Tick mark_price(MarketId market_id) const;
  Amount unrealized_pnl(UserId user, const std::string& market_symbol) const;
  Amount maintenance_margin(UserId user, const std::string& market_symbol) const;
  Amount account_equity(UserId user, const std::string& market_symbol) const;
  bool is_liquidatable(UserId user, const std::string& market_symbol) const;
  Result set_perp_risk_tiers(MarketId market_id, std::vector<PerpRiskTier> tiers);
  Result set_perp_fee_config(const std::string& market_symbol, PerpFeeConfig config);
  PerpFeeConfig get_perp_fee_config(const std::string& market_symbol) const;
  Amount account_fee_total(UserId user, const std::string& market_symbol) const;
  Result set_funding_rate(const std::string& market_symbol, int funding_rate_bps);
  int get_funding_rate(const std::string& market_symbol) const;
  Result settle_funding(const std::string& market_symbol, lob::Timestamp ts);
  lob::Timestamp next_funding_time(const std::string& market_symbol) const;
  Amount account_funding_total(UserId user, const std::string& market_symbol) const;
  // Diagnostic-only bankruptcy estimate for tests/logging. This is not a
  // production liquidation routing price and ignores insurance fund, ADL,
  // partial liquidation, and funding.
  lob::Tick bankruptcy_price(UserId user, const std::string& market_symbol) const;
  // Requires the account to be liquidatable. The current simplified model
  // performs full-position liquidation at mark price and restores ledger,
  // book/open orders, positions, and retained position margin on failure.
  Result liquidate_position(const std::string& market_symbol, UserId user, UserId liquidator, lob::Timestamp ts = 0);
  Result liquidate_if_needed(const std::string& market_symbol, UserId user, lob::Timestamp ts = 0);
  Result credit_insurance_fund(const std::string& market_symbol, Amount amount, const std::string& reason = "manual_credit", lob::Timestamp ts = 0);
  Amount insurance_fund_balance(const std::string& market_symbol) const;
  Amount bad_debt(const std::string& market_symbol) const;
  std::vector<AdlCandidate> rank_adl_candidates(const std::string& market_symbol) const;
  Result create_trigger_order(const std::string& market_symbol, UserId user, OrderId trigger_order_id,
                              lob::Side side, lob::Quantity qty, lob::Tick trigger_price,
                              TriggerPriceType price_type, TriggerCondition condition,
                              TriggerChildOrderType child_type, lob::Tick child_limit_price,
                              lob::Tick protection_price, uint32_t flags = lob::NONE, lob::Timestamp ts = 0);
  bool cancel_trigger_order(const std::string& market_symbol, UserId user, OrderId trigger_order_id, lob::Timestamp ts = 0);
  std::vector<TriggerOrder> trigger_orders(const std::string& market_symbol) const;
  int evaluate_triggers(const std::string& market_symbol, TriggerPriceType price_type, lob::Timestamp ts = 0);
  Position position(UserId user, const std::string& market_symbol) const;
  std::vector<Position> positions() const;

  WalletBalance balance(UserId user, const std::string& symbol) const;
  std::vector<std::pair<lob::Tick, lob::Quantity>> topN(const std::string& market_symbol, lob::Side side, int levels);
  std::vector<Candle> flush_candles();
  const std::vector<TradeEvent>& trades() const { return trade_history_; }
  const std::vector<Candle>& candles() const { return candle_history_; }
  std::vector<TradeEvent> drain_trades();
  std::vector<Candle> drain_candles();

  AssetRegistry& assets() { return assets_; }
  MarketRegistry& markets() { return markets_; }
  AccountLedger& ledger() { return ledger_; }
  EventStore& events() { return events_; }
  const EventStore& events() const { return events_; }
  PositionEngine& position_engine() { return positions_; }
  const PositionEngine& position_engine() const { return positions_; }

private:
  MarketEngine* engine_for_symbol(const std::string& market_symbol);
  const MarketEngine* engine_for_symbol(const std::string& market_symbol) const;
  MarketEngine* engine_for_id(MarketId market_id);
  const MarketEngine* engine_for_id(MarketId market_id) const;
  lob::Timestamp next_timestamp(lob::Timestamp requested);
  bool build_event_payloads() const { return retention_options_->build_event_payloads; }
  void record_committed_trade(const TradeEvent& trade);
  void append_kline_event(const Candle& candle, const std::string& type);

  AssetRegistry assets_;
  MarketRegistry markets_;
  AccountLedger ledger_;
  RiskEngine risk_;
  EventStore events_;
  KlineAggregator klines_;
  PositionEngine positions_;
  std::unique_ptr<RuntimeRetentionOptions> retention_options_;
  LiquidationOptions liquidation_options_;
  lob::Timestamp logical_time_{0};
  std::vector<TradeEvent> trade_history_;
  std::vector<Candle> candle_history_;
  std::unordered_map<MarketId, std::unique_ptr<MarketEngine>> engines_;
};

} // namespace lobx
