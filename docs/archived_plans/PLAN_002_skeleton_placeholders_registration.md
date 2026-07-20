# PLAN_002: 缺失算法机器人“工位/占位符（Skeleton Placeholders）”建立与注册接入

## 1. 目标与范围

遵循“**已有的先保留不测动，缺失的先开工位**”的工程原则：
对项目现有 5 种内置 Agent（`static_market_maker`, `noise_trader`, `momentum_follower`, `mean_reverter`, `whale_sweeper`）完全保持原样。

重点为目前系统中**完全缺失**的 5 种高级量化交易者搭建 C++ 与 Python 占位符接口（Skeleton Classes）与工厂注册：

1. `GridBotAgent` (网格交易员工位)
2. `FundingArbitrageAgent` (资金费率期现套利党工位)
3. `LiquidationSniperAgent` (清算爆仓猎人工位)
4. `OfiMomentumAgent` (OFI 订单流失衡高频动量工位)
5. `HawkesPanicRetailAgent` (霍克斯过程散户恐慌抛售工位)

---

## 2. 具体修改步骤

### Component A: C++ Core (`cpp/include/lobx/agents/` & `cpp/src/agents/`)
#### [MODIFY] [agent_types.hpp](file:///Users/mac/模拟交易所项目/cpp/include/lobx/agents/agent_types.hpp)
- 在 `AgentType` 枚举中增加缺失的类型值：`GridBot`, `FundingArbitrageur`, `LiquidationSniper`, `OfiMomentum`, `HawkesPanic`。

#### [NEW] [grid_bot_agent.hpp](file:///Users/mac/模拟交易所项目/cpp/include/lobx/agents/builtins/grid_bot_agent.hpp) & `.cpp`
#### [NEW] [funding_arbitrage_agent.hpp](file:///Users/mac/模拟交易所项目/cpp/include/lobx/agents/builtins/funding_arbitrage_agent.hpp) & `.cpp`
#### [NEW] [liquidation_sniper_agent.hpp](file:///Users/mac/模拟交易所项目/cpp/include/lobx/agents/builtins/liquidation_sniper_agent.hpp) & `.cpp`
#### [NEW] [ofi_momentum_agent.hpp](file:///Users/mac/模拟交易所项目/cpp/include/lobx/agents/builtins/ofi_momentum_agent.hpp) & `.cpp`
#### [NEW] [hawkes_panic_agent.hpp](file:///Users/mac/模拟交易所项目/cpp/include/lobx/agents/builtins/hawkes_panic_agent.hpp) & `.cpp`
- 声明并实现继承 `IAgent` 的占位类，`decide(AgentContext)` 目前仅返回空动作（`SleepUntil`），不参与行情交易。

#### [MODIFY] [agent_factory.cpp](file:///Users/mac/模拟交易所项目/cpp/src/agents/agent_factory.cpp)
- 在 `kAgentNameSpecs` 中注册这 5 种新机器人的 canonical 字符串（`grid_bot`, `funding_arbitrageur`, `liquidation_sniper`, `ofi_momentum`, `hawkes_panic`）。
- 在 `register_builtin_agents` 中注册上述新 Agent 的工厂函数。

#### [MODIFY] [CMakeLists.txt](file:///Users/mac/模拟交易所项目/CMakeLists.txt)
- 将新创建的 C++ 源码文件加入构建目标。

### Component B: Python Simulation Runner (`python/lobx/`)
#### [MODIFY] [mesa_model.py](file:///Users/mac/模拟交易所项目/python/lobx/mesa_model.py)
- 定义对应 Python 占位 Agent 类，`step()` 方法暂时为空操作（`pass`）。
- 在 `CryptoExchangeModel.__init__` 中增加对应 Agent 数量配置参数与初始化循环，充值初始余额。

---

## 3. 验证计划

1. 运行 `bash scripts/build_cmake.sh` 确保 C++ 没有任何编译错误，全套 CTest 单元测试 100% 通过。
2. 运行 Python 仿真集成测试，传入新占位机器人数量，验证能平滑完成空转运行。
