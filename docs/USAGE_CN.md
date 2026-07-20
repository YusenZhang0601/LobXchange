# 使用指南

以下命令假设已经按 `docs/BUILD_CN.md` 编译到 `build/`。

## 1. 基础交易所演示

使用内置订单路径：

```bash
./build/lobx_simulator --quiet
```

加载 CSV 并导出成交/K 线：

```bash
./build/lobx_simulator \
  --orders examples/orders_price_path.csv \
  --trades-out build/trades.csv \
  --candles-out build/candles.csv \
  --quiet
```

订单 CSV：

```text
ts,user,order_id,side,price,qty,flags
1000000001,200,2001,BID,101,15,NONE
```

## 2. 多 Agent 仿真

运行 legacy Mesa 路径：

```bash
./build/lobx_mesa_agent_simulator \
  --steps 200 \
  --makers 4 \
  --noise 6 \
  --momentum 2 \
  --mean-reversion 2 \
  --whales 1
```

运行解耦 AgentRuntime：

```bash
./build/lobx_mesa_agent_simulator \
  --use-agent-runtime \
  --steps 200 \
  --seed 42 \
  --makers 4 \
  --noise 6 \
  --momentum 2 \
  --mean-reversion 2 \
  --whales 1 \
  --actions-out build/agent_actions.jsonl \
  --events-out build/simulation_events.jsonl
```

## 3. Price impact 实验

轻量 smoke：

```bash
LOBX_PRICE_IMPACT_OUTPUT_DIR=build/price_impact_runs \
./build/lobx_agent_price_impact_tests
```

显式启用较大实验：

```bash
LOBX_RUN_LARGE_PRICE_IMPACT=1 \
LOBX_PRICE_IMPACT_OUTPUT_DIR=build/price_impact_runs \
./build/lobx_agent_price_impact_tests
```

绘图：

```bash
python3 scripts/plot_price_impact.py \
  --input build/price_impact_runs \
  --output build/price_impact_plots
```

## 4. 长时间诊断实验

推荐使用 bounded 场景：

```bash
LOBX_RUN_LONG_DIAGNOSTIC=1 \
LOBX_DIAG_SCENARIOS=bounded \
LOBX_DIAG_AGENT_COUNT=20 \
LOBX_DIAG_STEPS=5000 \
LOBX_DIAG_SAMPLE_INTERVAL=10 \
LOBX_DIAG_OUTPUT_DIR=build/diagnostic_runs \
./build/lobx_agent_long_diagnostic_tests
```

绘图：

```bash
python3 scripts/plot_diagnostic_bundle.py \
  --input build/diagnostic_runs \
  --output build/diagnostic_plots \
  --downsample 5
```

诊断时优先查看：

- `accounting_summary.json`：`system_pnl_residual`。
- `inventory_consistency_summary.json`：库存方向/累计残差。
- `open_order_growth.csv`：订单是否无界增长。
- `perf_summary.json`：主要耗时阶段和吞吐。
- `agent_final_state.csv`：total-equity PnL，而不是 cash-only。

## 5. ResearchRunner

```bash
./build/lobx_research_runner \
  --scenario examples/research/spot_scenario.json \
  --sweep examples/research/spot_sweep.json \
  --seeds examples/research/seeds.json \
  --rank-bot mm \
  --metric net_pnl \
  --out build/research_bundle \
  --top-n 10 \
  --verbose
```

查看完整参数：

```bash
./build/lobx_research_runner --help
```

## 6. 实时页面

```bash
bash scripts/run_realtime_window.sh
```

浏览器访问：

```text
http://127.0.0.1:8765
```

该路径启动 C++ realtime simulator，并由 Python HTTP/SSE 服务连接到 `web/realtime.html`。

## 7. Python 工具

创建环境并安装：

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -e .
python3 -m pip install pandas matplotlib
```

可选 Mesa 依赖：

```bash
python3 -m pip install -e '.[mesa]'
```

生成演示配置：

```bash
lobx init-config build/demo_config.json
```

生成对称流动性阶梯：

```bash
lobx ladder --mid-price 100 --levels 5 --spread-bps 20 --qty 10
```

## 8. 结果解释注意事项

- Equity 应使用 `cash_total + inventory_total * mark_price`。
- 不要把 available balance 或 cash-only 变化当作总 PnL。
- long diagnostic 的数量可能是底层整数单位，先检查 `unit_sanity_summary.json`。
- bounded 与 legacy 改变了决策频率和订单寿命，适合性能比较，不适合直接做经济结论对照。
