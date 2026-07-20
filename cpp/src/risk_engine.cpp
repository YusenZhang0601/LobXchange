#include "lobx/risk_engine.hpp"

#include <algorithm>
#include <limits>

namespace lobx {

namespace {

Amount fee_for_notional(Amount notional, int fee_bps) {
  if (notional <= 0 || fee_bps <= 0) return 0;
  if (notional > std::numeric_limits<Amount>::max() / fee_bps) return -1;
  return (notional * fee_bps) / 10000;
}

bool margin_for_notional(Amount notional, int leverage, Amount& out) {
  out = 0;
  if (notional <= 0) return false;
  const int lev = std::max(1, leverage);
  if (notional > std::numeric_limits<Amount>::max() - (lev - 1)) return false;
  out = std::max<Amount>(1, (notional + lev - 1) / lev);
  return true;
}

} // namespace

RiskDecision RiskEngine::check_limit_order(const OrderRequest& req, const Market& market, const AccountLedger& ledger,
                                           const PositionEngine* positions, int leverage,
                                           lob::Tick best_bid, lob::Tick best_ask, bool duplicate_order_id) const {
  constexpr uint32_t allowed_flags = lob::IOC | lob::FOK | lob::POST_ONLY | lob::STP | LOBX_REDUCE_ONLY;
  if ((req.flags & ~allowed_flags) != 0u) return RiskDecision{false, RejectCode::UnsupportedOrderType, "unsupported order flags", 0, 0};
  if ((req.flags & lob::IOC) != 0u && (req.flags & lob::FOK) != 0u) {
    return RiskDecision{false, RejectCode::UnsupportedOrderType, "IOC and FOK cannot both be set", 0, 0};
  }
  if (duplicate_order_id) return RiskDecision{false, RejectCode::DuplicateOrderId, "duplicate order id", 0, 0};
  if (market.status != MarketStatus::Active) return RiskDecision{false, RejectCode::MarketNotActive, "market is not active", 0, 0};

  if ((req.flags & lob::POST_ONLY) != 0u) {
    const bool crosses = req.side == lob::Side::Bid
      ? (best_ask != std::numeric_limits<lob::Tick>::max() && best_ask <= req.price)
      : (best_bid != std::numeric_limits<lob::Tick>::min() && best_bid >= req.price);
    if (crosses) return RiskDecision{false, RejectCode::PostOnlyWouldCross, "post-only order would cross", 0, 0};
  }
  if (req.price <= 0 || (req.price % market.tick_size) != 0) return RiskDecision{false, RejectCode::InvalidPrice, "price violates tick size", 0, 0};
  if (req.qty < market.min_qty || (req.qty % market.lot_size) != 0) return RiskDecision{false, RejectCode::InvalidQuantity, "qty violates lot/min qty", 0, 0};

  Amount notional = 0;
  if (!mul_amount(req.price, req.qty, notional) || notional < market.min_notional) return RiskDecision{false, RejectCode::InvalidNotional, "notional too small or overflow", 0, 0};

  if (market.type == MarketType::Spot) {
    if ((req.flags & LOBX_REDUCE_ONLY) != 0u) return RiskDecision{false, RejectCode::ReduceOnlyUnsupported, "reduce-only requires derivative market", 0, 0};
    const AssetId lock_asset = req.side == lob::Side::Bid ? market.quote_asset : market.base_asset;
    Amount lock_amount = req.side == lob::Side::Bid ? notional : req.qty;
    if (req.side == lob::Side::Bid) {
      Amount fee_reference_notional = notional;
      if (best_ask != std::numeric_limits<lob::Tick>::max() && best_ask <= req.price) {
        if (!mul_amount(best_ask, req.qty, fee_reference_notional)) {
          return RiskDecision{false, RejectCode::InvalidNotional, "fee notional overflow", 0, 0};
        }
      }
      const Amount taker_fee = fee_for_notional(fee_reference_notional, market.taker_fee_bps);
      if (taker_fee < 0 || fee_reference_notional > std::numeric_limits<Amount>::max() - taker_fee) {
        return RiskDecision{false, RejectCode::InvalidNotional, "fee notional overflow", 0, 0};
      }
      lock_amount = std::max(lock_amount, fee_reference_notional + taker_fee);
    }
    if (ledger.free(req.user, lock_asset) < lock_amount) return RiskDecision{false, RejectCode::InsufficientBalance, "insufficient free balance", lock_asset, lock_amount};
    return RiskDecision{true, RejectCode::None, {}, lock_asset, lock_amount};
  }

  if (market.type == MarketType::Perpetual) {
    const int lev = std::max(1, std::min(leverage, std::max(1, market.max_leverage)));
    if ((req.flags & LOBX_REDUCE_ONLY) != 0u) {
      if (!positions) return RiskDecision{false, RejectCode::ReduceOnlyUnsupported, "position engine unavailable", 0, 0};
      if (positions->reduce_only_would_increase(req.user, req.market_id, req.side, req.qty)) {
        return RiskDecision{false, RejectCode::ReduceOnlyWouldIncrease, "reduce-only order would increase position", 0, 0};
      }
      return RiskDecision{true, RejectCode::None, {}, market.margin_asset, 0};
    }
    lob::Tick margin_price = req.price;
    if (req.side == lob::Side::Ask && best_bid != std::numeric_limits<lob::Tick>::min() && best_bid > margin_price) {
      margin_price = best_bid;
      if (!mul_amount(margin_price, req.qty, notional) || notional < market.min_notional) {
        return RiskDecision{false, RejectCode::InvalidNotional, "notional too small or overflow", 0, 0};
      }
    }
    Amount lock_amount = 0;
    if (!margin_for_notional(notional, lev, lock_amount)) {
      return RiskDecision{false, RejectCode::InvalidNotional, "margin notional overflow", 0, 0};
    }
    if (ledger.free(req.user, market.margin_asset) < lock_amount) return RiskDecision{false, RejectCode::InsufficientBalance, "insufficient free margin", market.margin_asset, lock_amount};
    return RiskDecision{true, RejectCode::None, {}, market.margin_asset, lock_amount};
  }

  return RiskDecision{false, RejectCode::UnsupportedOrderType, "unsupported market type", 0, 0};
}

} // namespace lobx
