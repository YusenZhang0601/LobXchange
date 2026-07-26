# LobXChange 项目状态与维护交接

> 状态快照：2026-07-26
>
> 本地基线：`test/transaction-fault-recovery-integration-test@4ad0de8`
>
> 用途：记录作者重构进入可见基线前，当前仓库可以确认的 Git、实现与验证状态。

本文是协作接手入口，不把本地分支实现写成作者仓库已经接纳的能力。稳定项目概览见 [README](../README.md)，代码能力边界见 [实现状态](IMPLEMENTATION_STATUS_CN.md)。

## 1. 本轮维护边界

本轮只做以下收束：

- 核对本地分支、远端、开放 PR 与当前 GitHub 权限。
- 重新配置和编译当前 HEAD，记录真实测试基线。
- 修正文档中的能力口径、不可移植链接、失效测试命令和状态混用。
- 保留作者重构到位后迁移测试所需的入口。

本轮明确不做：

- 不合并、rebase、cherry-pick 或改写历史。
- 不关闭、改 base、合并或新建 PR，不提交或推送。
- 不修引擎逻辑、失败测试、golden baseline 或并发实现。
- 不删除分支、`build/`、`.venv/`、日志、历史计划或未接线素材。
- 不增加安全加固、依赖升级、功能扩展或新的协作门槛。

## 2. Git 与 GitHub 现状

| 项目 | 2026-07-26 核验结果 |
| :--- | :--- |
| 作者仓库 | `origin = adv-skem/LobXchange` |
| 个人仓库 | `fork = YusenZhang0601/LobXchange` |
| 作者默认分支 | `origin/main@686633b`，远端仍是 `first commit` |
| 当前本地分支 | `test/transaction-fault-recovery-integration-test@4ad0de8` |
| 当前分支远端副本 | 与 `fork` 同名分支 SHA 一致，但本地未设置 upstream |
| 作者重构 | **未验证**：当前作者仓库没有可见的新分支或新提交，尚无可用于迁移测试的 commit SHA |
| 当前账号权限 | GitHub API/CLI 对 `adv-skem/LobXchange` 返回 `READ` / `push=false`；当前账号也没有待处理仓库邀请 |

最后一项只描述当前登录账号 `YusenZhang0601` 的实时 API 结果。若作者已发出协作者邀请，需要先确认邀请对应的账号、仓库和权限已经在 GitHub 生效，再决定是否改变 fork + PR 工作流。

## 3. 开放 PR 是累计堆叠关系

当前 5 个 PR 都以旧的 `origin/main@686633b` 为 base，但后一个分支包含前一个分支的累计提交：

| PR | Head | 累计范围 | 当前状态 |
| :--- | :--- | :--- | :--- |
| [#1](https://github.com/adv-skem/LobXchange/pull/1) | `feat/agent-taxonomy-and-placeholders@f040927` | 7 commits / 32 files | Open |
| [#2](https://github.com/adv-skem/LobXchange/pull/2) | `refactor/tech-debt-1-two-phase-validation@30e50d0` | 8 commits / 33 files | Open |
| [#3](https://github.com/adv-skem/LobXchange/pull/3) | `test/empty-book-liquidation-stress-test@763d5af` | 9 commits / 36 files | Open |
| [#4](https://github.com/adv-skem/LobXchange/pull/4) | `test/oracle-mark-price-protection-test@267ea72` | 11 commits / 37 files | Open |
| [#5](https://github.com/adv-skem/LobXchange/pull/5) | `test/transaction-fault-recovery-integration-test@4ad0de8` | 12 commits / 39 files | Open |

GitHub 当前把它们标为 `MERGEABLE`，但没有 checks。`MERGEABLE` 只说明 GitHub 暂时能计算合并，不等于改动已验证、应按当前顺序合并，或能直接套到作者的新重构上。

在作者提供新基线前，不自动关闭、重定向或压缩这些 PR。新基线到位后，应先和作者确定采用“依次审阅”“合并为一个变更集”还是“仅迁移仍有价值的测试”。

## 4. 当前代码能力口径

- 交易所核心、AgentRuntime 和研究工具的稳定概览以 [README](../README.md) 为入口。
- 5 种 Agent 已实现交易逻辑；另 5 种 Agent 只是已注册的 NOP 工位，详见 [实现状态](IMPLEMENTATION_STATUS_CN.md)。
- 4 种类型只有枚举/名称映射，尚未注册工厂实现。
- 当前分支包含热路径快照重构和多组测试，但这些改动尚未合入作者 `main`。

因此，不再使用“10 种策略均已实现”或“重构已合入作者代码库”这类口径。

## 5. 2026-07-26 本机验证快照

### 构建

```bash
cmake -S . -B build
cmake --build build -j 4
```

- **通过**：当前 HEAD 重新配置成功。
- **通过**：全部 CMake targets 编译成功，新增的三个集成测试源已实际编入 `lobx_integration_tests`。

### C++ 测试

```bash
ctest --test-dir build --output-on-failure
```

- **不通过**：42/44 个 CTest 聚合目标通过。
- **不通过**：`lobx_integration_tests` 内 81 个子用例有 6 个失败。
- **不通过**：`lobx_mesa_agent_sim_tests` 内 45 个子用例有 2 个 golden baseline 失败。
- **不通过**：`lobx_concurrency_tests` 被传入 `EXPLICIT_RUN_ONLY` 后实际输出 `ran=0 failed=0`；CTest 虽显示 Passed，但不能计作有效测试。

新增与关联测试的点名结果：

| 测试过滤器 | 结果 |
| :--- | :--- |
| `PerpLiquidationStress` | **通过**：2/2 |
| `OracleMarkPriceProtection` | **不通过**：1/2 |
| `TransactionFaultRecoveryIntegration` | **不通过**：0/5 |
| `MarketEngineTransactionFaultInjection` | **通过**：7/7 |

点名运行聚合集成测试时，应直接使用测试二进制的 suite 过滤器：

```bash
./build/lobx_integration_tests PerpLiquidationStress
./build/lobx_integration_tests OracleMarkPriceProtection
./build/lobx_integration_tests TransactionFaultRecoveryIntegration
./build/lobx_regression_tests MarketEngineTransactionFaultInjection
```

不要使用源文件名执行 `ctest -R`；当前 CMake 只注册聚合目标，这种命令可能“未找到任何测试但返回成功”。

### Python 测试

```bash
.venv/bin/python -m unittest discover -s python/tests -p 'test_*.py' -v
```

- **通过**：Python 3.12.13，5/5。
- **未验证**：现有 GitHub Actions 只运行 CMake/CTest，没有运行这 5 个 Python 测试。

历史日志中的远端 44/44 结果只代表当时工位和当时产物，不能覆盖本次 fresh build 的失败结果。

## 6. 作者重构到位后的接入顺序

1. 记录作者新基线的仓库、分支和完整 commit SHA。
2. 比较新基线与 PR #1–#5，按“仍适用、需改写、已被作者覆盖、应放弃”分类。
3. 先确定 PR 处理策略，再创建或调整承载分支；不要先批量 rebase。
4. fresh configure/build，重跑本页的 C++、Python 与点名测试。
5. 只为新基线仍存在的行为补测试；失败先区分“旧测试假设失效”和“作者重构引入回归”。
6. 验证通过后再更新计划状态、PR 说明和当前状态快照。

## 7. 文档与素材边界

- [pending_plans](pending_plans/README.md) 记录尚未在作者新基线上闭环的工作。
- [archived_plans](archived_plans/README.md) 是历史迭代记录，不自动等同于已进入作者 `main`。
- [开发日志](../log/README.md) 保留当时证据，不作为当前验证真源。
- [docs/report.md](report.md) 是首提交遗留的历史审计快照，其中存在已失效 target 名称，不作为当前入口。
- `tests/golden/*.json` 与 `experiments/price_impact/*.json` 当前没有代码消费者；在决定接线或删除前原样保留。

本页是下一轮接手时首先更新的状态入口。
