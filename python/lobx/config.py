from __future__ import annotations

from dataclasses import asdict, dataclass, field
import json
from pathlib import Path
from typing import Any


@dataclass(slots=True)
class AssetConfig:
    symbol: str
    decimals: int
    max_supply: int
    issuer: int
    initial_supply: int = 0


@dataclass(slots=True)
class MarketConfig:
    symbol: str
    base: str
    quote: str
    tick_size: int = 1
    lot_size: int = 1
    min_qty: int = 1
    min_notional: int = 1


@dataclass(slots=True)
class LiquidityConfig:
    market: str
    lp_user: int
    mid_price: int
    levels: int = 5
    spread_bps: int = 20
    qty_per_level: int = 10
    price_step_bps: int = 10


@dataclass(slots=True)
class ExchangeBootstrap:
    assets: list[AssetConfig] = field(default_factory=list)
    markets: list[MarketConfig] = field(default_factory=list)
    liquidity: list[LiquidityConfig] = field(default_factory=list)

    @classmethod
    def default_demo(cls) -> "ExchangeBootstrap":
        return cls(
            assets=[
                AssetConfig("USDT", decimals=2, max_supply=1_000_000_000_000, issuer=1, initial_supply=1_000_000_000),
                AssetConfig("SIM", decimals=0, max_supply=100_000_000, issuer=100, initial_supply=1_000_000),
            ],
            markets=[MarketConfig("SIM-USDT", base="SIM", quote="USDT", tick_size=1, lot_size=1, min_qty=1, min_notional=1)],
            liquidity=[LiquidityConfig("SIM-USDT", lp_user=100, mid_price=1000, levels=5, spread_bps=20, qty_per_level=100)],
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "assets": [asdict(asset) for asset in self.assets],
            "markets": [asdict(market) for market in self.markets],
            "liquidity": [asdict(item) for item in self.liquidity],
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "ExchangeBootstrap":
        return cls(
            assets=[AssetConfig(**item) for item in data.get("assets", [])],
            markets=[MarketConfig(**item) for item in data.get("markets", [])],
            liquidity=[LiquidityConfig(**item) for item in data.get("liquidity", [])],
        )

    @classmethod
    def load(cls, path: str | Path) -> "ExchangeBootstrap":
        with Path(path).open("r", encoding="utf-8") as f:
            return cls.from_dict(json.load(f))

    def save(self, path: str | Path) -> None:
        target = Path(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("w", encoding="utf-8") as f:
            json.dump(self.to_dict(), f, indent=2, sort_keys=True)
            f.write("\n")
