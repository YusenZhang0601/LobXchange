# Fincept 环境与 Mesa 机器人接入使用说明

本文档记录可选 Conda 环境中 Mesa 机器人接入 LobXChange 的使用方式。以下命令假设：

```bash
export FINCEPT_ENV=/path/to/conda/env
export MESA_REPO=/path/to/mesa
```

## 当前环境版本

截至本次更新，`Fincept` 环境版本为：

```bash
"${FINCEPT_ENV}/bin/python" --version
```

```text
Python 3.12.13
```

Mesa 使用本地源码 editable 安装：

```bash
"${FINCEPT_ENV}/bin/python" -c "import mesa; print(mesa.__version__); print(mesa.__file__)"
```

```text
4.0.0a0
/path/to/mesa/mesa/__init__.py
```

核心 Python 依赖：

```bash
"${FINCEPT_ENV}/bin/python" -c "import numpy, scipy, pandas, tqdm; print(numpy.__version__, scipy.__version__, pandas.__version__, tqdm.__version__)"
```

当前结果：

```text
numpy 2.4.6
scipy 1.17.1
pandas 3.0.3
tqdm 4.67.3
```

基础构建包：

```text
pip 25.0.1
packaging 26.2
setuptools 82.0.1
wheel 0.47.0
```

## 环境更新命令

Mesa 4 开发版要求 Python `>=3.12`。如果需要重新准备环境，使用：

```bash
mamba install -n Fincept -c conda-forge -y python=3.12
```

如果升级过程中 `pip` 缺失，恢复：

```bash
"${FINCEPT_ENV}/bin/python" -m ensurepip --upgrade
```

安装本地 Mesa 4：

```bash
"${FINCEPT_ENV}/bin/python" -m pip install -e "${MESA_REPO}"
```

## 架构边界

当前保留两条路径：

- Mesa/Python 路径：保留为研究原型、Mesa 接入验证和实时浏览器路径。
- C++ 高性能路径：实现当前 Mesa 对应的 5 类机器人，直接调用 C++ 交易所内核，不走 Python subprocess。
- C++ 旧 research/emergence 模块继续保留为 baseline/regression test。

新增的 C++ 入口：

```text
build-fincept/lobx_step_exchange
```

它只接受 Python 发送的命令：

```text
DEPOSIT,user,asset,amount
BOOK,levels
BALANCE,user,asset
ORDER,user,order_id,side,price,qty,flags,ts
CANCEL,user,order_id,ts
FLUSH
STOP
```

Python 封装位于：

```text
python/lobx/mesa_exchange.py
python/lobx/mesa_model.py
```

C++ 对应机器人实现位于：

```text
cpp/include/lobx/simulation/mesa_agent_sim.hpp
cpp/src/simulation/mesa_agent_sim.cpp
cpp/apps/lobx_mesa_agent_simulator.cpp
```

## 构建 C++ step exchange

在仓库根目录运行：

```bash
cd LobXChange
cmake --build build-fincept --target lobx_step_exchange
```

## 运行 Mesa smoke

使用 `Fincept` 环境运行：

```bash
cd LobXChange
PYTHONPATH=python "${FINCEPT_ENV}/bin/python" -m lobx.cli mesa-smoke \
  --steps 80 \
  --seed 7 \
  --makers 4 \
  --noise 6 \
  --momentum 2 \
  --mean-reversion 2 \
  --whales 1 \
  --output /tmp/lobx_mesa_smoke_summary.json
```

可直接运行模块入口：

```bash
PYTHONPATH=python "${FINCEPT_ENV}/bin/python" -m lobx.mesa_model --steps 80
```

## 运行 Mesa 实时 K 线窗口

启动实时浏览器服务：

```bash
cd LobXChange
bash scripts/run_mesa_realtime_window.sh
```

默认地址：

```text
http://127.0.0.1:8770
```

浏览器打开页面后会自动启动默认 Mesa 实时流；修改参数后点击 `Start` 会重启一轮新的机器人模拟。

也可以直接使用 CLI：

```bash
PYTHONPATH=python "${FINCEPT_ENV}/bin/python" -m lobx.cli mesa-realtime \
  --host 127.0.0.1 \
  --port 8770 \
  --build-dir build-fincept
```

页面会通过 SSE 接收 Mesa 机器人运行产生的事件：

```text
agent_mix
trade
candle
stats
```

K 线周期按 Mesa step 聚合，当前支持：

```text
1 step, 5 steps, 15 steps, 60 steps
```

浏览器中可以调整：

- 总 step 数，`0` 表示持续运行。
- 每个 step 的毫秒间隔。
- 随机种子。
- `MarketMakerAgent`、`NoiseTraderAgent`、`MomentumAgent`、`MeanReversionAgent`、`WhaleSweeperAgent` 数量。

## 当前机器人类型

`python/lobx/mesa_model.py` 与 C++ `mesa_agent_sim` 当前都实现了 5 类机器人：

- `MarketMakerAgent`：双边 post-only 做市。
- `NoiseTraderAgent`：随机方向，部分 IOC 主动成交。
- `MomentumAgent`：根据最近成交价格方向追涨杀跌。
- `MeanReversionAgent`：价格偏离参考价时反向交易。
- `WhaleSweeperAgent`：周期性大额 IOC sweep。

## 运行 C++ 高性能对应路径

构建 C++ agent simulator：

```bash
cd LobXChange
cmake --build build-fincept --target lobx_mesa_agent_simulator
```

直接运行：

```bash
build-fincept/lobx_mesa_agent_simulator \
  --steps 80 \
  --seed 7 \
  --makers 4 \
  --noise 6 \
  --momentum 2 \
  --mean-reversion 2 \
  --whales 1
```

也可以通过 Fincept Python CLI 调用：

```bash
PYTHONPATH=python "${FINCEPT_ENV}/bin/python" -m lobx.cli mesa-cpp-smoke \
  --build \
  --steps 80 \
  --seed 7 \
  --makers 4 \
  --noise 6 \
  --momentum 2 \
  --mean-reversion 2 \
  --whales 1
```

输出 JSONL 事件流：

```bash
build-fincept/lobx_mesa_agent_simulator --steps 20 --seed 7 --jsonl
```

事件类型包括：

```text
agent_mix
trade
candle
stats
```

C++ 路径对应 Mesa 当前行为语义，但不追求 Python `random` 的逐位一致；C++ 使用固定 seed 的 `std::mt19937_64`，保证 C++ 自身可复现。

## 输出说明

`mesa-smoke` 输出 JSON summary：

```json
{
  "accepted_orders": 390,
  "agent_count": 11,
  "agent_types": {
    "market_maker": 3,
    "mean_reversion": 1,
    "momentum": 1,
    "noise_trader": 5,
    "whale_sweeper": 1
  },
  "final_best_ask": 102,
  "final_best_bid": 100,
  "final_mid_price": 101.0,
  "mean_spread": 1.65,
  "rejected_orders": 70,
  "steps": 40,
  "trade_count": 126
}
```

判断接入可用的最低标准：

- `agent_count` 等于配置的机器人总数。
- `agent_types` 覆盖所有配置的机器人类型。
- `accepted_orders > 0`。
- `trade_count > 0`。

## 回归测试

保留并验证现有 C++ baseline：

```bash
ctest --test-dir build-fincept -R "lobx_(emergence_runner_tests|market_emergence_scenarios|agent_population_tests|research_runner_tests)$" --output-on-failure
```

这些测试用于确保旧 C++ research/emergence baseline 没有被 Mesa 接入破坏。

新增 C++ 对应机器人测试：

```bash
ctest --test-dir build-fincept -R "lobx_mesa_agent_sim_tests" --output-on-failure
```
