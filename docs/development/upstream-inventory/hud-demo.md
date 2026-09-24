# HUD / Scoreboard / Demo-Ghost 录制 / 聊天 —— QmClient 自有实现 vs 上游 19.9→master 对照表

> 只读分析，未修改任何仓库文件（仅新增本文件）。所有结论基于 `HEAD` 提交内容核验，不基于工作区未提交改动。

## 0. 基线与方法（必读）

| 项 | 值 |
|---|---|
| 本地 HEAD | `78ffa71b7a`（分支 `master`，版本 3.12.x） |
| 上游引用 | `ddnet/master` tip = `a9ddb8eda5`（2026-09-22，已 fetch） |
| 上次同步点（merge-base） | `de609e845e`（2025-11-27 `Fix scoreboard not opening while menu is active (#11324)`） |
| 上游自同步点提交总数（非 merge） | 1258 |
| 本簇文件的上游提交数 | 94（`menus_demo.cpp` / `ghost.cpp` / `scoreboard.cpp` / `scoreboard.h` / `hud.cpp` / `chat.cpp` / `menus_ingame.cpp` / `engine/shared/demo.cpp`） |
| 上游 release 锚点 | 19.9 = 2026-07-09，20.0 = 2026-08-27（`https://ddnet.org/downloads/`），master 已越过 20.0 |

**方法学警告（重要）**：`git cherry HEAD ddnet/master` 说 1258 个上游提交里只有 249 个 patch 等价于本地，`git log --format=%s` 主题比对则命中 579 个 —— 两者都**不可靠**。实例：`b4f6206818`（scoreboard popup with map info）被 `git cherry` 判为「未应用（+）」，但本地 `scoreboard.cpp:388-410`、`scoreboard.h:198` 明确已有 `m_MapTitlePopupContext` / `m_MapTitleButtonId` / `m_aMapDescription`。因此**本表逐条以内容 grep / 读代码核验**，不采信 cherry 判定。

**本地形态判断**：本簇不是「本地小改 + 上游继续走」的常规 fork 关系。`menus_demo.cpp` 本地 3253 行 vs 上游 1618 行（多 1635 行；函数级 48 : 21，**28 个本地独有函数、上游独有仅 1 个**）；`scoreboard.cpp` 本地 2039 vs 上游 1161；`chat.cpp` 本地 3561 vs 上游 1197；`hud.cpp` 本地 6842 vs 上游 1726。这四个文件是**本地整体重写**，上游补丁多数无法直接 `cherry-pick`，必须按语义重挂。

---

## 1. 对照表

| 功能名 | QmClient 位置（文件） | 官方对应能力（提交 / changelog） | 判定建议 | 依据 |
|---|---|---|---|---|
| 交互式记分板（鼠标解锁 / Esc 关光标 / 不抢聊天光标 / 锁鼠标关弹窗） | `scoreboard.cpp` `OnInput`/`OnRelease`/`ConToggleScoreboardCursor`（`m_MouseUnlocked`，约 206-380）；`scoreboard.cpp:1693` `!m_Chat.IsActive()` | `811d8a4a26` + `e9ede56e9e` + `0b35f43ecd`；19.6 changelog「Interactive scoreboard」 | KEEP-FORK | 三项语义本地均已有等价实现（Esc 关弹窗 + 复位鼠标 + 记 `m_LastMousePos` + 聊天激活时不接管 UI） |
| 记分板鼠标提示（"Show cursor"） | 无对应实现 | `f15d3b1f94` add scoreboard cursor hint + `dbc77978c6` rework + `3848ee7f94` fix | TAKE-UPSTREAM | 本地 `Localize` 字符串集合中无 `Show cursor`；3 个修补都在上游，属完整小特性 |
| 记分板地图信息弹窗（点标题弹 map description） | `scoreboard.cpp:388-410`、`scoreboard.h:198`、`CMapTitlePopupContext::Render` `scoreboard.cpp:2274` | `b4f6206818` + 19.8「Add scoreboard popup with map info」 | KEEP-FORK | 本地**已含**（`m_aMapDescription[512]` 见 `gameclient.h:1274`），无需再取 |
| 弹窗打开时保持选中玩家高亮 | 未检出（无 `m_Selected` 类状态位） | `3e89dca02c`；19.9 changelog | TAKE-UPSTREAM | 本地无对应状态位，属纯修复、冲突面小 |
| hover 背景不覆盖完赛时间 | 未检出 | `889297caa6` Fix scoreboard hovered player background drawn over finish time | TAKE-UPSTREAM | 本地渲染顺序大改，需按本地结构重挂该修复 |
| 128 人队伍记分板排版 | `scoreboard.cpp:1952` 本地自写 `NumColumns = 3` / `ceil(128.0f/NumColumns)` + `SUiLayoutChild` | `bc927c2a64` Adjust team scoreboard for 128 players；20.0「128 player support」 | MERGE-BOTH | 两边各写一套（上游 2×64 双列，本地固定 3 列），需人工择一或保留本地 |
| 服务端下发队伍 min/max size + team rank 截断修复 | 无 | `a9c6d2efdf` network: Send min and max team size in game info；`5d449febc3` server: Fix team ranks of large teams being cut off | DEFER | 动 protocol/game info 与服务端，按仓库硬约束默认不做 |
| 记分板/HUD 厘秒·毫秒时间显示 | `scoreboard.cpp:422-435` 自写厘秒；`hud.cpp:7549/7564` `TimeCentiseconds`；`m_MapBestTimeMillis`、`m_ReceivedDDNetPlayerFinishTimesMillis` | `5fda31e735` + `31372ca680` + `3c5fcab898`；19.8「Show centiseconds in scoreboard and HUD」、19.7.1「Sort players by milliseconds」 | MERGE-BOTH | 本地已能显示厘秒/millis，但**无** `ETimeFormat` 用户开关；可只融合开关而不换渲染 |
| 皮肤名 tooltip / 复制皮肤 | `scoreboard.cpp:2198-2214` + `qmclient/scoreboard_skin.h`（`QmCopyScoreboardSkin`） | `ef974573d3`；20.0「Show skin names in scoreboard tooltips」 | KEEP-FORK | 本地是上游超集（多出 `Copy skin` 与 0.7 屏蔽分支） |
| 战队/名字被截断（小分辨率、clan offset） | `scoreboard.cpp:1159` `ClanOffset/ClanLength` 旧公式，无收缩保护 | `4239b0c144`、`80fba7dd7b`、`bc927c2a64` 中 `NameLength` 收缩 | TAKE-UPSTREAM | 本地 `ClanLength = CountryOffset - ClanOffset - 2.5f` 未做 `std::max(0,…)`/收缩，仍有裁剪路径 |
| 记分板文本容器跨帧缓存（性能） | 未使用 `CCachedText` | `5b10b49e16`；20.0「Cache scoreboard text containers」 | DEFER | 本地记分板渲染路径已重写，性能补丁冲突面大、收益需实测 |
| 屏幕外实体裁剪（`CScreenRect`，含 ghost/hook/player） | `ghost.cpp:317` 仍是旧签名 `RenderHook(&Prev,…)`/`RenderPlayer(&Prev,…)`；本地无 `CScreenRect` | `569edee60b` + `ada53c8cb3`；20.0「Stop rendering off-screen entities, players, hooks, ghosts and spectator chars」 | TAKE-UPSTREAM | 纯性能，跨 `players.cpp`/hook/ghost；本簇文件里 `ghost.cpp` 剩余差异**正好只剩这类基础设施提交**（见 §3） |
| 记分板底部媒体卡片 / 旁观者区 + 队伍模式（practice/team0/lock）记忆 | `qmclient/scoreboard_footer.h`、`qmclient/scoreboard_team_modes.h` + `scoreboard.cpp` | 未找到对应提交（上游仅 18.3 的 team0mode indicator） | KEEP-FORK | 官方只有普通 spectators 列表与布尔指示器，本地为自研卡片页脚 + 状态记忆 |
| HUD 自研布局与状态件（分数胶囊 / 绑定状态行 / 名牌布局） | `qmclient/score_hud_layout.h`、`qmclient/qm_bind_status_hud.{h,cpp}`、`qmclient/nameplate_layout.h` → `hud.cpp`/`nameplates.cpp`（38 个 `QmNameplate*` 配置） | 未找到对应提交（上游只有 `cl_showhud*`/`cl_nameplates*` 布尔开关） | KEEP-FORK | 三者均为本地独有语义（含语义配色状态机与纯函数布局），官方无对应实现 |
| demo 菜单已同步项（鼠标 seek / 按钮 tooltip / 短 demo 跳转时长） | `menus_demo.cpp:722-772`（`m_PausedBeforeSeeking`、`m_PrevSeekAmount`、`SetMouseSlow` + marker 吸附）、多处 `pPlayTooltip` 等、`:303-304/408-417` `m_SkipDurationIndex` 夹取 | `ea670dcc10`；19.8「Demo menu mouse seek improvements」／`366a359146`；19.9「Add tooltips to demo browser buttons」／`d429ac7209` + `59784659d3` | KEEP-FORK | 三项本地均已有等价或超集实现（seek 多出 marker 吸附、本地函数签名多 `TickToSeek`） |
| demo 播放器健壮性（时间轴 marker 校验 / delta 前置校验 / 无关键帧崩溃） | `engine/shared/demo.cpp`：markers 循环无校验；`DoTick` 无 `m_LastSnapshotDataSize == -1` 保护；`ScanFile` 结尾仍是旧式 `ResetToStartPosition(m_vKeyFrames.empty() ? …)` 且提前返回分支不检查 keyframe | `350d0398cc` + `887299f3e7` + `ca4363fe0c`；20.0「Validate and initialize demo player data」 | TAKE-UPSTREAM | 三处 grep 均无命中（`Invalid demo timeline marker`、`Delta snapshot before any full snapshot`、lambda 内 keyframe 检查）；文件未被本地重写，补丁小且纯防御 |
| racefinish 停止 demo/ghost 录制 | `ghost.cpp:673`（`NETMSGTYPE_SV_RACEFINISH`）+ `race_demo.cpp:166` | `69dbf395dc`；19.8「Use racefinish message to stop demo/ghost recording」 | KEEP-FORK（**已 cherry-pick**） | 本地提交 `9fda6e9449` 已落地，两处消息处理与上游逐行一致 |
| ghost 加载校验与错误日志 | `ghost.cpp:479-565`（`FoundCharacterNoTick/FoundCharacterTick` + `log_error_color`） | `0f668018b8` Add more error handling and log messages for ghost loading | KEEP-FORK（**已 cherry-pick**） | 本地提交 `795cf2bd9e` 已落地，diff 与上游逐行一致 |
| 删除 demo 后自动选中下一项 | `menus_demo.cpp` 多选删除流程（`PrepareDemoDeleteTargetsFromSelection`/`SyncDemoSelection`/`NumSelectedDeletableDemos`） | `cda171db1e` client: When deleting demo select the next demo | MERGE-BOTH | 上游是单文件 `DemolistSelectNeighbor()`（本地无此函数），本地是多选批量删除，需按本地流程补「删后选中」语义 |
| demo 浏览器删除目录后 UI 卡死 / 崩溃 | `menus_demo.cpp` 自写 `RenderDemoBrowser*`、`DemoBrowserBaseFolder`、`ResetDemoBrowserFolder` | `bcd2631e8b` + `c10ac3a839` | TAKE-UPSTREAM（需逐条核验） | 上游两修针对其自身目录逻辑；本地目录逻辑已重写，需确认同缺陷是否仍可达 |
| 粘贴多行聊天时替换换行 | `chat.cpp`（**未**注册 `m_Input.SetClipboardLineCallback`）+ `lineinput.cpp:389-393` 默认分支走 `str_sanitize_cc`（`str.cpp:169-178` 把 `<32` 含 `\n` 一律换成空格） | `ced6ea148d` Replace line breaks with spaces when pasting multiple lines in chat；19.8 changelog | KEEP-FORK | 语义已等价：本地没有「逐行立即发送」的旧回调，粘贴即按空格合并 |
| 聊天草稿恢复（发送 / 取消后上翻历史） | `chat.cpp:1261-1298`（`m_EditingNewLine` 机制**本地保留**）；发送与 `m_ClChatReset` 取消分支（约 988-1010 行）**未复位** `m_EditingNewLine = true`；另叠加自有 `m_QmChatSaveDraft`/`SaveDraft()` | `fcd79b9226` Fix chat draft restoration after sending or dismissing a history entry | TAKE-UPSTREAM | 本地保留了上游同一套 `m_EditingNewLine` + `m_aCurrentInputText`，但缺那 2 行复位，上游 bug 在本地同样可复现；重挂时需兼顾本地 `SaveDraft()` |
| HUD 暖场时钟定位与渲染 | `hud.cpp`（本地 6842 行，整体重写） | `de326e250d` Fix warmup timer positioning, cleanup warmup rendering | TAKE-UPSTREAM（需在本地结构上重挂） | 上游为定点修复，本地 hud 结构完全不同，无法直接合 |
| 记分板/HUD 的 `log_*`、`std::fill`、`std::clamp` 等基础重构 | 分散 | `1331cbd0cc`、`87abe2f936`、`b26985f15f`、`609647cacb` 等 | DEFER | 纯重构、无功能增量，建议随大版本整体融合而非逐条搬 |

---

## 2. 本地 demo 菜单相对上游多出来的自有功能清单（合并最大冲突源）

`menus_demo.cpp`：本地 3253 行 / 48 函数 vs 上游 1618 行 / 21 函数；**28 个本地独有函数**、上游独有仅 `DemolistSelectNeighbor()`。

### 2.1 函数级自有块（28 个，全部本地独有）

- 选择与批量删除：`IsDemoItemSelected`、`IsDemoItemDeletable`、`SetDemoSelectionSingle`、`ToggleDemoSelection`、`SelectDemoRange`、`SelectAllDemos`、`SyncDemoSelection`、`NumSelectedDemos`、`NumSelectedDeletableDemos`、`PrepareDemoDeleteTargetsFromSelection`、`PopupConfirmDeleteSelectedDemos`
- 截图浏览：`DemoBrowserBrowsingScreenshots`、`DemoBrowserSupportedFile`、`IsDemoScreenshotPreviewItem`、`ToggleDemoScreenshotPreview`、`SyncDemoScreenshotPreview`、`ResetDemoScreenshotPreview`、`LoadDemoScreenshotPreviewTexture`、`RenderDemoScreenshotPreview`
- 目录/元数据：`DemoBrowserBaseFolder`、`ResetDemoBrowserFolder`、`EnsureDemoDate`、`EnsureDemoSize`、`EnsureAllDemoDates`、`ResetDemoBrowserMetadataProgress`、`AdvanceDemoBrowserMetadata`
- 播放器 UI：`RenderDemoCard`、`RenderDemoDisplaySettings`、`RenderDemoExportDisplayToggle`

### 2.2 功能级自有清单（按本地 UI 文案与常量归纳）

1. **截图浏览器**：Replay / Screenshot 双模式（`Choose whether to browse replays or screenshots`）、截图纸质预览与失败提示（`Could not preview this image`、`Error opening screenshot`、`Unable to open the screenshot file`）、截图重命名/删除/打开（`Rename screenshot`、`Delete screenshot`、`Open the selected screenshot`、`No screenshot selected`）。
2. **多选与批量删除**：范围选择、`%d items selected`、`Are you sure that you want to delete %d selected items?`、部分失败汇总 `%d selected items were deleted, but %d items could not be deleted. The first failing item was '%s'.`。
3. **demo 裁切（cut）增强**：多段 cut 导出列表（`Cut segments`、`Add current cut to the export list`、`Clear current cut (right click to clear all cuts)`、`Current cut is invalid or already added`、`Cut start: %s`、`Cut end: %s`）、cut 预览状态机（`Preview cut (P)` / `Stop preview (P)`、`Preview`）、快捷键 `Set cut start at current position (I, right click to reset)` / `Set cut end at current position (O, right click to reset)`、`Failed to export demo cut`。上游只有单段 `Mark the beginning/end of a cut`。
4. **播放器 transport 栏重排**：十个图标按钮 + 时长选择 + 倍速文字的固定宽度布局（`qmclient/demo_ui.h` 的 `TransportWidth`/`TransportButtonSize`/`SliceContentHeight`/`PlayerRect`/`PopupRect`），以及 `Replay`、`Toggle keyboard shortcuts`、`Selection` 等本地文案。
5. **异步元数据抓取**：`Loading Demo` / `Loading demo files` / `Loading demo info` / `Loading screenshot files`，日期与体积后台抓取并带预算推进（`EnsureDemoDate`/`EnsureDemoSize`/`AdvanceDemoBrowserMetadata`）。
6. **demo 裁切逻辑纯函数化**：`qmclient/demo_cut.h`（`ResolveRange`、`ToCentiseconds` 先乘后除避免短片段截断、`CPreview` 类）。
7. **demo 只读显示配置**：`qmclient/demo_display.h` + 5 个配置 `qm_demo_show_direction` / `qm_demo_show_strong_weak` / `qm_demo_strong_weak_scope` / `qm_demo_show_hud` / `qm_demo_show_chat`（`Demo display`、`Show key presses`、`Hook strength scope`、`Show ingame HUD`、`Show chat`、`Strong Weak Hook`、`Icons`、`Icon and number`）。
8. **本地化文案改写**：`Open the folder containing demo files` / `Open the folder containing screenshots`（上游 `Open the directory that contains the demo files`）、`Open the selected folder`、`Rename demo`/`Rename folder`、`New name`、`Delete selected items` 等分支化文案。上游独有文案仅 3 条（`Mark the beginning of a cut`、`Mark the end of a cut`、`Open the directory that contains the demo files`），本地独有文案 56 条。
9. **同簇内其它自有件**（不在 `menus_demo.cpp`，但属本簇自有能力）：`qmclient/chat_emoji.{h,cpp}`（17 种 `:xx` 贴图表情）、`qmclient/qm_chat_export.h` + `qm_chat_avatar.h`（1080 宽聊天记录导出 PNG，含头像，`console.cpp` 内 `CQmChatExportJob` 异步作业）、聊天翻译子系统（LibreTranslate / LLM / Tencent Cloud / FTAPI + 屏蔽词 + 右键菜单 `Mute player`/`Copy name`/`Spectate`/`Reply`）、`qmclient/local_saves.h`/`local_save_display.h`/`map_history_ui.h`、`tclient/statusbar.*`、`tclient/player_indicator.cpp`、`tclient/warlist.*`、`tclient/swap_countdown_message.*`。

### 2.3 冲突提示

- 上游 `menus_demo.cpp` 的 14 个提交里有 6 个（`1356033f08`/`224dbc588c`/`9e534185b5`/`2302875af1`/`47b389f878`/`4a33f66df4`）是**基础设施改名**（`base/system.h` 移除、`TimestampFormat`、`ETimeFormat`、`font_icons`、`fs` 函数、`std::optional<SHA256_DIGEST>`）。它们会与本地重写行**发生大面积文本冲突**，但语义冲突小；建议按「先做基础设施、再重挂功能」的顺序同步。
- `menus_demo.cpp` 本地 3253 行意味着**任何上游补丁都不能直接 cherry-pick**；`git merge` 该文件预期重度冲突。

---

## 3. 已核对为「已同步完成」的上游条目（无需动作，防止重复劳动）

| 上游提交 | 本地证据 | 备注 |
|---|---|---|
| `69dbf395dc` racefinish 停录制 | `ghost.cpp:673`、`race_demo.cpp:166`；本地提交 `9fda6e9449` | diff 与上游逐行一致 |
| `0f668018b8` ghost 加载错误处理 | `ghost.cpp:479-565`；本地提交 `795cf2bd9e` | diff 与上游逐行一致 |
| `b4f6206818` 记分板地图信息弹窗 | `scoreboard.cpp:388-410/2274`、`scoreboard.h:198`、`gameclient.h:1274` | 被 `git cherry` 误判为未应用 |
| `811d8a4a26`/`e9ede56e9e`/`0b35f43ecd` 交互式记分板三个修补 | `scoreboard.cpp` `OnInput`/`OnRelease`/1693 | 本地未抽出 `LockMouse()`，但行为一致 |
| `ea670dcc10` demo 鼠标 seek | `menus_demo.cpp:722-772` | 三点全有 + marker 吸附 |
| `366a359146` demo 浏览器 tooltip | `menus_demo.cpp` 多处 tooltip | 超集 |
| `ef974573d3` 记分板皮肤名 tooltip | `scoreboard.cpp:2212-2214` | 本地为超集 |
| `ced6ea148d` 粘贴多行聊天 | `chat.cpp` 未注册 clipboard line callback | 默认分支经 `str_sanitize_cc` 已把 `\n` 换空格，语义等价 |
| `aa5d3c2735` `CDemoPlayer::SetPos` 返回 bool | `engine/shared/demo.cpp:1056` | 已同步 |
| `d429ac7209`/`59784659d3` 短 demo 跳转时长 | `menus_demo.cpp:303-304` 索引夹取 | 等价 |
| `3c5fcab898`/`c5c1ad3815`/`d8262dad4d` 毫秒与地图最佳时间 | `gameclient.h:997/1273`、`scoreboard.cpp:434`、`hud.cpp:7549` | 数据面已同步（缺 `ETimeFormat` 开关） |

**结论**：`ghost.cpp` 是本簇**最干净**的文件 —— 本地 HEAD 与 `ddnet/master` 的差异仅剩 `base/process.h`、`TimestampFormat`、`CScreenRect`、`std::max`、`IMap::BaseName()` 等**基础设施/性能**提交，功能上已与上游对齐。建议把 `ghost.cpp` 作为本簇第一个「低风险同步示范」。

---

## 4. 不确定清单（判断不了 / 需人工或运行时验证）

1. **记分板 hover/选中高亮**：`889297caa6`、`3e89dca02c` 在本地是否已有等价实现 —— 本地无 `m_Selected` 类字样，但渲染顺序已大改，无法仅靠 grep 断定缺失；需人工读本地 `RenderScoreboard` 绘制顺序。
2. **队内人数显示 `%d\n(%d/%d)`**（依赖服务端 min/max team size，`a9c6d2efdf`）：本地是否另有等价展示（3 列 + 页脚）未确认。
3. **`bcd2631e8b` / `c10ac3a839`**：本地目录逻辑已重写，同缺陷是否仍可达需实机验证（删除目录 / 进入已删除目录的父级）。
4. **`fcd79b9226` 重挂方式**：本地在 `m_EditingNewLine` 之外还叠了 `m_QmChatSaveDraft`/`m_aSavedInputText`，插入 `m_EditingNewLine = true` 的确切位置需人工确认不会与草稿恢复互相干扰。
5. **`de326e250d` 暖场时钟定位**：本地 `hud.cpp` 整体重写，未逐行核验暖场渲染是否已另有正确实现。
6. **纯重构类**（`87abe2f936`、`1331cbd0cc`、`b26985f15f`、`609647cacb`、`3541e1af8c`、`ad6f1e1013`）：是否已随其它 cherry-pick 顺带进入本地无法逐条确认；无功能增量，不建议单独追。
7. **128 人下哪种排版实际正确**：本地固定 3 列（每列 ≤43）vs 上游 2×64 列，均未做 128 人实机验证。
8. **`5b10b49e16` 记分板文本容器缓存**：本地记分板是否已用其它缓存机制（未检出 `CCachedText`，但本地有自研渲染层）未确认。
9. **工作区脏状态**：`src/game/client/components/hud.cpp` 等工作区文件有未提交改动（如 `RecordingDotScreenPixelSize` 取值时机调整），本分析一律按 `HEAD` 判定；合并评审时需先收口这批改动。

---

## 5. 建议的处理顺序（供父任务参考）

1. **零成本**：确认 §3 已同步项，把这批从同步清单划掉。
2. **低风险高收益**：`ca4363fe0c` + `350d0398cc` + `887299f3e7`（`engine/shared/demo.cpp` 三处健壮性修复）—— 文件未被本地重写，补丁小、仅防御性、不涉玩法语义。另加 `fcd79b9226`（2 行聊天草稿复位）。
3. **低风险示范**：`ghost.cpp` 的基础设施同步（`CScreenRect` / `TimestampFormat` / `base/process.h` / `IMap::BaseName()`）。
4. **需设计决策**：记分板 128 人排版（本地 3 列 vs 上游 2×64 列 + 协议 team size），先定方案再动手。
5. **可延后**：`menus_demo.cpp` 的上游补丁 —— 本地重写幅度大，建议只在有明确用户可见收益（如记分板光标提示、clan 名截断）时逐条重挂。
