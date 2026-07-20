# 项目概况与架构

## 1. 项目定位

LobXChange 是一个单机、确定性优先的交易所仿真平台。核心目标不是提供网络化交易服务，而是让研究人员能控制订单流、Agent 构成、延迟、市场环境和随机种子，并验证价格路径与账本结果。

项目同时保留三类运行路径：

1. Exchange core：直接提交订单，验证撮合、账本和风控语义。
2. Mesa simulation：较轻量、较快的既有机器人仿真路径。
3. AgentRuntime：策略与交易所解耦后的主研究路径，负责调度、延迟、状态视图、trace 和诊断。

长期方向是把研究能力统一到 `AgentRuntime + ExchangeCore + SimulationEvent`。

## 2. 交易所内核

主要模块：

- `Exchange`：市场、资产、订单和账户操作的统一入口。
- `MarketEngine`：spot/perp 撮合与订单生命周期。
- `AccountLedger`：free/locked/total 余额及资金守恒。
- `RiskEngine`：下单前检查、保证金和风险边界。
- `PositionEngine`：永续持仓、未实现盈亏和结算相关状态。
- `EventStore`：订单、成交、账户和市场事件。
- `KlineAggregator`：多周期 K 线。

核心行为已有单元、回归、集成、属性和会计守恒测试覆盖。

## 3. Agent 架构

Agent 只读取 `AgentContext` 并返回 `AgentAction`，不持有或调用 `Exchange`。

```text
IAgent::decide(context)
        -> SubmitLimitOrder / SubmitMarketOrder
        -> CancelOrder / ReplaceOrder / CancelAllOrders
        -> SleepUntil
```

`AgentRuntime` 是策略与执行之间的唯一适配层：

- 选择当前到期 Agent。
- 构建只读 market/private view。
- 应用决策 cadence 和 action latency。
- 将 AgentAction 转为交易所命令。
- 消费成交/订单结果并增量更新 AgentStateStore。
- 输出统一事件、action trace、价格序列和会计诊断。

内置 Agent 共 5 类，factory 使用 traits 与表驱动名称/别名注册。未知或未实现策略会显式抛错，不做 silent fallback。

## 4. 研究与诊断

Price impact 实验输出：

- mid-price time series
- return、realized volatility、drawdown
- trade count、volume
- accounting summary

Long diagnostic 进一步输出：

- per-agent state samples 和 final state
- inventory consistency residual
- open-order growth
- PnL by agent type
- runtime phase timing
- deterministic run hash
- unit/fixed-point scale sanity

zero-fee closed-system 场景要求：

```text
sum_agent_pnl + exchange_fee_revenue + house_pnl + insurance_pnl = 0
```

库存一致性要求：

```text
final_inventory = initial_inventory + buy_volume - sell_volume
```

## 5. 性能状态

legacy static market maker 会持续增加 resting quotes，导致 open orders 和 exchange apply 成本随时间上升。

bounded diagnostic 支持：

- 每边最大 open-order 数。
- quote refresh interval。
- quote TTL 与 stale cancel。
- per-agent-type decision cadence。

当前已验证 bounded 场景可运行 `50 agents x 50000 steps`。主要剩余耗时仍在 exchange apply 路径；非做市 Agent 的长期 GTC 残留仍需后续统一 TTL/生命周期治理。

## 6. 已知限制

- 单机、单进程，不包含生产级网络协议、HA 或持久化复制。
- Diagnostic bundle 当前以 spot total equity 为主要会计口径。
- Agent 策略用于框架研究，不能直接视为真实交易盈利模型。
- 数量和资产余额可能使用底层整数单位，图表解释前应检查 `unit_sanity_summary.json`。
