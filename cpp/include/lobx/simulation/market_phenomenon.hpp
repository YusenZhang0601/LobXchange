#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "lobx/types.hpp"

namespace lobx::sim {

enum class MarketPhenomenonType : uint8_t {
  RepeatedRangeSweep = 0,
  LongShortStopHunt = 1,
  WickSpike = 2,
  LiquidationCascade = 3,
  LiquidityVacuum = 4,
  FalseBreakout = 5,
  OrderBookImbalance = 6,
  StopRun = 7,
  SpreadWidening = 8,
  CrowdedTrade = 9,
  MeanReversionTrap = 10,
  MomentumIgnition = 11,
  QuoteStuffing = 12,
  SpoofingLikeBehavior = 13,
  ChoppyMeanReversion = 14,
};

struct PhenomenonTrade {
  lob::Timestamp ts{0};
  lob::Tick price{0};
  lob::Quantity qty{0};
  lob::Side aggressor_side{lob::Side::Bid};
};

struct PhenomenonEvent {
  lob::Timestamp ts{0};
  std::string type;
  std::string payload;
};

struct MarketObservation {
  lob::Timestamp ts{0};
  lob::Tick price{0};
  lob::Quantity qty{0};
  lob::Side aggressor_side{lob::Side::Bid};
  lob::Tick best_bid{0};
  lob::Tick best_ask{0};
  Amount bid_depth{0};
  Amount ask_depth{0};
  std::size_t submit_count{0};
  std::size_t cancel_count{0};
  std::size_t trade_count{0};
  std::size_t trigger_fired_count{0};
  std::size_t liquidation_count{0};
};

struct CrowdedPosition {
  UserId user{0};
  lob::Side side{lob::Side::Bid};
  lob::Tick entry_price{0};
  Amount qty{0};
  int leverage{1};
};

struct MarketPhenomenon {
  MarketPhenomenonType type{MarketPhenomenonType::RepeatedRangeSweep};
  lob::Timestamp start_ts{0};
  lob::Timestamp end_ts{0};
  lob::Tick low{0};
  lob::Tick high{0};
  lob::Tick open{0};
  lob::Tick close{0};
  Amount volume{0};
  std::size_t trade_count{0};
  std::size_t event_count{0};
  std::size_t trigger_count{0};
  std::size_t liquidation_count{0};
  Amount depth_before{0};
  Amount depth_after{0};
  Amount price_impact{0};
  long double score{0.0L};
  std::string explanation;
};

struct MarketPhenomenonConfig {
  lob::Tick range_threshold{1};
  Amount min_range_sweep_volume{4};
  std::size_t min_range_sweep_trades{6};
  long double wick_ratio_threshold{3.0L};
  lob::Tick min_stop_hunt_range{10};
  std::size_t min_liquidation_count{2};
  lob::Tick min_liquidity_vacuum_move{10};
  Amount max_liquidity_vacuum_volume{5};
  long double min_liquidity_vacuum_impact{4.0L};
  long double max_liquidity_vacuum_depth_ratio{0.35L};
  lob::Tick false_breakout_min_ticks{1};
  std::size_t false_breakout_max_return_steps{3};
  long double order_book_imbalance_threshold{0.80L};
  lob::Tick min_imbalance_follow_through{2};
  std::size_t min_stop_run_triggers{2};
  lob::Tick min_stop_run_follow_through{2};
  long double spread_widening_multiplier{3.0L};
  lob::Tick min_spread_widening_ticks{5};
  std::size_t crowded_trade_min_users{5};
  lob::Tick crowded_trade_max_entry_range{2};
  int crowded_trade_min_leverage{5};
  long double crowded_trade_min_score{1.0L};
};

std::vector<MarketPhenomenon> detect_market_phenomena(
    const std::vector<PhenomenonTrade>& trades,
    const std::vector<PhenomenonEvent>& events = {},
    MarketPhenomenonConfig config = {});

std::vector<MarketPhenomenon> detect_market_phenomena(
    const std::vector<MarketObservation>& observations,
    const std::vector<PhenomenonEvent>& events = {},
    const std::vector<CrowdedPosition>& positions = {},
    MarketPhenomenonConfig config = {});

std::vector<MarketPhenomenon> detect_market_phenomena(
    const std::vector<PhenomenonTrade>& trades,
    const std::vector<MarketObservation>& observations,
    const std::vector<PhenomenonEvent>& events = {},
    const std::vector<CrowdedPosition>& positions = {},
    MarketPhenomenonConfig config = {});

} // namespace lobx::sim
