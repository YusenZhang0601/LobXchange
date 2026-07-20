#include "lobx/simulation/strategy_metrics.hpp"

namespace lobx::sim {

bool operator==(const StrategyMetrics& a, const StrategyMetrics& b) {
  return a.user == b.user &&
         a.bot_name == b.bot_name &&
         a.starting_quote == b.starting_quote &&
         a.ending_quote == b.ending_quote &&
         a.starting_base == b.starting_base &&
         a.ending_base == b.ending_base &&
         a.fees_paid == b.fees_paid &&
         a.gross_pnl == b.gross_pnl &&
         a.net_pnl == b.net_pnl &&
         a.inventory == b.inventory &&
         a.submitted_orders == b.submitted_orders &&
         a.accepted_orders == b.accepted_orders &&
         a.rejected_orders == b.rejected_orders &&
         a.canceled_orders == b.canceled_orders &&
         a.fills == b.fills;
}

} // namespace lobx::sim
