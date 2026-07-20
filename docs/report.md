

  ## 1. 项目一句话概览

  这是一个基于本地 LOBX C++ 交易所内核的机器人市场仿真项目，当前交易不连接外部 OKX 或真实交易所，而是在本地 Exchange / MarketEngine / order book 上撮合、结算和记录事件。主性能路径已
  经是 C++ Exchange / Order Book + C++ Mesa Agents + C++ Simulation Loop + C++ benchmark/correctness。Python Mesa 机器人仍存在，但主要作为策略原型、实时窗口 glue code 和对照参考，不
  是当前性能目标。Realtime /events 当前只接受 engine=cpp，engine=python 或其他非法 engine 会返回 400。

  ## 2. 代码入口总览

  ### 2.1 C++ 仿真入口

  主要入口：

  - cpp/apps/lobx_mesa_agent_simulator.cpp
  - cpp/src/simulation/mesa_agent_sim.cpp
  - cpp/include/lobx/simulation/mesa_agent_sim.hpp

  CLI target：

  - lobx_mesa_agent_simulator

  启动方式：

  ./build-fincept/lobx_mesa_agent_simulator --steps 80 --seed 42
  ./build-fincept/lobx_mesa_agent_simulator --jsonl --steps 100 --seed 42 --sleep-ms 0

  参数在 lobx_mesa_agent_simulator.cpp::parse_args 设置：

  - --steps
  - --seed
  - --reference-price
  - --makers
  - --noise
  - --momentum
  - --mean-reversion
  - --whales
  - --jsonl
  - --sleep-ms

  默认配置在 MesaAgentSimConfig：

  - seed=42
  - steps=80
  - market_symbol="BTC-USDT"
  - reference_price=100
  - agent 默认数量 {4, 6, 2, 2, 1}
  - initial_quote=1000000000
  - initial_base=1000000
  - candle_intervals={1,5,15,60}

  核心流程在 MesaAgentSimulation::Impl::step()：

  ++now
  refresh_book()
  current_orders.clear()
  shuffle(agent order)
  for each shuffled agent -> agent.step()
  refresh_book()
  record spread
  collect new trades
  build MesaStepEvents {orders, trades, candles, stats}

  Exchange 在 mesa_agent_sim.cpp::bootstrap_exchange() 创建，发行 USDT/BTC，创建 BTC-USDT spot market，并关闭 memory event store。agent 在 Impl 构造函数里通过 add_agents<T>() 创建，
  随后给每个 agent deposit 初始资产。每 step 的 order decision 进入 MesaStepEvents.orders，成交进入 MesaStepEvents.trades，K 线由 StepCandleAggregator::update() 生成，指标由
  Impl::stats() 生成。

  ### 2.2 Exchange / Order Book 入口

  主要文件：

  - cpp/include/lobx/exchange.hpp
  - cpp/src/exchange.cpp
  - cpp/include/lobx/market_engine.hpp
  - cpp/src/market_engine.cpp
  - vendored order book：third_party/limit-order-book/cpp/include/lob/book_core.hpp
  - vendored price levels：third_party/limit-order-book/cpp/include/lob/price_levels.hpp

  核心入口：

  - Exchange::submit_limit(...)
  - Exchange::cancel(...)
  - Exchange::topN(...)
  - Exchange::drain_trades()
  - Exchange::drain_candles()
  - MarketEngine::submit_limit(...)
  - MarketEngine::simulate_fill(...)
  - MarketEngine::cancel(...)
  - MarketEngine::topN(...)

  撮合在 MarketEngine::submit_limit() 调用外部 lob::BookCore::submit_limit()。撮合产生的 raw fills 由 CollectingLogger::log_fill() 收集，再进入：

  - MarketEngine::settle_fill(...)
  - MarketEngine::settle_spot_fill(...)
  - MarketEngine::settle_perpetual_fill(...)

  cancel 路径：

  - Exchange::cancel(...)
  - MarketEngine::cancel(...)
  - MarketEngine::cancel_order(...)
  - BookCore::cancel(...)
  - release_and_erase(...) 释放锁定余额并删除 open order

  trade 记录：

  - MarketEngine::submit_limit() 返回 SubmitResult.trades
  - Exchange::submit_limit() 将 trades 推入 trade_history_
  - KlineAggregator::on_trade() 生成 closed candles

  best bid/ask/snapshot：

  - MarketEngine::best_bid()
  - MarketEngine::best_ask()
  - MarketEngine::topN(...)
  - Exchange::topN(...)

  ### 2.3 Realtime /events 入口

  server 文件：

  - python/lobx/mesa_realtime_server.py
  - HTML：web/mesa_realtime.html

  路由：

  - MesaRealtimeHandler.do_GET()
  - /events -> MesaRealtimeHandler.serve_events(...)
  - 参数解析 -> parse_run_params(...)

  默认 engine：

  - parse_run_params("") 默认 engine=cpp

  engine=cpp path：

  /events
    -> parse_run_params()
    -> MesaRealtimeManager.start()
    -> MesaRealtimeManager._run()
    -> _run_cpp()
    -> subprocess: build-fincept/lobx_mesa_agent_simulator --jsonl ...
    -> read JSONL line
    -> _emit([event])
    -> SSE: data: {"type":"batch","seq":...,"events":[...]}

  非法 engine：

  - engine=python 或 engine=fast 在 parse_run_params() 抛 ValueError("only engine=cpp is supported for /events")
  - serve_events() 捕获后 send_error(400, ...)

  SSE / JSON event：

  - C++ JSON 生成函数在 cpp/src/simulation/mesa_agent_sim.cpp：
      - mesa_agent_mix_json
      - mesa_trade_json
      - mesa_step_candle_json
      - mesa_step_stats_json

  - Python SSE batch 包装在 MesaRealtimeManager::_emit()

  注意：MesaStepEvents.orders 内部包含 agent 决策，但当前 realtime JSONL 只输出 agent_mix、trade、candle、stats，未发现把每个 MesaOrderEvent 序列化到 /events 的实现。

  ### 2.4 Benchmark 入口

  CMake target 均在 CMakeLists.txt 定义：

  - lobx_bench_exchange -> cpp/bench/bench_exchange.cpp
  - lobx_bench_agents -> cpp/bench/bench_agents.cpp
  - lobx_bench_mesa_agent_sim -> cpp/bench/bench_mesa_agent_sim.cpp
  - lobx_bench_events -> cpp/bench/bench_events.cpp

  它们都是自写 benchmark，可执行文件直接输出 JSON。

  ### 2.5 脚本入口

  script/run_cpp_correctness.sh

  bash scripts/run_cpp_correctness.sh

  用途：

  - build lobx_cpp_correctness_tests
  - build lobx_mesa_agent_sim_tests
  - 用 ctest 跑这两个测试
  - 额外跑 Python unittest：python/tests/test_realtime_cpp_endpoint.py

  成功判断：

  - shell set -euo pipefail
  - build、ctest、unittest 任一步失败即失败

  script/run_cpp_benchmarks.sh

  bash scripts/run_cpp_benchmarks.sh

  默认输出目录：

  artifacts/cpp_benchmarks/<timestamp>

  可用环境变量：

  - BUILD_DIR
  - OUT_DIR
  - EXCHANGE_ORDERS
  - AGENT_ITERATIONS
  - EVENT_STEPS
  - SIM_CASE

  输出：

  - bench_exchange.json
  - bench_agents.json
  - bench_mesa_agent_sim_<case>.json
  - bench_events.json

  script/compare_bench_baseline.py

  python scripts/compare_bench_baseline.py --baseline <baseline_dir> --current <current_dir> --mode warning
  python scripts/compare_bench_baseline.py --baseline <baseline_dir> --current <current_dir> --mode fail

  用途：

  - 对比 benchmark JSON
  - throughput 下降、latency/RSS 上升超过阈值时 warning/fail
  - 同 case 下 accepted_orders / rejected_orders / trade_count 改变也会报 golden change

  ## 3. 功能模块地图

  ### 3.1 Exchange 模块

  职责：

  - asset/market 创建
  - 账户充值
  - limit/IOC/FOK/POST_ONLY/STP/reduce-only 等订单校验与提交
  - 撮合调用
  - spot/perp 结算
  - event/trade/candle 记录
  - cancel 与资源释放

  核心类：

  - Exchange
  - MarketEngine
  - AccountLedger
  - RiskEngine
  - PositionEngine
  - EventStore
  - KlineAggregator

  支持订单模式：

  - lob::NONE
  - lob::IOC
  - lob::FOK
  - lob::POST_ONLY
  - lob::STP
  - 项目扩展：LOBX_REDUCE_ONLY

  撮合规则：

  - 外部 BookCore 负责价格优先与 FIFO
  - IOC/FOK 不入簿
  - POST_ONLY 跨价由 risk check 拒绝
  - 成交价来自 resting order，这在 matching 测试中明确验证

  结算规则：

  - spot 买方用 quote，卖方用 base
  - resting order 锁定资源随成交减少
  - IOC/FOK 剩余资源释放
  - taker fee 进入 dedicated fee account
  - perp 走 margin/position 逻辑

  关键不变量：

  - best_bid <= best_ask
  - IOC 剩余量不能入簿
  - POST_ONLY 不能主动成交
  - 成交价来自 resting order
  - ledger invariant 必须成立
  - 账户余额不能为负
  - rejected order 不能改变 wallet/book/trades
  - cancel 后锁定资产释放

  ### 3.2 Order Book 模块

  本项目通过 CMake 嵌入外部 LOB：

  - ${LOB_REPO}/cpp/src/book_core.cpp
  - ${LOB_REPO}/cpp/src/price_levels.cpp

  本地包装在 MarketEngine：

  - lob::PriceLevelsSparse bids_
  - lob::PriceLevelsSparse asks_
  - std::unique_ptr<lob::BookCore> book_
  - std::unordered_map<OrderId, OpenOrder> open_
  - std::unordered_set<OrderId> seen_order_ids_

  bid/ask 存储：

  - sparse price levels
  - bid best 为最高价
  - ask best 为最低价

  FIFO：

  - 由 BookCore / price level order node 维护
  - 本地测试验证同价 FIFO、cancel 后 FIFO、rollback 后 FIFO

  cancel 定位：

  - BookCore::cancel(order_id)
  - 本地 open_ 同步删除
  - release_and_erase(order_id) 释放锁定余额

  snapshot：

  - MarketEngine::topN(side, levels) 返回聚合深度
  - MarketEngine::make_snapshot() 用于 submit/cancel rollback
  - Exchange::topN(...) 对外暴露

  ### 3.3 C++ Mesa Agents 模块

  5 类 agent 都在 cpp/src/simulation/mesa_agent_sim.cpp::MesaAgentSimulation::Impl 内部定义。

  MarketMakerAgent

  - 每 step 下两笔单
  - side：一买一卖
  - price：last_price() - spread 和 last_price() + spread
  - spread：随机 2..4
  - qty：随机 2..4
  - flags：POST_ONLY
  - 不主动依赖 trade history，只用 last_price()
  - 测试：MM001..MM008

  NoiseTraderAgent

  - 每 step 下一笔单
  - side：rand_unit() < 0.5 买，否则卖
  - aggressive 概率：0.35
  - aggressive 用 IOC
  - 非 aggressive 用 POST_ONLY
  - qty 固定 1
  - price：
      - buy aggressive：mid + 8
      - buy passive：mid + price_offset
      - sell aggressive：mid - 8
      - sell passive：mid - price_offset

  - price_offset 随机 -4..4
  - 差异说明：描述里说“随机买卖；约 35% 激进 IOC，否则 POST_ONLY”正确；但实际 sell passive 是 mid - price_offset，当 offset 为负时价格会高于 mid。
  - 测试：NT001..NT005

  MomentumAgent

  - 依赖最近两笔成交价
  - 少于两笔成交不行动
  - last > previous：买
  - last < previous：卖
  - last == previous：不行动
  - BUY price：last + 12
  - SELL price：last - 12
  - qty：2
  - flags：IOC
  - 测试：MO001..MO007

  MeanReversionAgent

  - 依赖 last_price() 与 agent reference price
  - 偏离绝对值 < 3 不行动
  - last 高于 reference：卖
  - last 低于 reference：买
  - SELL price：last - 4
  - BUY price：last + 4
  - qty：2
  - flags：IOC
  - 测试：MR001..MR007

  WhaleSweeperAgent

  - 仅在 now % 12 == 0 行动
  - side：随机买/卖
  - BUY price：last + 25
  - SELL price：last - 25
  - qty：20
  - flags：IOC
  - 测试：WH001..WH008

  ### 3.4 Simulation Loop 模块

  位置：

  - cpp/src/simulation/mesa_agent_sim.cpp::MesaAgentSimulation::Impl::step()

  执行顺序：

  step + 1
    -> refresh_book()
    -> clear current_orders
    -> std::shuffle(order, rng)
    -> agent.step(model)
    -> submit_action()
    -> Exchange.submit_limit()
    -> MarketEngine.submit_limit()
    -> BookCore matching
    -> settlement
    -> append trades/trade_prices/current_orders
    -> refresh_book()
    -> record spread
    -> build new_trades
    -> StepCandleAggregator.update()
    -> stats()
    -> MesaStepEvents

  ### 3.5 Price Dynamics / Metrics 模块

  位置：

  - MesaAgentSimulation::Impl::last_price()
  - best_bid()
  - best_ask()
  - current_spread()
  - mean_spread()
  - stats()
  - summary()
  - tests：cpp/tests/test_price_dynamics.cpp

  记录字段：

  - last_price
  - best_bid
  - best_ask
  - spread
  - accepted_orders
  - rejected_orders
  - trade_count
  - agent_count
  - mean_spread

  volume：

  - step volume 在测试中通过 events.trades 求和
  - candle volume 在 StepCandleAggregator 中累加

  signed flow：

  - 代码里没有产品化指标字段
  - 测试 PX009SignedOrderFlowUsesAggressorDirection 用 trade.liquidity_side == Ask ? +qty : -qty 临时计算

  whale impact：

  - 没有专门 production 指标
  - 测试 PX010WhalePriceImpactWindowIsDefined 用 step 11/12/13 的价格和 volume 观测

  ### 3.6 Golden Baseline 模块

  文件：

  - tests/golden/cpp_mesa_seed_42_20_steps.json
  - tests/golden/cpp_mesa_seed_42_100_steps.json
  - tests/golden/cpp_mesa_seed_42_1000_steps.json

  内容：

   文件                                steps    agents    accepted    rejected    trade_count
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━  ━━━━━━━━  ━━━━━━━━━━  ━━━━━━━━━━  ━━━━━━━━━━━━━
   cpp_mesa_seed_42_20_steps.json         20        15         270          34             88
  ──────────────────────────────────  ───────  ────────  ──────────  ──────────  ─────────────
   cpp_mesa_seed_42_100_steps.json       100        15        1319         177            411
  ──────────────────────────────────  ───────  ────────  ──────────  ──────────  ─────────────
   cpp_mesa_seed_42_1000_steps.json     1000        15       13009        1791           3933

  重要差异：

  - golden JSON 文件存在。
  - 但当前 cpp/tests/test_mesa_agent_sim.cpp 是硬编码这些 baseline 数值进行断言，未发现测试代码直接读取 tests/golden/*.json。

  ## 4. 测试内容总览

  lobx_cpp_correctness_tests 包含：

  - cpp/tests/test_order_book.cpp
  - cpp/tests/test_exchange_matching.cpp
  - cpp/tests/test_exchange_settlement.cpp
  - cpp/tests/test_mesa_agents.cpp
  - cpp/tests/test_mesa_agent_sim.cpp
  - cpp/tests/test_price_dynamics.cpp
  - cpp/tests/test_realtime_events.cpp

  额外 Mesa sim 测试 target：

  - lobx_mesa_agent_sim_tests
  - source：cpp/tests/simulation/mesa_agent_sim_tests.cpp

  ### 4.1 Exchange correctness tests

  cpp/tests/test_exchange_matching.cpp 覆盖：

  - empty book
  - POST_ONLY buy/sell 入簿
  - buy IOC 按 resting ask 成交
  - sell IOC 按 resting bid 成交
  - IOC 无成交和部分成交不入簿
  - POST_ONLY crossing buy/sell 拒绝
  - buy sweep 最低 ask，价格优先，同价 FIFO
  - sell sweep 最高 bid，价格优先
  - cancel success / missing cancel noop
  - fresh engine reset

  ### 4.2 Settlement tests

  cpp/tests/test_exchange_settlement.cpp 覆盖：

  - 买方 quote 减少、base 增加
  - 卖方 base 减少、quote 增加
  - quote/base 总量守恒
  - 现金不足拒绝且无状态变化
  - 持仓不足拒绝且无状态变化
  - 多笔成交后账户非负
  - cancel 释放资源后可再次下单
  - sweep 多 maker 独立结算
  - rejected order 不改变 wallet/book/trades

  ### 4.3 Order Book tests

  cpp/tests/test_order_book.cpp 覆盖：

  - 单价格档聚合多个订单
  - bid 降序
  - ask 升序
  - 删除最后订单后移除 price level
  - best bid/ask cancel 后更新
  - depth snapshot 聚合每档数量
  - order id lookup cancel 后失效
  - bulk insert/cancel 后 book 与 open order index 一致

  ### 4.4 C++ Agent tests

  cpp/tests/test_mesa_agents.cpp：

  - MarketMakerAgent
      - 双边 POST_ONLY
      - price 围绕 reference/last price
      - qty 2-4
      - empty book 不主动成交
      - fixed seed reproducible

  - NoiseTraderAgent
      - side/flags/qty/seed reproducible
      - IOC 概率约 35%
      - IOC 可以成交
      - POST_ONLY 可以 rest

  - MomentumAgent
      - 最近两笔上涨 -> BUY IOC
      - 最近两笔下跌 -> SELL IOC
      - flat/cold start 不行动
      - price offset 为 +/-12
      - qty=2

  - MeanReversionAgent
      - 偏离 >= 3 tick 行动
      - 高于 reference -> SELL IOC
      - 低于 reference -> BUY IOC
      - 近 reference / missing price 不行动
      - price offset 为 +/-4
      - qty=2

  - WhaleSweeperAgent
      - 只在 12 的倍数 step 行动
      - qty=20
      - IOC
      - 有深度时 sweep
      - 浅簿时不 rest
      - fixed seed reproducible

  ### 4.5 Simulation tests

  cpp/tests/test_mesa_agent_sim.cpp 覆盖：

  - 初始化 15 agents 与 agent type counts
  - 每 step 发布 current book stats
  - shuffle fixed seed 可复现
  - always-acting agents 每 step 订单数正确
  - stats series 与 cumulative trade_count 一致
  - 0 agent / 1 agent smoke
  - 15 agents 20-step golden baseline
  - 100 agents 100-step smoke
  - same seed rerun produces same output
  - seed=42 / 100 和 1000 step baseline

  cpp/tests/simulation/mesa_agent_sim_tests.cpp 额外覆盖：

  - C++ Mesa agent population 与 Python Mesa kind 对齐
  - C++ Mesa agents produce orders/trades/candles
  - same seed deterministic
  - summary JSON shape
  - zero agents

  ### 4.6 Price Dynamics tests

  cpp/tests/test_price_dynamics.cpp 覆盖：

  - price/spread/last trade series 稳定
  - best_bid <= best_ask
  - spread 非负
  - last price 有成交时等于最后一笔成交价
  - 无成交时用 book mid fallback
  - return series 无 NaN/Inf
  - per-step volume 等于 trades qty sum
  - signed order flow 临时计算
  - whale impact window 定义
  - maker-only spread bounded
  - noise-only 无强制方向漂移
  - momentum/mean reversion warmup 控制方向
  - whale 每 12 step volume spike
  - maker-heavy/noise-heavy/whale-heavy scenario statistics computable

  ### 4.7 Realtime event tests

  C++：

  - cpp/tests/test_realtime_events.cpp

  覆盖：

  - SSE frame 以 data:  开头，以 \n\n 结束
  - stats JSON 有 step/best_bid/best_ask/spread/trade_count/agent_count
  - trade JSON 有 price/qty/buyer/seller
  - candle JSON 有 open/high/low/close/volume
  - 1000 step event stream 不 hang
  - fixed seed event sequence reproducible
  - agent mix JSON 包含 C++ agent types

  Python：

  - python/tests/test_realtime_cpp_endpoint.py

  覆盖：

  - /events 默认 engine=cpp
  - 显式 engine=cpp 允许
  - engine=python 禁止
  - 非法 engine 禁止
  - steps/sleep_ms/reference_price/makers minimum clamp

  ## 5. Benchmark 内容总览

  ### 5.1 lobx_bench_exchange

  源文件：

  - cpp/bench/bench_exchange.cpp

  默认参数：

  - --orders 100000
  - --seed 42

  场景：

  - 50% POST_ONLY 入簿
  - 35% IOC
  - 10% cancel
  - 5% topN snapshot
  - 混合 workload

  输出 JSON 字段：

  - benchmark
  - elapsed_ms
  - total_orders
  - accepted_orders
  - rejected_orders
  - trade_count
  - orders_per_sec
  - trades_per_sec
  - cancels_per_sec
  - snapshot_per_sec
  - submit_latency_p50_ns
  - submit_latency_p95_ns
  - submit_latency_p99_ns
  - submit_latency_max_ns
  - rss_mb

  适合判断：

  - exchange submit/cancel/snapshot 混合吞吐
  - submit latency p50/p95/p99
  - 撮合与账本结算综合瓶颈

  未发现：

  - 单独拆分 POST_ONLY 入簿、IOC 未成交、IOC 成交、多档 sweep 的独立子 benchmark。

  ### 5.2 lobx_bench_agents

  源文件：

  - cpp/bench/bench_agents.cpp

  默认参数：

  - --iterations 100000
  - --seed 42

  场景：

  - 分别创建单个 agent kind simulation
  - 每类 agent 重复 sim.step()
  - 额外测 std::shuffle 1k/10k agents 成本

  输出字段：

  - benchmark
  - iterations
  - agent_steps_per_sec
  - orders_generated
  - decision_latency_p50_ns
  - decision_latency_p95_ns
  - decision_latency_p99_ns
  - shuffle_1k_agents_ns
  - shuffle_10k_agents_ns
  - agent_breakdown

  适合判断：

  - agent 决策成本
  - agent 类型差异
  - shuffle 成本

  注意：

  - 顶层 decision_latency_* 使用 results.front()，即 market maker 的 latency，不是所有 agent 汇总 percentile。

  ### 5.3 lobx_bench_mesa_agent_sim

  源文件：

  - cpp/bench/bench_mesa_agent_sim.cpp

  默认参数：

  - --case tiny
  - --seed 42

  支持 case：

  - tiny
  - small
  - medium
  - large
  - huge-agents
  - maker-heavy
  - noise-heavy
  - whale-heavy
  - momentum-heavy
  - meanrev-heavy
  - 也支持 --steps / --agents 覆盖

  输出字段：

  - engine
  - case
  - steps
  - agents
  - seed
  - elapsed_ms
  - steps_per_sec
  - orders_per_sec
  - trades_per_sec
  - accepted_orders
  - rejected_orders
  - trade_count
  - total_volume
  - final_best_bid
  - final_best_ask
  - spread_mean
  - spread_p95
  - spread_p99
  - max_book_depth
  - rss_mb

  适合判断：

  - 完整 C++ simulation loop 性能
  - steps/sec、orders/sec、trades/sec
  - 不同 agent mix 下的瓶颈
  - depth 增长与 RSS

  ### 5.4 lobx_bench_events

  源文件：

  - cpp/bench/bench_events.cpp

  默认参数：

  - --steps 1000
  - --depth-levels 20
  - --seed 42

  场景：

  - 每 step 执行 C++ sim
  - 获取 top depth snapshot
  - 生成 stats JSON
  - 生成 book JSON
  - 生成 trade JSON
  - 生成 candle JSON
  - 包装为 SSE data: ...\n\n

  输出字段：

  - benchmark
  - steps
  - depth_levels
  - elapsed_ms
  - events_per_sec
  - bytes_per_sec
  - snapshot_latency_p50_us
  - snapshot_latency_p95_us
  - json_latency_p95_us
  - avg_event_size_bytes
  - max_event_size_bytes

  适合判断：

  - event serialization 成本
  - full/top depth snapshot 成本
  - SSE payload size 和 bytes/sec

  注意：

  - realtime server 当前未发现发送 book depth JSON；bench_events 测的是更重的事件路径，适合评估未来 full book snapshot 成本。

  ## 6. 如何运行

  ### 6.1 构建

  项目 CMake 默认 LOB_REPO 是：

  third_party/limit-order-book

  推荐：

  cmake -S . -B build-fincept -DCMAKE_BUILD_TYPE=Release
  cmake --build build-fincept -j

  如果外部 LOB 路径不同：

  cmake -S . -B build-fincept -DCMAKE_BUILD_TYPE=Release -DLOB_REPO=/path/to/limit-order-book
  cmake --build build-fincept -j

  ### 6.2 跑 C++ correctness

  bash scripts/run_cpp_correctness.sh

  等价核心 ctest：

  cmake --build build-fincept --target lobx_cpp_correctness_tests lobx_mesa_agent_sim_tests
  ctest --test-dir build-fincept -R 'lobx_cpp_correctness_tests|lobx_mesa_agent_sim_tests' --output-on-failure
  python -m unittest discover -s python/tests -p 'test_*.py'

  ### 6.3 跑 benchmark smoke

  bash scripts/run_cpp_benchmarks.sh

  更小 smoke：

  EXCHANGE_ORDERS=1000 AGENT_ITERATIONS=1000 EVENT_STEPS=100 SIM_CASE=tiny \
  bash scripts/run_cpp_benchmarks.sh

  指定输出目录：

  OUT_DIR=/tmp/lobx_cpp_bench_smoke bash scripts/run_cpp_benchmarks.sh

  ### 6.4 对比 benchmark baseline

  python scripts/compare_bench_baseline.py --baseline <baseline_dir> --current <current_dir> --mode warning
  python scripts/compare_bench_baseline.py --baseline <baseline_dir> --current <current_dir> --mode fail

  ### 6.5 跑单个 target

  ./build-fincept/lobx_cpp_correctness_tests
  ./build-fincept/lobx_mesa_agent_sim_tests

  ./build-fincept/lobx_bench_exchange --orders 100000
  ./build-fincept/lobx_bench_agents --iterations 100000
  ./build-fincept/lobx_bench_mesa_agent_sim --case tiny
  ./build-fincept/lobx_bench_events --steps 1000

  ./build-fincept/lobx_mesa_agent_simulator --steps 80 --seed 42
  ./build-fincept/lobx_mesa_agent_simulator --jsonl --steps 100 --seed 42 --sleep-ms 0

  Realtime server：

  python -m lobx.mesa_realtime_server --host 127.0.0.1 --port 8770 --build-dir build-fincept

  打开：

  http://127.0.0.1:8770

  ## 7. 当前项目的主数据流

  C++ 主路径：

  C++ Agent
    -> MesaOrderEvent decision
    -> MesaAgentSimulation::Impl::submit_action()
    -> Exchange::submit_limit(...)
    -> MarketEngine::submit_limit(...)
    -> lob::BookCore::submit_limit(...)
    -> CollectingLogger raw fills
    -> MarketEngine::settle_fill(...)
    -> AccountLedger / PositionEngine
    -> TradeEvent
    -> Exchange trade_history_ / KlineAggregator
    -> MesaStepEvents {orders, trades, candles, stats}
    -> Golden / Tests / Benchmark JSON / CLI JSONL

  Realtime /events：

  /events request
    -> parse_run_params(engine=cpp)
    -> MesaRealtimeManager::_run_cpp()
    -> subprocess lobx_mesa_agent_simulator --jsonl
    -> C++ simulation step
    -> JSONL event: agent_mix / trade / candle / stats
    -> Python batch JSON
    -> SSE data frame
    -> browser frontend

  当前不应把实时前端 K 线视为主性能路径。更稳妥的研究/性能路径是：C++ 仿真保存 summary / step_metrics / trades，之后 replay 生成 K 线或前端视图。

  ## 8. 当前已经完成什么，还缺什么

  ### 已完成

  - 本地 C++ exchange/order book 撮合与结算
  - C++ Mesa 5 类 agent
  - C++ simulation loop
  - C++ correctness target：lobx_cpp_correctness_tests
  - C++ Mesa sim target：lobx_mesa_agent_sim_tests
  - seed=42 golden baseline 文件与测试断言
  - realtime /events 只支持 engine=cpp
  - 4 个 C++ benchmark target
  - benchmark run 脚本与 baseline compare 脚本
  - CTest label 已有基础分层，包括 correctness;cpp_exchange;cpp_agents;price_dynamics;realtime_cpp

  ### 风险点

  - golden baseline 很脆弱：agent 行为、RNG 调用顺序、撮合实现、账户拒绝规则微调都会改变 accepted/rejected/trade_count。
  - golden JSON 未被测试直接读取，文件和硬编码断言可能漂移。
  - benchmark 对 build type 敏感，Debug build 结果意义有限。
  - bench_events 测了 depth JSON，但 realtime 当前不发 book depth，二者不是完全同一条路径。
  - logging/export I/O 未发现独立 benchmark。
  - /events 通过 Python subprocess 转发 C++ JSONL，不应进入主性能闭环。
  - full book snapshot 可能很重，bench_events 已提供初步观测，但还缺真实 replay/logging 场景。
  - bench_agents 顶层 decision latency 只取 market maker，不是 mixed agents 的总体 percentile。

  ### 建议下一步

  1. 增加 C++ 持久化输出：summary.json、step_metrics.csv/jsonl、trades.csv/jsonl。
  2. 增加 replay kline generator，用保存的 trades/step_metrics 离线生成 K 线。
  3. 增加 logging/export benchmark，单独测 JSONL/CSV/Parquet 或当前目标格式的 I/O。
  4. 增加 repeated-run reproducibility test：同 binary 同 seed 连跑 N 次比较 summary 与 event hash。
  5. 让 golden 测试直接读取 tests/golden/*.json，避免文件与断言漂移。
  6. benchmark 产物目录加入 environment.json：compiler、CMake build type、CPU、OS、commit hash、LOB_REPO commit。
  7. 细化 CTest label：correctness / smoke / slow / benchmark / realtime。
  8. 给 lobx_bench_exchange 拆子场景：pure post-only、pure IOC no-fill、pure IOC fill、multi-level sweep、cancel-only、snapshot-only。
  9. 明确 realtime event contract：是否需要序列化 MesaOrderEvent agent decisions；如果需要，补 JSON 函数和 tests。
  10. 建立 benchmark baseline 目录规范，例如 benchmarks/baselines/<date>_<commit>/。

  ## 9. 10 行以内记忆版总结

  主路径：C++ Mesa Agents -> C++ Exchange -> C++ OrderBook -> Settlement -> Metrics/Events。
  交易不是接 OKX，而是本地 BTC-USDT spot 仿真。
  C++ 仿真入口：cpp/apps/lobx_mesa_agent_simulator.cpp。
  核心 loop：cpp/src/simulation/mesa_agent_sim.cpp::Impl::step()。
  交易所入口：Exchange::submit_limit -> MarketEngine::submit_limit -> BookCore。
  /events 在 python/lobx/mesa_realtime_server.py，只支持 engine=cpp。
  correctness 聚合 target：lobx_cpp_correctness_tests。
  bench targets：exchange / agents / mesa_agent_sim / events。
  golden 在 tests/golden，但测试当前硬编码 baseline 数值。
  Python Mesa 保留为原型参考，不是性能目标。
