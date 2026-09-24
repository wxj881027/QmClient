# DDNet 上游同步：待实机/行为验证清单

本文件列出「已落地但本环境无法验证」的改动。每条给出：改了什么、怎么验、看什么、判定标准、相关提交。
配套记录见 `docs/development/upstream-sync-log.md`，流程见 `docs/development/upstream-sync-plan.md`。

> 门禁口径（2026-09-23 更新）：维护者提交在途改动后，既有失败从 45 例降到 **2 例**，且这 2 例在纯基线
> `aea1453cc7` 上同样失败（都是维护者新 UI 的源码断言）。因此现在可以直接比较「链上失败集合」与
> 「纯基线失败集合」来判定回归 —— 两者一致即为零回归。`--mode default` 已接近可用作验收口径。

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

## 4. 需要决策后才能验的（尚未落地）

| 项 | 现状 | 需要什么 |
|----|------|----------|
| `aio.cpp` C6262（64KB 栈帧） | `--mode full` 下 MSVC /analyze 报 `src/base/aio.cpp(78)`（上游 19.9 原样代码，运行在 `aio_thread` 独立线程） | 你拍板：加白名单（`refresh_allowlist.py`，保持与上游一致）或本地改为不占栈（门禁干净但偏离上游） |
| 控制台「引号参数也校验」 | 已做成**可选分支** `sync/opt-console-strict-args`（2 提交，构建与测试均通过）；未并入同步链 | 你拍板：采用上游严格语义，或保持 fork 现状 |
| windowed fullscreen 边框（`478781ad65`） | **未落地**：上游把 windowed fullscreen 改成有边框，而本地 `QmWindowModes` 两条测试锁定无边框；落地会让测试变红 | 你拍板：要「snippet 工具可用」还是「保持无边框外观」 |
| rescue 覆盖 `m_DDRaceState`（`b9c39900a9`） | 未落地：触及服务端玩法语义 | 你定夺（属既有玩法语义改动） |
| server 超时重置 snapshot（`ed2b08f5d9`） | 未落地：属 snapshot/时序语义 | 按仓库约束需单独批准 |
| 低刷新率/更新率时序（`9ed3b24d9d`） | 未落地：本地是自有 `WaitWithNetwork` 实现，需整体融合 | 实机看帧率与输入延迟后再决定 |
| 动态 antiping 玩家预测（`4af8164f26`+`b2dc98ca29`） | 未落地：`cl_antiping_players` 0..3，且 `AntiPingPlayers()` 由 bool 改 int，需与 `FastPractice.ForcePredict*` 重排 | 手感验证 + 配置语义确认 |
| 激光门预测解耦（`ac566d06f5`+`9d8a20bd0d`） | 未落地 | 碰撞语义验证（关武器预测时不应穿门） |
| MySQL SSL 支持（`9f59dcb1f6`+`78d8f82c52`） | 未落地：本地 `mysql.cpp` 完全没有 SSL 支持，属新增功能 | 是否需要该功能 + 第三方库/CI 影响评估 |
| rejoin 客户端状态（`4e4536bdae`+`4e25f792ea`） | 未落地：本地没有 `m_Rejoining`/`m_IngameBeforeRejoin`，属新增功能 | 是否需要该特性 |
