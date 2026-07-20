#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/random_order_generator.hpp"
#include "test_helpers/test_framework.hpp"

#include <sstream>
#include <vector>

using namespace lobx_test;

TEST(SpotBookPropertyTest, FixedSeedRandomSpotFlowMaintainsBookAndAccountingInvariants) {
  constexpr uint64_t seed = 2026060301ULL;
  auto f = ExchangeFixture::Spot();
  RandomOrderGenerator gen(seed);
  std::vector<std::string> sequence;

  for (int step = 0; step < 80; ++step) {
    auto op = gen.next_spot_operation(16000 + step);
    op.user = op.side == lob::Side::Bid ? f.bob : f.alice;
    sequence.push_back(op.describe());

    auto result = f.exchange.submit_limit(f.spot_symbol, op.user, op.order_id, op.side, op.price, op.qty, op.flags, step + 1);
    std::ostringstream detail;
    detail << "seed=" << seed << " step=" << step << " op=" << op.describe() << " reason=" << result.reason;
    EXPECT_NE_MSG(result.code, lobx::RejectCode::InternalError, detail.str());

    require_invariants(f.exchange);

    auto bids = f.exchange.topN(f.spot_symbol, lob::Side::Bid, 1);
    auto asks = f.exchange.topN(f.spot_symbol, lob::Side::Ask, 1);
    if (!bids.empty() && !asks.empty()) {
      EXPECT_TRUE_MSG(bids[0].first < asks[0].first, detail.str());
    }
  }
}
