#include "lobx/asset_registry.hpp"

#include <algorithm>

namespace lobx {

Result AssetRegistry::issue_asset(const std::string& symbol, uint8_t decimals, Amount max_supply, UserId issuer, AssetId* out_id) {
  if (symbol.empty()) return Result::fail(RejectCode::UnknownAsset, "asset symbol is empty");
  if (max_supply <= 0) return Result::fail(RejectCode::InvalidQuantity, "max_supply must be positive");
  if (by_symbol_.find(symbol) != by_symbol_.end()) return Result::fail(RejectCode::UnknownAsset, "asset already exists");
  const AssetId id = next_id_++;
  by_id_[id] = Asset{id, symbol, decimals, max_supply, 0, issuer, AssetStatus::Draft};
  by_symbol_[symbol] = id;
  if (out_id) *out_id = id;
  return Result::success();
}

Result AssetRegistry::activate(AssetId id) { auto* a = get_mut(id); if (!a) return Result::fail(RejectCode::UnknownAsset, "asset not found"); a->status = AssetStatus::Active; return Result::success(); }
Result AssetRegistry::suspend(AssetId id) { auto* a = get_mut(id); if (!a) return Result::fail(RejectCode::UnknownAsset, "asset not found"); a->status = AssetStatus::Suspended; return Result::success(); }

Result AssetRegistry::mint(AssetId id, Amount amount) {
  auto* a = get_mut(id);
  if (!a) return Result::fail(RejectCode::UnknownAsset, "asset not found");
  if (amount <= 0) return Result::fail(RejectCode::InvalidQuantity, "mint amount must be positive");
  if (a->circulating_supply > a->max_supply - amount) return Result::fail(RejectCode::InvalidQuantity, "mint exceeds max_supply");
  a->circulating_supply += amount;
  return Result::success();
}

Result AssetRegistry::burn(AssetId id, Amount amount) {
  auto* a = get_mut(id);
  if (!a) return Result::fail(RejectCode::UnknownAsset, "asset not found");
  if (amount <= 0) return Result::fail(RejectCode::InvalidQuantity, "burn amount must be positive");
  if (a->circulating_supply < amount) return Result::fail(RejectCode::InvalidQuantity, "burn exceeds circulating_supply");
  a->circulating_supply -= amount;
  return Result::success();
}

const Asset* AssetRegistry::get(AssetId id) const { auto it = by_id_.find(id); return it == by_id_.end() ? nullptr : &it->second; }
Asset* AssetRegistry::get_mut(AssetId id) { auto it = by_id_.find(id); return it == by_id_.end() ? nullptr : &it->second; }
const Asset* AssetRegistry::find_by_symbol(const std::string& symbol) const { auto it = by_symbol_.find(symbol); return it == by_symbol_.end() ? nullptr : get(it->second); }
AssetId AssetRegistry::id_for_symbol(const std::string& symbol) const { auto it = by_symbol_.find(symbol); return it == by_symbol_.end() ? 0 : it->second; }

std::vector<Asset> AssetRegistry::assets() const {
  std::vector<Asset> out;
  out.reserve(by_id_.size());
  for (const auto& kv : by_id_) out.push_back(kv.second);
  std::sort(out.begin(), out.end(), [](const Asset& a, const Asset& b) { return a.id < b.id; });
  return out;
}

} // namespace lobx
