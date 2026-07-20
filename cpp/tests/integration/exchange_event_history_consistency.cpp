#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

using namespace lobx_test;

namespace {

int count_events(lobx::Exchange& exchange, const std::string& type) {
  int count = 0;
  for (const auto& event : exchange.events().records()) {
    if (event.type == type) ++count;
  }
  return count;
}

void create_perp_profit_credit_failure(ExchangeFixture& f) {
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 47001, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 47002, lob::Side::Bid, 100, 4, lob::IOC, 2).accepted);
  const auto current = f.exchange.balance(f.alice, "USDT").total;
  EXPECT_TRUE(f.exchange.deposit(f.alice, "USDT", std::numeric_limits<lobx::Amount>::max() - current).ok);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.carol, 47003, lob::Side::Bid, 110, 4, lob::POST_ONLY, 3).accepted);
}

} // namespace

TEST(ExchangeEventHistoryConsistency, RejectedOrderDoesNotEnterTradeHistory) {
  auto f = ExchangeFixture::Spot();

  auto rejected = f.exchange.submit_limit(f.spot_symbol, f.alice, 47011, lob::Side::Ask, 100, 1, 1u << 30, 1);

  EXPECT_FALSE(rejected.accepted);
  EXPECT_TRUE(f.exchange.trades().empty());
  EXPECT_EQ(count_events(f.exchange, "order.rejected"), 1);
}

TEST(ExchangeEventHistoryConsistency, FailedSettlementDoesNotEnterTradeHistory) {
  auto f = ExchangeFixture::Perp();
  create_perp_profit_credit_failure(f);
  const auto trades_before = f.exchange.trades().size();

  auto close = f.exchange.submit_limit(f.perp_symbol, f.alice, 47004, lob::Side::Ask, 110, 4, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);

  EXPECT_FALSE(close.accepted);
  EXPECT_EQ(f.exchange.trades().size(), trades_before);
}

TEST(ExchangeEventHistoryConsistency, FailedSettlementCreatesExactlyOneRejectedEvent) {
  auto f = ExchangeFixture::Perp();
  create_perp_profit_credit_failure(f);
  const int rejected_before = count_events(f.exchange, "order.rejected");
  const int trades_before = count_events(f.exchange, "trade");

  auto close = f.exchange.submit_limit(f.perp_symbol, f.alice, 47024, lob::Side::Ask, 110, 4, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);

  EXPECT_FALSE(close.accepted);
  EXPECT_EQ(count_events(f.exchange, "order.rejected"), rejected_before + 1);
  EXPECT_EQ(count_events(f.exchange, "trade"), trades_before);
}

TEST(ExchangeEventHistoryConsistency, AcceptedTradeHasMatchingTradeEvent) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 47031, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto buy = f.exchange.submit_limit(f.spot_symbol, f.bob, 47032, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE(buy.accepted);
  EXPECT_EQ(f.exchange.trades().size(), 1UL);
  EXPECT_EQ(count_events(f.exchange, "trade"), 1);
}

TEST(ExchangeEventHistoryConsistency, OrderAcceptedEventPrecedesTradeEvents) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 47041, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto buy = f.exchange.submit_limit(f.spot_symbol, f.bob, 47042, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE(buy.accepted);
  size_t accepted_index = f.exchange.events().records().size();
  size_t trade_index = f.exchange.events().records().size();
  for (size_t i = 0; i < f.exchange.events().records().size(); ++i) {
    const auto& event = f.exchange.events().records()[i];
    if (event.type == "order.accepted" && event.payload.find("order=47042") != std::string::npos) accepted_index = i;
    if (event.type == "trade" && event.payload.find("buyer_order=47042") != std::string::npos) trade_index = i;
  }
  EXPECT_TRUE_MSG(accepted_index < trade_index, "accepted event should precede trade event for taker order");
}

TEST(ExchangeEventHistoryConsistency, ExpiredFOKHasOrderExpiredEventAndNoAcceptedEvent) {
  auto f = ExchangeFixture::Spot();

  auto fok = f.exchange.submit_limit(f.spot_symbol, f.bob, 47051, lob::Side::Bid, 100, 1, lob::FOK | lob::STP, 1);

  EXPECT_TRUE(fok.accepted);
  EXPECT_EQ(fok.exec.filled, 0);
  EXPECT_EQ(count_events(f.exchange, "order.expired"), 1);
  for (const auto& event : f.exchange.events().records()) {
    EXPECT_FALSE_MSG(event.type == "order.accepted" && event.payload.find("order=47051") != std::string::npos,
                     "expired FOK should not append order.accepted");
  }
}

TEST(ExchangeEventHistoryConsistency, UnknownMarketRejectDoesNotAppendEngineEvent) {
  auto f = ExchangeFixture::Spot();
  const auto event_count_before = f.exchange.events().records().size();

  auto rejected = f.exchange.submit_limit("UNKNOWN", f.alice, 47061, lob::Side::Bid, 100, 1, lob::NONE, 1);

  EXPECT_FALSE(rejected.accepted);
  EXPECT_EQ(rejected.code, lobx::RejectCode::UnknownMarket);
  EXPECT_EQ(f.exchange.events().records().size(), event_count_before);
}
