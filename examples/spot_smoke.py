from dataclasses import asdict

from lobx.config import ExchangeBootstrap
from lobx.liquidity import fixed_ladder


def main() -> None:
    cfg = ExchangeBootstrap.default_demo()
    market_lp = cfg.liquidity[0]
    orders = fixed_ladder(
        market_lp.mid_price,
        levels=market_lp.levels,
        spread_bps=market_lp.spread_bps,
        qty_per_level=market_lp.qty_per_level,
        price_step_bps=market_lp.price_step_bps,
    )
    print(cfg.to_dict())
    print([asdict(order) for order in orders[:4]])


if __name__ == "__main__":
    main()
