#pragma once

#include <string>
#include <vector>

#include "lobx/types.hpp"

namespace lobx::sim {

struct InitialBookLevel {
  lob::Side side{lob::Side::Bid};
  lob::Tick price{0};
  lob::Quantity qty{0};
};

struct MarketEnvironmentConfig {
  std::string market_symbol{"BTC-USDT"};

  lob::Tick reference_price{100};
  int ticks{0};
  int warmup_ticks{0};

  std::vector<InitialBookLevel> initial_book;

  double noise_intensity{0.0};
  double liquidity_scale{1.0};
  double volatility_regime{1.0};
  double spread_regime{1.0};
};

struct MarketEnvironmentValidation {
  bool ok{false};
  std::string reason;
};

MarketEnvironmentValidation validate_market_environment(const MarketEnvironmentConfig& config);

} // namespace lobx::sim
