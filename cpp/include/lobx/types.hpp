#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "lob/book_core.hpp"

namespace lobx {

using AssetId = uint32_t;
using MarketId = uint32_t;
using UserId = lob::UserId;
using OrderId = lob::OrderId;
using Amount = int64_t;

constexpr uint32_t LOBX_REDUCE_ONLY = 1u << 16;

struct DecimalSpec {
  uint8_t decimals{0};
};

enum class AssetStatus : uint8_t { Draft = 0, Active = 1, Suspended = 2, Delisted = 3 };
enum class MarketType : uint8_t { Spot = 0, Perpetual = 1 };
enum class MarketStatus : uint8_t { Draft = 0, Active = 1, Halted = 2, Delisted = 3 };
enum class MarkPriceMode : uint8_t { LastTrade = 0, MidPrice = 1, IndexPrice = 2 };
enum class LiquidationMode : uint8_t { Disabled = 0, InfiniteInsurance = 1, LimitedInsurance = 2, AdlEnabled = 3 };

struct PerpRiskTier {
  Amount notional_floor{0};
  Amount notional_cap{0};
  int initial_margin_bps{0};
  int maintenance_margin_bps{0};
  int max_leverage{1};
};

struct PerpFeeConfig {
  int maker_fee_bps{0};
  int taker_fee_bps{0};
  int liquidation_fee_bps{0};
};

struct PerpFeeBreakdown {
  Amount maker_fee{0};
  Amount taker_fee{0};
  Amount liquidation_fee{0};
  Amount total_fee{0};
};

struct PerpFundingConfig {
  lob::Timestamp funding_interval_ns{0};
  int funding_rate_bps{0};
  lob::Timestamp next_funding_time{0};
};

struct FundingSettlement {
  UserId account_id{0};
  std::string symbol;
  lob::Quantity position_qty{0};
  lob::Tick mark_price{0};
  int funding_rate_bps{0};
  Amount payment{0};
};

struct RuntimeRetentionOptions {
  bool record_events{true};
  bool build_event_payloads{true};
  bool record_trade_history{true};
  bool update_klines{true};
  bool record_candle_history{true};
};

struct LiquidationOptions {
  LiquidationMode mode{LiquidationMode::Disabled};
  bool allow_partial_liquidation{false};
  bool record_liquidation_events{true};
  bool use_bankruptcy_price{false};
  bool charge_liquidation_fee{false};
  bool enable_adl_execution{false};
  bool close_position_immediately{true};
};

enum class RejectCode : uint8_t {
  None = 0,
  UnknownAsset,
  UnknownMarket,
  MarketNotActive,
  DuplicateOrderId,
  InvalidPrice,
  InvalidQuantity,
  InvalidNotional,
  InsufficientBalance,
  PostOnlyWouldCross,
  ReduceOnlyUnsupported,
  ReduceOnlyWouldIncrease,
  UnsupportedOrderType,
  InternalError,
};

struct Result {
  bool ok{false};
  RejectCode code{RejectCode::None};
  std::string reason;

  static Result success() { return Result{true, RejectCode::None, {}}; }
  static Result fail(RejectCode c, std::string r) { return Result{false, c, std::move(r)}; }
};

struct OrderRequest {
  MarketId market_id{0};
  UserId user{0};
  OrderId order_id{0};
  lob::SeqNo seq{0};
  lob::Timestamp ts{0};
  lob::Side side{lob::Side::Bid};
  lob::Tick price{0};
  lob::Quantity qty{0};
  uint32_t flags{lob::NONE};
};

struct TradeEvent {
  MarketId market_id{0};
  lob::Timestamp ts{0};
  lob::Tick price{0};
  lob::Quantity qty{0};
  UserId buyer{0};
  UserId seller{0};
  OrderId buyer_order_id{0};
  OrderId seller_order_id{0};
  lob::Side liquidity_side{lob::Side::Ask};
};

struct SubmitResult {
  bool accepted{false};
  RejectCode code{RejectCode::None};
  std::string reason;
  lob::ExecResult exec{};
  std::vector<TradeEvent> trades;
};

inline bool mul_amount(lob::Tick px, lob::Quantity qty, Amount& out) {
  if (px < 0 || qty < 0) return false;
  if (px == 0 || qty == 0) {
    out = 0;
    return true;
  }
  if (px > std::numeric_limits<Amount>::max() / qty) return false;
  out = static_cast<Amount>(px * qty);
  return true;
}

inline const char* reject_code_name(RejectCode c) {
  switch (c) {
    case RejectCode::None: return "None";
    case RejectCode::UnknownAsset: return "UnknownAsset";
    case RejectCode::UnknownMarket: return "UnknownMarket";
    case RejectCode::MarketNotActive: return "MarketNotActive";
    case RejectCode::DuplicateOrderId: return "DuplicateOrderId";
    case RejectCode::InvalidPrice: return "InvalidPrice";
    case RejectCode::InvalidQuantity: return "InvalidQuantity";
    case RejectCode::InvalidNotional: return "InvalidNotional";
    case RejectCode::InsufficientBalance: return "InsufficientBalance";
    case RejectCode::PostOnlyWouldCross: return "PostOnlyWouldCross";
    case RejectCode::ReduceOnlyUnsupported: return "ReduceOnlyUnsupported";
    case RejectCode::ReduceOnlyWouldIncrease: return "ReduceOnlyWouldIncrease";
    case RejectCode::UnsupportedOrderType: return "UnsupportedOrderType";
    case RejectCode::InternalError: return "InternalError";
  }
  return "Unknown";
}

} // namespace lobx
