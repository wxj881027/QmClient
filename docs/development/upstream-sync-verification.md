# DDNet 上游同步：待实机/行为验证清单

本文件列出「已落地但本环境无法验证」的改动。每条给出：改了什么、怎么验、看什么、判定标准、相关提交。
配套记录见 `docs/development/upstream-sync-log.md`，流程见 `docs/development/upstream-sync-plan.md`。

> 门禁口径（2026-09-24 更新）：同步链基线已前移到 `origin/master`（`6b9a41fd21`），链尾
> `run_cxx_tests` 为 **3348 运行 / 3347 通过 / 1 环境跳过 / 0 失败**。因此验收口径从
> 「链上失败集合 = 纯基线失败集合」升级为「**链上零失败**」；若出现失败，先确认本地基线
> 是否已是 `origin/master` 最新状态（本次就差点把维护者已修好的 2 例记成既有失败）。

## 1. 渲染裁剪（视觉）

| 项 | 内容 |
|----|------|
| 改动 | 屏幕外投射物/激光/拾取物裁剪（`items.cpp`，边距 ±1/±0.5/±1.75 tile）；ghost 屏外裁剪（`ghost.cpp`，200×200 盒） |
| **自动化覆盖（2026-09-23 新增）** | `src/game/client/components/qmclient/qm_item_culling_logic.h`（边距常量 + 点/线段判定）与 `src/test/qm_item_culling_logic_test.cpp`（6 例：边距数值、闭区间边界、投射物/拾取物/激光/ghost 判定）；`QmItemCulling.*` 全过 |
| 仍需人工（只剩观感） | 进人多/ghost 多的场景与快速平移缩放，确认屏幕边缘实体不会凭空消失、跨边缘半可见实体正常绘制、帧率无回退 |
| 提交 | `d009e8899f`（S4）、`f699df66c2`（S8）、`12d878735e`（S9）、`d1ea0877d3`（S24 抽逻辑 + 测试） |

## 2. PNG 读写（皮肤与截图）

| 项 | 内容 |
|----|------|
| 改动 | `SavePng` 改为逐行写图；`LoadPng` 把 `png_create_info_struct` 提到 `setjmp` 前并对 longjmp 后读取的局部量加 `volatile` |
| **自动化覆盖（2026-09-23 新增）** | `src/test/image_test.cpp`：`PngSaveLoadRoundTripPreservesPixels`（RGBA、5×7 奇数尺寸）、`PngSaveLoadRoundTripPreservesRgbPixels`、`PngLoadRejectsTruncatedAndCorruptData`；`Image.*` 13 例全过 |
| 仍需人工（降级为顺手看） | 游戏内加载自定义皮肤、导出截图/皮肤 PNG 能正常显示与回读；非法 PNG 只报错不崩溃 |
| 提交 | `ec2b417880`（S7）、`50150d114c`（测试） |

## 3. 客户端行为（低风险，快速自查）

| 项 | 验证方式 | 判定 | 提交 |
|----|----------|------|------|
| 钩子提示线裁剪（本地 `qm_hook_coll_*` 机制 vs 上游两条提交） | **自动化**：`src/test/qm_hook_coll_visibility_test.cpp`（7 例）+ 既有的 `ddnet_19_9_sync_test.cpp`（3 例）覆盖「无可传送钩子时按距离裁剪 / 有则完全不裁 / legacy 语义 / Reach 边界」 | `QmHookCollVisibility.*` 10 例全过；只剩观感需实机 | 本地机制（S8 审计 KEEP-FORK）、测试 `74d06bf6c3` |
| 短 demo 跳过时长 | 打开一个 <1s 的 demo，切换跳过时长选项 | 0.1s/0.5s 正常显示，下标不越界、不崩溃 | `82eff0207d`、`49c0936dbc` |
| 聊天草稿复位 | 输入半句 → 发送 / 取消 → 再开聊天；再试历史上翻 | 草稿状态正确复位，不残留 | `4a41522174` |
| 控制台/rcon 粘贴 | 往控制台和 rcon 粘贴大段多行文本 | 不崩溃；换行按预期处理 | `5d096a456a`、`51ee2e66f5` |
| 表情抖动 | 连续做表情，观察动作收尾 | 结束平滑无突跳 | `8f2bdd64e9` |
| 社区图标 | 切换社区、刷新图标列表 | 不崩溃，图标正确 | `a165df517d` |
| 虚拟返回键 | 触屏/虚拟返回键反复进出菜单 | 不触发断言崩溃 | `c631f94a43` |
| 绑定列表顺序 | 控制设置里查看绑定列表 | 无修饰键的绑定排在带修饰键之前 | `4a7009145e` |
| demo 标记 | 停止录制后再触发 `add_demomarker` | 不再触发断言（debug 构建尤其） | `c6a396331a`、`b3d7652627` |

## 4. 已拍板并落地：四项官方语义（2026-09-24）

维护者拍板「改用官方实现」，四条已并入链尾切片 `sync/slice-23-official-semantics`（`1539bda58f`）：

| 上游提交 | 落地内容 | 自动化证据 | 仍需人工 |
|----------|----------|------------|----------|
| `821d5ae4b4` | 控制台引号参数也参与校验（非法值一律不派发） | `QmEmoteCommandsTest.PreservesExistingIntegerParsing`（`"invalid"` 期望改为不派发） | 无（纯解析语义） |
| `478781ad65` | windowed fullscreen 改回**有边框**；启动不再标记 `INITFLAG_BORDERLESS` | `QmWindowModes.WindowedFullscreenIsBorderedAndNonResizable`、`QmWindowModes.StartupDoesNotMarkWindowedFullscreenAsBorderless` | **实机确认外观**：切「窗口化全屏」应有边框；顺带验证 Windows 截图工具可用；确认纯窗口模式的 `qm` 无边框开关仍生效 |
| `b9c39900a9` | rescue 不再覆盖 `m_DDRaceState` | 无新增测试（服务端行为，本地无锁定旧行为的测试） | **服务端实机**：/rescue 后 DDrace 状态（是否算完成/计时）与上游一致 |
| `ed2b08f5d9` | 超时恢复旧连接时重置 snapshot | 无新增测试（需要双连接时序） | **服务端实机**：断线重连（timeout protection）后不出现 delta 基线错乱 |

## 5. 门禁豁免登记：MSVC `/analyze` 的上游 `src/base` 告警

`strict_build.py` 的 `_ANALYZE_UPSTREAM_BASE_ALLOWLIST` 按「文件 + 警告码」豁免下列告警，
只在 `/analyze` 阶段生效（自有代码、`src/base/unicode/*` 等不受影响，有 gate 单测锁定）。
这些文件都是 S1' 采纳的上游实现，改它们等于偏离上游，故选择「精确豁免 + 留理由」：

| 文件 | 警告码 | 行 | 理由 |
|------|--------|----|------|
| `src/base/aio.cpp` | C6262 | 78 | `aio_thread` 的 64KB 栈缓冲，上游原样实现 |
| `src/base/io.cpp` | C6308 ×4 | 106/120/134/137 | 上游 `realloc` 惯用写法（先赋值、后判空），语义与上游一致 |
| `src/base/io.cpp` | C28182 | 139 | 同一处 `realloc` 路径的连带告警 |
| `src/base/os.cpp` | C6011 | 51 | 上游 argv 构造路径，analyzer 未覆盖其前置保证 |
| `src/base/os.cpp` | C6388 / C6387 ×2 | 160/167 | `GetFileVersionInfoW` / `VerQueryValueW` 返回值静态分析误报 |
| `src/base/net.cpp` | C6011 ×3 | 922/1309/1389 | 上游 `net.cpp` 的 socket 生命周期分析误报 |

复现方式（重要）：`/analyze` 是**增量**检查 —— 源码没改动时 ninja 会跳过编译，检查报 PASS 并不代表
跑过。要复核就先 touch 分析范围内的文件，再跑 `--mode full`，并核对报告里
`MSVC /analyze 触发范围` 与 `MSVC /analyze 上游豁免` 两条的规模。

## 6. 仍需要决策后才能验的（尚未落地）

| 项 | 现状 | 需要什么 |
|----|------|----------|
| 低刷新率/更新率时序（`9ed3b24d9d`） | 未落地：本地是自有 `WaitWithNetwork` 实现，需整体融合 | 实机看帧率与输入延迟后再决定 |
| 动态 antiping 玩家预测（`4af8164f26`+`b2dc98ca29`） | 未落地：`cl_antiping_players` 0..3，且 `AntiPingPlayers()` 由 bool 改 int，需与 `FastPractice.ForcePredict*` 重排 | 手感验证 + 配置语义确认 |
| 激光门预测解耦（`ac566d06f5`+`9d8a20bd0d`） | 未落地 | 碰撞语义验证（关武器预测时不应穿门） |
| MySQL SSL 支持（`9f59dcb1f6`+`78d8f82c52`） | 未落地：本地 `mysql.cpp` 完全没有 SSL 支持，属新增功能 | 是否需要该功能 + 第三方库/CI 影响评估 |
| rejoin 客户端状态（`4e4536bdae`+`4e25f792ea`） | 未落地：本地没有 `m_Rejoining`/`m_IngameBeforeRejoin`，属新增功能 | 是否需要该特性 |
| 平台缺口（`platform-feature-gaps.md` §4） | 非 Windows 上一批设置项「看得见、点了没用」 | 是否按平台隐藏/标注这些卡片 |
