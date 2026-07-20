#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "lobx/types.hpp"

namespace lobx {

struct Market {
  MarketId id{0};
  std::string symbol;
  AssetId base_asset{0};
  AssetId quote_asset{0};
  AssetId margin_asset{0};
  MarketType type{MarketType::Spot};
  MarketStatus status{MarketStatus::Draft};
  lob::Tick tick_size{1};
  lob::Quantity lot_size{1};
  lob::Quantity min_qty{1};
  Amount min_notional{1};
  int maker_fee_bps{0};
  int taker_fee_bps{0};
  int max_leverage{1};
};

class MarketRegistry {
public:
  Result create_spot_market(const std::string& symbol,
                            AssetId base_asset,
                            AssetId quote_asset,
                            lob::Tick tick_size,
                            lob::Quantity lot_size,
                            lob::Quantity min_qty,
                            Amount min_notional,
                            MarketId* out_id = nullptr);
  Result create_perpetual_market(const std::string& symbol,
                                 AssetId base_asset,
                                 AssetId quote_asset,
                                 AssetId margin_asset,
                                 lob::Tick tick_size,
                                 lob::Quantity lot_size,
                                 lob::Quantity min_qty,
                                 Amount min_notional,
                                 int max_leverage,
                                 MarketId* out_id = nullptr);
  Result activate(MarketId id);
  Result halt(MarketId id);

  const Market* get(MarketId id) const;
  Market* get_mut(MarketId id);
  const Market* find_by_symbol(const std::string& symbol) const;
  MarketId id_for_symbol(const std::string& symbol) const;
  std::vector<Market> markets() const;

private:
  MarketId next_id_{1};
  std::unordered_map<MarketId, Market> by_id_;
  std::unordered_map<std::string, MarketId> by_symbol_;
};

} // namespace lobx
