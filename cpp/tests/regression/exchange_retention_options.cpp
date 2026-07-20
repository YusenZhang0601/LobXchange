#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

void expect_ok(const lobx::Result& result) {
  EXPECT_TRUE_MSG(result.ok, result.reason);
}

void expect_ok(const lobx::SubmitResult& result) {
  EXPECT_TRUE_MSG(result.accepted, result.reason);
}

lobx::RuntimeRetentionOptions fast_retention_options() {
  lobx::RuntimeRetentionOptions options{};
  options.record_events = false;
  options.build_event_payloads = false;
  options.record_trade_history = false;
  options.update_klines = false;
  options.record_candle_history = false;
  return options;
}

lobx::SubmitResult submit_spot_trade(ExchangeFixture& f, lobx::OrderId maker_id, lobx::OrderId taker_id,
                                     lob::Tick price = 100, lob::Quantity qty = 1) {
  expect_ok(f.exchange.submit_limit(f.spot_symbol, f.bob, maker_id, lob::Side::Ask,
                                    price, qty, lob::POST_ONLY, maker_id));
  return f.exchange.submit_limit(f.spot_symbol, f.alice, taker_id, lob::Side::Bid,
                                 price, qty, lob::IOC, taker_id);
}

lobx::TriggerOrder trigger_by_id(const ExchangeFixture& f, lobx::OrderId id) {
  for (const auto& trigger : f.exchange.trigger_orders(f.perp_symbol)) {
    if (trigger.request.trigger_order_id == id) return trigger;
  }
  return {};
}

} // namespace

TEST(ExchangeRetentionOptions, DefaultRetentionKeepsEventsTradesAndCandles) {
  auto f = ExchangeFixture::Spot();
  const auto events_before = f.exchange.events().records().size();

  auto trade = submit_spot_trade(f, 930001, 930002);
  auto candles = f.exchange.flush_candles();

  expect_ok(trade);
  EXPECT_EQ(trade.exec.filled, 1);
  EXPECT_TRUE(f.exchange.events().records().size() > events_before);
  EXPECT_EQ(f.exchange.trades().size(), 1UL);
  EXPECT_FALSE(candles.empty());
  EXPECT_FALSE(f.exchange.candles().empty());
}

TEST(ExchangeRetentionOptions, FastRetentionDisablesEventsTradesAndKlines) {
  auto f = ExchangeFixture::Spot();
  f.exchange.set_retention_options(fast_retention_options());
  const auto events_before = f.exchange.events().records().size();
  const auto candles_before = f.exchange.candles().size();

  auto trade = submit_spot_trade(f, 930011, 930012);
  auto candles = f.exchange.flush_candles();

  expect_ok(trade);
  EXPECT_EQ(trade.exec.filled, 1);
  EXPECT_EQ(f.exchange.events().records().size(), events_before);
  EXPECT_TRUE(f.exchange.trades().empty());
  EXPECT_TRUE(candles.empty());
  EXPECT_EQ(f.exchange.candles().size(), candles_before);
}

TEST(ExchangeRetentionOptions, FastRetentionDoesNotBreakLastPriceTrigger) {
  auto f = ExchangeFixture::Perp();
  f.exchange.set_retention_options(fast_retention_options());
  constexpr lobx::OrderId trigger_id = 930020;

  expect_ok(f.exchange.create_trigger_order(f.perp_symbol, f.alice, trigger_id,
                                            lob::Side::Bid, 1, 100,
                                            lobx::TriggerPriceType::Last,
                                            lobx::TriggerCondition::AboveOrEqual,
                                            lobx::TriggerChildOrderType::Market,
                                            0, 100, lob::NONE, 1));
  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Last, 2), 0);
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, 930021, lob::Side::Ask,
                                    100, 1, lob::POST_ONLY, 3));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.alice, 930022, lob::Side::Bid,
                                    100, 1, lob::IOC, 4));
  EXPECT_TRUE(f.exchange.trades().empty());
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, 930023, lob::Side::Ask,
                                    100, 1, lob::POST_ONLY, 5));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Last, 6), 1);

  EXPECT_EQ(trigger_by_id(f, trigger_id).status, lobx::TriggerOrderStatus::Triggered);
  EXPECT_EQ(trigger_by_id(f, trigger_id).child_order_id, trigger_id);
}

TEST(ExchangeRetentionOptions, FastRetentionDoesNotBreakPerpSettlement) {
  auto f = ExchangeFixture::Perp();
  f.exchange.set_retention_options(fast_retention_options());
  expect_ok(f.exchange.set_perp_fee_config(f.perp_symbol, lobx::PerpFeeConfig{10, 20, 0}));
  const auto alice_before = f.exchange.balance(f.alice, "USDT");
  const auto bob_before = f.exchange.balance(f.bob, "USDT");

  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, 930031, lob::Side::Ask,
                                    100, 10, lob::POST_ONLY, 1));
  auto fill = f.exchange.submit_limit(f.perp_symbol, f.alice, 930032, lob::Side::Bid,
                                      100, 10, lob::IOC, 2);

  expect_ok(fill);
  EXPECT_EQ(fill.exec.filled, 10);
  EXPECT_TRUE(f.exchange.trades().empty());
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 10);
  EXPECT_EQ(f.exchange.position(f.bob, f.perp_symbol).signed_qty, -10);
  EXPECT_EQ(f.exchange.account_fee_total(f.alice, f.perp_symbol), 2);
  EXPECT_EQ(f.exchange.account_fee_total(f.bob, f.perp_symbol), 1);
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").total, alice_before.total - 2);
  EXPECT_EQ(f.exchange.balance(f.bob, "USDT").total, bob_before.total - 1);
}

TEST(ExchangeRetentionOptions, CanReenableRetention) {
  auto f = ExchangeFixture::Spot();
  f.exchange.set_retention_options(fast_retention_options());
  const auto events_before = f.exchange.events().records().size();

  expect_ok(submit_spot_trade(f, 930041, 930042, 100, 1));
  EXPECT_EQ(f.exchange.events().records().size(), events_before);
  EXPECT_TRUE(f.exchange.trades().empty());
  EXPECT_TRUE(f.exchange.flush_candles().empty());

  f.exchange.set_retention_options(lobx::RuntimeRetentionOptions{});
  expect_ok(submit_spot_trade(f, 930043, 930044, 101, 1));
  auto candles = f.exchange.flush_candles();

  EXPECT_TRUE(f.exchange.events().records().size() > events_before);
  EXPECT_EQ(f.exchange.trades().size(), 1UL);
  EXPECT_FALSE(candles.empty());
  EXPECT_FALSE(f.exchange.candles().empty());
}
