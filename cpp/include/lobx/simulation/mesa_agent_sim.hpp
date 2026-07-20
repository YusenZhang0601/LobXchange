#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lobx/types.hpp"

namespace lobx::sim {

struct MesaAgentCounts {
  int market_makers{4};
  int noise_traders{6};
  int momentum{2};
  int mean_reversion{2};
  int whale_sweepers{1};
};

struct MesaAgentSimConfig {
  uint64_t seed{42};
  int steps{80};
  std::string market_symbol{"BTC-USDT"};
  lob::Tick reference_price{100};
  std::vector<lob::Tick> initial_trade_prices;
  MesaAgentCounts agents{};
  UserId first_user_id{100};
  Amount initial_quote{1000000000LL};
  Amount initial_base{1000000LL};
  int book_levels{10};
  std::vector<int> candle_intervals{1, 5, 15, 60};
};

struct MesaStepCandle {
  int interval_steps{1};
  int open_step{1};
  int close_step{1};
  lob::Tick open{0};
  lob::Tick high{0};
  lob::Tick low{0};
  lob::Tick close{0};
  lob::Quantity volume{0};
  Amount quote_volume{0};
  int trade_count{0};
};

struct MesaStepStats {
  int step{0};
  lob::Tick last_price{0};
  lob::Tick best_bid{0};
  lob::Tick best_ask{0};
  lob::Tick spread{0};
  int accepted_orders{0};
  int rejected_orders{0};
  int trade_count{0};
  int agent_count{0};
  double mean_spread{0.0};
};

struct MesaOrderEvent {
  int step{0};
  UserId user{0};
  std::string agent_type;
  lob::Side side{lob::Side::Bid};
  lob::Tick price{0};
  lob::Quantity qty{0};
  uint32_t flags{lob::NONE};
  bool accepted{false};
  RejectCode code{RejectCode::None};
  int trade_count{0};
  lob::Quantity filled{0};
  lob::Quantity remaining{0};
};

struct MesaStepEvents {
  int step{0};
  std::vector<MesaOrderEvent> orders;
  std::vector<TradeEvent> trades;
  std::vector<MesaStepCandle> candles;
  MesaStepStats stats;
};

struct MesaAgentSimSummary {
  int steps{0};
  int agent_count{0};
  int accepted_orders{0};
  int rejected_orders{0};
  int trade_count{0};
  lob::Tick final_best_bid{0};
  lob::Tick final_best_ask{0};
  double final_mid_price{0.0};
  double mean_spread{0.0};
  std::map<std::string, int> agent_types;
};

class MesaAgentSimulation {
public:
  explicit MesaAgentSimulation(MesaAgentSimConfig config = {});
  ~MesaAgentSimulation();

  MesaAgentSimulation(MesaAgentSimulation&&) noexcept;
  MesaAgentSimulation& operator=(MesaAgentSimulation&&) noexcept;

  MesaAgentSimulation(const MesaAgentSimulation&) = delete;
  MesaAgentSimulation& operator=(const MesaAgentSimulation&) = delete;

  MesaStepEvents step();
  MesaAgentSimSummary run();

  MesaAgentSimSummary summary() const;
  MesaStepStats stats() const;
  const std::vector<TradeEvent>& trades() const;
  bool accounting_invariant_ok() const;
  std::vector<std::pair<lob::Tick, lob::Quantity>> bids(int levels = 10) const;
  std::vector<std::pair<lob::Tick, lob::Quantity>> asks(int levels = 10) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

std::string mesa_agent_summary_json(const MesaAgentSimSummary& summary, bool pretty = true);
std::string mesa_step_stats_json(const MesaStepStats& stats);
std::string mesa_agent_mix_json(const MesaAgentSimSummary& summary);
std::string mesa_trade_json(const TradeEvent& trade, int step);
std::string mesa_step_candle_json(const MesaStepCandle& candle);

} // namespace lobx::sim
