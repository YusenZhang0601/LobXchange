#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <algorithm>
#include <limits>
#include <ostream>
#include <string>

using namespace lobx_test;

namespace {

struct WideAmount {
  __int128 value{0};
};

bool operator==(WideAmount lhs, WideAmount rhs) {
  return lhs.value == rhs.value;
}

std::ostream& operator<<(std::ostream& os, WideAmount amount) {
  if (amount.value == 0) return os << '0';
  bool negative = amount.value < 0;
  unsigned __int128 remaining = negative ? static_cast<unsigned __int128>(-amount.value)
                                         : static_cast<unsigned __int128>(amount.value);
  std::string digits;
  while (remaining > 0) {
    digits.push_back(static_cast<char>('0' + remaining % 10));
    remaining /= 10;
  }
  if (negative) digits.push_back('-');
  std::reverse(digits.begin(), digits.end());
  return os << digits;
}

WideAmount total_asset(const lobx::AccountLedger& ledger, lobx::AssetId asset) {
  WideAmount total{};
  for (const auto& balance : ledger.balances()) {
    if (balance.asset == asset) total.value += balance.total;
  }
  return total;
}

void force_fee_credit_overflow(SpotEngineFixture& f) {
  EXPECT_TRUE(f.ledger.deposit(std::numeric_limits<lobx::UserId>::max(), f.quote_asset,
                               std::numeric_limits<lobx::Amount>::max()).ok);
}

} // namespace

TEST(SystemConservationInvariants, SpotConservesBaseAssetAcrossTradesCancelsFailures) {
  SpotEngineFixture f;
  const auto base_before = total_asset(f.ledger, f.base_asset);

  EXPECT_TRUE(f.submit(f.alice, 49001, lob::Side::Ask, 100, 5, lob::NONE, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 49002, lob::Side::Bid, 100, 2, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.engine.cancel(49001, f.alice, 3));

  EXPECT_EQ_MSG(total_asset(f.ledger, f.base_asset), base_before, "spot base asset should be conserved");
}

TEST(SystemConservationInvariants, SpotConservesQuoteAssetIncludingFeeAccount) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  const auto quote_before = total_asset(f.ledger, f.quote_asset);

  EXPECT_TRUE(f.submit(f.alice, 49011, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 49012, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  EXPECT_EQ_MSG(total_asset(f.ledger, f.quote_asset), quote_before, "spot quote asset should be conserved including fee account");
}

TEST(SystemConservationInvariants, SpotFailedOrdersDoNotChangeTotalAssets) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  force_fee_credit_overflow(f);
  EXPECT_TRUE(f.submit(f.alice, 49021, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto base_before = total_asset(f.ledger, f.base_asset);
  const auto quote_before = total_asset(f.ledger, f.quote_asset);

  auto failed = f.submit(f.bob, 49022, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_FALSE(failed.accepted);
  EXPECT_EQ_MSG(total_asset(f.ledger, f.base_asset), base_before, "failed order must not change base total");
  EXPECT_EQ_MSG(total_asset(f.ledger, f.quote_asset), quote_before, "failed order must not change quote total");
}

TEST(SystemConservationInvariants, BookDepthEqualsOpenOrdersAggregate) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 49031, lob::Side::Ask, 100, 2, lob::NONE, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 49032, lob::Side::Bid, 90, 3, lob::NONE, 2).accepted);

  lob::Quantity open_asks = 0;
  lob::Quantity open_bids = 0;
  for (const auto& order : f.engine.open_orders()) {
    if (order.side == lob::Side::Ask) open_asks += order.leaves_qty;
    if (order.side == lob::Side::Bid) open_bids += order.leaves_qty;
  }
  lob::Quantity book_asks = 0;
  lob::Quantity book_bids = 0;
  for (const auto& level : f.engine.topN(lob::Side::Ask, 100)) book_asks += level.second;
  for (const auto& level : f.engine.topN(lob::Side::Bid, 100)) book_bids += level.second;

  EXPECT_EQ(open_asks, book_asks);
  EXPECT_EQ(open_bids, book_bids);
}

TEST(SystemConservationInvariants, IOCAndFOKNeverRest) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 49041, lob::Side::Bid, 100, 1, lob::IOC, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 49042, lob::Side::Bid, 100, 1, lob::FOK, 2).accepted);

  for (const auto& order : f.engine.open_orders()) {
    EXPECT_TRUE_MSG((order.flags & (lob::IOC | lob::FOK)) == 0u, "IOC/FOK order must not remain open");
  }
}
