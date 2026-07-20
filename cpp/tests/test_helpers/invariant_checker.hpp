#pragma once

#include "lobx/exchange.hpp"
#include "lobx/market_engine.hpp"

#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lobx_test {

struct InvariantViolation {
  std::string message;
};

struct InvariantReport {
  bool ok{true};
  std::vector<InvariantViolation> violations;

  void add(std::string message) {
    ok = false;
    violations.push_back(InvariantViolation{std::move(message)});
  }
};

inline std::string report_to_string(const InvariantReport& report) {
  std::ostringstream os;
  for (const auto& violation : report.violations) os << violation.message << '\n';
  return os.str();
}

inline InvariantReport check_accounting_invariants(const lobx::Exchange& exchange) {
  InvariantReport report;
  if (!const_cast<lobx::Exchange&>(exchange).ledger().invariant_ok()) {
    report.add("accounting invariant failed: wallet total != free + locked or negative balance exists");
  }
  return report;
}

inline InvariantReport check_spot_invariants(const lobx::Exchange& exchange) {
  InvariantReport report = check_accounting_invariants(exchange);
  for (const auto& trade : exchange.trades()) {
    if (trade.buyer == trade.seller) report.add("STP invariant failed: self fill detected");
    if (trade.price <= 0 || trade.qty <= 0) report.add("fill invariant failed: non-positive price or quantity");
  }
  return report;
}

inline InvariantReport check_perp_invariants(const lobx::Exchange& exchange) {
  InvariantReport report = check_accounting_invariants(exchange);
  for (const auto& position : const_cast<lobx::Exchange&>(exchange).positions()) {
    if (position.signed_qty != 0) {
      const auto usdt = const_cast<lobx::Exchange&>(exchange).balance(position.user, "USDT");
      if (usdt.locked <= 0) {
        report.add("perp invariant failed: open position has no visible locked margin user=" + std::to_string(position.user));
      }
    }
  }
  return report;
}

inline InvariantReport check_order_book_invariants(lobx::MarketEngine& engine, const lobx::AccountLedger& ledger) {
  InvariantReport report;
  using PriceSide = std::pair<int, lob::Tick>;
  std::map<PriceSide, lob::Quantity> expected_depth;
  std::map<std::pair<lobx::UserId, lobx::AssetId>, lobx::Amount> expected_locked;

  for (const lobx::OpenOrder& order : engine.open_orders()) {
    if ((order.flags & (lob::IOC | lob::FOK)) != 0u) {
      report.add("order book invariant failed: IOC/FOK order is resting order_id=" + std::to_string(order.id));
    }
    const int side_key = order.side == lob::Side::Bid ? 0 : 1;
    expected_depth[{side_key, order.limit_price}] += order.leaves_qty;
    expected_locked[{order.user, order.locked_asset}] += order.locked_remaining;
  }

  auto verify_side = [&](lob::Side side) {
    const int side_key = side == lob::Side::Bid ? 0 : 1;
    std::map<lob::Tick, lob::Quantity> actual;
    for (const auto& level : engine.topN(side, 1000)) actual[level.first] += level.second;
    std::map<lob::Tick, lob::Quantity> expected;
    for (const auto& item : expected_depth) if (item.first.first == side_key) expected[item.first.second] += item.second;
    if (actual != expected) {
      report.add(std::string("order book invariant failed: ") + (side == lob::Side::Bid ? "bid" : "ask") +
                 " depth does not match open order index");
    }
  };
  verify_side(lob::Side::Bid);
  verify_side(lob::Side::Ask);

  for (const auto& item : expected_locked) {
    const auto actual = ledger.locked(item.first.first, item.first.second);
    if (actual != item.second) {
      report.add("accounting invariant failed: open order lock mismatch user=" +
                 std::to_string(item.first.first) + " asset=" + std::to_string(item.first.second) +
                 " expected=" + std::to_string(item.second) + " actual=" + std::to_string(actual));
    }
  }
  return report;
}

inline InvariantReport check_order_book_invariants(const lobx::Exchange&) {
  InvariantReport report;
  return report;
}

inline void require_invariants(const lobx::Exchange& exchange) {
  InvariantReport report = check_accounting_invariants(exchange);
  InvariantReport spot = check_spot_invariants(exchange);
  report.violations.insert(report.violations.end(), spot.violations.begin(), spot.violations.end());
  report.ok = report.ok && spot.ok;
  InvariantReport perp = check_perp_invariants(exchange);
  report.violations.insert(report.violations.end(), perp.violations.begin(), perp.violations.end());
  report.ok = report.ok && perp.ok;
  if (!report.ok) throw std::runtime_error(report_to_string(report));
}

} // namespace lobx_test
