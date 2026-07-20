#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"
#include "test_utils/accounting_test_utils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <vector>

using namespace lobx_test;

namespace {

constexpr double kMarkPrice = 100.0;
constexpr double kTolerance = 0.0;
constexpr lobx::UserId kFeeAccount = std::numeric_limits<lobx::UserId>::max();

std::string accounting_details(const char* label,
                               const AccountSnapshot& before,
                               const AccountSnapshot& after,
                               double mark_price) {
  std::ostringstream os;
  os << label << " before{" << account_snapshot_string(before, mark_price)
     << "} after{" << account_snapshot_string(after, mark_price)
     << "} pnl=" << after.equity(mark_price) - before.equity(mark_price);
  return os.str();
}

void expect_trade(const lobx::SubmitResult& result,
                  lobx::UserId buyer,
                  lobx::UserId seller,
                  lobx::OrderId buyer_order,
                  lobx::OrderId seller_order,
                  lob::Side liquidity_side) {
  EXPECT_TRUE_MSG(result.accepted, result.reason);
  EXPECT_EQ(result.trades.size(), static_cast<std::size_t>(1));
  const lobx::TradeEvent& trade = result.trades.front();
  EXPECT_EQ(trade.price, 100);
  EXPECT_EQ(trade.qty, 10);
  EXPECT_EQ(trade.buyer, buyer);
  EXPECT_EQ(trade.seller, seller);
  EXPECT_EQ(trade.buyer_order_id, buyer_order);
  EXPECT_EQ(trade.seller_order_id, seller_order);
  EXPECT_EQ(trade.liquidity_side, liquidity_side);
}

double pnl(const AccountSnapshot& before, const AccountSnapshot& after, double mark_price) {
  return after.equity(mark_price) - before.equity(mark_price);
}

} // namespace

TEST(ExchangeAccountingInvariants, ZeroFeeTakerBuyConservesTotalEquity) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/0);
  const lobx::UserId seller = f.alice;
  const lobx::UserId buyer = f.bob;

  const AccountSnapshot buyer_before = snapshot_account(f.ledger, buyer, f.quote_asset, f.base_asset);
  const AccountSnapshot seller_before = snapshot_account(f.ledger, seller, f.quote_asset, f.base_asset);
  const double system_before = total_equity({buyer_before, seller_before}, kMarkPrice);

  EXPECT_TRUE_MSG(f.submit(seller, 1001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted,
                  f.ledger_summary(seller));
  const lobx::SubmitResult buy = f.submit(buyer, 1002, lob::Side::Bid, 100, 10, lob::IOC, 2);
  expect_trade(buy, buyer, seller, 1002, 1001, lob::Side::Ask);

  const AccountSnapshot buyer_after = snapshot_account(f.ledger, buyer, f.quote_asset, f.base_asset);
  const AccountSnapshot seller_after = snapshot_account(f.ledger, seller, f.quote_asset, f.base_asset);

  EXPECT_NEAR_VALUE(buyer_after.total_cash - buyer_before.total_cash, -1000.0, kTolerance,
                    accounting_details("buyer", buyer_before, buyer_after, kMarkPrice));
  EXPECT_NEAR_VALUE(buyer_after.total_inventory - buyer_before.total_inventory, 10.0, kTolerance,
                    accounting_details("buyer", buyer_before, buyer_after, kMarkPrice));
  EXPECT_NEAR_VALUE(seller_after.total_cash - seller_before.total_cash, 1000.0, kTolerance,
                    accounting_details("seller", seller_before, seller_after, kMarkPrice));
  EXPECT_NEAR_VALUE(seller_after.total_inventory - seller_before.total_inventory, -10.0, kTolerance,
                    accounting_details("seller", seller_before, seller_after, kMarkPrice));
  EXPECT_NEAR_VALUE(pnl(buyer_before, buyer_after, kMarkPrice), 0.0, kTolerance,
                    accounting_details("buyer", buyer_before, buyer_after, kMarkPrice));
  EXPECT_NEAR_VALUE(pnl(seller_before, seller_after, kMarkPrice), 0.0, kTolerance,
                    accounting_details("seller", seller_before, seller_after, kMarkPrice));
  EXPECT_NEAR_VALUE(total_equity({buyer_after, seller_after}, kMarkPrice), system_before, kTolerance,
                    "system equity");
  EXPECT_FALSE(pnl(buyer_before, buyer_after, kMarkPrice) < 0.0 && pnl(seller_before, seller_after, kMarkPrice) < 0.0);
  EXPECT_TRUE(f.ledger.invariant_ok());
}

TEST(ExchangeAccountingInvariants, ZeroFeeTakerSellConservesTotalEquity) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/0);
  const lobx::UserId buyer = f.alice;
  const lobx::UserId seller = f.bob;

  const AccountSnapshot buyer_before = snapshot_account(f.ledger, buyer, f.quote_asset, f.base_asset);
  const AccountSnapshot seller_before = snapshot_account(f.ledger, seller, f.quote_asset, f.base_asset);
  const double system_before = total_equity({buyer_before, seller_before}, kMarkPrice);

  EXPECT_TRUE_MSG(f.submit(buyer, 2001, lob::Side::Bid, 100, 10, lob::POST_ONLY, 1).accepted,
                  f.ledger_summary(buyer));
  const lobx::SubmitResult sell = f.submit(seller, 2002, lob::Side::Ask, 100, 10, lob::IOC, 2);
  expect_trade(sell, buyer, seller, 2001, 2002, lob::Side::Bid);

  const AccountSnapshot buyer_after = snapshot_account(f.ledger, buyer, f.quote_asset, f.base_asset);
  const AccountSnapshot seller_after = snapshot_account(f.ledger, seller, f.quote_asset, f.base_asset);

  EXPECT_NEAR_VALUE(buyer_after.total_cash - buyer_before.total_cash, -1000.0, kTolerance,
                    accounting_details("buyer", buyer_before, buyer_after, kMarkPrice));
  EXPECT_NEAR_VALUE(buyer_after.total_inventory - buyer_before.total_inventory, 10.0, kTolerance,
                    accounting_details("buyer", buyer_before, buyer_after, kMarkPrice));
  EXPECT_NEAR_VALUE(seller_after.total_cash - seller_before.total_cash, 1000.0, kTolerance,
                    accounting_details("seller", seller_before, seller_after, kMarkPrice));
  EXPECT_NEAR_VALUE(seller_after.total_inventory - seller_before.total_inventory, -10.0, kTolerance,
                    accounting_details("seller", seller_before, seller_after, kMarkPrice));
  EXPECT_NEAR_VALUE(total_equity({buyer_after, seller_after}, kMarkPrice), system_before, kTolerance,
                    "system equity");
  EXPECT_FALSE((buyer_after.total_cash - buyer_before.total_cash) < 0.0 &&
               (seller_after.total_cash - seller_before.total_cash) < 0.0);
  EXPECT_FALSE(((buyer_after.total_inventory - buyer_before.total_inventory) > 0.0) ==
               ((seller_after.total_inventory - seller_before.total_inventory) > 0.0));
  EXPECT_TRUE(f.ledger.invariant_ok());
}

TEST(ExchangeAccountingInvariants, BuyerDebitEqualsSellerCreditWhenFeesAreZero) {
  {
    SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/0);
    const AccountSnapshot buyer_before = snapshot_account(f.ledger, f.bob, f.quote_asset, f.base_asset);
    const AccountSnapshot seller_before = snapshot_account(f.ledger, f.alice, f.quote_asset, f.base_asset);
    EXPECT_TRUE(f.submit(f.alice, 3001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);
    const lobx::SubmitResult buy = f.submit(f.bob, 3002, lob::Side::Bid, 100, 10, lob::IOC, 2);
    expect_trade(buy, f.bob, f.alice, 3002, 3001, lob::Side::Ask);
    const AccountSnapshot buyer_after = snapshot_account(f.ledger, f.bob, f.quote_asset, f.base_asset);
    const AccountSnapshot seller_after = snapshot_account(f.ledger, f.alice, f.quote_asset, f.base_asset);
    EXPECT_NEAR_VALUE((buyer_after.total_cash - buyer_before.total_cash) +
                          (seller_after.total_cash - seller_before.total_cash),
                      0.0, kTolerance, "taker buy cash symmetry");
    EXPECT_NEAR_VALUE((buyer_after.total_inventory - buyer_before.total_inventory) +
                          (seller_after.total_inventory - seller_before.total_inventory),
                      0.0, kTolerance, "taker buy inventory symmetry");
  }
  {
    SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/0);
    const AccountSnapshot buyer_before = snapshot_account(f.ledger, f.alice, f.quote_asset, f.base_asset);
    const AccountSnapshot seller_before = snapshot_account(f.ledger, f.bob, f.quote_asset, f.base_asset);
    EXPECT_TRUE(f.submit(f.alice, 3003, lob::Side::Bid, 100, 10, lob::POST_ONLY, 1).accepted);
    const lobx::SubmitResult sell = f.submit(f.bob, 3004, lob::Side::Ask, 100, 10, lob::IOC, 2);
    expect_trade(sell, f.alice, f.bob, 3003, 3004, lob::Side::Bid);
    const AccountSnapshot buyer_after = snapshot_account(f.ledger, f.alice, f.quote_asset, f.base_asset);
    const AccountSnapshot seller_after = snapshot_account(f.ledger, f.bob, f.quote_asset, f.base_asset);
    EXPECT_NEAR_VALUE((buyer_after.total_cash - buyer_before.total_cash) +
                          (seller_after.total_cash - seller_before.total_cash),
                      0.0, kTolerance, "taker sell cash symmetry");
    EXPECT_NEAR_VALUE((buyer_after.total_inventory - buyer_before.total_inventory) +
                          (seller_after.total_inventory - seller_before.total_inventory),
                      0.0, kTolerance, "taker sell inventory symmetry");
  }
}

TEST(ExchangeAccountingInvariants, RestingLimitOrderLockDoesNotReduceTotalEquity) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/0);
  const lobx::UserId user = f.alice;
  const AccountSnapshot before_bid = snapshot_account(f.ledger, user, f.quote_asset, f.base_asset);
  EXPECT_TRUE_MSG(f.submit(user, 4001, lob::Side::Bid, 90, 10, lob::POST_ONLY, 1).accepted, f.ledger_summary(user));
  const AccountSnapshot after_bid = snapshot_account(f.ledger, user, f.quote_asset, f.base_asset);
  EXPECT_TRUE(after_bid.available_cash < before_bid.available_cash);
  EXPECT_TRUE(after_bid.locked_cash > before_bid.locked_cash);
  EXPECT_NEAR_VALUE(after_bid.total_cash, before_bid.total_cash, kTolerance, "resting bid total cash");
  EXPECT_NEAR_VALUE(after_bid.total_inventory, before_bid.total_inventory, kTolerance, "resting bid inventory");
  EXPECT_NEAR_VALUE(after_bid.equity(kMarkPrice), before_bid.equity(kMarkPrice), kTolerance, "resting bid equity");

  const AccountSnapshot before_ask = after_bid;
  EXPECT_TRUE_MSG(f.submit(user, 4002, lob::Side::Ask, 110, 10, lob::POST_ONLY, 2).accepted, f.ledger_summary(user));
  const AccountSnapshot after_ask = snapshot_account(f.ledger, user, f.quote_asset, f.base_asset);
  EXPECT_TRUE(after_ask.available_inventory < before_ask.available_inventory);
  EXPECT_TRUE(after_ask.locked_inventory > before_ask.locked_inventory);
  EXPECT_NEAR_VALUE(after_ask.total_cash, before_ask.total_cash, kTolerance, "resting ask cash");
  EXPECT_NEAR_VALUE(after_ask.total_inventory, before_ask.total_inventory, kTolerance, "resting ask total inventory");
  EXPECT_NEAR_VALUE(after_ask.equity(kMarkPrice), before_ask.equity(kMarkPrice), kTolerance, "resting ask equity");
}

TEST(ExchangeAccountingInvariants, CancelOrderReleasesLockedBalances) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/0);
  const lobx::UserId user = f.alice;

  const AccountSnapshot bid_before = snapshot_account(f.ledger, user, f.quote_asset, f.base_asset);
  EXPECT_TRUE(f.submit(user, 5001, lob::Side::Bid, 90, 10, lob::POST_ONLY, 1).accepted);
  const AccountSnapshot bid_locked = snapshot_account(f.ledger, user, f.quote_asset, f.base_asset);
  EXPECT_TRUE(bid_locked.locked_cash > bid_before.locked_cash);
  EXPECT_TRUE(f.engine.cancel(5001, user, 2));
  const AccountSnapshot bid_after_cancel = snapshot_account(f.ledger, user, f.quote_asset, f.base_asset);
  EXPECT_NEAR_VALUE(bid_after_cancel.available_cash, bid_before.available_cash, kTolerance, "cancel bid free cash");
  EXPECT_NEAR_VALUE(bid_after_cancel.locked_cash, bid_before.locked_cash, kTolerance, "cancel bid locked cash");
  EXPECT_NEAR_VALUE(bid_after_cancel.total_cash, bid_before.total_cash, kTolerance, "cancel bid total cash");
  EXPECT_NEAR_VALUE(bid_after_cancel.equity(kMarkPrice), bid_before.equity(kMarkPrice), kTolerance, "cancel bid equity");

  const AccountSnapshot ask_before = snapshot_account(f.ledger, user, f.quote_asset, f.base_asset);
  EXPECT_TRUE(f.submit(user, 5002, lob::Side::Ask, 110, 10, lob::POST_ONLY, 3).accepted);
  const AccountSnapshot ask_locked = snapshot_account(f.ledger, user, f.quote_asset, f.base_asset);
  EXPECT_TRUE(ask_locked.locked_inventory > ask_before.locked_inventory);
  EXPECT_TRUE(f.engine.cancel(5002, user, 4));
  const AccountSnapshot ask_after_cancel = snapshot_account(f.ledger, user, f.quote_asset, f.base_asset);
  EXPECT_NEAR_VALUE(ask_after_cancel.available_inventory, ask_before.available_inventory, kTolerance,
                    "cancel ask free inventory");
  EXPECT_NEAR_VALUE(ask_after_cancel.locked_inventory, ask_before.locked_inventory, kTolerance,
                    "cancel ask locked inventory");
  EXPECT_NEAR_VALUE(ask_after_cancel.total_inventory, ask_before.total_inventory, kTolerance,
                    "cancel ask total inventory");
  EXPECT_NEAR_VALUE(ask_after_cancel.equity(kMarkPrice), ask_before.equity(kMarkPrice), kTolerance,
                    "cancel ask equity");
}

TEST(ExchangeAccountingInvariants, PartialFillThenCancelReleasesRemainingLockedBalance) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/0);
  const lobx::UserId maker = f.alice;
  const lobx::UserId seller = f.bob;

  const AccountSnapshot maker_before = snapshot_account(f.ledger, maker, f.quote_asset, f.base_asset);
  const AccountSnapshot seller_before = snapshot_account(f.ledger, seller, f.quote_asset, f.base_asset);
  EXPECT_TRUE_MSG(f.submit(maker, 6001, lob::Side::Bid, 100, 10, lob::POST_ONLY, 1).accepted, f.ledger_summary(maker));
  const AccountSnapshot maker_locked = snapshot_account(f.ledger, maker, f.quote_asset, f.base_asset);
  EXPECT_NEAR_VALUE(maker_locked.locked_cash - maker_before.locked_cash, 1000.0, kTolerance, "maker bid lock");

  const lobx::SubmitResult sell = f.submit(seller, 6002, lob::Side::Ask, 100, 4, lob::IOC, 2);
  EXPECT_TRUE_MSG(sell.accepted, sell.reason);
  EXPECT_EQ(sell.trades.size(), static_cast<std::size_t>(1));
  EXPECT_EQ(sell.trades.front().qty, 4);
  const AccountSnapshot maker_after_fill = snapshot_account(f.ledger, maker, f.quote_asset, f.base_asset);
  EXPECT_NEAR_VALUE(maker_after_fill.total_cash - maker_before.total_cash, -400.0, kTolerance,
                    accounting_details("maker after partial fill", maker_before, maker_after_fill, kMarkPrice));
  EXPECT_NEAR_VALUE(maker_after_fill.total_inventory - maker_before.total_inventory, 4.0, kTolerance,
                    accounting_details("maker after partial fill", maker_before, maker_after_fill, kMarkPrice));
  EXPECT_NEAR_VALUE(maker_after_fill.locked_cash - maker_before.locked_cash, 600.0, kTolerance,
                    "remaining locked cash after partial fill");

  EXPECT_TRUE(f.engine.cancel(6001, maker, 3));
  const AccountSnapshot maker_after_cancel = snapshot_account(f.ledger, maker, f.quote_asset, f.base_asset);
  const AccountSnapshot seller_after = snapshot_account(f.ledger, seller, f.quote_asset, f.base_asset);
  EXPECT_NEAR_VALUE(maker_after_cancel.locked_cash, maker_before.locked_cash, kTolerance,
                    "remaining bid lock released");
  EXPECT_NEAR_VALUE(maker_after_cancel.total_cash - maker_before.total_cash, -400.0, kTolerance,
                    "maker paid filled quantity only");
  EXPECT_NEAR_VALUE(maker_after_cancel.total_inventory - maker_before.total_inventory, 4.0, kTolerance,
                    "maker inventory after fill");
  EXPECT_NEAR_VALUE(seller_after.total_cash - seller_before.total_cash, 400.0, kTolerance,
                    "seller cash after partial fill");
  EXPECT_NEAR_VALUE(seller_after.total_inventory - seller_before.total_inventory, -4.0, kTolerance,
                    "seller inventory after partial fill");
  EXPECT_NEAR_VALUE(total_equity({maker_after_cancel, seller_after}, kMarkPrice),
                    total_equity({maker_before, seller_before}, kMarkPrice), kTolerance,
                    "partial fill cancel system equity");
}

TEST(ExchangeAccountingInvariants, FeeRevenueExplainsAggregateNegativePnl) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  const lobx::UserId seller = f.alice;
  const lobx::UserId buyer = f.bob;
  const AccountSnapshot buyer_before = snapshot_account(f.ledger, buyer, f.quote_asset, f.base_asset);
  const AccountSnapshot seller_before = snapshot_account(f.ledger, seller, f.quote_asset, f.base_asset);
  const double initial_agents_equity = total_equity({buyer_before, seller_before}, kMarkPrice);
  const lobx::Amount fee_before = f.ledger.balance(kFeeAccount, f.quote_asset).total;

  EXPECT_TRUE(f.submit(seller, 7001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1).accepted);
  const lobx::SubmitResult buy = f.submit(buyer, 7002, lob::Side::Bid, 100, 10, lob::IOC, 2);
  expect_trade(buy, buyer, seller, 7002, 7001, lob::Side::Ask);

  const AccountSnapshot buyer_after = snapshot_account(f.ledger, buyer, f.quote_asset, f.base_asset);
  const AccountSnapshot seller_after = snapshot_account(f.ledger, seller, f.quote_asset, f.base_asset);
  const double final_agents_equity = total_equity({buyer_after, seller_after}, kMarkPrice);
  const lobx::Amount fee_after = f.ledger.balance(kFeeAccount, f.quote_asset).total;
  const double fee_revenue = static_cast<double>(fee_after - fee_before);

  EXPECT_NEAR_VALUE(fee_revenue, 10.0, kTolerance, "fee revenue");
  EXPECT_NEAR_VALUE(final_agents_equity + fee_revenue, initial_agents_equity, kTolerance,
                    "agent pnl plus fee revenue");
  EXPECT_NEAR_VALUE((final_agents_equity - initial_agents_equity) + fee_revenue, 0.0, kTolerance,
                    "aggregate pnl explained by fee revenue");
}

