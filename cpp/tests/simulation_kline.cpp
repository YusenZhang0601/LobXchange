#include "lobx/exchange.hpp"

#include <cassert>
#include <vector>

namespace {

void require_ok(const lobx::Result& r) { assert(r.ok && r.reason.c_str()); }

lobx::Exchange make_exchange() {
  lobx::Exchange ex;
  require_ok(ex.issue_asset("USDT", 2, 1000000000000LL, 1, 100000000000LL));
  require_ok(ex.issue_asset("SIM", 0, 1000000000LL, 100, 10000000LL));
  require_ok(ex.create_spot_market("SIM-USDT", "SIM", "USDT", 1, 1, 1, 1));
  for (lobx::UserId user : {100ULL, 101ULL, 200ULL, 201ULL, 202ULL}) {
    require_ok(ex.deposit(user, "USDT", 1000000000LL));
    require_ok(ex.deposit(user, "SIM", 1000000LL));
  }
  return ex;
}

void submit_ok(lobx::Exchange& ex, lobx::UserId user, lobx::OrderId order_id, lob::Side side,
               lob::Tick price, lob::Quantity qty, uint32_t flags, lob::Timestamp ts) {
  const auto r = ex.submit_limit("SIM-USDT", user, order_id, side, price, qty, flags, ts);
  assert(r.accepted && r.reason.c_str());
}

} // namespace

int main() {
  lobx::Exchange ex = make_exchange();
  const int64_t s = 1000000000LL;

  submit_ok(ex, 100, 1001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1000000LL);
  submit_ok(ex, 100, 1002, lob::Side::Ask, 101, 10, lob::POST_ONLY, 1000001LL);
  submit_ok(ex, 100, 1003, lob::Side::Ask, 103, 10, lob::POST_ONLY, 1000002LL);
  submit_ok(ex, 101, 1004, lob::Side::Bid, 99, 10, lob::POST_ONLY, 1000003LL);
  submit_ok(ex, 101, 1005, lob::Side::Bid, 98, 10, lob::POST_ONLY, 1000004LL);

  submit_ok(ex, 200, 2001, lob::Side::Bid, 101, 15, lob::NONE, 1 * s + 1);
  submit_ok(ex, 201, 2002, lob::Side::Ask, 98, 12, lob::NONE, 2 * s + 1);
  submit_ok(ex, 202, 2003, lob::Side::Bid, 103, 12, lob::NONE, 2 * s + 500000000LL);
  ex.flush_candles();

  const auto& trades = ex.trades();
  assert(trades.size() == 6);
  const std::vector<lob::Tick> expected_prices{100, 101, 99, 98, 101, 103};
  const std::vector<lob::Quantity> expected_qty{10, 5, 10, 2, 5, 7};
  for (size_t i = 0; i < trades.size(); ++i) {
    assert(trades[i].price == expected_prices[i]);
    assert(trades[i].qty == expected_qty[i]);
  }

  std::vector<lobx::Candle> one_second;
  std::vector<lobx::Candle> one_minute;
  for (const auto& c : ex.candles()) {
    if (c.interval_ns == s) one_second.push_back(c);
    if (c.interval_ns == 60 * s) one_minute.push_back(c);
  }

  assert(one_second.size() == 2);
  assert(one_second[0].open_time_ns == 1 * s);
  assert(one_second[0].open == 100);
  assert(one_second[0].high == 101);
  assert(one_second[0].low == 100);
  assert(one_second[0].close == 101);
  assert(one_second[0].volume == 15);
  assert(one_second[0].quote_volume == 1505);
  assert(one_second[0].trade_count == 2);

  assert(one_second[1].open_time_ns == 2 * s);
  assert(one_second[1].open == 99);
  assert(one_second[1].high == 103);
  assert(one_second[1].low == 98);
  assert(one_second[1].close == 103);
  assert(one_second[1].volume == 24);
  assert(one_second[1].quote_volume == 2412);
  assert(one_second[1].trade_count == 4);

  assert(one_minute.size() == 1);
  assert(one_minute[0].open == 100);
  assert(one_minute[0].high == 103);
  assert(one_minute[0].low == 98);
  assert(one_minute[0].close == 103);
  assert(one_minute[0].volume == 39);
  assert(one_minute[0].quote_volume == 3917);
  assert(one_minute[0].trade_count == 6);

  return 0;
}
