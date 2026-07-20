# 实现状态

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

## 未实现或未统一

- `dynamic_market_maker`、`liquidity_taker`、`adversarial_sweeper`、`liquidity_withdrawer` 的 AgentRuntime 实现。
- Perp/funding/insurance 与 spot long diagnostic bundle 的统一会计视图。
- 非做市 Agent 的统一 resting-order TTL。
- 完全事件驱动的高性能 Agent scheduler。
- 生产级网络协议、持久化复制、高可用和多节点撮合。

未知或未实现 Agent 类型会显式报错，不会 silent fallback 到已有策略。

## 当前性能观察

- legacy static quoting 会导致 open orders 线性增长。
- bounded quoting + cadence 已验证可运行 `50 agents x 50000 steps`。
- 当前主要耗时仍在 exchange apply 路径。
- zero-fee closed-system accounting residual 和 inventory residual 已验证为 0。
