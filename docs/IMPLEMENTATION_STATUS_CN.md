# 实现状态

> 本文只描述当前检出代码的能力边界，不代表相关改动已经合入作者仓库。
> Git/PR 与验证状态见 [项目状态与维护交接](HANDOVER_GUIDE_CN.md)。

## 已实现

- Spot/perpetual exchange core。
- 撮合、IOC/FOK/POST_ONLY/STP、撤单、改单路径。
- 账户账本、冻结余额、风险、结算、事件和 K 线。
- Perp 持仓、费用、funding、清算与保险相关测试。
- `IAgent`、`AgentAction`、`AgentContext`、`AgentFactoryRegistry`。
- 5 个真实内置 Agent：做市、噪声、动量、均值回归、鲸鱼 sweep。
- `AgentRuntime`、延迟队列、AgentStateStore、SimulationEvent。
- bounded static quoting 和按 Agent 类型 decision cadence。
- price impact、accounting/PnL invariant 和 long diagnostic 实验。
- price/accounting/inventory/open-order/performance CSV/JSON bundle。
- Python price impact 与 diagnostic 可视化。
- same-seed run hash 和 sampling consistency 测试。

## 已注册但尚未实现交易逻辑

- `grid_bot`
- `funding_arbitrageur`
- `liquidation_sniper`
- `ofi_momentum`
- `hawkes_panic`

这 5 种类型可以由工厂创建，但当前 `decide()` 返回空动作，不能计为已实现策略。

## 未实现或未统一

- `dynamic_market_maker`、`liquidity_taker`、`adversarial_sweeper`、`liquidity_withdrawer` 只有类型/名称映射，未注册工厂实现。
- Perp/funding/insurance 与 spot long diagnostic bundle 的统一会计视图。
- 非做市 Agent 的统一 resting-order TTL。
- 完全事件驱动的高性能 Agent scheduler。
- 生产级网络协议、持久化复制、高可用和多节点撮合。

未知或未实现 Agent 类型会显式报错，不会 silent fallback 到已有策略。

## 当前验证状态

- **通过**：当前 HEAD 可重新配置并完整编译。
- **通过**：Python 3.12.13 下现有 5 个 `unittest` 全部通过。
- **不通过**：2026-07-26 重新构建后，全量 CTest 为 42/44 个聚合目标通过；`lobx_integration_tests` 与 `lobx_mesa_agent_sim_tests` 失败。
- **不通过**：`lobx_concurrency_tests` 当前经 CTest 运行时实际为 `ran=0`，不能计作有效并发覆盖。

以上是维护快照，不替代作者重构进入新基线后的重新验证。

## 历史性能观察（待新基线重验）

- legacy static quoting 会导致 open orders 线性增长。
- bounded quoting + cadence 曾在旧基线上运行 `50 agents x 50000 steps`。
- 旧基线观察的主要耗时在 exchange apply 路径。
- zero-fee closed-system accounting residual 和 inventory residual 曾在旧基线上记录为 0。
