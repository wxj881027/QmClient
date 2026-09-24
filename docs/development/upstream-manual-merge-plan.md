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

## 4. 建议的推进顺序（按「冲突块数」重排，**已修正上一版**）

上一版按「双方改动行数」排序，把 `scoreboard.cpp` 列进了性价比候选 —— 实测它有 **50 个冲突块**，
按文件融合并不便宜。原因是**行数看不出双方是否重写了同一批函数**。改用逐文件三方合并
（`git merge-file` 的 base/ours/theirs）得到的**冲突块数**后，排序才可靠
（工具已内置：`--file-conflicts`，见下表；数值越小越便宜）。

| 顺序 | 文件 | 冲突块 | 上游改动 | 为什么排这个位置 |
|------|------|-------:|---------:|------------------|
| 1 | `ui_scrollregion.cpp` | **6** | 421 | 冲突最少而上游内容最多，**性价比最高** |
| 2 | `ui.cpp` | 17 | 842 | 上游内容最多的一档，冲突中等 |
| 3 | `controls.cpp` / `ui.h` / `components/console.cpp` | 2 / 4 / 8 | 69 / 70 / 144 | 便宜的小修，适合当热身 |
| 4 | `prediction/entities/character.cpp` | 15 | 281 | 上游内容有意义，但属**预测/手感** → 必须实机对比 |
| — | `nameplates.cpp` / `chat.cpp` / `skins.cpp` | 11 / 12 / 9 | 43 / 49 / 72 | 冲突不多但上游内容也少：**顺手摘小修**即可，不必整体融合 |
| ✗ | `scoreboard.cpp` | **50** | 683 | **降级**：冲突块最多的一档，不适合按文件融合（可只挑单条渲染修复试摘） |
| ✗ | `gameclient.cpp` / `menus_demo.cpp` / `menus_browser.cpp` / `menus.cpp` | 36 / 31 / 28 / 26 | 1640 / 199 / 259 / 558 | 冲突多且本地重写主导，**不做** |

每个文件开工时的验收方式同上一版：对应区域的 C++ 测试 + 具体实机场景（例如
`ui_scrollregion.cpp` 要验证「弹窗遮挡下拉时不能点到滚动条」与触摸横向滚动）。

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
