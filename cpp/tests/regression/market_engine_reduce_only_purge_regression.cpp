#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

void open_alice_long(PerpEngineFixture& f) {
  EXPECT_TRUE(f.submit(f.bob, 46001, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 46002, lob::Side::Bid, 100, 4, lob::IOC, 2).accepted);
}

bool has_order(lobx::MarketEngine& engine, lobx::OrderId id) {
  for (const auto& order : engine.open_orders()) {
    if (order.id == id) return true;
  }
  return false;
}

int event_count(const lobx::EventStore& events, const std::string& type) {
  int count = 0;
  for (const auto& event : events.records()) {
    if (event.type == type) ++count;
  }
  return count;
}

void leave_stale_alice_reduce_only(PerpEngineFixture& f, lobx::OrderId stale_id) {
  open_alice_long(f);
  EXPECT_TRUE(f.submit(f.alice, stale_id, lob::Side::Ask, 120, 4, lobx::LOBX_REDUCE_ONLY | lob::POST_ONLY, 3).accepted);
  EXPECT_TRUE(f.submit(f.carol, stale_id + 1, lob::Side::Bid, 100, 4, lob::POST_ONLY, 4).accepted);
  EXPECT_TRUE(f.submit(f.alice, stale_id + 2, lob::Side::Ask, 100, 4, lobx::LOBX_REDUCE_ONLY | lob::IOC, 5).accepted);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
  EXPECT_TRUE(has_order(f.engine, stale_id));
}

} // namespace

TEST(MarketEngineReduceOnlyPurgeRegression, PurgeInvalidReduceOnlyReleasesLockWhenCancelSucceeds) {
  PerpEngineFixture f;
  open_alice_long(f);
  EXPECT_TRUE(f.submit(f.alice, 46011, lob::Side::Ask, 120, 4, lobx::LOBX_REDUCE_ONLY | lob::POST_ONLY, 3).accepted);
  EXPECT_TRUE(has_order(f.engine, 46011));
  EXPECT_TRUE(f.submit(f.carol, 46012, lob::Side::Bid, 100, 4, lob::POST_ONLY, 4).accepted);
  EXPECT_TRUE(f.submit(f.alice, 46013, lob::Side::Ask, 100, 4, lobx::LOBX_REDUCE_ONLY | lob::IOC, 5).accepted);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);

  auto cross_stale = f.submit(f.carol, 46014, lob::Side::Bid, 120, 1, lob::IOC, 6);

  EXPECT_TRUE(cross_stale.accepted);
  EXPECT_EQ(cross_stale.exec.filled, 0);
  EXPECT_FALSE_MSG(has_order(f.engine, 46011), "stale reduce-only order should be purged before matching");
}

TEST(MarketEngineReduceOnlyPurgeRegression, PurgeCancelEventIsCommittedOnlyWhenSubmitSucceeds) {
  PerpEngineFixture f;
  leave_stale_alice_reduce_only(f, 46101);
  const int cancel_events_before = event_count(f.events, "order.canceled");

  auto cross_stale = f.submit(f.carol, 46104, lob::Side::Bid, 120, 1, lob::IOC, 6);

  EXPECT_TRUE(cross_stale.accepted);
  EXPECT_FALSE_MSG(has_order(f.engine, 46101), "successful submit should commit stale reduce-only purge");
  EXPECT_EQ_MSG(event_count(f.events, "order.canceled"), cancel_events_before + 1,
                "successful purge should append exactly one order.canceled event");
}

TEST(MarketEngineReduceOnlyPurgeRegression, PurgeCancelEventIsNotCommittedWhenSubmitRiskRejects) {
  PerpEngineFixture f;
  leave_stale_alice_reduce_only(f, 46111);
  const int cancel_events_before = event_count(f.events, "order.canceled");

  auto rejected = f.submit(f.carol, 46114, lob::Side::Bid, 120, 1, 1u << 30, 6);

  EXPECT_FALSE(rejected.accepted);
  EXPECT_TRUE_MSG(has_order(f.engine, 46111), "risk reject after purge attempt must restore stale reduce-only order");
  EXPECT_EQ_MSG(event_count(f.events, "order.canceled"), cancel_events_before,
                "risk reject after purge attempt must not append order.canceled");
}

TEST(MarketEngineReduceOnlyPurgeRegression, PurgeCancelEventIsNotCommittedWhenSubmitSettlementFails) {
  PerpEngineFixture f;
  leave_stale_alice_reduce_only(f, 46121);
  EXPECT_TRUE(f.submit(f.carol, 46124, lob::Side::Ask, 100, 8, lob::POST_ONLY, 6).accepted);
  EXPECT_TRUE(f.submit(f.bob, 46125, lob::Side::Bid, 100, 8, lob::IOC, 7).accepted);
  EXPECT_TRUE(f.submit(f.bob, 46126, lob::Side::Ask, 120, 4, lobx::LOBX_REDUCE_ONLY | lob::POST_ONLY, 8).accepted);
  EXPECT_TRUE(f.ledger.debit_locked(f.bob, f.margin_asset, 1).ok);
  const int cancel_events_before = event_count(f.events, "order.canceled");

  auto failed = f.submit(f.carol, 46127, lob::Side::Bid, 120, 4, lob::IOC, 9);

  EXPECT_FALSE(failed.accepted);
  EXPECT_TRUE_MSG(has_order(f.engine, 46121), "settlement failure after purge attempt must restore stale reduce-only order");
  EXPECT_TRUE_MSG(has_order(f.engine, 46126), "settlement failure must restore valid reduce-only counterparty order");
  EXPECT_EQ_MSG(event_count(f.events, "order.canceled"), cancel_events_before,
                "settlement failure after purge attempt must not append order.canceled");
}

TEST(MarketEngineReduceOnlyPurgeRegression, PurgeInvalidReduceOnlyDoesNotCancelValidReduceOnlyOrders) {
  PerpEngineFixture f;
  open_alice_long(f);
  EXPECT_TRUE(f.submit(f.alice, 46021, lob::Side::Ask, 120, 4, lobx::LOBX_REDUCE_ONLY | lob::POST_ONLY, 3).accepted);

  auto unrelated_bid = f.submit(f.carol, 46022, lob::Side::Bid, 110, 1, lob::POST_ONLY, 4);

  EXPECT_TRUE(unrelated_bid.accepted);
  EXPECT_TRUE_MSG(has_order(f.engine, 46021), "valid reduce-only order should remain resting");
}

TEST(MarketEngineReduceOnlyPurgeRegression, PurgeInvalidReduceOnlySkipsSameSideOrdersCorrectly) {
  PerpEngineFixture f;
  open_alice_long(f);
  EXPECT_TRUE(f.submit(f.alice, 46031, lob::Side::Ask, 120, 4, lobx::LOBX_REDUCE_ONLY | lob::POST_ONLY, 3).accepted);

  auto same_side = f.submit(f.carol, 46032, lob::Side::Ask, 119, 1, lob::POST_ONLY, 4);

  EXPECT_TRUE(same_side.accepted);
  EXPECT_TRUE_MSG(has_order(f.engine, 46031), "same-side submit should not purge opposite reduce-only order");
}

TEST(MarketEngineReduceOnlyPurgeRegression, ReduceOnlyOrderWithZeroPositionIsRejected) {
  PerpEngineFixture f;

  auto reduce = f.submit(f.alice, 46041, lob::Side::Ask, 100, 1, lobx::LOBX_REDUCE_ONLY | lob::IOC, 1);

  EXPECT_FALSE(reduce.accepted);
  EXPECT_EQ(reduce.code, lobx::RejectCode::ReduceOnlyWouldIncrease);
}

TEST(MarketEngineReduceOnlyPurgeRegression, ReduceOnlyOrderGreaterThanPositionIsRejectedOrClipped) {
  PerpEngineFixture f;
  open_alice_long(f);

  auto reduce = f.submit(f.alice, 46051, lob::Side::Ask, 100, 5, lobx::LOBX_REDUCE_ONLY | lob::IOC, 3);

  EXPECT_FALSE(reduce.accepted);
  EXPECT_EQ(reduce.code, lobx::RejectCode::ReduceOnlyWouldIncrease);
}

TEST(MarketEngineReduceOnlyPurgeRegression, ReduceOnlyPartialFillCannotFlipPosition) {
  PerpEngineFixture f;
  open_alice_long(f);
  EXPECT_TRUE(f.submit(f.carol, 46061, lob::Side::Bid, 100, 2, lob::POST_ONLY, 3).accepted);

  auto reduce = f.submit(f.alice, 46062, lob::Side::Ask, 100, 4, lobx::LOBX_REDUCE_ONLY, 4);

  EXPECT_TRUE(reduce.accepted);
  EXPECT_EQ(reduce.exec.filled, 2);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 2);
}
