# UI 全局术语与组件规范

状态：现行规格（2026-11-14 建立）。权威范围：`src/game/client/QmUi/`（291 个 .h/.cpp/.inc）、`src/game/client/components/qmclient/` 的 Qm 界面、`ui_token::`（`src/game/client/QmUi/UiTokens.h`）。

本文件是 UI 改动的唯一入口。出现下表任何中文规范名时，先查本文件；代码只允许走「唯一实现」一列，禁止走「禁止的反面写法」一列。本文件不推翻既有定稿：§9 的行级七词原样继承自 `docs/规格/2026-11-11-设置页Token与一致性改动报告.md` 第 9 节，口径一致。

命名约定：术语标题为「中文规范名（English name）」。定义句只写核心交互/布局语义，不写实现细节；「存在」与「变灰」是两类语义，不得混用。

DDNet 原生 UI（`src/game/client/ui.cpp`、`ui_listbox.cpp`、`menus*.cpp` 的上游部分）不在本文件改造范围。本文件只约束 Qm 自有 UI；`CListBox` 属共享组件，改动需同时遵守其既有交互契约。

## 零、分段索引

1. 全局边界隔离（§1）：哪些东西不允许越过 UI 层边界。
2. 底层基建（§2）：Token、布局、动画、浮层、滚动、卡片骨架。
3. 通用组件与行级模式（§3）：页签、选择、输入、列表、提示、行级七词与浮层。
4. 横向分工对比表随各分组末尾给出，涉及「职责相近但语义不同」的组件。
5. 维护协议（§4）。

## 一、全局边界隔离

### 1.1 UI 生命周期守卫（UI lifetime guard）

- 定义：一帧之内 UI 的初始化、绘制、销毁有明确先后；任何跨帧持有 UI 对象的行为必须挂守卫，避免销毁后仍被引用。
- 唯一实现：`CQmUiLifetime`（`src/game/client/QmUi/QmUiLifetime.h`），入口在 `src/game/client/ui.h`。
- 禁止的反面写法：静态缓存裸指针持有 `CUi` / `CTooltips` / 渲染后端；用 `static CListBox` 之外再自建全局实例；在 `~CUi` 之后清理资源。

### 1.2 UI 上下文契约（UI context contract）

- 定义：组件只通过 `IUiContext` 拿 UI、输入、客户端与存储，不直接摸全局单例；不同页面注入不同上下文。
- 唯一实现：`IUiContext`（`src/game/client/QmUi/UiContext.h`）；设置页用 `SettingsUiContext(MakeUiScopeHash("…"), UiScale)`，页签条用 `TabBarUiContext()`（`src/game/client/components/menus.h`），卡片内小组件用 `ui_widget::ControlContext(Ui())`。
- 禁止的反面写法：在组件里直接调 `GameClient()` 取 UI 或输入；同一个卡片里两种上下文混用；作用域哈希写成随机字符串。

### 1.3 UI 作用域哈希（UI scope hash）

- 定义：动画节点、缓存键按「页面 + 组件 + 实例」三段唯一化，同一组件出现两次不共享动画。
- 唯一实现：`MakeUiScopeHash("…")`（`UiContext.h`）+ `BuildUiAnimNodeKey / UiAnimNodeKey`（`src/game/client/QmUi/QmMotion.h`、`QmAnimResolve.h`）。
- 禁止的反面写法：用 `reinterpret_cast<uint64_t>(pThis)` 之外再混入随机数；两处组件复用同一个哈希；哈希里塞可变配置值（改配置就重置动画）。

### 1.4 弹层来源与所有权（popup source and ownership）

- 定义：弹层的打开者、持有者与关闭条件必须有唯一来源；外部点击、Esc、父容器销毁都走同一条收尾路径。
- 唯一实现：`CUi::DoPopupMenu` + `SPopupMenuProperties` + `SPopupMenuId`（`src/game/client/ui.h`、`src/game/client/ui_popups.cpp`）；来源刷新用 `QmRefreshPopupSource` + `CQmUiInteractionSource`（`src/game/client/QmUi/QmPopupSource.h`）；指针遮挡用 `src/game/client/QmUi/QmPopupPointer.h`。
- 禁止的反面写法：自己用 `if(Hovered && Clicked)` 实现「点外面关闭」；一个弹层两个来源；关闭时不复位 `SPopupMenuId`，导致下次打开复用旧状态。

### 1.5 模块渲染边界（module render boundary）

- 定义：设置页的每个功能模块由「卡片定义 + 内容渲染钩子」组成，模块只在自己那段内容里排版，不直接碰页面壳层。
- 唯一实现：`EQmModuleId`（`src/game/client/QmUi/QmModuleTypes.h`）、`SSettingsCardDefinition` + `CSettingsCardDeck`（`src/game/client/QmUi/SettingsCardDeck.h`）、`qm_card_catalog::QmCardRenderHook`（`src/game/client/QmUi/cards/QmCardRenderBridge.cpp`）。
- 禁止的反面写法：卡片里去改页面外边距或卡片宽度；在模块内容里重新算 `SSettingsContentMetrics`；绕过 deck 直接画卡片外壳。

### 1.6 配置项与文案边界（config and copy boundary）

- 定义：玩家可调的视觉量必须是 `qm_ui_*` 配置；其余一律是 `ui_token::`；界面文案必须走 `Localize` / `Localizable`。
- 唯一实现：`src/engine/shared/config_variables_qmclient.h`（`qm_ui_color`、`qm_ui_control_color`、`qm_ui_layer_color`、`qm_ui_card_color`、`qm_ui_accent_color`、`qm_ui_text_color_mode`、`qm_ui_motion_level`）+ `ResolveConfigured*` 系列（`src/game/client/QmUi/UiTheme.h`）+ `Localizable` 标记。
- 禁止的反面写法：给单个控件加一条颜色配置；在 `.cpp` 里写裸 `ColorRGBA`；拼接中英混排字符串绕过 `Localize`。

## 二、底层基建

### 2.1 设计 Token（design token）

- 定义：所有尺寸、颜色、圆角、字号、动效参数只有一个来源；调用点选语义，不写数字。
- 唯一实现：`src/game/client/QmUi/UiTokens.h`，命名空间共 25 个：`ui_token::color`（58 项）、`ui_token::spacing`、`ui_token::radius`（`NONE/TIGHT/BASE/SMALL_MENU/MENU/CARD/PILL`）、`ui_token::font`（`HEADLINE_LG/HEADLINE/TITLE/BODY/SMALL/CAPTION/TIP`）、`ui_token::motion`（36 项）、`ui_token::elevation`、`ui_token::control`（`PadX/PadY/IconBox/IconTextGap/SUFFIX_RATIO/SwitchMinWidth/SwitchTrackAspect`）、`ui_token::list`、`ui_token::settings` + `ui_token::settings::row`、`ui_token::feedback`、`ui_token::glow`、`ui_token::tooltip`、`ui_token::secondary_panel`、`ui_token::server_filter`、`ui_token::status_bar`、`ui_token::server_info`（`TITLE_SIZE/BODY_SIZE/TITLE_GAP/ROW_HEIGHT_RATIO/LABEL_WIDTH/NOTE_*/CARD_RADIUS`）、`ui_token::motd`（`FONT_SIZE/BOX_CELL_MIN_RATIO`）、`ui_token::motd`（`DESIGN_WIDTH/DESIGN_HEIGHT/TITLE_HEIGHT_RATIO/TITLE_GAP/BOX_GLYPHS_PER_STEP/FONT_SIZE`）、`ui_token::page_title`（`FONT_SIZE/HEIGHT/ICON_SIZE/ICON_GAP/ICON_Y_OFFSET`）、`ui_token::panel`（`GAP/PADDING/MARGIN`）、`ui_token::chat`（滚动条 + 气泡圆角派生 + `input::RowHeight/ButtonBorderWidth` 聊天输入行行高与翻译按钮描边）、`ui_token::player_card`（玩家信息卡分区度量）、`ui_token::text_caret`（`THICKNESS_PX/RADIUS_PX/BLOCK_RADIUS_PX/WIDTH_RATIO/OUTLINE_PX`）、`ui_token::confirm_dialog`、`ui_token::dropdown`、`ui_token::discrete_slider`、`ui_token::ime`、`ui_token::hud::font`、`ui_token::page_title`（`FONT_SIZE/HEIGHT/ICON_SIZE/ICON_GAP`）、`ui_token::panel`（`GAP/PADDING/MARGIN`）、`ui_token::chat`（`SCROLLBAR_*/BUBBLE_*`、`BubbleRadius`）、`ui_token::player_card`（`SIDE_BY_SIDE_MIN_WIDTH/CARD_MAX_WIDTH/CARD_WIDTH_RATIO/STACK_CARD_HEIGHT_RATIO/LIST_CARD_GAP/PADDING/RADIUS/SECTION_GAP/HEADER_*/ACTION_*/ICON_*/STATUS_*/DETAIL_*/LIST_*/SEARCH_HEIGHT/COPY_TOAST_SECONDS`）。
- 禁止的反面写法：`Row.h - 2.0f` 这类就地魔数；为单个组件新开命名空间放通用色；用近似值复制已有 token（`PREVIEW_BACKDROP` 与 `SURFACE_OVERLAY` 二选一时必须复用）。

### 2.2 设置内容布局流（settings content flow）

- 定义：卡片内容按「行」声明：高度、行前间距、ID；同一段代码在声明趟、测量趟、输入趟、绘制趟各跑一次，输出必须一致。
- 唯一实现：`CSettingsContentLayoutFlow`（`src/game/client/QmUi/SettingsContentLayout.h`）：`Control(id, 高度, Rect, 行前间距)`、`Text(id, 文本, 缩进, FontSize, 行后间距, 测量, 绘制)`、`Space(间距)`、`AdvancedHeight()`、`PublishedText()`；趟次由 `ESettingsContentPass`（`SettingsCardContentBridge.h`）区分；`CSettingsContentLayoutScope` 提供绘制/测量上下文。
- 禁止的反面写法：行后再补 `Flow.Space`（双计行距）；在 `if` 里跳过声明（高度瞬跳）；声明趟与绘制趟用不同高度；`Control` 的 ID 重复。

### 2.3 内容度量（content metrics）

- 定义：字号、控件高、标签列宽由视口缩放推导，不由各卡片自算。
- 唯一实现：`SSettingsContentMetrics` + `CurrentSettingsContentMetrics()`（`src/game/client/QmUi/SettingsPageLayout.h`），派生函数 `ResolveSettingsControlHeight`、`ResolveSettingsControlFontSize`、`ResolveSettingsSmallFontSize`、`ResolveSettingsTextScale`、`ResolveSettingsCardLabelWidth`、`ResolveSettingsControlWidth`、`ResolveSettingsContentMetrics`；行切分用 `SplitSettingsControlRow`。
- 禁止的反面写法：`TextRender()->TextWidth(12.0f, …)` 里写死字号；用 `Row.h * ms_FontmodHeight` 反推控件字号；同一卡片里混用两套 metrics。

### 2.4 行距档位（row gap tiers）

- 定义：行距只有四档，由行自己声明：子行 0.5×、行间 1×、分区间 2×、卡尾 4×。
- 唯一实现：`ui_token::settings::RowGap(行距)` / `SubRowGap` / `SectionGap` / `ActionGap`（`UiTokens.h`，`ui_token::settings`）；行类型旁路 `ui_token::settings::row::Of(EType)`（`PRIMARY/GROUP_HEADER/SUB/DIVIDER/FOLD/NOTE`，未缩放，仅已迁移调用点使用）。
- 禁止的反面写法：上一行尾部补间距；用 `Space(2.0f)` 代替档位；用 `settings::row` 的数值冒充派生链档位。

### 2.5 圆角与表面（rounded surface）

- 定义：圆角按尺寸与曲线档推导，表面统一走同一个绘制入口，描边与光晕只是参数。
- 唯一实现：`DrawRoundedSurface(...)`、`DrawRoundedSurfaceGlow(...)`、`ResolveRoundedSurfacePlan(...)`、`ResolveRoundedSurfaceCornerRadii(...)`、`QmCornerSmoothingForCurve` / `QmEffectiveCornerSmoothing`（`src/game/client/QmUi/UiSurface.h`、`UiSurface.cpp`）；圆角档 `ui_token::radius::*`、列表档 `ui_token::list::RowRadius(H)` 与 `ContainerRadius(W,H)`。
- 禁止的反面写法：用 `Row.Draw(Color, CORNER_ALL, 5.0f)` 写死圆角；自己按 `min(w,h)/2` 算胶囊；两套描边宽度并存；用「矩形放大 + 半径加宽」手拼光晕。

### 2.6 动效运行时（animation runtime）

- 定义：所有过渡走同一套轨道解析器，按属性（值/颜色/透明度）补齐；本地时钟动画只在交互趟推进。
- 唯一实现：`CUiV2AnimationRuntime`（`src/game/client/QmUi/QmAnimationBackend.h`，`GameClient()->UiRuntimeV2()->AnimRuntime()` + `FrameDt()`）、`SUiSpringConfig` / `SUiBezier` / `SUiAnimRequest`；曲线 `src/game/client/QmUi/QmAnimCurves.h`；解析 `QmAnimResolve.h`；时长与弹簧档 `ui_token::motion::*`（`HOVER_FADE/PRESS_SCALE/PAGE_SLIDE/TAB_SWITCH/POPUP_OPEN_SEC/POPUP_CLOSE_SEC/INTERACTIVE_REVEAL_SEC/TOGGLE_SPRING/SLIDER_SPRING` 等）。
- 禁止的反面写法：手写 `Lerp(Current, Target, 0.2f)`；写死 `0.15f` 时长；在绘制趟推进本地时钟（出图趟跑完动画，交互趟瞬变）；用 `g_Config.m_QmUiMotionLevel` 之外的第二套动效开关。

### 2.7 交互趟推进守卫（interactive-pass advance）

- 定义：本地时钟动画只在「输入趟」推进，测量趟与只读趟只读，保证过渡不被后台绘制吃掉。
- 唯一实现：`QmSettingsRevealShouldAdvance(...)`、`QmInteractiveRevealSeconds(...)`、`QmInteractiveRevealProgress(...)`、`QmSettingsRevealCanMeasureContent(...)`（`src/game/client/QmUi/SettingsRowReveal.h`）。
- 禁止的反面写法：不分趟次直接 `Progress += Dt`；在 `MeasurementOnly` 趟写进度；用进度反推显隐判定。

### 2.8 滚动与让位（scroll and reserve）

- 定义：滚动容器与列表共用同一套滚动物理；覆盖式滚动条不占内容宽度，需要时按过渡让位。
- 唯一实现：`CQmScrollController` + `SQmScrollMetrics` / `SQmScrollConfig` / `SQmResolvedScrollPolicy`（`src/game/client/QmUi/QmScroll.h`）；列表侧 `CListBox`（`src/game/client/ui_listbox.h`）：`SetCapsuleRows`、`SetContainerPadding`、`SetVisibleRowRange`、`SetScrollbarOverlay`、`RowReserveWidth()`、`SetReserveRightWhenOverflowing(bool)`；让位宽 `ui_token::list::OverlayRailReserve()` = `SCROLLBAR_WIDTH + SCROLLBAR_MARGIN + SCROLLBAR_ROW_GAP`。
- 禁止的反面写法：调用点写 `View.w -= 12`；自己实心滚动条；在「内容刚好装满」时也恒定让位（能用真实内容高度时必须按溢出判断）。

### 2.9 卡片骨架与测量（card skeleton and measure）

- 定义：卡片外壳、标题、描边、展开高度由 deck 统一测量与绘制，卡片内容只填内容区。
- 唯一实现：`CSettingsCardDeck`（`SettingsCardDeck.h`）、`SSettingsCardDefinition`、`ResolveSettingsCardDeckPointerInput`、`CSettingsCardDeckFrameRuntime`（`SettingsCardDeckLogic.h`）；外壳绘制 `ExecuteSettingsCardChromeDraw`、边框 `SettingsCardInteractionBorderVisible`、内容裁剪 `ResolveSettingsCardContentClipRect`（`SettingsCard.h`）；测量 `SettingsCardMeasure.h`；宽度 `SettingsCardWidth.h`；动效 `SettingsCardMotion.h`。
- 禁止的反面写法：卡片里自己画标题栏或描边；卡片内容高度与测量函数各写一份；卡片折叠状态不进 `SettingsCardCollapseState`。

### 2.10 标签与本地化缓存（label and localized cache）

- 定义：设置行标签只走统一入口，本地化后的文本与宽度进缓存，避免每帧重排。
- 唯一实现：`DoSettingsMenuLabel(...)`（`src/game/client/components/menus.cpp`）、`CQmLocalizedLabelCache`（`src/game/client/QmUi/QmLocalizedLabels.h`）、`SLabelProperties`（`m_MaxWidth`、`m_EllipsisAtEnd`、`m_vColorSplits`）、字体档 `ui_token::font::*`。
- 禁止的反面写法：`Ui()->DoLabel` 里写死字号与颜色；标签宽度自己量；同一行两种截断策略。

### 2.11 页面主标题（page title）

- 定义：页面级标题（设置页顶部「设置」、收藏地图工作区标题）共用一个字号与行高档，图标与文字作为一个整体对齐；标题不再借用正文标尺。
- 唯一实现：`ui_token::page_title::{FONT_SIZE, HEIGHT, ICON_SIZE, ICON_GAP}`（`UiTokens.h`）+ `ui_widget::PageTitle(...)`（`src/game/client/QmUi/UiOverlays.h`，内部 `DoLabel_QmIcon` + `DoLabel`）；设置页标题样式键 `CMenus::BuildSettingsShellTitleTextStyle`（`menus.cpp`）读同一档字号。
- 禁止的反面写法：标题字号写死 16 或 `font::HEADLINE_LG`（曾把「设置」压成正文观感）；标题前面自己 `DoLabel` 画图标导致换语言后错位；工作区标题用 `h * 0.72f` 按行高反推字号。

### 2.12 页面页签字号（page tab font）

- 定义：页签条文字只有一档字号，设置页页签与工作区页签并排看起来一样大。
- 唯一实现：`ui_token::settings::TAB_FONT_SIZE`（`UiTokens.h`）+ `ui_token::settings::TAB_HEIGHT`；`DoButton_MenuTab` / `DoMapWorkspaceTabs` 显式传该档值。
- 禁止的反面写法：`DoButton_MenuTab(..., -1.0f, ...)` 让行高反推字号（26 行高推到 15.4，与 13 档并排就露馅）；两个页面各写一个字号。

### 2.13 面板间距（panel spacing）

- 定义：页面级面板的留白只有三档——面板之间的缝、内容到面板边缘的内边距、面板外缘到窗口边缘的外距。服务器列表是基准，回放/截图浏览器与设置页壳层跟它对齐。
- 唯一实现：`ui_token::panel::{GAP, PADDING, MARGIN}`（`UiTokens.h`）；消费点 `QmBrowserPanelLayout`（`browser_status_layout.h`）、`ResolveSettingsShellLayout` 的 `SETTINGS_SHELL_GAP` / `SETTINGS_PANEL_MARGIN`（`SettingsPageLayout.h`）、`CMenus::RenderDemoBrowser`（`menus_demo.cpp`，底部操作条到窗口底取 `MARGIN`）。
- 禁止的反面写法：调用点写 `HSplitBottom(10.0f, ...)` / `Margin(12.0f, ...)`；三个页面各用一套 8/10/12；面板间隙、内边距与外距互相顶替（底边留白拿 `GAP` 当外距就是这一条）。

### 2.9b 列表卡片（list card）

- 定义：网格或列表里的一张卡——外壳（矩形、圆角、内边距）+ 头部（标题 / 副标题 / 尾部徽标·按钮）+ 内容区 + 可选底栏，悬浮时勾一圈统一白边。
- 唯一实现：`qm_list_card::ResolveLayout` / `SMetrics` / `SOptions` / `SLayout` / `DrawShell` / `DrawStaticShell`（`src/game/client/QmUi/QmListCard.h`）；圆角 `ui_token::radius::CARD`（资源卡片这一档传 `MENU`）、内边距 `ui_token::settings::CARD_PADDING`、描边 `ui_token::feedback::HOVER_BORDER`。接入点：`CMenus::RenderSettingsBindWorkshop`（`bind_workshop_ui.cpp`）、`CMenus::RenderSettingsCustom` 的 `RenderAssetsCardShell`（`menus_settings_assets.cpp`）。
- 禁止的反面写法：卡片里自己写内边距与圆角再调 `DrawRoundedSurface`；在命中已由 `CListBox` 登记时再调 `DrawShell`（会抢命中，静态壳用 `DrawStaticShell`）；把列表卡片当设置卡片用（折叠、卡间阴影、deck 测量不属于它）。

### 2.14 对比表：三类动效机制

| 机制 | 何时用 | 布局占位 | 唯一实现 | 禁止混用 |
| --- | --- | --- | --- | --- |
| 卡片入场/高度过渡 | 卡片出现、内容高变化 | 由 deck 测量，过渡期高度连续 | `SettingsCardMotion.h` + `CUiV2AnimationRuntime` | 用它做单行显隐 |
| 从属行展开 | 单行随父项出现/消失 | 关闭时不占高、不进命中 | `CSettingsConditionalRows` + `CSettingsContentRevealScope` | 用固定高度留白冒充 |
| 互斥分支切换 | 同一槽位二选一/三选一 | 槽高恒定 | `CSettingsExclusiveReveal` + `CSettingsExclusiveBranchScope` + `SlotPadding()` | 用它写从属行（白占一档槽高） |

### 2.8b 控制台日志滚动（console log scroll）

- 定义：控制台日志的滚轮、翻页、滑块与触控拖动共用全站滚动物理；偏移以像素推进、读取时折回「整行 + 亚行余量」，贴底时新日志自动跟随。
- 唯一实现：`CQmConsoleScrollRuntime`（`src/game/client/QmUi/QmConsoleScroll.h`）+ `CQmScrollState` / `QmNativeWheelScrollConfig`；调用点 `CGameConsole::CInstance::OnInput`、`CGameConsole::OnRender`（`src/game/client/components/console.cpp`）。
- 禁止的反面写法：在控制台里自己写 `m_BacklogCurLine += 行数` 直跳滚轮；拖动滑块走弹簧（应跟手）；新日志到达时搬整型行锚而不是按内容增长量抬高偏移；另写一套平滑时长或阻尼。

### 2.8c 覆盖式滚动条（overlay scrollbar）

- 定义：铺在内容上层、不占内容宽度的滚动条。轨道长度等于宿主视口，两端各内缩一档；滑块长度按「视口高 / 内容总高 × 轨道」求，夹在最小滑块与轨道之间；位置由进度决定（0 = 顶部/最旧）。内容装得下时不出现。
- 唯一实现：`ui_widget::ResolveOverlayScrollbarFrame`（`src/game/client/QmUi/UiForms.h`，几何本体委托 `CQmScrollController::PreviewFrame`），绘制走 `ui_widget::DrawScrollbarHandle`；滑块悬浮/按下放大的热项必须由 `CUi::DoButtonLogic` 登记，控件才会画大。
- 禁止的反面写法：各页面自己算轨道与滑块；用固定像素上限截断滑块长度；只画滑块、不登记热项导致悬浮不变大；另写一套放大像素。

### 2.8d 控制台顶栏按钮（console toolbar button）

- 定义：控制台筛选与动作按钮的出现、消失都走同一条存在度；同一按钮换文案时复用槽位并平滑过渡宽度，按钮宽度、透明度和命中一起收放，任意两颗实际可见的按钮之间保留同一档固定间距，目标集合决定是否折成两行，选中底色取全局强调色的派生档。
- 唯一实现：`QmConsoleUi::CToolbarPresenceMotion`、`QmConsoleUi::LayoutToolbarAnimated`、`QmConsoleUi::ToolbarFlowGap`、`QmConsoleUi::ToolbarFlowWidth`、`QmConsoleUi::ResolveToolbarWidth`、`QmConsoleUi::ResolveConsoleButtonFill`、`QmConsoleUi::Button`（`src/game/client/QmUi/QmConsoleUi.h`）；显隐动效取 `ui_token::motion::NAVIGATION_APPEAR`、文案宽度取 `ui_token::motion::TAB_SWITCH`，调用点 `CGameConsole::OnRender`（`src/game/client/components/console.cpp`）。
- 禁止的反面写法：动作集合变化时瞬切按钮或只淡文字不缩宽度；把搜索/取消、展开/收起拆成两颗按钮同时一缩一长；用当前动画宽度决定折行；隐藏槽位吞掉左右按钮之间的间距或让间距随存在度缩到 0；选中底色取控制台命令语法色或在调用点写裸 `ColorRGBA`；显隐和选中共用同一条 `ALPHA` 轨道。

### 2.8e 控制台类型切换与远程头部（console type switch）

- 定义：本地/远程控制台是同一面板的两种内容；切换时背景、边框、阴影和顶部栏按同一条 0→1 混合轨道过渡，远程头部文本单独走存在度入场/出场；类型混合轨道从当前值续接，F1/F2 切换时头部重新入场。
- 唯一实现：`QmConsoleUi::ResolveConsoleTypeBlend`、`QmConsoleUi::MixColor`（`src/game/client/QmUi/QmConsoleUi.h`）；远程头部存在度 `QmConsoleUi::CToolbarPresenceMotion<1>`（`CGameConsole::m_HeaderPresenceMotion`）；调用点 `CGameConsole::OnRender`、`CGameConsole::Toggle`（`src/game/client/components/console.cpp`）。类型混合取 `ui_token::motion::TAB_SWITCH`，头部取 `ui_token::motion::NAVIGATION_APPEAR`。
- 禁止的反面写法：本地/远程整块硬切；一个 `ALPHA` 轨道同时承担类型混合和头部显隐；远程顶部用裸 `TextRender()->Text` 直接瞬移；切换时把两份日志同时画出做交叉淡化；远程与本地各写一套顶栏行高或文字基线；顶栏/页脚固定高度不跟 `ConsoleHeight` 收缩（主体已收起，横条还留在屏幕上）。

### 2.19 圆角与胶囊（radius / capsule）

- 定义：表面形状只由「半径」决定。半径取控件短边的一半即胶囊端头，取短边一半且宽高相等即正圆；宽度明显大于高度（≥1.8 倍）时按半高取端头。
- 唯一实现：`ui_widget::DrawRoundedSurface`（`src/game/client/QmUi/UiSurface.h`）+ `ResolveRoundedSurfaceCorners`。给了非零半径又传 `IGraphics::CORNER_NONE` 的调用点一律按四角全圆处理。
- 禁止的反面写法：同时传半径与 `CORNER_NONE` 期望方块（要方块应把半径给 0）；在调用点自行判断走引擎四角接口还是 SDF；把贴图内容裁成胶囊（贴图只做内收缩进圆头）。

### 2.15 玩家信息卡（player card）

- 定义：玩家页一名玩家一行，整行一张卡，与服务器浏览器「好友」页同一种读法：左边角色头像、中间名字与战队两行、右侧一排动作图标（好友 / 静音 / 屏蔽表情 / 跟随 / 复制皮肤）。没有选中态，也没有右侧展示页。
- 唯一实现：`QmPlayersUi::PlayerRowLayout` / `QmPlayersUi::PlayerIconRow`（`src/game/client/QmUi/QmPlayerCard.h`），列表为单列 `CListBox`（`src/game/client/QmUi/QmPlayersMenu.cpp`）。
- 禁止的反面写法：点选后另开右栏展示页；把动作按钮放在展示页里；跟随用眼睛图标（应复用好友列表的播放/停止）；复制动作拆成「复制战队」之类的臆造项。

### 2.16 聊天气泡与聊天滚动条（chat bubble / chat scrollbar）

- 定义：游戏内聊天框是独立的 300 高虚拟屏，尺寸单位是「聊天逻辑单位」（1 单位 ≈ 屏幕高度/300），不是 UI 像素；消息气泡单行画成完整胶囊、多行画成圆角矩形，聊天滚动条与列表滚动条共用同一套滑块绘制入口。
- 唯一实现：`ui_token::chat`（`SCROLLBAR_WIDTH/SCROLLBAR_MARGIN/SCROLLBAR_MIN_THUMB/SCROLLBAR_MAX_THUMB_RATIO/SCROLLBAR_RADIUS/SCROLLBAR_FEEDBACK_INSET_MAX_RATIO/BUBBLE_*`、`BubbleIsSingleLine`、`BubbleRadius`）+ `ui_token::feedback::HANDLE_{HOVER,PRESS}_GROW`（滑块悬浮/按下放大，与 `ui_widget::DrawScrollbarHandle` 同源）+ `src/game/client/components/qmclient/chat_scrollbar.h`（`QmChatHistoryScrollMetrics/ScrollConfig/ScrollLine/Wheel/DragToLine` 为历史滚动推进唯一入口；`QmResolveChatScrollbarFrame/QmChatScrollbarThumbHeight/QmChatScrollbarProgressFromPointer` 为滑块几何，几何本体委托 `CQmScrollController::PreviewFrame`，与列表覆盖式轨道同一条算法）（`QmChatScrollbarRail`、`QmChatScrollbarHandleHeight`、`QmDrawChatScrollbar`）；滑块绘制走 `ui_widget::DrawScrollbarHandle`（与 `CListBox`/`CScrollRegion` 同一入口），气泡圆角走 `DrawRoundedSurface`（`ResolveRoundedSurfacePlan` + `QmEffectiveCornerSmoothing` 曲线链）。
- 禁止的反面写法：在 chat.cpp 里写 `FontSize() * 0.5f` 之类的半径、给气泡另起一套 `CreateRectQuadContainer` 直绘几何（只认圆弧，和气泡自己的背景模糊会错位）、把列表滚动条的 UI 像素值直接当聊天单位用、聊天滚动条自己画强调色与卡片描边。

## 三、通用组件与行级模式

### 2.16b 聊天输入行（chat input row）

- 定义：游戏内聊天输入行最左侧的翻译按钮、桌面「全体:」文字与手机「全体」前缀块共用一条行高；按钮直径、描边粗细、图标盒子与字形都从这条行高按比例派生，三者永远等高。
- 唯一实现：`ui_token::chat::input`（`ROW_HEIGHT_EXTRA/ROW_HEIGHT_MIN/BUTTON_GAP/BUTTON_BORDER_RATIO`）+ `QmChatInputRowHeight`、`QmChatTranslateButtonSize`、`QmChatTranslateButtonBorderWidth`、`QmChatTranslateButtonInnerSize`、`QmChatTranslateIconBoxSize`、`QmChatTranslateIconGlyphSize`（`src/game/client/components/qmclient/chat_input_layout.h`）；调用点 `CChat::RenderTranslateButton`（`chat.cpp`）。
- 禁止的反面写法：按钮直径直接取字号（行高与前缀块各算一次就一大一小）；在小圆上画 1 单位的固定描边；把图标盒子与字形尺寸混为一谈（`DoLabel_QmIcon` 收盒子，绘制层再乘墨迹比例）；图标在盒子内再内缩一圈。

### 2.18 图标按钮（icon button）

- 定义：只表达一个动作、不带文字的方形/圆形按钮。刷新、重载、重试、复制这类「动词即全部语义」的动作一律用它；文字按钮只留给需要完整句子才能说清的动作（如「导入并重启」）。
- 唯一实现：`ui_widget::DoIconButton`（`src/game/client/QmUi/UiButtons.cpp`），调用点走 `CUi::DoButton_QmIcon` / `DoButton_Menu_QmIcon`；图标语义固定：刷新与重试 `EQmIcon::ARROW_ROTATE_RIGHT` + `FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT`，复制 `EQmIcon::COPY`，好友与收藏 `EQmIcon::HEART`，静音 `EQmIcon::BAN`，观战与跟随 `EQmIcon::EYE`，屏蔽表情 `EQmIcon::HEART_CRACK`。禁用走 `Enabled` 形参（外观与命中一起关闭），不要用 `Flags = 0` 绕过。
- 禁止的反面写法：用文字按钮承担刷新/复制/重试；同一语义换用别的字形（`ARROWS_ROTATE` 与 `ARROW_ROTATE_RIGHT` 混用、直接写 `"\\xEE\\x80\\xB6"` 转义）；图标按钮不给悬停气泡；禁用态直接跳过绘制（按钮数量随状态横跳，玩家看不到「为什么没有这个动作」）；边长各写一个魔数而不是随行高派生。

### 2.17 输入光标（text caret）

- 定义：所有输入框共用的光标形态与闪烁相位，样式可配（竖线 / 下划线 / 方块），相位只由绝对时间推导。
- 唯一实现：`qm_text_caret::ResolveShape` / `BlinkVisible` / `Render`（`src/game/client/QmUi/UiTextCaret.h`、`UiTextCaret.cpp`）；调用点 `CLineInput::RenderCaret`（`src/game/client/lineinput.cpp`）、`QmConsoleUi::DrawCaret`（`QmConsoleUi.h`）；相位与闪烁门控由 `qm_text_caret::NowSeconds` / `ShouldBlink` 统一给出；token `ui_token::text_caret::*`、`ui_token::motion::CARET_BLINK_*`；配置 `qm_text_caret_style`。
- 禁止的反面写法：调用点自己画光标方块（`Graphics()->QuadsDrawTL`）；保存「本框闪烁起点」导致两个输入框相位不同；把时间缓存成静态值或写死 `m_Blink = false` 让光标常亮；写死 0.5 秒闪烁或 2 像素线宽。

### 3.1 行级七词（继承自 2026-11-11 报告 §9.1，口径不变）

同一处文字在代码里有多种写法，口头沟通时「说明」「提示」「小字」「副标题」常指向不同东西。这一节只定「设置页里一行字该叫什么、该走哪个入口」。

| 术语 | 是什么 | 何时用 | 代码里唯一正确入口 | 禁止的做法 |
|---|---|---|---|---|
| **主控行** | 承载可交互控件的普通行；它的存在与父项无关 | 设置项的默认形态 | `CSettingsContentLayoutFlow::Control(id, 高度, Rect, 行前间距)`（`src/game/client/QmUi/SettingsContentLayout.h`），间距取 `ui_token::settings::RowGap / SubRowGap / SectionGap / ActionGap` | 行后再补 `Flow.Space`（双计）；把间距写在上一行的尾部 |
| **从属行** | 只有主控行**当前值**成立时才存在的行；父项关掉时**不参与布局、不占高、不可交互** | 开关打开后才出现的参数行 | `SettingsDependentRows::Reveal(Ui(), Flow, 条件, FrameDt, MotionLevel)`（`src/game/client/QmUi/SettingsRowReveal.h`，内部即 `CSettingsConditionalRows` + `CSettingsContentRevealScope`） | 用禁用态/置灰冒充隐藏；用固定高度留白；在 `if` 里直接不声明（高度瞬跳、无过渡）；自己算显隐进度 |
| **行尾说明** | 同一行内、控件右侧或紧随控件的**常显**短提示 | 单位、当前值、一行内放得下的补充约束 | `DoSettingsInlineHint(CUi*, id, Row, RightReserve, 文本, m_SmallSize)`（`SettingsRowReveal.h`，内部 `TEXT_SECONDARY` + 右对齐 + 省略 + 不占行高）；控件内后缀走 `ui_widget::SInputFieldOptions::m_pTrailingText` + `ResolveInlineTrailingTextLayout`（`src/game/client/QmUi/UiForms.h`） | 写成长句或换行；为它单开一行高度；用 tooltip 代替常显提示；自己 `DoLabel` 且字号/颜色各写一份 |
| **说明行** | 独立一行、自己占行高的解释文字 | 一句话说不完、需要换行的补充说明 | `CSettingsContentLayoutFlow::Text(id, 文本, 缩进, FontSize, 行后间距, 测量, 绘制)`，字号用 `SSettingsContentMetrics::m_SmallSize` | 把它塞进行尾说明（挤爆同行）；再补 `Flow.Space`（`Text` 第 5 参本身就是行后间距） |
| **折叠行** | 「个性调整」这类「小字 + chevron」、整行可点的行，控制一段内容的展开 | 把低频高级项收进一段 | `SettingsFoldRow(...)` + `ResolveSettingsFoldRowStyle(...)`（`src/game/client/QmUi/SettingsPersonalization.h`）；展开态进 `qm_expanded_sections` | 再画分割线（已取消）；用标题字号冒充；展开态不持久化 |
| **分区标题** | 卡片内一段内容的标题 | 一张卡里有两段以上内容 | `DoSettingsSectionHeading(CUi*, Flow, id, 文本, Metrics, 行前间距)`（`SettingsRowReveal.h`） | 各卡用 `Flow.Text` 自己写一份；字号与行前间距各写一份；与分区间距叠加 |
| **卡尾动作区** | 卡片末尾的整卡级动作（「全部设为…」） | 一次性作用于整卡或整列表的操作 | 主控行 + `ui_token::settings::ActionGap(行距)`（4×） | 用分区档（2×）当卡尾；动作区前后再补 `Flow.Space` |

词汇边界：主控行/从属行讲的是**存不存在**；行尾说明/说明行讲的是**文字放在哪**；折叠行讲的是**一段内容怎么收**；分区标题/卡尾动作区讲的是**这一段是什么**。

### 3.2 从属行（Dependent row）与禁用行（Disabled row）

- 定义：从属行表示「这一行不存在」；禁用行表示「这一行存在，但当前不能改」。两者语义互斥，判定只看一条：把父项关掉，看这一行还剩什么。
- 唯一实现：`SettingsDependentRows`（`SettingsRowReveal.h`，底层 `CSettingsConditionalRows` + `CSettingsContentRevealScope`）；禁用态只用于存在但不许改的行（`ReadOnly`）。
- 禁止的反面写法：变灰但占高（禁用态冒充隐藏）；关闭时 push 一个 0 高度行（测高与卡片高度动画都会把它算进去）；绘制趟另算一次显隐。
- 三条硬约束：① 存在性只依赖父项**当前值**，`CSettingsConditionalRows` 里的进度不是判定依据；② 关闭时声明趟**不 push 这一行**；③ 测高与绘制同一入口（`Update`/`Reveal` → `Fade` → `CSettingsContentRevealScope`）。

### 3.3 互斥分支（Exclusive branch）

- 定义：同一槽位二选一/三选一，切换时槽高恒定。
- 唯一实现：`CSettingsExclusiveReveal` + `CSettingsExclusiveBranchScope` + `SlotPadding()`（`SettingsRowReveal.h`）。
- 禁止的反面写法：拿它写从属行（平白占一档槽高，正是「固定高度留白」）；两个分支同时声明两份控件 ID。

### 3.4 个性调整（Personalization reveal）

- 定义：整卡「个性调整」开关控制一整段内容的露头，不是单行从属关系。
- 唯一实现：`CMenus::PersonalizationReveal`（`src/game/client/QmUi/cards/QmCardCatalog.h`）、渲染钩子版 `QmCardRenderHook::PersonalizationReveal`（`cards/QmCardCatalog.cpp`）。
- 禁止的反面写法：用它代替 `SettingsDependentRows` 写单行；展开状态不进 `qm_expanded_sections`。

### 3.4b 页面主标题组件（page title widget）

- 定义：页面标题的唯一绘制入口，可选前置图标；图标与文字作为一个整体按左/中/右对齐，超宽省略。
- 唯一实现：`ui_widget::PageTitle(Ctx, Rect, pLabel, Icon, pFallbackIcon, Align)`（`src/game/client/QmUi/UiOverlays.h`）；尺寸走 `ui_token::page_title`；图标字形取 `FontIcons::FONT_ICON_*`。
- 禁止的反面写法：自己算图标矩形再调 `DoLabel`（居中对齐时算错）；标题不设 `m_MaxWidth` / `m_EllipsisAtEnd`（长语言串撑出面板）；图标尺寸写死 16/18。

### 3.5 胶囊页签（Capsule tab bar）

- 定义：一排等分胶囊，选中项由指示条滑块表达；页签高度与字号走设置页档位。
- 唯一实现：`ui_widget::CapsuleTabBar` / `CapsuleTabBarChrome` / `CapsuleTabBarSlotAtPoint`（`src/game/client/QmUi/UiNavigation.h`）；样式 `ui_widget::SCapsuleTabBarStyle`、`CapsuleTabBarStyleFor(表面色)`、`SettingsCapsuleTabBarStyle()`；上下文 `TabBarUiContext()`；标签按钮 `DoButton_MenuTab`（`src/game/client/components/menus.h`）；尺寸 `ui_token::settings::TAB_HEIGHT` / `TAB_FONT_SIZE` / `TAB_GAP` / `SUB_TAB_HEIGHT`。
- 禁止的反面写法：手写一排按钮 + 自绘滑块；页签高度按窗口高度浮出 22~28；页签字号按行高反推（与设置页不一致）。

### 3.6 分段选择（Segmented choice）

- 定义：行内多选一，值绑定配置；窄窗口退化下拉。
- 唯一实现：`CMenus::DoSegmentedChoice` / `DoSegmentedChoiceInternal` / `DoNavigationTabs`（`menus.cpp`）、`PrepareSettingsChoice`（`src/game/client/QmUi/SettingsChoice.h`）、`SSettingsChoiceLayout`（`SettingsPageLayout.h`）。
- 禁止的反面写法：一排单选按钮；自己算等分宽度；导航型选择用 `DoSegmentedChoice`（应走 `DoNavigationTabs`）。

### 3.7 筛选胶囊（Filter chip）

- 定义：可多选/可切换的短标签胶囊，等宽网格排布，文字带左右内边距。
- 唯一实现：`ui_widget::FilterChip`（`src/game/client/QmUi/UiForms.cpp`）+ `QmBrowserChipGrid` / `SQmBrowserChipGrid`（`src/game/client/components/qmclient/browser_status_layout.h`）；网格 `ui_token::server_filter::CHIP_*`（`CHIP_LABEL_PER_ROW`、`CHIP_TEXT_INSET_RATIO`、`CHIP_MIN_FONT_SIZE`）。
- 禁止的反面写法：手写胶囊底与描边；按固定像素决定一行几个；把字号写死 11。

### 3.8 选项池（Option pool）

- 定义：把一组选项分到「启用池 / 关闭池」两个集合，拖动或单击在池间迁移即切换配置项；颜色等属性在预览上改，不在池里改。
- 唯一实现：`QmResolvePieOptionPool` / `QmPieOptionChipWidth` / `SQmPieOptionPoolLayout` / `SQmPieOptionPoolDrag` / `QmDrawPieOptionChip`（`src/game/client/QmUi/QmPieOptionPool.h`）；池容器圆角 `ui_token::radius::MENU`、胶囊 `ui_token::radius::PILL`、内边距 `ui_token::list::ContainerPadding`。
- 禁止的反面写法：每个选项旁再挂一个色块控件（同一件事两个入口）；拖动不设阈值（轻点即误触）；空池不保留一行高度（最后一个选项离开时卡片高度跳）；拖动状态在绘制趟推进。

### 3.9 输入框（Input field）

- 定义：单行输入是胶囊、多行保持方角；聚焦光晕由 token 推导，开关 `qm_ui_focus_glow` 只关光晕、不关焦点描边与文本焦点。
- 唯一实现：`ui_widget::InputField` / `ReadOnlyTextField` / `PasswordField` / `IntegerField`（`src/game/client/QmUi/UiForms.h`）、`ResolveInputFieldLayout`、`ResolveInputCapsuleRadius`、`ResolveInputFocusGlowLayers`、`ResolveInlineTrailingTextLayout`；光晕 `ui_token::glow::*`。
- 禁止的反面写法：直接用 `CLineInput` 自绘外壳；单行输入写成方角；聚焦描边跟随强调色之外的第四种颜色；给光晕再开一条颜色配置；用强调色不透明度冒充光晕开关。

### 3.10 开关（Toggle）

- 定义：二值开关，轨道与旋钮尺寸按字号派生；开关行的标签列由行级布局决定；悬停描边与按钮同源，固定为白色。
- 唯一实现：`ui_widget::DrawToggle` / `ResolveToggleLayout` / `ResolveBooleanControlLayout` / `ResolveSwitchRowLabelColumn`（`UiForms.h`）、设置页包装 `DoSettingsButton_CheckBox`；尺寸 `ui_token::control::SwitchMinWidth` / `SwitchTrackAspect`；悬停描边走 `ResolveUiSecondaryButtonStyle` → `ui_token::feedback::HOVER_BORDER`。
- 禁止的反面写法：自绘轨道与旋钮；开关行自己算右侧宽度（标签会压到开关）；描边取轨道的自动对比色（开着是亮轨道就描黑边、关着描白边，和按钮的悬停白边割裂）。

### 3.11 按钮（Button）

- 定义：按钮底色、描边、按压反馈由同一套样式解析，禁用态只衰减不换色。
- 唯一实现：`ui_widget::ResolveUiButtonStyle` / `ResolveUiSecondaryButtonStyle` / `SUiButtonState` / `BlendUiButtonSurface` / `ResolveUiSurfaceRadius` / `ResolveUiTextButtonRadius`（`src/game/client/QmUi/UiButtonStyle.h`）、`DrawButtonSurface`、`DoButton_Menu`、`DoButton_QmIcon`；反馈 `ui_token::feedback::*`。
- 禁止的反面写法：自绘胶囊底 + 手工描边；禁用态换一个灰；按压反馈自己写 alpha 系数。

### 3.12 列表（List）

- 定义：行式或网格式可滚动集合，行反馈（选中/悬浮）由主题派生，容器内边距与「悬浮是否带描边」都是显式能力。
- 唯一实现：`CListBox`（`src/game/client/ui_listbox.h`）：`SetCapsuleRows`、`SetContainerPadding`、`SetItemSelectionInset`、`SetRowFeedbackBorder`、`SetVisibleRowRange`、`SetScrollbarOverlay`、`DoAutoSpacing`；入场动画 `CQmListEntryAnimation` + `QmListBoxShouldStartEntryAnimation`（`src/game/client/QmUi/QmListEntryAnimation.h`）；反馈色 `Ui()->QmControlTheme().m_Selected`；悬浮描边 `ui_token::feedback::HOVER_BORDER`；行档 `ui_token::list::RowRadius/RowInsetX/ContainerPadding/PaddedRowsHeight/ROW_SPACING`。
- 禁止的反面写法：自绘行背景；给选中态再加一圈描边；把行距折进行高后 `DoAutoSpacing(0)`（组件已按行推行距）；容器内边距与 `PaddedRowsHeight` 不成对（组件会把留白兜底缩回 0）；行列表打开 `SetRowFeedbackBorder`（整行大的白框会把列表读成一排按钮，只有卡片形态才开）。

### 3.13 下拉框（Dropdown）

- 定义：触发器 + 浮层列表，浮层底色属于「弹层」层，触发器底色属于「控件」层，搜索与分页由组件内部完成。
- 唯一实现：`CQmDropdownState` / `CQmDropdownSearch` / `SQmDropdownVisualStyle` / `SQmDropdownPopupPolicy` / `QmDropdownFixedHeight`（`src/game/client/QmUi/QmDropdown.h`）、`QmSettingsDropdownVisualStyle`、`CUi::DoSettingsDropDown`、`ui_token::dropdown::EntryInsetX`。
- 禁止的反面写法：自绘展开列表；复制一份弹层配色；触发器与浮层各用一份透明度。

### 3.14 提示（Tooltip）

- 定义：悬停后的补充说明，默认不常显、不占布局；配置项说明取配置描述。
- 唯一实现：`ui_widget::Tooltip`（`src/game/client/QmUi/UiOverlays.h`）、`GameClient()->m_Tooltips.DoToolTip(...)` / `DoSmallToolTip(...)` / `DoSettingsToolTipForConfig(...)`（`CTooltips`，`src/game/client/ui.h`）；尺寸与颜色 `ui_token::tooltip::*`、`ui_token::motion::TOOLTIP_FADE`。
- 禁止的反面写法：用 tooltip 承载必须常显的信息（应当用行尾说明）；自己画矩形加文字；把长句塞进 tooltip 而不给 `m_MaxWidth`。

### 3.15 浮层骨架（Secondary panel）

- 定义：二级菜单/信息面板的外壳：标题栏、滚动内容区、行（开关/下拉/说明），生命周期由外壳统一管理。
- 唯一实现：`CSecondaryPanel` / `ui_widget::DrawSecondaryPanelShell` / `SecondaryPanelProperties` / `ResolveSecondaryPanelMetrics`（`src/game/client/QmUi/SecondaryPanel.h`）、`CUi::DoPopupMenu` + `SPopupMenuProperties`（`ui.h`）；尺寸与颜色 `ui_token::secondary_panel::*`。
- 禁止的反面写法：自己搭标题栏与滚动区；面板内行间距各写一份；打开时不登记 `SPopupMenuId`。

### 3.16 模态与轻提示（Modal / Toast）

- 定义：模态需要用户确认，轻提示不需要；两者都走统一的出现/消失过渡与层级。
- 唯一实现：`ui_widget::Modal` / `ui_widget::Toast` / `ui_widget::AnimatePresence`（`UiOverlays.h`）、`CUi::ShowPopupConfirm` 链路（`src/game/client/ui_popups.cpp`）；尺寸 `ui_token::confirm_dialog::*`、动效 `ui_token::motion::MODAL_FADE_SCALE` / `TOAST_SLIDE`。
- 禁止的反面写法：用 toast 承载需要确认的破坏性操作；模态自己写淡入；确认框宽度写死。

### 3.17 状态栏角色色（Status bar roles）

- 定义：状态栏底轨、条目底、条目标签常态与选中态是「组件内部角色」，不是玩家配置项。
- 唯一实现：`ui_token::status_bar::{TRACK, ITEM_FILL, ITEM_LABEL, ITEM_LABEL_SELECTED, RADIUS}`（`UiTokens.h`）。
- 禁止的反面写法：卡片里写 `ColorRGBA(0,0,0,0.5f)` / `ColorRGBA()`；给状态栏加一条玩家颜色配置。

### 3.18 对比表：各类提示

| 组件 | 出现时机 | 布局占位 | 交互与生命周期 | 唯一实现 |
| --- | --- | --- | --- | --- |
| 行尾说明 | 常量显示 | 同行内、不占行高 | 无交互，随行存在 | `DoSettingsInlineHint` |
| 说明行 | 常量显示 | 独占一行（`Flow.Text`） | 无交互 | `CSettingsContentLayoutFlow::Text` |
| 控件内后缀 | 常量显示 | 控件内部右侧 | 随控件值变化 | `ResolveInlineTrailingTextLayout` |
| Tooltip | 悬停/聚焦 | 浮层，不占布局 | 移开即消失；配置项自动取描述 | `CTooltips::DoToolTip` / `ui_widget::Tooltip` |
| Toast | 事件触发 | 浮层，不占布局 | 计时自动消失 | `ui_widget::Toast` |
| 模态确认 | 破坏性操作前 | 居中浮层 | 必须点确认/取消，Esc 等同取消 | `CUi::DoPopupMenu` + 确认上下文 |

### 3.19 对比表：各类浮层与菜单

| 组件 | 打开方式 | 布局占位 | 关闭条件 | 唯一实现 |
| --- | --- | --- | --- | --- |
| 下拉浮层 | 点触发器 | 覆盖在内容上，不撑开布局 | 选中、点外部、Esc、触发器失焦 | `CQmDropdownState` + `CUi::DoSettingsDropDown` |
| 二级菜单 | 点父项 | 覆盖，带 `m_BlockUnderlyingPointerInput` 语义 | 点外部、Esc、父项切换、父容器销毁 | `CSecondaryPanel` + `SPopupMenuProperties` |
| 模态确认 | 操作请求 | 居中遮罩 | 确认/取消/Esc | `CUi::DoPopupMenu` + `SConfirmPopupContext` |
| 轻提示 | 事件 | 角落浮层 | 计时结束 | `ui_widget::Toast` |
| 颜色选择器 | 点色块 | 浮层，锚定指针 | 选中、点外部、Esc | `SettingsColorPicker.h` + `CUi::ShowPopupColorPicker` |

### 3.20 公告行级排版（MOTD line layout）

- 定义：服务端公告按行分类，整行都是框线/制表字符的行按统一格子推进（一格一字形、字间距为 0，相邻横线首尾相接），其余行沿用普通文本排版；格宽与行高由字体实测，不写死像素。
- 唯一实现：`QmMotdLayout::Build` / `QmMotdLayout::IsBoxDrawingLine` / `QmMotdLayout::IsBoxDrawingCodepoint`（`src/game/client/QmUi/QmMotdLayout.h`）；接入点 `CMenus::DrainIngameMotdParagraphCache` 与 `CMenus::RenderIngameMotdStableParagraphCache`（`src/game/client/components/menus_ingame.cpp`，逐行排版与整段文本容器共用同一套请求/预算节奏）；字号与压缩下限走 `ui_token::motd::FONT_SIZE`、`ui_token::motd::BOX_CELL_MIN_RATIO`。
- 禁止的反面写法：把框线行交给普通文本排版（字距把横线切成一节一节）；用固定像素当格宽或行高（换字号就错位）；超宽时继续压缩到笔画互相吞掉而不是截断可见字数；在渲染趟逐字形即时绘制（每帧重建文本容器）。

## 四、后续更新维护协议

1. **新增或重构组件/Token 时必须同轮更新本文件。** 每次在 `src/game/client/QmUi/` 新增、重构或废弃通用组件、布局原语、行级模式或 `ui_token::` 命名空间，必须在同一次 Git 提交内按「标题（英文名） / 一句话定义 / 唯一实现 / 禁止的反面写法」四项格式增删改对应条目，缺一项视为未完成。
2. **条目格式不可裁剪。** 四项缺一不可；职责相近、易混淆的组件必须补横向对比表（时机 / 布局占位 / 交互与生命周期 / 唯一实现）。
3. **名称必须来自代码。** 条目里的类名、函数名、文件名、Token 名必须能在仓库内检索到；不得凭记忆写近似名。新增条目后至少执行一次 `grep` 校验唯一实现路径存在。
4. **既有定稿不得推翻。** §3.1 的七词与 `docs/规格/2026-11-11-设置页Token与一致性改动报告.md` 第 9 节同源；语义调整走「修订 + 记录」而不是重写。实现指针可以随代码迁移更新（例如 `DoSettingsInlineHint` 从「待补」变为已落地）。
5. **删除组件时同步删条目。** 同时检索 `src/game/client/` 与 `src/test/` 的调用点，删除只为该组件存在的测试与 helper。
6. **人工核对优先于自动检查。** 本文件没有自动校验脚本；提交前逐条对照代码，并在交付说明里写明本轮是否改动本文件。
7. **与原则文档分工。** 本文件定「叫什么、走哪个入口、禁止怎么写」；视觉分组与配置边界见 `docs/规格/2026-11-14-UI视觉分组与自定义边界.md`。两者冲突时以本文件的具体入口为准，原则层以分组文档为准。

## 五、与既有文档的关系

| 文档 | 关系 |
| --- | --- |
| `docs/规格/2026-11-11-设置页Token与一致性改动报告.md` §9 | §3.1~§3.4 的来源，术语与判定标准原样继承；实现指针按当前代码更新 |
| `docs/规格/2026-11-14-UI视觉分组与自定义边界.md` | 上游原则：配色按视觉层分组、组件内部角色色、拖拽池模式 |
| `docs/规格/2026-10-13-UI颜色token迁移总览.md` | Token 迁移账本与防回潮审计；本文件是它的规则入口 |
| `docs/规格/2026-10-10-共享UI布局与交互统一执行方案.md` | 布局与交互组件化方案；本文件补充「术语 ↔ 入口 ↔ 反面写法」三层结构 |
| `docs/规格/2026-10-21-弹窗与二级菜单交互规范.md` | 浮层交互细节；本文件 §3.15/§3.19 只定归口与对比 |
| `AGENTS.md` | 「按需读取」与「提交同步」两条引用本文件，作为 UI 改动的强制入口 |
