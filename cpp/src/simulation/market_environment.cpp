#include "lobx/simulation/market_environment.hpp"

#include <utility>

namespace lobx::sim {

MarketEnvironmentValidation validate_market_environment(const MarketEnvironmentConfig& config) {
  auto fail = [](std::string reason) {
    return MarketEnvironmentValidation{false, std::move(reason)};
  };

  if (config.market_symbol.empty()) return fail("market_symbol must not be empty");
  if (config.reference_price <= 0) return fail("reference_price must be positive");
  if (config.ticks <= 0) return fail("ticks must be positive");
  if (config.warmup_ticks < 0) return fail("warmup_ticks must be non-negative");
  if (config.warmup_ticks >= config.ticks) return fail("warmup_ticks must be less than ticks");
  if (config.noise_intensity < 0.0) return fail("noise_intensity must be non-negative");
  if (config.liquidity_scale <= 0.0) return fail("liquidity_scale must be positive");
  if (config.volatility_regime <= 0.0) return fail("volatility_regime must be positive");
  if (config.spread_regime <= 0.0) return fail("spread_regime must be positive");

  for (const InitialBookLevel& level : config.initial_book) {
    if (level.side != lob::Side::Bid && level.side != lob::Side::Ask) {
      return fail("initial book level side must be bid or ask");
    }
    if (level.price <= 0) return fail("initial book level price must be positive");
    if (level.qty <= 0) return fail("initial book level qty must be positive");
  }

  return MarketEnvironmentValidation{true, {}};
}

} // namespace lobx::sim
