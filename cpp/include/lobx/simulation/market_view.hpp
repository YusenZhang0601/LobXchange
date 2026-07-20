#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace lobx::simulation {

struct BookLevelView {
  double price{0.0};
  double quantity{0.0};
};

struct TradePrintView {
  double price{0.0};
  double quantity{0.0};
  int side{0};
  std::int64_t ts{0};
};

// Read-only market snapshot passed to agents. The spans point to runtime-owned
// per-step buffers, so agents must not retain them after decide() returns.
struct MarketView {
  std::string symbol;
  std::span<const BookLevelView> bids;
  std::span<const BookLevelView> asks;
  std::span<const TradePrintView> recent_trades;
  double best_bid{0.0};
  double best_ask{0.0};
  double mid_price{0.0};
  double spread_bps{0.0};
  std::int64_t exchange_ts{0};
  std::int64_t observed_ts{0};
};

} // namespace lobx::simulation
