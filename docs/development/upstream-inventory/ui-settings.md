# UI / 设置菜单 / 卡片体系 / 皮肤与资源 —— QmClient 自有实现 vs 官方 19.9→master 对照表

- 仓库：`D:\QmClient_code\QmClient`（只读分析，未修改任何受版本控制的文件）
- HEAD：`78ffa71b7a8bf2d56d4279695294ac4177f1d395`（QmClient master，2026-09-22）
- 上次同步点（merge-base）：`de609e845ee1deea0a707c5507b4288a381b4e66`（2025-11-27，`Fix scoreboard not opening while menu is active (#11324)`）
- 上游引用：`ddnet/master` = `a9ddb8eda5e92b5168460e88f62ab0d454c46e65`（2026-09-22，`UI: Rework scoreboard cursor hint (#12903)`）
- 同步区间提交数：`git rev-list --count de609e845..ddnet/master` = **2117**
- 本轮范围：设置菜单拆分 / 设置页渲染 / CUi 弹窗与滚动 / 卡片体系 / 皮肤（0.6 与 0.7）/ 皮肤与社区图标资源加载

判定标签：`KEEP-FORK`（保留自有）、`TAKE-UPSTREAM`（吸收官方）、`MERGE-BOTH`（两边能力都要）、`DEFER`（暂缓，需单独排期）。括号内为「已同步 / 结构不同」等状态说明，不改变四类标签。

## 一、对照表

| # | 功能名 / 菜单页 | QmClient 位置（文件） | 官方对应能力（提交 hash / changelog 条目） | 判定建议 | 依据（一句话） |
|---|---|---|---|---|---|
| 1 | 设置菜单文件拆分（General/Graphics/Sound/Tee/Tee7/Player/Language/Appearance/Credits/DDNet） | `src/game/client/components/menus_settings.cpp`（8260 行单文件，承载 General/Player/Tee/Graphics/Sound/Language/Appearance/DDNet）+ `menus_settings7.cpp` | `e30d6be289`、`c000373eb2`、`c3d1dddb10`、`b824eccf95`、`8c4acbe637`、`8b7b8f4b9a`、`2634a72933`、`f8a83dba1d`、`bc396d820d`、`144603b1f4`（tee7 改名），均在 2026-07-05 | DEFER | 上游拆分是「搬迁 + 顺手清理 / 函数改名」，而本地同文件已叠了约 5600 行自有实现，按文件对齐收益低于冲突成本；只在新改某个设置页时按上游命名落位。 |
| 2 | CUi 弹窗菜单实现拆分 | `src/game/client/ui.cpp:2611` `DoPopupMenu`、`:2660` `RenderPopupMenus`、`:3242` `PopupColorPicker` | `f1dfd5187b`（2026-07-11）新增 `src/game/client/ui_popups.cpp`，自 `ui.cpp` 迁出 646 行 | DEFER | 纯文件重组、无功能变化，本地 `ui.cpp` 已重度 fork（+1749 行），迁移只会放大冲突面。 |
| 3 | 设置全局搜索页（跨页卡片搜索/跳转） | `src/game/client/components/qmclient/menus_qmclient.cpp:5147` `RenderSettingsGlobalSearch`、`:338` `NavigateToGlobalSearchCard` | 未找到对应提交（上游设置页无搜索能力） | KEEP-FORK | 上游无对应实现，是卡片体系独有的入口。 |
| 4 | 全局卡片目录 / SettingsCard 卡片渲染体系 | `src/game/client/QmUi/cards/QmCardCatalog*.cpp`、`QmUi/QmCardRegistry.{h,cpp}`、`QmUi/SettingsCardDeck*.{h,cpp}`、`QmUi/cards/QmCardRenderBridge.cpp` | 未找到对应提交（上游无卡片体系） | KEEP-FORK | stableId + 标题/测量/重测版本/预布局 + `QmCardRenderHook` 桥接是自有架构，上游设置页仍是线性 `DoButton` 堆叠。 |
| 5 | 设置资源预览缓存 / 预热预算（缩略图异步解码与上传限流） | `src/game/client/components/qmclient/settings_resource_preview.{h,cpp}`、`components/settings_resource_jobs.*`、`settings_warmup.*`、`settings_runtime_cache.*` | 未找到对应提交（上游 `src/game/client/components/` 下无 resource/preview/warmup 文件） | KEEP-FORK | 上游无资源预览与预热层，本地为皮肤/资产页性能自研，属独有能力。 |
| 6 | 皮肤数据管线重建（数据规划 / 列表缓存 / 加载优先级） | `src/game/client/components/skins.cpp`（`BuildSkinDataPlan`、`CSkinListPlanJob`、`CSkinContainer::RequestLoad(ESettingsResourcePriority)`；vs merge-base +3574 行） | 上游同窗口对 `skins.cpp/h` 仅小额改动（见 #9~#11），无同构重写 | KEEP-FORK | 本地皮肤管线是自有重写（优先级、warmup、perf 日志），上游结构无法直接替换。 |
| 7 | 皮肤队列与皮肤预设命名 | `components/skins.cpp`（`SKIN_QUEUE_*`、`CSkinQueueEntry`）、`menus_settings.cpp:8719` `PopupSkinQueuePresetRename` | 未找到对应提交 | KEEP-FORK | 上游没有皮肤队列 / 预设概念。 |
| 8 | 官方皮肤索引在线下载与缓存 | `components/skins.cpp:44` `OFFICIAL_SKIN_INDEX_URL`、`LoadOfficialSkinIndexCache`、`QueueOfficialSkinIndexRequest` | 未找到对应提交 | KEEP-FORK | 上游只扫描本地皮肤目录，无 `ddnet.org/skins/.../skins.json` 在线索引。 |
| 9 | 皮肤 metrics 在「下载文件二次加载」时未重置 | `components/skins.cpp:937-983`（`BuildSkinDataPlan` → 12 个 `Data.m_Metrics.*` 字段全量覆写） | `03407db9e6`（2026-08-30，`LoadSkinData` 中补 `Data.m_Metrics.Reset()`） | KEEP-FORK（结构不同，无此 bug） | 本地每次加载都由 Plan 完整重写全部 metrics 字段，不存在上游「同一输出变量复用未清零」的前置条件。 |
| 10 | Refresh 中止皮肤加载任务后未置空导致崩溃 | `components/skins.cpp:2478` 起 `CSkins::Refresh` 循环内 `m_pLoadJob->Abort(); m_pLoadJob = nullptr;` | `364c03c797`（2026-09-02，`Fix crash when refreshing skins while skins are still loading`，上游补的就是这行置空） | KEEP-FORK（已同步） | 本地已具备等价置空处理，上游修复意图已覆盖。 |
| 11 | `events` 设置变更后刷新事件皮肤 | `components/skins.cpp:2449` `RefreshEventSkins`；`gameclient.cpp:766` `pConsole->Chain("events", ConchainRefreshEventSkins, ...)` | `cb34c14ca2`（2025-12-24，`Refresh skins when changing events setting`） | KEEP-FORK（已同步） | 本地已完整移植（含 XMAS `santa` 前缀逻辑与控制台链）。 |
| 12 | 0.7 皮肤删除后自动选中替代项 | `components/menus_settings7.cpp:404` `PopupConfirmDeleteSkin7`（仅记 `m_SelectedSkin7Name`，无索引跟踪） | `3e7e479de1`（2026-08-19，新增 `m_SelectedSkinIndex7` / `m_DeletedSkinIndex7`，删除后 `NewSelected = min(deletedIndex, size-1)`） | TAKE-UPSTREAM | 本地没有「删除后选中替代项」逻辑，删除后列表选中态会丢失，官方实现更完整。 |
| 13 | 0.7 皮肤名校验（防栈溢出 / 非法文件名） | `components/skins7.cpp:419` `str_length(aSkinName) >= sizeof(CSkin().m_aName) \|\| !str_valid_filename(...)` | `47007311e2`（2026-06-13，`Validate 0.7 skin names, fix stack overflow`） | KEEP-FORK（已同步） | 本地已有同款校验（含 `.json` 后缀截断处理），与上游语义一致。 |
| 14 | 社区图标加载线程安全（`IJob::Done()` 含 aborted） | `components/community_icons.cpp:170` `if(pJob->Done()) { if(pJob->Success()) ...` | `45c684bc46`（2026-02-23，改为 `pJob->State() == IJob::STATE_DONE && pJob->Success()`） | TAKE-UPSTREAM | 本地仍是旧写法，aborted 任务可能仍在运行即被读结果，存在与上游同样的竞态。 |
| 15 | 社区图标下载并发上限 / 超时 / 解码健壮性 | `components/community_icons.cpp`（`MAX_CONCURRENT_ICON_DOWNLOADS = 4`、`CTimeout{10000,30000,500,10}`、`CImageLoader::LoadPng`、可选 `IconSha256()`） | 未找到对应提交 | KEEP-FORK | 上游无并发上限与下载超时，本地增强更适合公共社区图标源（含失败退出与灰度图拷贝判空）。 |
| 16 | 国家/地区旗帜校验与玩家页国家筛选 | `components/countryflags.cpp:54/78` `ValidateCountryCodeString` / `ValidateCountryCodeIntegerString`；`menus_settings.cpp:1032` `PopupSettingsCountrySelection` | `6ee3c8dffd`（2026-06-18，`Improve country flag error handling`）、`82047118ea`（2026-06-21，客户端/服务器信息校验国家码）、`a825602d43`（2026-06-25，玩家设置国家筛选改进） | MERGE-BOTH | 本地已有 `Validate*` 校验，但上游对 `countryflags` 加载 API 与玩家页筛选交互的改动值得逐条吸收。 |
| 17 | Credits / 贡献者展示 | `menus_settings.cpp:5652`（语言页 credits 弹窗）；`components/qmclient/menus_qmclient.cpp:1478` `RenderSettingsQmClientContributors` | `bc396d820d`（2026-08-10，credits 改为客户端弹窗 + 独立 `menus_settings_credits.cpp` + 更新贡献者名单）、`0d2d717c82`（2026-08-15，credits 页文字对齐修正） | MERGE-BOTH | 本地已是弹窗形态，但上游维护的贡献者名单与独立页可择优并入自有 Contributors 页。 |
| 18 | CUi 虚拟返回键断言崩溃修复 | `src/game/client/ui.cpp:2514` `CUi::DoBackButton`（仍是 `if(Result && Clicked)` … `else if(Result && Abrupted)`） | `624f58e477`（2026-08-16，改为 `if(Clicked \|\| Abrupted)` 单分支统一复位 `m_BackButtonOp`） | TAKE-UPSTREAM | 本地旧分支写法在右/中键取消时复位不全，会触发 `m_ActiveDraggableButtonLogicButton invalid` 断言。 |
| 19 | CUi 虚拟返回键本体（拖拽/回调/结构） | `src/game/client/ui.h:1088` `DoBackButton`/`RenderBackButton`、`ui.cpp:2514`、`EBackButtonOp` | `775365126d`（2026-04-18，`Add back button`，改 `ui.{h,cpp}`+`key_binder`+`editor`） | KEEP-FORK（已同步） | 本地已完整具备该能力（拖拽偏移、初始鼠标、按下回调），无需再取。 |
| 20 | 控制设置绑定排序（无修饰键绑定优先） | `components/menus_settings_controls.cpp:61` `CBindSlotUiElement::operator<` | `04dc39acb8`（2026-09-11，`List unmodified binds before binds with modifiers`） | TAKE-UPSTREAM | 本地仍是 `m_ModifierMask < Other... \|\| m_Key < Other.m_Key` 旧比较器，排序与上游不一致且不满足严格弱序。 |
| 21 | 控制设置分块渲染 / 只读绑定视图 | `components/menus_settings_controls.cpp:189` `Render`、`:587` `RenderSettingsBindCard`、`:606` `RenderSettingsBinds(..., bool ReadOnly)` | 上游同文件 `RenderSettingsBlock` / `RenderSettingsBindsBlock` 重构（同窗口内） | MERGE-BOTH | 本地卡片化与 `ReadOnly`（绑定只读态）是自有需求，上游分块切分可读性更好，按页合并即可。 |
| 22 | 摇杆 aim-relative 与相对直触输入 | `components/touch_controls.cpp:36`（本地仍留有 `TODO: Add "joystick-aim-relative" ... "aim-relative" ingame direct touch input`） | `d8a2bd9dbd`（2026-07-23，`Add Aim-relative Joystick and relative direct input`） | TAKE-UPSTREAM | 本地源码注释里就是待办项，上游已实现 `CJoystickAimRelativeTouchButtonBehavior` + `IsRelative()` 与编辑器选项。 |
| 23 | keynames 从 Input 内核拆出 | `src/engine/client/keynames.{h,cpp}`（已存在） | `9c5c6f6c20`（2026-09-09，`refactor keynames out of the Input kernel`） | KEEP-FORK（已同步） | 本地与上游文件位置一致且差异仅 2 行，等价实现已具备。 |
| 24 | HSLA 滚动条显示真实 HSL 数值 | `components/menus_settings.cpp:6255` `RenderHslaScrollbars`（Hue 度数、Lht/Sat/Alpha 百分比 + 原 0-255 值） | `2eab41a6f0`（2026-01-22，`add real HSL values to the Hsla scrollbars`） | KEEP-FORK（已同步） | 本地已包含同款格式化（`%.1f°` / `%.1f%%`）并额外接入 `SSettingsContentMetrics`。 |
| 25 | `RenderSettingsCustom` → `RenderSettingsAssets` 重命名 | `components/menus_settings_assets.cpp:4157` `CMenus::RenderSettingsCustom`、`menus.h:2080` | `cfb9c8226a`（2026-07-05，`Rename CMenus::RenderSettingsCustom to RenderSettingsAssets`） | TAKE-UPSTREAM | 纯重命名（术语与 `SETTINGS_ASSETS` 页签一致），成本极低且能减少后续与上游的 diff 噪声；资产页内容仍 KEEP-FORK。 |

### 补充核对（不计入上表，结论已明确、无后续动作）

- **声音设置「需重启」提示**：官方 `2b51f7bfbe`（2026-03-31）改用 `Sound()->IsSoundEnabled()`；本地 `menus_settings.cpp:5349` 已是同款写法 → 已同步。
- **Predict events 默认关闭 + 设置项**：官方 `7de1b10535`；本地 `config_variables.h:17` 默认值已为 `0`，且 `menus_settings.cpp:8522` 有开关 → 已同步。
- **easter mapres 资源**：官方 `a9d856f5d0`；本地 `data/mapres/easter_0.7.png` 已存在且 `mapimages.cpp:154` 已处理 `easter` → 已同步。
- **绑定 `escape` 隐藏与补全、`ctrl++` 崩溃**：官方 `0175013159` / `389ab1c5d6` 涉及控制台与 key_binder，本地 `key_binder.cpp`/`console` 已 fork，未逐条比对（见不确定清单）。

## 二、官方设置菜单拆分文件清单（上游新增 / 删除文件）与本地对应文件

### 2.1 上游在同步区间内对 `src/game/client/components/` 的新增文件（`git log --diff-filter=A`）

| 上游新增文件 | 引入提交 | 日期 | 拆分来源 | 行数（master） |
|---|---|---|---|---|
| `components/menus_settings_general.cpp` | `e30d6be289` | 2026-07-05 | `menus_settings.cpp` + `menus.cpp`（`RenderThemeSelection`） | 236 |
| `components/menus_settings_graphics.cpp` | `c000373eb2` | 2026-07-05 | `menus_settings.cpp` | 360 |
| `components/menus_settings_sound.cpp` | `c3d1dddb10` | 2026-07-05 | `menus_settings.cpp` | 79 |
| `components/menus_settings_tee.cpp` | `b824eccf95` | 2026-07-05 | `menus_settings.cpp` | 450 |
| `components/menus_settings_player.cpp` | `8c4acbe637` | 2026-07-05 | `menus_settings.cpp` | 161 |
| `components/menus_settings_language.cpp` | `8b7b8f4b9a` | 2026-07-05 | `menus_settings.cpp` | 84 |
| `components/menus_settings_appearance.cpp` | `2634a72933` | 2026-07-05 | `menus_settings.cpp` | 830 |
| `components/menus_settings_ddnet.cpp` | `f8a83dba1d` | 2026-07-05 | `menus_settings.cpp`（含 `PopupMapPicker` + `CPopupMapPickerContext`） | 375 |
| `components/menus_settings_credits.cpp` | `bc396d820d` | 2026-08-10 | 新页（credits 从弹窗/其他页独立，服务器侧 credits 同时从 `ddracechat.cpp`/`gamecontext.cpp` 摘除） | 92 |
| `src/game/client/ui_popups.cpp`（非 components，同级目录） | `f1dfd5187b` | 2026-07-11 | `src/game/client/ui.cpp` 迁出 634 行 CUi 弹窗实现 | 554 |

其他相关变更（非新增文件）：

- `144603b1f4`（2026-07-05）**重命名** `components/menus_settings7.cpp` → `components/menus_settings_tee7.cpp`（含 `menus.h` 24 行符号改名）。
- `cfb9c8226a`（2026-07-05）`RenderSettingsCustom` → `RenderSettingsAssets`；`5960f70f81` 删除未用成员 `CMenus::m_SettingPlayerPage`；`fc2f88f389` 清理设置菜单 include。
- 同步区间内 `src/game/client/components/` **没有文件被删除**（`--diff-filter=D` 无命中）；上游 master 该目录共 107 个文件。
- 上游拆分后仍保留的旧文件：`menus_settings.cpp`（仅 354 行：`SetNeedSendInfo` / `RenderSettings` / `RenderHslaScrollbars`）、`menus_settings_assets.cpp`（664）、`menus_settings_controls.{cpp,h}`（714 / h）。
- 上游 `CMakeLists.txt` 相应把 `components/menus_settings*.cpp`（12 条）+ `ui_popups.cpp` 登记进 `GAME_CLIENT`。

### 2.2 本地对应文件

| 上游文件 | 本地对应文件 | 本地状态 |
|---|---|---|
| `menus_settings.cpp`（354） | `src/game/client/components/menus_settings.cpp`（8260） | 未拆分，承载 General/Player/Tee/Graphics/Sound/Language/Appearance/DDNet + `PopupSettingsCountrySelection`/`PopupSkinQueuePresetRename`/`PopupMapPicker` + HSLA 滚动条 |
| `menus_settings_general.cpp` | 同上（`RenderSettingsGeneral:753`）；`RenderThemeSelection` 在 `components/menus.cpp:4972` | 未拆分（另有 `theme_scan.h` 自有主题扫描） |
| `menus_settings_graphics.cpp` | 同上（`RenderSettingsGraphics:3622`） | 未拆分 |
| `menus_settings_sound.cpp` | 同上（`RenderSettingsSound:5253`，含自有 `AudioPackEditor*`） | 未拆分且已扩展 |
| `menus_settings_tee.cpp` | 同上（`RenderSettingsTee:1356`、`RenderSettingsTeeIdentity:1082`） | 未拆分且已扩展 |
| `menus_settings_tee7.cpp` | `src/game/client/components/menus_settings7.cpp`（642） | 文件名为上游旧名；`PopupConfirmDeleteSkin7:404` 等已本地化改写 |
| `menus_settings_player.cpp` | `menus_settings.cpp:1152` `RenderSettingsPlayer` | 未拆分 |
| `menus_settings_language.cpp` | `menus_settings.cpp:5649` `RenderLanguageSettings`、`:5681` `RenderLanguageSelection` | 未拆分 |
| `menus_settings_appearance.cpp` | `menus_settings.cpp:6514` `RenderSettingsAppearance` | 未拆分 |
| `menus_settings_ddnet.cpp` | `menus_settings.cpp:8260` `RenderSettingsDDNet`、`:8764` `PopupMapPicker` | 未拆分 |
| `menus_settings_credits.cpp` | 无独立页：`menus_settings.cpp:5652` credits 弹窗 + `components/qmclient/menus_qmclient.cpp:1478` Contributors 页 | 形态不同 |
| `menus_settings_assets.cpp` | `src/game/client/components/menus_settings_assets.cpp`（7192，`RenderSettingsCustom:4157`） | 同文件、已重度扩展（工作坊/资产预览） |
| `menus_settings_controls.{cpp,h}` | `src/game/client/components/menus_settings_controls.{cpp,h}`（852 / h） | 同文件、已卡片化 |
| `ui_popups.cpp` | 无独立文件；`src/game/client/ui.cpp`（`DoPopupMenu:2611`、`RenderPopupMenus:2660`、`PopupColorPicker:3242`） | 未拆分 |
| （上游无） | `src/game/client/components/qmclient/menus_qmclient.cpp`（5749）、`QmUi/**`、`QmUi/cards/**`、`tclient/menus_tclient.cpp`（6981）、`settings_resource_preview.*` 等 | QmClient 独有 |

本地 `CMakeLists.txt` 中 `GAME_CLIENT` 的对应登记（供后续拆分时对照）：`components/menus_settings.cpp`、`menus_settings7.cpp`、`menus_settings_assets.cpp`、`menus_settings_controls.{cpp,h}`，以及 `QmUi/**`、`QmUi/cards/**`、`components/qmclient/menus_qmclient.cpp`、`components/settings_{runtime_cache,resource_jobs,warmup}.*`。

## 三、不确定清单（无法仅凭静态阅读定论）

1. **不可见（全透明）sprite 资源回退**：官方 `756dcc04f9`（2026-04-01）给 `Graphics()->LoadSpriteTexture` 增加 `std::nullopt` 分支以检测不可见贴图并回退默认 sprite；本地 `skins.cpp` 已不再直接调用 `LoadSpriteTexture`，改走 `QmPrepareSkinTextures` / `ExtractSpriteImage`（`QmUi/../qmclient/skin_prepared_textures.h`）。需进一步确认 `ExtractSpriteImage` 是否同样识别全透明贴图，才能判定「已覆盖 / 需补」。
2. **触控编辑器帮助说明**：官方 `9c793e8d3f`（2026-01-01）为行为/预定义类型加简短说明；本地 `menus_ingame_touch_controls.cpp:376/452` 已有 `HelpMessageForBehaviorType` / `HelpMessageForPredefinedBehaviorType` 帮助按钮，但文案覆盖度是否与上游等价未逐条比对。
3. **触控横向滚动**：官方 `fdba82286c`（2026-05-22）为触控增加横向滚动；本地 `ui_scrollregion` 已有自有的 `EQmScrollAxis::HORIZONTAL` 抽象，但触控输入路径是否等价未验证。
4. **`CScrollRegion` 系列 API 演进**：官方 `cf5cb30606`（`Begin` 去掉 `pOutOffset`）、`d483b5b27e`（`m_Flags` → 布尔）、`007bcb92c5`（方向无关变量改名）、`aafa2fda41`（移除 `MAGIC_HEIGHT_FIX`）、`2b45da78a8`/`569edee60b`（裁剪一致性 / `CScreenRect`）。本地 `ui_scrollregion.h:201` 仍是 `Begin(pClipRect, pOutOffset, params)` 且已有自有轴抽象，改造会牵动大量设置页调用点，合并顺序与范围未定。
5. **上游设置页在拆分同时新增/调整的具体设置项**（如 `2b51f7bfbe` 声音提示、`a3bf1e5657` 移除 `gfx_color_depth`、`b3bc9f18c8` 图形颜色 setter 重构、`c50b26448c`/`7ec722151b` hook line tip 自定义颜色与 alpha）在本地渲染路径（卡片体系）下的落点未逐项核；其中 hook line tip 颜色项本地 `config_variables.h` 未找到对应配置。
6. **计分板相关 UI 是否属本簇**：官方 `b4f6206818`（计分板地图信息弹窗）、`ef974573d3`（计分板显示皮肤名 tooltip，本地 `scoreboard.cpp` 已 fork 且渲染皮肤格，6 行小改可并入）、`dbc77978c6`/`f15d3b1f94`（计分板 cursor hint）语义归计分板簇，是否在本簇一并处理待定。
7. **控制台 / key_binder 侧小修复**：官方 `389ab1c5d6`（`bind ctrl++ say hi` 崩溃）、`0175013159`（禁止绑定 `escape` 并在补全中隐藏）、`dd56d7aa26`/`775365126d`（控制台/虚拟返回键路径）与本地 fork 的交集未逐条比对。
