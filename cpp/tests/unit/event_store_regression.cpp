#include "lobx/event_store.hpp"
#include "test_helpers/test_framework.hpp"

TEST(EventStoreRegression, NextSeqDoesNotAdvanceSequence) {
  lobx::EventStore events;

  const auto first = events.next_seq();
  const auto second = events.next_seq();

  EXPECT_EQ(first, 1ULL);
  EXPECT_EQ(second, first);
  EXPECT_TRUE(events.records().empty());
}

TEST(EventStoreRegression, AppendAssignsStrictlyIncreasingSeq) {
  lobx::EventStore events;

  events.append(10, "one", "payload=1");
  events.append(11, "two", "payload=2");

  EXPECT_EQ(events.records().size(), 2UL);
  EXPECT_EQ(events.records()[0].seq, 1ULL);
  EXPECT_EQ(events.records()[1].seq, 2ULL);
  EXPECT_TRUE(events.records()[0].seq < events.records()[1].seq);
  EXPECT_EQ(events.next_seq(), 3ULL);
}

TEST(EventStoreRegression, AppendPreservesTimestampTypePayload) {
  lobx::EventStore events;

  events.append(123, "order.accepted", "market=BTC-USDT,order=1,user=10,price=100,qty=1");

  EXPECT_EQ(events.records().size(), 1UL);
  EXPECT_EQ(events.records()[0].ts, 123);
  EXPECT_EQ(events.records()[0].type, std::string("order.accepted"));
  EXPECT_EQ(events.records()[0].payload, std::string("market=BTC-USDT,order=1,user=10,price=100,qty=1"));
}
