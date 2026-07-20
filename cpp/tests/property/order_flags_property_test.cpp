#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(OrderFlagsPropertyTest, UnknownFlagBitsAreRejectedForManyCombinations) {
  constexpr uint32_t allowed = lob::IOC | lob::FOK | lob::POST_ONLY | lob::STP | lobx::LOBX_REDUCE_ONLY;

  for (uint32_t bits : {0u, 1u, 2u, 3u, 4u, 5u, 8u, 9u, 16u, allowed, allowed | (1u << 30), 1u << 30}) {
    auto f = ExchangeFixture::Spot();
    auto result = f.exchange.submit_limit(f.spot_symbol, f.alice, 17000 + bits, lob::Side::Ask, 100, 1, bits, 1);
    if ((bits & ~allowed) != 0u) {
      EXPECT_FALSE_MSG(result.accepted, "flags=" + std::to_string(bits) + " reason=" + result.reason);
      EXPECT_EQ(result.code, lobx::RejectCode::UnsupportedOrderType);
    } else {
      EXPECT_NE_MSG(result.code, lobx::RejectCode::InternalError, "flags=" + std::to_string(bits) + " reason=" + result.reason);
    }
    require_invariants(f.exchange);
  }
}

TEST(OrderFlagsPropertyTest, IocAndFokOrdersNeverRestWhenAccepted) {
  for (uint32_t flags : {static_cast<uint32_t>(lob::IOC),
                         static_cast<uint32_t>(lob::FOK),
                         static_cast<uint32_t>(lob::IOC | lob::STP),
                         static_cast<uint32_t>(lob::FOK | lob::STP)}) {
    auto f = ExchangeFixture::Spot();
    auto result = f.exchange.submit_limit(f.spot_symbol, f.alice, 17100 + flags, lob::Side::Ask, 100, 1, flags, 1);
    EXPECT_TRUE_MSG(result.accepted, "flags=" + std::to_string(flags) + " reason=" + result.reason);
    EXPECT_TRUE_MSG(f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10).empty(), "flags=" + std::to_string(flags));
    require_invariants(f.exchange);
  }
}

TEST(OrderFlagsPropertyTest, PostOnlyDoesNotImmediatelyTrade) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 17201, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto cross = f.exchange.submit_limit(f.spot_symbol, f.bob, 17202, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2);
  EXPECT_FALSE(cross.accepted);
  EXPECT_EQ(cross.code, lobx::RejectCode::PostOnlyWouldCross);
  EXPECT_EQ(f.exchange.trades().size(), 0UL);
  require_invariants(f.exchange);
}

TEST(OrderFlagsPropertyTest, StpOrdersDoNotProduceSelfFills) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 17301, lob::Side::Ask, 100, 2, lob::NONE, 1).accepted);
  auto stp = f.exchange.submit_limit(f.spot_symbol, f.alice, 17302, lob::Side::Bid, 100, 2, lob::IOC | lob::STP, 2);
  EXPECT_TRUE_MSG(stp.accepted, "user=alice symbol=BTC-USDT order_id=17302 side=BUY price=100 qty=2 flags=IOC|STP reason=" + stp.reason);
  EXPECT_EQ(stp.exec.filled, 0);
  for (const auto& trade : f.exchange.trades()) {
    EXPECT_NE_MSG(trade.buyer, trade.seller, "STP self fill detected");
  }
  require_invariants(f.exchange);
}
