#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

void open_long_for_reduce_only(PerpEngineFixture& f) {
  EXPECT_TRUE(f.submit(f.bob, 33001, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  auto open = f.submit(f.alice, 33002, lob::Side::Bid, 100, 4, lob::IOC, 2);
  EXPECT_TRUE_MSG(open.accepted, "setup open long reason=" + open.reason + " " + f.margin_summary(f.alice));
  EXPECT_EQ(open.exec.filled, 4);
}

} // namespace

TEST(MarketEngineReduceOnlyRegression, ReduceOnlyOrderDoesNotLockAdditionalMargin) {
  PerpEngineFixture f;
  open_long_for_reduce_only(f);
  const auto locked_before = f.ledger.locked(f.alice, f.margin_asset);

  auto reduce = f.submit(f.alice, 33011, lob::Side::Ask, 120, 4, lobx::LOBX_REDUCE_ONLY | lob::POST_ONLY, 3);
  EXPECT_TRUE_MSG(reduce.accepted, "user=alice symbol=BTC-USDT-PERP order_id=33011 side=SELL price=120 qty=4 flags=REDUCE_ONLY|POST_ONLY reason=" + reduce.reason);

  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.margin_asset), locked_before,
                "reduce-only resting order should not add order margin: " + f.margin_summary(f.alice));
}

TEST(MarketEngineReduceOnlyRegression, ReduceOnlyPartialFillRemainingDoesNotRelockMargin) {
  PerpEngineFixture f;
  open_long_for_reduce_only(f);

  EXPECT_TRUE(f.submit(f.carol, 33021, lob::Side::Bid, 100, 2, lob::POST_ONLY, 3).accepted);
  auto reduce = f.submit(f.alice, 33022, lob::Side::Ask, 100, 4, lobx::LOBX_REDUCE_ONLY, 4);

  EXPECT_TRUE_MSG(reduce.accepted, "setup reduce-only partial fill reason=" + reduce.reason + " " + f.margin_summary(f.alice));
  EXPECT_EQ(reduce.exec.filled, 2);
  EXPECT_EQ(reduce.exec.remaining, 2);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 2);
  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.margin_asset), 40,
                "reduce-only remaining order must not be re-locked by adjust_resting_lock: " + f.margin_summary(f.alice));
}

TEST(MarketEngineReduceOnlyRegression, ReduceOnlyCannotIncreaseAbsExposure) {
  PerpEngineFixture f;
  open_long_for_reduce_only(f);

  auto increase = f.submit(f.alice, 33031, lob::Side::Bid, 100, 1, lobx::LOBX_REDUCE_ONLY | lob::IOC, 3);
  EXPECT_FALSE_MSG(increase.accepted, "reduce-only buy from long should be rejected");
  EXPECT_EQ(increase.code, lobx::RejectCode::ReduceOnlyWouldIncrease);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 4);
}

TEST(MarketEngineReduceOnlyRegression, ReduceOnlyCrossZeroIsRejectedOrClipped) {
  PerpEngineFixture f;
  open_long_for_reduce_only(f);

  EXPECT_TRUE(f.submit(f.carol, 33041, lob::Side::Bid, 100, 6, lob::POST_ONLY, 3).accepted);
  auto flip = f.submit(f.alice, 33042, lob::Side::Ask, 100, 6, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);
  EXPECT_FALSE_MSG(flip.accepted, "reduce-only sell qty greater than long position should be rejected or clipped before matching");
  EXPECT_EQ(flip.code, lobx::RejectCode::ReduceOnlyWouldIncrease);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 4);
}

TEST(MarketEngineReduceOnlyRegression, PurgeInvalidReduceOnlyCancelsAndReleasesAnyExistingLock) {
  PerpEngineFixture f;
  open_long_for_reduce_only(f);

  auto resting_reduce = f.submit(f.alice, 33051, lob::Side::Ask, 120, 4, lobx::LOBX_REDUCE_ONLY | lob::POST_ONLY, 3);
  EXPECT_TRUE(resting_reduce.accepted);
  EXPECT_TRUE(f.submit(f.carol, 33052, lob::Side::Bid, 100, 4, lob::POST_ONLY, 4).accepted);
  EXPECT_TRUE(f.submit(f.alice, 33053, lob::Side::Ask, 100, 4, lobx::LOBX_REDUCE_ONLY | lob::IOC, 5).accepted);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);

  auto stale_cross = f.submit(f.carol, 33054, lob::Side::Bid, 120, 4, lob::IOC, 6);
  EXPECT_TRUE_MSG(stale_cross.accepted, "stale reduce-only purge should avoid internal settlement failure reason=" + stale_cross.reason);
  EXPECT_EQ(stale_cross.exec.filled, 0);
  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.margin_asset), 0, f.margin_summary(f.alice));
}
