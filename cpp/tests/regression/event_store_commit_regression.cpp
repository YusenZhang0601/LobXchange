#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

const lobx::EventRecord* last_event_of_type(const lobx::EventStore& events, const std::string& type) {
  const lobx::EventRecord* found = nullptr;
  for (const auto& event : events.records()) {
    if (event.type == type) found = &event;
  }
  return found;
}

void set_buyer_quote(SpotEngineFixture& f, lobx::Amount target_free) {
  const auto current = f.ledger.balance(f.bob, f.quote_asset).free;
  if (current > target_free) {
    auto withdraw = f.ledger.withdraw(f.bob, f.quote_asset, current - target_free);
    EXPECT_TRUE_MSG(withdraw.ok, "setup set buyer quote reason=" + withdraw.reason);
  }
}

} // namespace

TEST(EventStoreCommitRegression, CancelEventUsesProvidedTimestamp) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 34001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 77).accepted);

  EXPECT_TRUE(f.engine.cancel(34001, f.alice, 88));
  const auto* cancel = last_event_of_type(f.events, "order.canceled");
  EXPECT_TRUE_MSG(cancel != nullptr, "setup expected order.canceled event");
  EXPECT_EQ_MSG(cancel->ts, 88, "cancel event timestamp should use provided business timestamp");
}

TEST(EventStoreCommitRegression, CancelWithoutTimestampIsRejected) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 34002, lob::Side::Ask, 100, 1, lob::POST_ONLY, 77).accepted);
  const auto event_count_before = f.events.records().size();

  EXPECT_FALSE(f.engine.cancel(34002));

  EXPECT_EQ_MSG(f.events.records().size(), event_count_before, "cancel without timestamp must not append event");
  EXPECT_FALSE_MSG(f.engine.topN(lob::Side::Ask, 10).empty(), "cancel without timestamp must not remove resting order");
}

TEST(EventStoreCommitRegression, FailedSettlementDoesNotLeaveFinalAcceptedEvent) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_buyer_quote(f, 100);
  EXPECT_TRUE(f.submit(f.alice, 34011, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto event_count_before = f.events.records().size();

  auto buy = f.submit(f.bob, 34012, lob::Side::Bid, 100, 1, lob::IOC, 2);
  EXPECT_FALSE(buy.accepted);

  EXPECT_EQ_MSG(f.events.records().size(), event_count_before + 1,
                "failed settlement should append exactly one terminal rejection event");
  EXPECT_EQ_MSG(f.events.records().back().type, std::string("order.rejected"),
                "failed settlement must not leave order.accepted as the final state");
}

TEST(EventStoreCommitRegression, RejectedOrderProducesRejectedEvent) {
  SpotEngineFixture f;
  auto bad = f.submit(f.alice, 34021, lob::Side::Ask, 100, 1, 1u << 30, 1);
  EXPECT_FALSE(bad.accepted);

  const auto* rejected = last_event_of_type(f.events, "order.rejected");
  EXPECT_TRUE_MSG(rejected != nullptr, "rejected orders should produce order.rejected event");
}
