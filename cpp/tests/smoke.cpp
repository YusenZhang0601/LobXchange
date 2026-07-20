#include "lobx/exchange.hpp"

#include <cassert>
#include <iostream>

int main() {
  lobx::Exchange ex;
  lobx::AssetId usdt = 0;
  lobx::AssetId abc = 0;
  lobx::MarketId market = 0;

  assert(ex.issue_asset("USDT", 2, 1000000000, 1, 1000000, &usdt).ok);
  assert(ex.issue_asset("ABC", 0, 1000000, 100, 1000, &abc).ok);
  assert(ex.create_spot_market("ABC-USDT", "ABC", "USDT", 1, 1, 1, 1, &market).ok);

  assert(ex.deposit(2, "USDT", 1000).ok);

  auto ask = ex.submit_limit("ABC-USDT", 100, 5001, lob::Side::Ask, 10, 5, lob::NONE, 1000000000LL);
  assert(ask.accepted);
  assert(ask.exec.filled == 0);
  assert(ask.exec.remaining == 5);
  assert(ex.balance(100, "ABC").free == 995);
  assert(ex.balance(100, "ABC").locked == 5);

  auto post_only_cross = ex.submit_limit("ABC-USDT", 2, 5002, lob::Side::Bid, 10, 1, lob::POST_ONLY, 1000000001LL);
  assert(!post_only_cross.accepted);
  assert(post_only_cross.code == lobx::RejectCode::PostOnlyWouldCross);

  auto bid = ex.submit_limit("ABC-USDT", 2, 5003, lob::Side::Bid, 10, 3, lob::NONE, 1000000002LL);
  assert(bid.accepted);
  assert(bid.exec.filled == 3);
  assert(bid.exec.remaining == 0);
  assert(bid.trades.size() == 1);
  assert(ex.balance(2, "ABC").total == 3);
  assert(ex.balance(2, "USDT").total == 970);
  assert(ex.balance(2, "USDT").locked == 0);
  assert(ex.balance(100, "ABC").total == 997);
  assert(ex.balance(100, "ABC").locked == 2);
  assert(ex.balance(100, "USDT").total == 30);

  auto asks = ex.topN("ABC-USDT", lob::Side::Ask, 5);
  assert(asks.size() == 1);
  assert(asks[0].first == 10);
  assert(asks[0].second == 2);

  assert(ex.cancel("ABC-USDT", 5001));
  assert(ex.balance(100, "ABC").locked == 0);
  assert(ex.balance(100, "ABC").free == 997);
  assert(ex.topN("ABC-USDT", lob::Side::Ask, 5).empty());

  auto candles = ex.flush_candles();
  assert(!candles.empty());
  assert(candles.front().open == 10);
  assert(candles.front().volume == 3);

  std::cout << "lobx smoke ok: market=" << market << " usdt=" << usdt << " abc=" << abc << "\n";
  return 0;
}
