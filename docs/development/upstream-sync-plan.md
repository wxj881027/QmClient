# DDNet 上游跟进与切片同步计划

本文是 QmClient 跟进 DDNet 上游的流程与判定记录入口。基线数据用
`py -3 qmclient_scripts/support/upstream_status.py --conflicts`（只读，不改工作树）重新测量。

## 0. 当前状态速览（2026-09-24）

| 项 | 状态 |
|----|------|
| 同步基线 | `6b9a41fd21`（`origin/master`，含已合并的 PR #257/#258；本链此前基于 `aea1453cc7`，2026-09-24 前移） |
| 切片链 | `sync/slice-1-base-engine` … `sync/slice-23-official-semantics`（末端 `3b3d3265af`，约 78 条上游派生改动 + Unicode 17 数据表 + 四组自动化测试：PNG 往返 / 实体裁剪 / 钩子提示线可见性 / DB 浮点语义） |
| PR 状态 | 三段 stacked 均已推送并开 PR：PR-1 [#259](https://github.com/wxj881027/QmClient/pull/259)、PR-2 [#260](https://github.com/wxj881027/QmClient/pull/260)、PR-3 [#261](https://github.com/wxj881027/QmClient/pull/261) |
| 可选项分支 | `sync/opt-console-strict-args`（控制台引号参数严格化；该语义已并入链尾切片 23） |
| 验证口径 | 链尾 `run_cxx_tests` **3348 运行 / 3347 通过 / 1 环境跳过 / 0 失败**：基线前移后，原先 2 例维护者新 UI 源码断言随 `origin/master` 的源侧实现一起消失 → 既零回归也零失败 |
| 回退自审 | 61 条 `-x` 溯源，**0 条**把「上游已回退的改动」留在链上 |
| 工具行为验证 | S11 的「工具只有编译级证据」已补：`qmclient_scripts/tests/test_map_tools_smoke.py` 跑通 `dummy_map`（CRC32/SHA256 自校验）→ `map_convert_07` 全链路（2026-09-24 关闭） |
| 工作流工具 | `qmclient_scripts/support/`：`upstream_status.py`（基线/冲突干跑）、`upstream_commit_check.py`（内容判定 + 回退检查）、`upstream_coverage.py`（量化差距）、`upstream_revert_audit.py`（整链回退自审） |
| 文档 | 计划（本文）、`upstream-sync-log.md`（逐切片判定与证据）、`upstream-sync-verification.md`（待实机验证 + 门禁豁免登记）、`upstream-sync-pr-drafts.md`（PR 标题/正文/推送命令）、`upstream-inventory/`（四簇对照表）、`platform-feature-gaps.md`（平台功能差异） |
| 待维护者拍板 | ① 推 PR 的方式（三段 stacked 还是合一）② 四项曾挂起的语义已确认"采用官方"并在切片 23 落地 ③ §5 平台缺口是否要按平台隐藏无效设置项 |
| 待实机验证 | 渲染与 quad 裁剪、ghost 裁剪、PNG（含已按上游回退的 libpng 项）、7 项客户端行为自查；切片 23 的 windowed fullscreen 外观变化 |

**收益递减提示**：低风险可摘项基本吃完。剩余差距集中在三类，都需要维护者参与 ——
本地重写文件（`src/game/client` 的 scoreboard/chat/hud/menus/QmUi、`players.cpp`）、
依赖上游新特性的改动（rejoin 状态、MySQL SSL、128 人、hook teleins）、
协议/玩法/时序语义（snapshot、demo delta、rescue 状态）。

## 1. 目标与已确认决策

目标：在保留 QmClient 自有功能的前提下，分阶段把 DDNet 上游（当前正式版 20.0，master 已到 2026-09-22）
同步进来；官方的实现明显更优时，用官方替换或融合，而不是长期各写一套。

2026-09-23 与维护者确认的决策：

| 项 | 决定 |
|----|------|
| 同步方式 | 分阶段切片合并；不做一次性全量 merge（实测 300+ 冲突不可控），也不做「只 cherry-pick 少量功能」的永久落后 |
| 冲突默认策略 | 默认保留 fork 行为；逐项评估后个别采用官方实现，判定逐条记录 |
| 范围 | 客户端 demo、渲染与性能、预测/反 ping、UI/设置架构、HUD/scoreboard、editor、server/协议/snapshot |
| 范围边界 | editor 与协议/snapshot 类改动按仓库约束逐项单独批准；协议字段、物理、预测语义、demo/skin 格式、地图行为默认不动 |

## 2. 基线（2026-09-23 测量）

```
upstream        : ddnet/master (a9ddb8eda5, 2026-09-22)
上次同步点      : de609e845e 2025-11-27
领先 / 落后     : 3600 / 2117
本地改动文件    : 1656
上游改动文件    : 784
双方都改过      : 569
上游非合并提交  : 1258（其中 1187 触及 fork 也改过的文件，71 为零重叠）
干跑合并冲突    : 330 个路径（content 294 / add/add 24 / modify/delete 11 / submodule 1 / rename-delete 1）
冲突最集中      : src/game/client 64（components 占 48）、src/engine/shared 33、src/game/editor 30、
                  src/engine/client 26、src/game/server 22、data/languages 21、src/test 18
```

### 关键修正（S1 实测，2026-09-23）

1. **fork 的内容基线不是「19.9」**：HEAD 没有 19.9 就已存在的 base 模块化
   （`src/base/os.cpp` / `process.cpp` / `net.cpp` 在 19.9 存在、HEAD 仍是 `system.cpp` 单体），
   也没有 19.9 的 `src/game/editor/envelope_editor.cpp`。`aec0a1370`（2026-08-05「同步 DDNet 19.9」）
   两个 parent 都是 fork 本地分支，属于**选择性移植**而非上游 merge，所以 merge-base 停在 2025-11-27，
   2117 个「落后提交」里相当一部分是结构性重构，不是可选功能。
2. **「零重叠 ⇒ 可直接摘」只在双方文件布局一致时成立**：上游重构过的区域（base、backend、
   gameclient 资源、settings、server）必须先整区域对齐布局再逐提交搬运；此外 rename 映射
   （上游 `net.cpp` → fork `system.cpp`）的 cherry-pick 结果必须人工核对。
3. 逐提交 cherry-pick 只能做布局未变的小修（S1 实测 14 条里仅 3 条可落地），**不足以推进主线**。

冲突最重的 `src/game/client/components` 48 个文件里，`menus_demo.cpp`、`menus_settings*.cpp`、
`hud.cpp`、`scoreboard.cpp`、`chat.cpp`、`ghost.cpp`、`items.cpp`、`maplayers.cpp` 全部在列 ——
与本地定制区域完全重合，佐证了「先做低风险切片、UI/HUD 放到最后一次性收编」的顺序。

含义：

- 落后 2117 个提交、569 个文件双方都动过 —— 逐文件手工融合的成本远高于按区域切片；
- 零重叠提交（只碰 fork 从未改过的文件）通常可以直接 `cherry-pick -x`，是低风险存量；
- **零重叠 ≠ 一定能摘**：例如 editor envelope 系列只碰上游新增的 `envelope_editor.cpp`，
  但依赖仍是重叠的 `6d88c786a Rewrite envelope editor as editor component`，
  必须整组处理或直接跳过。每个候选提交都要实际试摘并编译验证。

历史同步点偏旧的根因：上次同步（`de609e845e`）之后 fork 累积了 3600 个自有提交，
其中 1656 个文件被改动，所以后续每次同步的冲突面都会随自有功能增加而扩大 —— 这也是
把 `menus_*`、设置页架构这类「每次同步都撞」的区域排在后面、优先做一次性收编的原因。

## 3. 切片顺序

| 切片 | 内容 | 风险 | 状态 |
|------|------|------|------|
| S0 | 跟进基线与工具（本文件 + `upstream_status.py`） | 无 | 完成 |
| S1 | **已完成（待评审/PR）**：`src/base` 布局对齐 —— 新增上游 `io/aio/net/os/process/bytes` 六个模块（net/os 取 master 版），删除 `system.cpp`，`system.h` 改为聚合 shim；保留 QoS、`send_errors`、`AnySuccess` 语义。实测：game-client 编译通过、C++ 测试零回归（45/45 与基线一致）、原 5 条 base 阻塞提交内容已入树 | 中（引擎核心） | 完成，见 `upstream-sync-log.md` |
| S2 | **已完成**：demo 三处防御修复 + 首批低风险吸收共 8 条落地（聊天草稿复位、表情抖动公式、`community_icons` 竞态、返回键断言、绑定比较器、demo marker/delta/ScanFile 校验）；1 条 DEFER（0.7 皮肤选中替代项依赖设置页拆分产物） | 低 | 完成，见 `upstream-sync-log.md` |
| S3 | 渲染与性能：quad clipping、backend、maplayers/particles、FPS 优化 | 中 | **部分完成**：屏幕外实体裁剪已落地（上游语义 + 本地基建）；`CScreenRect` 基建已补齐（slice-8）；玩家/ghost 与钩子提示线裁剪仍需手工融合（本地已分叉，且需实机验证） |
| S4 | 预测与反 ping | 中高（手感敏感） | **部分完成**：3 条不改变手感的初始化修复已落地（见 S3）；antiping 三态、激光门预测、input 提前仍挂起，等实机验证 |
| S5 | UI/设置架构 | 高 | **降级为 DEFER**：上游 `menus_settings_*` 拆分只是搬迁+顺手清理，本地同文件已叠约 5600 行自有实现；改为「新改某页时按上游命名落位」 |
| S6 | HUD / scoreboard | 高（本地定制最重） | 未开始 |
| S7 | editor | 中（大量重叠，需单独批准） | 未开始 |
| S8 | server / 协议 / snapshot 硬化 | 高（需单独批准） | 未开始 |

每个切片必须能独立回滚、独立验证，不与其他切片混在一个 PR 里。

> 编号说明：日志 `upstream-sync-log.md` 里的 `slice-N` 是**执行顺序**（分支名），
> 与本表的 `S0–S8`（区域划分）不是一一对应。

| 增量窗口 | 内容 | 风险 | 状态 |
|----------|------|------|------|
| N（20.0 之后的 nightly） | master 上 20.0 之后的 250 个非 merge 提交，按「小、可编译验证、不依赖本地缺失特性」筛选 | 低-中 | **部分完成**：已落 7 条（服务器保存/传送/限流/崩溃修复、`str_length` 循环、clang 构建、demo marker 防护）；其余按区域归入上表 |
| 量化差距（2026-09-23） | 窗口内 1258 条非 merge 提交的逐行覆盖率：同步前 42.1% → 链尾 44.4%，**实际追上 78 条** | — | 见 `upstream-sync-log.md` S18；缺口热点：editor（未纳入）、`src/game/client`（本地重写）、`src/base/unicode`（**已在 S19 同步**） |
| Unicode 数据 | **已完成（slice-16）**：Unicode 15.0.0 → 17.0.0，5 个文件与上游逐字节一致，生成链补全；`Str.Utf8CompConfusables` 与 name_ban 测试通过 | 低 | 完成，见 `upstream-sync-log.md` S19 |

### 结构差异地图（2026-09-23 实测）

自上次同步点以来：上游新增 **103** 个文件，fork 新增 **1034** 个文件；其中 **30 个路径双方都新增**
（fork 当年的手工移植与上游正式文件撞名，表现为 add/add 冲突）。

| 类别 | 文件 | 处理原则 |
|------|------|----------|
| add/add，fork 版=上游版+少量本地改动 | `src/base/{dbg,mem,secure,sphore,thread,time,windows}.{cpp,h}`、`crashdump.h`、`src/game/editor/quad_knife.*`、`src/game/server/interactions.*`、`scripts/check_standard_headers.py`、`scripts/generate_rust_bridge.py`、`ruff.toml` 等 | 以上游文件为基线，重放 fork 的本地小改动（差异多在 1–45 行内，编译可验） |
| add/add 且内容一致 | `data/mapres/easter_0.7.png`、`scripts/android/download_android_sdk.sh`、`src/base/secure.h`、`src/base/dbg.rs`、`src/test/dbg_test.cpp` | 直接取上游 |

上游新增的关键文件（对应后续切片）：

- S5 UI：`src/game/client/components/menus_settings_{general,graphics,sound,tee,tee7,language,player,appearance,credits,ddnet}.cpp`
- S3 渲染：`src/engine/client/backend_egl.{cpp,h}`、`backend_threaded.cpp`、`keyboard.{cpp,h}`；`src/game/client/` 下 12 个新文件
- 引擎网络：`src/engine/shared/http_curl.*`、`http_emscripten.*`（WASM 目标，QmClient 可跳过）
- S7 editor：6 个新文件（含 `envelope_editor.*`）
- S8 server：`src/game/server/interactions.*` 等 4 个

### S1 候选提交（原计划清单）

实测结论见 `docs/development/upstream-sync-log.md`：14 条候选里只有 3 条能干净落地，
其余因上游文件布局重构（base 模块化、backend 拆分、gameclient 资源重构、settings 拆分）
或三方合并冲突而失败。这份清单保留作为「布局未变的小修」候选池。

网络与基础：

- `44e7f84fa` base: Fix host lookup fallback for `host:port` hostnames
- `9931d2dc9` Improve `net_addr_is_local` and `net_host_lookup_fallback`
- `d3810ba51` net: Fix setting tos on ipv6/ipv4
- `c6757b72e` Don't stop receiving packets on empty datagrams
- `fb1d4b86d` Limit number of arguments in `process_execute`
- `8c131294a` Fix error handling when `execlp` fails after forking process

引擎与客户端：

- `adac08c6c` shared: Inline `CVariableInt::Pack`
- `5b147f6ac` Use safe array access to prevent out-of-bounds panic
- `6f941a85f` Fix wrong localization context used for graphics errors
- `c63b1b944` Use `std::size` instead of hard-coding size
- `ca698e390` Avoid duplicate code in `CComponent::LocalTime/time` functions
- `c32720d3d` Fix keyboard navigation in background map picker
- `3600493b9` Use `std::fill` in more cases for gameclient assets
- `ebbe3225b` Fix see-others by vote rate limit

地图工具（`src/tools`）：

- `04cbd07ce` / `71582a2fb` / `8c5153c41` `dummy_map` 工具修复
- `b75fe1f98` `map_diff`: Handle invalid tile layer data
- `2aa670ad9` `map_replace_image`: Fix loading png
- `bc2c2f930` `map_convert_07`: Fix use-after-free
- `7cf4372da` Fix double free of Windows command line in `map_replace_area`

需单独批准（协议相邻，先不动）：`d98e1e4ea`（0.7 skins 128 人 server info 尺寸）、
`2ecaaf638`（sixup snapshot 翻译 ID 重复）。

明确跳过：Emscripten 系列（`021dfd5c0`、`b611680ed`、`b53c14c05`、`a073f12ab`、`98ca8c95f`、
`8e57ffcb4`、`7c47712d5`、`ef5278e0b`、`fef0d2bda`、`76700d05e` 等）与 `944cf0c2a`/`2d670ac55`
（libtw2 vendor 撤销/重做）、纯文档与 `.mailmap` 类提交 —— QmClient 不做 WASM 目标，收益为零。

## 4. 工作流

1. **开工前**：工作树必须干净（用户自己的改动先提交或 stash）；跑一次
   `py -3 qmclient_scripts/support/upstream_status.py --conflicts` 存档基线。
2. **分支**：从 `master` 切 `sync/slice-<N>-<area>`，一个切片一个分支/PR。
3. **取上游改动**，按代价从低到高：
   - 零重叠提交：`git cherry-pick -x <hash>`（`-x` 留下来源记录）；
   - 重叠文件、官方整体更优：`git checkout <upstream> -- <path>` 后按 fork 需求补回；
   - 重叠文件、双方都要：手工融合，逐条记判定。
4. **判定记录**：每个文件/功能一条，写入 `docs/development/upstream-sync-log.md`。
5. **验证**：切片内改动完成后跑 `py -3 qmclient_scripts/gate/check_gate.py --mode default`；
   手感/渲染类切片必须补一次实机验证，并在日志里写清验证方式与结果。
6. **收口**：切片 PR 合入后更新本文件的状态列与基线数字。

## 5. 判定记录格式

`docs/development/upstream-sync-log.md` 每行一条：

| 路径 / 功能 | 上游提交 | 判定 | 理由 | 验证 |
|-------------|----------|------|------|------|
| `src/game/client/components/menus_demo.cpp` | `887299f3e` | KEEP-FORK | 本地 demo UI 分叉 1.6k 行，仅吸收校验逻辑 | 编译 + 手动打开损坏 demo |

判定取值：

- `KEEP-FORK`：保留本地实现，只吸收必要的官方修复；
- `TAKE-UPSTREAM`：官方实现明显更好，整段替换；
- `MERGE-BOTH`：双方能力都要，手工融合；
- `DEFER`：本轮不做，记录原因（依赖未合、需批准、收益不足）。

## 6. 前置条件与风险

- **工作树脏**：本计划建立时仓库有 16 个已改/已暂存文件（`src/base/fs.cpp`、`hud.cpp`、
  `qmclient/axiom_scores.cpp`、若干 test 等），合并前必须先落定。
- **子模块**：`ddnet-libs` 需要能取到上游提交，否则合并阶段无法完成（已确认可 fetch）。
- **构建口径**：Windows 用 `qmclient_scripts/cmake-windows.cmd`；开发期只跑 quick 门禁，
  提交前跑 `--mode default`；同一 build 目录内 `game-client`/`testrunner`/`run_*_tests` 串行。
- **最大风险**：S5/S6 —— 官方把设置菜单拆成 `menus_settings_*.cpp` 并重做了 scoreboard，
  而我们这两块都是重度定制；这两轮必须以「结构对齐、行为不变」为目标并逐页验证。

## 7. 收口与 PR 计划（当前本地分支链）

同步链是**线性堆叠**：每个切片从上一个切片分出，因此「推哪个分支」就等于「进到第几段」。
**基线已 rebase 到 `aea1453cc7`**（维护者的 `fix/qm-test-contract-alignment` 分支）：

| 段 | 分支（末端提交） | 内容 | 建议 PR |
|----|------------------|------|---------|
| 1 | `sync/slice-1-base-engine`（`8258823b45`） | `src/base` 布局对齐（12 文件 + 类型/改名 + QoS/统计保留） | **PR-1 地基** |
| 2–8 | `sync/slice-2-demo-and-lowrisk` … `sync/slice-8-screenrect` | demo/预测/裁剪/健壮性/nightly/PNG/CScreenRect | **PR-2 客户端修复** |
| 9 | `sync/slice-9-ghost-cull`（`cb1305025b`） | ghost 屏外裁剪 | PR-2 末尾 |
| 10 | `sync/slice-10-server-smallfix` | 服务器/控制台 3 条 | **PR-3 服务器与工具** |
| 11 | `sync/slice-11-tools`（`e4914db96e`） | `src/tools` 7 条（含 UAF/double free） | 同上 |
| 13 | `sync/slice-13-sweep-smallfix` | 全窗口扫描后再落 7 条（time/图形/quad 裁剪/hot_reload 泄漏/websockets/信封点钳制/connlimit） | 同上 |
| 14 | `sync/slice-14-sweep-b` | 扫描第二批 9 条（切换编号边界/截图 alpha/shader OOB/libpng 版本/datafile 卸载/assert unlikely/add_map_votes/截图错误日志/死成员） | 同上 |

- 可选分支（**不在同步链上**，等维护者决策）：`sync/opt-console-strict-args`（`018cced509`）
- 回归对照（2026-09-23）：链上 2 例失败 = 纯基线 `aea1453cc7` 的 2 例失败（均为维护者新 UI 的源码断言）
  → **零回归**；详见 `upstream-sync-log.md` 的 S13。
- 推送策略：逐段评审就按 PR-1 → PR-2 → PR-3 依次提（stacked，后一个在前一个合并后 rebase）；
  想一次合就在 `sync/slice-11-tools` 上压成 2–3 个提交提单个 PR。
- 判定工具：`py -3 qmclient_scripts/support/upstream_commit_check.py <commit>` 可事前判断某条上游提交
  在本地是否**已存在**（防重复插入）**以及是否被上游回退过**（防把已撤销的改动当成果），
  判定口径与局限见 `docs/development/upstream-sync-verification.md`。
- 待验证项见 `docs/development/upstream-sync-verification.md`。
- **PR 标题/正文/推送命令已起草**：`docs/development/upstream-sync-pr-drafts.md`（含逐段复验证据表）。
- 逐段复验（2026-09-23）：PR-1 / PR-2 / PR-3 三个边界的失败集合都等于纯基线 `aea1453cc7` 的 2 例 → 逐段零回归。

## 8. 参考

- 上游下载与 changelog：<https://ddnet.org/downloads/>
- 状态脚本：`py -3 qmclient_scripts/support/upstream_status.py [--clean-list] [--conflicts] [--json tmp/upstream-status.json]`
- 脚本分层说明：`qmclient_scripts/scripts_overview.md`
