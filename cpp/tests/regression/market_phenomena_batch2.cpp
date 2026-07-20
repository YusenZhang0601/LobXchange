#include "lobx/simulation/market_phenomenon.hpp"
#include "test_helpers/test_framework.hpp"

#include <string>
#include <vector>

using namespace lobx_test;

namespace {

using lobx::sim::CrowdedPosition;
using lobx::sim::MarketObservation;
using lobx::sim::MarketPhenomenon;
using lobx::sim::MarketPhenomenonType;
using lobx::sim::PhenomenonEvent;
using lobx::sim::PhenomenonTrade;

std::vector<PhenomenonTrade> trades(std::initializer_list<lob::Tick> prices,
                                    lob::Side side = lob::Side::Bid) {
  std::vector<PhenomenonTrade> out;
  int step = 1;
  for (const lob::Tick price : prices) {
    out.push_back(PhenomenonTrade{step, price, 1, side});
    ++step;
  }
  return out;
}

MarketObservation observation(lob::Timestamp ts,
                              lob::Tick price,
                              lob::Tick best_bid,
                              lob::Tick best_ask,
                              lobx::Amount bid_depth,
                              lobx::Amount ask_depth,
                              lob::Quantity qty = 1,
                              lob::Side side = lob::Side::Bid) {
  MarketObservation obs{};
  obs.ts = ts;
  obs.price = price;
  obs.qty = qty;
  obs.aggressor_side = side;
  obs.best_bid = best_bid;
  obs.best_ask = best_ask;
  obs.bid_depth = bid_depth;
  obs.ask_depth = ask_depth;
  obs.trade_count = qty > 0 ? 1U : 0U;
  return obs;
}

bool has_type(const std::vector<MarketPhenomenon>& phenomena, MarketPhenomenonType type) {
  for (const auto& phenomenon : phenomena) {
    if (phenomenon.type == type) return true;
  }
  return false;
}

const MarketPhenomenon* find_type(const std::vector<MarketPhenomenon>& phenomena,
                                  MarketPhenomenonType type) {
  for (const auto& phenomenon : phenomena) {
    if (phenomenon.type == type) return &phenomenon;
  }
  return nullptr;
}

std::string explanation_for(const std::vector<MarketPhenomenon>& phenomena,
                            MarketPhenomenonType type) {
  const MarketPhenomenon* phenomenon = find_type(phenomena, type);
  return phenomenon == nullptr ? std::string{} : phenomenon->explanation;
}

} // namespace

TEST(MarketPhenomenaBatch2, DetectsLiquidityVacuum) {
  const auto price_path = trades({100, 110, 130});
  const std::vector<MarketObservation> observations{
      observation(1, 100, 99, 100, 100, 3),
      observation(2, 110, 99, 110, 100, 1),
      observation(3, 130, 99, 130, 100, 0),
  };

  const auto result = lobx::sim::detect_market_phenomena(price_path, observations);
  const MarketPhenomenon* vacuum = find_type(result, MarketPhenomenonType::LiquidityVacuum);

  EXPECT_TRUE(vacuum != nullptr);
  EXPECT_EQ(vacuum->price_impact, 30);
  EXPECT_TRUE_MSG(vacuum->score >= 10.0L, vacuum->explanation);
  EXPECT_TRUE_MSG(vacuum->explanation.find("price impact") != std::string::npos, vacuum->explanation);
  EXPECT_TRUE_MSG(vacuum->explanation.find("levels swept") != std::string::npos, vacuum->explanation);
}

TEST(MarketPhenomenaBatch2, DetectsFalseBreakout) {
  lobx::sim::MarketPhenomenonConfig config;
  config.false_breakout_min_ticks = 2;
  const auto result = lobx::sim::detect_market_phenomena(
      trades({100, 104, 105, 107, 104, 103}), {}, config);

  EXPECT_TRUE(has_type(result, MarketPhenomenonType::FalseBreakout));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::WickSpike));
  const std::string explanation = explanation_for(result, MarketPhenomenonType::FalseBreakout);
  EXPECT_TRUE_MSG(explanation.find("breakout level=105") != std::string::npos, explanation);
  EXPECT_TRUE_MSG(explanation.find("return_step=") != std::string::npos, explanation);
}

TEST(MarketPhenomenaBatch2, DetectsOrderBookImbalanceBeforeMove) {
  const std::vector<MarketObservation> observations{
      observation(1, 100, 99, 101, 1000, 50),
      observation(2, 101, 100, 102, 900, 40),
      observation(3, 103, 102, 104, 700, 30),
  };

  const auto result = lobx::sim::detect_market_phenomena(observations);
  const MarketPhenomenon* imbalance = find_type(result, MarketPhenomenonType::OrderBookImbalance);

  EXPECT_TRUE(imbalance != nullptr);
  EXPECT_EQ(imbalance->price_impact, 3);
  EXPECT_TRUE_MSG(imbalance->score >= 0.8L, imbalance->explanation);
  EXPECT_TRUE_MSG(imbalance->explanation.find("imbalance ratio=") != std::string::npos,
                  imbalance->explanation);
}

TEST(MarketPhenomenaBatch2, DetectsStopRunFromTriggerCluster) {
  const auto price_path = trades({104, 105, 108, 110});
  const std::vector<PhenomenonEvent> events{
      PhenomenonEvent{2, "trigger.fired", "side=BID trigger=1"},
      PhenomenonEvent{3, "trigger.fired", "side=BID trigger=2"},
      PhenomenonEvent{4, "trigger.child_order", "side=BID child=3"},
  };

  const auto result = lobx::sim::detect_market_phenomena(price_path, events);
  const MarketPhenomenon* stop_run = find_type(result, MarketPhenomenonType::StopRun);

  EXPECT_TRUE(stop_run != nullptr);
  EXPECT_EQ(stop_run->trigger_count, 3UL);
  EXPECT_EQ(stop_run->price_impact, 6);
  EXPECT_TRUE_MSG(stop_run->explanation.find("trigger_count=") != std::string::npos,
                  stop_run->explanation);
}

TEST(MarketPhenomenaBatch2, DetectsSpreadWideningAfterMakerWithdrawal) {
  const std::vector<MarketObservation> observations{
      observation(1, 100, 99, 101, 500, 500, 0),
      observation(2, 100, 90, 110, 20, 20, 0),
  };

  const auto result = lobx::sim::detect_market_phenomena(observations);
  const MarketPhenomenon* widening = find_type(result, MarketPhenomenonType::SpreadWidening);

  EXPECT_TRUE(widening != nullptr);
  EXPECT_EQ(widening->price_impact, 18);
  EXPECT_EQ(widening->depth_before, 1000);
  EXPECT_EQ(widening->depth_after, 40);
  EXPECT_TRUE_MSG(widening->explanation.find("spread_before=2") != std::string::npos,
                  widening->explanation);
}

TEST(MarketPhenomenaBatch2, DetectsCrowdedLongTrade) {
  std::vector<CrowdedPosition> positions;
  for (lobx::UserId user = 100; user < 106; ++user) {
    positions.push_back(CrowdedPosition{user, lob::Side::Bid, user % 2 == 0 ? 100 : 101, 10, 10});
  }

  const auto result = lobx::sim::detect_market_phenomena(
      std::vector<MarketObservation>{}, std::vector<PhenomenonEvent>{}, positions);
  const MarketPhenomenon* crowded = find_type(result, MarketPhenomenonType::CrowdedTrade);

  EXPECT_TRUE(crowded != nullptr);
  EXPECT_EQ(crowded->volume, 60);
  EXPECT_TRUE_MSG(crowded->score >= 2.0L, crowded->explanation);
  EXPECT_TRUE_MSG(crowded->explanation.find("side=long") != std::string::npos,
                  crowded->explanation);
  EXPECT_TRUE_MSG(crowded->explanation.find("user_count=6") != std::string::npos,
                  crowded->explanation);
}

TEST(MarketPhenomenaBatch2, DoesNotFalsePositiveOnHealthyTrend) {
  const auto price_path = trades({100, 101, 102, 103, 104});
  const std::vector<MarketObservation> observations{
      observation(1, 100, 99, 101, 500, 500),
      observation(2, 101, 100, 102, 500, 500),
      observation(3, 102, 101, 103, 500, 500),
      observation(4, 103, 102, 104, 500, 500),
      observation(5, 104, 103, 105, 500, 500),
  };

  const auto result = lobx::sim::detect_market_phenomena(price_path, observations);

  EXPECT_FALSE(has_type(result, MarketPhenomenonType::LiquidityVacuum));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::FalseBreakout));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::OrderBookImbalance));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::StopRun));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::SpreadWidening));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::CrowdedTrade));
}

TEST(MarketPhenomenaBatch2, DoesNotFalsePositiveOnNormalRange) {
  const auto price_path = trades({100, 101, 100, 101});
  const std::vector<MarketObservation> observations{
      observation(1, 100, 99, 101, 250, 250),
      observation(2, 101, 100, 102, 250, 250),
      observation(3, 100, 99, 101, 250, 250, 1, lob::Side::Ask),
      observation(4, 101, 100, 102, 250, 250),
  };

  const auto result = lobx::sim::detect_market_phenomena(price_path, observations);

  EXPECT_FALSE(has_type(result, MarketPhenomenonType::LiquidityVacuum));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::FalseBreakout));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::OrderBookImbalance));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::StopRun));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::SpreadWidening));
  EXPECT_FALSE(has_type(result, MarketPhenomenonType::CrowdedTrade));
}
