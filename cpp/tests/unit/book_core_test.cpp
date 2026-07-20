#include "lob/book_core.hpp"
#include "lob/price_levels.hpp"
#include "test_helpers/test_framework.hpp"

#include <vector>

namespace {

struct Fill {
  lob::Tick price;
  lob::Quantity qty;
  lob::OrderId passive_id;
  lob::OrderId taker_id;
};

struct Logger final : lob::IEventLogger {
  std::vector<Fill> fills;
  std::vector<lob::OrderId> cancels;
  void log_new(const lob::NewOrder&, bool, lob::Tick, lob::Timestamp) override {}
  void log_fill(lob::Tick px, lob::Quantity qty, lob::Side, lob::OrderId passive_id, lob::OrderId taker_id, lob::Timestamp) override {
    fills.push_back(Fill{px, qty, passive_id, taker_id});
  }
  void log_cancel(lob::OrderId id, lob::Timestamp) override { cancels.push_back(id); }
};

struct BookFixture {
  lob::PriceLevelsSparse bids;
  lob::PriceLevelsSparse asks;
  Logger logger;
  lob::BookCore book{bids, asks, &logger};

  lob::ExecResult submit(lob::OrderId id, lob::UserId user, lob::Side side, lob::Tick px, lob::Quantity qty, uint32_t flags = lob::NONE) {
    return book.submit_limit(lob::NewOrder{id, static_cast<lob::Timestamp>(id), id, user, side, px, qty, flags});
  }
};

} // namespace

TEST(BookCoreTest, BestBidAndAskAreOrdered) {
  BookFixture f;
  f.submit(1, 10, lob::Side::Bid, 99, 1);
  f.submit(2, 10, lob::Side::Bid, 101, 1);
  f.submit(3, 20, lob::Side::Ask, 103, 1);
  f.submit(4, 20, lob::Side::Ask, 102, 1);
  EXPECT_EQ(f.book.topN(lob::Side::Bid, 2)[0].first, 101);
  EXPECT_EQ(f.book.topN(lob::Side::Ask, 2)[0].first, 102);
}

TEST(BookCoreTest, SamePriceOrdersFillFifo) {
  BookFixture f;
  f.submit(1, 10, lob::Side::Ask, 100, 1);
  f.submit(2, 20, lob::Side::Ask, 100, 1);
  auto taker = f.submit(3, 30, lob::Side::Bid, 100, 2, lob::IOC);
  EXPECT_EQ(taker.filled, 2);
  EXPECT_EQ(f.logger.fills[0].passive_id, 1);
  EXPECT_EQ(f.logger.fills[1].passive_id, 2);
}

TEST(BookCoreTest, MultiLevelMatchUsesMakerPrices) {
  BookFixture f;
  f.submit(1, 10, lob::Side::Ask, 100, 1);
  f.submit(2, 20, lob::Side::Ask, 101, 2);
  auto taker = f.submit(3, 30, lob::Side::Bid, 105, 3, lob::IOC);
  EXPECT_EQ(taker.filled, 3);
  EXPECT_EQ(f.logger.fills[0].price, 100);
  EXPECT_EQ(f.logger.fills[1].price, 101);
}

TEST(BookCoreTest, PartialFillLeavesQuantityOnBook) {
  BookFixture f;
  f.submit(1, 10, lob::Side::Ask, 100, 5);
  auto taker = f.submit(2, 20, lob::Side::Bid, 100, 2, lob::IOC);
  EXPECT_EQ(taker.filled, 2);
  EXPECT_EQ(f.book.topN(lob::Side::Ask, 1)[0].second, 3);
}

TEST(BookCoreTest, FokAndIocDoNotRestRemainder) {
  BookFixture f;
  auto fok = f.submit(1, 10, lob::Side::Bid, 100, 5, lob::FOK);
  EXPECT_EQ(fok.filled, 0);
  EXPECT_TRUE(f.book.topN(lob::Side::Bid, 1).empty());

  auto ioc = f.submit(2, 10, lob::Side::Bid, 100, 5, lob::IOC);
  EXPECT_EQ(ioc.filled, 0);
  EXPECT_TRUE(f.book.topN(lob::Side::Bid, 1).empty());
}
