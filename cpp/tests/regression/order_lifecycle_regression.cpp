#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

using namespace lobx_test;

TEST(OrderLifecycleRegression, IocAndFokCombinationMustBeRejected) {
  auto f = ExchangeFixture::Spot();

  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 9001, lob::Side::Ask, 100, 1, lob::IOC | lob::FOK, 1);
  EXPECT_FALSE_MSG(order.accepted,
                   "user=alice symbol=BTC-USDT order_id=9001 side=SELL price=100 qty=1 flags=IOC|FOK must be rejected");
  EXPECT_EQ(order.code, lobx::RejectCode::UnsupportedOrderType);
}

TEST(OrderLifecycleRegression, CancelRequiresOwnerIdentityToRejectNonOwnerCancel) {
  auto f = ExchangeFixture::Spot();

  auto ask = f.exchange.submit_limit(f.spot_symbol, f.alice, 9002, lob::Side::Ask, 100, 5, lob::NONE, 1);
  EXPECT_TRUE_MSG(ask.accepted, "setup user=alice symbol=BTC-USDT order_id=9002 side=SELL price=100 qty=5 reason=" + ask.reason);

  EXPECT_FALSE_MSG(f.exchange.cancel(f.spot_symbol, f.bob, 9002, 2),
                   "non-owner user=bob must not cancel alice order_id=9002");
  EXPECT_TRUE_MSG(f.exchange.cancel(f.spot_symbol, f.alice, 9002, 3),
                  "owner user=alice should cancel order_id=9002");
}

TEST(OrderLifecycleRegression, TakerFeeIsCreditedToFeeAccountLedger) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);

  EXPECT_TRUE(f.submit(f.alice, 9003, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto buy = f.submit(f.bob, 9004, lob::Side::Bid, 100, 1, lob::IOC, 2);
  EXPECT_TRUE_MSG(buy.accepted, "setup taker buy reason=" + buy.reason);

  constexpr lobx::UserId fee_account = std::numeric_limits<lobx::UserId>::max();
  EXPECT_EQ_MSG(f.ledger.balance(fee_account, f.quote_asset).total, 1,
                "taker fee should be credited to fee account ledger");
}
