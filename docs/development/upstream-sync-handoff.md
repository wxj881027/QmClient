# DDNet 上游同步：交接说明

一页式交接。判定细节在 `docs/development/upstream-sync-log.md`（S1'…S44），
流程与口径在 `docs/development/upstream-sync-plan.md`，下一步方案在
`docs/development/upstream-manual-merge-plan.md`。

## 1. 当前状态（2026-09-24）

| 项 | 值 |
|----|----|
| 同步基线 | `7cf432eacb`（`origin/master`，含贡献者 PR #256 的 str_toint/浮点断言/linux 构建修复） |
| 同步链 | `sync/slice-1-base-engine`（`3c362185d6`）→ `sync/slice-9-ghost-cull`（`81f76d1e58`）→ `sync/slice-23-official-semantics`（`3f6b1fe9cc`），共 91 个提交 |
| 打开的 PR | [#263](https://github.com/wxj881027/QmClient/pull/263)（→master）、[#260](https://github.com/wxj881027/QmClient/pull/260)（→#263）、[#261](https://github.com/wxj881027/QmClient/pull/261)（→#260）；均 stacked |
| 记录线 | 分支 `sync/docs-and-tooling`（worktree `tmp/docs-wt`）承载文档与工具 |
| 本地验证 | 链尾 `run_cxx_tests` **3348 运行 / 3347 通过 / 1 环境跳过 / 0 失败**；`--target everything` 全量编译通过；`--mode full` 门禁：`strict_build` 通过 |
| CI | `check-style` / Linux / macOS / Windows / Android / Analyze(python) 全绿；`check-clang-tidy` 与 `check-clang-san` 是 **master 级既有红**（见 §4） |
| 量化差距 | 窗口内 1258 条上游提交的逐行覆盖率：基线 **42.0%** → 链尾 **44.6%**，本轮实追 **88 条**（ABSENT 522 → 434） |

## 2. 已完成（都有证据）

- **`src/base` 布局对齐**（slice-1）：采纳上游 `io/aio/net/os/process/bytes`，`system.h` 改聚合 shim，
  保留自有 qWave QoS / `send_errors` / 「任一 socket 成功即成功」语义；
- **客户端与引擎修复批次**（slice 2–9）：demo 三处防御修复、屏外实体与 ghost 裁剪、
  预测初始化 3 条、健壮性 9 条、nightly 7 条、PNG 读写 2 条；
- **服务器/工具/数据**（slice 10–23）：`/map` 优化、`GetOptionalFloat` 语义、`src/tools` 7 条
  （含 `map_convert_07` **use-after-free**、`map_replace_area` **double free**）、Unicode 15→17 数据表；
- **四项官方语义**（维护者拍板「改用官方」）：控制台引号参数也校验、windowed fullscreen 有边框、
  rescue 不覆盖 `m_DDRaceState`、超时恢复连接重置 snapshot；
- **验证补强**：PNG 往返 3 例、裁剪判定 6 例、钩子提示线 7 例、DB 浮点语义 2 例、
  地图工具功能冒烟 1 例（`dummy_map` 产物 CRC32/SHA256 自校验 → `map_convert_07`）；
- **门禁**：`/analyze` 对采纳的上游 `src/base` 的 13 条告警做「文件+警告码」精确豁免并列出理由；
- **流程工具**（`qmclient_scripts/support/`，均有单测，Python 全套 84 例通过）：
  基线快照 + **基线新鲜度**检查、内容判定与回退检查、量化差距、整链回退自审、**融合排序**。

## 3. 待办（按接手人分类）

**等合入后自动可做**
1. 贡献者 PR [#264](https://github.com/wxj881027/QmClient/pull/264) 合入 master 后：把三段 PR
   `git rebase --onto <新 master> 7cf432eacb sync/slice-23-official-semantics --update-refs`，
   预期 `check-clang-tidy` / `check-clang-san` 转绿（见 §4），届时 CI 无红项；
2. 三段 PR 合并顺序 1→2→3；PR-1 合并后 PR-2 的分支改基到 master（GitHub 会自动改基）。

**需要维护者拍板**
3. 零重叠里**不需额外批准又相关**的只剩 2–4 条（`6dcc46307` 爱沙尼亚语语言代码、
   `301eb091f` 删重复无用成员，加两条文档/元数据）—— 可摘可不摘；
4. 两条需批准的零重叠提交：`a23094c69`（根 CMakeLists 的 libpng 查找）、
   `2ecaaf638`（sixup snapshot ID 去重 workaround）；
5. editor 那 12 条零重叠（envelope editor 为主）是否单排一个切片；
6. 手工融合是否按 `upstream-manual-merge-plan.md` 的顺序开工（首选 `ui_scrollregion.cpp`）。

**必须实机**
7. windowed fullscreen 外观变化（有边框，换取 Windows 截图工具可用）、rescue 后 DDrace 状态、
   断线重连后 snapshot 基线；渲染裁剪与 PNG 的观感（清单见 `upstream-sync-verification.md` §4）。

## 4. 两个「既有红」的归属（别再重复排查）

`Check clang-tidy` 与 `Check ASan & UBSan` 在 master 的 PR #256 合并**之后**两次 push 运行都是 failure，
与本同步链无关：

- `check-clang-tidy`：`misc-use-internal-linkage`（上游/本地代码均有，我的链只多了
  `src/base/aio.cpp:14` 一条同类诊断）；
- `check-clang-san`：三份状态 `client-server: passed` / `tests: passed` / `integration: failed`，
  失败是 mastersrv/smoke 集成测试超时。

**PR #264 正在同时修这两处**（`.clang-tidy` 禁用该检查并修掉「注释行混进折叠块导致检查静默启用」，
以及给集成测试放宽超时 + smoke 客户端加 `qm_chat_hide_system_prefix 0`）。

## 5. 常用命令

```bash
# 基线与冲突快照（在同步 worktree 里跑；会附带「基线新鲜度」一行）
py -3 qmclient_scripts/support/upstream_status.py --conflicts
py -3 qmclient_scripts/support/upstream_status.py --clean-list      # 零重叠，区分独立可摘/需前置

# 量化差距
py -3 qmclient_scripts/support/upstream_coverage.py --repo tmp/sync-slice-1 --start <基线> --tip <链尾>

# 整链回退自审
py -3 qmclient_scripts/support/upstream_revert_audit.py --repo tmp/sync-slice-1 --base <基线> --tip <链尾>

# 融合排序（冲突块数越小越便宜）
py -3 qmclient_scripts/support/upstream_merge_survey.py --repo tmp/sync-slice-1 --path src/game/client --top 26 --file-conflicts

# 构建与测试（同步 worktree）
cmd /c qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target run_cxx_tests -j 12
py -3 qmclient_scripts/gate/check_gate.py --mode full
```

## 6. 踩过的坑（省下重复排查）

1. **worktree 里的 `ddnet-libs` 是 junction**：`git switch/checkout` 改内容时会因
   `ddnet-libs/../.git/modules/ddnet-libs` 直接 `fatal` 并留下半切换状态。
   可靠做法：用**临时干净 worktree**（`git worktree add --detach`）做改写；必须在 junction worktree
   内切分支时，先把 junction 挪走，切完按「junction 挪出 → 空目录改名 → junction 就位 → 删空目录」
   复位（删之前先断言目录为空，避免递归删除穿透 junction）。
2. **clang-format 20 与 22 要求互斥**：上游那条数组引用声明两侧都报错，已用 `// clang-format off/on`
   保护；CI 用 22，本地门禁是 20。需要权威结果时把 22 的 wheel 解到 `tmp/cf22` 直接调用（别装进环境）。
3. **GitHub 会连带关闭以被删分支为基线的 PR**，且不可重开 —— 删 stacked PR 的中间基线要格外小心
   （本次就因此重建了 PR #263）。
4. **动手修「既有失败」前先 `git fetch`**：维护者可能同一时间已修（S37 的教训，工具里已加基线新鲜度检查）。
5. **指标口径**：`零重叠` 会高估可摘量（改上游新文件的提交也算进来，摘了会 `DU`），
   一律用「独立可摘」一档；覆盖率是**下界**（改写式移植会低估）。
6. **新 worktree 跑仓库自带 Python 测试前**，先按同样方式接 `ddnet-libs` / `vendor/minhook` /
   `vendor/msdfgen` 的 junction，否则会有 9 例假失败。
