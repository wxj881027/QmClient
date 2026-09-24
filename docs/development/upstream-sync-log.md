# DDNet 上游同步记录

流程与判定口径见 `docs/development/upstream-sync-plan.md`。每个切片一节，记：目标、执行、结果、证据、结论。
基线数字用 `py -3 qmclient_scripts/support/upstream_status.py --conflicts` 复测。

## 2026-09-23 · 上游对照表（4 簇）与首批可落地清单

四份只读对照表（渲染/视觉/性能、预测/网络/手感、UI/设置、HUD/scoreboard/demo/聊天）已归档到
`docs/development/upstream-inventory/`，共 100 条判定 + 30 条不确定项。

### 上游内容覆盖核验（我方抽样，独立于子代理结论）

方法：取上游提交里最长的「新增行」作为内容指纹，在 `HEAD -- src` 中查是否已存在。

| 上游提交 | 日期 | 主题（抽样含义） | 指纹结果 |
|----------|------|------------------|----------|
| `e333184d88` | 2025-11-30 | 预测簇（子代理称已同步） | PRESENT |
| `c9e92cbac7` | 2025-12-28 | 预测簇（子代理称已同步） | MISSING |
| `14fc1e9d1e` | 2026-06-11 | 屏幕外实体裁剪 | MISSING |
| `ac566d06f5` | 2026-08-01 | 激光门预测解耦 | MISSING |
| `350d0398cc` | 2026-09-06 | demo marker 非法值 UB | MISSING |
| `f15d3b1f94` | 2026-09-12 | 计分板光标提示 | MISSING |

结论：**「上游内容已跟进到 2026-07」这类整体性说法不成立**，实际是「选择性移植、覆盖不均匀」，
6 例里 1 例确认已同步、1 例结论冲突（指纹法可能因本地改写而误判）。因此：

- 后续一切判定以**逐条内容核验**为准，`git cherry` 与标题比对都不可信（子代理独立发现 `git cherry`
  把已实现的 `b4f6206818` 记分板地图信息弹窗误判为「未应用」）；
- 保守基线仍是 `upstream_status.py` 的 merge-base 口径（2117 提交），不能按「只差 2.5 个月」乐观估算。

### 首批可落地清单（跨簇汇总，按风险从低到高）

| 项 | 上游提交 | 判定 | 风险 | 来源 |
|----|----------|------|------|------|
| `engine/shared/demo.cpp` 三处防御修复（marker UB / 无 full snapshot / ScanFile 崩溃） | `350d0398cc`、`887299f3e7`、`ca4363fe0c` | TAKE-UPSTREAM | 低（该文件未被本地重写） | hud-demo |
| 聊天草稿复位（发送/取消后 `m_EditingNewLine`） | `fcd79b9226` | TAKE-UPSTREAM | 低（2 行，需兼顾 `QmChatSaveDraft`） | hud-demo |
| 表情抖动公式 `sin(5*Wiggle)` → `sin(2*pi*Wiggle)` | `1cfa7f927a` | TAKE-UPSTREAM | 低（1 行，零冲突） | rendering |
| `community_icons` job 状态竞态（`Done()`+`Success()` → `State()==DONE`） | `45c684bc46` | TAKE-UPSTREAM | 低 | ui-settings |
| `ui.cpp` `DoBackButton` 旧分支写法（可触发 draggable 断言） | `624f58e477` | TAKE-UPSTREAM | 低 | ui-settings |
| 绑定比较器严格弱序 | `04dc39acb8` | TAKE-UPSTREAM | 低 | ui-settings |
| 0.7 皮肤删除后选替代项 | `3e7e479de1` | TAKE-UPSTREAM | 低 | ui-settings |
| `client: Handle input earlier`（输入提前，60Hz 约 16ms） | `178da1eade` | TAKE-UPSTREAM | 中（需实测与自有 fast input 叠加） | prediction |
| 预测初始化补全（`m_TuneZone`、`CLaser/CPlasma` 成员） | `8edc5a3c21`、`7820407734`、`c2a0621bb1`、`d62e5de553` | TAKE-UPSTREAM | 中 | prediction |
| 屏幕外实体/投影物裁剪（20.0 的 FPS 项） | `14fc1e9d1e`、`fabf09a3a3`、`061242fe47`、`ada53c8cb3` | MERGE-BOTH | 中高（自有渲染偏离极大） | rendering |
| 动态 antiping 玩家预测（`cl_antiping_players` 0..3 + bool→int） | `4af8164f26`、`b2dc98ca29` | MERGE-BOTH | 高（手感 + 与 `FastPractice.ForcePredict*` 重排） | prediction |
| 激光门预测解耦（关武器预测仍预测门碰撞） | `ac566d06f5`、`9d8a20bd0d` | TAKE-UPSTREAM | 高（碰撞语义） | prediction |
| 128 人记分板排版（本地固定 3 列 vs 上游 2×64 列） | `bc927c2a64` + 协议字段 `a9c6d2efdf` | DEFER（需设计 + 协议批准） | 高 | hud-demo |
| 设置菜单按上游拆成 `menus_settings_*.cpp` | `e30d6be289` 等 10 提交 | DEFER（本地 8260 行单文件，冲突大于收益） | 高 | ui-settings |

对计划的修正：

- **S5 从「高价值架构对齐」降级为 DEFER**：上游设置菜单拆分只是搬迁+顺手清理，而本地同文件已叠
  约 5600 行自有实现，按文件对齐的冲突面大于收益；改为「新改某页时按上游命名落位」。
- S2（demo）优先级上调：`engine/shared/demo.cpp` 三处防御修复是本次清单里性价比最高的一项。
- S4（预测/反 ping）确认是**最高风险簇**（9 项手感敏感），必须逐项实机验证后再落。
- S3（渲染）大多数上游提交无法直接摘，只能手工融合，且自有 `hud.cpp`(6842:1726)、
  `chat.cpp`(3561:1197)、`scoreboard.cpp`(2039:1161)、`menus_demo.cpp`(3253:1618) 都是本地整体重写。


## 2026-09-23 · S1'：`src/base` 布局对齐（已执行）

维护者批准后执行。目标：让 `src/base` 与上游文件布局一致，使后续上游提交可以直接搬运。

### 实际范围比计划大（计划外但必须做）

原计划只是「搬 5 个文件」，执行中被编译器牵出三层连带改动：

1. **base 文件缺口是 12 个**（不是 3 个）：`io.{cpp,h}`、`aio.{cpp,h}`、`net.{cpp,h}`、`os.{cpp,h}`、
   `process.{cpp,h}`、`bytes.{cpp,h}` —— 全部取自上游。
2. **`types.h` API 漂移**：上游拆分时把 `ASYNCIO`、`IO_MAX_PATH_LENGTH`、`CFsFileInfo` 等搬进了
   `types.h`（196 行 vs fork 117 行），并把 `io_seek` 的原点参数从 `enum ESeekOrigin`/`IOSEEK_*`
   改成 `enum class EIoSeekOrigin`。不做这一步，新 `aio.h` / `datafile.cpp` 根本编译不过。
3. **符号改名**：`shell_execute→process_execute`(3)、`kill_process→process_kill`(1)、
   `is_process_alive→process_is_alive`(1)、`open_link→os_open_link`(2)、`pid→process_id`(14)；
   另 `IOSEEK_*→EIoSeekOrigin::START/CURRENT/END`(30 处，跨 7 个文件)、`ESeekOrigin→EIoSeekOrigin`。

### 判定记录

| 项 | 判定 | 说明 |
|----|------|------|
| `io/aio/net/os/process/bytes` 六个模块 | TAKE-UPSTREAM | 直接采用上游文件，**net.cpp/net.h/os.cpp 取自 master**（顺带带入 `44e7f84fa`、`9931d2dc9`、`d3810ba51` 三条原计划要单独摘的缺失窗口修复），其余取 19.9 |
| `src/base/system.h` | MERGE-BOTH | 不删除，改写为**聚合 shim**（include 12 个新头 + QoS 声明）。理由：仓库有 **272 处** `#include <base/system.h>`，一次性改调用方会把切片撑爆；上游已无 system.h，后续可逐目录清理后删除 shim |
| `src/base/system.cpp` | TAKE-UPSTREAM（删除） | 内容已全部由新文件覆盖（函数级核验：上游 95 个顶层函数中 93 个在 fork 的 `system.cpp` 命中，剩余 8 个是改名 + QoS） |
| Windows QoS（`NETQOS_INTERNAL`/`SQwaveApi`/`net_qos_*`） | KEEP-FORK | 全仓唯一自有功能，抽出后并入 `net.cpp` 末尾（需要 `NETSOCKET` 内部结构，独立文件需额外访问器，故不单独建文件）；声明保留在 shim 里 |
| `send_errors` 统计 + 「任一 socket 成功即成功」语义 | MERGE-BOTH | 上游 master 的 `net_udp_send` 用单个 `d`，会丢掉「IPv4 成功但 IPv6 失败」的结果。按其结构补回 `AnySuccess`/`SuccessfulResult`，并在 `priv_net_udp_send_failed` 里计数 |
| IPv4/IPv6 TOS（IPv6 用 `IPV6_TCLASS`） | TAKE-UPSTREAM | fork 自己写的 `0x10 IPTOS_LOWDELAY` 版本被上游 master 的 `0xB8 IPTOS_DSCP_EF` 取代（行为变化，记录在案） |
| `CMakeLists.txt` BASE 列表 | 必须改 | `+12` 个上游文件、`-system.cpp`；`system.h` 保留 |

### 复用脚本（本次写的，均在 `tmp/`，便于复跑）

- `tmp/port_qos.py`：从 `HEAD:src/base/system.cpp` 抽取 QoS 块并入 `net.cpp`（带块边界断言）
- `tmp/slice1_apply.py`：符号改名 + CMake BASE 列表更新（带锚点断言，CRLF 安全）
- `tmp/slice1_types.py`：`types.h` 对齐 + `IOSEEK_*` 改名
- `tmp/slice1_net.py`：`send_errors` 与 `AnySuccess` 合并
- `tmp/check_fork_lines.py`：核对 fork 对 `system.cpp` 的 209 行新增是否都被新文件覆盖（最终残留 27 行，已逐条处理）

### S1' 构建与测试结果

| 检查 | 命令 | 结果 |
|------|------|------|
| 客户端编译 | `--build cmake-build-release --target game-client -j 12` | **通过**（194/194；格式修正后复跑 271/271，均链接出 `DDNet.exe`） |
| C++ 测试 | 同上 `--target run_cxx_tests -j 12` | **45 例失败，与切片前基线完全一致**（无新增、无消失），即零回归 |
| Rust 测试 | 同上 `--target run_rust_tests` | 通过（切片前已验） |
| quick 门禁 | `check_gate.py --mode quick` | 9 项通过；唯一失败是 worktree 下 `ddnet-libs` 用 junction 导致的「Git 子模块前置检查」（环境问题，非代码） |

执行中踩到并已修正的三个坑（都值得后续切片复用经验）：

1. **`types.h` 是隐藏依赖**：只搬 `aio.h`/`io.h` 会编译不过，因为 `ASYNCIO`、`IO_MAX_PATH_LENGTH` 等已搬到上游
   `types.h`，所以必须整份对齐后再补回 fork 自有声明。
2. **换行必须保持 LF**：仓库存储形态是 LF，而 PowerShell `Set-Content` / Python `write_text()` 默认会写成 CRLF。
   这不会体现在 `git diff` 里（autocrlf 归一化），却会让**读源码做断言的测试**失败 —— 本次因此出现 5 个
   `QmMonitoringHelpers.Assets*` 假失败，`tmp/slice1_fixup.py` 统一恢复 LF 后消失。
3. **上游文件的 clang-format 版本与本地 gate 不一致**：新增的 `io.cpp`/`net.cpp`/`os.cpp`/`process.cpp`
   直接违反本地格式检查，需跑一次 `qmclient_scripts/fix_style.py` 重排。代价是这些上游文件带上了
   本地格式差异，后续同步时要注意（不影响语义）。

### 执行事故与恢复（必须记录）

首次执行时出现两步失误，导致改动一度丢失，已按脚本重放恢复：

1. **`git add -A -- .` 静默失败**：worktree 下 `ddnet-libs` 用 junction 指向主仓库，git 递归子模块时报
   `fatal: not a git repository: ddnet-libs/../.git/modules/ddnet-libs`；该错误被 `2>$null` 吞掉，
   结果第一次提交（`72de324a1d`）**只包含 12 个新增文件**，26 个文件的修改没进提交。
2. **`git reset --hard` 冲掉了未提交改动**：在「验证阻塞提交现在能否 cherry-pick」的循环里，
   每次试验后用 `git reset --hard HEAD` 清场，把上一条尚未提交的 26 个文件改动一并丢弃
   （`system.cpp` 复活、`CMakeLists.txt` 回退、别名与 shim 全部消失）。

恢复方式：把当时写的 4 个脚本按序重放（`tmp/port_qos.py` → `slice1_apply.py` → `slice1_types.py`
→ `slice1_net.py` → `slice1_fixup.py`）+ `fix_style.py`，改动完整复现，并 `commit --amend` 成
单个完整提交 `9f3b4ced53`（33 文件，含 `system.cpp → net.cpp` 的重命名识别）。

流程教训（后续切片必须遵守）：

- worktree 内暂存一律用**显式路径**：`git add -A -- src CMakeLists.txt`，不要用 `.`；
- 执行 `reset --hard` 之前先确认 `git status --short --ignore-submodules=all` 为空；
- 每条验证性 `cherry-pick -n` 之后，用 `git reset -q --hard HEAD` 只在**工作区干净**时使用。

> 注：上一版记录里「原 S1 的 5 条阻塞项已解」的结论是在**混合状态**下测得的（首轮带改动、
> 后续轮次已被 reset 冲掉），已在恢复后重新验证，见下节。

### S1' 成果：原 S1 的阻塞项已解（恢复后重新验证）

用 `git cherry-pick -n` 逐个实测（随后 `reset --hard`），原 S1 里因「上游重构了文件布局」而失败的 5 条
base 相关提交现在**内容都已在树中**（本次直接采用 master 版 `net.cpp`/`os.cpp` 带入）：

| 上游提交 | 主题 | 现状 |
|----------|------|------|
| `fb1d4b86d` | Limit number of arguments in `process_execute` | 已在树中（空 cherry-pick） |
| `8c131294a` | Fix error handling when `execlp` fails after forking | 已在树中 |
| `44e7f84fa` | base: Fix host lookup fallback for `host:port` | 已在树中 |
| `9931d2dc9` | Improve `net_addr_is_local` / `net_host_lookup_fallback` | 已在树中 |
| `d3810ba51` | net: Fix setting tos on ipv6/ipv4 | 已在树中 |

仍未解（属客户端区域，留给后续切片）：`ca698e390`（`component.cpp`）、`3600493b9`（gameclient `assets.cpp`）。

提交：`eff90a0632 refactor(sync): 对齐 DDNet 上游 src/base 布局并保留自有 QoS/统计行为`
（单个完整提交，33 文件；分支 `sync/slice-1-base-engine`，worktree `tmp/sync-slice-1`）。



## 2026-09-24 · S31：三段边界在新基线上复测（全部零失败）+ 差距量化复测

### 1. PR 边界复测：三个边界都是 0 失败

S30 之后哈希又前移一次，文档里「PR-1/PR-2 待复测」的欠账在本轮补齐（同一 build 目录串行跑，
每个边界都是 `cmake-windows.cmd --build cmake-build-release --target run_cxx_tests`）：

| 边界 | 引用 | 运行 | 通过 | 跳过 | 失败 | 退出码 |
|------|------|------|------|------|------|--------|
| PR-1（`src/base` 布局） | `0956e8bebb` | 3329 | 3328 | 1（`QmWebSocketLive`，环境） | **0** | 0 |
| PR-2（客户端与引擎批次） | `6e1072062a` | 3330 | 3329 | 1（同上） | **0** | 0 |
| PR-3 / 链尾（服务器、工具、Unicode、四项官方语义） | `3b3d3265af` | 3348 | 3347 | 1（同上） | **0** | 0 |

所以「逐段零回归」这条验收口径现在升级为**逐段零失败**：新基线 `origin/master` 里那 2 例
维护者 UI 源码断言已经不再失败（S29 已说明原因），链上也没有引入任何新失败。

### 2. 差距量化复测：42.0% → 44.6%，本轮实追 88 条

用 `upstream_coverage.py` 在**新基线**上重测（窗口仍是 `de609e845e`(2025-11-27) … `ddnet/master`，
共 1258 条非 merge 提交；逐行命中是下界，改写式移植会被低估）：

| 引用 | 逐行命中 | ABSENT | ALREADY-PRESENT | PARTIAL |
|------|----------|--------|-----------------|---------|
| 新基线 `6b9a41fd21`（`origin/master`） | 23172/55108 = **42.0%** | 522 | 368 | 280 |
| 链尾 `3b3d3265af` | 24552/55108 = **44.6%** | 434 | 453 | 283 |

**本轮实际追上 88 条**（基线时非 ALREADY-PRESENT、链尾已 ALREADY-PRESENT），例如
`16189b741c` clang/antibot 构建、`1cfa7f927a` 表情抖动收尾、`498c8f8cee` `str_length` 循环、
`6a315ad54c` `pause_game` 崩溃、`7f5ac6ed07` PNG 逐行写、`32d5bef9ec` save 数组局部化、
`04dc39acb8` 绑定排序。

**剩余缺口热点（未命中/总数）**：`src/game/editor` 5036/8089、`src/game/client` 5020/6971、
`src/base/unicode` 4395/4454（数据表 .h→.cpp relocation，属已知低估）、
`src/engine/shared` 2135/3168、`src/game/server` 1829/3078、`src/engine/client` 1689/2706。

含义：`src/game/client` 与 `src/game/editor` 合计占未命中的大头，两者都是**本地整体重写/未纳入**
的区域 —— 与 S22「收益递减、剩下三类硬骨头」的结论一致：继续扫小提交的边际收益已经很低，
下一步要动的是「按区域手工融合 + 实机验证」，而不是继续 cherry-pick。

## 2026-09-24 · S30：推 PR 后的 CI 首轮反馈（clang-format 20 vs 22 冲突）与修复

三段 stacked PR 已推送并创建：

| PR | 分支 | 基线 | 链接 |
|----|------|------|------|
| PR-1 | `sync/slice-1-base-engine`（`0956e8bebb`） | `master` | https://github.com/wxj881027/QmClient/pull/259 |
| PR-2 | `sync/slice-9-ghost-cull`（`6e1072062a`） | PR-1 分支 | https://github.com/wxj881027/QmClient/pull/260 |
| PR-3 | `sync/slice-23-official-semantics`（`3b3d3265af`） | PR-2 分支 | https://github.com/wxj881027/QmClient/pull/261 |

### CI 首轮：`check-style` 失败，但**不是**本地门禁能抓到的原因

CI（`style.yml` → `check_gate.py --mode quick --ci-mode`）报：

```
src/engine/client/client.cpp:5540:42: error: code should be clang-formatted [-Wclang-format-violations]
CDemoRecorder (&CClient::DemoRecorders())[RECORDER_MAX]
```

定位过程与结论：

1. 这行是**上游原样声明**（`ddnet/master` 里逐字相同），且**不是**本次改动新增的行 ——
   只是 PR-1 改了 `client.cpp`（base 模块 include/符号改名 9 行），整文件因此进入格式检查，
   把一个潜在冲突暴露出来；
2. 本地门禁用的是 **clang-format 20.1.8**（CI 用 **22**）。为取得权威结果，把 clang-format 22
   的 wheel 解到 `tmp/cf22`（**只解包、未安装进环境**）后实测：**两个版本的要求互斥** ——
   - clang-format 20：`CDemoRecorder (&CClient::DemoRecorders())[RECORDER_MAX]` + 大括号另起一行
   - clang-format 22：`CDemoRecorder (&CClient::DemoRecorders()) [RECORDER_MAX] {`
3. 因此「按某一版重排」必然让另一侧报错。**上游代码不改写**，改为给该函数加
   `// clang-format off` / `// clang-format on`（两个版本都通过；用探针文件分别验证 rc=0）；
4. 顺手用 clang-format 22 扫了整条链改动过的 **101 个源文件**：**只有这一处**冲突（2 条 violation），
   所以这一步只需改一处、不需要逐 PR 试错。

### 顺带的工具坑：带 submodule junction 的 worktree 不能直接切分支

对 `tmp/sync-slice-1` 执行 `git switch` 时，git 会去读 `ddnet-libs/../.git/modules/ddnet-libs`
（junction 指向主仓库子模块，相对路径在 worktree 下失效）→ `fatal`，并且**留下半切换状态**
（HEAD 没动、工作区内容已变成目标分支），`--no-recurse-submodules`、`-c submodule.recurse=false`
都拦不住。

可靠做法（本轮采用，建议后续沿用）：

1. 用**临时干净 worktree** 做改写：`git worktree add --detach tmp/rebase-wt <branch>`
   （新 worktree 里 `ddnet-libs` 是空目录，不是 junction，切分支正常）；
2. 先把「占用了目标分支」的另一侧 worktree 脱离分支（`git -C <worktree> switch --detach`）；
3. 在临时 worktree 里改 + `git rebase --update-refs`，推完分支后 `git worktree remove`；
4. 若确实要在 junction worktree 内切分支：**先把 junction 改名挪走**，切完再挪回来；
   注意 git 会在原位建一个真目录，直接 `Move-Item` 回去会变成「目录套 junction」——
   正确顺序是「junction 挪出 → 空目录改名 → junction 就位 → 删除空目录」，且删之前先断言目录为空
   （避免递归删除穿透 junction 删到主仓库内容）。

### 证据（2026-09-24）

| 检查 | 结果 |
|------|------|
| CI `check-style`（PR-1，修复后） | **PASS**（2m49s；修复前同一 job FAIL） |
| `run_cxx_tests`（新链尾 `3b3d3265af`） | **3348 运行 / 3347 通过 / 1 环境跳过 / 0 失败**，退出码 0 |
| clang-format 22 全链扫描（101 个改动源文件） | **0 条 violation** |
| 本地 clang-format 20 门禁（同一文件） | 通过（修复前后都通过，故本地无法发现该冲突） |
| 三段分支强制推送 | `0956e8bebb` / `6e1072062a` / `3b3d3265af`，PR 已随更新重跑 CI |

**待观察**：PR-1 上的 Linux / Windows / macOS / Android / `check-clang-tidy` / `check-clang-san`
仍在跑（这几项耗时最长，Android job 上限 180 分钟）。

## 2026-09-24 · S29：四项语义改用官方实现 + 基线前移 + `/analyze` 豁免收口（已执行）

本切片（`sync/slice-23-official-semantics`）三件事：落地维护者新决策、把同步链基线前移到
`origin/master`、以及解决准发布门禁里 `/analyze` 的真实告警。

### 1. 维护者决策：四条挂起项全部「改用官方实现」

此前按「默认保留 fork 行为」把四条与本地冲突的上游改动挂起等拍板。本轮维护者明确选择
**采用官方实现**，逐条落地：

| 上游提交 | 内容 | 落地方式 |
|----------|------|----------|
| `821d5ae4b4` | 控制台引号参数也参与校验 | cherry-pick（干净）＋同步本地断言：`qm_modes_test.cpp` 的 `"invalid"` 由「接受」改为「不派发」 |
| `478781ad65` | windowed fullscreen 改回**有边框**（修 Windows 截图工具失效） | cherry-pick（干净）＋改写本地两条 `QmWindowModes` 源码断言 |
| `b9c39900a9` | rescue 不再覆盖 `m_DDRaceState` | cherry-pick（干净；本地无锁定旧行为的测试，只有 HUD 文案测试，不受影响） |
| `ed2b08f5d9` | 超时恢复连接时重置 snapshot | cherry-pick，**唯一冲突**：本地在 `SetTimedOut` 里多了 `OrigClientBrand` 与 `DelClientCallback` 两行 → 两边都保留 |

`478781ad65` 落地时被本地测试抓了一次：第一版断言写成「`IssueInit` 里不得出现
`INITFLAG_BORDERLESS`」，但纯窗口模式的 `qm` 无边框开关（`if(g_Config.m_GfxBorderless)`）
本来就该留着 —— 断言过宽，改为「不得再出现 `else // Windowed fullscreen` 分支」＋
「纯窗口无边框分支仍在」，才是这次语义变化的准确表述。

内容判定工具在这条上报了 **PARTIAL（18/21 行命中）**：该提交主要是把校验块从引号分支里搬出来
（缩进搬移），指纹法会把旧代码的行算成「已存在」——**PARTIAL 也可能是假阳性**，行为层面仍是缺失。
这是 `upstream_commit_check.py` 已知口径的又一例，已在工具文档里说明。

### 2. 门禁教训：`/analyze` 的 PASS 可能是「Ninja 没重编」造成的空心通过

第一次 full 门禁报 `MSVC /analyze 构建` **PASS**。但该检查自述「实际分析范围由 game-client
构建目标和 Ninja 增量状态决定」——`src/base/*` 自上轮跑过后没改动，ninja 直接跳过编译，
于是没有告警可报。把分析范围内的 80 个文件 touch 一遍重跑，暴露出 **13 条真实告警**：

| 文件 | 告警 |
|------|------|
| `src/base/aio.cpp(78)` | C6262（`aio_thread` 的 64KB 栈缓冲） |
| `src/base/io.cpp(106/120/134/137)` | C6308 `realloc` 可能返回 null |
| `src/base/io.cpp(139)` | C28182 同一路径的连带告警 |
| `src/base/os.cpp(51/160/167)` | C6011、C6388、C6387×2 |
| `src/base/net.cpp(922/1309/1389)` | C6011 |

全部落在 S1' 采纳的上游 `src/base` 模块里，属「上游本来就有、与本次同步无关」。
处理方式是**精确豁免 + 留理由**，而不是忽略整个检查：

- `strict_build.py` 新增 `_ANALYZE_UPSTREAM_BASE_ALLOWLIST`：按「文件 + 警告码」匹配，
  **只在 `/analyze` 阶段生效**，自有代码与其它文件仍会阻断（同警告码出现在 `hud.cpp` 或
  `src/base/unicode/*` 上不会被豁免，有单测锁定）；
- 命中的告警以 `INFO` 形式连同理由列进报告（不静默丢弃），本轮报出 13 条；
- 新增 gate 单测 5 例：命中 / 同码自有文件不豁免 / 同文件其它码不豁免 / 非 analyze 行不匹配 /
  条目必须带理由。

顺带记一条**可复现流程**：`/analyze` 是增量检查，只信它的 PASS 会误判；要么 touch 分析范围内的
文件强制重编，要么在报告里核对「触发范围」那一条的规模。

### 3. 基线前移：`aea1453cc7` → `6b9a41fd21`（`origin/master`）

之前一直记着「纯基线也失败 2 例，属维护者新 UI 的源码断言」。本轮查证发现：这 2 例**不是无解的
既有缺陷**，而是本地基线落后 `origin/master` 三个提交 —— 其中 `8b286754a2`（PR #258
「修复皮肤搜索网格的队列/收藏点击与双击应用」）正是那两个测试期待的源侧实现。

- 做法：`git rebase --onto 6b9a41fd21 aea1453cc7 sync/slice-23-official-semantics`，
  并用 git 2.54 的 `--update-refs` 一次性把 23 个切片分支全部搬到新基线；那 3 个提交只碰
  `menus_settings.cpp` / `tee_skin_apply.h` / `skins.h` / `QmLayoutTest.cpp`，整条链都没改过
  这四个文件，**rebase 零冲突**；
- 哈希（S30 加 clang-format 保护提交后的最终值）：`slice-1` `0956e8bebb`、`slice-9` `6e1072062a`、
  `slice-22` `662cbb192e`、链尾 `sync/slice-23-official-semantics` `3b3d3265af`；
- 主仓库分支 `fix/qm-test-contract-alignment` 同步前移到新基线上（文档/工具提交）。

> 注：S29 执行时的中间哈希（`5695b6fdeb` / `8755169bf0` / `1539bda58f`）已被 S30 取代，
> 上表给的是当前有效值。

**证据（2026-09-24）**

| 检查 | 结果 |
|------|------|
| `run_cxx_tests`（链尾 `1539bda58f`） | **3348 运行 / 3347 通过 / 1 环境跳过（`QmWebSocketLive`）/ 0 失败**，退出码 0 |
| `--mode full`（全量门禁） | **18 通过 / 3 警告 / 3 失败**；`CMake run_cxx_tests` 已 **PASS**；`/analyze` 的 13 条上游告警按豁免表放行并单列理由 |
| `--target everything`（Release 全量编译） | **通过**（`cmake-windows.cmd --build cmake-build-release --target everything -j 12`，316/316，退出码 0；`DDNet.exe` 16.7MB、`DDNet-Server.exe` 6.6MB、`testrunner.exe` 24.1MB、地图工具均重新产出） |
| gate 单测 | `qmclient_scripts/gate/tests/test_strict_build_analyze_allowlist.py` 5 例通过 |
| 平台审计 | 三条只读审计产出 `docs/development/platform-feature-gaps.md`（含抽检标注与 10 条不确定项） |

**剩余非零项（全部已定性，非本次改动引入）**

1. `Git 子模块前置检查`：worktree 的 `ddnet-libs` 是指向主仓库子模块的 junction，
   `.git/modules` 相对路径失效 —— 只在 worktree 复现，正常 checkout 不受影响；
2. `clang-tidy 前置检查`：本机 PATH 无 clang-tidy（CI 的 `clang-tidy.yml` 用 clang-tidy-22 跑，本地无法替代）；
3. `Check dilated images`：`data/input overlay-Zac/*.png`、`data/qmclient/chat_emojis/*.png` 未 dilate；
   整条链**没有改过 `data/`**（`git diff --stat <base>..<tip> -- data/` 为空）→ 既有问题；
4. `标识符命名检查解析失败`（WARN）：`extract_identifiers.py` 依赖的 clang python 绑定缺失，该检查设计上降级为 WARN。

**流程改进（写进判定口径）**：以后遇到「纯基线也失败的测试」，先确认本地基线是否已是
`origin/master` 的最新状态，再判断它是不是既有缺陷 —— 这次两者差 3 个提交，差点把维护者
已经修好的东西记成「已知失败」。

## 2026-09-24 · S28：地图工具的「行为未实测」gap 收口（`src/tools` 功能冒烟）

S11 留的 gap 原文是「工具只做到编译链接级验证，行为没实测」。本轮补上功能冒烟：
新增 `qmclient_scripts/tests/test_map_tools_smoke.py`（1 例）。

**做法**：在临时目录里跑 `dummy_map`，用工具**自报的** CRC32/SHA256 与实际产物比对（写-读回一致），
再把产物喂给 `map_convert_07` 走一遍完整转换（正好覆盖 S11 落地的 use-after-free 修复路径）。

- 产物路径：工具写到 storage 根的 `maps/dummy3.map`，Windows 上是 `%APPDATA%\DDNet\maps\dummy3.map`
  —— **这就是 S11 冒烟「未取得结论」的原因**：上游行为不是写进当前 CWD，临时 CWD 下看不到产物。
  测试改为直接读该固定路径，并在跑之前**备份**、跑完**恢复/删除**，不污染用户目录。
- 断言链：`dummy_map` 退出码 0 → 输出含 `Dummy map written` → 从输出解析出 CRC32/SHA256 →
  产物以 `DATA` 开头且 > 256 字节 → CRC32/SHA256 与工具自报一致 → `map_convert_07` 退出码 0
  且转换结果同样是 `DATA` 地图。工具未构建时 `skipTest`（跳过不算通过）。

**证据（2026-09-24）**

| 检查 | 结果 |
|------|------|
| `QM_MAP_TOOLS_BUILD_DIR=<worktree>/cmake-build-release` 下运行 | **1 例 PASS**（rc=0；即链尾源码的构建） |
| 同一测试指向主仓库 `cmake-build-release` | **FAILED**：那里的 `dummy_map.exe` 是 9/18 的旧构建，只 hex dump tile 数据、不报告写出与校验值 |
| `ruff check` / `ruff format --check` | `ruff check` 通过；格式不适用——`ruff.toml` 的 `include` 只含 `datasrc/`、`scripts/`，`qmclient_scripts/` 不在门禁范围（同级既有测试同样 4 空格缩进） |

**踩到的坑（值得记）**：测试一开始「莫名」失败在 `Dummy map written`，而把同样的命令单独写成 Python 片段
却能成功。原因是 `find_tool()` 在**主仓库**的构建目录里先找到了 9/18 的旧 `dummy_map.exe`，
测的根本不是当前改动。现在加了 `QM_MAP_TOOLS_BUILD_DIR` 覆盖开关（同步工作在 worktree 构建时用），
断言信息里带上实际使用的 exe 路径；并且如上表所示，这条测试**确实能区分新旧二进制**（旧构建必然失败），
不是空跑。副作用提醒：主仓库构建目录没更新时跑它必然失败——这不是回归，是「构建目录比源码旧」。

**结论**：S11 的行为 gap 关闭，`src/tools` 这条线从此有可重复证据。工具相关的剩余验证只有实机观感类
（已在 `upstream-sync-verification.md` 登记）。

## 2026-09-23 · S27：给 `GetOptionalFloat` 补测试，并做反向验证（测试确实能抓住原 bug）

S10 手工落地了上游 `d1c518a5f9`（`GetOptionalFloat` 误用 `GetInt`，会把小数截断），但当时没有测试。
本轮补上 `src/test/db_connection_test.cpp`（用 SQLite `:memory:` 连接，仓库既有的
`CreateSqliteConnection(":memory:", true)` 工厂）：

| 用例 | 断言 |
|------|------|
| `OptionalFloatKeepsFractionalPart` | `42.87` 经 `GetOptionalFloat` 仍为 42.87（`GetOptionalInt` 为 42，用于对照两种语义）；`NULL` → `nullopt`；整数值不受影响 |
| `OptionalFloatHandlesNegativeAndZero` | `-0.5` 与 `0` 按浮点语义返回 |

**反向验证（关键）**：把实现临时改回 `return GetInt(Col);`（制造原 bug）后重建，两例**都失败**
（`42.87 → 42`、`-0.5 → 0`）；恢复修复后两例通过。这证明测试断言的是真实行为差异，而不是「恰好通过」。

顺带记一个 API 细节：DB 层的列号是 **1-based**（sqlite 实现内部用 `Col - 1`），写这类测试时容易踩。

提交：`7ccc5e86f0 test(server): 补 GetOptionalFloat 的浮点语义测试`（分支 `sync/slice-22-db-test`；
测试文件已登记到 CMake 显式 `TESTS` 列表）。

**证据**

| 检查 | 结果 |
|------|------|
| `testrunner --gtest_filter=DbConnection.*` | 修复态 **2 例 PASS**；回退态 **2 例 FAIL** |
| `run_cxx_tests` | 通过 3345 例（较上轮 +2），失败 2 例 = 纯基线 → 零回归 |
| quick 门禁 | `fix_style.py` 处理后通过（仅 worktree junction 的子模块检查失败） |

## 2026-09-23 · S26：扩展钩子提示线可见性裁剪的边界用例

审计里「钩子提示线本地自有机制 vs 上游 `fabf09a3a3`/`061242fe47`，裁剪等价性需实测」一直是 DEFER 的理由。
本轮先把这块的**可判定部分**纳入回归网：`CQmHookCollVisibility` 是纯逻辑（`OnMapLoad` 扫描 tele 瓦片 +
`MayReachView` 距离判定），可以直接单测。

**先说清现状（避免夸大）**：这个类**此前已有 3 例**测试（在 `src/test/ddnet_19_9_sync_test.cpp`，
我先前按文件名过滤漏看了）。所以本轮是**扩展覆盖**，不是填补空白。

新增 `src/test/qm_hook_coll_visibility_test.cpp`（7 例）：

| 用例 | 断言的意图 |
|------|-----------|
| `ConservativeBeforeMapLoad` | 未加载地图时对任何位置都返回「可能可见」，绝不误裁 |
| `TeleHookInMapDisablesDistanceCulling` | 地图里有可用 `TILE_TELEINHOOK` 时不做距离裁剪；legacy 语义下该旗标不生效 |
| `DisabledTeleTilesAreIgnored` | `Number == 0` 的 tele 瓦片是禁用状态，不应让裁剪失效 |
| `LegacyTeleportFlagSelectsDetection` | 旧式 `TILE_TELEIN` 只有开启 legacy 语义才算数 |
| `CullsOnlyBeyondReach` | 只有超出 `Reach` 才裁；上下左右四向 + 屏幕内位置 |
| `LinePaddingExtendsReach` | 线宽余量会扩大保留范围 |
| `LongerHookKeepsMorePositions` | 钩长增大时保留范围单调增大 |

提交：`74d06bf6c3 test(client): 扩展钩子提示线可见性裁剪的边界用例`（分支 `sync/slice-21-hookcoll-test`；
新测试文件已登记到 CMake 显式 `TESTS` 列表）。

**证据**

| 检查 | 结果 |
|------|------|
| `testrunner --gtest_filter=QmHookCollVisibility.*` | **10 例全部 PASS**（3 例既有 + 7 例新增） |
| `run_cxx_tests` | 通过 3343 例（较上轮 +7），失败 2 例 = 纯基线 → 零回归 |
| quick 门禁 | `fix_style.py` 处理后通过（仅 worktree junction 的子模块检查失败） |

**意义**：上游这两条提交的「等价性」问题现在有了明确口径 —— 本地机制在「地图无可传送钩子时按距离裁剪、
有则完全不裁」这一点上被测试固定住；剩下的只是**观感**（提示线是否在视觉上等价）需要实机看一眼。

## 2026-09-23 · S25：对整条链跑准发布门禁 `--mode full`（结果与归属逐条定位）

此前只跑过 quick / default。本轮在链尾（slice-20，`d1ea0877d3`）跑仓库的**准发布门禁**：

| 门禁项 | 结果 | 归属 |
|--------|------|------|
| Git 子模块前置检查 | FAIL | **环境**：worktree 下 `ddnet-libs` 是 junction，git 找不到 `.git/modules/ddnet-libs` |
| MSVC `/analyze` 构建 | FAIL（19 条 warning） | **18 条为基线既有**（zlib 外部 10 条 + 本地自有头 `hud_media_island_logic.h`、`qm_chat_avatar.h`、`qm_skin_outline.h`，这些文件同步链从未改动）；**1 条由本轮同步引入**：`src/base/aio.cpp(78) C6262`（`aio_thread` 里 64KB 栈缓冲，来自上游 19.9 的 `aio.cpp`） |
| clang-tidy 前置检查 / 全量 .clang-tidy | FAIL / WARN | **环境**：PATH 中没有 clang-tidy（门禁自己降级为 WARN） |
| Check dilated images | FAIL | **基线既有**：失败项是 `data/input overlay-Zac/*.png` 与 `data/qmclient/chat_emojis/*.png`，同步链**从未改动**这些文件（`git diff` 与基线为空） |
| 标识符命名检查 | WARN | 环境：`extract_identifiers.py` 在当前编译环境下解析失败，门禁降级为 WARN |
| CMake run_cxx_tests | FAIL | 纯基线的 2 例既有失败（维护者新 UI 源码断言） |

**唯一需要处理的新增项**是 `aio.cpp` 的 C6262（64KB 栈帧）。两条路，各有取舍：

1. **不动上游代码，加白名单**：`refresh_allowlist.py` 是仓库既有的、需人工确认的维护动作
   （`check_gate.py --mode quick --report-json-path …` → `refresh_allowlist.py`）。保持与上游一致，
   后续合并 `aio.cpp` 不会产生额外冲突；
2. **本地改为不占栈**（如 `static thread_local` 或堆分配）：门禁干净，但偏离上游实现，
   以后上游改 `aio.cpp` 会有小冲突。

我倾向 1（同步优先保持与上游一致，且该栈帧运行在 `aio_thread` 的独立线程上，Windows 默认栈足够）；
但这属于维护决策，等你定。

**结论**：同步链在 `--mode full` 下**没有引入除 `aio.cpp` 栈帧告警之外的新问题**；
其余失败项要么是环境（缺少 clang-tidy、worktree junction），要么是基线既有（本地资产未 dilate、
本地自有头的 /analyze 告警、2 例测试断言）。

## 2026-09-23 · S24：把裁剪的边距与判定抽成可测逻辑（观感之外的部分进回归网）

S4/S8/S9 的屏幕外裁剪此前只有「编译 + 实机观察」两级证据。本轮按仓库既有约定
（`src/game/client/components/qmclient/*_logic.h` + `src/test/*_test.cpp`）把边距与判定抽出来：

- 新增 `src/game/client/components/qmclient/qm_item_culling_logic.h`：边距常量（投射物 ±1 tile、
  激光 ±0.5 tile、拾取物 x ±1.75 / y ±0.75 tile、ghost ±100）与点/线段判定（闭区间点判定、
  「线段四向不相交」判定）；
- `items.cpp`、`ghost.cpp` 改为调用该头（**语义不变**：同一组常量、同一套判定，只是从内联 lambda
  改为可复用函数）；
- 新增 `src/test/qm_item_culling_logic_test.cpp`：6 例——边距数值与上游一致、点判定在边界上闭区间、
  投射物 ±1 tile、拾取物非对称边距、激光只在整段完全位于某一侧之外才裁剪、ghost 200×200 盒。

提交：`d1ea0877d3 refactor(client): 裁剪边距与判定抽成可测逻辑并补单测`（分支 `sync/slice-20-culling-logic`）。

**顺带发现的构建约定坑（值得记）**：CMake 里的 `TEST`/`BASE` 源列表是**显式枚举**而非自动 glob，
新增测试文件必须登记进列表并重新 configure —— 我第一次跑 `--gtest_filter=QmItemCulling.*` 得到
「0 tests」就是因为它还没被编进去。已登记 `qm_item_culling_logic_test.cpp` 到 `TESTS` 列表。

**证据**

| 检查 | 结果 |
|------|------|
| `testrunner --gtest_filter=QmItemCulling.*` | **6 例全部 PASS**（格式化后重编复跑仍全过） |
| `run_cxx_tests` | 通过 3336 例（较上轮 +6）、失败 2 例 = 纯基线 → 零回归 |
| quick 门禁 | `fix_style.py` 处理后通过（仅 worktree junction 的子模块检查失败） |

**意义**：裁剪的「数学部分」（边距数值、闭区间、线段判定）从此有回归网；实机只需确认观感
（屏幕边缘实体/ghost 不会凭空消失），不再需要重新推导数值。

## 2026-09-23 · S23：把 PNG 读写的「只能实机验证」补成自动化测试

S7 留下的 gap 是「`src/test` 里没有 PNG 编解码往返测试，只有编译级证据」。本轮把它补上：
`testrunner` 本来就包含 `engine-gfx` 对象并链接 libpng（`skins_test.cpp` 已有 WebP 往返测试作先例），
所以在 `src/test/image_test.cpp` 里加了 3 个用例：

| 用例 | 覆盖点 |
|------|--------|
| `Image.PngSaveLoadRoundTripPreservesPixels` | RGBA、**5×7 奇数尺寸**往返后尺寸/格式/像素完全一致（覆盖 S7 的 `SavePng` 逐行写图与 `LoadPng` 缓冲区路径） |
| `Image.PngSaveLoadRoundTripPreservesRgbPixels` | RGB（无 alpha）往返一致 |
| `Image.PngLoadRejectsTruncatedAndCorruptData` | 截断数据与非法签名只返回 `false`、不崩溃（覆盖 `longjmp → Cleanup` 路径，也就是 S7 加 `volatile` 的那段） |

提交：`50150d114c test(image): 补 PNG 读写往返与损坏文件测试`（分支 `sync/slice-19-png-test`）。

**证据**

| 检查 | 结果 |
|------|------|
| `testrunner --gtest_filter=Image.*` | **13 tests 全部 PASS**（含新增 3 例） |
| `run_cxx_tests` | 用例总数 **3330 → 3333**（新增 3 例），失败仍是纯基线的 2 例 → 零回归 |
| quick 门禁 | `fix_style.py` 调整 include 顺序后通过（仅 worktree junction 的子模块检查失败） |

**意义**：S7 那条改动从「需要实机确认皮肤/截图」变成「编解码往返有自动化覆盖」；实机验证清单里
PNG 一项可以降级为「顺手看一眼」，不再是硬性 gap。

## 2026-09-23 · S22：扫描第五批（2 条）+ 本轮已进入收益递减区

**落地 2 条**

| 上游提交 | 主题 | 方式 |
|----------|------|------|
| `8be49024be` | 加载 Tee 时释放丢失了 tile 的地面钩子 | cherry-pick |
| `a34ebba739` | 地图 bug 比较必须带上 SHA256（本地结构已有该成员） | **手工等价**（+测试注释） |

**本轮 8 条候选里 6 条冲突**，逐个查明后多数属「本地已分叉或缺少上游特性」，收益递减：

- `1ed3568f5b` + `7e42356fe8`（探测 hook teleins）：本地**没有** `m_HasHookTeleIns`/`m_TeleIns`，
  钩子提示线的可见性由自有 `CQmHookCollVisibility` 机制负责 → 属机制不同，DEFER。
- `697e97b1a2`（新增 `dbg_websockets` 开关）、`c0c9934db2`（刷新率设置描述）：`config_variables.h` 本地文案/结构不同 → DEFER（低收益）。
- `f05ee1cc80`（删除死代码）：`gameclient.cpp` 本地已分叉 → DEFER。
- `b91785cba9`（测试运行器断言栈溢出）：`test.cpp` 本地已改 → DEFER（测试基建，收益低）。

**证据**

| 检查 | 结果 |
|------|------|
| `run_cxx_tests` | 3330 例运行，2 例失败 = 纯基线（无新增） |
| 回退自审（全链） | 61 条溯源、**0 条待处理**（libpng 那条已含回退；tune zone 属 revert-of-revert） |

分支：`sync/slice-18-sweep-e`（2 个提交，堆叠在 `sync/slice-17-sweep-d` 之上）。

**结论（写给后续排期）**：低风险可摘项基本吃完。剩下的分三类，都需要维护者参与：
① 本地重写文件（`src/game/client` 的 scoreboard/chat/hud/menus/QmUi、`players.cpp`）——只能手工融合且需实机验证；
② 依赖上游新特性（rejoin 状态、MySQL SSL、128 人、hook teleins、websocket 调试开关）——属新增功能；
③ 协议/玩法/时序语义（snapshot、demo delta、rescue 状态）——按仓库约束需单独批准。

## 2026-09-23 · S21：对整条同步链做回退自审（0 条待处理）

S20 发现「上游会回退自己」之后，本轮把**整条链**过了一遍：新增
`qmclient_scripts/support/upstream_revert_audit.py`（只读，已登记 `scripts_overview.md`），
解析每个 cherry-pick 的 `-x` 溯源（`cherry picked from commit <sha>`），再对照回退表判断。

**审计结果（`aea1453cc7..sync/slice-17-sweep-d`）**

```
链上带 -x 溯源的提交：60 条

=== 已被上游回退且链上还没有回退：0 条（需要处理） ===

=== 已被上游回退、但链上已包含回退：1 条（无需处理） ===
  链上 f6b8bfe029  ←  上游 96d35b3f94  client: Fix using a new libpng version than built against
      回退提交：073b8d0777

=== 曾回退但又被回退（净效果保留）：1 条（无需处理） ===
  链上 f03e6232db  ←  上游 8edc5a3c21  prediction: Initialize tune zone settings
      回退 6592ca8708 → 再回退 f8f3afe30b
```

即：**60 条 cherry-pick 里没有一条把「已被官方撤销的改动」留在链上**；唯一一处（libpng）已在 S20 按上游回退，
另一处是 revert-of-revert（净效果保留，我当时的判断与上游最终状态一致）。

**新增单测**：`qmclient_scripts/tests/test_upstream_revert_audit.py`（3 例，用临时 git 仓库构造
`Fix X` / `Revert "Fix X"` / `Revert "Revert "Fix X""` 三种情形）→ `Ran 3 tests ... OK`。

**证据**

| 检查 | 结果 |
|------|------|
| 回退自审 | 60 条溯源、**0 条待处理** |
| 工具单测 | 新增 3 例通过（另原有 4 例通过） |
| quick 门禁 | **PASS**（10 通过 / 0 失败 / 1 不适用） |

## 2026-09-23 · S20：扫描第四批（7 条）+ 抓到一条「上游自己回退过」的提交

**重要发现：S15 摘的 libpng「修复」被上游回退了**

`96d35b3f94 client: Fix using a new libpng version than built against`（2026-08-20，S15 已落）
在 **5 天后**被 `76444d6647 Revert "..."`（2026-08-25）回退 —— 上游把 `png_get_libpng_ver(nullptr)`
改回了 `PNG_LIBPNG_VER_STRING`。同步链已补上这条回退（`073b8d0777`），避免把「已被官方撤销的改动」当成同步成果。

**为此给判定工具加了「回退检查」**（`upstream_commit_check.py`）：扫描所有 `Revert "X"` 提交，
命中时提示 `⚠ 回退检查：**已被上游回退**（<hash>）`；若是 revert-of-revert（净效果保留）则提示
「曾被回退…但该回退又被回退 —— 净效果是保留」。自测证据：

- `96d35b3f94` → 报「已被上游回退」
- `8edc5a3c21`（S3 已落的那条）→ 报「曾被回退（6592ca8708），但该回退又被回退（f8f3afe30b）—— 净效果是保留」，与我当时的判断一致

**本轮落地 7 条**

| 上游提交 | 主题 | 方式 |
|----------|------|------|
| `76444d6647` | **Revert** libpng 版本修复（见上） | cherry-pick |
| `16f9db6680` | 渲染 freeze bar 时的崩溃 | cherry-pick |
| `f284e03386` | lineinput 反向选择 | cherry-pick |
| `95c2cd1b7b` | censorlist 忽略空行 | cherry-pick |
| `19e7d08a96` | 复活节季节判断遗漏 Holy Saturday | cherry-pick |
| `fa457bd213` | 误导性参数改名 | cherry-pick |
| `945cc8c30a` | switch number 钳制修正（`gameclient.cpp` + `collision.cpp`） | **手工等价** |

**DEFER / 已等价**

- `491a33294c`（0.7 客户端 `sv_map_window 0`）：本地配置**下限本来就是 1** → ALREADY-EQUIVALENT。
- `0390cb4306`（去掉冗余的 `cl_auto_demo_on_connect` 处理）：与本地配置/逻辑冲突，属行为改动 → DEFER。
- `b9fd48964e`（`DeltaY`→`DeltaPos` 改名）：`ui_scrollregion.cpp` 本地已重写 → DEFER（纯改名，无收益）。

**证据**

| 检查 | 结果 |
|------|------|
| `game-client` / `run_cxx_tests` 编译 | 通过（无编译错误；`FAILED: run_cxx_tests` 是测试断言失败而非构建失败） |
| `run_cxx_tests` | 3330 例运行，2 例失败 = 纯基线（无新增） |

分支：`sync/slice-17-sweep-d`（7 个提交，堆叠在 `sync/slice-16-unicode` 之上）。

## 2026-09-23 · S19：Unicode 数据表同步（Unicode 15.0.0 → 17.0.0）

S18 量化后发现的最大「非编辑器」缺口是 `src/base/unicode`（4451/4454 未命中）。查明原因后，
这是一个**体量明确、与玩法无关的生成数据缺口**：

- 本地 `VERSION.txt` = **15.0.0**，上游 = **17.0.0**；
- 文件集合两边一致，`confusables.cpp/h`、`confusables_data.h`、`tolower_data.h` 逐字节相同，
  差异只在 `confusables_data.cpp`（数据）、`tolower_data.cpp`（+55 行）与 `VERSION.txt`；
- 上游有生成脚本 `scripts/generate_unicode_confusables_data.py`、`scripts/generate_unicode_tolower.py`
  （依赖 `scripts/unicode.py`，本地已有）。

**落地（slice-16）**：把 5 个文件对齐到上游 master，**逐字节一致**：

| 文件 | 与上游 |
|------|--------|
| `src/base/unicode/VERSION.txt` | 一致（17.0.0） |
| `src/base/unicode/confusables_data.cpp` | 一致（8067 行变更） |
| `src/base/unicode/tolower_data.cpp` | 一致（+55） |
| `scripts/generate_unicode_confusables_data.py` | 一致 |
| `scripts/generate_unicode_tolower.py` | 一致 |

提交：`b35be43135 chore(unicode): 同步上游 Unicode 17.0.0 的 confusables/tolower 数据表`
（分支 `sync/slice-16-unicode`）。生成链完整（脚本 + `scripts/unicode.py`），日后可按脚本头注释重新生成。

**证据**

| 检查 | 结果 |
|------|------|
| `game-client` 编译 | 通过（341/341，数据变更触发全量重编） |
| `run_cxx_tests` | 3330 例运行，2 例失败 = 纯基线（无新增） |
| **相关测试** | `Str.Utf8CompConfusables`（`str_test.cpp:157`）与 `name_ban_test.cpp` 的 Confusables 用例**均通过** —— 新数据被测试直接覆盖 |

**顺带发现：量化工具的第二种低估**

同步后 `src/base/unicode` 的未命中只从 4451 降到 4395，几乎没动。原因不是没同步，而是
**上游自己搬迁过这批数据**：`b0511975af` 把数据写进 `confusables_data.h`，随后 `d64b8fca89`
把定义移到了 `.cpp` —— 于是那个提交的 4394 行新增在当前任何树上都不存在，永远计为未命中。
已把这条口径限制写进 `upstream_coverage.py` 的说明：**该指标只适合横向比较与定位热点，不等于完成度**。

## 2026-09-23 · S18：给「差距缩小了多少」一个量化答案

新增 `qmclient_scripts/support/upstream_coverage.py`（只读，已登记 `scripts_overview.md`）：
对窗口内**每条**上游非 merge 提交，分别以「同步前」与「链尾」两个引用计算逐行覆盖率，输出判定分布、
缺口最大的区域，以及**本轮实际追上的条数**。同时给 `upstream_commit_check.py` 加了 `--ref`（按 git 引用
而非工作树取内容），使「同步前 vs 同步后」可以直接比较。

**测量结果（窗口 `de609e845..ddnet/master`，1258 条非 merge 提交）**

| 引用 | 逐行命中 | ALREADY-PRESENT | PARTIAL | ABSENT |
|------|----------|-----------------|---------|--------|
| 同步前 `78ffa71b7a` | 23173/55108 = **42.1%** | 369 | 279 | 522 |
| 链尾 `sync/slice-15-sweep-c` | 24478/55108 = **44.4%** | 443 | 281 | 446 |

- **本轮实际追上 78 条**（判定从非 ALREADY-PRESENT 变为 ALREADY-PRESENT；名单在 `tmp/coverage-report.txt`）。
- 口径是**下界**：改写式移植（行为等价但代码不同）会被算成未命中，所以真实差距小于这里的数字；
  指标用于横向比较与定位热点，不用于宣称「已完成多少百分比」。

**缺口最大的区域（链尾视角）**

| 区域 | 未命中/总数 | 说明 |
|------|-------------|------|
| `src/game/editor` | 5036/8089 | 地图编辑器，本同步按仓库约束**未纳入** |
| `src/game/client` | 5018/6971 | 本地整体重写（scoreboard/chat/hud/menus/QmUi）所在区 |
| `src/base/unicode` | 4451/4454 | **新发现的整体缺口**：上游重新生成的 Unicode 表（confusables/tolower）与本地几乎完全不同 |
| `src/engine/shared` | 2137/3168 | 混有 `demo.cpp` 等已有改写 |
| `src/game/server` | 1836/3078 | 服务端玩法/特性差异（128 人等） |
| `src/engine/client` | 1690/2706 | 渲染/后端分叉 |

**由此产生的候选**：`src/base/unicode`（含 `scripts/unicode.py` 生成链）是一个**体量明确、风险低、
与玩法无关**的缺口（约 4.5k 行、生成数据），适合作为后续切片单独评估；本次未动（体量大且需确认
生成脚本与数据来源一致）。

## 2026-09-23 · S17：收口准备——逐段复验 + PR 草案

**逐段复验**（把「每个拟提 PR 的边界」都单独跑一遍，而不是只验链尾）：

| 边界 | 提交数 | 运行用例 | 失败 | 基线外新增 |
|------|--------|----------|------|------------|
| 纯基线 `aea1453cc7` | — | 3329 | 2 | — |
| PR-1 `8258823b45`（`src/base` 布局） | 4 | 3329 | 2 | 无 |
| PR-2 `cb1305025b`（客户端与引擎批次） | 35 | 3330 | 2 | 无 |
| PR-3 `sync/slice-15-sweep-c`（服务器/工具/扫描） | 69 | 3330 | 2 | 无 |

三段边界的失败集合都等于纯基线（那 2 例是维护者新 UI 的源码断言）→ **逐段零回归**。

> 上表是当时（S17）的快照。链尾其后推进到 `sync/slice-22-db-test`（`7ccc5e86f0`，3348 运行 /
> 3345 通过 / 2 失败 = 纯基线），最新口径以 `docs/development/upstream-sync-pr-drafts.md` 为准。

**产出 `docs/development/upstream-sync-pr-drafts.md`**：三段的标题/正文要点/规模/评审要点/推送命令，
外加「未包含的可选项」表（控制台引号参数、windowed fullscreen、rescue `m_DDRaceState`、snapshot 超时重置、
MySQL SSL 与 rejoin 两个新增特性）与「一次合完」的替代做法。

分支仍全部本地未推送；主仓库工作区里新增/修改的文档与工具（9 个文件）也未提交 —— 等维护者一句话。

## 2026-09-23 · S16：扫描管线第三批（8 条）+ 又一次语义冲突（windowed fullscreen）

**落地 8 条**

| 上游提交 | 主题 | 方式 |
|----------|------|------|
| `20713ba92f` | vanilla tuning 比较修正（`gameclient.cpp`） | cherry-pick |
| `cbc93d0be6` | tile 数据访问修正（`game/layers.cpp`） | cherry-pick |
| `097eb98f56` | 启动 Android 服务器时工作目录的 **use-after-free** | cherry-pick |
| `d5b627908c` | Huffman 兼容性测试的潜在 **OOB 读** | cherry-pick |
| `e8ec7e377e` | `str_comp_filenames` 正确性 | cherry-pick |
| `6fa9e4c7ab` | 未分组 quad 的可见性检查清理（`render_layer.cpp`） | cherry-pick |
| `8122e00609` | Rust 版本号后多余换行 | cherry-pick |
| `8ed0a5b923` | `CHuffman::Compress/Decompress` 输入输出尺寸断言 | **手工等价**（本地已由 `base/system.h` 引入 `dbg.h`，未重复加 include） |

**第三次语义冲突：`478781ad65`（snippet 工具在 windowed fullscreen 下失效）**

上游把 windowed fullscreen 从 **无边框** 改成 **有边框**：

- `backend_sdl.cpp`：`SDL_SetWindowBordered(m_pWindow, SDL_FALSE)` → `SDL_TRUE`
- `graphics_threaded.cpp`：删掉 windowed fullscreen 分支里的 `Flags |= INITFLAG_BORDERLESS;`

而本地有两条测试**明确锁定无边框行为**（读源码断言）：

- `QmWindowModes.StartupMarksWindowedFullscreenAsBorderless`
- `QmWindowModes.WindowedFullscreenRemainsABorderlessNonResizableWindow`

落地后这两条立即变红 → 按「默认保留 fork 行为」**已从分支移出**（rebase-drop），
作为待决项记录：采用上游可修好 snippet 工具，但会改掉本地 windowed fullscreen 的外观。

其余 DEFER：`8e346654ba`（ddrace 队伍数崩溃修复，本地 `gameclient.cpp` 没有 `GameInfoEx` 队伍字段）、
`bdd2253cb0` 与 `90753667c3`（`sound.cpp` 本地已分叉）。

**证据（与纯基线对照）**

| 状态 | 运行用例 | 失败 |
|------|----------|------|
| S16 链尾（slice-15，约 72 条上游派生改动） | 3330 | **2** |
| 纯基线 `aea1453cc7` | 3329 | **2**（同一批，维护者新 UI 源码断言） |

→ 移出冲突提交后回到**零回归**。

分支：`sync/slice-15-sweep-c`（8 个提交，堆叠在 `sync/slice-14-sweep-b` 之上）。

## 2026-09-23 · S15：扫描管线第二批（9 条）

沿用 S14 的扫描结果，本轮取「客户端/引擎的机械性修复」，**避开**协议、玩法语义、以及审计已判定行为等价的预测类。

**落地 9 条**

| 上游提交 | 主题 | 方式 |
|----------|------|------|
| `424a584a22` | 切换编号检查的边界修正（`gameclient.cpp`） | cherry-pick |
| `a2574e36cf` | 奇数高度截图中间行 alpha 错误 | cherry-pick |
| `c9a9931f8a` | shader log 意外消息导致的 **OOB 读写** | cherry-pick |
| `6f764e4f11` | 带贴图 tile program 的 OpenGL shader location 未绑定 | cherry-pick |
| `96d35b3f94` | 客户端用到的 libpng 版本比编译时更新 | cherry-pick（**已在 S20 按上游回退**） |
| `4b4fdabd40` | 加载失败时不再把 datafile 数据标记为已卸载 | cherry-pick |
| `5ca8082426` | 失败的 `dbg_assert` 标 `[[unlikely]]` | cherry-pick |
| `40d807be03` | `add_map_votes <dir>` 不带斜杠不再崩溃 | cherry-pick |
| `f11a2b3b1c` + `7a10863ad5` | 截图失败补错误日志 + 删除未使用的 `CUserErrorStruct::m_pReader` | **手工等价** |

`d55dd34226`（text-width 空串检查）cherry-pick 判定为空改动 → **本地已存在**。

**DEFER / 不适用（附理由）**

- `bc7b3225ac`（UI clipping 未考虑交集位置）：本地 `ui.cpp` 是 QmUi 重写版，`ClipEnable` 的交集代码形态不同 → 需要专门处理 UI 层。
- `fc5366ddcb`（MySQL float 参数缓冲长度）：本地已经用 `sizeof(...m_Float)` → ALREADY-EQUIVALENT。
- `8461fc0bdc`（更新时不要把 `.ttf` 当二进制改名）：本地 updater 没有该条件 → 不适用。
- `ed2b08f5d9`（server: Reset snapshots on timeout）：属 snapshot/时序语义，按仓库约束**需单独批准**。
- 按类别跳过：协议/格式类（`d98e1e4ea3`、`2beb307c20`、`669c88f2a1`、`80aec863ef` 的 demo delta 字节）、
  版本号与 CI/脚本/README 类、以及 `b9c39900a9`（rescue 覆盖 `m_DDRaceState`，触及服务端玩法语义，留给维护者定夺）。

**工具局限的第二个实例（重要）**：扫描把 `2c6c75f1cc`、`ef8008be88`、`422cd52735` 判为 ABSENT，
但预测簇审计（2026-09-23）已确认这三条**本地行为等价**（改写式移植导致逐行不匹配）。
→ **逐行 ABSENT ≠ 行为缺失**，尤其预测/网络类改动必须先看审计结论，不能按扫描结果批量 cherry-pick。

**证据（与纯基线对照）**

| 状态 | 运行用例 | 失败 |
|------|----------|------|
| S15 链尾（slice-14，约 64 条上游派生改动） | 3330 | **2** |
| 纯基线 `aea1453cc7` | 3329 | **2**（同一批，维护者新 UI 源码断言） |

→ 继续**零回归**。

分支：`sync/slice-14-sweep-b`（9 个提交，堆叠在 `sync/slice-13-sweep-smallfix` 之上）。

## 2026-09-23 · S14：用新工具做全窗口扫描 + 再落 7 条（已执行）

**扫描**：`tmp/sweep_candidates.py` 用 `upstream_commit_check.py` 对窗口内所有非 merge 提交做内容判定，
再按「是否属于本地重写区 + 改动行数」过滤，得到 **411 条候选**，前 30 条全是 1–2 行、判定 ABSENT 的小修复。

**落地 7 条**

| 上游提交 | 主题 | 方式 |
|----------|------|------|
| `1f5095abe6` | 断言打印地址而非时间值（`base/time.cpp`） | cherry-pick |
| `05ab16f91a` | 调试图形负最大值显示错误（`graph.cpp`） | cherry-pick |
| `945456bd1d` | 负偏移下的 quad 裁剪错误（`render_layer.cpp`） | cherry-pick |
| `7627324893` | quad layer 的 alpha 可见性检查无效（`render_layer.cpp`） | cherry-pick |
| `82fabe040c` | `hot_reload` 内存泄漏（`gamecontext.cpp`） | cherry-pick |
| `8273b427e0` | `sv_ipv4only` 不再阻断 websockets（`server.cpp`） | cherry-pick |
| `3a2dd0a66f` + `dd35e93d37` | `render_map` 信封点范围钳制崩溃修复 + `sv_connlimit` 下限改 1 | **手工等价**（本地写法/文案不同） |

**同轮判定为「不用做」的四条（证据）**

- `b68c684a2d`（serverbrowser 重置 sqlite 语句）：本地**已经重置**（写法多了 `m_pLoadStmt &&` 判空）→ ALREADY-EQUIVALENT。
  这也是新工具的一个局限实例：判定基于**逐行精确匹配**，写法略异就会报 ABSENT，所以 ABSENT ≠ 可直接摘。
- `22caab64a8`（Vulkan shader 编译顺序确定化）：本地 `cmake/BuildVulkanShaders.cmake` 是**显式列表**生成、
  没有 GLOB，无 sorter 对象 → 不适用。
- `9fbddf182c`（服务端关停时也关掉 HTTP 接口）：本地 `CServer` **没有** `m_pHttp`（HTTP 接口在本地不存在）→ 需先加特性。
- `abfe01656c`（Show own name plate）：**KEEP-FORK，且本地测试明确禁止上游写法** ——
  `qm_new_ui_menu_branch_test.cpp:2780`、`:2917` 两处 `EXPECT_EQ(...m_ClNamePlatesOwn..., npos)`，
  以及 `qm_modes_test.cpp:546` 的注释「当前：只有当前操控角色显示自己的昵称（= 旧 cl_nameplates_own 行为）」。
  上游那条会把 `cl_nameplates_own` 重新接回渲染早退条件，与本地既定行为冲突。

**证据（与纯基线对照）**

| 状态 | 运行用例 | 失败 |
|------|----------|------|
| S14 链尾（slice-13，约 55 条上游派生改动） | 3330 | **2** |
| 纯基线 `aea1453cc7` | 3329 | **2**（同一批，维护者新 UI 源码断言） |

→ 继续**零回归**。

分支：`sync/slice-13-sweep-smallfix`（7 个提交，堆叠在 `sync/slice-11-tools` 之上）。

## 2026-09-23 · S13：同步链 rebase 到新基线 + 内容级判定工具（本轮）

维护者把在途改动提交了（`78ffa71b7a` → `93b151a9db` → `aea1453cc7`，分支 `fix/qm-test-contract-alignment`），
同步链基线因此过时 2 个提交。本轮做三件事。

**1）整链 rebase（11 个分支引用一起前移）**

```
git rebase --onto aea1453cc7 78ffa71b7a sync/slice-11-tools --update-refs
git rebase --onto e4914db96e cdc59df128 sync/opt-console-strict-args
```

新位置：`slice-1` = `8258823b45`、`slice-9` = `cb1305025b`、`slice-11` = `e4914db96e`、
`opt-console-strict-args` = `018cced509`。无冲突。

**2）零回归对照实验（本轮最重要的证据）**

| 被测状态 | 运行用例 | 失败 |
|----------|----------|------|
| rebase 后的同步链（slice-11，约 48 条上游派生改动） | 3330 | **2** |
| **纯新基线** `aea1453cc7`（不含任何同步改动） | 3329 | **2** |

两次失败的**就是同两条**：

- `QmMonitoringHelpers.BaseSettingsStableTextCandidateAuditIsEmptyExceptAllowlist`
  （报 `Localize("Enhanced rendering")` 未进稳定文案白名单）
- `QmNewUiMenuBranches.TeeRestoresCardContentsAndKeepsDoubleClickActions`
  （断言 Tee 页源码文本 `vSkinList` / `ApplySkinListEntry` / 双击分支）

两条都是**维护者新 UI/测试合同**的源码断言，涉及文件同步链从未改动 → **同步链零回归**。
附带收获：维护者那批测试合同修复把原来的 45 例既有失败降到了 2 例，`--mode default` 现在基本可用作验收口径。

**3）新增内容级判定工具，专治第 14 轮那类事故**

`qmclient_scripts/support/upstream_commit_check.py`（只读；已登记 `scripts_overview.md`，
配套 4 个单测 `qmclient_scripts/tests/test_upstream_commit_check.py` 全部通过）：

- 按「上游提交新增行在当前工作树中的命中率」判定：
  `ALREADY-PRESENT`(≥0.9) / `PARTIAL`(≥0.15) / `ABSENT` / `NO-CODE-CHANGES`；
- 用第 14 轮事故反向验证：

| 提交 | 判定 | 说明 |
|------|------|------|
| `18081b172`（曾把 `EAsyncIoFinishState` 重复插入） | **ALREADY-PRESENT** | 事前就能拦住这次事故 |
| `9f59dcb1f6`（MySQL SSL，本地确实没有） | ABSENT | 与事实一致 |
| `821d5ae4b4` 在 `opt` 分支 / 在 `slice-11` | ALREADY-PRESENT / PARTIAL | 分支持有状态可分辨 |

- 局限（写在脚本说明里）：`PARTIAL` 常见于「搬移既有行」的补丁，**不能**当成「安全可摘」；
  真正的护栏是 `ALREADY-PRESENT`。判定仍以编译为准。

**证据**

| 检查 | 结果 |
|------|------|
| rebase | 无冲突，11 个分支引用全部前移到 `aea1453cc7` |
| `run_cxx_tests`（链上 / 纯基线） | 2 / 2 例失败，集合一致 → 零回归 |
| 新工具单测 | `py -3 -m unittest discover -s qmclient_scripts/tests -p "test_upstream_commit_check.py"` → 4 passed |
| 主仓库 quick 门禁 | **PASS**（10 通过 / 0 失败 / 1 不适用）—— 主仓库没有 worktree 的 junction 问题 |

## 2026-09-23 · S12：收口准备（可选分支 + 验证清单 + 一次「cherry-pick 成功但构建失败」事故）

本轮不再扩大同步面，转向收口。三件事：

**1）把待决策项做成「一句话可拍板」的可选分支**

`sync/opt-console-strict-args`（**不并入同步链**，从 `sync/slice-11-tools` 分出）：

- `d972247ae1` console: Fix validating quoted victim arguments（上游 `821d5ae4b4`）
- `8f85af1faa` test(console): 跟随上游语义更新 emote 期望

构建通过、`run_cxx_tests` 3330 例运行、45 例失败 = 基线（无新增）。采用与否只差你一句话；
不采用则保持 S10 的结论（保留 fork 历史语义）。

**2）输出待实机验证清单**：`docs/development/upstream-sync-verification.md`
（渲染裁剪、PNG 读写、7 项客户端行为自查、以及需要先决策的 6 项）。

**3）事故：`cherry-pick` 成功但构建失败，且原因不是冲突**

从零重叠列表挑 base 清理提交时，`18081b172 Replace ASYNCIO_* enum with enum class EAsyncIoFinishState`
**cherry-pick 干净通过**，但构建报 `error C2011: "EAsyncIoFinishState" 类型重定义` ——
因为它早已存在于我们采纳的上游 19.9 版 `aio.cpp` 中，git 的 3 次合并把同一段枚举又插了一次。
已用 `git branch -D` 丢弃该分支并重建确认绿。

**教训（补充方法论）**：`cherry-pick` 无冲突 **不等于** 结果正确；已采纳的较新上游文件会让旧提交
「重复插入」。**每次 cherry-pick 后必须编译**（本轮已按此执行并捕获）。

**同轮确认「其实早已等价」的三条**（`cherry-pick -n` 后 `git diff --cached` 为空）：

| 上游提交 | 主题 | 原因 |
|----------|------|------|
| `5b363d770` | Fix `readability-redundant-casting` | 本地已无该冗余转换 |
| `df8f2a52b` | Add `Bytes` documentation group | 注释分组已在 |
| `0a4966cf6` | Rename `Shell` documentation group to `OS` | 同上 |
| `d8a78b266` | `static constexpr` 取代 `#define`（base 常量） | 采纳的上游 19.9 `aio.cpp` 已是 `static constexpr` |
| `a8156cf36` | 去掉 `base/os.cpp` 重复 `mem.h` include | 采纳的 master 版 `os.cpp` 只有 1 处 include |

## 2026-09-23 · S11：地图工具（`src/tools`）修复批次（已执行）

上游 `src/tools` 在窗口内有 7 条修复，全部与本地文件布局无关（工具是独立可执行文件），落地 **7 条**：

| 上游提交 | 主题 | 落地 |
|----------|------|------|
| `71582a2fb` | `dummy_map`：game layer 颜色修正 | cherry-pick 干净 |
| `8c5153c41` | `dummy_map`：改用 `log_*` | cherry-pick 干净 |
| `04cbd07ce` | `dummy_map`：两个 layer 复用同一份 tile data | **手工等价**（本地 `CTile` 仍是 `m_Reserved`，上游已改名 `m_MustBe0`，hunk 上下文不同） |
| `b75fe1f98` | `map_diff`：处理非法 tile layer 数据 | cherry-pick 干净 |
| `2aa670ad9` | `map_replace_image`：PNG 加载修复 | cherry-pick 干净 |
| `bc2c2f930` | `map_convert_07`：**use-after-free** 修复 | cherry-pick 干净 |
| `7cf4372da` | `map_replace_area`：Windows 命令行 **double free** 修复 | cherry-pick 干净 |

其中 `bc2c2f930`（use-after-free）与 `7cf4372da`（double free）是实打实的内存安全修复，工具此前没有被任何切片覆盖过。

**证据**

| 检查 | 结果 |
|------|------|
| 5 个工具目标编译链接 | **通过**（`dummy_map` / `map_diff` / `map_convert_07` / `map_replace_image` / `map_replace_area`，12/12 链接出 exe） |
| `run_cxx_tests` | 3330 例全部运行，45 例失败 = 基线逐条一致（无新增、无消失） |
| quick 门禁 | 除 worktree junction 的子模块前置检查外全部通过 |

**Gap**：想给工具做功能冒烟时发现 `dummy_map` 的产物路径是 storage 相对的 `maps/dummy3.map`，在临时 CWD 下没有落盘，冒烟测试**未取得结论**（只做到编译链接级验证）。这不影响本次修复的正确性判断，但「工具行为」这一层没有实测证据。

**同轮的两条「其实不用做」**

- `55ebaf373a`（connlimit 提到 reconnect 分支之前）：本地 `TryAcceptClient` **本来就在最前面做 connlimit**，而且完全没有上游那套 `Reconnect` 槽位复用分支 → 该修复的目标在本地已成立，记为 ALREADY-EQUIVALENT。
- `78d8f82c52`（MySQL TLS 无 CA 证书）：本地 `mysql.cpp` **完全没有 SSL 支持**（grep `ssl/SSL/TLS` 零命中），上游那条是 20.0 新增特性 `9f59dcb1f6` 的补丁 → 属**新增功能**，需先讨论再加。

**观察（对后续优先级有用）**：这轮连续遇到「上游提交在本地已等价 / 属新增特性」的情况，说明**剩余真实差距比 2000+ 提交的账面数字小得多**；
真正挡住同步的是三类：(1) 本地整体重写的文件（scoreboard / chat / hud / menus_demo）、(2) 依赖新特性的改动、
(3) 需要实机或服务器验证的行为变更。建议把后续重点放在收口与验证，而不是继续扫提交。

分支：`sync/slice-11-tools`（7 个提交，堆叠在 `sync/slice-10-server-smallfix` 之上）。

## 2026-09-23 · S10：服务器/控制台小修批次 + 一处语义冲突（已执行）

**落地 3 条**

| 上游提交 | 主题 | 落地 |
|----------|------|------|
| `bb5e2d5e7f` | Optimize slow `/map` | cherry-pick 干净 |
| `e917c4e9ca` | Assert that command used with `CConsole::Chain` exists | cherry-pick 干净 |
| `d1c518a5f9` | server: Fix `GetOptionalFloat` db getter（`GetInt` → `GetFloat`） | 手工等价（上游的测试改动依赖本地没有的测试辅助，未一并摘） |

**发现一处真正的语义冲突（需要维护者决策，已按默认策略保留 fork 行为）**

上游 `821d5ae4b4 console: Fix validating quoted victim arguments` 把参数校验从「仅未加引号的参数」扩展到
**加引号的参数也校验**。落地后本地测试立刻变红：

```
[  FAILED  ] CQmEmoteCommandsTest.PreservesExistingIntegerParsing
qm_modes_test.cpp(1022): Expected equality: m_pConsole->LineIsValid(...) = false vs Case.m_Dispatched = true
```

对应本地测试用例（`src/test/qm_modes_test.cpp:1012`）：

```cpp
// 引号参数沿用现有控制台语义，无法转为整数时 GetInteger 返回 0。
{"\"invalid\"", true, 0},
```

即：本地**有意保留**「`emote "invalid"` 不报错、按 0 处理」这一历史控制台语义；上游的修复会让这类输入变成
非法。按「默认保留 fork 行为，仅逐项采用官方更优实现」，该提交已从分支移出，作为**待决项**记录：

- 采用上游（更严格、更安全，尤其对 `ban`/`kick` 的 victim 参数）：需要同时改本地测试期望并在实机/服务器上确认
  chat 命令行为变化；
- 保持现状：本地语义不变，但这块以后每次同步都会再撞一次。

**DEFER（原因写清）**

- `55ebaf373a`（connlimit 提前到 reconnect 分支之前）、`78d8f82c52`（MySQL TLS 无 CA 证书）、
  `00def9cb1f`（map config 指向 client id 的崩溃）：本地对应文件已分叉；其中 `00def9cb1f` 的守卫依赖
  本地不存在的 `ClientSupportsServerMaxClients`（128 人/服务端 max_clients 特性自带）。
- `4e4536bdae`（Change client states while rejoining）+ `4e25f792ea`（空槽崩溃修复）：本地**完全没有**
  `m_Rejoining`/`m_IngameBeforeRejoin`，属新增特性而非同步 → 不做。
- 记分板族 `f15d3b1f94`/`dbc77978c6`/`3848ee7f94`（cursor hint、高亮渲染顺序）与
  `4239b0c144`/`80fba7dd7b`（clan 间距常量）：本地 `scoreboard.cpp` 已整体重写（2039 行 vs 上游 1161 行），
  上游这几条是间距常量与提示文案，属外观微调且需按本地页脚形态重做 → DEFER（不盲合）。

**证据**

| 检查 | 结果 |
|------|------|
| `game-client` 编译 | 通过（9/9 增量，链接 `DDNet.exe`） |
| `run_cxx_tests` | **3330 例全部运行**，45 例失败 = 基线逐条一致（无新增、无消失） |
| `--mode default` 门禁 | quick 层 11 项通过；Rust 测试通过；失败项只有 worktree junction 的子模块前置检查与 45 例既有失败 |

分支：`sync/slice-10-server-smallfix`（3 个提交，堆叠在 `sync/slice-9-ghost-cull` 之上）。

## 2026-09-23 · S9：ghost 屏外裁剪（`ada53c8cb3` 的实际缺口，已执行）

S3/S8 一直把 `ada53c8cb3`（玩家/钩子/ghost 裁剪）记为「需要手工融合」。本轮逐处核对本地代码后，
结论与预期不同，按证据重新判定：

| 上游改动点 | 本地现状 | 判定 |
|------------|----------|------|
| 玩家屏外裁剪 | **已存在**：`CPlayers::OnRender` 在调用层裁剪（`players.cpp:2045`，100 世界单位扩边；观战角色见 `2015`） | KEEP-FORK |
| 钩子屏外裁剪 | **已存在**：`CPlayers::RenderHook` 内部 AABB 裁剪（`players.cpp:678-684`，同样 100 扩边） | KEEP-FORK |
| ghost 屏外裁剪 | **缺失**：`ghost.cpp` 的 `m_aActiveGhosts` 循环没有任何裁剪，而且皮肤/忍者渲染信息准备都在两个 Render 调用之前 | 本次补上 |

因此**不引入上游的 `RenderPlayer`/`RenderHook` 签名改动**（那会牵动 8 处调用点、扩大合并面，
而本地已在调用层实现同等裁剪），只补 ghost 这一环：

- `ghost.cpp` 循环前计算 `CScreenRect` 并 `Expand(100.0f)`（上游的 200×200 盒语义）；
- 循环内算出插值位置后、在皮肤/忍者准备**之前**早退（这正是省下开销的位置）。

提交：`12d878735e perf(client): ghost 渲染补上屏外裁剪（对齐上游 ada53c8cb3 的缺口）`
（分支 `sync/slice-9-ghost-cull`）。

**证据**

| 检查 | 结果 |
|------|------|
| `game-client` 编译 | 通过（222/222，链接 `DDNet.exe`） |
| `run_cxx_tests` | **3330 例全部运行**，45 例失败 = 基线逐条一致（无新增、无消失） |
| quick 门禁 | 除 worktree junction 的子模块前置检查外全部通过 |

**Gap**：ghost 裁剪同样属于「少画了东西」类改动，需要实机确认屏幕边缘附近观战/回放 ghost
不会凭空消失。

`fabf09a3a3` + `061242fe47`（钩子提示线裁剪）仍然 DEFER：本地是自有 `qm_hook_coll_*` 四件套 +
`CQmHookCollVisibility`（`players.cpp:391` 已有 `MayReachView` 视野裁剪），机制不同、目标一致。

## 2026-09-23 · S8：引入 `CScreenRect` 基建并把实体裁剪改成上游同形（已执行）

S4 的结论是「渲染族都卡在本地没有 `CScreenRect`」。本轮先把基建补上，并把自有裁剪改写成上游形态。

**做了什么**

1. `src/engine/graphics.h` 新增上游 `CScreenRect`（`Move/Size/Width/Height/Inside/Expand`）与
   `IGraphics::GetScreen()` 重载（内联实现，内部仍调用原有四浮点纯虚函数）。
   **不动任何后端、不改原有接口** → 纯增量，编译风险为零。
2. `src/game/client/components/items.cpp` 的实体裁剪由四浮点写法改写为上游同形写法
   （`CScreenRect ScreenRectLaser = Graphics()->GetScreen();` + `Expand(TileSize)` 等），
   边距数值与判定语义完全不变（`Inside()` 的 `in_range` 与我原来的 `>=/<=` 同为闭区间）。

提交：`f699df66c2 refactor(client): 引入上游 CScreenRect 并把实体裁剪改写为上游同形实现`
（`fix_style.py` 调整了 `graphics.h` 一处空行后 amend）。

**基建到位后的可应用性复测（`cherry-pick -n` 逐条，随后 reset 清场）**

| 上游提交 | 之前失败原因 | 现在 |
|----------|--------------|------|
| `14fc1e9d1e` 实体裁剪 | 无 `CScreenRect` | 仍冲突 —— 因为本地**已经移植了同样的裁剪**（S4 成果），hunk 落在同一区域 |
| `ada53c8cb3` 玩家/钩子/ghost 裁剪 | 无 `CScreenRect` | 仍冲突在 `players.cpp`：本地有自己的钩子 AABB 预筛与大量自有渲染逻辑，需手工融合 |
| `fabf09a3a3`、`061242fe47` 钩子提示线 | 无 `CScreenRect` | 仍冲突在 `players.cpp`：本地是自有 `qm_hook_coll_*` 机制 |

结论（写清楚，避免夸大）：**`CScreenRect` 让「以上游形态书写的新代码」可以直接对齐，但上述四条
具体提交仍然需要手工融合**，因为本地对应文件已经分叉（S4 已自行实现裁剪、players.cpp 有自有机制）。
基建价值在于后续新补丁与增量改动不再被 API 缺失挡住。

**证据**

| 检查 | 结果 |
|------|------|
| `game-client` 编译 | 通过（213/213，`graphics.h` 改动触发大范围重编） |
| `run_cxx_tests` | **3330 例全部运行**，45 例失败 = 基线逐条一致（无新增、无消失） |
| 行为一致性 | 边距与判定语义逐条对齐 S4 实现（`Expand` 与 `Inside` 等价），仅书写形态变化 |

**Gap**：与 S4 相同 —— 裁剪类改动无法在本环境做视觉验证，实机确认仍需维护者完成。

## 2026-09-23 · S7：PNG 读写健壮性（已执行）

三条上游 PNG 提交的逐个处置（本地 `image_loader.cpp` 是**重写版**：单一连续缓冲 + 行指针指向内部，
并带更强的尺寸/溢出校验，所以上游补丁不能直接摘）：

| 上游提交 | 处置 | 说明 |
|----------|------|------|
| `7f5ac6ed07` Write PNG rows directly instead of copying | **落地** | 本地 `SavePng` 与上游修复前同构（逐行 `new`+`mem_copy`+`png_write_image`），改为逐行 `png_write_row`，并把 `WidthBytes` 改成 `size_t` 避免大图 int 溢出 |
| `3bed38882c` Make PNG loader locals read after longjmp volatile | **适配落地** | 本地 `LoadPng` 结构不同，但同样存在「setjmp 之后写入、longjmp 之后在 Cleanup 读取」的 UB。按同样语义把 `png_create_info_struct` 提到 `setjmp` 之前（失败时直接销毁 read struct），并把 `pRowPointers`/`Height` 标为 `volatile`（用到的地方补 `(png_bytepp)` / `(size_t)` 转换） |
| `f84356c0ce` Hold PNG dimensions in size_t | **已等价，不改** | 本地版本已用 `png_uint_32` + int 上界检查、`size_t BytesInRow`、`PRIzu` 日志格式，比上游这份更强 |

提交：`ec2b417880 PNG 读写健壮性：逐行写图 + longjmp 后 volatile 局部量`（分支 `sync/slice-7-png`）。

**证据**

| 检查 | 结果 |
|------|------|
| `game-client` 编译 | 通过（10/10 增量，链接 `DDNet.exe`） |
| `run_cxx_tests` | **3330 例全部运行**，45 例失败 = 基线逐条一致（无新增、无消失） |
| quick 门禁 | 通过（仅 worktree junction 的子模块前置检查失败）；`fix_style.py` 已处理一处格式差异 |

**Gap（必须说明）**：`src/test/image_test.cpp` 只有 `CImageInfo` 的纯逻辑用例，**没有 PNG 编解码往返测试**
（全仓 grep 无 `SavePng`/`LoadPng` 用例）。因此这两处改动目前只有编译与既有测试的旁证，
**需要实机确认皮肤/贴图加载与截图导出正常**（PNG 路径同时服务这两条链路）。

> 过程教训（第二次踩）：统计失败用例时最初用 `^\[  FAILED  \]` + 排除 `(N ms)` 的正则，
> 把 `[  FAILED  ] 45 tests, listed below:` 这类汇总行也算进集合，得出了与基线不一致的假差异。
> 正确做法是用 `\[\s+FAILED\s+\]\s+([A-Za-z_]\w*\.[\w]+)` 只取真正的 `Suite.Test`。

## 2026-09-23 · S6：20.0 之后的 nightly 批次（已执行）

背景：20.0（2026-08-27）之后 master 还有 **250 个非 merge 提交**（最新到 2026-09-22）。本轮挑其中
「小、可编译验证、不依赖本地缺失特性」的部分。

**落地 7 条**

| 上游提交 | 主题 |
|----------|------|
| `32d5bef9ec` | server: Keep save array sizes in locals |
| `a743330648` | Fix `tele` not using position of target player |
| `a2e1141501` | Rate limit `/timecp` queries |
| `498c8f8cee` | Avoid using `str_length` in loop conditions |
| `6a315ad54c` | server: Fix crash on `pause_game` in map configs |
| `16189b741c` | Fix building server with clang and antibot |
| `d75014d9e9` | demos: Don't try to add markers for stopped recorders（手工；与已摘的 `350d0398cc` 强化断言配套，避免停录后触发断言） |

**一个必须先记的事故：第一次验证是假证据**

第一次跑完 `run_cxx_tests` 后看到「0 例失败」，差点当成零回归提交。实际是 **testrunner 根本没跑起来**：
它依赖 `game-server-without-main`，而其中一条提交引用了本地不存在的成员 → 服务器编译失败，
日志里没有 `FAILED` 行只是因为没有测试执行。**教训：只看有没有 `[  FAILED  ]` 不能判定测试通过，
必须核对汇总行（`[==========] N tests ran` / `[  PASSED  ]`）或者用 `--mode default` 全量跑。**

**DEFER（8 条，原因写清）**

- `4e25f792ea` server 空槽崩溃修复：依赖上游 `m_IngameBeforeRejoin`（来自 `4e4536bdae`
  「Change client states while rejoining」），本地无该成员 → 编译失败，已从分支移除。
- `f5ff86e44e` DecodeWV 内存泄漏：**本地已存在**（fork 的 `DecodeWV` 各错误路径都已 `WavpackCloseFile`，
  还多了 SampleRate/NumSamples 校验）。
- `83607c15fe`、`4ba8fef2f`：都改上游新的 `WakeTime`/`NextUpdateTime` 循环，本地是自有 `WaitWithNetwork`
  实现 → 单摘没有意义；上游 `9ed3b24d9d`（低刷新率性能）整体仍列为后续 MERGE-BOTH 候选。
- `7f5ac6ed07`、`3bed38882c`、`f84356c0ce`：PNG 读写健壮性修复，本地 `image_loader.cpp` 已分叉为重写版，
  需逐函数对比后单独切片（PNG 路径影响皮肤/地图，不能盲合）。
- `9eb5bb6913`（删未用成员）、`55ebaf373a`（connlimit 提前）：纯清理 / 优先级低。
- 其余 nightly 提交属于：assets 资源重构（本地无 `assets.cpp`）、第三方库（wavpack 4.40→5.9.0、
  vendored json-parser）、editor、teehistorian 角色名、CMake/测试基建 —— 按仓库约束需单独批准或不在范围。

**证据（第二轮，真实）**

| 检查 | 结果 |
|------|------|
| `game-client` 编译 | 通过（13/14 增量，链接 `DDNet.exe`） |
| `check_gate.py --mode default` | quick 层 11 项通过；`run_cxx_tests` **3330 例全部运行**，45 例失败 = 基线逐条一致（无新增、无消失）；Rust 测试通过 |
| 门禁结论 | `default` 报 FAIL，两项原因均为**环境/基线**：worktree junction 的子模块前置检查、以及维护者在途改动造成的 45 例既有失败 |

分支：`sync/slice-6-nightly-batch`（7 个提交，堆叠在 `sync/slice-5-robustness` 之上）。

## 2026-09-23 · S5：健壮性小修批次（已执行）

目标：吃「小改动 + 有测试覆盖」的健壮性修复 —— 控制台/rcon 粘贴、JSON 解析与转义、demo 跳过时长、
服务器 demo 文件名清洗。结果：**9 条落地（6 条干净 cherry-pick + 3 条手工等价），4 条 DEFER**。

| 上游提交 | 主题 | 落地方式 |
|----------|------|----------|
| `7b7360521` | JSON 对象值里的字符串未校验 UTF-8 | cherry-pick（冲突仅在 `json_test.cpp` 头部注释/include，手工合并并保留本地文件头） |
| `ff55fe211` | JSON 解析 + UTF-8 校验的栈溢出 | cherry-pick 干净 |
| `51447775d` | `EscapeJson` 可能截断 `\uXXXX` 转义 | cherry-pick 干净 |
| `54fcf119a` | rcon 粘贴大段文本崩溃 | cherry-pick 干净 |
| `61645e4e3` | 控制台多行粘贴的换行处理 | cherry-pick 干净 |
| `59784659d` | 短 demo 不显示 0.1s/0.5s 跳过时长 | cherry-pick 干净 |
| `d429ac720` | 短 demo 的跳过时长下标非法 | 手工：只搬下标语义（`<2` 归零），其余 hunk 是上游花括号风格，本地已是等价写法 → 按「保留 fork 行为」不动 |
| `e32c05342` | 录制服务器玩家 demo 时清洗文件名 | 手工：本地用 `GetMapName()`，补 `str_sanitize_filename` |
| `43296ff66` | `ConUserCommandStatus` → `ConCmdlistChat` 重命名 | 手工：3 处 |

**DEFER（原因写清）**

- `4ba8fef2f` spin wait 阈值 200us→1000us：本地**没有**上游 `9ed3b24d9d`（2026-08-31
  「client: Improve performance for low refresh/update rates」）引入的 halving 循环，fork 是自己的
  `WaitWithNetwork` 实现 → 单摘这条没有意义。**`9ed3b24d9d` 本身列为后续 MERGE-BOTH 候选**
  （低刷新/更新率下的性能改善，需要和本地等待逻辑融合）。
- `4731fc664` teehistorian `AddString` 显式 Limit：本地该函数签名不同（`int Level` 而非上游
  `const char *pRoleName`），上游的「角色名」特性尚未同步。
- `9cf22b5e5` + `17e3c2e83` 升级 vendored json-parser：属**第三方库**，按仓库约束需单独批准；
  本轮只落 `src/engine/shared/json.cpp` 侧的两条修复。

**证据**

| 检查 | 结果 |
|------|------|
| `game-client` 编译 | 通过（9/9 增量，链接 `DDNet.exe`） |
| `run_cxx_tests` | 3330 例中 45 例失败，与基线逐条一致（无新增、无消失） |
| JSON 专项 | `Json.ParseValidation`（含本次新增的非法 UTF-8 用例）与 `Json.Escape` 均 **PASS** —— 直接验证了 JSON 两条移植 |
| quick 门禁 | 除 worktree junction 造成的子模块前置检查外全部通过 |

分支：`sync/slice-5-robustness`（9 个提交，堆叠在 `sync/slice-4-entity-culling` 之上）。

## 2026-09-23 · S4：渲染/性能 —— 屏幕外实体裁剪（已执行）

上游 20.0 的 "More FPS" 由四类改动组成：`14fc1e9d1e`（实体裁剪）、`ada53c8cb3`（玩家/钩子/ghost 裁剪）、
`fabf09a3a3` + `061242fe47`（钩子提示线裁剪）。

**关键发现（决定了本轮的切法）**：`14fc1e9d1e` 与 `ada53c8cb3` 都依赖上游 `569edee60b add CScreenRect`
（37 文件、+319/-306，含 `src/game/map/render_layer.cpp` / `render_map.cpp` 重构），而本地**完全没有
`CScreenRect`**（Graphics 仍是 `GetScreen(&x0, &y0, &x1, &y1)` 形态）。直接 cherry-pick 不可能；
先搬 `CScreenRect` 等于把 37 文件的渲染基建重构塞进这一轮，风险与收益不匹配。

**本轮做法（MERGE-BOTH：上游语义 + 本地基建）**：

- 只在 `items.cpp` 的 5 个渲染点加裁剪：预测投射物、预测激光、snapshot 投射物 / 拾取物 / 激光；
- 边距与上游完全一致：投射物 ±1 tile(64)、激光 ±0.5 tile(32)、拾取物 x ±1.75 tile / y ±0.75 tile；
- 激光沿用上游的「线段与矩形四向不相交」判定，投射物/拾取物用点判定；
- 不引入 `CScreenRect`、不改任何函数签名 —— 改动 39 行、单文件 `d009e8899f`。

**DEFER（本轮不做，理由写清）**：

- `ada53c8cb3` 玩家 / ghost 裁剪：需把 `RenderPlayer`/`RenderHook` 签名加上 `CScreenRect` 参数并改全部调用点；
  且本地 `players.cpp` 已有等价的钩子 AABB 预筛（100 单位缓冲，正好等于上游 ghost 的 ±100 展开）。
  收益（至多 128 个玩家）小于实体裁剪，误裁却更显眼 → 等 CScreenRect 基建或实机验证后再做。
- `fabf09a3a3` + `061242fe47` 钩子提示线裁剪：本地是自有 `qm_hook_coll_*` 四件套 + `CQmHookCollVisibility`，
  目标相同、机制不同；按「默认保留 fork 行为」本轮不动，等实机对比。

**证据**

| 检查 | 结果 |
|------|------|
| `game-client` 编译 | 通过（增量 9/9，链接 `DDNet.exe`） |
| `run_cxx_tests` | 45 例失败，与基线逐条一致（无新增、无消失） |
| quick 门禁 | 除 worktree junction 造成的子模块前置检查外全部通过 |

**Gap（必须说明）**：本环境**无法做视觉验证**。裁剪属于「少画了东西」类改动，必须在游戏内确认
屏幕边缘不出现投射物/激光/拾取物凭空消失。已通过对齐上游边距降低风险，但仍需维护者实机确认。

分支：`sync/slice-4-entity-culling`（1 个提交，堆叠在 `sync/slice-3-prediction-init` 之上）。

## 2026-09-23 · S3：预测初始化修复（已执行）

目标：吃掉 S4 簇里**不改变手感语义**的正确性修复（未初始化成员、首次 Read 前的武器状态），
把手感敏感项（antiping 三态、激光门预测解耦、input 提前）继续挂起等实机验证。

| 上游提交 | 主题 | 落地 | 判定 |
|----------|------|------|------|
| `8edc5a3c21` | `prediction: Initialize tune zone settings`（`m_TuneZone`/`m_TuneZoneOverride`） | `723e42c706` | TAKE-UPSTREAM（`7820407734` 是它的 revert-of-revert，内容相同，无需重复摘） |
| `c2a0621bb1` | `client: More pred character initializations`（`m_LastSnapWeapon`、`m_Core.m_ActiveWeapon`、首次 Read 兜底） | `97dd4e0f35` | TAKE-UPSTREAM |
| `d62e5de553` | `CLaser`/`CPlasma` 的 `m_Layer`/`m_Number` 未初始化 | `ef74806a6c` | TAKE-UPSTREAM（**手工等价落地**：上游该补丁依赖上游版 `laser.h`，本地头文件结构与行上下文不同，改为按上游语义手工改 4 个文件） |

`d62e5de553` 的手工落地内容：`laser.h` 第二构造函数参数 `CLaserData *` → `const CLaserData *`；
client `laser.cpp` 补 `m_Number = pLaser->m_SwitchNumber; m_Layer = m_Number > 0 ? LAYER_SWITCH : LAYER_GAME;`；
server `laser.cpp` / `plasma.cpp` 构造函数补 `m_Number = 0; m_Layer = LAYER_GAME;`，`plasma.cpp` 补
`#include <game/mapitems.h>`（本地 include 顺序与上游不同，需跑 `fix_style.py` 才过格式门禁）。

**证据**

| 检查 | 结果 |
|------|------|
| `game-client` 编译 | 通过（增量 13/13，链接 `DDNet.exe`） |
| `run_cxx_tests` | 45 例失败，与基线逐条一致（无新增、无消失） |
| quick 门禁 | 除 worktree junction 造成的子模块前置检查外全部通过 |

分支：`sync/slice-3-prediction-init`（3 个提交，堆叠在 `sync/slice-2-demo-and-lowrisk` 之上）。

**继续挂起（需要维护者实机确认后再动）**：

- `178da1eade client: Handle input earlier`（60Hz 下提前约 16ms）—— 唯一直接改善输入延迟的上游改动，
  但会与本地 `qm_fast_input_mode` / 自适应边距叠加，必须在游戏内验证手感；
- `ac566d06f5`+`9d8a20bd0d` 激光门预测解耦（关武器预测时会穿门，碰撞语义敏感）；
- `4af8164f26`+`b2dc98ca29` 动态 antiping 玩家预测（`cl_antiping_players` 0..3，且 `AntiPingPlayers()`
  由 bool 改 int，需与 `FastPractice.ForcePredict*` 重排）。

## 2026-09-23 · S2：demo 防御修复 + 首批低风险吸收（已执行）

目标：把四簇对照表里风险最低、收益明确的一批上游修复落到分支。结果：**8 条落地，1 条 DEFER**。

| 上游提交 | 主题 | 落地提交 | 判定 |
|----------|------|----------|------|
| `350d0398cc` | demo timeline marker 非法值 UB（含 `in_range` 校验、提前 `ScanFile`） | `c6a396331a` | TAKE-UPSTREAM |
| `887299f3e7` | delta snapshot 早于 full snapshot 时停止回放 | `8783cb1e3c` | TAKE-UPSTREAM |
| `ca4363fe0c` | ScanFile 无关键帧时不再崩溃 | `5453308760` | TAKE-UPSTREAM |
| `fcd79b9226` | 聊天草稿复位（2 行，与本地 `QmChatSaveDraft` 并存） | `4a41522174` | TAKE-UPSTREAM |
| `1cfa7f927a` | 表情抖动公式 `sin(5w)` → `sin(2πw)` | `8f2bdd64e9` | TAKE-UPSTREAM |
| `45c684bc46` | `community_icons` job 状态竞态 | `a165df517d` | TAKE-UPSTREAM |
| `624f58e477` | 虚拟返回键断言崩溃 | `c631f94a43` | TAKE-UPSTREAM |
| `04dc39acb8` | 绑定比较器严格弱序 | `4a7009145e` | TAKE-UPSTREAM |
| `3e7e479de1` | 0.7 皮肤删除后选中替代项 | — | **DEFER**：依赖上游设置页拆分产物 `menus_settings_tee7.cpp`（本地是 `menus_settings7.cpp`），留到 S5 按页落位时处理 |

**核对方式**（避免「看着像就合了」）：逐条 `git patch-id` 比对，4 条完全等价；另 3 条 patch-id 不同，
但把增删行集合直接对比后确认**内容完全一致**（差异只来自 hunk 上下文/行号），第 4 条（`ca4363fe0c`）等价。
另外逐条人工读 diff，确认 chat / players / community_icons / ui / menus_settings_controls 的改动都落在
本地同名逻辑上，没有覆盖 QmClient 自有分支。

**证据**

| 检查 | 结果 |
|------|------|
| `game-client` 编译 | 通过（增量 14/14，链接 `DDNet.exe`） |
| `run_cxx_tests` | 45 例失败，与基线逐条一致（无新增、无消失） |
| quick 门禁 | 10 项通过；唯一失败仍是 worktree junction 造成的子模块前置检查（环境） |

分支：`sync/slice-2-demo-and-lowrisk`（8 个提交，堆叠在 `sync/slice-1-base-engine` = `eff90a0632` 之上）。



## 2026-09-23 · S1 切片：零重叠 cherry-pick 试验

### 目标

按计划 S1 摘取「零重叠」上游提交（只碰 fork 从未改过的文件），验证 cherry-pick 路线的真实成本。

### 执行

- 主工作区有 19 个未落定文件（维护者自己的在途改动），因此不动主工作区：
  新建分支 `sync/slice-1-base-engine` + worktree `tmp/sync-slice-1`（`tmp/` 已被 gitignore）。
- 候选 14 条，取自 `--clean-list` 的 71 条零重叠提交中与 QmClient 目标相关的部分。

### 结果：3 条落地、1 条已存在、10 条失败

**落地（已提交，按时间顺序）**

| 上游提交 | 实际落地文件 | 说明 |
|----------|--------------|------|
| `c6757b72e` Don't stop receiving packets on empty datagrams | `src/base/system.cpp`（+29/-19） | git 把上游 `net.cpp` 的改动按重命名映射到 fork 的 `system.cpp`；**重命名映射结果必须人工核对**，不能当作机械移植 |
| `adac08c6c` shared: Inline `CVariableInt::Pack` | `compression.cpp` / `compression.h`（+32/-32） | 与文件布局无关，干净落地 |
| `c63b1b944` Use `std::size` instead of hard-coding size | `opengl_sl_program.cpp`（+4/-2） | 干净落地 |

**已存在**：`5b147f6ac`（`src/engine/shared/snapshot/delta.rs`）cherry-pick 为空提交 ——
说明 fork 此前已手工带上该修复，历史上确实做过选择性移植。

**重命名映射的人工核对**（`c6757b72e` → `system.cpp`）：逐行比对上游 `net.cpp` 与落地后的
`system.cpp` hunk，`net_udp_recv` 的改动内容完全一致（`while` 循环跳过空数据报 + `do/while` 接收重试），
只有文件名与行号不同 → 该条判定为语义等价，可保留。

**失败 10 条**，原因两类：

1. 上游重构了文件布局，fork 仍是旧布局（6 条，表现为 modify/delete 冲突）：
   - `8c131294a`、`fb1d4b86d`、`44e7f84fa`、`d3810ba51`、`9931d2dc9` → 上游 `src/base` 已模块化
     （`os.cpp` / `process.cpp` / `net.cpp`），fork 仍是 `src/base/system.cpp` 单体；
   - `6f941a85f` → `src/engine/client/backend_threaded.cpp`（上游 backend 拆分产物）；
   - `3600493b9` → `src/game/client/assets.cpp`（上游 gameclient 资源重构产物）；
   - `c32720d3d` → `src/game/client/components/menus_settings_ddnet.cpp`（上游设置页拆分产物）；
   - `ebbe3225b` → `src/game/server/playermapping.cpp/.h`（上游新增文件）。
2. 三方合并内容冲突（4 条）：`ca698e390`（`component.cpp` 的 `LocalTime/time` 重构）、
   `8c131294a` 之外的 `d3810ba51` / `9931d2dc9` / `44e7f84fa`（映射到 `system.cpp` 后与 fork 改动撞行）。

### 关键发现（修正计划假设）

1. **fork 的内容基线并不是「19.9」**。实测文件存在性：

   | 文件 | HEAD | 19.9 | 上次同步点 | ddnet/master |
   |------|------|------|-----------|--------------|
   | `src/base/system.cpp` | Y | - | Y | - |
   | `src/base/os.cpp` / `process.cpp` / `net.cpp` | - | Y | - | Y |
   | `src/game/editor/envelope_editor.cpp` | - | Y | - | Y |
   | `src/game/client/assets.cpp`、`menus_settings_*.cpp`、`src/engine/client/backend_threaded.cpp`、`src/game/server/playermapping.cpp` | - | - | - | Y |

   `aec0a1370`（2026-08-05「merge(client): 同步 DDNet 19.9 与开发者认证」，129 文件 +12326/-6741）
   的两个 parent 都是 fork 本地分支，**不是真正的上游 merge** —— 上游内容是被选择性移植进来的，
   所以 git 视角的 merge-base 停在 2025-11-27，「落后 2117 提交」里相当一部分是结构性重构，不是可选功能。
2. **「零重叠 ⇒ 可直接摘」只在双方文件布局一致时成立**。上游重构过的区域（base、backend、
   gameclient 资源、settings、server）必须先整区域对齐布局，再谈逐提交搬运。
3. 逐提交 cherry-pick 只适合布局未变的小修（本轮 3 条），**不足以推进主线同步**。

### 下一步

S1 改为「`src/base` 布局对齐」：把上游的 base 模块化（`os.cpp` / `process.cpp` / `net.cpp` / …）
移植进 fork，同时把 fork 对 `system.cpp` 的自有改动重新分配到新文件；完成后按同样的方式对齐
`src/engine/shared`、`src/engine/client`。逐区域做，每区域一次编译 + 门禁验证。

### 门禁与构建证据

`py -3 qmclient_scripts/gate/check_gate.py --mode default`（worktree 内）：

- 通过：quick 层 9 项，含 `fix_style.py -n src/base/system.cpp …opengl_sl_program.cpp …compression.*`
  （本次改动面）、ruff、shellcheck、头文件 guard 等；
- 失败 3 项，均为**环境原因**而非代码问题：
  - `Git 子模块前置检查`：worktree 的 `ddnet-libs` 是指向主仓库的 junction，其 `.git` 指向
    `.git/modules/ddnet-libs`，在 worktree 中不存在，故检查失败；
  - `CMake run_cxx_tests` / `run_rust_tests`：worktree 的 `cmake-build-release` 尚未配置
    （`is not a directory`）。
- worktree 还需要两个 gitlink 子模块：`vendor/minhook`、`vendor/msdfgen`（连同 `ddnet-libs` 一起用 junction
  指向主仓库），否则 configure 会报 `vendor/minhook ... does not contain a CMakeLists.txt`。

### 构建与测试结果（2026-09-23）

| 检查 | 命令 | 结果 |
|------|------|------|
| quick 层源码卫生 | `check_gate.py --mode quick` | **通过**（含对本次 4 个改动文件的 `fix_style.py -n`、ruff、shellcheck、头文件 guard） |
| C++ 测试 | `--build cmake-build-release --target run_cxx_tests -j 12` | testrunner **编译通过**，运行 **45 例失败** |
| Rust 测试 | 同上 `--target run_rust_tests` | **通过**（exit 0） |
| 客户端编译 | 同上 `--target game-client` | **通过**（412/412，链接出 `DDNet.exe`） |

45 例失败的归因：全部落在 QmClient 自有测试套件（`QmMonitoringHelpers` 15、`QmNewUiMenuBranches` 11、
`Skins` 4、`QmHudMediaIsland*` 5、`QmTitleStyle` 2，其余 `QmConsoleLogFilter` / `QmAxiomScoresComponent` /
`CServerBrowserFilterTest` / `QmTeeTrailStyles` / `QmUiScaleSource` / `QmMediaIslandGpuSdfContract` /
`GraphicsRenderTargetBackbufferCapture` / `QmClient` 各 1），对应测试文件正是维护者当前在途改动的那批
（`qmclient_monitoring_test.cpp`、`qm_new_ui_menu_branch_test.cpp`、`skins_test.cpp`、
`qm_hud_media_island_logic_test.cpp`、`render_target_test.cpp`、`voice_core_test.cpp`、
`qm_console_log_filter_test.cpp`，以及被改源码 `qm_title_style.cpp`、`axiom_scores.cpp`）。
本切片 3 个提交只碰 `src/base/system.cpp`、`compression.*`、`opengl_sl_program.cpp`，与这些套件无关。

**结论与缺口**：本切片的编译证据成立（`game-client` 与 `testrunner` 均编译通过、Rust 测试通过）；
但 C++ 测试暂时不能作为验收口径 —— 需要先落定在途改动，再跑一次基线（HEAD 与 HEAD+切片各一次），
才能把「45 例失败」与切片彻底解耦。

### `src/base` 差距实测（S1' 依据）

- fork 早已选择性移植了上游 base 拆分的大部分：`dbg.cpp`、`mem.cpp`、`secure.cpp`、`sphore.cpp`、
  `thread.cpp`、`time.cpp`、`windows.cpp`、`crashdump.h` 均已存在，与 19.9 的差异只有 1–22 行。
- 仍缺的**全部**拆分类文件：`net.cpp`(1252 行) / `io.cpp`(253) / `aio.cpp`(284) / `os.cpp`(261) /
  `process.cpp`(119) 以及对应头文件 `net.h` / `io.h` / `aio.h` / `os.h` / `process.h` —— 这些代码在 fork 里
  仍留在 `src/base/system.cpp`（2301 行）与 `system.h` 中。
- fork 的 `system.cpp` 相对上次同步点自有改动为 **+182 / -1740** —— 主要是把代码搬去新文件，
  真正自有新增只有 182 行，对齐成本可控。

→ **S1' 提案**：按上游的切分方式把 fork 的 `system.cpp` / `system.h` 落成独立文件，
并把 fork 自有改动重新分配进去。属引擎核心改动，按仓库约定需维护者批准后执行；详细方案见下节。

### S1' 执行方案（待批准）

映射实测（`py -3 tmp/analyze_base_split.py`，只读）：

| 上游文件 | 行数 19.9/master | fork `system.cpp` 命中顶层函数 | 说明 |
|----------|------------------|-------------------------------|------|
| `net.cpp` | 1252 / 1301 | 50 / 50 | 完整命中，可整段搬 |
| `io.cpp` | 253 / 253 | 全部（`io_*` 系列） | fork 无 `io.cpp` / `io.h` |
| `aio.cpp` | 284 / 284 | 全部（`aio_*` 系列） | fork 无 `aio.cpp` / `aio.h` |
| `os.cpp` | 261 / 261 | 4 / 6 | `os_open_file` / `os_open_link` 在 fork 里叫 `open_file` / `open_link` |
| `process.cpp` | 119 / 119 | 0 / 4 | fork 用旧名：`shell_execute` / `kill_process` / `is_process_alive` / `pid` |

fork 自有内容（必须保留）：`system.cpp` 相对上次同步点的自有新增只有 **182 行**，集中在
**Windows QoS 网络优先级**（`NETQOS_INTERNAL`、`SQwaveApi`、`net_qos_add_socket` / `net_qos_remove_socket`）
与 `sphore.h` / `thread.h` / `windows.h` 引用；`system.h` 自有新增 **57 行**（其余 -1118 行是声明已搬去新头文件）。

改名影响面（机械替换 + 编译可验）：`pid` 11 处、`shell_execute` 5 文件、`open_link` 5 文件、
`kill_process` 3 文件、`is_process_alive` 3 文件、`open_file` 3 文件。

步骤：

1. 以上游文件为基线取 `io.cpp` / `aio.cpp` / `net.cpp` / `os.cpp` / `process.cpp` 与对应头文件；
2. 把这 182 行 QoS 自有实现并入（放哪见下方待决项）；
3. 全仓替换改名符号：`shell_execute→process_execute`、`pid→process_id`、`kill_process→process_kill`、
   `is_process_alive→process_is_alive`、`open_file→os_open_file`、`open_link→os_open_link`；
4. 删除 `system.cpp` / `system.h`，更新 CMake 源列表（涉及根 `CMakeLists.txt`，属默认不改文件，需一并授权）；
5. 验证：worktree 内 configure + `run_cxx_tests` + 构建 `game-client`。

待维护者决定：

- (a) 是否批准该切片（引擎核心改动）；
- (b) QoS 自有代码放上游同名 `net.cpp`（后续仍会冲突），还是抽成 QmClient 自有文件
  （建议：抽成自有文件，后续同步零冲突，代价是改动面稍大）。


### 证据

```
py -3 qmclient_scripts/support/upstream_status.py --conflicts --json tmp/upstream-status.json
git worktree add -b sync/slice-1-base-engine tmp/sync-slice-1 HEAD
git -C tmp/sync-slice-1 cherry-pick -x <hash>        # 14 条逐条执行，失败即 --abort
py -3 qmclient_scripts/gate/check_gate.py --mode default   # 在 worktree 内执行
```

- 分支 `sync/slice-1-base-engine`（worktree `tmp/sync-slice-1`）现有 3 个上游提交，
  `src/base/system.cpp`、`compression.*`、`opengl_sl_program.cpp` 为唯一改动面。
- worktree 位于 gitignore 的 `tmp/` 下，用于隔离主工作区的在途改动；清理 `tmp/` 前注意先
  `git worktree remove`。
