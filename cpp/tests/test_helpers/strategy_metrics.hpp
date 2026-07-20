#pragma once

#include "lobx/account_ledger.hpp"
#include "lobx/types.hpp"

#include <map>
#include <vector>

namespace lobx_test {

struct StrategyMetrics {
  lobx::UserId user{0};

  lobx::Amount starting_quote{0};
  lobx::Amount ending_quote{0};
  lobx::Amount starting_base{0};
  lobx::Amount ending_base{0};

  lobx::Amount fees_paid{0};

  long double gross_pnl{0.0L};
  long double net_pnl{0.0L};
  lob::Quantity inventory{0};

  int submitted_orders{0};
  int accepted_orders{0};
  int rejected_orders{0};
  int canceled_orders{0};
  int fills{0};

  bool operator==(const StrategyMetrics&) const = default;
};

inline long double spot_mark_to_reference(lobx::Amount quote_delta,
                                          lobx::Amount base_delta,
                                          lob::Tick reference_price) {
  return static_cast<long double>(quote_delta) +
         static_cast<long double>(base_delta) * static_cast<long double>(reference_price);
}

inline void finalize_strategy_metrics(StrategyMetrics& metrics,
                                      const lobx::WalletBalance& ending_quote,
                                      const lobx::WalletBalance& ending_base,
                                      lob::Tick reference_price) {
  metrics.ending_quote = ending_quote.total;
  metrics.ending_base = ending_base.total;
  metrics.inventory = ending_base.total - metrics.starting_base;
  metrics.net_pnl = spot_mark_to_reference(metrics.ending_quote - metrics.starting_quote,
                                           metrics.ending_base - metrics.starting_base,
                                           reference_price);
  metrics.gross_pnl = metrics.net_pnl + static_cast<long double>(metrics.fees_paid);
}

inline lobx::Amount fee_for_notional(lobx::Amount notional, int fee_bps) {
  if (notional <= 0 || fee_bps <= 0) return 0;
  return (notional * fee_bps) / 10000;
}

inline bool metrics_match_ledger_balances(const StrategyMetrics& metrics,
                                          const lobx::WalletBalance& quote,
                                          const lobx::WalletBalance& base) {
  return metrics.ending_quote == quote.total &&
         metrics.ending_base == base.total &&
         metrics.inventory == metrics.ending_base - metrics.starting_base;
}

} // namespace lobx_test
