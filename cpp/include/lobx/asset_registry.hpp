#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "lobx/types.hpp"

namespace lobx {

struct Asset {
  AssetId id{0};
  std::string symbol;
  uint8_t decimals{0};
  Amount max_supply{0};
  Amount circulating_supply{0};
  UserId issuer{0};
  AssetStatus status{AssetStatus::Draft};
};

class AssetRegistry {
public:
  Result issue_asset(const std::string& symbol, uint8_t decimals, Amount max_supply, UserId issuer, AssetId* out_id = nullptr);
  Result activate(AssetId id);
  Result suspend(AssetId id);
  Result mint(AssetId id, Amount amount);
  Result burn(AssetId id, Amount amount);

  const Asset* get(AssetId id) const;
  Asset* get_mut(AssetId id);
  const Asset* find_by_symbol(const std::string& symbol) const;
  AssetId id_for_symbol(const std::string& symbol) const;
  std::vector<Asset> assets() const;

private:
  AssetId next_id_{1};
  std::unordered_map<AssetId, Asset> by_id_;
  std::unordered_map<std::string, AssetId> by_symbol_;
};

} // namespace lobx
