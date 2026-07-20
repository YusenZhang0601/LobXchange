# PLAN_003: 现有简易机器人的未来升级预期计划 (Future Upgrades for Existing Agents)

## 1. 说明与定位

本计划为**预留计划**（处于 Pending 状态），将在完成缺失机器人的工位开辟（`PLAN_002`）以及逐步填充其算法逻辑后执行。

目地是保持现有 5 种基础机器人（`static_market_maker`, `noise_trader`, `momentum_follower`, `mean_reverter`, `whale_sweeper`）稳定不动的的前提下，规划未来对其进行微观模型升级的路线。

---

## 2. 预期升级明细

1. **`static_market_maker` $\rightarrow$ Avellaneda-Stoikov (AS) 库存驱动做市商**
   - **预期**：增加库存 $q$ 与波动率 $\sigma^2$ 参数，将固定双边挂单升级为基于 HJB 方程的保留价格 $r$ 动态偏移报价。
2. **`noise_trader` $\rightarrow$ 多维标记霍克斯过程 (Hawkes Process)**
   - **预期**：将平滑均匀分布的下单时间戳，升级为带有自我激发系数 $\alpha_{s,s}$ 的事件聚集下单（重现散户恐慌踩踏）。
3. **`momentum_follower` $\rightarrow$ 订单流失衡 (OFI) 高频动量**
   - **预期**：从滞后的 K 线指标升级为监听盘口买/卖一挂单墙消耗（OFI），实现微秒级突破抢跑。
4. **`whale_sweeper` $\rightarrow$ TWAP / VWAP 冰山拆单算法**
   - **预期**：将一次性市价大单砸盘，升级为带随机加噪的长窗口切片委托，降低瞬时市场冲击。
