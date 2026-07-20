# PositionEngine 集成说明

`PositionEngine` 负责永续市场持仓与持仓级状态，交易所撮合和账户账本仍由 `MarketEngine`、`AccountLedger` 与 `RiskEngine` 负责。

主要文件：

```text
cpp/include/lobx/position_engine.hpp
cpp/src/position_engine.cpp
cpp/include/lobx/market_engine.hpp
cpp/src/market_engine.cpp
```

当前覆盖：

- 多空持仓数量与均价更新。
- realized/unrealized PnL。
- reduce-only 边界。
- maintenance margin 与风险 tier。
- liquidation、funding、fee、insurance/ADL 相关测试路径。

构建与测试：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4

ctest --test-dir build \
  -R 'lobx_perp_risk_tests|lobx_perp_fee_tests|lobx_perp_funding_tests|lobx_perp_insurance_adl_tests|lobx_perp_order_types_tests|lobx_perp_trigger_orders_tests' \
  --output-on-failure
```

相关文档：

- `docs/PERPETUAL_RISK_MODEL.md`
- `docs/PERPETUAL_SCOPE.md`
- `docs/PERPETUAL_TEST_MATRIX.md`

当前 long diagnostic bundle 主要使用 spot total-equity 口径；perp/funding/insurance 尚未统一进该 bundle。
