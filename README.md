# LobXChange

LobXChange 是一个面向市场微观结构研究的单机交易所与多 Agent 仿真平台。项目以 C++20 实现撮合、账本、风控、结算和仿真运行时，并提供 Python 数据分析与可视化工具。

它适合研究以下问题：

- 大规模机器人交易如何改变价格、价差、深度和成交量。
- 做市、噪声、动量、均值回归和大单扫盘之间如何发生财富与库存转移。
- 延迟、订单生命周期、冻结资金和手续费如何影响实验结果。
- 长时间运行时，账本是否守恒、订单是否无界增长、结果是否可复现。

## 当前能力

- Spot 与 perpetual 市场基础设施。
- 价格优先、时间优先撮合。
- IOC、FOK、POST_ONLY、STP、撤单和改单路径。
- 账户账本、冻结余额、风险检查、结算、手续费、持仓和清算相关测试。
- 事件流、K 线和实时仿真输出。
- 解耦的 `AgentRuntime + AgentAction + AgentContext` 架构。
- 10 种内置与扩展算法机器人工位架构支持：
  - 基础策略：`static_market_maker`, `noise_trader`, `momentum_follower`, `mean_reverter`, `whale_sweeper`
  - 高级扩展：`grid_bot`, `funding_arbitrageur`, `liquidation_sniper`, `ofi_momentum`, `hawkes_panic`
- 固定延迟队列、Agent 私有状态、action/event trace。
- price impact、PnL/accounting invariant 和 long diagnostic 实验。
- CSV/JSON/JSONL 研究 bundle 与 pandas/matplotlib 可视化。

## 架构

```text
Agent implementations
        |
        | decide(AgentContext) -> AgentAction[]
        v
AgentRuntime
  - scheduling / cadence
  - action latency queue
  - market/private views
  - action -> exchange command adapter
  - state/accounting diagnostics
        |
        v
Exchange core
  - matching
  - ledger / risk / settlement
  - events / candles / positions
        |
        v
CSV / JSON / JSONL / Python plots
```

## 快速编译

要求：CMake 3.21+、支持 C++20 的 GCC/Clang，推荐 Ninja。

```bash
git clone <your-github-url> LobXChange
cd LobXChange

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

也可以使用脚本：

```bash
bash scripts/build_cmake.sh
```

撮合簿的最小依赖已经放在 `third_party/limit-order-book`，默认不依赖本机绝对路径。

## 快速运行

基础交易所演示：

```bash
./build/lobx_simulator --quiet
```

解耦 AgentRuntime 演示：

```bash
./build/lobx_mesa_agent_simulator \
  --use-agent-runtime \
  --steps 200 \
  --makers 4 \
  --noise 6 \
  --momentum 2 \
  --mean-reversion 2 \
  --whales 1
```

输出 action/event trace：

```bash
./build/lobx_mesa_agent_simulator \
  --use-agent-runtime \
  --steps 200 \
  --actions-out build/agent_actions.jsonl \
  --events-out build/simulation_events.jsonl
```

## 文档

- [项目概况与架构](docs/PROJECT_OVERVIEW_CN.md)
- [Web3 与加密货币微观市场交易者生态建模指南](docs/research_reports/web3_market_microstructure_research.md)
- [CMake 编译指南](docs/BUILD_CN.md)
- [使用指南](docs/USAGE_CN.md)
- [GitHub 上传清单](docs/GITHUB_UPLOAD_CN.md)
- [AgentRuntime 架构](docs/AGENT_RUNTIME_ARCHITECTURE_CN.md)
- [价格冲击实验](docs/price_impact_experiments.md)
- [长时间诊断实验](docs/diagnostic_experiments.md)
- [永续合约风险模型](docs/PERPETUAL_RISK_MODEL.md)

## 目录

```text
cpp/include/lobx/          公共 C++ 接口
cpp/src/                   交易所、Agent 与仿真实现
cpp/apps/                  可执行程序
cpp/tools/                 研究工具
cpp/bench/                 benchmark
cpp/tests/                 单元、回归、集成、属性和研究测试
python/lobx/               Python 辅助与实时服务
scripts/                   构建、实验和绘图脚本
examples/                  示例订单与研究配置
experiments/               价格冲击配置
docs/                      设计、构建、研究报告与计划文档
log/                       开发日志与架构追溯记录
third_party/               最小 vendored 撮合簿依赖
web/                       实时市场页面
```

## 研究边界

- 这是单进程研究平台，不是生产交易所服务。
- long diagnostic 当前主要验证 spot accounting；perp/funding/insurance 有独立测试，但尚未统一进同一诊断 bundle。
- mark-to-market 默认使用 mid price。
- bounded quoting 会改变报价频率和订单寿命，不能与 legacy 场景直接当作同一经济实验比较。
- 上传公开仓库前，请确认 `third_party/limit-order-book` 的上游许可证和归属说明。
