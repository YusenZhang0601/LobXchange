#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

int cancel_event_count(const lobx::EventStore& events) {
  int count = 0;
  for (const auto& event : events.records()) {
    if (event.type == "order.canceled") ++count;
  }
  return count;
}

const lobx::EventRecord* last_cancel_event(const lobx::EventStore& events) {
  const lobx::EventRecord* found = nullptr;
  for (const auto& event : events.records()) {
    if (event.type == "order.canceled") found = &event;
  }
  return found;
}

bool has_open_order(lobx::MarketEngine& engine, lobx::OrderId id) {
  for (const auto& order : engine.open_orders()) {
    if (order.id == id) return true;
  }
  return false;
}

} // namespace

TEST(MarketEngineCancelConsistencyRegression, CancelReleaseFailureDoesNotRemoveFromBook) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 43001, lob::Side::Ask, 100, 2, lob::NONE, 1).accepted);
  EXPECT_TRUE(f.ledger.debit_locked(f.alice, f.base_asset, 1).ok);

  const bool canceled = f.engine.cancel(43001, f.alice, 2);

  EXPECT_FALSE(canceled);
  auto asks = f.engine.topN(lob::Side::Ask, 10);
  EXPECT_FALSE_MSG(asks.empty(), "failed cancel must leave book level intact");
  EXPECT_EQ_MSG(asks[0].second, 2, "failed cancel must not remove book qty");
}

TEST(MarketEngineCancelConsistencyRegression, CancelFailureKeepsBookAndOpenConsistent) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 43011, lob::Side::Ask, 100, 2, lob::NONE, 1).accepted);
  EXPECT_TRUE(f.ledger.debit_locked(f.alice, f.base_asset, 1).ok);

  EXPECT_FALSE(f.engine.cancel(43011, f.alice, 2));

  EXPECT_TRUE_MSG(has_open_order(f.engine, 43011), "failed cancel must keep open order metadata");
  auto asks = f.engine.topN(lob::Side::Ask, 10);
  EXPECT_FALSE(asks.empty());
  EXPECT_EQ(asks[0].second, 2);
}

TEST(MarketEngineCancelConsistencyRegression, CancelOwnerMismatchDoesNotMutateBookOrOpen) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 43021, lob::Side::Ask, 100, 2, lob::NONE, 1).accepted);
  const auto event_count_before = f.events.records().size();

  EXPECT_FALSE(f.engine.cancel(43021, f.bob, 2));

  EXPECT_TRUE(has_open_order(f.engine, 43021));
  auto asks = f.engine.topN(lob::Side::Ask, 10);
  EXPECT_FALSE(asks.empty());
  EXPECT_EQ(asks[0].second, 2);
  EXPECT_EQ_MSG(f.events.records().size(), event_count_before, "owner mismatch cancel must not append event");
}

TEST(MarketEngineCancelConsistencyRegression, CancelUnknownOrderDoesNotAppendEvent) {
  SpotEngineFixture f;
  const auto event_count_before = f.events.records().size();

  EXPECT_FALSE(f.engine.cancel(43999, f.alice, 2));

  EXPECT_EQ_MSG(f.events.records().size(), event_count_before, "unknown cancel must not append event");
}

TEST(MarketEngineCancelConsistencyRegression, CancelSuccessfulOrderReleasesExactlyLockedRemaining) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 43031, lob::Side::Ask, 100, 5, lob::NONE, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 43032, lob::Side::Bid, 100, 2, lob::IOC, 2).accepted);
  EXPECT_EQ(f.ledger.locked(f.alice, f.base_asset), 3);

  EXPECT_TRUE(f.engine.cancel(43031, f.alice, 3));

  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.base_asset), 0, f.ledger_summary(f.alice));
  EXPECT_EQ_MSG(f.ledger.balance(f.alice, f.base_asset).total, 1000000LL - 2LL, f.ledger_summary(f.alice));
}

TEST(MarketEngineCancelConsistencyRegression, CancelSuccessfulOrderRemovesOpenOrderAndBookLevel) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 43041, lob::Side::Ask, 100, 1, lob::NONE, 1).accepted);

  EXPECT_TRUE(f.engine.cancel(43041, f.alice, 2));

  EXPECT_FALSE(has_open_order(f.engine, 43041));
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10).empty());
}

TEST(MarketEngineCancelConsistencyRegression, CancelTwiceSecondCallNoMutationNoEvent) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 43051, lob::Side::Ask, 100, 1, lob::NONE, 1).accepted);
  EXPECT_TRUE(f.engine.cancel(43051, f.alice, 2));
  const auto event_count_before = f.events.records().size();

  EXPECT_FALSE(f.engine.cancel(43051, f.alice, 3));

  EXPECT_EQ_MSG(f.events.records().size(), event_count_before, "second cancel must not append event");
}

TEST(MarketEngineCancelConsistencyRegression, CancelTimestampUsesProvidedTimestamp) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 43061, lob::Side::Ask, 100, 1, lob::NONE, 1).accepted);

  EXPECT_TRUE(f.engine.cancel(43061, f.alice, 987654321));

  const auto* event = last_cancel_event(f.events);
  EXPECT_TRUE(event != nullptr);
  EXPECT_EQ(event->ts, 987654321);
}

TEST(MarketEngineCancelConsistencyRegression, CancelWithoutTimestampIsRejected) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 43071, lob::Side::Ask, 100, 1, lob::NONE, 1).accepted);
  const auto event_count_before = f.events.records().size();

  EXPECT_FALSE(f.engine.cancel(43071));

  EXPECT_EQ_MSG(f.events.records().size(), event_count_before, "cancel without timestamp must not append event");
  EXPECT_TRUE_MSG(has_open_order(f.engine, 43071), "cancel without timestamp must not mutate open orders");
}
