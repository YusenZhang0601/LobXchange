#include "lobx/exchange.hpp"

#include <limits>
#include <optional>
#include <sstream>
#include <utility>

namespace lobx {

Exchange::Exchange()
    : klines_({1000000000LL, 60000000000LL, 900000000000LL, 3600000000000LL, 86400000000000LL}),
      retention_options_(std::make_unique<RuntimeRetentionOptions>()) {}

void Exchange::set_retention_options(RuntimeRetentionOptions options) {
  *retention_options_ = options;
  events_.set_memory_enabled(retention_options_->record_events);
}

void Exchange::set_liquidation_options(LiquidationOptions options) {
  liquidation_options_ = options;
}

Result Exchange::issue_asset(const std::string& symbol, uint8_t decimals, Amount max_supply, UserId issuer,
                             Amount initial_supply, AssetId* out_id) {
  if (initial_supply < 0) return Result::fail(RejectCode::InvalidQuantity, "initial_supply must be non-negative");
  if (max_supply > 0 && initial_supply > max_supply) return Result::fail(RejectCode::InvalidQuantity, "initial_supply exceeds max_supply");
  AssetId id = 0;
  Result r = assets_.issue_asset(symbol, decimals, max_supply, issuer, &id);
  if (!r.ok) return r;
  r = assets_.activate(id);
  if (!r.ok) return r;
  if (initial_supply > 0) {
    r = assets_.mint(id, initial_supply);
    if (!r.ok) return r;
    r = ledger_.deposit(issuer, id, initial_supply);
    if (!r.ok) return r;
  }
  if (out_id) *out_id = id;
  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << "symbol=" << symbol << ",asset_id=" << id << ",issuer=" << issuer << ",initial=" << initial_supply;
    payload = os.str();
  }
  events_.append(0, "asset.issued", std::move(payload));
  return Result::success();
}

Result Exchange::create_spot_market(const std::string& symbol, const std::string& base_symbol, const std::string& quote_symbol,
                                    lob::Tick tick_size, lob::Quantity lot_size, lob::Quantity min_qty, Amount min_notional,
                                    MarketId* out_id) {
  const Asset* base = assets_.find_by_symbol(base_symbol);
  const Asset* quote = assets_.find_by_symbol(quote_symbol);
  if (!base || !quote) return Result::fail(RejectCode::UnknownAsset, "base or quote asset not found");
  if (base->status != AssetStatus::Active || quote->status != AssetStatus::Active) {
    return Result::fail(RejectCode::UnknownAsset, "base or quote asset is not active");
  }
  MarketId id = 0;
  Result r = markets_.create_spot_market(symbol, base->id, quote->id, tick_size, lot_size, min_qty, min_notional, &id);
  if (!r.ok) return r;
  const Market* market = markets_.get(id);
  if (!market) return Result::fail(RejectCode::UnknownMarket, "market creation failed");
  engines_.emplace(id, std::make_unique<MarketEngine>(*market, ledger_, risk_, &positions_, &events_, retention_options_.get()));
  if (out_id) *out_id = id;
  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << "symbol=" << symbol << ",market_id=" << id << ",base=" << base_symbol << ",quote=" << quote_symbol;
    payload = os.str();
  }
  events_.append(0, "market.created", std::move(payload));
  return Result::success();
}

Result Exchange::create_perpetual_market(const std::string& symbol, const std::string& base_symbol, const std::string& quote_symbol,
                                               const std::string& margin_symbol, lob::Tick tick_size, lob::Quantity lot_size,
                                               lob::Quantity min_qty, Amount min_notional, int max_leverage,
                                               MarketId* out_id) {
  const Asset* base = assets_.find_by_symbol(base_symbol);
  const Asset* quote = assets_.find_by_symbol(quote_symbol);
  const Asset* margin = assets_.find_by_symbol(margin_symbol);
  if (!base || !quote || !margin) return Result::fail(RejectCode::UnknownAsset, "base, quote or margin asset not found");
  if (base->status != AssetStatus::Active || quote->status != AssetStatus::Active || margin->status != AssetStatus::Active) {
    return Result::fail(RejectCode::UnknownAsset, "base, quote or margin asset is not active");
  }
  MarketId id = 0;
  Result r = markets_.create_perpetual_market(symbol, base->id, quote->id, margin->id, tick_size, lot_size, min_qty, min_notional, max_leverage, &id);
  if (!r.ok) return r;
  const Market* market = markets_.get(id);
  if (!market) return Result::fail(RejectCode::UnknownMarket, "market creation failed");
  engines_.emplace(id, std::make_unique<MarketEngine>(*market, ledger_, risk_, &positions_, &events_, retention_options_.get()));
  if (out_id) *out_id = id;
  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << "symbol=" << symbol << ",market_id=" << id << ",base=" << base_symbol
       << ",quote=" << quote_symbol << ",margin=" << margin_symbol << ",type=perpetual,max_leverage=" << max_leverage;
    payload = os.str();
  }
  events_.append(0, "market.created", std::move(payload));
  return Result::success();
}

Result Exchange::deposit(UserId user, const std::string& symbol, Amount amount) {
  const Asset* asset = assets_.find_by_symbol(symbol);
  if (!asset || asset->status != AssetStatus::Active) return Result::fail(RejectCode::UnknownAsset, "asset not found or inactive");
  Result r = ledger_.deposit(user, asset->id, amount);
  if (!r.ok) return r;
  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << "user=" << user << ",symbol=" << symbol << ",amount=" << amount;
    payload = os.str();
  }
  events_.append(0, "wallet.deposit", std::move(payload));
  return Result::success();
}

MarketEngine* Exchange::engine_for_symbol(const std::string& market_symbol) {
  const MarketId id = markets_.id_for_symbol(market_symbol);
  auto it = engines_.find(id);
  return it == engines_.end() ? nullptr : it->second.get();
}

const MarketEngine* Exchange::engine_for_symbol(const std::string& market_symbol) const {
  const MarketId id = markets_.id_for_symbol(market_symbol);
  auto it = engines_.find(id);
  return it == engines_.end() ? nullptr : it->second.get();
}

MarketEngine* Exchange::engine_for_id(MarketId market_id) {
  auto it = engines_.find(market_id);
  return it == engines_.end() ? nullptr : it->second.get();
}

const MarketEngine* Exchange::engine_for_id(MarketId market_id) const {
  auto it = engines_.find(market_id);
  return it == engines_.end() ? nullptr : it->second.get();
}

lob::Timestamp Exchange::next_timestamp(lob::Timestamp requested) {
  if (requested > 0) {
    if (requested > logical_time_) logical_time_ = requested;
    return requested;
  }
  if (logical_time_ < std::numeric_limits<lob::Timestamp>::max()) ++logical_time_;
  if (logical_time_ <= 0) logical_time_ = 1;
  return logical_time_;
}

void Exchange::append_kline_event(const Candle& candle, const std::string& type) {
  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << "market_id=" << candle.market_id << ",interval=" << candle.interval_ns
       << ",open_time=" << candle.open_time_ns << ",open=" << candle.open
       << ",high=" << candle.high << ",low=" << candle.low << ",close=" << candle.close
       << ",volume=" << candle.volume;
    payload = os.str();
  }
  events_.append(candle.close_time_ns, type, std::move(payload));
}

void Exchange::record_committed_trade(const TradeEvent& trade) {
  if (retention_options_->record_trade_history) {
    trade_history_.push_back(trade);
  }
  if (!retention_options_->update_klines) return;
  for (const Candle& candle : klines_.on_trade(trade)) {
    if (retention_options_->record_candle_history) {
      candle_history_.push_back(candle);
    }
    append_kline_event(candle, "kline.closed");
  }
}

SubmitResult Exchange::submit_limit(const std::string& market_symbol, UserId user, OrderId order_id, lob::Side side,
                                    lob::Tick price, lob::Quantity qty, uint32_t flags, lob::Timestamp ts) {
  SubmitResult result{};
  MarketEngine* engine = engine_for_symbol(market_symbol);
  if (!engine) {
    result.accepted = false;
    result.code = RejectCode::UnknownMarket;
    result.reason = "market not found";
    return result;
  }
  const Market* market = markets_.get(engine->market().id);
  if (!market || market->status != MarketStatus::Active) {
    result.accepted = false;
    result.code = RejectCode::MarketNotActive;
    result.reason = "market is not active";
    return result;
  }
  const lob::Timestamp event_ts = next_timestamp(ts);
  OrderRequest req{engine->market().id, user, order_id, events_.next_seq(), event_ts, side, price, qty, flags};
  result = engine->submit_limit(req);
  if (result.accepted) {
    for (const TradeEvent& trade : result.trades) {
      record_committed_trade(trade);
    }
  }
  return result;
}

SubmitResult Exchange::submit_market(const std::string& market_symbol, UserId user, OrderId order_id, lob::Side side,
                                     lob::Quantity qty, lob::Tick protection_price, uint32_t flags, lob::Timestamp ts) {
  SubmitResult result{};
  MarketEngine* engine = engine_for_symbol(market_symbol);
  if (!engine) {
    result.accepted = false;
    result.code = RejectCode::UnknownMarket;
    result.reason = "market not found";
    return result;
  }
  const Market* market = markets_.get(engine->market().id);
  if (!market || market->status != MarketStatus::Active) {
    result.accepted = false;
    result.code = RejectCode::MarketNotActive;
    result.reason = "market is not active";
    return result;
  }
  const lob::Timestamp event_ts = next_timestamp(ts);
  OrderRequest req{engine->market().id, user, order_id, events_.next_seq(), event_ts, side, protection_price, qty, flags};
  result = engine->submit_market(req, protection_price);
  if (result.accepted) {
    for (const TradeEvent& trade : result.trades) {
      record_committed_trade(trade);
    }
  }
  return result;
}

SimulatedFill Exchange::simulate_fill(const std::string& market_symbol, UserId user, lob::Side side,
                                      lob::Tick limit_price, lob::Quantity qty, uint32_t flags) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  if (!engine) {
    SimulatedFill result{};
    result.supported = false;
    result.code = RejectCode::UnknownMarket;
    result.reason = "market not found";
    result.requested_qty = qty;
    return result;
  }
  return engine->simulate_fill(user, side, limit_price, qty, flags);
}

bool Exchange::cancel(const std::string& market_symbol, OrderId order_id) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->cancel(order_id, std::nullopt, next_timestamp(0)) : false;
}

bool Exchange::cancel(const std::string& market_symbol, UserId user, OrderId order_id, lob::Timestamp ts) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->cancel(order_id, user, next_timestamp(ts)) : false;
}

void Exchange::set_leverage(UserId user, const std::string& market_symbol, int leverage) {
  const MarketId id = markets_.id_for_symbol(market_symbol);
  if (id == 0) return;
  MarketEngine* engine = engine_for_id(id);
  if (engine) engine->set_user_leverage(user, leverage);
  else positions_.set_leverage(user, id, leverage);
}

Result Exchange::set_index_price(MarketId market_id, lob::Tick price) {
  MarketEngine* engine = engine_for_id(market_id);
  if (!engine) return Result::fail(RejectCode::UnknownMarket, "market not found");
  return engine->set_index_price(price);
}

lob::Tick Exchange::get_index_price(MarketId market_id) const {
  const MarketEngine* engine = engine_for_id(market_id);
  return engine ? engine->index_price() : 0;
}

Result Exchange::set_mark_price_mode(MarketId market_id, MarkPriceMode mode) {
  MarketEngine* engine = engine_for_id(market_id);
  if (!engine) return Result::fail(RejectCode::UnknownMarket, "market not found");
  return engine->set_mark_price_mode(mode);
}

lob::Tick Exchange::mark_price(MarketId market_id) const {
  const MarketEngine* engine = engine_for_id(market_id);
  return engine ? engine->mark_price() : 0;
}

Amount Exchange::unrealized_pnl(UserId user, const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->unrealized_pnl(user) : 0;
}

Amount Exchange::maintenance_margin(UserId user, const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->maintenance_margin(user) : 0;
}

Amount Exchange::account_equity(UserId user, const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->account_equity(user) : 0;
}

bool Exchange::is_liquidatable(UserId user, const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->is_liquidatable(user) : false;
}

Result Exchange::set_perp_risk_tiers(MarketId market_id, std::vector<PerpRiskTier> tiers) {
  MarketEngine* engine = engine_for_id(market_id);
  if (!engine) return Result::fail(RejectCode::UnknownMarket, "market not found");
  return engine->set_risk_tiers(std::move(tiers));
}

Result Exchange::set_perp_fee_config(const std::string& market_symbol, PerpFeeConfig config) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  if (!engine) return Result::fail(RejectCode::UnknownMarket, "market not found");
  return engine->set_fee_config(config);
}

PerpFeeConfig Exchange::get_perp_fee_config(const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->fee_config() : PerpFeeConfig{};
}

Amount Exchange::account_fee_total(UserId user, const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->account_fee_total(user) : 0;
}

Result Exchange::set_funding_rate(const std::string& market_symbol, int funding_rate_bps) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  if (!engine) return Result::fail(RejectCode::UnknownMarket, "market not found");
  return engine->set_funding_rate(funding_rate_bps);
}

int Exchange::get_funding_rate(const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->funding_rate() : 0;
}

Result Exchange::settle_funding(const std::string& market_symbol, lob::Timestamp ts) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  if (!engine) return Result::fail(RejectCode::UnknownMarket, "market not found");
  return engine->settle_funding(next_timestamp(ts));
}

lob::Timestamp Exchange::next_funding_time(const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->next_funding_time() : 0;
}

Amount Exchange::account_funding_total(UserId user, const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->account_funding_total(user) : 0;
}

lob::Tick Exchange::bankruptcy_price(UserId user, const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->bankruptcy_price(user) : 0;
}

Result Exchange::liquidate_position(const std::string& market_symbol, UserId user, UserId liquidator, lob::Timestamp ts) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  if (!engine) return Result::fail(RejectCode::UnknownMarket, "market not found");
  return engine->liquidate_position(user, liquidator, next_timestamp(ts));
}

Result Exchange::liquidate_if_needed(const std::string& market_symbol, UserId user, lob::Timestamp ts) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  if (!engine) return Result::fail(RejectCode::UnknownMarket, "market not found");
  if (liquidation_options_.mode == LiquidationMode::Disabled) return Result::success();
  if (!engine->is_liquidatable(user)) return Result::success();
  if (liquidation_options_.mode != LiquidationMode::InfiniteInsurance) {
    return Result::fail(RejectCode::UnsupportedOrderType, "liquidation mode is not implemented");
  }
  return engine->liquidate_position(user, std::numeric_limits<UserId>::max(), next_timestamp(ts), &liquidation_options_);
}

Result Exchange::credit_insurance_fund(const std::string& market_symbol, Amount amount, const std::string& reason, lob::Timestamp ts) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  if (!engine) return Result::fail(RejectCode::UnknownMarket, "market not found");
  return engine->credit_insurance_fund(amount, next_timestamp(ts), reason);
}

Amount Exchange::insurance_fund_balance(const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->insurance_fund_balance() : 0;
}

Amount Exchange::bad_debt(const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->bad_debt() : 0;
}

std::vector<AdlCandidate> Exchange::rank_adl_candidates(const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->rank_adl_candidates() : std::vector<AdlCandidate>{};
}

Result Exchange::create_trigger_order(const std::string& market_symbol, UserId user, OrderId trigger_order_id,
                                      lob::Side side, lob::Quantity qty, lob::Tick trigger_price,
                                      TriggerPriceType price_type, TriggerCondition condition,
                                      TriggerChildOrderType child_type, lob::Tick child_limit_price,
                                      lob::Tick protection_price, uint32_t flags, lob::Timestamp ts) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  if (!engine) return Result::fail(RejectCode::UnknownMarket, "market not found");
  TriggerOrderRequest req{engine->market().id,
                          user,
                          trigger_order_id,
                          side,
                          qty,
                          trigger_price,
                          price_type,
                          condition,
                          child_type,
                          child_limit_price,
                          protection_price,
                          flags,
                          next_timestamp(ts)};
  return engine->create_trigger_order(req);
}

bool Exchange::cancel_trigger_order(const std::string& market_symbol, UserId user, OrderId trigger_order_id, lob::Timestamp ts) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->cancel_trigger_order(trigger_order_id, user, next_timestamp(ts)) : false;
}

std::vector<TriggerOrder> Exchange::trigger_orders(const std::string& market_symbol) const {
  const MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->trigger_orders() : std::vector<TriggerOrder>{};
}

int Exchange::evaluate_triggers(const std::string& market_symbol, TriggerPriceType price_type, lob::Timestamp ts) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->evaluate_triggers(price_type, next_timestamp(ts)) : 0;
}

Position Exchange::position(UserId user, const std::string& market_symbol) const {
  const MarketId id = markets_.id_for_symbol(market_symbol);
  return positions_.position(user, id);
}

std::vector<Position> Exchange::positions() const {
  return positions_.positions();
}

WalletBalance Exchange::balance(UserId user, const std::string& symbol) const {
  const Asset* asset = assets_.find_by_symbol(symbol);
  if (!asset) return WalletBalance{user, 0, 0, 0, 0};
  return ledger_.balance(user, asset->id);
}

std::vector<std::pair<lob::Tick, lob::Quantity>> Exchange::topN(const std::string& market_symbol, lob::Side side, int levels) {
  MarketEngine* engine = engine_for_symbol(market_symbol);
  return engine ? engine->topN(side, levels) : std::vector<std::pair<lob::Tick, lob::Quantity>>{};
}

std::vector<Candle> Exchange::flush_candles() {
  if (!retention_options_->update_klines) return {};
  std::vector<Candle> candles = klines_.flush_all();
  for (const Candle& candle : candles) {
    if (retention_options_->record_candle_history) {
      candle_history_.push_back(candle);
    }
    append_kline_event(candle, "kline.flushed");
  }
  return candles;
}

std::vector<TradeEvent> Exchange::drain_trades() {
  std::vector<TradeEvent> out;
  out.swap(trade_history_);
  return out;
}

std::vector<Candle> Exchange::drain_candles() {
  std::vector<Candle> out;
  out.swap(candle_history_);
  return out;
}

} // namespace lobx
