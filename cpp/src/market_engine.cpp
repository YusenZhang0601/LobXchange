#include "lobx/market_engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace lobx {

namespace {

constexpr UserId kDedicatedFeeAccountUser = std::numeric_limits<UserId>::max();

bool checked_add_amount(Amount a, Amount b, Amount& out) {
  if ((b > 0 && a > std::numeric_limits<Amount>::max() - b) ||
      (b < 0 && a < std::numeric_limits<Amount>::min() - b)) {
    return false;
  }
  out = a + b;
  return true;
}

bool checked_abs_quantity(lob::Quantity qty, lob::Quantity& out) {
  if (qty == std::numeric_limits<lob::Quantity>::min()) return false;
  out = qty < 0 ? -qty : qty;
  return true;
}

bool checked_realized_delta(lob::Tick price, lob::Tick entry_price, lob::Quantity close_qty, int direction, Amount& out) {
  out = 0;
  if (close_qty < 0) return false;
  Amount delta = 0;
  Amount value = 0;
  if (__builtin_sub_overflow(price, entry_price, &delta)) return false;
  if (__builtin_mul_overflow(delta, close_qty, &value)) return false;
  if (__builtin_mul_overflow(value, static_cast<Amount>(direction), &out)) return false;
  return true;
}

bool proportional_amount(Amount amount, lob::Quantity part, lob::Quantity total, Amount& out) {
  out = 0;
  if (amount < 0 || part < 0 || total <= 0) return false;
  if (part > 0 && amount > std::numeric_limits<Amount>::max() / part) return false;
  out = (amount * part) / total;
  return true;
}

} // namespace

MarketEngine::MarketEngine(Market market, AccountLedger& ledger, RiskEngine& risk, PositionEngine* positions,
                           EventStore* events, const RuntimeRetentionOptions* retention_options)
    : market_(std::move(market)),
      ledger_(ledger),
      risk_(risk),
      positions_(positions),
      events_(events),
      retention_options_(retention_options) {
  reset_book();
}

bool MarketEngine::target_lock_for(const OpenOrder& order, Amount& out) const {
  out = 0;
  if (order.leaves_qty <= 0) return true;
  if ((order.flags & LOBX_REDUCE_ONLY) != 0u) return true;
  Amount notional = 0;
  if (!mul_amount(order.limit_price, order.leaves_qty, notional)) return false;
  if (market_.type == MarketType::Perpetual) {
    const int lev = std::max(1, order.leverage);
    Amount numerator = 0;
    if (!checked_add_amount(notional, lev - 1, numerator)) return false;
    out = std::max<Amount>(1, numerator / lev);
    return true;
  }
  out = order.side == lob::Side::Ask ? order.leaves_qty : notional;
  return true;
}

bool MarketEngine::margin_for(lob::Tick price, lob::Quantity qty, int leverage, Amount& out) const {
  out = 0;
  if (price <= 0 || qty <= 0) return true;
  Amount notional = 0;
  if (!mul_amount(price, qty, notional)) return false;
  const int lev = std::max(1, leverage);
  Amount numerator = 0;
  if (!checked_add_amount(notional, lev - 1, numerator)) return false;
  out = std::max<Amount>(1, numerator / lev);
  return true;
}

bool MarketEngine::fee_for(Amount notional, int fee_bps, Amount& out) const {
  out = 0;
  if (notional <= 0 || fee_bps <= 0) return true;
  if (notional > std::numeric_limits<Amount>::max() / fee_bps) return false;
  out = (notional * fee_bps) / 10000;
  return true;
}

UserId MarketEngine::fee_account_user() const {
  return kDedicatedFeeAccountUser;
}

lob::Tick MarketEngine::fallback_mark_price() const {
  const lob::Tick bid = best_bid();
  const lob::Tick ask = best_ask();
  if (bid != std::numeric_limits<lob::Tick>::min() && ask != std::numeric_limits<lob::Tick>::max()) return (bid + ask) / 2;
  if (last_trade_price_ > 0) return last_trade_price_;
  if (index_price_ > 0) return index_price_;
  if (bid != std::numeric_limits<lob::Tick>::min()) return bid;
  if (ask != std::numeric_limits<lob::Tick>::max()) return ask;
  return 0;
}

Result MarketEngine::set_index_price(lob::Tick price) {
  if (market_.type != MarketType::Perpetual) return Result::fail(RejectCode::UnsupportedOrderType, "index price requires perpetual market");
  if (price <= 0) return Result::fail(RejectCode::InvalidPrice, "index price must be positive");
  index_price_ = price;
  return Result::success();
}

Result MarketEngine::set_mark_price_mode(MarkPriceMode mode) {
  if (market_.type != MarketType::Perpetual) return Result::fail(RejectCode::UnsupportedOrderType, "mark price mode requires perpetual market");
  mark_price_mode_ = mode;
  return Result::success();
}

lob::Tick MarketEngine::mark_price() const {
  if (market_.type != MarketType::Perpetual) return 0;
  if (mark_price_mode_ == MarkPriceMode::IndexPrice) return index_price_ > 0 ? index_price_ : fallback_mark_price();
  if (mark_price_mode_ == MarkPriceMode::MidPrice) return fallback_mark_price();
  return last_trade_price_ > 0 ? last_trade_price_ : fallback_mark_price();
}

Amount MarketEngine::position_notional(UserId user, lob::Tick price) const {
  if (!positions_ || price <= 0) return 0;
  const Position p = positions_->position(user, market_.id);
  lob::Quantity abs_qty = 0;
  if (!checked_abs_quantity(p.signed_qty, abs_qty)) return 0;
  Amount notional = 0;
  if (!mul_amount(price, abs_qty, notional)) return std::numeric_limits<Amount>::max();
  return notional;
}

Amount MarketEngine::projected_notional_after(const OrderRequest& req) const {
  if (!positions_ || market_.type != MarketType::Perpetual) return 0;
  const Position p = positions_->position(req.user, market_.id);
  const lob::Quantity delta = req.side == lob::Side::Bid ? req.qty : -req.qty;
  lob::Quantity projected = 0;
  if (!__builtin_add_overflow(p.signed_qty, delta, &projected)) {
    lob::Quantity abs_qty = 0;
    if (!checked_abs_quantity(projected, abs_qty)) return std::numeric_limits<Amount>::max();
    Amount notional = 0;
    if (!mul_amount(req.price, abs_qty, notional)) return std::numeric_limits<Amount>::max();
    return notional;
  }
  return std::numeric_limits<Amount>::max();
}

const PerpRiskTier* MarketEngine::tier_for_notional(Amount notional) const {
  for (const PerpRiskTier& tier : risk_tiers_) {
    const bool above_floor = notional >= tier.notional_floor;
    const bool below_cap = tier.notional_cap == 0 || notional < tier.notional_cap;
    if (above_floor && below_cap) return &tier;
  }
  return nullptr;
}

int MarketEngine::maintenance_margin_bps_for(Amount notional) const {
  const PerpRiskTier* tier = tier_for_notional(notional);
  return tier ? std::max(0, tier->maintenance_margin_bps) : 0;
}

int MarketEngine::effective_max_leverage(UserId, Amount projected_notional) const {
  int max_leverage = std::max(1, market_.max_leverage);
  if (const PerpRiskTier* tier = tier_for_notional(projected_notional)) {
    max_leverage = std::min(max_leverage, std::max(1, tier->max_leverage));
  }
  return max_leverage;
}

void MarketEngine::set_user_leverage(UserId user, int leverage) {
  if (!positions_) return;
  const Amount notional = position_notional(user, mark_price());
  positions_->set_leverage(user, market_.id, std::min(std::max(1, leverage), effective_max_leverage(user, notional)));
}

Result MarketEngine::set_risk_tiers(std::vector<PerpRiskTier> tiers) {
  if (market_.type != MarketType::Perpetual) return Result::fail(RejectCode::UnsupportedOrderType, "risk tiers require perpetual market");
  std::sort(tiers.begin(), tiers.end(), [](const PerpRiskTier& a, const PerpRiskTier& b) {
    return a.notional_floor < b.notional_floor;
  });
  Amount previous_floor = -1;
  for (const PerpRiskTier& tier : tiers) {
    if (tier.notional_floor < 0 || tier.notional_cap < 0) return Result::fail(RejectCode::InvalidNotional, "risk tier notional must be non-negative");
    if (tier.notional_cap != 0 && tier.notional_cap <= tier.notional_floor) return Result::fail(RejectCode::InvalidNotional, "risk tier cap must exceed floor");
    if (tier.notional_floor <= previous_floor) return Result::fail(RejectCode::InvalidNotional, "risk tier floors must increase");
    if (tier.initial_margin_bps < 0 || tier.maintenance_margin_bps < 0) return Result::fail(RejectCode::InvalidQuantity, "risk tier margin bps must be non-negative");
    if (tier.max_leverage <= 0) return Result::fail(RejectCode::InvalidQuantity, "risk tier max leverage must be positive");
    previous_floor = tier.notional_floor;
  }
  risk_tiers_ = std::move(tiers);
  return Result::success();
}

Result MarketEngine::set_fee_config(PerpFeeConfig config) {
  if (market_.type != MarketType::Perpetual) return Result::fail(RejectCode::UnsupportedOrderType, "fee config requires perpetual market");
  if (config.maker_fee_bps < 0 || config.taker_fee_bps < 0 || config.liquidation_fee_bps < 0) {
    return Result::fail(RejectCode::InvalidQuantity, "perp fee bps must be non-negative");
  }
  fee_config_ = config;
  return Result::success();
}

Amount MarketEngine::account_fee_total(UserId user) const {
  auto it = fee_totals_.find(user);
  return it == fee_totals_.end() ? 0 : it->second;
}

Result MarketEngine::set_funding_rate(int funding_rate_bps) {
  if (market_.type != MarketType::Perpetual) return Result::fail(RejectCode::UnsupportedOrderType, "funding requires perpetual market");
  if (funding_rate_bps == std::numeric_limits<int>::min()) return Result::fail(RejectCode::InvalidQuantity, "funding rate is out of range");
  funding_config_.funding_rate_bps = funding_rate_bps;
  return Result::success();
}

Amount MarketEngine::account_funding_total(UserId user) const {
  auto it = funding_totals_.find(user);
  return it == funding_totals_.end() ? 0 : it->second;
}

Amount MarketEngine::unrealized_pnl(UserId user) const {
  if (market_.type != MarketType::Perpetual || !positions_) return 0;
  const Position p = positions_->position(user, market_.id);
  if (p.signed_qty == 0) return 0;
  const lob::Tick mark = mark_price();
  if (mark <= 0 || p.entry_price <= 0) return 0;
  lob::Quantity abs_qty = 0;
  if (!checked_abs_quantity(p.signed_qty, abs_qty)) return 0;
  Amount delta = 0;
  Amount value = 0;
  if (__builtin_sub_overflow(mark, p.entry_price, &delta)) return 0;
  if (__builtin_mul_overflow(delta, abs_qty, &value)) return 0;
  return p.signed_qty > 0 ? value : -value;
}

Amount MarketEngine::maintenance_margin(UserId user) const {
  const Amount notional = position_notional(user, mark_price());
  if (notional <= 0) return 0;
  const int bps = maintenance_margin_bps_for(notional);
  if (bps <= 0) return 0;
  if (notional > std::numeric_limits<Amount>::max() / bps) return std::numeric_limits<Amount>::max();
  return (notional * bps + 9999) / 10000;
}

Amount MarketEngine::account_equity(UserId user) const {
  if (market_.type != MarketType::Perpetual) return 0;
  Amount equity = ledger_.balance(user, market_.margin_asset).total;
  Amount pnl = unrealized_pnl(user);
  Amount out = 0;
  if (!checked_add_amount(equity, pnl, out)) return pnl < 0 ? std::numeric_limits<Amount>::min() : std::numeric_limits<Amount>::max();
  return out;
}

bool MarketEngine::is_liquidatable(UserId user) const {
  if (market_.type != MarketType::Perpetual || !positions_) return false;
  if (positions_->position(user, market_.id).signed_qty == 0) return false;
  return account_equity(user) <= maintenance_margin(user);
}

lob::Tick MarketEngine::bankruptcy_price(UserId user) const {
  if (market_.type != MarketType::Perpetual || !positions_) return 0;
  const Position p = positions_->position(user, market_.id);
  if (p.signed_qty == 0 || p.entry_price <= 0) return 0;
  lob::Quantity abs_qty = 0;
  if (!checked_abs_quantity(p.signed_qty, abs_qty) || abs_qty <= 0) return 0;
  const Amount margin = position_margin_.count(user) ? position_margin_.at(user) : 0;
  const lob::Tick distance = static_cast<lob::Tick>(margin / abs_qty);
  if (p.signed_qty > 0) return p.entry_price > distance ? p.entry_price - distance : 1;
  return p.entry_price + distance;
}

bool MarketEngine::charge_perp_fee(UserId user, Amount fee, bool liquidation_fee, lob::Timestamp ts,
                                   Amount notional, int fee_bps, std::vector<PendingEvent>* pending_events) {
  if (fee <= 0) return true;
  Result r = ledger_.withdraw(user, market_.margin_asset, fee);
  if (!r.ok) return false;
  r = ledger_.credit(fee_account_user(), market_.margin_asset, fee);
  if (!r.ok) return false;
  remember_fee_total(user);
  Amount updated = 0;
  if (!checked_add_amount(fee_totals_[user], fee, updated)) return false;
  fee_totals_[user] = updated;
  if (pending_events) {
    std::string payload;
    if (build_event_payloads()) {
      std::ostringstream os;
      os << "market=" << market_.symbol << ",account_id=" << user
         << ",notional=" << notional << ",fee_bps=" << fee_bps
         << ",amount=" << fee << ",liquidation=" << (liquidation_fee ? 1 : 0);
      payload = os.str();
    }
    pending_events->push_back(PendingEvent{ts, "perp.fee_charged", std::move(payload)});
  }
  if (fault_active(FaultPoint::AfterFeeCharge)) return false;
  return true;
}

bool MarketEngine::record_funding(UserId user, Amount payment) {
  remember_funding_total(user);
  Amount updated = 0;
  if (!checked_add_amount(funding_totals_[user], payment, updated)) return false;
  funding_totals_[user] = updated;
  return true;
}

Result MarketEngine::credit_insurance_fund(Amount amount, lob::Timestamp ts, const std::string& reason) {
  if (market_.type != MarketType::Perpetual) return Result::fail(RejectCode::UnsupportedOrderType, "insurance fund requires perpetual market");
  if (amount <= 0) return Result::fail(RejectCode::InvalidQuantity, "insurance fund credit must be positive");
  Amount updated = 0;
  if (!checked_add_amount(insurance_fund_balance_, amount, updated)) {
    return Result::fail(RejectCode::InvalidNotional, "insurance fund balance overflow");
  }
  insurance_fund_balance_ = updated;
  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << "market=" << market_.symbol << ",market_id=" << market_.id
       << ",asset=" << market_.margin_asset << ",amount=" << amount
       << ",balance_after=" << insurance_fund_balance_ << ",reason=" << reason;
    payload = os.str();
  }
  append_event(ts > 0 ? ts : 0, "insurance_fund.credited", payload);
  return Result::success();
}

bool MarketEngine::debit_insurance_fund(Amount amount, lob::Timestamp ts, const std::string& reason, std::vector<PendingEvent>& pending_events) {
  if (amount < 0 || amount > insurance_fund_balance_) return false;
  if (amount == 0) return true;
  insurance_fund_balance_ -= amount;
  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << "market=" << market_.symbol << ",market_id=" << market_.id
       << ",asset=" << market_.margin_asset << ",amount=" << amount
       << ",balance_after=" << insurance_fund_balance_ << ",reason=" << reason;
    payload = os.str();
  }
  pending_events.push_back(PendingEvent{ts > 0 ? ts : 0, "insurance_fund.debited", std::move(payload)});
  return true;
}

bool MarketEngine::record_infinite_insurance_absorption(UserId user, Amount amount, lob::Timestamp ts,
                                                        const std::string& reason,
                                                        std::vector<PendingEvent>& pending_events) const {
  if (amount < 0) return false;
  if (amount == 0) return true;
  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << "market=" << market_.symbol << ",market_id=" << market_.id
       << ",asset=" << market_.margin_asset << ",account_id=" << user
       << ",amount=" << amount << ",balance_after=" << insurance_fund_balance_
       << ",mode=infinite_insurance,reason=" << reason;
    payload = os.str();
  }
  pending_events.push_back(PendingEvent{ts > 0 ? ts : 0, "insurance_fund.absorbed_loss", std::move(payload)});
  return true;
}

bool MarketEngine::record_bad_debt(UserId user, Amount amount, lob::Timestamp ts, const std::string& reason, std::vector<PendingEvent>& pending_events) {
  if (amount < 0) return false;
  if (amount == 0) return true;
  Amount updated = 0;
  if (!checked_add_amount(bad_debt_, amount, updated)) return false;
  bad_debt_ = updated;
  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << "market=" << market_.symbol << ",market_id=" << market_.id
       << ",asset=" << market_.margin_asset << ",account_id=" << user
       << ",amount=" << amount << ",total_bad_debt=" << bad_debt_
       << ",reason=" << reason;
    payload = os.str();
  }
  pending_events.push_back(PendingEvent{ts > 0 ? ts : 0, "perp.bad_debt_recorded", std::move(payload)});
  return true;
}

void MarketEngine::append_adl_required_event(UserId user, Amount bad_debt_amount, Amount insurance_paid, lob::Timestamp ts,
                                             std::vector<PendingEvent>& pending_events) const {
  std::string payload;
  if (!retention_options_ || retention_options_->build_event_payloads) {
    std::ostringstream os;
    os << "market=" << market_.symbol << ",market_id=" << market_.id
       << ",asset=" << market_.margin_asset << ",account_id=" << user
       << ",bad_debt=" << bad_debt_amount << ",insurance_paid=" << insurance_paid
       << ",candidate_count=" << rank_adl_candidates().size()
       << ",reason=insurance_fund_insufficient";
    payload = os.str();
  }
  pending_events.push_back(PendingEvent{ts > 0 ? ts : 0, "ADL_REQUIRED", std::move(payload)});
}

std::vector<AdlCandidate> MarketEngine::rank_adl_candidates() const {
  std::vector<AdlCandidate> candidates;
  if (market_.type != MarketType::Perpetual || !positions_) return candidates;
  const lob::Tick mark = mark_price();
  if (mark <= 0) return candidates;
  for (const Position& p : positions_->positions()) {
    if (p.market_id != market_.id || p.signed_qty == 0) continue;
    lob::Quantity abs_qty = 0;
    if (!checked_abs_quantity(p.signed_qty, abs_qty) || abs_qty <= 0) continue;
    Amount notional = 0;
    if (!mul_amount(mark, abs_qty, notional) || notional <= 0) continue;
    const Amount pnl = unrealized_pnl(p.user);
    if (pnl <= 0) continue;
    const Amount equity = account_equity(p.user);
    const long double pnl_ratio = static_cast<long double>(pnl) / static_cast<long double>(notional);
    const long double leverage_denominator = equity > 0 ? static_cast<long double>(equity) : 1.0L;
    const long double effective_leverage = static_cast<long double>(notional) / leverage_denominator;
    candidates.push_back(AdlCandidate{p.user, p.signed_qty, notional, pnl, pnl_ratio, effective_leverage, 0});
  }
  std::sort(candidates.begin(), candidates.end(), [](const AdlCandidate& a, const AdlCandidate& b) {
    if (a.pnl_ratio != b.pnl_ratio) return a.pnl_ratio > b.pnl_ratio;
    if (a.effective_leverage != b.effective_leverage) return a.effective_leverage > b.effective_leverage;
    return a.account_id < b.account_id;
  });
  for (size_t i = 0; i < candidates.size(); ++i) candidates[i].rank = static_cast<int>(i + 1);
  return candidates;
}

Result MarketEngine::settle_funding(lob::Timestamp ts) {
  if (market_.type != MarketType::Perpetual || !positions_) return Result::fail(RejectCode::UnsupportedOrderType, "funding requires perpetual market");
  const lob::Tick mark = mark_price();
  if (mark <= 0) return Result::fail(RejectCode::InvalidPrice, "mark price unavailable");
  const int rate_bps = funding_config_.funding_rate_bps;
  const Snapshot snapshot = make_snapshot();
  auto fail = [&](RejectCode code, const std::string& reason) {
    restore_snapshot(snapshot);
    return Result::fail(code, reason);
  };

  Amount gross_payment = 0;
  int settled_accounts = 0;
  std::vector<PendingEvent> pending_events;
  for (const Position& p : positions_->positions()) {
    if (p.market_id != market_.id || p.signed_qty == 0) continue;
    lob::Quantity abs_qty = 0;
    if (!checked_abs_quantity(p.signed_qty, abs_qty)) return fail(RejectCode::InvalidQuantity, "funding quantity overflow");
    Amount notional = 0;
    if (!mul_amount(mark, abs_qty, notional)) return fail(RejectCode::InvalidNotional, "funding notional overflow");
    const int abs_rate = rate_bps < 0 ? -rate_bps : rate_bps;
    Amount payment = 0;
    if (!fee_for(notional, abs_rate, payment)) return fail(RejectCode::InvalidNotional, "funding payment overflow");
    if (payment == 0) continue;

    const bool pays = rate_bps > 0 ? p.signed_qty > 0 : p.signed_qty < 0;
    const Amount signed_payment = pays ? -payment : payment;
    if (pays) {
      Result r = ledger_.withdraw(p.user, market_.margin_asset, payment);
      if (!r.ok) return fail(r.code, r.reason);
      if (!record_funding(p.user, signed_payment)) return fail(RejectCode::InvalidNotional, "funding total overflow");
    } else {
      Result r = ledger_.credit(p.user, market_.margin_asset, payment);
      if (!r.ok) return fail(r.code, r.reason);
      if (!record_funding(p.user, signed_payment)) return fail(RejectCode::InvalidNotional, "funding total overflow");
    }
    std::string account_payload;
    if (build_event_payloads()) {
      std::ostringstream os;
      os << "market=" << market_.symbol << ",account_id=" << p.user
         << ",qty=" << p.signed_qty << ",notional=" << notional
         << ",mark=" << mark << ",funding_rate_bps=" << rate_bps
         << ",payment=" << signed_payment;
      account_payload = os.str();
    }
    pending_events.push_back(PendingEvent{ts > 0 ? ts : 0, "funding.payment", std::move(account_payload)});
    Amount updated_gross = 0;
    if (!checked_add_amount(gross_payment, payment, updated_gross)) return fail(RejectCode::InvalidNotional, "funding gross overflow");
    gross_payment = updated_gross;
    ++settled_accounts;
  }

  if (!ledger_.invariant_ok()) return fail(RejectCode::InternalError, "ledger invariant violation");
  if (funding_config_.funding_interval_ns > 0 && ts > 0) {
    funding_config_.next_funding_time = ts + funding_config_.funding_interval_ns;
  }
  if (settled_accounts > 0) {
    for (const PendingEvent& event : pending_events) append_event(event.ts, event.type, event.payload);
    std::string payload;
    if (build_event_payloads()) {
      std::ostringstream os;
      os << "market=" << market_.symbol << ",rate_bps=" << rate_bps
         << ",mark=" << mark << ",accounts=" << settled_accounts
         << ",gross_payment=" << gross_payment;
      payload = os.str();
    }
    append_event(ts > 0 ? ts : 0, "funding.settled", payload);
  }
  return Result::success();
}

bool MarketEngine::release_user_orders(UserId user) {
  std::vector<OrderId> ids;
  for (const auto& kv : open_) {
    if (kv.second.user == user) ids.push_back(kv.first);
  }
  for (OrderId id : ids) {
    if (open_.find(id) == open_.end()) continue;
    if (!can_release_order_lock(id)) return false;
    if (!book_->cancel(id)) return false;
    if (!release_and_erase(id)) return false;
  }
  return true;
}

Result MarketEngine::liquidate_position(UserId user, UserId liquidator, lob::Timestamp ts,
                                        const LiquidationOptions* options) {
  if (market_.type != MarketType::Perpetual || !positions_) return Result::fail(RejectCode::UnsupportedOrderType, "liquidation requires perpetual market");
  if (options && options->mode != LiquidationMode::InfiniteInsurance) {
    return Result::fail(RejectCode::UnsupportedOrderType, "liquidation mode is not implemented");
  }
  if (options && !options->close_position_immediately) {
    return Result::fail(RejectCode::UnsupportedOrderType, "only immediate close-at-mark liquidation is implemented");
  }
  const Position before = positions_->position(user, market_.id);
  if (before.signed_qty == 0) return Result::fail(RejectCode::InvalidQuantity, "no position to liquidate");
  const lob::Tick price = mark_price();
  if (price <= 0) return Result::fail(RejectCode::InvalidPrice, "mark price unavailable");
  if (!is_liquidatable(user)) return Result::fail(RejectCode::InvalidNotional, "account is not liquidatable");
  const Snapshot snapshot = make_snapshot();
  auto fail = [&](RejectCode code, const std::string& reason) {
    restore_snapshot(snapshot);
    return Result::fail(code, reason);
  };
  if (!release_user_orders(user)) return fail(RejectCode::InternalError, "failed to release user orders");
  const lob::Side close_side = before.signed_qty > 0 ? lob::Side::Ask : lob::Side::Bid;
  lob::Quantity abs_qty = 0;
  if (!checked_abs_quantity(before.signed_qty, abs_qty)) return fail(RejectCode::InvalidQuantity, "position quantity overflow");

  const Amount margin = position_margin_[user];
  if (margin > 0) {
    Result r = ledger_.release(user, market_.margin_asset, margin);
    if (!r.ok) return fail(r.code, r.reason);
    position_margin_[user] = 0;
  }

  Amount realized = 0;
  const int direction = before.signed_qty > 0 ? 1 : -1;
  if (!checked_realized_delta(price, before.entry_price, abs_qty, direction, realized)) {
    return fail(RejectCode::InvalidNotional, "liquidation realized pnl overflow");
  }
  Amount liquidation_loss = 0;
  Amount account_loss_paid = 0;
  Amount insurance_paid = 0;
  Amount bad_debt_amount = 0;
  std::vector<PendingEvent> liquidation_loss_events;
  const bool infinite_insurance = options && options->mode == LiquidationMode::InfiniteInsurance;
  const bool record_liquidation_events = !options || options->record_liquidation_events;
  if (realized > 0) {
    Result r = ledger_.credit(user, market_.margin_asset, realized);
    if (!r.ok) return fail(r.code, r.reason);
  } else if (realized < 0) {
    if (realized == std::numeric_limits<Amount>::min()) return fail(RejectCode::InvalidNotional, "liquidation loss overflow");
    liquidation_loss = -realized;
    Amount remaining_loss = liquidation_loss;
    account_loss_paid = std::min(ledger_.free(user, market_.margin_asset), remaining_loss);
    if (account_loss_paid > 0) {
      Result r = ledger_.withdraw(user, market_.margin_asset, account_loss_paid);
      if (!r.ok) return fail(r.code, r.reason);
      remaining_loss -= account_loss_paid;
    }
    if (remaining_loss > 0) {
      if (infinite_insurance) {
        insurance_paid = remaining_loss;
        if (!record_infinite_insurance_absorption(user, insurance_paid, ts, "liquidation_loss", liquidation_loss_events)) {
          return fail(RejectCode::InternalError, "failed to record infinite insurance absorption");
        }
        remaining_loss = 0;
      } else {
        insurance_paid = std::min(insurance_fund_balance_, remaining_loss);
        if (!debit_insurance_fund(insurance_paid, ts, "liquidation_loss", liquidation_loss_events)) {
          return fail(RejectCode::InternalError, "failed to debit insurance fund");
        }
        remaining_loss -= insurance_paid;
      }
    }
    if (remaining_loss > 0) {
      bad_debt_amount = remaining_loss;
      if (!record_bad_debt(user, bad_debt_amount, ts, "liquidation_loss", liquidation_loss_events)) {
        return fail(RejectCode::InvalidNotional, "failed to record bad debt");
      }
      append_adl_required_event(user, bad_debt_amount, insurance_paid, ts, liquidation_loss_events);
    }
  }
  Amount liquidation_fee = 0;
  Amount liquidation_notional = 0;
  if (!mul_amount(price, abs_qty, liquidation_notional)) return fail(RejectCode::InvalidNotional, "liquidation fee notional overflow");
  const bool charge_liquidation_fee = !options || options->charge_liquidation_fee;
  if (charge_liquidation_fee &&
      !fee_for(liquidation_notional, fee_config_.liquidation_fee_bps, liquidation_fee)) {
    return fail(RejectCode::InvalidNotional, "liquidation fee overflow");
  }
  std::vector<PendingEvent> fee_events;
  if (!charge_perp_fee(user, liquidation_fee, true, ts > 0 ? ts : 0, liquidation_notional,
                       fee_config_.liquidation_fee_bps, &fee_events)) {
    return fail(RejectCode::InsufficientBalance, "insufficient balance for liquidation fee");
  }
  if (!positions_->apply_trade_checked(user, market_.id, close_side, price, abs_qty)) {
    return fail(RejectCode::InternalError, "failed to close liquidated position");
  }
  if (!ledger_.invariant_ok()) return fail(RejectCode::InternalError, "ledger invariant violation");

  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << "market=" << market_.symbol << ",market_id=" << market_.id
       << ",account_id=" << user << ",liquidator=" << liquidator
       << ",position_qty=" << before.signed_qty
       << ",mark_price=" << price << ",liquidation_price=" << price
       << ",qty=" << abs_qty << ",realized_pnl=" << realized
       << ",loss=" << liquidation_loss << ",account_loss_paid=" << account_loss_paid
       << ",insurance_paid=" << insurance_paid << ",bad_debt=" << bad_debt_amount
       << ",fee=" << liquidation_fee
       << ",mode=" << (infinite_insurance ? "infinite_insurance" : "limited_insurance");
    payload = os.str();
  }
  if (record_liquidation_events) {
    for (const PendingEvent& event : liquidation_loss_events) append_event(event.ts, event.type, event.payload);
    for (const PendingEvent& event : fee_events) append_event(event.ts, event.type, event.payload);
    append_event(ts > 0 ? ts : 0, "liquidation", payload);
  }
  return Result::success();
}

lob::Quantity MarketEngine::available_to_fill(const OrderRequest& req) const {
  lob::Quantity available = 0;
  const bool stp = (req.flags & lob::STP) != 0u;
  for (const auto& kv : open_) {
    const OpenOrder& order = kv.second;
    if (order.leaves_qty <= 0 || order.side == req.side) continue;
    if (stp && order.user == req.user) continue;
    const bool crosses = req.side == lob::Side::Bid ? order.limit_price <= req.price : order.limit_price >= req.price;
    if (!crosses) continue;
    available += order.leaves_qty;
    if (available >= req.qty) return available;
  }
  return available;
}

bool MarketEngine::has_loss_capacity(const OrderRequest& req, int) const {
  if (market_.type != MarketType::Perpetual || !positions_) return true;
  const Position p = positions_->position(req.user, market_.id);
  if (p.signed_qty == 0) return true;
  const lob::Quantity delta = req.side == lob::Side::Bid ? req.qty : -req.qty;
  if ((p.signed_qty > 0) == (delta > 0)) return true;

  lob::Quantity old_abs = 0;
  lob::Quantity delta_abs = 0;
  if (!checked_abs_quantity(p.signed_qty, old_abs) || !checked_abs_quantity(delta, delta_abs)) return false;
  const lob::Quantity close_qty = std::min<lob::Quantity>(old_abs, delta_abs);
  const int direction = p.signed_qty > 0 ? 1 : -1;
  Amount realized = 0;
  if (!checked_realized_delta(req.price, p.entry_price, close_qty, direction, realized)) return false;
  if (realized >= 0) return true;

  Amount releasable_margin = 0;
  const auto margin_it = position_margin_.find(req.user);
  if (margin_it != position_margin_.end() && old_abs > 0) {
    if (close_qty >= old_abs) {
      releasable_margin = margin_it->second;
    } else if (!proportional_amount(margin_it->second, close_qty, old_abs, releasable_margin)) {
      return false;
    }
  }
  Amount available = 0;
  if (!checked_add_amount(ledger_.free(req.user, market_.margin_asset), releasable_margin, available)) return false;
  return available >= -realized;
}

SimulatedFill MarketEngine::simulate_fill(UserId user, lob::Side side, lob::Tick limit_price, lob::Quantity qty, uint32_t flags) const {
  OrderRequest req{market_.id, user, 0, 0, 0, side, limit_price, qty, flags};
  return simulate_fill(req);
}

SimulatedFill MarketEngine::simulate_fill(const OrderRequest& req) const {
  SimulatedFill result{};
  result.requested_qty = req.qty;

  if (market_.type == MarketType::Perpetual) {
    if (!positions_) {
      result.supported = false;
      result.code = RejectCode::UnsupportedOrderType;
      result.reason = "perpetual simulate_fill requires positions";
      return result;
    }
    constexpr uint32_t allowed_flags = lob::IOC | lob::FOK | lob::POST_ONLY | lob::STP | LOBX_REDUCE_ONLY;
    if ((req.flags & ~allowed_flags) != 0u) {
      result.code = RejectCode::UnsupportedOrderType;
      result.reason = "unsupported order flags";
      return result;
    }
    if ((req.flags & lob::IOC) != 0u && (req.flags & lob::FOK) != 0u) {
      result.code = RejectCode::UnsupportedOrderType;
      result.reason = "IOC and FOK cannot both be set";
      return result;
    }
    if (req.price <= 0) {
      result.code = RejectCode::InvalidPrice;
      result.reason = "price must be positive";
      return result;
    }
    if (req.qty <= 0) {
      result.code = RejectCode::InvalidQuantity;
      result.reason = "qty must be positive";
      return result;
    }
    if ((req.flags & LOBX_REDUCE_ONLY) != 0u && positions_->reduce_only_would_increase(req.user, market_.id, req.side, req.qty)) {
      result.code = RejectCode::ReduceOnlyWouldIncrease;
      result.reason = "reduce-only order would increase position";
      return result;
    }

    struct Candidate {
      OrderId id{0};
      UserId user{0};
      lob::Tick price{0};
      lob::Quantity qty{0};
      lob::Timestamp ts{0};
      lob::SeqNo seq{0};
    };

    std::vector<Candidate> candidates;
    candidates.reserve(open_.size());
    const bool stp = (req.flags & lob::STP) != 0u;
    for (const auto& kv : open_) {
      const OpenOrder& order = kv.second;
      if (order.leaves_qty <= 0 || order.side == req.side) continue;
      const bool crosses = req.side == lob::Side::Bid ? order.limit_price <= req.price : order.limit_price >= req.price;
      if (!crosses) continue;
      if (stp && order.user == req.user) {
        result.self_liquidity_skipped += order.leaves_qty;
        continue;
      }
      candidates.push_back(Candidate{order.id, order.user, order.limit_price, order.leaves_qty, order.ts, order.seq});
    }
    std::sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) {
      if (a.price != b.price) return req.side == lob::Side::Bid ? a.price < b.price : a.price > b.price;
      if (a.seq != b.seq) return a.seq < b.seq;
      if (a.ts != b.ts) return a.ts < b.ts;
      return a.id < b.id;
    });

    if ((req.flags & lob::POST_ONLY) != 0u && !candidates.empty()) {
      result.crosses = true;
      result.code = RejectCode::PostOnlyWouldCross;
      result.reason = "post-only order would cross";
      return result;
    }

    const bool duplicate = seen_order_ids_.find(req.order_id) != seen_order_ids_.end();
    const int requested_leverage = positions_->leverage(req.user, req.market_id);
    const Amount projected_notional = projected_notional_after(req);
    const int user_leverage = std::max(1, std::min(requested_leverage, effective_max_leverage(req.user, projected_notional)));
    const RiskDecision decision = risk_.check_limit_order(req, market_, ledger_, positions_, user_leverage, best_bid(), best_ask(), duplicate);
    result.lock_asset = decision.lock_asset;
    result.required_lock = decision.lock_amount;
    if (!decision.accepted) {
      result.code = decision.code;
      result.reason = decision.reason;
      return result;
    }
    if (!has_loss_capacity(req, user_leverage)) {
      result.code = RejectCode::InsufficientBalance;
      result.reason = "insufficient margin to realize loss";
      return result;
    }

    Position before = positions_->position(req.user, market_.id);
    result.position_qty_before = before.signed_qty;
    result.entry_price_before = before.entry_price;
    lob::Quantity simulated_qty = before.signed_qty;
    lob::Tick simulated_entry = before.entry_price;
    Amount simulated_position_margin = position_margin_.count(req.user) ? position_margin_.at(req.user) : 0;
    lob::Quantity remaining = req.qty;
    std::set<lob::Tick> consumed_prices;

    auto apply_simulated_trade = [&](lob::Tick price, lob::Quantity qty) -> bool {
      const lob::Quantity delta = req.side == lob::Side::Bid ? qty : -qty;
      lob::Quantity old_abs = 0;
      lob::Quantity delta_abs = 0;
      if (!checked_abs_quantity(simulated_qty, old_abs) || !checked_abs_quantity(delta, delta_abs)) return false;
      lob::Quantity closed_qty = 0;
      lob::Quantity opened_qty = 0;
      if (simulated_qty == 0 || ((simulated_qty > 0) == (delta > 0))) {
        opened_qty = delta_abs;
      } else {
        closed_qty = std::min<lob::Quantity>(old_abs, delta_abs);
        opened_qty = delta_abs - closed_qty;
      }
      if (closed_qty > 0) {
        Amount realized = 0;
        const int direction = simulated_qty > 0 ? 1 : -1;
        if (!checked_realized_delta(price, simulated_entry, closed_qty, direction, realized)) return false;
        Amount updated_realized = 0;
        if (!checked_add_amount(result.estimated_realized_pnl, realized, updated_realized)) return false;
        result.estimated_realized_pnl = updated_realized;
        Amount release_margin = 0;
        if (old_abs > 0 && simulated_position_margin > 0) {
          if (closed_qty >= old_abs) release_margin = simulated_position_margin;
          else if (!proportional_amount(simulated_position_margin, closed_qty, old_abs, release_margin)) return false;
        }
        simulated_position_margin = std::max<Amount>(0, simulated_position_margin - release_margin);
        result.margin_delta -= release_margin;
      }

      lob::Quantity updated_qty = 0;
      if (__builtin_add_overflow(simulated_qty, delta, &updated_qty)) return false;
      if (opened_qty > 0) {
        Amount required_margin = 0;
        if (!margin_for(price, opened_qty, user_leverage, required_margin)) return false;
        simulated_position_margin += required_margin;
        result.margin_delta += required_margin;
        if (simulated_qty == 0 || ((simulated_qty > 0) == (delta > 0))) {
          const lob::Quantity new_abs = old_abs + opened_qty;
          const long double weighted = static_cast<long double>(simulated_entry) * old_abs + static_cast<long double>(price) * opened_qty;
          simulated_entry = static_cast<lob::Tick>(weighted / new_abs);
        } else {
          simulated_entry = price;
        }
      }
      simulated_qty = updated_qty;
      if (simulated_qty == 0) simulated_entry = 0;
      return true;
    };

    for (const Candidate& candidate : candidates) {
      if (remaining <= 0) break;
      const lob::Quantity fill_qty = std::min<lob::Quantity>(remaining, candidate.qty);
      Amount fill_notional = 0;
      if (!mul_amount(candidate.price, fill_qty, fill_notional)) {
        result.code = RejectCode::InvalidNotional;
        result.reason = "simulate fill notional overflow";
        return result;
      }
      Amount taker_fee = 0;
      if (!fee_for(fill_notional, fee_config_.taker_fee_bps, taker_fee)) {
        result.code = RejectCode::InvalidNotional;
        result.reason = "simulate fill fee overflow";
        return result;
      }
      if (!apply_simulated_trade(candidate.price, fill_qty)) {
        result.code = RejectCode::InvalidNotional;
        result.reason = "simulate fill position overflow";
        return result;
      }
      result.fills.push_back(SimulatedFillLeg{candidate.price, fill_qty, candidate.user, req.user, candidate.id, req.side, fill_notional, taker_fee});
      Amount updated_notional = 0;
      if (!checked_add_amount(result.notional, fill_notional, updated_notional)) {
        result.code = RejectCode::InvalidNotional;
        result.reason = "simulate fill notional overflow";
        return result;
      }
      result.notional = updated_notional;
      result.fillable_qty += fill_qty;
      result.estimated_fee += taker_fee;
      result.estimated_taker_fee += taker_fee;
      result.worst_price = candidate.price;
      consumed_prices.insert(candidate.price);
      remaining -= fill_qty;
    }

    if ((req.flags & lob::FOK) != 0u && result.fillable_qty < req.qty) {
      result.fillable_qty = 0;
      result.estimated_filled_qty = 0;
      result.notional = 0;
      result.estimated_fee = 0;
      result.estimated_taker_fee = 0;
      result.fills.clear();
      result.code = RejectCode::InvalidQuantity;
      result.reason = "FOK order would not fully fill";
      return result;
    }

    result.crosses = result.fillable_qty > 0;
    result.fok_would_fill = result.fillable_qty >= req.qty;
    result.levels_consumed = static_cast<int>(consumed_prices.size());
    if (result.fillable_qty > 0) {
      result.avg_price = static_cast<long double>(result.notional) / static_cast<long double>(result.fillable_qty);
    }
    result.would_rest = remaining > 0 && ((req.flags & (lob::IOC | lob::FOK)) == 0u);
    result.would_accept = true;
    result.code = RejectCode::None;
    result.reason.clear();
    result.estimated_filled_qty = result.fillable_qty;
    result.estimated_notional = result.notional;
    result.estimated_required_margin = result.margin_delta > 0 ? result.margin_delta : 0;
    result.position_qty_after = simulated_qty;
    result.entry_price_after = simulated_entry;
    result.wallet_delta = result.estimated_realized_pnl - result.estimated_fee;
    Amount free_after_wallet = 0;
    if (!checked_add_amount(ledger_.free(req.user, market_.margin_asset), result.wallet_delta, free_after_wallet)) {
      result.would_accept = false;
      result.code = RejectCode::InsufficientBalance;
      result.reason = "insufficient free margin after simulated fill";
      return result;
    }
    Amount free_after_fill = 0;
    if (!checked_add_amount(free_after_wallet, -result.margin_delta, free_after_fill) || free_after_fill < 0) {
      result.would_accept = false;
      result.code = RejectCode::InsufficientBalance;
      result.reason = "insufficient free margin after simulated fill";
      return result;
    }
    if (simulated_qty != 0 && simulated_entry > 0) {
      const lob::Tick mark = mark_price();
      if (mark > 0) {
        lob::Quantity abs_qty = 0;
        Amount delta = 0;
        Amount value = 0;
        if (checked_abs_quantity(simulated_qty, abs_qty) &&
            !__builtin_sub_overflow(mark, simulated_entry, &delta) &&
            !__builtin_mul_overflow(delta, abs_qty, &value)) {
          result.estimated_unrealized_pnl_after = simulated_qty > 0 ? value : -value;
        }
      }
    }
    return result;
  }

  if (market_.type != MarketType::Spot) {
    result.supported = false;
    result.code = RejectCode::UnsupportedOrderType;
    result.reason = "simulate_fill currently supports spot markets only";
    return result;
  }
  constexpr uint32_t allowed_flags = lob::IOC | lob::FOK | lob::POST_ONLY | lob::STP;
  if ((req.flags & ~allowed_flags) != 0u) {
    result.code = RejectCode::UnsupportedOrderType;
    result.reason = "unsupported order flags";
    return result;
  }
  if ((req.flags & lob::IOC) != 0u && (req.flags & lob::FOK) != 0u) {
    result.code = RejectCode::UnsupportedOrderType;
    result.reason = "IOC and FOK cannot both be set";
    return result;
  }
  if (req.price <= 0) {
    result.code = RejectCode::InvalidPrice;
    result.reason = "price must be positive";
    return result;
  }
  if (req.qty <= 0) {
    result.code = RejectCode::InvalidQuantity;
    result.reason = "qty must be positive";
    return result;
  }

  RiskDecision decision = risk_.check_limit_order(req, market_, ledger_, positions_, 1, best_bid(), best_ask(), false);
  result.lock_asset = decision.lock_asset;
  result.required_lock = decision.lock_amount;
  if (!decision.accepted && decision.code == RejectCode::PostOnlyWouldCross) {
    result.crosses = true;
    result.would_rest = false;
    result.code = decision.code;
    result.reason = decision.reason;
    return result;
  }
  if (!decision.accepted) {
    result.code = decision.code;
    result.reason = decision.reason;
    return result;
  }

  struct Candidate {
    OrderId id{0};
    UserId user{0};
    lob::Tick price{0};
    lob::Quantity qty{0};
    lob::Timestamp ts{0};
    lob::SeqNo seq{0};
  };

  std::vector<Candidate> candidates;
  candidates.reserve(open_.size());
  const bool stp = (req.flags & lob::STP) != 0u;
  for (const auto& kv : open_) {
    const OpenOrder& order = kv.second;
    if (order.leaves_qty <= 0 || order.side == req.side) continue;
    const bool crosses = req.side == lob::Side::Bid ? order.limit_price <= req.price : order.limit_price >= req.price;
    if (!crosses) continue;
    if (stp && order.user == req.user) {
      if (result.self_liquidity_skipped > std::numeric_limits<lob::Quantity>::max() - order.leaves_qty) {
        result.code = RejectCode::InvalidQuantity;
        result.reason = "simulate fill self liquidity overflow";
        return result;
      }
      result.self_liquidity_skipped += order.leaves_qty;
      continue;
    }
    candidates.push_back(Candidate{order.id, order.user, order.limit_price, order.leaves_qty, order.ts, order.seq});
  }

  std::sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) {
    if (a.price != b.price) {
      return req.side == lob::Side::Bid ? a.price < b.price : a.price > b.price;
    }
    if (a.seq != b.seq) return a.seq < b.seq;
    if (a.ts != b.ts) return a.ts < b.ts;
    return a.id < b.id;
  });

  if ((req.flags & lob::POST_ONLY) != 0u && !candidates.empty()) {
    result.crosses = true;
    result.would_rest = false;
    result.fillable_qty = 0;
    result.notional = 0;
    result.worst_price = 0;
    result.avg_price = 0.0L;
    result.levels_consumed = 0;
    result.estimated_taker_fee = 0;
    result.code = RejectCode::PostOnlyWouldCross;
    result.reason = "post-only order would cross";
    return result;
  }

  lob::Quantity remaining = req.qty;
  std::set<lob::Tick> consumed_prices;
  for (const Candidate& candidate : candidates) {
    if (remaining <= 0) break;
    const lob::Quantity fill_qty = std::min<lob::Quantity>(remaining, candidate.qty);
    Amount fill_notional = 0;
    if (!mul_amount(candidate.price, fill_qty, fill_notional)) {
      result.code = RejectCode::InvalidNotional;
      result.reason = "simulate fill notional overflow";
      return result;
    }
    Amount updated_notional = 0;
    if (!checked_add_amount(result.notional, fill_notional, updated_notional)) {
      result.code = RejectCode::InvalidNotional;
      result.reason = "simulate fill notional overflow";
      return result;
    }
    if (result.fillable_qty > std::numeric_limits<lob::Quantity>::max() - fill_qty) {
      result.code = RejectCode::InvalidQuantity;
      result.reason = "simulate fill quantity overflow";
      return result;
    }
    result.notional = updated_notional;
    result.fillable_qty += fill_qty;
    remaining -= fill_qty;
    result.worst_price = candidate.price;
    consumed_prices.insert(candidate.price);
  }

  result.crosses = result.fillable_qty > 0;
  result.fok_would_fill = result.fillable_qty >= req.qty;
  result.levels_consumed = static_cast<int>(consumed_prices.size());
  if (result.fillable_qty > 0) {
    result.avg_price = static_cast<long double>(result.notional) / static_cast<long double>(result.fillable_qty);
    if (!fee_for(result.notional, market_.taker_fee_bps, result.estimated_taker_fee)) {
      result.code = RejectCode::InvalidNotional;
      result.reason = "simulate fill fee overflow";
      return result;
    }
  }

  const bool remainder = remaining > 0;
  result.would_rest = remainder && ((req.flags & (lob::IOC | lob::FOK | lob::POST_ONLY)) == 0u);
  if ((req.flags & lob::POST_ONLY) != 0u && !result.crosses) result.would_rest = true;
  return result;
}

bool MarketEngine::purge_invalid_reduce_only_orders(const OrderRequest& req, std::vector<PendingEvent>& pending_events) {
  if (market_.type != MarketType::Perpetual || !positions_) return true;
  std::vector<OrderId> invalid;
  for (const auto& kv : open_) {
    const OpenOrder& order = kv.second;
    if ((order.flags & LOBX_REDUCE_ONLY) == 0u || order.side == req.side) continue;
    const bool crosses = req.side == lob::Side::Bid ? order.limit_price <= req.price : order.limit_price >= req.price;
    if (!crosses) continue;
    if (positions_->reduce_only_would_increase(order.user, market_.id, order.side, order.leaves_qty)) {
      invalid.push_back(order.id);
    }
  }
  for (const OrderId id : invalid) {
    PendingEvent event{};
    if (!cancel_order(id, std::nullopt, req.ts, &event)) return false;
    pending_events.push_back(std::move(event));
  }
  return true;
}

void MarketEngine::reset_book() {
  book_ = std::make_unique<lob::BookCore>(bids_, asks_, &logger_);
}

void MarketEngine::rebuild_book(const std::vector<BookRestingOrder>& orders) {
  book_.reset();
  bids_ = lob::PriceLevelsSparse{};
  asks_ = lob::PriceLevelsSparse{};
  reset_book();
  logger_.clear();
  for (const BookRestingOrder& resting : orders) {
    const lob::NewOrder order{resting.seq, resting.ts, resting.id, resting.user, resting.side, resting.price, resting.qty, lob::NONE};
    (void)book_->submit_limit(order);
  }
  logger_.clear();
}

MarketEngine::Snapshot MarketEngine::make_snapshot() const {
  Snapshot snapshot{};
  bids_.for_each_order([&](lob::Tick px, lob::OrderNode* node) {
    lob::SeqNo seq = 0;
    auto it = open_.find(node->id);
    if (it != open_.end()) seq = it->second.seq;
    snapshot.book_orders.push_back(BookRestingOrder{node->id, node->user, seq, lob::Side::Bid, px, node->qty, node->ts});
  });
  asks_.for_each_order([&](lob::Tick px, lob::OrderNode* node) {
    lob::SeqNo seq = 0;
    auto it = open_.find(node->id);
    if (it != open_.end()) seq = it->second.seq;
    snapshot.book_orders.push_back(BookRestingOrder{node->id, node->user, seq, lob::Side::Ask, px, node->qty, node->ts});
  });
  snapshot.open = open_;
  snapshot.position_margin = position_margin_;
  snapshot.fee_totals = fee_totals_;
  snapshot.funding_totals = funding_totals_;
  snapshot.insurance_fund_balance = insurance_fund_balance_;
  snapshot.bad_debt = bad_debt_;
  snapshot.trigger_orders = trigger_orders_;
  snapshot.ledger = ledger_.snapshot();
  if (positions_) snapshot.positions = positions_->snapshot();
  return snapshot;
}

void MarketEngine::restore_snapshot(const Snapshot& snapshot) {
  open_ = snapshot.open;
  position_margin_ = snapshot.position_margin;
  fee_totals_ = snapshot.fee_totals;
  funding_totals_ = snapshot.funding_totals;
  insurance_fund_balance_ = snapshot.insurance_fund_balance;
  bad_debt_ = snapshot.bad_debt;
  trigger_orders_ = snapshot.trigger_orders;
  ledger_.restore(snapshot.ledger);
  if (positions_) positions_->restore(snapshot.positions);
  rebuild_book(snapshot.book_orders);
}

MarketEngine::SubmitSnapshot MarketEngine::make_submit_snapshot() const {
  SubmitSnapshot snapshot{};
  bids_.for_each_order([&](lob::Tick px, lob::OrderNode* node) {
    lob::SeqNo seq = 0;
    auto it = open_.find(node->id);
    if (it != open_.end()) seq = it->second.seq;
    snapshot.book_orders.push_back(BookRestingOrder{node->id, node->user, seq, lob::Side::Bid, px, node->qty, node->ts});
  });
  asks_.for_each_order([&](lob::Tick px, lob::OrderNode* node) {
    lob::SeqNo seq = 0;
    auto it = open_.find(node->id);
    if (it != open_.end()) seq = it->second.seq;
    snapshot.book_orders.push_back(BookRestingOrder{node->id, node->user, seq, lob::Side::Ask, px, node->qty, node->ts});
  });
  snapshot.open = open_;
  snapshot.ledger = ledger_.snapshot();
  if (positions_) snapshot.positions = positions_->snapshot();
  return snapshot;
}

void MarketEngine::restore_submit_snapshot(const SubmitSnapshot& snapshot) {
  open_ = snapshot.open;
  ledger_.restore(snapshot.ledger);
  if (positions_) positions_->restore(snapshot.positions);
  rebuild_book(snapshot.book_orders);
}

void MarketEngine::remember_scalar_value(std::unordered_map<UserId, Amount>& map,
                                         std::vector<ScalarMapUndoEntry>& undo,
                                         UserId user) {
  for (const ScalarMapUndoEntry& entry : undo) {
    if (entry.user == user) return;
  }
  auto it = map.find(user);
  undo.push_back(ScalarMapUndoEntry{user, it != map.end(), it == map.end() ? 0 : it->second});
}

void MarketEngine::restore_scalar_values(std::unordered_map<UserId, Amount>& map,
                                         const std::vector<ScalarMapUndoEntry>& undo) {
  for (auto it = undo.rbegin(); it != undo.rend(); ++it) {
    if (it->existed) {
      map[it->user] = it->old_value;
    } else {
      map.erase(it->user);
    }
  }
}

void MarketEngine::restore_scalar_map_undo(const ScalarMapUndo& undo) {
  restore_scalar_values(position_margin_, undo.position_margin);
  restore_scalar_values(fee_totals_, undo.fee_totals);
  restore_scalar_values(funding_totals_, undo.funding_totals);
}

void MarketEngine::remember_position_margin(UserId user) {
  if (!active_scalar_undo_) return;
  remember_scalar_value(position_margin_, active_scalar_undo_->position_margin, user);
}

void MarketEngine::remember_fee_total(UserId user) {
  if (!active_scalar_undo_) return;
  remember_scalar_value(fee_totals_, active_scalar_undo_->fee_totals, user);
}

void MarketEngine::remember_funding_total(UserId user) {
  if (!active_scalar_undo_) return;
  remember_scalar_value(funding_totals_, active_scalar_undo_->funding_totals, user);
}

bool MarketEngine::build_event_payloads() const {
  return !retention_options_ || retention_options_->build_event_payloads;
}

void MarketEngine::append_event(lob::Timestamp ts, const std::string& type, const std::string& payload) {
  if (events_) events_->append(ts, type, payload);
}

std::string MarketEngine::order_payload(const OrderRequest& req) const {
  if (!build_event_payloads()) return {};
  std::ostringstream payload;
  payload << "market=" << market_.symbol << ",user=" << req.user << ",order=" << req.order_id
          << ",side=" << (req.side == lob::Side::Bid ? "BID" : "ASK")
          << ",price=" << req.price << ",qty=" << req.qty << ",flags=" << req.flags;
  return payload.str();
}

std::string MarketEngine::trade_payload(const TradeEvent& trade) const {
  if (!build_event_payloads()) return {};
  std::ostringstream payload;
  payload << "market=" << market_.symbol << ",price=" << trade.price << ",qty=" << trade.qty
          << ",buyer=" << trade.buyer << ",seller=" << trade.seller
          << ",buyer_order=" << trade.buyer_order_id << ",seller_order=" << trade.seller_order_id;
  if (market_.type == MarketType::Perpetual) payload << ",type=perpetual";
  return payload.str();
}

void MarketEngine::append_trigger_event(lob::Timestamp ts, const std::string& type, const TriggerOrder& trigger, const std::string& extra) {
  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << "market=" << market_.symbol << ",market_id=" << market_.id
       << ",trigger_order_id=" << trigger.request.trigger_order_id
       << ",account_id=" << trigger.request.user
       << ",side=" << (trigger.request.side == lob::Side::Bid ? "BID" : "ASK")
       << ",qty=" << trigger.request.qty
       << ",trigger_price=" << trigger.request.trigger_price
       << ",status=" << static_cast<int>(trigger.status)
       << ",child_order_id=" << trigger.child_order_id;
    if (!extra.empty()) os << "," << extra;
    payload = os.str();
  }
  append_event(ts, type, payload);
}

void MarketEngine::append_reject_event(const OrderRequest& req, RejectCode code, const std::string& reason) {
  std::string payload;
  if (build_event_payloads()) {
    std::ostringstream os;
    os << order_payload(req) << ",code=" << reject_code_name(code) << ",reason=" << reason;
    payload = os.str();
  }
  append_event(req.ts, "order.rejected", payload);
}

lob::Tick MarketEngine::trigger_reference_price(TriggerPriceType price_type) const {
  if (price_type == TriggerPriceType::Last) return last_trade_price_;
  if (price_type == TriggerPriceType::Index) return index_price_;
  return mark_price();
}

bool MarketEngine::trigger_condition_met(const TriggerOrder& trigger, lob::Tick reference) const {
  if (reference <= 0) return false;
  if (trigger.request.trigger_condition == TriggerCondition::AboveOrEqual) return reference >= trigger.request.trigger_price;
  return reference <= trigger.request.trigger_price;
}

bool MarketEngine::can_release_order_lock(OrderId order_id) const {
  auto it = open_.find(order_id);
  if (it == open_.end()) return false;
  const OpenOrder& o = it->second;
  return o.locked_remaining <= 0 || ledger_.locked(o.user, o.locked_asset) >= o.locked_remaining;
}

bool MarketEngine::release_and_erase(OrderId order_id) {
  auto it = open_.find(order_id);
  if (it == open_.end()) return false;
  auto& o = it->second;
  if (o.locked_remaining > 0) {
    Result r = ledger_.release(o.user, o.locked_asset, o.locked_remaining);
    if (!r.ok) return false;
    o.locked_remaining = 0;
  }
  open_.erase(it);
  if (fault_active(FaultPoint::AfterReleaseAndErase)) return false;
  return true;
}

bool MarketEngine::cancel_order(OrderId order_id, std::optional<UserId> owner, lob::Timestamp ts, PendingEvent* pending_event) {
  if (ts <= 0) return false;
  auto it = open_.find(order_id);
  if (it == open_.end()) return false;
  if (owner.has_value() && it->second.user != *owner) return false;
  if (!can_release_order_lock(order_id)) return false;
  const bool canceled = book_->cancel(order_id);
  if (!canceled) return false;
  if (!release_and_erase(order_id)) return false;
  if (pending_event) {
    std::string payload;
    if (build_event_payloads()) {
      std::ostringstream os;
      os << "market=" << market_.symbol << ",order=" << order_id;
      payload = os.str();
    }
    *pending_event = PendingEvent{ts, "order.canceled", std::move(payload)};
  }
  return true;
}

bool MarketEngine::adjust_resting_lock(OrderId order_id) {
  auto it = open_.find(order_id);
  if (it == open_.end()) return false;
  auto& o = it->second;
  if (o.leaves_qty <= 0) {
    return release_and_erase(order_id);
  }
  Amount target = 0;
  if (!target_lock_for(o, target)) return false;
  if (o.locked_remaining > target) {
    const Amount excess = o.locked_remaining - target;
    Result r = ledger_.release(o.user, o.locked_asset, excess);
    if (!r.ok) return false;
    o.locked_remaining = target;
  } else if (o.locked_remaining < target) {
    const Amount extra = target - o.locked_remaining;
    const Result r = ledger_.lock(o.user, o.locked_asset, extra);
    if (!r.ok) return false;
    o.locked_remaining = target;
  }
  if (fault_active(FaultPoint::AfterAdjustRestingLock)) return false;
  return true;
}

bool MarketEngine::settle_spot_fill(const RawFill& fill, TradeEvent& out) {
  auto pit = open_.find(fill.passive_order_id);
  auto tit = open_.find(fill.taker_order_id);
  if (pit == open_.end() || tit == open_.end()) return false;

  OpenOrder& passive = pit->second;
  OpenOrder& taker = tit->second;

  Amount quote_amount = 0;
  if (!mul_amount(fill.price, fill.qty, quote_amount)) return false;

  OpenOrder* buyer = nullptr;
  OpenOrder* seller = nullptr;
  if (fill.liquidity_side == lob::Side::Ask) {
    seller = &passive;
    buyer = &taker;
  } else {
    buyer = &passive;
    seller = &taker;
  }

  Result r = ledger_.debit_locked(buyer->user, market_.quote_asset, quote_amount);
  if (!r.ok) return false;
  buyer->locked_remaining = std::max<Amount>(0, buyer->locked_remaining - quote_amount);
  r = ledger_.credit(buyer->user, market_.base_asset, fill.qty);
  if (!r.ok) return false;

  r = ledger_.debit_locked(seller->user, market_.base_asset, fill.qty);
  if (!r.ok) return false;
  seller->locked_remaining = std::max<Amount>(0, seller->locked_remaining - fill.qty);
  r = ledger_.credit(seller->user, market_.quote_asset, quote_amount);
  if (!r.ok) return false;

  Amount taker_fee = 0;
  if (!fee_for(quote_amount, market_.taker_fee_bps, taker_fee)) return false;
  if (taker_fee > 0) {
    if (taker.side == lob::Side::Bid) {
      r = ledger_.debit_locked(taker.user, market_.quote_asset, taker_fee);
      if (!r.ok) return false;
      taker.locked_remaining = std::max<Amount>(0, taker.locked_remaining - taker_fee);
    } else {
      r = ledger_.withdraw(taker.user, market_.quote_asset, taker_fee);
      if (!r.ok) return false;
    }
    r = ledger_.credit(fee_account_user(), market_.quote_asset, taker_fee);
    if (!r.ok) return false;
  }

  buyer->leaves_qty = std::max<lob::Quantity>(0, buyer->leaves_qty - fill.qty);
  seller->leaves_qty = std::max<lob::Quantity>(0, seller->leaves_qty - fill.qty);

  out = TradeEvent{market_.id,
                   fill.ts,
                   fill.price,
                   fill.qty,
                   buyer->user,
                   seller->user,
                   buyer->id,
                   seller->id,
                   fill.liquidity_side};

  const OrderId passive_id = fill.passive_order_id;
  if (passive_id != fill.taker_order_id) {
    auto now = open_.find(passive_id);
    if (now != open_.end() && now->second.leaves_qty <= 0) {
      if (!release_and_erase(passive_id)) return false;
    }
  }
  return true;
}

bool MarketEngine::can_settle_perpetual_participant(const OpenOrder& order, lob::Side fill_side, lob::Tick price, lob::Quantity qty, Amount fee) const {
  if (!positions_ || qty <= 0) return false;
  const Position before = positions_->position(order.user, market_.id);
  const lob::Quantity delta = fill_side == lob::Side::Bid ? qty : -qty;
  lob::Quantity old_abs = 0;
  lob::Quantity delta_abs = 0;
  if (!checked_abs_quantity(before.signed_qty, old_abs) || !checked_abs_quantity(delta, delta_abs)) return false;
  lob::Quantity closed_qty = 0;
  lob::Quantity opened_qty = 0;
  if (before.signed_qty == 0 || ((before.signed_qty > 0) == (delta > 0))) {
    opened_qty = delta_abs;
  } else {
    closed_qty = std::min<lob::Quantity>(old_abs, delta_abs);
    opened_qty = delta_abs - closed_qty;
  }

  Amount old_position_margin = 0;
  auto margin_it = position_margin_.find(order.user);
  if (margin_it != position_margin_.end()) old_position_margin = margin_it->second;
  Amount release_margin = 0;
  if (closed_qty > 0 && old_abs > 0 && old_position_margin > 0) {
    if (closed_qty >= old_abs) {
      release_margin = old_position_margin;
    } else if (!proportional_amount(old_position_margin, closed_qty, old_abs, release_margin)) {
      return false;
    }
  }

  Amount realized_delta = 0;
  if (closed_qty > 0) {
    const int direction = before.signed_qty > 0 ? 1 : -1;
    if (!checked_realized_delta(price, before.entry_price, closed_qty, direction, realized_delta)) return false;
  }

  Amount free_after_loss = 0;
  if (!checked_add_amount(ledger_.free(order.user, market_.margin_asset), release_margin, free_after_loss)) return false;
  if (realized_delta < 0) {
    if (free_after_loss < -realized_delta) return false;
    free_after_loss -= -realized_delta;
  }

  OpenOrder after_fill = order;
  after_fill.leaves_qty = std::max<lob::Quantity>(0, after_fill.leaves_qty - qty);
  Amount target_after = 0;
  if (!target_lock_for(after_fill, target_after)) return false;
  const Amount available_fill_lock = order.locked_remaining > target_after ? order.locked_remaining - target_after : 0;
  Amount required_open_margin = 0;
  if (!margin_for(price, opened_qty, order.leverage, required_open_margin)) return false;
  if (required_open_margin > available_fill_lock && free_after_loss < required_open_margin - available_fill_lock) return false;
  const Amount margin_shortfall = required_open_margin > available_fill_lock ? required_open_margin - available_fill_lock : 0;
  if (free_after_loss < margin_shortfall) return false;
  return free_after_loss - margin_shortfall >= fee;
}

bool MarketEngine::settle_perpetual_participant(OpenOrder& order, lob::Side fill_side, lob::Tick price, lob::Quantity qty) {
  if (!can_settle_perpetual_participant(order, fill_side, price, qty)) return false;

  const Position before = positions_->position(order.user, market_.id);
  const lob::Quantity delta = fill_side == lob::Side::Bid ? qty : -qty;
  lob::Quantity old_abs = 0;
  lob::Quantity delta_abs = 0;
  if (!checked_abs_quantity(before.signed_qty, old_abs) || !checked_abs_quantity(delta, delta_abs)) return false;
  lob::Quantity closed_qty = 0;
  lob::Quantity opened_qty = 0;
  if (before.signed_qty == 0 || ((before.signed_qty > 0) == (delta > 0))) {
    opened_qty = delta_abs;
  } else {
    closed_qty = std::min<lob::Quantity>(old_abs, delta_abs);
    opened_qty = delta_abs - closed_qty;
  }

  remember_position_margin(order.user);
  const Amount old_position_margin = position_margin_[order.user];
  Amount release_margin = 0;
  if (closed_qty > 0 && old_abs > 0 && old_position_margin > 0) {
    if (closed_qty >= old_abs) {
      release_margin = old_position_margin;
    } else if (!proportional_amount(old_position_margin, closed_qty, old_abs, release_margin)) {
      return false;
    }
    if (release_margin > 0) {
      Result r = ledger_.release(order.user, market_.margin_asset, release_margin);
      if (!r.ok) return false;
      position_margin_[order.user] = std::max<Amount>(0, position_margin_[order.user] - release_margin);
    }
  }

  Amount realized_delta = 0;
  if (closed_qty > 0) {
    const int direction = before.signed_qty > 0 ? 1 : -1;
    if (!checked_realized_delta(price, before.entry_price, closed_qty, direction, realized_delta)) return false;
  }
  if (realized_delta > 0) {
    Result r = ledger_.credit(order.user, market_.margin_asset, realized_delta);
    if (!r.ok) return false;
  } else if (realized_delta < 0) {
    Result r = ledger_.withdraw(order.user, market_.margin_asset, -realized_delta);
    if (!r.ok) return false;
  }

  OpenOrder after_fill = order;
  after_fill.leaves_qty = std::max<lob::Quantity>(0, after_fill.leaves_qty - qty);
  Amount target_after = 0;
  if (!target_lock_for(after_fill, target_after)) return false;
  Amount available_fill_lock = order.locked_remaining > target_after ? order.locked_remaining - target_after : 0;
  Amount required_open_margin = 0;
  if (!margin_for(price, opened_qty, order.leverage, required_open_margin)) return false;
  if (required_open_margin > available_fill_lock) {
    const Amount extra = required_open_margin - available_fill_lock;
    Result r = ledger_.lock(order.user, order.locked_asset, extra);
    if (!r.ok) return false;
    if (!checked_add_amount(order.locked_remaining, extra, order.locked_remaining)) return false;
    if (!checked_add_amount(available_fill_lock, extra, available_fill_lock)) return false;
  }

  order.leaves_qty = after_fill.leaves_qty;
  order.locked_remaining -= available_fill_lock;

  const Amount margin_to_retain = required_open_margin;
  if (margin_to_retain > 0) {
    Amount updated_margin = 0;
    if (!checked_add_amount(position_margin_[order.user], margin_to_retain, updated_margin)) return false;
    position_margin_[order.user] = updated_margin;
  }

  const Amount margin_to_release = available_fill_lock - margin_to_retain;
  if (margin_to_release > 0) {
    Result r = ledger_.release(order.user, order.locked_asset, margin_to_release);
    if (!r.ok) return false;
  }

  if (!positions_->apply_trade_checked(order.user, market_.id, fill_side, price, qty)) return false;
  if (fault_active(FaultPoint::AfterPositionApply)) return false;
  return true;
}

bool MarketEngine::settle_perpetual_fill(const RawFill& fill, TradeEvent& out, std::vector<PendingEvent>& pending_events) {
  auto pit = open_.find(fill.passive_order_id);
  auto tit = open_.find(fill.taker_order_id);
  if (pit == open_.end() || tit == open_.end() || !positions_) return false;

  OpenOrder& passive = pit->second;
  OpenOrder& taker = tit->second;

  OpenOrder* buyer = nullptr;
  OpenOrder* seller = nullptr;
  if (fill.liquidity_side == lob::Side::Ask) {
    seller = &passive;
    buyer = &taker;
  } else {
    buyer = &passive;
    seller = &taker;
  }

  Amount notional = 0;
  if (!mul_amount(fill.price, fill.qty, notional)) return false;
  Amount maker_fee = 0;
  Amount taker_fee = 0;
  if (!fee_for(notional, fee_config_.maker_fee_bps, maker_fee)) return false;
  if (!fee_for(notional, fee_config_.taker_fee_bps, taker_fee)) return false;
  const Amount buyer_fee = buyer == &passive ? maker_fee : taker_fee;
  const Amount seller_fee = seller == &passive ? maker_fee : taker_fee;

  if (!can_settle_perpetual_participant(*buyer, lob::Side::Bid, fill.price, fill.qty, buyer_fee)) return false;
  if (!can_settle_perpetual_participant(*seller, lob::Side::Ask, fill.price, fill.qty, seller_fee)) return false;
  if (!settle_perpetual_participant(*buyer, lob::Side::Bid, fill.price, fill.qty)) return false;
  if (!settle_perpetual_participant(*seller, lob::Side::Ask, fill.price, fill.qty)) return false;
  if (!charge_perp_fee(passive.user, maker_fee, false, fill.ts, notional, fee_config_.maker_fee_bps, &pending_events)) return false;
  if (!charge_perp_fee(taker.user, taker_fee, false, fill.ts, notional, fee_config_.taker_fee_bps, &pending_events)) return false;

  out = TradeEvent{market_.id,
                   fill.ts,
                   fill.price,
                   fill.qty,
                   buyer->user,
                   seller->user,
                   buyer->id,
                   seller->id,
                   fill.liquidity_side};

  const OrderId passive_id = fill.passive_order_id;
  if (passive_id != fill.taker_order_id) {
    auto now = open_.find(passive_id);
    if (now != open_.end() && now->second.leaves_qty <= 0) {
      if (!release_and_erase(passive_id)) return false;
    }
  }
  return true;
}

bool MarketEngine::settle_fill(const RawFill& fill, const OrderRequest&, TradeEvent& out, std::vector<PendingEvent>& pending_events) {
  if (market_.type == MarketType::Perpetual) return settle_perpetual_fill(fill, out, pending_events);
  return settle_spot_fill(fill, out);
}

SubmitResult MarketEngine::submit_market(const OrderRequest& req, lob::Tick protection_price) {
  if (protection_price <= 0) {
    SubmitResult failed{};
    failed.accepted = false;
    failed.code = RejectCode::InvalidPrice;
    failed.reason = "market order requires protection price";
    append_reject_event(req, failed.code, failed.reason);
    return failed;
  }
  OrderRequest protected_ioc = req;
  protected_ioc.price = protection_price;
  protected_ioc.flags |= lob::IOC;
  if (available_to_fill(protected_ioc) <= 0) {
    SubmitResult failed{};
    failed.accepted = false;
    failed.code = RejectCode::InvalidPrice;
    failed.reason = "market order protection prevents execution";
    append_reject_event(protected_ioc, failed.code, failed.reason);
    return failed;
  }
  SubmitResult result = submit_limit(protected_ioc);
  if (result.accepted) {
    std::string payload;
    if (build_event_payloads()) {
      std::ostringstream os;
      os << order_payload(protected_ioc) << ",order_type=market,protection_price=" << protection_price;
      payload = os.str();
    }
    append_event(protected_ioc.ts, "order.market", payload);
  }
  return result;
}

SubmitResult MarketEngine::submit_limit(const OrderRequest& req) {
  SubmitResult result{};
  std::vector<PendingEvent> pending_events;
  const SubmitSnapshot snapshot = make_submit_snapshot();
  ScalarMapUndo scalar_undo;
  struct ScalarUndoScope {
    MarketEngine& engine;
    ScalarMapUndo* previous{nullptr};

    ScalarUndoScope(MarketEngine& engine_ref, ScalarMapUndo& undo)
        : engine(engine_ref), previous(engine_ref.active_scalar_undo_) {
      engine.active_scalar_undo_ = &undo;
    }

    ~ScalarUndoScope() {
      engine.active_scalar_undo_ = previous;
    }
  } scalar_undo_scope{*this, scalar_undo};
  (void)scalar_undo_scope;

  bool inserted_seen_order_id = false;
  auto mark_seen_order_id = [&]() {
    auto [it, inserted] = seen_order_ids_.insert(req.order_id);
    (void)it;
    inserted_seen_order_id = inserted_seen_order_id || inserted;
  };
  auto rollback_seen_order_id_if_needed = [&]() {
    if (inserted_seen_order_id) {
      seen_order_ids_.erase(req.order_id);
      inserted_seen_order_id = false;
    }
  };
  auto fail_after_mutation = [&](RejectCode code, const std::string& reason) -> SubmitResult {
    restore_submit_snapshot(snapshot);
    restore_scalar_map_undo(scalar_undo);
    rollback_seen_order_id_if_needed();
    SubmitResult failed{};
    failed.accepted = false;
    failed.code = code;
    failed.reason = reason;
    append_reject_event(req, code, reason);
    return failed;
  };

  if (req.user == kDedicatedFeeAccountUser) {
    result.accepted = false;
    result.code = RejectCode::UnsupportedOrderType;
    result.reason = "system fee account cannot submit orders";
    append_reject_event(req, result.code, result.reason);
    return result;
  }

  if (!purge_invalid_reduce_only_orders(req, pending_events)) {
    return fail_after_mutation(RejectCode::InternalError, "failed to purge invalid reduce-only order");
  }

  const bool duplicate = seen_order_ids_.find(req.order_id) != seen_order_ids_.end();
  const int requested_leverage = positions_ ? positions_->leverage(req.user, req.market_id) : 1;
  const Amount projected_notional = market_.type == MarketType::Perpetual ? projected_notional_after(req) : 0;
  const int user_leverage = std::max(1, std::min(requested_leverage, effective_max_leverage(req.user, projected_notional)));
  const RiskDecision decision = risk_.check_limit_order(req, market_, ledger_, positions_, user_leverage, best_bid(), best_ask(), duplicate);
  if (!decision.accepted) {
    return fail_after_mutation(decision.code, decision.reason);
  }

  if ((req.flags & lob::FOK) != 0u && available_to_fill(req) < req.qty) {
    mark_seen_order_id();
    result.accepted = true;
    result.exec.filled = 0;
    result.exec.remaining = req.qty;
    for (const PendingEvent& event : pending_events) append_event(event.ts, event.type, event.payload);
    std::string payload;
    if (build_event_payloads()) {
      payload = "market=" + market_.symbol + ",order=" + std::to_string(req.order_id) + ",reason=fok_insufficient_liquidity";
    }
    append_event(req.ts, "order.expired", payload);
    return result;
  }

  if (!has_loss_capacity(req, user_leverage)) {
    return fail_after_mutation(RejectCode::InsufficientBalance, "insufficient margin to realize loss");
  }

  Result lock_result = ledger_.lock(req.user, decision.lock_asset, decision.lock_amount);
  if (!lock_result.ok) {
    return fail_after_mutation(lock_result.code, lock_result.reason);
  }

  mark_seen_order_id();
  open_[req.order_id] = OpenOrder{req.order_id,
                                  req.user,
                                  req.seq,
                                  req.side,
                                  req.price,
                                  req.qty,
                                  req.ts,
                                  decision.lock_asset,
                                  decision.lock_amount,
                                  user_leverage,
                                  req.flags};

  const std::string accepted_payload = build_event_payloads() ? order_payload(req) : std::string{};

  logger_.clear();
  const uint32_t book_flags = (req.flags & lob::FOK) != 0u ? (req.flags | lob::IOC) : req.flags;
  const lob::NewOrder order{req.seq, req.ts, req.order_id, req.user, req.side, req.price, req.qty, book_flags};
  result.exec = book_->submit_limit(order);
  result.accepted = true;

  if (fault_active(FaultPoint::AfterBookSubmit)) {
    return fail_after_mutation(RejectCode::InternalError, "forced failure after book submit");
  }

  for (const RawFill& fill : logger_.fills) {
    TradeEvent ev{};
    if (!settle_fill(fill, req, ev, pending_events)) {
      return fail_after_mutation(RejectCode::InternalError, "failed to settle fill");
    }
    result.trades.push_back(ev);
  }

  for (const OrderId canceled_id : logger_.cancels) {
    if (!release_and_erase(canceled_id)) {
      return fail_after_mutation(RejectCode::InternalError, "failed to release canceled order lock");
    }
  }

  auto it = open_.find(req.order_id);
  if (it != open_.end()) {
    it->second.leaves_qty = result.exec.remaining;
    const bool can_rest = (result.exec.remaining > 0) && ((req.flags & (lob::IOC | lob::FOK)) == 0u);
    if (can_rest) {
      if (!adjust_resting_lock(req.order_id)) {
        return fail_after_mutation(RejectCode::InternalError, "failed to adjust resting order lock");
      }
    } else {
      if (!release_and_erase(req.order_id)) {
        return fail_after_mutation(RejectCode::InternalError, "failed to release completed order lock");
      }
    }
  }

  if (fault_active(FaultPoint::BeforeInvariantCheck)) {
    return fail_after_mutation(RejectCode::InternalError, "forced failure before invariant check");
  }

  if (fault_active(FaultPoint::ForceLedgerInvariantFailure) || !ledger_.invariant_ok()) {
    return fail_after_mutation(RejectCode::InternalError, "ledger invariant violation");
  }

  for (const PendingEvent& event : pending_events) append_event(event.ts, event.type, event.payload);
  append_event(req.ts, "order.accepted", accepted_payload);
  for (const TradeEvent& trade : result.trades) {
    if (market_.type == MarketType::Perpetual) last_trade_price_ = trade.price;
    append_event(trade.ts, "trade", trade_payload(trade));
  }
  return result;
}

bool MarketEngine::cancel(OrderId order_id, std::optional<UserId> owner, lob::Timestamp ts) {
  const Snapshot snapshot = make_snapshot();
  PendingEvent event{};
  if (!cancel_order(order_id, owner, ts, &event)) {
    restore_snapshot(snapshot);
    return false;
  }
  append_event(event.ts, event.type, event.payload);
  return true;
}

Result MarketEngine::create_trigger_order(const TriggerOrderRequest& req) {
  if (market_.type != MarketType::Perpetual) return Result::fail(RejectCode::UnsupportedOrderType, "trigger order requires perpetual market");
  if (req.market_id != market_.id) return Result::fail(RejectCode::UnknownMarket, "trigger market mismatch");
  if (req.trigger_order_id == 0) return Result::fail(RejectCode::InvalidQuantity, "trigger order id must be non-zero");
  if (trigger_orders_.find(req.trigger_order_id) != trigger_orders_.end() || seen_order_ids_.find(req.trigger_order_id) != seen_order_ids_.end()) {
    return Result::fail(RejectCode::DuplicateOrderId, "duplicate trigger order id");
  }
  if (req.qty <= 0) return Result::fail(RejectCode::InvalidQuantity, "trigger qty must be positive");
  if (req.trigger_price <= 0) return Result::fail(RejectCode::InvalidPrice, "trigger price must be positive");
  if (req.child_order_type == TriggerChildOrderType::Limit && req.child_limit_price <= 0) {
    return Result::fail(RejectCode::InvalidPrice, "trigger child limit price must be positive");
  }
  if (req.child_order_type == TriggerChildOrderType::Market && req.protection_price <= 0) {
    return Result::fail(RejectCode::InvalidPrice, "trigger child market requires protection price");
  }
  constexpr uint32_t allowed_flags = lob::STP | LOBX_REDUCE_ONLY;
  if ((req.flags & ~allowed_flags) != 0u) return Result::fail(RejectCode::UnsupportedOrderType, "unsupported trigger flags");

  TriggerOrder trigger{};
  trigger.request = req;
  trigger.status = TriggerOrderStatus::Active;
  trigger_orders_[req.trigger_order_id] = trigger;
  append_trigger_event(req.ts, "trigger.created", trigger_orders_[req.trigger_order_id]);
  return Result::success();
}

bool MarketEngine::cancel_trigger_order(OrderId trigger_order_id, std::optional<UserId> owner, lob::Timestamp ts) {
  auto it = trigger_orders_.find(trigger_order_id);
  if (it == trigger_orders_.end()) return false;
  if (owner.has_value() && it->second.request.user != *owner) return false;
  if (it->second.status != TriggerOrderStatus::Active) return false;
  it->second.status = TriggerOrderStatus::Cancelled;
  append_trigger_event(ts > 0 ? ts : 0, "trigger.cancelled", it->second);
  return true;
}

std::vector<TriggerOrder> MarketEngine::trigger_orders() const {
  std::vector<TriggerOrder> out;
  out.reserve(trigger_orders_.size());
  for (const auto& kv : trigger_orders_) out.push_back(kv.second);
  std::sort(out.begin(), out.end(), [](const TriggerOrder& a, const TriggerOrder& b) {
    return a.request.trigger_order_id < b.request.trigger_order_id;
  });
  return out;
}

int MarketEngine::evaluate_triggers(TriggerPriceType price_type, lob::Timestamp ts) {
  const lob::Tick reference = trigger_reference_price(price_type);
  if (reference <= 0) return 0;
  std::vector<OrderId> ready;
  for (const auto& kv : trigger_orders_) {
    const TriggerOrder& trigger = kv.second;
    if (trigger.status != TriggerOrderStatus::Active) continue;
    if (trigger.request.trigger_price_type != price_type) continue;
    if (trigger_condition_met(trigger, reference)) ready.push_back(kv.first);
  }

  int fired = 0;
  for (OrderId id : ready) {
    auto it = trigger_orders_.find(id);
    if (it == trigger_orders_.end() || it->second.status != TriggerOrderStatus::Active) continue;
    TriggerOrder& trigger = it->second;
    OrderRequest child{market_.id,
                       trigger.request.user,
                       trigger.request.trigger_order_id,
                       0,
                       ts > 0 ? ts : trigger.request.ts,
                       trigger.request.side,
                       trigger.request.child_order_type == TriggerChildOrderType::Limit ? trigger.request.child_limit_price : trigger.request.protection_price,
                       trigger.request.qty,
                       trigger.request.flags};
    SubmitResult child_result = trigger.request.child_order_type == TriggerChildOrderType::Market
      ? submit_market(child, trigger.request.protection_price)
      : submit_limit(child);
    trigger.child_order_id = child.order_id;
    if (child_result.accepted) {
      trigger.status = TriggerOrderStatus::Triggered;
      append_trigger_event(child.ts, "trigger.fired", trigger, "reference_price=" + std::to_string(reference));
      append_trigger_event(child.ts, "trigger.child_order", trigger);
      ++fired;
    } else {
      trigger.status = TriggerOrderStatus::Failed;
      trigger.failure_reason = child_result.reason;
      append_trigger_event(child.ts, "trigger.failed", trigger, "reason=" + child_result.reason);
    }
  }
  return fired;
}

std::vector<std::pair<lob::Tick, lob::Quantity>> MarketEngine::topN(lob::Side side, int levels) {
  return book_->topN(side, levels);
}

std::vector<OpenOrder> MarketEngine::open_orders() const {
  std::vector<OpenOrder> out;
  out.reserve(open_.size());
  for (const auto& kv : open_) out.push_back(kv.second);
  std::sort(out.begin(), out.end(), [](const OpenOrder& a, const OpenOrder& b) { return a.id < b.id; });
  return out;
}

} // namespace lobx
