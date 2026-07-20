# PLAN_001: 算法交易机器人全景对比盘点与开发路线图

## 1. 目标与背景

在 `LobXChange` 模拟交易所沙盒中，要为 AI 交易智能体提供真实、不发生“过拟合”的试炼环境，必须具备丰富且符合 Web3 币圈微观结构的算法交易机器人生态。

本计划旨在：
1. **对齐盘点（Gap Analysis）**：对比当前 `LobXChange` 已有的内置 Agent 与 `docs/research_reports/web3_market_microstructure_research.md` 研报中的高级数学模型。
2. **分类与路线图**：制定小步快跑的 Agent 分批开发路线图。

---

## 2. 对比盘点 (Gap Analysis)

| 交易者类型 | 当前项目状态 | 研报高级数学抽象模型 | 差距诊断 (Gap) |
|---|---|---|---|
| **做市商 (Market Maker)** | 仅有 `static_market_maker` (静态固定点差) | Avellaneda-Stoikov (AS) 库存驱动模型 + Funding-Aware MM | **缺乏库存避险与波动率响应**。当前做市商不会根据库存 $q$ 倾斜买卖价差，波动率飙升时不会自动抽撤流动性。 |
| **套利者 (Arbitrageur)** | ❌ **无** | Delta-Neutral 资金费率套利党 ($F_{predict} > C_{fees}$ 阈值) | **缺乏跨期/期现锚定机制**。永续合约与现货价格无法通过资金费率被动态锚定。 |
| **网格交易者 (Grid Bot)** | ❌ **无** | 几何网格 (Geometric Grid) / 等差网格被动流动性提供者 | **缺乏币圈最典型的震荡市被动策略**。无法模拟震荡市中强烈的均值回归阻力。 |
| **高频动量 (Momentum HFT)** | 仅有简单 K 线追涨杀跌 `momentum_follower` | 订单流失衡 (OFI) + 微观价格 ($P_{micro}$) 抢跑 | **缺乏微观挂单墙消耗识别**。无法模拟在买/卖一盘口即将吃光时的瞬时抢跑砸盘。 |
| **大单执行 (Execution Algo)** | 仅有市价砸盘 `whale_sweeper` | TWAP / VWAP 冰山拆单算法 (Randomized Slicing) | **冲击成本失真**。现有巨鲸直接市价砸盘造成瞬间暴跌，缺少机构在长窗口内隐秘切片吃单的真实冲击。 |
| **清算猎人 (Liquidation Sniper)** | ❌ **无** | 标记价格强平热力图 + 砸盘成本收益核算 | **缺乏对杠杆连环爆仓机制的掠夺行为**。无法重现币圈特有的“砸盘插针（Wick）接针”现象。 |
| **散户恐慌 (Panic Retail)** | 仅有均匀分布 `noise_trader` | 多维标记霍克斯过程 (Bivariate Marked Hawkes Process) | **缺乏时间聚集性 (Clustering)**。当前噪声交易是平滑随机的，无法重现爆仓事件触发的自我激发式散户恐慌抛售潮。 |

---

## 3. 分阶段小步快跑路线图 (Roadmap)

我们将按照“从基础流动性 $\rightarrow$ 币圈衍生品机制 $\rightarrow$ 高频/非线性恐慌”的顺序小步推进：

### 阶段一：基础微观结构补全 (Step 1)
- **目标**：实现 **Avellaneda-Stoikov (AS) 库存做市机器人** 与 **网格交易机器人 (Grid Bot)**。
- **验证**：验证在单边行情中 AS 做市商是否会倾斜报价平仓，以及网格机器人在震荡市中的盈利与突破时的亏损。

### 阶段二：永续合约衍生品机制 (Step 2)
- **目标**：实现 **资金费率套利党 (Funding Rate Farmer)** 与 **清算猎人 (Liquidation Sniper)**。
- **验证**：验证永续合约价格是否成功被套利者锚定在现货附近，以及清算猎人是否会在杠杆率极高时触发插针连环爆仓。

### 阶段三：高频与非线性博弈 (Step 3)
- **目标**：实现 **OFI 动量抢跑者**、**TWAP/VWAP 冰山委托** 与 **Hawkes 散户恐慌抛售器**。
- **验证**：验证市场是否涌现出真实的长尾厚尾分布与流动性瞬间抽空现象。

---

## 4. 本计划产出

- 本文档作为 `PLAN_001` 存入 `docs/pending_plans/PLAN_001_agent_gap_analysis_and_roadmap.md`。
- 确认差距后，开启下一个细分开发计划（`PLAN_002_avellaneda_stoikov_agent.md`）。
