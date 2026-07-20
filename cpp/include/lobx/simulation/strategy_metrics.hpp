#pragma once

#include <string>

#include "lobx/types.hpp"

namespace lobx::sim {

struct StrategyMetrics {
  UserId user{0};
  std::string bot_name;

  Amount starting_quote{0};
  Amount ending_quote{0};
  Amount starting_base{0};
  Amount ending_base{0};

  Amount fees_paid{0};

  long double gross_pnl{0.0L};
  long double net_pnl{0.0L};
  lob::Quantity inventory{0};

  int submitted_orders{0};
  int accepted_orders{0};
  int rejected_orders{0};
  int canceled_orders{0};
  int fills{0};
};

bool operator==(const StrategyMetrics& a, const StrategyMetrics& b);

} // namespace lobx::sim
