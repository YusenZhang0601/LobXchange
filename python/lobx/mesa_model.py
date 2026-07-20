from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
from statistics import mean
from typing import Any

import mesa

from .mesa_exchange import LobxStepExchange


POST_ONLY = "POST_ONLY"
IOC = "IOC"


@dataclass(slots=True)
class OrderAction:
    user: int
    side: str
    price: int
    qty: int
    flags: str


@dataclass(slots=True)
class MesaRunSummary:
    steps: int
    agent_count: int
    accepted_orders: int
    rejected_orders: int
    trade_count: int
    final_best_bid: int
    final_best_ask: int
    final_mid_price: float
    mean_spread: float
    agent_types: dict[str, int]


class CryptoExchangeModel(mesa.Model):
    def __init__(
        self,
        seed: int = 42,
        build_dir: str = "build-fincept",
        makers: int = 4,
        noise: int = 6,
        momentum: int = 2,
        mean_reversion: int = 2,
        whales: int = 1,
        grid_bots: int = 0,
        funding_arbitrageurs: int = 0,
        liquidation_snipers: int = 0,
        ofi_momentums: int = 0,
        hawkes_panics: int = 0,
        reference_price: int = 100,
    ) -> None:
        super().__init__(rng=seed)
        self.exchange = LobxStepExchange(build_dir=build_dir)
        self.reference_price = reference_price
        self.next_order_id = 1
        self.now = 0
        self.book: dict[str, Any] = {"bids": [], "asks": []}
        self.trades: list[dict[str, Any]] = []
        self.spreads: list[int] = []
        self.accepted_orders = 0
        self.rejected_orders = 0
        self.agent_type_counts: dict[str, int] = {}

        next_user = 100
        for i in range(makers):
            MarketMakerAgent(self, next_user, f"maker_{i}", reference_price)
            next_user += 1
        for i in range(noise):
            NoiseTraderAgent(self, next_user, f"noise_{i}", reference_price)
            next_user += 1
        for i in range(momentum):
            MomentumAgent(self, next_user, f"momentum_{i}", reference_price)
            next_user += 1
        for i in range(mean_reversion):
            MeanReversionAgent(self, next_user, f"mean_reversion_{i}", reference_price)
            next_user += 1
        for i in range(whales):
            WhaleSweeperAgent(self, next_user, f"whale_{i}", reference_price)
            next_user += 1
        for i in range(grid_bots):
            GridBotAgent(self, next_user, f"grid_bot_{i}", reference_price)
            next_user += 1
        for i in range(funding_arbitrageurs):
            FundingArbitrageAgent(self, next_user, f"funding_arb_{i}", reference_price)
            next_user += 1
        for i in range(liquidation_snipers):
            LiquidationSniperAgent(self, next_user, f"sniper_{i}", reference_price)
            next_user += 1
        for i in range(ofi_momentums):
            OfiMomentumAgent(self, next_user, f"ofi_mom_{i}", reference_price)
            next_user += 1
        for i in range(hawkes_panics):
            HawkesPanicAgent(self, next_user, f"hawkes_panic_{i}", reference_price)
            next_user += 1

        for agent in self.agents:
            self.exchange.deposit(agent.user_id, "USDT", 1_000_000_000)
            self.exchange.deposit(agent.user_id, "BTC", 1_000_000)
            self.agent_type_counts[agent.kind] = self.agent_type_counts.get(agent.kind, 0) + 1

    def step(self) -> None:
        self.now += 1
        self.book = self.exchange.book(levels=10)
        self.agents.shuffle_do("step")
        self.book = self.exchange.book(levels=10)
        spread = self.current_spread()
        if spread is not None:
            self.spreads.append(spread)

    def submit_action(self, action: OrderAction) -> dict[str, Any]:
        order_id = self.next_order_id
        self.next_order_id += 1
        result = self.exchange.order(
            action.user,
            order_id,
            action.side,
            action.price,
            action.qty,
            action.flags,
            self.now,
        )
        if result.get("accepted"):
            self.accepted_orders += 1
        else:
            self.rejected_orders += 1
        self.trades.extend(result.get("trades", []))
        return result

    def best_bid(self) -> int | None:
        bids = self.book.get("bids", [])
        return int(bids[0]["price"]) if bids else None

    def best_ask(self) -> int | None:
        asks = self.book.get("asks", [])
        return int(asks[0]["price"]) if asks else None

    def last_price(self) -> int:
        if self.trades:
            return int(self.trades[-1]["price"])
        bid = self.best_bid()
        ask = self.best_ask()
        if bid is not None and ask is not None:
            return (bid + ask) // 2
        return self.reference_price

    def current_spread(self) -> int | None:
        bid = self.best_bid()
        ask = self.best_ask()
        if bid is None or ask is None or ask < bid:
            return None
        return ask - bid

    def summary(self) -> MesaRunSummary:
        bid = self.best_bid() or 0
        ask = self.best_ask() or 0
        mid = ((bid + ask) / 2.0) if bid > 0 and ask > 0 else float(self.last_price())
        return MesaRunSummary(
            steps=self.now,
            agent_count=len(self.agents),
            accepted_orders=self.accepted_orders,
            rejected_orders=self.rejected_orders,
            trade_count=len(self.trades),
            final_best_bid=bid,
            final_best_ask=ask,
            final_mid_price=mid,
            mean_spread=mean(self.spreads) if self.spreads else 0.0,
            agent_types=dict(sorted(self.agent_type_counts.items())),
        )

    def close(self) -> None:
        self.exchange.close()


class TradingAgent(mesa.Agent):
    kind = "base"

    def __init__(self, model: CryptoExchangeModel, user_id: int, name: str, reference_price: int) -> None:
        super().__init__(model)
        self.user_id = user_id
        self.name = name
        self.reference_price = reference_price

    def submit(self, side: str, price: int, qty: int, flags: str) -> dict[str, Any]:
        return self.model.submit_action(OrderAction(self.user_id, side, max(1, price), max(1, qty), flags))


class MarketMakerAgent(TradingAgent):
    kind = "market_maker"

    def step(self) -> None:
        mid = self.model.last_price()
        spread = 2 + int(self.model.random.random() * 3)
        qty = 2 + int(self.model.random.random() * 3)
        self.submit("BUY", mid - spread, qty, POST_ONLY)
        self.submit("SELL", mid + spread, qty, POST_ONLY)


class NoiseTraderAgent(TradingAgent):
    kind = "noise_trader"

    def step(self) -> None:
        mid = self.model.last_price()
        side = "BUY" if self.model.random.random() < 0.5 else "SELL"
        price_offset = int(self.model.random.randrange(-4, 5))
        aggressive = self.model.random.random() < 0.35
        if side == "BUY":
            price = mid + (8 if aggressive else price_offset)
        else:
            price = mid - (8 if aggressive else price_offset)
        flags = IOC if aggressive else POST_ONLY
        self.submit(side, price, 1, flags)


class MomentumAgent(TradingAgent):
    kind = "momentum"

    def step(self) -> None:
        if len(self.model.trades) < 2:
            return
        previous = int(self.model.trades[-2]["price"])
        last = int(self.model.trades[-1]["price"])
        if last == previous:
            return
        side = "BUY" if last > previous else "SELL"
        price = last + 12 if side == "BUY" else last - 12
        self.submit(side, price, 2, IOC)


class MeanReversionAgent(TradingAgent):
    kind = "mean_reversion"

    def step(self) -> None:
        last = self.model.last_price()
        deviation = last - self.reference_price
        if abs(deviation) < 3:
            return
        side = "SELL" if deviation > 0 else "BUY"
        price = last - 4 if side == "SELL" else last + 4
        self.submit(side, price, 2, IOC)


class WhaleSweeperAgent(TradingAgent):
    kind = "whale_sweeper"

    def step(self) -> None:
        if self.model.now % 12 != 0:
            return
        side = "BUY" if self.model.random.random() < 0.5 else "SELL"
        last = self.model.last_price()
        price = last + 25 if side == "BUY" else last - 25
        self.submit(side, price, 20, IOC)


class GridBotAgent(TradingAgent):
    kind = "grid_bot"

    def step(self) -> None:
        # Skeleton placeholder: NOP for now
        pass


class FundingArbitrageAgent(TradingAgent):
    kind = "funding_arbitrageur"

    def step(self) -> None:
        # Skeleton placeholder: NOP for now
        pass


class LiquidationSniperAgent(TradingAgent):
    kind = "liquidation_sniper"

    def step(self) -> None:
        # Skeleton placeholder: NOP for now
        pass


class OfiMomentumAgent(TradingAgent):
    kind = "ofi_momentum"

    def step(self) -> None:
        # Skeleton placeholder: NOP for now
        pass


class HawkesPanicAgent(TradingAgent):
    kind = "hawkes_panic"

    def step(self) -> None:
        # Skeleton placeholder: NOP for now
        pass


def run_smoke(args: argparse.Namespace) -> MesaRunSummary:
    model = CryptoExchangeModel(
        seed=args.seed,
        build_dir=args.build_dir,
        makers=args.makers,
        noise=args.noise,
        momentum=args.momentum,
        mean_reversion=args.mean_reversion,
        whales=args.whales,
        grid_bots=getattr(args, "grid_bots", 0),
        funding_arbitrageurs=getattr(args, "funding_arbitrageurs", 0),
        liquidation_snipers=getattr(args, "liquidation_snipers", 0),
        ofi_momentums=getattr(args, "ofi_momentums", 0),
        hawkes_panics=getattr(args, "hawkes_panics", 0),
        reference_price=args.reference_price,
    )
    try:
        for _ in range(args.steps):
            model.step()
        summary = model.summary()
        if args.output:
            with open(args.output, "w", encoding="utf-8") as f:
                json.dump(asdict(summary), f, indent=2, sort_keys=True)
                f.write("\n")
        return summary
    finally:
        model.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run a Mesa-only robot smoke test against the LOBX exchange core")
    parser.add_argument("--build-dir", default="build-fincept")
    parser.add_argument("--steps", type=int, default=80)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--reference-price", type=int, default=100)
    parser.add_argument("--makers", type=int, default=4)
    parser.add_argument("--noise", type=int, default=6)
    parser.add_argument("--momentum", type=int, default=2)
    parser.add_argument("--mean-reversion", type=int, default=2)
    parser.add_argument("--whales", type=int, default=1)
    parser.add_argument("--grid-bots", type=int, default=0)
    parser.add_argument("--funding-arbitrageurs", type=int, default=0)
    parser.add_argument("--liquidation-snipers", type=int, default=0)
    parser.add_argument("--ofi-momentums", type=int, default=0)
    parser.add_argument("--hawkes-panics", type=int, default=0)
    parser.add_argument("--output")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    summary = run_smoke(args)
    print(json.dumps(asdict(summary), indent=2, sort_keys=True))
    return 0 if summary.trade_count > 0 and summary.accepted_orders > 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
