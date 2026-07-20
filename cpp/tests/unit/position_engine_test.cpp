#include "lobx/position_engine.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

TEST(PositionEngineTest, OpensLongAndShort) {
  lobx::PositionEngine positions;
  positions.apply_trade(10, 1, lob::Side::Bid, 100, 5);
  positions.apply_trade(20, 1, lob::Side::Ask, 100, 5);
  EXPECT_EQ(positions.position(10, 1).signed_qty, 5);
  EXPECT_EQ(positions.position(20, 1).signed_qty, -5);
}

TEST(PositionEngineTest, AddingPositionUpdatesAverageEntry) {
  lobx::PositionEngine positions;
  positions.apply_trade(10, 1, lob::Side::Bid, 100, 5);
  positions.apply_trade(10, 1, lob::Side::Bid, 120, 5);
  EXPECT_EQ(positions.position(10, 1).signed_qty, 10);
  EXPECT_EQ(positions.position(10, 1).entry_price, 110);
}

TEST(PositionEngineTest, PartialAndFullCloseRealizePnl) {
  lobx::PositionEngine positions;
  positions.apply_trade(10, 1, lob::Side::Bid, 100, 10);
  positions.apply_trade(10, 1, lob::Side::Ask, 110, 4);
  EXPECT_EQ(positions.position(10, 1).signed_qty, 6);
  EXPECT_EQ(positions.position(10, 1).realized_pnl, 40);
  positions.apply_trade(10, 1, lob::Side::Ask, 90, 6);
  EXPECT_EQ(positions.position(10, 1).signed_qty, 0);
  EXPECT_EQ(positions.position(10, 1).entry_price, 0);
  EXPECT_EQ(positions.position(10, 1).realized_pnl, -20);
}

TEST(PositionEngineTest, ReduceOnlyCannotIncreaseOrFlipPosition) {
  lobx::PositionEngine positions;
  positions.apply_trade(10, 1, lob::Side::Bid, 100, 5);
  EXPECT_TRUE(positions.reduce_only_would_increase(10, 1, lob::Side::Bid, 1));
  EXPECT_TRUE(positions.reduce_only_would_increase(10, 1, lob::Side::Ask, 6));
  EXPECT_FALSE(positions.reduce_only_would_increase(10, 1, lob::Side::Ask, 5));
}

TEST(PositionEngineTest, RealizedPnlOverflowIsRejectedWithoutPositionMutation) {
  lobx::PositionEngine positions;
  EXPECT_TRUE(positions.apply_trade_checked(10, 1, lob::Side::Bid, 1, 2));
  const auto before = positions.position(10, 1);

  EXPECT_FALSE(positions.apply_trade_checked(10, 1, lob::Side::Ask,
                                             std::numeric_limits<lob::Tick>::max(), 2));

  const auto after = positions.position(10, 1);
  EXPECT_EQ(after.signed_qty, before.signed_qty);
  EXPECT_EQ(after.entry_price, before.entry_price);
  EXPECT_EQ(after.realized_pnl, before.realized_pnl);
}
