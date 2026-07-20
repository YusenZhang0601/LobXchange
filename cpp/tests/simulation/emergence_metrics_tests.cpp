#include "lobx/simulation/emergence_metrics.hpp"

#include "test_helpers/test_framework.hpp"

#include <string>
#include <vector>

using namespace lobx::sim;

namespace {

std::vector<MarketTickSample> samples() {
  return {MarketTickSample{1, 100, 99, 101, 10, 10, 30, 30, 1, 100},
          MarketTickSample{2, 102, 101, 103, 10, 10, 30, 30, 2, 204},
          MarketTickSample{3, 101, 100, 102, 1, 1, 5, 5, 0, 0},
          MarketTickSample{4, 105, 102, 112, 10, 10, 30, 30, 3, 315},
          MarketTickSample{5, 99, 98, 100, 10, 10, 30, 30, 1, 99}};
}

} // namespace

TEST(EmergenceMetricsTests, MetricsRecordsMidPriceSeries) {
  const EmergenceMetrics metrics = summarize_market_samples(0, samples());

  EXPECT_EQ(metrics.samples.size(), 5UL);
  EXPECT_EQ(metrics.samples.front().mid_price, 100);
}

TEST(EmergenceMetricsTests, MetricsComputesMeanSpread) {
  const EmergenceMetrics metrics = summarize_market_samples(0, samples());

  EXPECT_TRUE(metrics.mean_spread > 3.0L);
}

TEST(EmergenceMetricsTests, MetricsComputesMedianSpread) {
  const EmergenceMetrics metrics = summarize_market_samples(0, samples());

  EXPECT_EQ(static_cast<int>(metrics.median_spread), 2);
}

TEST(EmergenceMetricsTests, MetricsComputesRealizedVolatility) {
  const EmergenceMetrics metrics = summarize_market_samples(0, samples());

  EXPECT_TRUE(metrics.realized_volatility > 0.0L);
}

TEST(EmergenceMetricsTests, MetricsComputesReturnAutocorrelation) {
  const EmergenceMetrics metrics = summarize_market_samples(0, samples());

  EXPECT_TRUE(metrics.return_autocorrelation <= 1.0L);
  EXPECT_TRUE(metrics.return_autocorrelation >= -1.0L);
}

TEST(EmergenceMetricsTests, MetricsComputesMaxDrawdown) {
  const EmergenceMetrics metrics = summarize_market_samples(0, samples());

  EXPECT_TRUE(metrics.max_drawdown > 0.0L);
}

TEST(EmergenceMetricsTests, MetricsDetectsLiquidityCrash) {
  const EmergenceMetrics metrics = summarize_market_samples(0, samples());

  EXPECT_TRUE(metrics.liquidity_crashes >= 1);
}

TEST(EmergenceMetricsTests, MetricsDetectsSpreadSpike) {
  const EmergenceMetrics metrics = summarize_market_samples(0, samples());

  EXPECT_TRUE(metrics.spread_spikes >= 1);
}

TEST(EmergenceMetricsTests, MetricsExcludesWarmupTicks) {
  const EmergenceMetrics metrics = summarize_market_samples(2, samples());

  EXPECT_EQ(metrics.measured_ticks, 3);
  EXPECT_EQ(metrics.samples.front().tick, 3);
}

TEST(EmergenceMetricsTests, MetricsExportIsDeterministic) {
  const EmergenceMetrics metrics = summarize_market_samples(0, samples());

  EXPECT_EQ(export_emergence_summary_json(metrics), export_emergence_summary_json(metrics));
  EXPECT_TRUE(export_price_series_csv(metrics).find("tick,mid_price") != std::string::npos);
}
