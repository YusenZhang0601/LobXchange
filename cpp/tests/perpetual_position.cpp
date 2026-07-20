#include "lobx/exchange.hpp"

#include <cassert>
#include <iostream>

int main() {
  lobx::Exchange ex;
  lobx::AssetId usdt = 0;
  lobx::AssetId sim = 0;
  assert(ex.issue_asset("USDT", 6, 1000000000, 1, 0, &usdt).ok);
  assert(ex.issue_asset("SIM", 6, 1000000000, 1, 0, &sim).ok);

  lobx::MarketId market = 0;
  assert(ex.create_perpetual_market("SIM-PERP", "SIM", "USDT", "USDT", 1, 1, 1, 1, 10, &market).ok);

  assert(ex.deposit(10, "USDT", 10000).ok);
  assert(ex.deposit(20, "USDT", 10000).ok);
  assert(ex.deposit(30, "USDT", 10000).ok);
  ex.set_leverage(10, "SIM-PERP", 5);
  ex.set_leverage(20, "SIM-PERP", 5);
  ex.set_leverage(30, "SIM-PERP", 5);

  auto ask = ex.submit_limit("SIM-PERP", 20, 2001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1);
  assert(ask.accepted);
  assert(ex.balance(20, "USDT").locked == 200);

  auto buy = ex.submit_limit("SIM-PERP", 10, 1001, lob::Side::Bid, 100, 4, lob::IOC, 2);
  assert(buy.accepted);
  assert(buy.trades.size() == 1);
  assert(buy.trades[0].price == 100);
  assert(buy.trades[0].qty == 4);

  lobx::Position long_pos = ex.position(10, "SIM-PERP");
  lobx::Position short_pos = ex.position(20, "SIM-PERP");
  assert(long_pos.signed_qty == 4);
  assert(long_pos.entry_price == 100);
  assert(short_pos.signed_qty == -4);
  assert(short_pos.entry_price == 100);

  auto bad_ro = ex.submit_limit("SIM-PERP", 10, 1002, lob::Side::Bid, 101, 1, lobx::LOBX_REDUCE_ONLY | lob::IOC, 3);
  assert(!bad_ro.accepted);
  assert(bad_ro.code == lobx::RejectCode::ReduceOnlyWouldIncrease);

  auto empty_ro = ex.submit_limit("SIM-PERP", 30, 3001, lob::Side::Ask, 100, 1, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);
  assert(!empty_ro.accepted);
  assert(empty_ro.code == lobx::RejectCode::ReduceOnlyWouldIncrease);

  auto bid = ex.submit_limit("SIM-PERP", 20, 2002, lob::Side::Bid, 100, 2, lob::POST_ONLY, 5);
  assert(bid.accepted);
  auto reduce = ex.submit_limit("SIM-PERP", 10, 1003, lob::Side::Ask, 100, 2, lobx::LOBX_REDUCE_ONLY | lob::IOC, 6);
  assert(reduce.accepted);
  assert(reduce.trades.size() == 1);
  assert(ex.position(10, "SIM-PERP").signed_qty == 2);
  assert(ex.position(20, "SIM-PERP").signed_qty == -2);
  assert(ex.position(10, "SIM-PERP").realized_pnl == 0);
  assert(ex.ledger().invariant_ok());

  std::cout << "perpetual_position ok\n";
  return 0;
}
