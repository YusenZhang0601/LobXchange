#pragma once

#include <string>
#include <vector>

#include "lobx/simulation/research_runner.hpp"
#include "lobx/simulation/strategy_metrics.hpp"

namespace lobx::sim {

struct MarketTickSample {
  int tick{0};
  lob::Tick mid_price{0};
  lob::Tick best_bid{0};
  lob::Tick best_ask{0};
  lob::Quantity bid_depth_top1{0};
  lob::Quantity ask_depth_top1{0};
  lob::Quantity bid_depth_top5{0};
  lob::Quantity ask_depth_top5{0};
  lob::Quantity trade_volume{0};
  Amount quote_volume{0};
};

struct EmergenceMetrics {
  int ticks{0};
  int warmup_ticks{0};
  int measured_ticks{0};

  int trade_count{0};
  long double total_volume{0.0L};
  long double total_quote_volume{0.0L};

  long double mean_spread{0.0L};
  long double median_spread{0.0L};

  long double mean_depth_top1{0.0L};
  long double mean_depth_top5{0.0L};

  long double realized_volatility{0.0L};
  long double return_autocorrelation{0.0L};

  long double max_drawdown{0.0L};
  long double max_price_impact{0.0L};
  long double average_slippage{0.0L};

  int liquidity_crashes{0};
  int spread_spikes{0};
  int volatility_clusters{0};

  std::vector<MarketTickSample> samples;
  std::vector<StrategyMetrics> agent_metrics;
};

class EmergenceMetricsCollector {
public:
  explicit EmergenceMetricsCollector(int warmup_ticks);

  void record_tick(int tick, const ResearchRunResult& partial_or_final_state);

  EmergenceMetrics finalize() const;

private:
  int warmup_ticks_{0};
  std::vector<MarketTickSample> samples_;
  std::vector<StrategyMetrics> agent_metrics_;
};

EmergenceMetrics summarize_market_samples(int warmup_ticks,
                                          const std::vector<MarketTickSample>& samples,
                                          const std::vector<StrategyMetrics>& agent_metrics = {});

std::string export_emergence_summary_json(const EmergenceMetrics& metrics);
std::string export_price_series_csv(const EmergenceMetrics& metrics);
std::string export_spread_series_csv(const EmergenceMetrics& metrics);
std::string export_depth_series_csv(const EmergenceMetrics& metrics);
std::string export_agent_metrics_csv(const EmergenceMetrics& metrics);

} // namespace lobx::sim
