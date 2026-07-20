# AgentRuntime 第一阶段架构说明

本阶段新增 `lobx::agents` 策略层和 `lobx::simulation::AgentRuntime` 运行时层，用于把机器人决策逻辑从交易所撮合内核中解耦。

## 边界

- Exchange / MarketEngine 只负责接收订单命令、撮合、结算、风控和输出交易所事件。
- Agent 只实现 `IAgent::decide(const AgentContext&)`，返回 `AgentAction`。
- Agent 不 include `exchange.hpp` 或 `market_engine.hpp`，也不持有交易所指针。
- AgentRuntime 是唯一把 `AgentAction` 转换为 `Exchange::submit_*` / `Exchange::cancel` 的地方。

## 新增核心接口

- `cpp/include/lobx/agents/agent.hpp`
- `cpp/include/lobx/agents/agent_action.hpp`
- `cpp/include/lobx/agents/agent_context.hpp`
- `cpp/include/lobx/agents/agent_factory.hpp`
- `cpp/include/lobx/simulation/agent_runtime.hpp`
- `cpp/include/lobx/simulation/action_queue.hpp`
- `cpp/include/lobx/simulation/agent_state_store.hpp`
- `cpp/include/lobx/simulation/market_view.hpp`
- `cpp/include/lobx/simulation/simulation_event.hpp`

## 内置策略

已迁移 5 个 Mesa 等价策略为 `IAgent`：

- `static_market_maker`
- `noise_trader`
- `momentum_follower`
- `mean_reverter`
- `whale_sweeper`

这些策略只读取 `AgentContext.market_view` 和 `AgentContext.private_state`，只输出 `AgentAction`。

## CLI

旧 Mesa 路径保持默认不变。新增新 runtime 路径：

```bash
./build-bench/lobx_mesa_agent_simulator \
  --use-agent-runtime \
  --steps 20 \
  --makers 2 \
  --noise 2 \
  --momentum 1 \
  --mean-reversion 1 \
  --whales 1 \
  --actions-out /tmp/lobx_agent_actions.jsonl \
  --events-out /tmp/lobx_agent_events.jsonl
```

## ResearchRunner 过渡状态

ResearchRunner 暂未删除。它仍负责旧 research/emergence 路径中的延迟队列、公开/私有数据延迟、action trace 和指标。

本阶段已移除 `agent_population.cpp` 中把复杂策略静默映射成 `market_maker` 或 `taker_sweep` 的行为。复杂策略会保留原始类型；旧 ResearchRunner 遇到尚未支持的策略会显式失败。后续应将 ResearchRunner 的延迟模型和指标逐步并入 `AgentRuntime + ExchangeCore + SimulationEvent` 主路径。

## 已知性能风险

- `AgentRuntime` 第一阶段仍使用 step-based clock。
- `MarketView` 只缓存 topN 和 recent trades，避免 agent 扫全局历史。
- `AgentStateStore` 用 vector 维护 open orders 和 recent fills，接口已隔离，后续可替换为 hash map 或 ring buffer。
- 事件 trace 当前保存在内存并支持 JSONL 写出；大规模实验需要流式 recorder。
