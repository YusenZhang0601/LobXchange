#pragma once

#include "lobx/account_ledger.hpp"

#include "test_helpers/test_framework.hpp"

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace lobx_test {

struct AccountSnapshot {
  double available_cash{0.0};
  double locked_cash{0.0};
  double total_cash{0.0};
  double available_inventory{0.0};
  double locked_inventory{0.0};
  double total_inventory{0.0};

  double equity(double mark_price) const {
    return total_cash + total_inventory * mark_price;
  }
};

inline AccountSnapshot snapshot_account(const lobx::AccountLedger& ledger,
                                        lobx::UserId user,
                                        lobx::AssetId cash_asset,
                                        lobx::AssetId inventory_asset) {
  const lobx::WalletBalance cash = ledger.balance(user, cash_asset);
  const lobx::WalletBalance inventory = ledger.balance(user, inventory_asset);
  return AccountSnapshot{static_cast<double>(cash.free),
                         static_cast<double>(cash.locked),
                         static_cast<double>(cash.total),
                         static_cast<double>(inventory.free),
                         static_cast<double>(inventory.locked),
                         static_cast<double>(inventory.total)};
}

inline double total_equity(const std::vector<AccountSnapshot>& accounts, double mark_price) {
  double out = 0.0;
  for (const AccountSnapshot& account : accounts) out += account.equity(mark_price);
  return out;
}

inline std::string account_snapshot_string(const AccountSnapshot& s, double mark_price) {
  std::ostringstream os;
  os << "cash(total=" << s.total_cash << ",free=" << s.available_cash << ",locked=" << s.locked_cash << ")"
     << " inventory(total=" << s.total_inventory << ",free=" << s.available_inventory
     << ",locked=" << s.locked_inventory << ")"
     << " equity=" << s.equity(mark_price);
  return os.str();
}

inline void expect_near(double actual,
                        double expected,
                        double tolerance,
                        const std::string& label,
                        const char* file,
                        int line) {
  if (std::isfinite(actual) && std::isfinite(expected) && std::abs(actual - expected) <= tolerance) return;
  std::ostringstream os;
  os << label << " expected near " << expected << " actual=" << actual << " tolerance=" << tolerance;
  lobx_test::fail(file, line, os.str());
}

} // namespace lobx_test

#define EXPECT_NEAR_VALUE(actual, expected, tolerance, label) \
  ::lobx_test::expect_near((actual), (expected), (tolerance), (label), __FILE__, __LINE__)

