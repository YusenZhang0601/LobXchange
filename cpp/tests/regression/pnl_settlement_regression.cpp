#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

void open_long(ExchangeFixture& f, lob::Tick entry, lob::Quantity qty, lobx::OrderId maker_id, lobx::OrderId taker_id) {
  auto ask = f.exchange.submit_limit(f.perp_symbol, f.bob, maker_id, lob::Side::Ask, entry, qty, lob::POST_ONLY, 1);
  EXPECT_TRUE_MSG(ask.accepted, "open_long maker ask reason=" + ask.reason);
  auto bid = f.exchange.submit_limit(f.perp_symbol, f.alice, taker_id, lob::Side::Bid, entry, qty, lob::IOC, 2);
  EXPECT_TRUE_MSG(bid.accepted, "open_long taker bid reason=" + bid.reason);
  EXPECT_EQ(bid.exec.filled, qty);
}

void close_long(ExchangeFixture& f, lob::Tick exit, lob::Quantity qty, lobx::OrderId maker_id, lobx::OrderId taker_id) {
  auto bid = f.exchange.submit_limit(f.perp_symbol, f.carol, maker_id, lob::Side::Bid, exit, qty, lob::POST_ONLY, 3);
  EXPECT_TRUE_MSG(bid.accepted, "close_long maker bid reason=" + bid.reason);
  auto ask = f.exchange.submit_limit(f.perp_symbol, f.alice, taker_id, lob::Side::Ask, exit, qty, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);
  EXPECT_TRUE_MSG(ask.accepted, "close_long reduce-only ask reason=" + ask.reason);
  EXPECT_EQ(ask.exec.filled, qty);
}

void open_short(ExchangeFixture& f, lob::Tick entry, lob::Quantity qty, lobx::OrderId maker_id, lobx::OrderId taker_id) {
  auto bid = f.exchange.submit_limit(f.perp_symbol, f.bob, maker_id, lob::Side::Bid, entry, qty, lob::POST_ONLY, 1);
  EXPECT_TRUE_MSG(bid.accepted, "open_short maker bid reason=" + bid.reason);
  auto ask = f.exchange.submit_limit(f.perp_symbol, f.alice, taker_id, lob::Side::Ask, entry, qty, lob::IOC, 2);
  EXPECT_TRUE_MSG(ask.accepted, "open_short taker ask reason=" + ask.reason);
  EXPECT_EQ(ask.exec.filled, qty);
}

void close_short(ExchangeFixture& f, lob::Tick exit, lob::Quantity qty, lobx::OrderId maker_id, lobx::OrderId taker_id) {
  auto ask = f.exchange.submit_limit(f.perp_symbol, f.carol, maker_id, lob::Side::Ask, exit, qty, lob::POST_ONLY, 3);
  EXPECT_TRUE_MSG(ask.accepted, "close_short maker ask reason=" + ask.reason);
  auto bid = f.exchange.submit_limit(f.perp_symbol, f.alice, taker_id, lob::Side::Bid, exit, qty, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);
  EXPECT_TRUE_MSG(bid.accepted, "close_short reduce-only bid reason=" + bid.reason);
  EXPECT_EQ(bid.exec.filled, qty);
}

} // namespace

TEST(PnlSettlementRegression, LongProfitCloseIncreasesQuoteWallet) {
  auto f = ExchangeFixture::Perp();
  const auto initial = f.exchange.balance(f.alice, "USDT").total;

  open_long(f, 100, 4, 5001, 5002);
  close_long(f, 110, 4, 5003, 5004);

  const auto position = f.exchange.position(f.alice, f.perp_symbol);
  EXPECT_EQ(position.signed_qty, 0);
  EXPECT_EQ(position.realized_pnl, 40);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").total, initial + 40, f.wallet_summary(f.alice));
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").locked, 0, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}

TEST(PnlSettlementRegression, LongLossCloseDecreasesQuoteWallet) {
  auto f = ExchangeFixture::Perp();
  const auto initial = f.exchange.balance(f.alice, "USDT").total;

  open_long(f, 100, 4, 5011, 5012);
  close_long(f, 90, 4, 5013, 5014);

  const auto position = f.exchange.position(f.alice, f.perp_symbol);
  EXPECT_EQ(position.signed_qty, 0);
  EXPECT_EQ(position.realized_pnl, -40);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").total, initial - 40, f.wallet_summary(f.alice));
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").locked, 0, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}

TEST(PnlSettlementRegression, ShortProfitCloseIncreasesQuoteWallet) {
  auto f = ExchangeFixture::Perp();
  const auto initial = f.exchange.balance(f.alice, "USDT").total;

  open_short(f, 100, 4, 5021, 5022);
  close_short(f, 90, 4, 5023, 5024);

  const auto position = f.exchange.position(f.alice, f.perp_symbol);
  EXPECT_EQ(position.signed_qty, 0);
  EXPECT_EQ(position.realized_pnl, 40);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").total, initial + 40, f.wallet_summary(f.alice));
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").locked, 0, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}

TEST(PnlSettlementRegression, ShortLossCloseDecreasesQuoteWallet) {
  auto f = ExchangeFixture::Perp();
  const auto initial = f.exchange.balance(f.alice, "USDT").total;

  open_short(f, 100, 4, 5031, 5032);
  close_short(f, 110, 4, 5033, 5034);

  const auto position = f.exchange.position(f.alice, f.perp_symbol);
  EXPECT_EQ(position.signed_qty, 0);
  EXPECT_EQ(position.realized_pnl, -40);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").total, initial - 40, f.wallet_summary(f.alice));
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").locked, 0, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}

TEST(PnlSettlementRegression, RealizedPnlIsNotOnlyStoredInsidePositionEngine) {
  auto f = ExchangeFixture::Perp();
  const auto initial = f.exchange.balance(f.alice, "USDT").total;

  open_long(f, 100, 2, 5041, 5042);
  close_long(f, 120, 2, 5043, 5044);

  const auto position = f.exchange.position(f.alice, f.perp_symbol);
  EXPECT_EQ(position.realized_pnl, 40);
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").total, initial + position.realized_pnl, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}
