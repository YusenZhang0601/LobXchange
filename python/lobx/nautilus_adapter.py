from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(slots=True)
class NautilusLobxAdapterPlan:
    """Boundary object for mapping Nautilus commands into the C++ exchange core.

    The compiled binding is intentionally not introduced in this first slice.
    The adapter documents the conversion surface so the next increment can add
    pybind11 or pyo3 without changing strategy-level code.
    """

    default_account_id: str = "SIM-001"
    default_venue: str = "LOBX"


def to_lobx_order(command: Any, market_id: int, user_id: int) -> dict[str, Any]:
    side_value = getattr(command, "side", None)
    order_id_value = getattr(command, "client_order_id", getattr(command, "id", None))
    price_value = getattr(command, "price", None)
    quantity_value = getattr(command, "quantity", None)
    tif_value = str(getattr(command, "time_in_force", "")).upper()
    post_only = bool(getattr(command, "post_only", False))
    reduce_only = bool(getattr(command, "reduce_only", False))

    flags: list[str] = []
    if "IOC" in tif_value:
      flags.append("IOC")
    if "FOK" in tif_value:
      flags.append("FOK")
    if post_only:
      flags.append("POST_ONLY")
    if reduce_only:
      flags.append("REDUCE_ONLY")

    return {
        "market_id": market_id,
        "user": user_id,
        "order_id": str(order_id_value) if order_id_value is not None else "",
        "side": str(side_value).lower(),
        "price": int(price_value) if price_value is not None else 0,
        "qty": int(quantity_value) if quantity_value is not None else 0,
        "flags": flags,
    }
