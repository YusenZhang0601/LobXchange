#include "lobx/market_registry.hpp"

#include <algorithm>

namespace lobx {

Result MarketRegistry::create_spot_market(const std::string& symbol, AssetId base_asset, AssetId quote_asset,
                                          lob::Tick tick_size, lob::Quantity lot_size, lob::Quantity min_qty,
                                          Amount min_notional, MarketId* out_id) {
  if (symbol.empty()) return Result::fail(RejectCode::UnknownMarket, "market symbol is empty");
  if (base_asset == 0 || quote_asset == 0 || base_asset == quote_asset) return Result::fail(RejectCode::UnknownAsset, "invalid base/quote assets");
  if (tick_size <= 0) return Result::fail(RejectCode::InvalidPrice, "tick_size must be positive");
  if (lot_size <= 0 || min_qty <= 0) return Result::fail(RejectCode::InvalidQuantity, "lot/min qty must be positive");
  if (min_notional <= 0) return Result::fail(RejectCode::InvalidNotional, "min_notional must be positive");
  if (by_symbol_.find(symbol) != by_symbol_.end()) return Result::fail(RejectCode::UnknownMarket, "market already exists");
  const MarketId id = next_id_++;
  by_id_[id] = Market{id, symbol, base_asset, quote_asset, quote_asset, MarketType::Spot, MarketStatus::Active, tick_size, lot_size, min_qty, min_notional, 0, 0, 1};
  by_symbol_[symbol] = id;
  if (out_id) *out_id = id;
  return Result::success();
}

Result MarketRegistry::create_perpetual_market(const std::string& symbol, AssetId base_asset, AssetId quote_asset,
                                                AssetId margin_asset, lob::Tick tick_size, lob::Quantity lot_size,
                                                lob::Quantity min_qty, Amount min_notional, int max_leverage,
                                                MarketId* out_id) {
  if (symbol.empty()) return Result::fail(RejectCode::UnknownMarket, "market symbol is empty");
  if (base_asset == 0 || quote_asset == 0 || margin_asset == 0) return Result::fail(RejectCode::UnknownAsset, "invalid perpetual assets");
  if (tick_size <= 0) return Result::fail(RejectCode::InvalidPrice, "tick_size must be positive");
  if (lot_size <= 0 || min_qty <= 0) return Result::fail(RejectCode::InvalidQuantity, "lot/min qty must be positive");
  if (min_notional <= 0) return Result::fail(RejectCode::InvalidNotional, "min_notional must be positive");
  if (max_leverage <= 0) return Result::fail(RejectCode::InvalidQuantity, "max_leverage must be positive");
  if (by_symbol_.find(symbol) != by_symbol_.end()) return Result::fail(RejectCode::UnknownMarket, "market already exists");
  const MarketId id = next_id_++;
  by_id_[id] = Market{id, symbol, base_asset, quote_asset, margin_asset, MarketType::Perpetual, MarketStatus::Active, tick_size, lot_size, min_qty, min_notional, 0, 0, max_leverage};
  by_symbol_[symbol] = id;
  if (out_id) *out_id = id;
  return Result::success();
}

Result MarketRegistry::activate(MarketId id) { auto* m = get_mut(id); if (!m) return Result::fail(RejectCode::UnknownMarket, "market not found"); m->status = MarketStatus::Active; return Result::success(); }
Result MarketRegistry::halt(MarketId id) { auto* m = get_mut(id); if (!m) return Result::fail(RejectCode::UnknownMarket, "market not found"); m->status = MarketStatus::Halted; return Result::success(); }
const Market* MarketRegistry::get(MarketId id) const { auto it = by_id_.find(id); return it == by_id_.end() ? nullptr : &it->second; }
Market* MarketRegistry::get_mut(MarketId id) { auto it = by_id_.find(id); return it == by_id_.end() ? nullptr : &it->second; }
const Market* MarketRegistry::find_by_symbol(const std::string& symbol) const { auto it = by_symbol_.find(symbol); return it == by_symbol_.end() ? nullptr : get(it->second); }
MarketId MarketRegistry::id_for_symbol(const std::string& symbol) const { auto it = by_symbol_.find(symbol); return it == by_symbol_.end() ? 0 : it->second; }

std::vector<Market> MarketRegistry::markets() const {
  std::vector<Market> out;
  out.reserve(by_id_.size());
  for (const auto& kv : by_id_) out.push_back(kv.second);
  std::sort(out.begin(), out.end(), [](const Market& a, const Market& b) { return a.id < b.id; });
  return out;
}

} // namespace lobx
