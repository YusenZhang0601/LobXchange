#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

struct PerpStateSnapshot {
  lobx::WalletBalance balance;
  lobx::Position position;
  std::vector<std::pair<lob::Tick, lob::Quantity>> bids;
  std::vector<std::pair<lob::Tick, lob::Quantity>> asks;
  size_t open_orders{0};
  size_t events{0};
};

void expect_ok(const lobx::SubmitResult& result) {
  EXPECT_TRUE_MSG(result.accepted, result.reason);
}

void rest_ask(PerpEngineFixture& f, lobx::UserId user, lobx::OrderId id, lob::Tick price, lob::Quantity qty) {
  expect_ok(f.submit(user, id, lob::Side::Ask, price, qty, lob::POST_ONLY, static_cast<lob::Timestamp>(id)));
}

void rest_bid(PerpEngineFixture& f, lobx::UserId user, lobx::OrderId id, lob::Tick price, lob::Quantity qty) {
  expect_ok(f.submit(user, id, lob::Side::Bid, price, qty, lob::POST_ONLY, static_cast<lob::Timestamp>(id)));
}

void open_long(PerpEngineFixture& f, lob::Tick price, lob::Quantity qty, lobx::OrderId base_id) {
  rest_ask(f, f.bob, base_id, price, qty);
  expect_ok(f.submit(f.alice, base_id + 1, lob::Side::Bid, price, qty, lob::IOC, static_cast<lob::Timestamp>(base_id + 1)));
}

void open_short(PerpEngineFixture& f, lob::Tick price, lob::Quantity qty, lobx::OrderId base_id) {
  rest_bid(f, f.bob, base_id, price, qty);
  expect_ok(f.submit(f.alice, base_id + 1, lob::Side::Ask, price, qty, lob::IOC, static_cast<lob::Timestamp>(base_id + 1)));
}

PerpStateSnapshot snapshot_user_state(PerpEngineFixture& f, lobx::UserId user) {
  return PerpStateSnapshot{f.ledger.balance(user, f.margin_asset),
                           f.positions.position(user, f.market.id),
                           f.engine.topN(lob::Side::Bid, 10),
                           f.engine.topN(lob::Side::Ask, 10),
                           f.engine.open_orders().size(),
                           f.events.records().size()};
}

void expect_state_unchanged(PerpEngineFixture& f, lobx::UserId user, const PerpStateSnapshot& before) {
  const auto balance = f.ledger.balance(user, f.margin_asset);
  const auto position = f.positions.position(user, f.market.id);
  EXPECT_EQ(balance.total, before.balance.total);
  EXPECT_EQ(balance.free, before.balance.free);
  EXPECT_EQ(balance.locked, before.balance.locked);
  EXPECT_EQ(position.signed_qty, before.position.signed_qty);
  EXPECT_EQ(position.entry_price, before.position.entry_price);
  EXPECT_EQ(position.realized_pnl, before.position.realized_pnl);
  EXPECT_TRUE(f.engine.topN(lob::Side::Bid, 10) == before.bids);
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10) == before.asks);
  EXPECT_EQ(f.engine.open_orders().size(), before.open_orders);
  EXPECT_EQ(f.events.records().size(), before.events);
}

lob::Quantity filled_qty(const lobx::SubmitResult& result) {
  lob::Quantity qty = 0;
  for (const auto& trade : result.trades) qty += trade.qty;
  return qty;
}

lobx::Amount filled_notional(const lobx::SubmitResult& result) {
  lobx::Amount notional = 0;
  for (const auto& trade : result.trades) notional += trade.price * trade.qty;
  return notional;
}

} // namespace

TEST(PerpSimulateFill, SIMF_PERP_001LongOpenDryRunEstimatesPositionAndMargin) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 81001, 100, 2);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 100, 2, lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.estimated_filled_qty, 2);
  EXPECT_EQ(sim.estimated_notional, 200);
  EXPECT_EQ(sim.estimated_required_margin, 40);
  EXPECT_EQ(sim.position_qty_before, 0);
  EXPECT_EQ(sim.position_qty_after, 2);
  EXPECT_EQ(sim.entry_price_after, 100);
}

TEST(PerpSimulateFill, SIMF_PERP_002ShortOpenDryRunEstimatesPositionAndMargin) {
  PerpEngineFixture f;
  rest_bid(f, f.bob, 81011, 100, 3);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Ask, 100, 3, lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.estimated_filled_qty, 3);
  EXPECT_EQ(sim.estimated_notional, 300);
  EXPECT_EQ(sim.estimated_required_margin, 60);
  EXPECT_EQ(sim.position_qty_after, -3);
  EXPECT_EQ(sim.entry_price_after, 100);
}

TEST(PerpSimulateFill, SIMF_PERP_003LongAddDryRunAveragesEntry) {
  PerpEngineFixture f;
  open_long(f, 100, 2, 81020);
  rest_ask(f, f.carol, 81022, 110, 2);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 110, 2, lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.position_qty_before, 2);
  EXPECT_EQ(sim.position_qty_after, 4);
  EXPECT_EQ(sim.entry_price_before, 100);
  EXPECT_EQ(sim.entry_price_after, 105);
  EXPECT_EQ(sim.estimated_required_margin, 44);
}

TEST(PerpSimulateFill, SIMF_PERP_004ShortAddDryRunAveragesEntry) {
  PerpEngineFixture f;
  open_short(f, 100, 2, 81030);
  rest_bid(f, f.carol, 81032, 90, 2);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Ask, 90, 2, lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.position_qty_before, -2);
  EXPECT_EQ(sim.position_qty_after, -4);
  EXPECT_EQ(sim.entry_price_before, 100);
  EXPECT_EQ(sim.entry_price_after, 95);
  EXPECT_EQ(sim.estimated_required_margin, 36);
}

TEST(PerpSimulateFill, SIMF_PERP_005LongPartialCloseDryRunRealizesPnlAndReleasesMargin) {
  PerpEngineFixture f;
  open_long(f, 100, 4, 81040);
  rest_bid(f, f.carol, 81042, 110, 2);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Ask, 110, 2, lobx::LOBX_REDUCE_ONLY | lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.position_qty_after, 2);
  EXPECT_EQ(sim.entry_price_after, 100);
  EXPECT_EQ(sim.estimated_realized_pnl, 20);
  EXPECT_EQ(sim.margin_delta, -40);
  EXPECT_EQ(sim.wallet_delta, 20);
}

TEST(PerpSimulateFill, SIMF_PERP_006ShortPartialCloseDryRunRealizesPnlAndReleasesMargin) {
  PerpEngineFixture f;
  open_short(f, 100, 4, 81050);
  rest_ask(f, f.carol, 81052, 90, 2);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 90, 2, lobx::LOBX_REDUCE_ONLY | lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.position_qty_after, -2);
  EXPECT_EQ(sim.entry_price_after, 100);
  EXPECT_EQ(sim.estimated_realized_pnl, 20);
  EXPECT_EQ(sim.margin_delta, -40);
  EXPECT_EQ(sim.wallet_delta, 20);
}

TEST(PerpSimulateFill, SIMF_PERP_007ReduceOnlyCloseHasNoRequiredOpenMargin) {
  PerpEngineFixture f;
  open_long(f, 100, 2, 81060);
  rest_bid(f, f.carol, 81062, 105, 1);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Ask, 105, 1, lobx::LOBX_REDUCE_ONLY | lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.estimated_required_margin, 0);
  EXPECT_EQ(sim.position_qty_after, 1);
  EXPECT_EQ(sim.estimated_realized_pnl, 5);
}

TEST(PerpSimulateFill, SIMF_PERP_008ReduceOnlyIncreaseRejects) {
  PerpEngineFixture f;
  open_long(f, 100, 2, 81070);
  rest_ask(f, f.carol, 81072, 100, 1);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 100, 1, lobx::LOBX_REDUCE_ONLY | lob::IOC);

  EXPECT_FALSE(sim.would_accept);
  EXPECT_EQ(sim.code, lobx::RejectCode::ReduceOnlyWouldIncrease);
}

TEST(PerpSimulateFill, SIMF_PERP_009InsufficientMarginRejects) {
  PerpEngineFixture f(1000);
  rest_ask(f, f.bob, 81081, 100, 1);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 100, 100, lob::IOC);

  EXPECT_FALSE(sim.would_accept);
  EXPECT_EQ(sim.code, lobx::RejectCode::InsufficientBalance);
}

TEST(PerpSimulateFill, SIMF_PERP_010PostOnlyWouldCrossRejects) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 81091, 100, 1);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 100, 1, lob::POST_ONLY);

  EXPECT_FALSE(sim.would_accept);
  EXPECT_EQ(sim.code, lobx::RejectCode::PostOnlyWouldCross);
  EXPECT_TRUE(sim.crosses);
}

TEST(PerpSimulateFill, SIMF_PERP_011IocPartialFillHasNoResidualRest) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 81101, 100, 1);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 100, 3, lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.estimated_filled_qty, 1);
  EXPECT_FALSE(sim.would_rest);
}

TEST(PerpSimulateFill, SIMF_PERP_012FokPartialFillRejectsWithoutEstimatedFills) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 81111, 100, 1);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 100, 2, lob::FOK);

  EXPECT_FALSE(sim.would_accept);
  EXPECT_EQ(sim.estimated_filled_qty, 0);
  EXPECT_TRUE(sim.fills.empty());
}

TEST(PerpSimulateFill, SIMF_PERP_013DryRunDoesNotMutateWalletPositionBookOrEvents) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 81121, 100, 2);
  const auto alice_balance_before = f.ledger.balance(f.alice, f.margin_asset);
  const auto bob_balance_before = f.ledger.balance(f.bob, f.margin_asset);
  const auto alice_position_before = f.positions.position(f.alice, f.market.id);
  const auto asks_before = f.engine.topN(lob::Side::Ask, 10);
  const auto bids_before = f.engine.topN(lob::Side::Bid, 10);
  const auto open_count_before = f.engine.open_orders().size();
  const auto events_before = f.events.records().size();

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 100, 1, lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, alice_balance_before.total);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).free, alice_balance_before.free);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).locked, bob_balance_before.locked);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, alice_position_before.signed_qty);
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10) == asks_before);
  EXPECT_TRUE(f.engine.topN(lob::Side::Bid, 10) == bids_before);
  EXPECT_EQ(f.engine.open_orders().size(), open_count_before);
  EXPECT_EQ(f.events.records().size(), events_before);
}

TEST(PerpSimulateFill, SIMF_PERP_014FeeEstimateAffectsWalletDelta) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{0, 100, 0}).ok);
  rest_ask(f, f.bob, 81131, 10000, 2);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 10000, 2, lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.estimated_fee, 200);
  EXPECT_EQ(sim.estimated_taker_fee, 200);
  EXPECT_EQ(sim.wallet_delta, -200);
}

TEST(PerpSimulateFill, SIMF_PERP_015UnrealizedPnlAfterUsesMarkPrice) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_index_price(110).ok);
  EXPECT_TRUE(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice).ok);
  rest_ask(f, f.bob, 81141, 100, 2);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 100, 2, lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.estimated_unrealized_pnl_after, 20);
}

TEST(PerpSimulateFill, SIMF_PERP_016ExchangeApiReturnsPerpDryRunEstimates) {
  ExchangeFixture f = ExchangeFixture::Perp();
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, 81151, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1));

  const auto sim = f.exchange.simulate_fill(f.perp_symbol, f.alice, lob::Side::Bid, 100, 2, lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.estimated_filled_qty, 2);
  EXPECT_EQ(sim.position_qty_after, 2);
  EXPECT_EQ(sim.entry_price_after, 100);
}

TEST(PerpSimulateFill, SIMF_PERP_017SimulatedFillListPreservesPriceTimeSweep) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 81161, 100, 1);
  rest_ask(f, f.carol, 81162, 101, 2);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 101, 3, lob::IOC);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.fills.size(), 2UL);
  EXPECT_EQ(sim.fills[0].maker_account, f.bob);
  EXPECT_EQ(sim.fills[0].maker_order_id, 81161);
  EXPECT_EQ(sim.fills[0].price, 100);
  EXPECT_EQ(sim.fills[1].maker_account, f.carol);
  EXPECT_EQ(sim.fills[1].maker_order_id, 81162);
  EXPECT_EQ(sim.fills[1].price, 101);
  EXPECT_EQ(sim.levels_consumed, 2);
}

TEST(PerpSimulateFill, SIMF_PERP_018SimulateFokRejectsPartialFill) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 81171, 100, 1);
  const auto before = snapshot_user_state(f, f.alice);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 100, 2, lob::FOK);

  EXPECT_FALSE(sim.would_accept);
  EXPECT_EQ(sim.code, lobx::RejectCode::InvalidQuantity);
  EXPECT_TRUE(sim.reason.find("FOK") != std::string::npos);
  EXPECT_EQ(sim.estimated_filled_qty, 0);
  EXPECT_EQ(sim.estimated_notional, 0);
  EXPECT_TRUE(sim.fills.empty());
  expect_state_unchanged(f, f.alice, before);
}

TEST(PerpSimulateFill, SIMF_PERP_019SimulateStpPreventsSelfTrade) {
  PerpEngineFixture f;
  rest_ask(f, f.alice, 81181, 100, 2);
  const auto before = snapshot_user_state(f, f.alice);

  const auto sim = f.engine.simulate_fill(f.alice, lob::Side::Bid, 100, 2, lob::IOC | lob::STP);

  EXPECT_TRUE(sim.would_accept);
  EXPECT_EQ(sim.estimated_filled_qty, 0);
  EXPECT_EQ(sim.estimated_notional, 0);
  EXPECT_EQ(sim.self_liquidity_skipped, 2);
  EXPECT_FALSE(sim.crosses);
  EXPECT_FALSE(sim.would_rest);
  EXPECT_TRUE(sim.fills.empty());
  EXPECT_EQ(sim.code, lobx::RejectCode::None);
  expect_state_unchanged(f, f.alice, before);
}

TEST(PerpSimulateFill, SIMF_PERP_020SimulatedFeeMatchesRealSubmit) {
  PerpEngineFixture sim_f;
  PerpEngineFixture real_f;
  EXPECT_TRUE(sim_f.engine.set_fee_config(lobx::PerpFeeConfig{0, 100, 0}).ok);
  EXPECT_TRUE(real_f.engine.set_fee_config(lobx::PerpFeeConfig{0, 100, 0}).ok);
  rest_ask(sim_f, sim_f.bob, 81191, 10000, 2);
  rest_ask(real_f, real_f.bob, 81191, 10000, 2);
  const auto sim_before = snapshot_user_state(sim_f, sim_f.alice);
  const auto real_balance_before = real_f.ledger.balance(real_f.alice, real_f.margin_asset);
  const lobx::Amount real_fee_before = real_f.engine.account_fee_total(real_f.alice);

  const auto sim = sim_f.engine.simulate_fill(sim_f.alice, lob::Side::Bid, 10000, 2, lob::IOC);
  const auto submitted = real_f.submit(real_f.alice, 81192, lob::Side::Bid, 10000, 2, lob::IOC, 81192);

  EXPECT_TRUE(sim.would_accept);
  expect_ok(submitted);
  EXPECT_EQ(sim.estimated_fee, real_f.engine.account_fee_total(real_f.alice) - real_fee_before);
  EXPECT_EQ(sim.wallet_delta, real_f.ledger.balance(real_f.alice, real_f.margin_asset).total - real_balance_before.total);
  expect_state_unchanged(sim_f, sim_f.alice, sim_before);
}

TEST(PerpSimulateFill, SIMF_PERP_021SimulatedPositionAfterMatchesRealSubmit) {
  PerpEngineFixture sim_f;
  PerpEngineFixture real_f;
  open_long(sim_f, 100, 2, 81200);
  open_long(real_f, 100, 2, 81200);
  rest_ask(sim_f, sim_f.carol, 81202, 110, 2);
  rest_ask(real_f, real_f.carol, 81202, 110, 2);
  const auto sim_before = snapshot_user_state(sim_f, sim_f.alice);
  const auto real_balance_before = real_f.ledger.balance(real_f.alice, real_f.margin_asset);

  const auto sim = sim_f.engine.simulate_fill(sim_f.alice, lob::Side::Bid, 110, 2, lob::IOC);
  const auto submitted = real_f.submit(real_f.alice, 81203, lob::Side::Bid, 110, 2, lob::IOC, 81203);
  const auto real_position = real_f.positions.position(real_f.alice, real_f.market.id);
  const auto real_balance_after = real_f.ledger.balance(real_f.alice, real_f.margin_asset);

  EXPECT_TRUE(sim.would_accept);
  expect_ok(submitted);
  EXPECT_EQ(sim.position_qty_after, real_position.signed_qty);
  EXPECT_EQ(sim.entry_price_after, real_position.entry_price);
  EXPECT_EQ(sim.margin_delta, real_balance_after.locked - real_balance_before.locked);
  expect_state_unchanged(sim_f, sim_f.alice, sim_before);
}

TEST(PerpSimulateFill, SIMF_PERP_022SimulatedRealizedPnlMatchesRealSubmit) {
  PerpEngineFixture sim_f;
  PerpEngineFixture real_f;
  open_long(sim_f, 100, 4, 81210);
  open_long(real_f, 100, 4, 81210);
  rest_bid(sim_f, sim_f.carol, 81212, 110, 2);
  rest_bid(real_f, real_f.carol, 81212, 110, 2);
  const auto sim_before = snapshot_user_state(sim_f, sim_f.alice);
  const auto real_position_before = real_f.positions.position(real_f.alice, real_f.market.id);
  const auto real_balance_before = real_f.ledger.balance(real_f.alice, real_f.margin_asset);

  const auto sim = sim_f.engine.simulate_fill(sim_f.alice, lob::Side::Ask, 110, 2, lobx::LOBX_REDUCE_ONLY | lob::IOC);
  const auto submitted = real_f.submit(real_f.alice, 81213, lob::Side::Ask, 110, 2, lobx::LOBX_REDUCE_ONLY | lob::IOC, 81213);
  const auto real_position_after = real_f.positions.position(real_f.alice, real_f.market.id);
  const auto real_balance_after = real_f.ledger.balance(real_f.alice, real_f.margin_asset);

  EXPECT_TRUE(sim.would_accept);
  expect_ok(submitted);
  EXPECT_EQ(sim.estimated_realized_pnl, real_position_after.realized_pnl - real_position_before.realized_pnl);
  EXPECT_EQ(sim.position_qty_after, real_position_after.signed_qty);
  EXPECT_EQ(sim.entry_price_after, real_position_after.entry_price);
  EXPECT_EQ(sim.wallet_delta, real_balance_after.total - real_balance_before.total);
  EXPECT_EQ(sim.margin_delta, real_balance_after.locked - real_balance_before.locked);
  expect_state_unchanged(sim_f, sim_f.alice, sim_before);
}

TEST(PerpSimulateFill, SIMF_PERP_023SimulatedFillQtyAndNotionalMatchRealSubmit) {
  PerpEngineFixture sim_f;
  PerpEngineFixture real_f;
  rest_ask(sim_f, sim_f.bob, 81221, 100, 1);
  rest_ask(sim_f, sim_f.carol, 81222, 101, 2);
  rest_ask(real_f, real_f.bob, 81221, 100, 1);
  rest_ask(real_f, real_f.carol, 81222, 101, 2);
  const auto sim_before = snapshot_user_state(sim_f, sim_f.alice);

  const auto sim = sim_f.engine.simulate_fill(sim_f.alice, lob::Side::Bid, 101, 3, lob::IOC);
  const auto submitted = real_f.submit(real_f.alice, 81223, lob::Side::Bid, 101, 3, lob::IOC, 81223);

  EXPECT_TRUE(sim.would_accept);
  expect_ok(submitted);
  EXPECT_EQ(sim.estimated_filled_qty, filled_qty(submitted));
  EXPECT_EQ(sim.estimated_notional, filled_notional(submitted));
  EXPECT_EQ(sim.fills.size(), submitted.trades.size());
  expect_state_unchanged(sim_f, sim_f.alice, sim_before);
}
