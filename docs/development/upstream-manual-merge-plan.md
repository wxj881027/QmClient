# 手工融合方案：按区域收编上游改动（决策稿）

本文回答「零重叠可摘基本吃完之后，剩下的差距怎么收」。结论来自四轮不同角度的测量
（S22 收益递减、S31 缺口热点、S39 冲突分布、S41 独立可摘只剩 12 条），
**唯一可行的下一步是按区域手工融合 + 实机验证**。

数据采集工具：`py -3 qmclient_scripts/support/upstream_merge_survey.py --path <区域>`
（只读；输出双方改动文件数、每个文件的双方提交数与增删行数，按「双方都动得多」排序）。

口径提醒：本文的「上游/本地提交数」是窗口 `merge-base(de609e845e)..ddnet/master` 与
`..HEAD` 内**非 merge 提交**计数；「改动」是该区间内该文件的增删行之和。

## 1. 三个区域的总体量

| 区域 | 上游改动文件 | 本地改动文件 | **双方都改过** | 干跑冲突最集中 |
|------|--------------|--------------|----------------|----------------|
| `src/game/client` | 125 | 461 | **98** | 64 |
| `src/engine/shared` | 74 | 80 | **55** | 32 |
| `src/game/editor` | 73 | 62 | **52** | 30 |
| `src/engine/client` | — | — | — | 26 |
| `src/game/server` | — | — | — | 20 |

## 2. `src/game/client` 融合成本排序（前 22）

| 文件 | 上游提交 | 本地提交 | 上游改动 | 本地改动 | 读法 |
|------|---------:|---------:|---------:|---------:|------|
| `gameclient.cpp` | 74 | 295 | 1640 | 5058 | 本地整体重写，**不碰** |
| `components/menus_settings.cpp` | 34 | 323 | 3205 | 10209 | 上游已拆文件（S5 DEFER），**不碰** |
| `components/menus.h` | 25 | 202 | 128 | 2343 | 本地重写 |
| `components/menus.cpp` | 21 | 189 | 558 | 8378 | 本地重写 |
| `gameclient.h` | 32 | 156 | 141 | 547 | 接口被本地改过，**逐条评估** |
| `components/hud.cpp` | 16 | 171 | 173 | 7221 | 本地重写，**不碰** |
| `components/nameplates.cpp` | 8 | 165 | **43** | 2491 | 上游改动小 → **可逐条摘** |
| `components/players.cpp` | 18 | 152 | 226 | 1423 | 多数已在 S2/S8/S9 落地 |
| `components/chat.cpp` | 12 | 155 | 49 | 3023 | 上游改动小 |
| `components/menus_browser.cpp` | 26 | 89 | 259 | 2721 | 中等 |
| `components/scoreboard.cpp` | 32 | 78 | **683** | 1944 | **性价比高**，但含 128 人协议项 |
| `ui.cpp` | 22 | 81 | 842 | 1745 | 中等偏上 |
| `components/chat.h` | 2 | 80 | 14 | 634 | 上游几乎没动 |
| `components/menus_ingame.cpp` | 17 | 64 | 111 | 2009 | 本地重写 |
| `components/menus_settings_assets.cpp` | 5 | 74 | 23 | 7982 | 本地重写，上游几乎没动 |
| `components/skins.cpp` | 9 | 56 | **72** | 3574 | 上游小修可摘 |
| `components/controls.cpp` | 8 | 53 | 69 | 156 | 双方都小 → 可直接融合 |
| `ui.h` | 8 | 52 | 70 | 510 | |
| `prediction/entities/character.cpp` | 25 | 30 | 281 | 358 | **双方都不小 → 性价比高**，但属预测（需实机） |
| `components/menus_settings_controls.cpp` | 7 | 47 | 25 | 474 | |
| `ui_scrollregion.cpp` | 22 | 31 | **421** | 478 | **性价比最高**（双方量级接近） |
| `components/menus_demo.cpp` | 14 | 36 | 199 | 2605 | |

## 3. 上游在这些「性价比候选」里到底做了什么

| 文件 | 上游提交主题（近几条） | 判断 |
|------|------------------------|------|
| `ui_scrollregion.cpp` | `be7fcccd5d` 修复滚动条被弹窗遮挡仍可点；`fdba82286c` 触摸横向滚动；`e88d3d2bc4` 新增 `ContentAreaPos/ContentAreaSize/MaxScroll`；`b26985f15f` `std::min/max` 现代化 | **优先做**：一条真 bugfix + 一条功能 + API 增补 |
| `scoreboard.cpp` | `a9ddb8eda5`/`dbc77978c6` 记分板光标提示重构；`3848ee7f94` 修复高亮盖住玩家名；`a3f21cb336`/`bc927c2a64` 128 人队伍记分板 | **剔除 128 人（协议/需批准）后可做**：两条渲染修复 + 一条提示特性 |
| `nameplates.cpp` | `abfe01656c` 修复「显示自己名牌」；`569edee60b` `CScreenRect`；`5e8efb1f33` `str_copy`；`d22c7e9f84` clang-tidy | **可逐条摘**（上游只 43 行） |
| `players.cpp` | `1cfa7f927a` 表情抖动（S2 已落地）；`061242fe47`/`fabf09a3a3`/`ada53c8cb3` 钩子提示线与裁剪（S8/S9 已等价落地）；`569edee60b`/`73d4d3a915` 基建与现代化 | **大部分已处理**，只剩现代化类小改 |
| `ui_scrollregion.cpp` 同族 `ui.cpp` | 22 条，含 842 行改动 | 需逐条看，与上表同法处理 |

## 4. 建议的推进顺序（每次一个文件、独立验证）

| 顺序 | 文件 | 为什么先做 | 验收方式 |
|------|------|------------|----------|
| 1 | `ui_scrollregion.cpp` | 双方改动量级接近（421 vs 478），上游有一条真 bugfix + 一条功能 | `QmUi`/滚动相关 C++ 测试 + 实机：弹窗遮挡下拉、触摸横向滚动 |
| 2 | `nameplates.cpp` | 上游只有 43 行，可逐条评估 | nameplate 相关测试 + 实机开「显示自己名牌」 |
| 3 | `scoreboard.cpp` | 上游 683 行里有两条渲染修复 + 一条光标提示特性 | 记分板测试 + 实机（含观战、队伍分、光标提示） |
| 4 | `ui.cpp`、`menus_browser.cpp`、`skins.cpp` | 中等量级，逐条评估 | 各自相关测试 + 实机 |
| 5 | `prediction/entities/character.cpp`、`controls.cpp` | 属预测/手感，**风险最高** | 必须实机对比手感，且改前先记录现状 |

## 5. 明确不做的

- `gameclient.cpp`、`menus_settings.cpp`、`menus.cpp`、`hud.cpp`、`menus_settings_assets.cpp`：
  本地重写量是上游的数倍，按文件融合的冲突面远大于收益；继续走「新改某页时按上游命名落位」。
- 128 人记分板 / server info 尺寸（`bc927c2a64`、`d98e1e4ea`）：协议字段改动，需单独批准。
- 上游 `editor` 的 52 个双方改动文件（`editor.cpp` 上游 89 提交/7265 行）：编辑器改动按仓库约定
  逐项批准，且 `editor.cpp` 双方各改数千行，建议作为**独立工作流**排期，不要塞进现在的同步链。

## 6. 风险与不确定性

1. 上表只统计「提交数 / 行数」，**不代表改动语义重要程度** —— 例如 `chat.cpp` 上游只有 49 行，
   但可能是关键 bugfix；逐文件开工前仍需按 S1 的做法读真实 diff。
2. `ui.cpp` / `ui_scrollregion.cpp` 属于本地 `QmUi` 的外围，融合时要确认不会破坏自有滚动策略
   （`QmResolveScrollPolicy` / `CQmScrollState`）。
3. 预测/手感类（`character.cpp`、`controls.cpp`）改动必须实机验证，且要记录改前基线。
4. 本方案**尚未动任何代码**；三个 PR（#263/#260/#261）仍在评审，建议先合并再开手工融合。
