#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/scenario_builder.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(FokStpRegression, FokDoesNotCountSelfOrdersAsFillableLiquidity) {
  auto f = ExchangeFixture::Spot();
  ScenarioBuilder s(f.exchange);

  auto alice_ask = s.user(f.alice).sell(f.spot_symbol, 5, 100).gtc();
  auto bob_ask = s.user(f.bob).sell(f.spot_symbol, 5, 100).gtc();
  EXPECT_TRUE_MSG(alice_ask.accepted, s.sequence());
  EXPECT_TRUE_MSG(bob_ask.accepted, s.sequence());

  const auto before_trades = f.exchange.trades().size();
  auto fok = s.user(f.alice).buy(f.spot_symbol, 8, 100).fok().stp_cancel_newest().submit();

  EXPECT_TRUE_MSG(fok.accepted, s.sequence());
  EXPECT_EQ_MSG(fok.exec.filled, 0, s.sequence());
  EXPECT_EQ_MSG(f.exchange.trades().size(), before_trades, s.sequence());
  EXPECT_TRUE_MSG(f.exchange.topN(f.spot_symbol, lob::Side::Bid, 10).empty(), s.sequence());
  auto asks = f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10);
  EXPECT_FALSE_MSG(asks.empty(), s.sequence());
  EXPECT_EQ_MSG(asks[0].second, 10, s.sequence());
  s.expect_no_self_trade();
  s.expect_invariants_hold();
}

TEST(FokStpRegression, FokStpFailureDoesNotLeaveRestingGhostOrder) {
  SpotEngineFixture f;

  auto ask = f.submit(f.alice, 2001, lob::Side::Ask, 100, 5, lob::POST_ONLY, 1);
  EXPECT_TRUE_MSG(ask.accepted, f.ledger_summary(f.alice));

  auto fok = f.submit(f.alice, 2002, lob::Side::Bid, 100, 8, lob::FOK | lob::STP, 2);
  EXPECT_TRUE_MSG(fok.code != lobx::RejectCode::InternalError, "order_id=2002 side=BUY price=100 qty=8 flags=FOK|STP reason=" + fok.reason);
  EXPECT_EQ(fok.exec.filled, 0);

  auto report = check_order_book_invariants(f.engine, f.ledger);
  EXPECT_TRUE_MSG(report.ok, report_to_string(report));
  EXPECT_TRUE(f.engine.topN(lob::Side::Bid, 10).empty());
}

TEST(FokStpRegression, FokInsufficientExternalLiquidityDoesNotPartiallyTrade) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 2011, lob::Side::Ask, 100, 5, lob::POST_ONLY, 1).accepted);
  const auto before_trades = f.exchange.trades().size();

  auto fok = f.exchange.submit_limit(f.spot_symbol, f.alice, 2012, lob::Side::Bid, 100, 8, lob::FOK, 2);
  EXPECT_TRUE_MSG(fok.accepted, "user=alice symbol=BTC-USDT order_id=2012 side=BUY price=100 qty=8 flags=FOK reason=" + fok.reason);
  EXPECT_EQ(fok.exec.filled, 0);
  EXPECT_EQ(f.exchange.trades().size(), before_trades);
  EXPECT_TRUE(f.exchange.topN(f.spot_symbol, lob::Side::Bid, 10).empty());
  require_invariants(f.exchange);
}

TEST(FokStpRegression, StpCancelKeepsBookOpenOrdersAndLocksConsistent) {
  SpotEngineFixture f;

  auto ask = f.submit(f.alice, 2021, lob::Side::Ask, 100, 5, lob::NONE, 1);
  EXPECT_TRUE_MSG(ask.accepted, f.ledger_summary(f.alice));
  EXPECT_EQ(f.ledger.locked(f.alice, f.base_asset), 5);

  auto self_cross = f.submit(f.alice, 2022, lob::Side::Bid, 100, 5, lob::IOC | lob::STP, 2);
  EXPECT_TRUE_MSG(self_cross.accepted, "order_id=2022 flags=IOC|STP reason=" + self_cross.reason);
  EXPECT_EQ(self_cross.exec.filled, 0);

  auto report = check_order_book_invariants(f.engine, f.ledger);
  EXPECT_TRUE_MSG(report.ok, report_to_string(report));
  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.base_asset), 0, f.ledger_summary(f.alice));
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10).empty());
}

TEST(FokStpRegression, FollowupNormalOrderAfterFokStpDoesNotFailSettlement) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 2031, lob::Side::Ask, 100, 5, lob::POST_ONLY, 1).accepted);
  auto fok = f.exchange.submit_limit(f.spot_symbol, f.alice, 2032, lob::Side::Bid, 100, 8, lob::FOK | lob::STP, 2);
  EXPECT_TRUE_MSG(fok.code != lobx::RejectCode::InternalError, "order_id=2032 flags=FOK|STP reason=" + fok.reason);

  auto followup = f.exchange.submit_limit(f.spot_symbol, f.carol, 2033, lob::Side::Bid, 100, 1, lob::IOC, 3);
  EXPECT_TRUE_MSG(followup.accepted, "user=carol symbol=BTC-USDT order_id=2033 side=BUY price=100 qty=1 flags=IOC reason=" + followup.reason);
  EXPECT_NE(followup.code, lobx::RejectCode::InternalError);
  require_invariants(f.exchange);
}
