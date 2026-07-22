# LobXChange 模拟交易所项目接手与维护交接文档

> **文档版本**：v1.0.0  
> **更新时间**：2026-07-22  
> **适用对象**：后续接手 LobXChange 开发、运维、策略研究及架构重构的工程师与量化研究员。

---

## 1. 项目基本信息

| 项目属性 | 详细说明 |
| :--- | :--- |
| **项目名称** | LobXChange (`adv-skem/LobXchange`) |
| **项目定位** | 面向加密货币/金融市场微观结构研究的高性能 **C++20 单机交易所与多 Agent 模拟仿真平台** |
| **核心技术栈** | C++20 (撮合/账本/风控/AgentRuntime)、Python 3.10+ (Mesa ABM 仿真/分析)、Web (WebSocket/HTTP) |
| **构建系统** | CMake 3.21+，支持 GCC/Clang，默认兼容 Ninja 快速并行构建 |
| **测试框架** | 包含 44 个全量 CTest 自动化测试套件（单元测试、回归测试、并发测试、属性守恒测试） |

---

## 2. 系统核心架构与模块分工

系统设计遵循 **“C++ 核心撮合 + AgentRuntime 隔离 + 外部分析桥接”** 的解耦架构。

```text
 Agent 策略实现 (C++ Builtin Agents / Python Mesa Agents)
          │
          │ decide(AgentContext) -> AgentAction[]
          ▼
   AgentRuntime (仿真运行时)
     ├── 调度与步长控制 (Cadence / Scheduling)
     ├── 动作延迟队列 (Action Latency Queue)
     ├── 市场视图与私有状态隔离 (MarketView / AgentStateStore)
     └── 动作转换器 (Action -> Exchange Command Adapter)
          │
          ▼
   Exchange (统一交易所大面板)
     ├── MarketEngine  : 限价/市价撮合簿 (IOC/FOK/POST_ONLY/STP/改撤单)
     ├── AccountLedger : 资金账本 (可用余额/冻结余额/守恒检查)
     ├── PositionEngine: 现货持仓与永续合约持仓 (加权均价/未实现盈亏)
     ├── RiskEngine    : 逐仓/交叉保证金风控、清算线计算
     └── EventStore    : 成交与盘口变更事件广播、K线聚合器 (KlineAggregator)
          │
          ▼
   数据导出与分析 (CSV / JSON / JSONL / Web 实时展示)
```

---

## 3. 代码库目录与核心文件对照表

### 3.1 C++ 核心引擎与仿真 (`cpp/`)

| 文件/目录路径 | 模块名称 | 核心职责与关键类/结构体 |
| :--- | :--- | :--- |
| [cpp/include/lobx/market_engine.hpp](file:///Users/mac/模拟交易所项目/cpp/include/lobx/market_engine.hpp) | 撮合引擎 | `MarketEngine`：订单簿数据结构、撮合算法、IOC/FOK/STP 处理 |
| [cpp/include/lobx/account_ledger.hpp](file:///Users/mac/模拟交易所项目/cpp/include/lobx/account_ledger.hpp) | 资金账本 | `AccountLedger`：资产注册、可用/冻结余额变更、资金绝对守恒断言 |
| [cpp/include/lobx/position_engine.hpp](file:///Users/mac/模拟交易所项目/cpp/include/lobx/position_engine.hpp) | 持仓引擎 | `PositionEngine`：多空持仓、开仓均价、已实现/未实现盈亏结算 |
| [cpp/include/lobx/exchange.hpp](file:///Users/mac/模拟交易所项目/cpp/include/lobx/exchange.hpp) | 交易所聚合 | `Exchange`：对外统一 API，组装 Market/Ledger/Position/Risk 引擎 |
| [cpp/include/lobx/agents/](file:///Users/mac/模拟交易所项目/cpp/include/lobx/agents) | Agent 策略集 | `Agent` 基类、`AgentAction` 定义、`AgentFactory` 策略工厂 |
| [cpp/include/lobx/agents/builtins](file:///Users/mac/模拟交易所项目/cpp/include/lobx/agents/builtins) | 内置策略 | 做市商、噪声交易者、动量、均值回归、网格、资金费套利等 10 种策略 |
| [cpp/include/lobx/simulation/agent_runtime.hpp](file:///Users/mac/模拟交易所项目/cpp/include/lobx/simulation/agent_runtime.hpp) | 仿真运行时 | `AgentRuntime`：动作延迟队列、私有状态存储、批量指令提交 |
| [cpp/tests/](file:///Users/mac/模拟交易所项目/cpp/tests) | CTest 测试集 | 44 个单元、回归、并发、属性守恒与长时间仿真测试套件 |

### 3.2 Python 与 Web 模块 (`python/` & `web/`)

| 文件/目录路径 | 模块名称 | 核心职责 |
| :--- | :--- | :--- |
| [python/lobx/mesa_exchange.py](file:///Users/mac/模拟交易所项目/python/lobx/mesa_exchange.py) | Python IPC 桥 | 包装 Python 与 C++ `lobx_step_exchange` 二进制之间的 Subprocess 通信 |
| [python/lobx/mesa_model.py](file:///Users/mac/模拟交易所项目/python/lobx/mesa_model.py) | ABM 建模主类 | 基于 Python Mesa 框架的多 Agent 仿真模型类 |
| [python/lobx/realtime_server.py](file:///Users/mac/模拟交易所项目/python/lobx/realtime_server.py) | 实时数据推送 | WebSocket / HTTP 实时广播盘口、成交与 K 线数据 |
| [web/](file:///Users/mac/模拟交易所项目/web) | 前端展示 | 静态与实时前端可视化交互页面 |

### 3.3 核心文档 (`docs/`)

| 文档路径 | 文档主题 |
| :--- | :--- |
| [docs/PROJECT_OVERVIEW_CN.md](file:///Users/mac/模拟交易所项目/docs/PROJECT_OVERVIEW_CN.md) | 项目概况与整体架构设计中文说明 |
| [docs/PERPETUAL_RISK_MODEL.md](file:///Users/mac/模拟交易所项目/docs/PERPETUAL_RISK_MODEL.md) | 永续合约风控模型、维持保证金与清算规则 |
| [docs/AGENT_RUNTIME_ARCHITECTURE_CN.md](file:///Users/mac/模拟交易所项目/docs/AGENT_RUNTIME_ARCHITECTURE_CN.md) | AgentRuntime 延迟队列与私有视图架构 |
| [docs/price_impact_experiments.md](file:///Users/mac/模拟交易所项目/docs/price_impact_experiments.md) | 价格冲击实验方法论与诊断说明 |

---

## 4. 编译、测试与开发工作流

### 4.1 环境要求
- CMake 3.21 或更高版本
- 支持 C++20 的编译器（GCC 11+ / Clang 13+ / MSVC 2019+）
- Python 3.10+（若需要运行 Python Mesa 仿真与数据绘图）

### 4.2 构建与全量测试标准命令

```bash
# 1. 配置并构建项目 (Release 模式)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4

# 2. 执行全量自动化测试套件
ctest --test-dir build --output-on-failure
```

### 4.3 基础运行命令

- **单机撮合演示可执行文件**：
  ```bash
  ./build/lobx_simulator --quiet
  ```
- **解耦 AgentRuntime 多 Agent 仿真**：
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
- **生成结构化 Trace 日志（用于画图或量化回测分析）**：
  ```bash
  ./build/lobx_mesa_agent_simulator \
    --use-agent-runtime \
    --steps 200 \
    --actions-out build/agent_actions.jsonl \
    --events-out build/simulation_events.jsonl
  ```

---

## 5. 现存技术债与已完成的重构

### 5.1 已落地的关键重构（路线 A 快照优化）
- **原隐患**：`MarketEngine::submit_limit` 热路径过去在每次订单风控失败或提交时无条件执行全量深拷贝快照（`make_submit_snapshot`），时间复杂度退化为 $O(N)$。
- **优化方案**：重构为“两阶段纯读校验 + 延迟快照恢复”，不影响状态时不触发深拷贝。
- **验证结果**：吞吐量提升 20.2%，且通过了 **40 大项 / 250 断言** 故障注入与资金守恒回归测试。

### 5.2 推荐的下一阶段重构路线图

1. **Python - C++ 交互层优化（通信瓶颈）**：
   - 当前 Python Mesa 使用 Subprocess JSON IPC 交互 C++ 二进制，频繁的序列化与 I/O 是高频仿真的主要瓶颈。
   - **推荐方案**：采用 `pybind11` 或共享内存 (Shared Memory) 直接暴露 C++ API 给 Python 层。
2. **数据结构与内存连续性**：
   - `AccountLedger` 与 `PositionEngine` 当前采用嵌套哈希表（`std::unordered_map`），且按 `std::string market_symbol` 查找。
   - **推荐方案**：为 Asset/Market 分配连续整数 ID（`AssetId` / `MarketId`），哈希表替换为一维 `std::vector` 连续内存映射，提升 CPU L1/L2 Cache 命中率。
3. **永续合约测试与长周期诊断整合**：
   - 现有的 Long Diagnostic 主要测试现货账本；永续合约（Funding Rate、Insurance Fund、ADL）虽然有单独的 CTest 测试，但应合并入长周期诊断 Bundle 中。

---

## 6. 接手常见问题与排错 (FAQ)

- **Q1: 运行 `ctest` 时部分测试运行较慢正常吗？**  
  **A**: 正常。`lobx_mesa_agent_sim_tests`（40s+）和 `lobx_random_bot_invariants_tests`（约 5s）包含长周期高频 Agent 涌现和随机不变量校验。若只需快速验证基础功能，可以运行 `ctest -R lobx_unit_tests`。
- **Q2: 修改账本或持仓逻辑时需要注意什么？**  
  **A**: 必须确保资金与持仓的**原子回滚**与**守恒不变量**。任何更改后，务必运行 `lobx_property_tests` 和 `lobx_regression_tests`，验证是否有资损或资金泄漏。
- **Q3: 如何添加一种新的内置交易策略？**  
  **A**: 继承 `lobx::agents::Agent` 接口，实现 `decide(const AgentContext& ctx)` 方法，并在 `cpp/include/lobx/agents/agent_factory.hpp` 中添加名称映射。

---

## 7. 维护与知识库同步规范

根据 Second Brain 维护规则，每次完成重构、技术故障排查或重大架构更新后：
1. 评估更改的复杂度与可复用性；
2. 在 Second Brain 知识库（`Experience/` 或 `Technical/`）追加或更新相关条目；
3. 同步更新 `90-System/INDEX.md` 和 `90-System/LOG.md`。

---
*LobXChange 项目交接文档完结。*
