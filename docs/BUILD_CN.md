# CMake 编译指南

## 环境要求

- Linux、WSL2 或 macOS。
- CMake 3.21 或更高。
- GCC 11+ 或 Clang 14+，需要 C++20。
- Ninja 可选；没有 Ninja 时可使用默认 Makefiles。
- Python 3.12+ 仅用于 Python 工具和绘图，不是 C++ 内核的编译依赖；版本下限以 `pyproject.toml` 为准。

Ubuntu/WSL 可安装：

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build python3 python3-venv
```

## 标准 Release 构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
```

没有 Ninja：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
```

脚本入口：

```bash
BUILD_TYPE=Release JOBS=4 bash scripts/build_cmake.sh
```

设置 `RUN_TESTS=0` 可以只编译：

```bash
RUN_TESTS=0 bash scripts/build_cmake.sh
```

## Debug 构建

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j 4
```

## 外部撮合簿依赖

默认使用仓库内：

```text
third_party/limit-order-book
```

也可以切换到完整上游 checkout：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLOB_REPO=/absolute/path/to/limit-order-book
```

## 运行测试

全部默认测试：

```bash
ctest --test-dir build --output-on-failure
```

核心 Agent/accounting 测试：

```bash
ctest --test-dir build \
  -R 'lobx_agent_runtime_tests|lobx_agent_price_impact_tests|lobx_exchange_accounting_tests|lobx_agent_pnl_invariants_tests' \
  --output-on-failure
```

长时间实验默认不会执行其大规模部分，只有设置环境变量才运行：

```bash
LOBX_RUN_LONG_DIAGNOSTIC=1 \
LOBX_DIAG_SCENARIOS=bounded \
LOBX_DIAG_AGENT_COUNT=20 \
LOBX_DIAG_STEPS=5000 \
LOBX_DIAG_OUTPUT_DIR=build/diagnostic_runs \
./build/lobx_agent_long_diagnostic_tests
```

## 主要可执行文件

```text
lobx_simulator                     基础订单回放与 K 线演示
lobx_realtime_simulator            实时 NDJSON 仿真进程
lobx_step_exchange                 单步交互式 exchange
lobx_mesa_agent_simulator          Mesa/AgentRuntime 机器人仿真
lobx_research_runner               参数扫描、多 seed 与研究导出
lobx_bench_exchange                exchange benchmark
lobx_bench_agents                  agent benchmark
lobx_bench_events                  event benchmark
lobx_agent_price_impact_tests      价格冲击实验
lobx_agent_long_diagnostic_tests   长时间诊断实验
```

## 常见问题

`LOB_REPO does not contain...`：确认 `third_party/limit-order-book` 已完整上传，或者显式传入 `-DLOB_REPO`。

编译器不支持 `std::span`：升级到完整支持 C++20 的 GCC/Clang。

绘图缺少依赖：

```bash
python3 -m pip install pandas matplotlib
```

公开发布前还应确认 `third_party/limit-order-book/UPSTREAM.md` 中记录的上游许可证要求。
