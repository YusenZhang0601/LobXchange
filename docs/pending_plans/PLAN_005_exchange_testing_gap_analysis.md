# PLAN_005：模拟交易所测试缺口与迁移状态

> **当前状态（2026-07-26）**：测试源已在个人 PR 栈的旧基线上添加，但 fresh build 重新验证未通过。作者的新重构尚不可见，本计划暂时冻结，不继续扩充用例。

## 1. 计划边界

本计划记录底层已有相关业务逻辑、但过去缺少边界验证的测试。它不要求提前实现网络会话、混合保证金或分级平仓，也不把“测试源已写入”当成“测试有效通过”。

当前只保留四组分支测试：

1. 并发读写竞争基线。
2. 流动性空洞与强平坏账。
3. 标记价格与预言机保护。
4. 事务故障注入与原子回滚。

## 2. 当前分支验证矩阵

| 范围 | 当前文件 | 2026-07-26 fresh build 结果 | 结论 |
| :--- | :--- | :--- | :--- |
| 并发读写 | [concurrent_market_engine_test.cpp](../../cpp/tests/concurrency/concurrent_market_engine_test.cpp) | CTest 传入 `EXPLICIT_RUN_ONLY` 后输出 `ran=0 failed=0` | **不通过**：没有实际运行用例，不能计作覆盖 |
| 流动性空洞/ADL | [perp_liquidation_stress_test.cpp](../../cpp/tests/integration/perp_liquidation_stress_test.cpp) | `PerpLiquidationStress` 2/2 | **通过**：仅当前旧基线 |
| 标记价格保护 | [oracle_mark_price_protection_test.cpp](../../cpp/tests/integration/oracle_mark_price_protection_test.cpp) | `OracleMarkPriceProtection` 1/2 | **不通过** |
| 事务故障恢复 | [transaction_fault_recovery_integration_test.cpp](../../cpp/tests/integration/transaction_fault_recovery_integration_test.cpp) | `TransactionFaultRecoveryIntegration` 0/5 | **不通过** |
| 既有事务回归 | [market_engine_transaction_fault_injection.cpp](../../cpp/tests/regression/market_engine_transaction_fault_injection.cpp) | `MarketEngineTransactionFaultInjection` 7/7 | **通过**：用于区分旧回归与新增集成假设 |

事务故障恢复用例曾以 `PLAN_006` 出现在提交和日志标题中，但仓库没有对应计划文件。本次维护不事后补造另一套计划，将它作为本测试缺口计划的分支验证范围统一管理。

## 3. 正确的点名验证方式

当前 CMake 注册的是聚合测试目标，不是源文件名。以下旧写法不能使用：

```bash
ctest -R concurrent_market_engine_test
ctest -R perp_liquidation_stress_test
ctest -R oracle_mark_price_protection_test
```

它们可能找不到任何测试却仍成功退出，形成伪绿。

当前分支应先 fresh build：

```bash
cmake -S . -B build
cmake --build build -j 4
```

再通过自制测试框架的 suite 过滤器点名执行：

```bash
./build/lobx_integration_tests PerpLiquidationStress
./build/lobx_integration_tests OracleMarkPriceProtection
./build/lobx_integration_tests TransactionFaultRecoveryIntegration
./build/lobx_regression_tests MarketEngineTransactionFaultInjection
```

全量入口仍是：

```bash
ctest --test-dir build --output-on-failure
```

但全量结果必须同时查看各聚合二进制的 `ran=<n> failed=<n>`；只看 CTest target 数量不足以证明子用例已运行。

并发测试在修正当前零用例隔离方式前，没有有效的自动验证命令。不要直接无参数运行 `lobx_concurrency_tests`，因为该测试本来用于复现未解决的数据竞争，可能崩溃。

## 4. 未来功能依赖型缺口

以下范围仍然暂缓，不能在底层功能不存在时先写成通过测试：

| 范围 | 当前缺失依赖 | 状态 |
| :--- | :--- | :--- |
| 断线自撤（COD） | 网络连接、会话上下文、心跳/超时 | 暂缓 |
| 混合保证金质押 | 多币种抵押物与 haircut 模型 | 暂缓 |
| 逐仓与分级平仓 | isolated margin 与阶梯减仓 | 暂缓 |

## 5. 作者新重构到位后的重新判定

对新基线逐组回答：

1. 被测业务行为是否仍存在，契约是否改变。
2. 作者是否已经提供等价或更强测试。
3. 旧用例失败是旧假设失效、测试夹具漂移，还是实际回归。
4. 用例是否应迁移、重写或删除。
5. 聚合目标是否真实运行了非零子用例。

只有在作者新基线上重新构建、点名测试和全量测试都得到与实际执行数量一致的结果后，本计划才可归档。
