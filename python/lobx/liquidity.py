from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class LadderOrder:
    side: str
    price: int
    qty: int
    post_only: bool = True


def fixed_ladder(
    mid_price: int,
    levels: int,
    spread_bps: int,
    qty_per_level: int,
    price_step_bps: int = 10,
) -> list[LadderOrder]:
    """Create symmetric LP orders around a midpoint using integer ticks."""
    if mid_price <= 0:
        raise ValueError("mid_price must be positive")
    if levels <= 0:
        raise ValueError("levels must be positive")
    if qty_per_level <= 0:
        raise ValueError("qty_per_level must be positive")
    if spread_bps < 0 or price_step_bps < 0:
        raise ValueError("bps values must be non-negative")

    half_spread = max(1, spread_bps // 2)
    orders: list[LadderOrder] = []
    for level in range(levels):
        offset_bps = half_spread + level * price_step_bps
        offset = max(1, (mid_price * offset_bps) // 10_000)
        bid = max(1, mid_price - offset)
        ask = mid_price + offset
        orders.append(LadderOrder("bid", bid, qty_per_level, post_only=True))
        orders.append(LadderOrder("ask", ask, qty_per_level, post_only=True))
    return orders
