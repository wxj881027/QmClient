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
- 唯一实现：`src/game/client/QmUi/UiTokens.h`，命名空间共 25 个：`ui_token::color`（60 项，含搜索命中的 `SEARCH_MATCH_ON_DARK/SEARCH_MATCH_ON_LIGHT`）、`ui_token::spacing`、`ui_token::radius`（`NONE/TIGHT/BASE/SMALL_MENU/MENU/CARD/PILL`）、`ui_token::font`（`HEADLINE_LG/HEADLINE/TITLE/BODY/SMALL/CAPTION/TIP`）、`ui_token::motion`（36 项）、`ui_token::elevation`、`ui_token::control`（`PadX/PadY/IconBox/IconTextGap/SUFFIX_RATIO/SwitchMinWidth/SwitchTrackAspect`）、`ui_token::list`、`ui_token::settings` + `ui_token::settings::row`、`ui_token::feedback`、`ui_token::glow`、`ui_token::tooltip`、`ui_token::secondary_panel`、`ui_token::server_filter`、`ui_token::status_bar`、`ui_token::server_info`（`TITLE_SIZE/BODY_SIZE/TITLE_GAP/ROW_HEIGHT_RATIO/LABEL_WIDTH/CARD_RADIUS/TAGS_*`，整页只认一套行制：行高 = `BODY_SIZE × ROW_HEIGHT_RATIO`、标签列 = `LABEL_WIDTH`，备注行与开关行不再自带 `NOTE_*` 度量）、`ui_token::motd`（`FONT_SIZE/BOX_CELL_MIN_RATIO`）、`ui_token::motd`（`DESIGN_WIDTH/DESIGN_HEIGHT/TITLE_HEIGHT_RATIO/TITLE_GAP/BOX_GLYPHS_PER_STEP/FONT_SIZE`）、`ui_token::page_title`（`FONT_SIZE/HEIGHT/ICON_SIZE/ICON_GAP/ICON_Y_OFFSET`）、`ui_token::panel`（`GAP/PADDING/MARGIN`）、`ui_token::chat`（滚动条 + 气泡圆角派生 + `input::RowHeight/ButtonBorderWidth` 聊天输入行行高与翻译按钮描边）、`ui_token::player_card`（玩家信息卡分区度量）、`ui_token::text_caret`（`THICKNESS_PX/RADIUS_PX/BLOCK_RADIUS_PX/WIDTH_RATIO/OUTLINE_PX`）、`ui_token::confirm_dialog`、`ui_token::dropdown`、`ui_token::discrete_slider`、`ui_token::ime`、`ui_token::hud::font`、`ui_token::page_title`（`FONT_SIZE/HEIGHT/ICON_SIZE/ICON_GAP`）、`ui_token::panel`（`GAP/PADDING/MARGIN`）、`ui_token::chat`（`SCROLLBAR_*/BUBBLE_*`、`BubbleRadius`）、`ui_token::player_card`（`SIDE_BY_SIDE_MIN_WIDTH/CARD_MAX_WIDTH/CARD_WIDTH_RATIO/STACK_CARD_HEIGHT_RATIO/LIST_CARD_GAP/PADDING/RADIUS/SECTION_GAP/HEADER_*/ACTION_*/ICON_*/STATUS_*/DETAIL_*/LIST_*/SEARCH_HEIGHT/COPY_TOAST_SECONDS`）。
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

- 定义：行距只有四档，由行自己声明：子行 0.5×、行间 1×、分区间 2×、卡尾 4×。一组行的高度按「N 行 + (N−1) 档行距」测量，末行之后不再补行距。
- 唯一实现：`ui_token::settings::RowGap(行距)` / `SubRowGap` / `SectionGap` / `ActionGap`（`UiTokens.h`，`ui_token::settings`）；行类型旁路 `ui_token::settings::row::Of(EType)`（`PRIMARY/GROUP_HEADER/SUB/DIVIDER/FOLD/NOTE`，未缩放，仅已迁移调用点使用）。
- 禁止的反面写法：上一行尾部补间距；用 `Space(2.0f)` 代替档位；用 `settings::row` 的数值冒充派生链档位；按 N × `m_RowStep` 测量一组行（把末行行距也算进去，组间距会变成「行距 + 分区间」两倍以上——「变色速度」上方那道断层就是这么来的）。

### 2.5 圆角与表面（rounded surface）

- 定义：圆角按尺寸与曲线档推导，表面统一走同一个绘制入口，描边与光晕只是参数。
- 唯一实现：`DrawRoundedSurface(...)`、`DrawRoundedSurfaceGlow(...)`、`ResolveRoundedSurfacePlan(...)`、`ResolveRoundedSurfaceCornerRadii(...)`、`QmCornerSmoothingForCurve` / `QmEffectiveCornerSmoothing`（`src/game/client/QmUi/UiSurface.h`、`UiSurface.cpp`）；无 SDF 回退轮廓的唯一入口是 `ResolveRoundedSurfaceOutline(...)`，每角固定 `ROUNDED_SURFACE_CORNER_SEGMENTS = 24` 段，填充扇、边框环带和外缘羽化都从这一条轮廓生成（`ResolveRoundedSurfaceBorderMesh(...)` 只是它的环带拼装）；圆角档 `ui_token::radius::*`、列表档 `ui_token::list::RowRadius(H)` 与 `ContainerRadius(W,H)`。
- 禁止的反面写法：用 `Row.Draw(Color, CORNER_ALL, 5.0f)` 写死圆角；自己按 `min(w,h)/2` 算胶囊；两套描边宽度并存；用「矩形放大 + 半径加宽」手拼光晕；回退路径另写正圆、`sqrt(R²-d²)` 或固定 32×16 网格；填充、边框、羽化各取一套几何。

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

- 定义：卡片外壳、标题、描边、展开高度由 deck 统一测量与绘制，卡片内容只填内容区；内容末行到卡片下边框的留白由一档 token 统一给出，卡高随内容自适应。
- 唯一实现：`CSettingsCardDeck`（`SettingsCardDeck.h`）、`SSettingsCardDefinition`、`ResolveSettingsCardDeckPointerInput`、`CSettingsCardDeckFrameRuntime`（`SettingsCardDeckLogic.h`）；外壳绘制 `ExecuteSettingsCardChromeDraw`、边框 `SettingsCardInteractionBorderVisible`、内容裁剪 `ResolveSettingsCardContentClipRect`（`SettingsCard.h`）；测量 `SettingsCardMeasure.h`；宽度 `SettingsCardWidth.h`；动效 `SettingsCardMotion.h`；卡片几何 `BuildSettingsCardFrame`（`SettingsCardGeometry.cpp`），底边留白 = `ui_token::settings::CARD_PADDING` + `ui_token::settings::CARD_CONTENT_BOTTOM_GAP`。
- 禁止的反面写法：卡片里自己画标题栏或描边；卡片内容高度与测量函数各写一份；卡片折叠状态不进 `SettingsCardCollapseState`；在卡片末尾用 `Flow.Space` 自行拼底距（会让同页卡片的末行到底边各不相同——统一走 `CARD_CONTENT_BOTTOM_GAP`）；测量函数按旧版行数多留空白（改版后没同步测量，卡片底部会凭空多出整行）。

### 2.10 标签与本地化缓存（label and localized cache）

- 定义：设置行标签只走统一入口，本地化后的文本与宽度进缓存，避免每帧重排。
- 唯一实现：`DoSettingsMenuLabel(...)`（`src/game/client/components/menus.cpp`）、`CQmLocalizedLabelCache`（`src/game/client/QmUi/QmLocalizedLabels.h`）、`SLabelProperties`（`m_MaxWidth`、`m_EllipsisAtEnd`、`m_vColorSplits`）、字体档 `ui_token::font::*`。
- 禁止的反面写法：`Ui()->DoLabel` 里写死字号与颜色；标签宽度自己量；同一行两种截断策略。

### 2.11 页面主标题（page title）

- 定义：页面级标题（设置页顶部「设置」、收藏地图工作区标题）共用一个字号与行高档，图标与文字作为一个整体对齐；标题不再借用正文标尺。
- 唯一实现：`ui_token::page_title::{FONT_SIZE, HEIGHT, ICON_SIZE, ICON_GAP, ICON_Y_OFFSET, TEXT_WIDTH_SLACK}` + `ResolveTitleGroup` / `ResolveTitleTextMaxWidth`（`UiTokens.h`，一次算出图标与文字两个矩形并给出文字的最大宽度） + `ui_widget::PageTitle(...)`（`src/game/client/QmUi/UiOverlays.h`）；设置页接入点 `menus_settings.cpp`，标题样式键 `CMenus::BuildSettingsShellTitleTextStyle`（`menus.cpp`）读同一档字号。水平居中由组几何保证，垂直由 `ICON_Y_OFFSET` 做字形留白的视觉下沉。
- **文字矩形取实测自然宽度，不裁到「行宽 − 图标 − 缝」**；行宽不足时整组居中溢出到两侧空白，字号与单行都不变。传给标签的最大宽度必须走 `ResolveTitleTextMaxWidth(实测宽)`：它加 `TEXT_WIDTH_SLACK` 并**向上取整**。
- 禁止的反面写法：标题字号写死 16 或 `font::HEADLINE_LG`（曾把「设置」压成正文观感）；标题前面自己 `DoLabel` 画图标导致换语言后错位；工作区标题用 `h * 0.72f` 按行高反推字号；**文字按整行 MC 单独居中、图标再挂到左边**（组重心偏左 (图标宽+缝)/2，实机看着就是没对齐）；**把实测宽度直接塞给标签当最大宽度，或用 `(int)` 向下截断**（中文无空格，宽度差半个像素就被 `text.cpp` 的 "word can't be fitted" 分支在字间断开——「设置」变成上下两行，且与窗口比例无关）；长语言标题把文字矩形裁到行宽以内（同一处断行的另一条路径）。

### 2.12 页面页签字号（page tab font）

- 定义：页签条文字只有一档字号，设置页页签与工作区页签并排看起来一样大。
- 唯一实现：`ui_token::settings::TAB_FONT_SIZE`（`UiTokens.h`）+ `ui_token::settings::TAB_HEIGHT`；`DoButton_MenuTab` / `DoMapWorkspaceTabs` 显式传该档值。
- 禁止的反面写法：`DoButton_MenuTab(..., -1.0f, ...)` 让行高反推字号（26 行高推到 15.4，与 13 档并排就露馅）；两个页面各写一个字号。

### 2.13 面板间距（panel spacing）

- 定义：页面级面板的留白只有三档——面板之间的缝（8）、内容到面板边缘的内边距（10）、面板外缘到窗口边缘的外距（8）。
- 唯一实现：`ui_token::panel::{GAP, PADDING, MARGIN}`（`UiTokens.h`）；消费点 `QmBrowserPanelLayout` / `QmResolveBrowserBottomInset`（`browser_status_layout.h`）、`ResolveSettingsShellLayout` 的 `SETTINGS_SHELL_GAP` / `SETTINGS_PANEL_MARGIN`（`SettingsPageLayout.h`，内容区内缩仍取 `PADDING`）、`CMenus::RenderDemoBrowser`（`menus_demo.cpp`）、菜单壳层全局安全区 `Screen.Margin(MARGIN)`（`menus.cpp`）。
- **外距只算一次**：壳层已对 `Screen` 应用过 `MARGIN` 的四边安全区，页面再无条件切一次 `MARGIN` 会变成 16。页面必须用 `QmResolveBrowserBottomInset(*Ui()->Screen(), MainView)` 按「整屏底 − 页面底」算出已内缩量，只补足差值；回放/幽灵视角下安全区不生效时自动补满 8。最终「面板底 → 窗口底」恒等于 `MARGIN`，与窗口缩放无关。
- 禁止的反面写法：调用点写 `HSplitBottom(10.0f, ...)` / `Margin(12.0f, ...)`；三个页面各用一套 8/10/12；面板间隙、内边距与外距互相顶替——底边留白拿 `GAP` 当外距、或把设置页内容区内缩改成 `MARGIN`（8）让整页缩水 2px，都属于这一条；**只改常量名字却不去算实际叠加量**（改 `GAP`→`MARGIN` 两者同为 8，视觉不变，安全区仍被重复叠加）。

### 2.9b 列表卡片（list card）

- 定义：网格或列表里的一张卡，两种形态共用同一条几何路径：`STACK`（头部标题/副标题/尾部区 + 内容 + 可选底栏）与 `ROW`（前置区 + 主体 + 可选尾部槽）。组件只定「一张卡占多大、各部分从哪切、外壳怎么画」。
- 唯一实现：`qm_list_card::ResolveLayout` / `EAxis` / `SMetrics` / `SOptions` / `SLayout` / `DrawShell` / `DrawStaticShell`（`src/game/client/QmUi/QmListCard.h`）；圆角 `ui_token::radius::CARD`（资源卡片这一档传 `MENU`）、内边距 `ui_token::settings::CARD_PADDING`、描边 `ui_token::feedback::HOVER_BORDER`。接入点：`CMenus::RenderSettingsBindWorkshop`（`bind_workshop_ui.cpp`，STACK + DrawShell）、`RenderAssetsCardShell`（`menus_settings_assets.cpp`，STACK + DrawStaticShell）、`CMenus::RenderSettingsTeeSkinList`（`QmCardCatalogTee.cpp`，ROW，内边距 0 保持原观感）。
- 禁止的反面写法：卡片里自己写内边距与圆角再调 `DrawRoundedSurface`；在命中已由 `CListBox` 登记时再调 `DrawShell`（会抢命中，静态壳用 `DrawStaticShell`）；把数据加载、缩略图任务、预览缓存、入场动画塞进组件（资源页与皮肤页各有一套独一份的预算与调度）；把列表卡片当设置卡片用（折叠、卡间阴影、deck 测量不属于它）；**卡片头部文字四边用同一个 `Margin` 内缩**——纵向只能留 6（头部高度固定 30/40，标题行 + 作者行要靠这段空间），横向同档 6（`ui_token::spacing` 的固定档），直接调大到 8 以上会把作者行压成 6px 高。

### 2.9c 标签（tag）

- 定义：一颗**尺寸固定**的胶囊，承载「本地 / 网络 / 跟随 / 地图标签」这类状态词或分类词。它不是按钮：可点语义（命中、动画、禁用）留在调用点，组件只管外形与配色。
- 唯一实现：`ui_tag::{HEIGHT, PADDING_X, RADIUS, FONT_SIZE, EColorMode, SColors, Resolve, MeasureWidth, FitRect, Draw, DrawInRect}`（`src/game/client/QmUi/UiTag.h/.cpp`）。高度 16、水平内边距 6、圆角 8、字号 `ui_token::font::SMALL`，全部是常量——同一排标签永远等高、等圆，只有文本长度影响宽度。`UiScale` 只给「整个面板按可用空间缩放」的场景（服务器信息面板）用，同一次绘制传同一个值。
- **三种颜色来源，调用点不许自己写 fill**：
  1. `RANDOM_FIXED`：由标签文本哈希出**莫奈色板的下标**（`ui_tag::MONET_PALETTE`，12 色相 × 4 变体 = 48 色：罂粟红 / 赭橙 / 麦黄 / 橄榄 / 苔绿 / 灰绿 / 雾青 / 湖蓝 / 群青 / 紫罗兰 / 丁香紫 / 玫瑰粉，每个色相有浅灰 / 浅艳 / 深灰 / 深艳四档）。同一个词永远同一色，跨会话、跨机器稳定——「随机」只发生一次，之后由哈希钉死，所以不需要存颜色配置，也不会换会话变色（地图标签、用户自建标签）。**为什么是固定色板而不是 HSL 等距生成**：莫奈的关键在「低饱和 + 相近明度」，按色相均分会跑出高饱和青绿与紫红；48 色里每个色相的明度是**按感知亮度反解**的（两档 0.50 / 0.42），所以黄不会亮到发白、紫不会暗到发黑。**取色要打散**（`ui_tag::MonetPaletteIndex`）：数组按色相分组写，索引换算是「变体在外、色相在内」，相邻种子不会落在同一色相的深浅变体上。**索引不按添加顺序轮转**：顺序是玩家拖动排的，按位置取色会让一次拖动把整排颜色全换一遍。
  2. `FIXED`：调用点给语义色（本地 = 绿、网络 = 蓝灰），整颗胶囊用它。
  3. `DERIVED`：调用点给基色，按固定公式加深一档（`× 0.72`）（跟随标签跟好友卡片同色系，但比卡片更实）。
- 取色器（`CUi::ShowPopupColorPicker`）里的「预设 · 莫奈」组**直接引用同一张色表**（`ui_tag::MONET_PALETTE`，48 格按色相分组、8 列布局一行两个色相），不在取色器里再抄一份 RGB：改了色板，标签与预设一起变。该组只读、点击直接应用，名字按色相重复四次（连续四格就是同一色相的四档，格宽只够 3~4 个汉字）。
- 胶囊**不描边**，文字**不按填充亮度自动翻色**：文字统一取 `ResolveConfiguredTextColor()`（用户「文本颜色模式」那一档）。1px 亮边会让标签读成一张小卡片，抢列表卡片的层级；自动翻色会让同一屏出现两套文字色。
- 接入点：资源卡片状态徽章（`AssetsCardStatusReadyColor` / `NetworkColor`，FIXED）、好友列表「跟随」药丸（accent 色，DERIVED）、地图标签面板的已审核标签与「当前标签」行（RANDOM_FIXED，同一词在两处同色）；用户自建标签存 `QmMapTags::CLocalTags`（`qm_local_map_tags.txt`，仅本地、不上传）。
- 地图标签面板的版式（自上而下）：标题 + 右上角两颗图标按钮（**上传本地标签供审核** + 刷新，说明只在悬浮提示里） → **`本地标签：`（第一行）**：自己给这张图打的词，空时只有一颗 `+` → **`云端标签：`**：已审核标签（点击 = 赞同，可换行往下长；空态就写在这一行「当前暂无标签」，不再单独占一整块列表区）→ **`标签示例：`（钉在面板底边）**。中间整块空间归云端标签，`本地标签` 不再被底部留白推到面板下半部分。
- 标签行的**行内输入框**：点 `+` 后输入框长在 `+` 原来的位置，与胶囊同高（`ui_tag::HEIGHT`）、同一行，底栏行数与高度都不变——不允许另起一行把上面的内容顶上去，也不允许输入框比同一行的胶囊更高。点 `+` 即把焦点交给输入框（不用再点一次）；点行外或输入框失焦 = 放弃这次添加，清空并收起、把 `+` 还回去（点示例不算放弃）。
- 行内小动作按钮（`+` 加标签、`×` 删标签）边长统一取 `ui_token::server_info::TAGS_ACTION_SIZE`（= 胶囊高 16），两颗粒按钮视觉等大；不允许一颗写 14 一颗写 12。**两颗都走 `DoButton_QmIcon`**（圆底、悬浮反馈、图标墨迹居中由同一件提供），不要自绘圆底再调 `DrawQmIcon`——自绘版取的是通用 `TINT_PART`，与 ICON role 不是同一色阶，并排能看出不是一套件。
- 本地标签的两种交互**必须共用一颗胶囊**（`MapTagsPanel.cpp`）：**单击 = 展开/收起它自己右侧的 `×`**（`×` 的宽度与透明度按进度从 0 生长，收起动画播完才清「正在绘制」的那颗，换一颗则从 0 重新长）；**按住移动超过 `ui_token::spacing::XS` = 池内拖动排序**（复用选项池的 `SQmPieOptionPoolDrag` 骨架 + `QmResolveChipDropSlot`，落点画插入竖线、拖动中画幽灵胶囊；松手写 `QmMapTags::CLocalTags::Move`，顺序落盘 `qm_local_map_tags.txt`）。拖动中的 `DoButtonLogic` 返回值一律忽略，否则松手会被当成一次「点击展开」。云端标签显示的文本带「×N +1」后缀，取色 key 必须传词本身（否则赞同数一变整颗胶囊就换色，同一个词在本地/云端也不同色）。未展开的标签**不占 `×` 的位置**；行末恒留一个动作位（收起时 `+`、输入时输入框），标签再多也不把它挤掉。
- 面板里的图标按钮即使当前无事可做也画成**正常亮度**：禁用外观会压暗图标再加一条红斜线，玩家看不出它是什么。可用时机由点击处理决定，原因写进悬浮提示；提示必须走 `DoToolTipForRect`（矩形悬浮）——禁用按钮不会登记热项，普通 `DoToolTip` 的「热项 == 自己」判定会让整条提示消失。
- 服务器信息页的行制（同一页四张卡共用一个读法）：一行 = 「标签列 + 值列」，行高 `BODY_SIZE × ROW_HEIGHT_RATIO`、标签列宽 `LABEL_WIDTH`，两列都以**同一个基线**绘制——标签走 `DoMenuLabelStreamed`，值走 `RenderIngameServerInfoValueCached`，两者必须都经 `RenderLabelTextContainerAligned` 出图（在矩形左上角裸画 `RenderTextContainer` 会让值比标签高半个字高）。备注行、收藏开关行与上面的字段行同高同列同间距，不再各自带 `NOTE_*` 度量或额外空行。
- 布尔开关全站只有一颗件：`ui_widget::Toggle` + `ResolveBooleanControlLayout`（轨道宽 = `max(SwitchMinWidth(正文), 行高 × SwitchTrackAspect)`）。服务器信息页的收藏开关也用这颗件，但尺寸取**绝对档**（`settings::ROW_HEIGHT` 20 × `SwitchTrackAspect` 1.65 = 33），不跟本页行高（22.5）走——跟行高走会比设置页大一圈，两处并排读作两个控件。行内垂直居中，标签列与值列的基线不受影响。
- 行内输入框（地图备注等）的**文字比同行标签小一档**（`FontSizeBody × 0.85`）：正文 15 号塞进 22 高的胶囊会顶满上下边距，读起来比左侧标签还重。
- 禁止的反面写法：自绘胶囊再调 `DrawRoundedSurface`；字号/内边距/高度跟着所在行高或卡片宽度变；每次进游戏重新随机一次颜色（必须由标签文本决定）；给标签加描边；把「提交到服务器」和「加到我自己的标签」混成同一个动作；把审核规则写成面板正文（应进上传按钮的 tooltip）；把行内输入框塞进第二行、或让它比同行胶囊高；给图标按钮配可能为假的 `Enabled` 再指望普通 `DoToolTip` 出提示；常驻一排 `×`（删除钮只在点击后出现）；另写一套拖动排序状态（池内排序与选项池换池共用同一骨架，只有落点计算不同）。

### 2.14 对比表：三类动效机制

| 机制 | 何时用 | 布局占位 | 唯一实现 | 禁止混用 |
| --- | --- | --- | --- | --- |
| 卡片入场/高度过渡 | 卡片出现、内容高变化 | 由 deck 测量，过渡期高度连续 | `SettingsCardMotion.h` + `CUiV2AnimationRuntime` | 用它做单行显隐 |
| 从属行展开 | 单行随父项出现/消失 | 关闭时不占高、不进命中 | `CSettingsConditionalRows` + `CSettingsContentRevealScope` | 用固定高度留白冒充 |
| 互斥分支切换 | 同一槽位二选一/三选一 | 槽高恒定 | `CSettingsExclusiveReveal` + `CSettingsExclusiveBranchScope` + `SlotPadding()` | 用它写从属行（白占一档槽高） |

### 2.8b 控制台日志滚动（console log scroll）

- 定义：控制台日志的滚轮、翻页、滑块与触控拖动共用全站滚动物理；偏移以像素推进、读取时折回「整行 + 亚行余量」，贴底时新日志自动跟随。滚轮每档以 5 行为基准，按全站倍率缩放，最多 8 行，并按可见行数兜底。
- 唯一实现：`CQmConsoleScrollRuntime`（`src/game/client/QmUi/QmConsoleScroll.h`）+ `CQmScrollState` / `QmNativeWheelScrollConfig`，行数解析走 `QmConsoleWheelLines`（`console_scroll.h`）与 `QmResolveWheelLineCount`（`QmUi/QmScroll.h`）；调用点 `CGameConsole::CInstance::OnInput`、`CGameConsole::OnRender`（`src/game/client/components/console.cpp`）。
- 禁止的反面写法：在控制台里自己写 `m_BacklogCurLine += 行数` 直跳滚轮；拖动滑块走弹簧（应跟手）；新日志到达时搬整型行锚而不是按内容增长量抬高偏移；另写一套平滑时长或阻尼；把 `qm_ui_scroll_step` 当像素步长除以行高来求行数。

### 2.8c 覆盖式滚动条（overlay scrollbar）

- 定义：铺在内容上层、不占内容宽度的滚动条。轨道长度等于宿主视口，两端各内缩一档；滑块长度按「视口高 / 内容总高 × 轨道」求，夹在最小滑块与轨道之间；位置由进度决定（0 = 顶部/最旧）。内容装得下时不出现。
- 唯一实现：`ui_widget::ResolveOverlayScrollbarFrame`（`src/game/client/QmUi/UiForms.h`，几何本体委托 `CQmScrollController::PreviewFrame`），绘制走 `ui_widget::DrawScrollbarHandle`；滑块悬浮/按下放大的热项必须由 `CUi::DoButtonLogic` 登记，控件才会画大。
- 禁止的反面写法：各页面自己算轨道与滑块；用固定像素上限截断滑块长度；只画滑块、不登记热项导致悬浮不变大；另写一套放大像素。

### 2.8d 控制台顶栏按钮（console toolbar button）

- 定义：控制台筛选与动作按钮的出现、消失都走同一条存在度；同一按钮换文案时复用槽位并平滑过渡宽度，按钮宽度、透明度和命中一起收放，任意两颗实际可见的按钮之间保留同一档固定间距，目标集合决定是否折成两行，选中底色取全局强调色的派生档。
- 唯一实现：`QmConsoleUi::CToolbarPresenceMotion`、`QmConsoleUi::LayoutToolbarAnimated`、`QmConsoleUi::ToolbarFlowGap`、`QmConsoleUi::ToolbarFlowWidth`、`QmConsoleUi::ResolveToolbarWidth`、`QmConsoleUi::ResolveConsoleButtonFill`、`QmConsoleUi::Button`（`src/game/client/QmUi/QmConsoleUi.h`）；显隐动效取 `ui_token::motion::NAVIGATION_APPEAR`、文案宽度取 `ui_token::motion::TAB_SWITCH`，调用点 `CGameConsole::OnRender`（`src/game/client/components/console.cpp`）。
- 禁止的反面写法：动作集合变化时瞬切按钮或只淡文字不缩宽度；把搜索/取消、展开/收起拆成两颗按钮同时一缩一长；用当前动画宽度决定折行；隐藏槽位吞掉左右按钮之间的间距或让间距随存在度缩到 0；选中底色取控制台命令语法色或在调用点写裸 `ColorRGBA`；显隐和选中共用同一条 `ALPHA` 轨道。

### 2.8e 控制台类型切换与远程头部（console type switch）

- 定义：本地/远程控制台是同一面板的两种内容；切换时背景、边框、阴影和顶部栏按同一条 0→1 混合轨道过渡，远程头部文本单独走存在度入场/出场；类型混合轨道从当前值续接，F1/F2 切换时头部重新入场。远程与本地共用同一档 `qm_console_font_size` 字号，日志、头部、页脚不再各用一套字号。
- 唯一实现：`QmConsoleUi::ResolveConsoleTypeBlend`、`QmConsoleUi::MixColor`（`src/game/client/QmUi/QmConsoleUi.h`）；远程头部存在度 `QmConsoleUi::CToolbarPresenceMotion<1>`（`CGameConsole::m_HeaderPresenceMotion`）；调用点 `CGameConsole::OnRender`、`CGameConsole::Toggle`（`src/game/client/components/console.cpp`）。类型混合取 `ui_token::motion::TAB_SWITCH`，头部取 `ui_token::motion::NAVIGATION_APPEAR`。
- 远程角色色：`ui_token::console_remote`（`src/game/client/QmUi/UiTokens.h`）只放远程背板、边框、投影三档灰紫/玫红，属于远程状态的角色色，不是玩家逐项配置项。
- 禁止的反面写法：本地/远程整块硬切；一个 `ALPHA` 轨道同时承担类型混合和头部显隐；远程顶部用裸 `TextRender()->Text` 直接瞬移；切换时把两份日志同时画出做交叉淡化；远程与本地各写一套顶栏行高、文字基线或字号；远程固定写死 `10` 或另抄一套 `ColorRGBA(0.4f, 0.2f, 0.2f, ...)` 暗红；顶栏/页脚固定高度不跟 `ConsoleHeight` 收缩（主体已收起，横条还留在屏幕上）。

### 2.8f 控制台页脚状态行与输入提示区（console footer / hint block）

- 定义：本地与远程控制台共用同一条底部页脚：左侧固定为 `行 x - y（当前/已锁定）`，右侧为快捷键提示；远程不再把这段信息放在左上头部。输入行下方的提示区按本帧真实内容动态占位：空输入为 0 行，搜索匹配为 1 行，命令帮助按可用宽度换行（最多 2 行）并与用法各占一行，合计最多 3 行。
- 唯一实现：`QmConsoleUi::ResolveConsoleHintReserve`（`src/game/client/QmUi/QmConsoleUi.h`）；页脚与提示绘制调用点 `CGameConsole::OnRender`（`src/game/client/components/console.cpp`）。
- 禁止的反面写法：远程与本地各写一条状态行或各用一套页脚高度；空输入仍固定预留提示行；把 `帮助:` 与 `用法:` 串在同一行互相覆盖；提示换行后越过页脚；为提示区另写一套行高/留白常量。

### 2.8g 终端按钮折返手势（start menu console toggle）

- 定义：主页右下角终端按钮在控制台打开/关闭动画期间仍可点按；主页每帧把按钮的真实命中矩形登记给控制台，控制台在自己的输入趟按「按钮内按下、按钮内释放」判定一次开关，因此动画中每次释放都能折返，且不会把打开动画首帧的同一次点击立刻又关掉。
- 唯一实现：`QmConsoleUi::CStartMenuTogglePress`（`src/game/client/QmUi/QmConsoleUi.h`）、`CGameConsole::RegisterStartMenuToggle` / `CGameConsole::UpdateStartMenuToggle`（`src/game/client/components/console.cpp`）；登记点 `CMenusStart::RenderStartMenuImpl`（`src/game/client/components/menus_start.cpp`）。
- 禁止的反面写法：动画期间直接让主页跳过输入（按钮永远点不到）；在主页背景渲染趟直接处理点击（同一次点击被控制台和主页各算一次）；按下发生在控制台激活之前也算作折返；按钮矩形跨帧复用导致切页后误点。

### 2.19b 地图预览面板（map preview panel）

- 定义：服务器列表与投票页的地图预览是同一个二级面板：标题与右侧说明、实体层开关、离屏地图画布、拖拽平移与滚轮缩放共用一份实现和一份 LRU 缓存；调用点只提供地图名、说明文字和缺图时是否允许自动下载。
- 唯一实现：`CQmMapPreviewPanel`（`src/game/client/components/qmclient/map_preview_panel.h`，缓存与离屏渲染在 `.cpp`）、`CQmBrowserMapPreviewView` + `QmBrowserMapPreviewInput`（同目录 `browser_map_preview.h`）、`CQmBrowserMapPreviewLoadJob`（`browser_map_preview_loader.h`）；入口 `DoIconButton` 只用 `EQmIcon::EYE`。
- 禁止的反面写法：投票页另画封面缩略图或整图适配作为“预览”；两处各建一份地图缓存；在调用点复制拖拽/缩放/实体层开关；把地图画布塞进事实行排版（预览必须以地图为主体）。

### 2.19 圆角与胶囊（radius / capsule）

- 定义：表面形状只由「半径」决定。半径取控件短边的一半即胶囊端头，取短边一半且宽高相等即正圆；宽度明显大于高度（≥1.8 倍）时按半高取端头。
- 唯一实现：`ui_widget::DrawRoundedSurface`（`src/game/client/QmUi/UiSurface.h`）+ `ResolveRoundedSurfaceCorners`。给了非零半径又传 `IGraphics::CORNER_NONE` 的调用点一律按四角全圆处理。
- 胶囊端头档位（capsule seam）：配置项 `qm_ui_capsule_style` 与 `qm_ui_capsule_apple_strength` 只允许经 `QmEffectiveCapsuleSeam()` 换算成 `m_CornerSeam`（0 = 普通端头，0.5 = 丝滑直边，1.0+强度 = 苹果三段式端帽）。该函数与 `QmEffectiveCornerSmoothing()` 并列，任何要画胶囊端头或复制面板圆角掩码的调用点（`DrawRoundedSurface` 内部、模糊掩码、HUD 小地图、作者名牌、地图预览）都取这一个入口。
- 禁止的反面写法：同时传半径与 `CORNER_NONE` 期望方块（要方块应把半径给 0）；在调用点自行判断走引擎四角接口还是 SDF；把贴图内容裁成胶囊（贴图只做内收缩进圆头）；在调用点重写 `qm_ui_capsule_style == 2 ? 1.0f + ... : (== 1 ? 0.5f : 0.0f)` 这类三元式（分档容易写反，投票页地图预览就曾写成 `== 1 ? 0.0f : 1.0f`，与同屏其余五处端头相反）。

### 2.15 玩家信息卡（player card）

- 定义：玩家页一名玩家一行，整行一张卡，与服务器浏览器「好友」页同一种读法：左边角色头像、中间名字与战队两行、右侧一排动作图标（好友 / 静音 / 屏蔽表情 / 跟随 / 复制皮肤）。没有选中态，也没有右侧展示页。
- 唯一实现：`QmPlayersUi::PlayerRowLayout` / `QmPlayersUi::PlayerIconRow`（`src/game/client/QmUi/QmPlayerCard.h`），列表为单列 `CListBox`（`src/game/client/QmUi/QmPlayersMenu.cpp`）。
- 禁止的反面写法：点选后另开右栏展示页；把动作按钮放在展示页里；跟随用眼睛图标（应复用好友列表的播放/停止）；复制动作拆成「复制战队」之类的臆造项。

### 2.16 聊天气泡与聊天滚动条（chat bubble / chat scrollbar）

- 定义：游戏内聊天框是独立的 300 高虚拟屏，尺寸单位是「聊天逻辑单位」（1 单位 ≈ 屏幕高度/300），不是 UI 像素；消息气泡单行画成完整胶囊、多行画成圆角矩形，聊天滚动条与列表滚动条共用同一套滑块绘制入口。历史滚轮以 3 行为基准，按全站倍率缩放，最多 8 行，并按可见行数兜底。输入框激活时始终展开历史；`cl_showchat=0` 只控制常驻聊天 HUD，不能隐藏正在输入时的历史。
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
- 唯一实现：`QmResolvePieOptionPool` / `QmPieOptionChipWidth` / `SQmPieOptionPoolLayout` / `SQmPieOptionPoolDrag` / `QmResolveChipDropSlot` / `QmDrawPieOptionChip`（`src/game/client/QmUi/QmPieOptionPool.h`）；池容器圆角 `ui_token::radius::MENU`、胶囊 `ui_token::radius::PILL`、内边距 `ui_token::list::ContainerPadding`。
- 两种用法共用同一骨架：**换池**（选项池：`SQmPieOptionPoolDrag` + 落点池判定）与**池内排序**（地图标签：同一拖动结构 + `QmResolveChipDropSlot` 算插入槽位，落点画一条竖线）。区别只在落点怎么解释，阈值、幽灵、抓取偏移、状态跨帧规则全部相同。
- 说明文本（「饼菜单包含 / 饼菜单排除」）与池容器之间的间距**取常规行距 `LineSpacing`**，与池到下一条说明的间距同档；写 0 会让标题贴住池的上边框，两块读成黏在一起的一整块。
- 禁止的反面写法：每个选项旁再挂一个色块控件（同一件事两个入口）；拖动不设阈值（轻点即误触）；空池不保留一行高度（最后一个选项离开时卡片高度跳）；拖动状态在绘制趟推进；说明文本与池之间传 0 间距。

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
- 唯一实现：`CListBox`（`src/game/client/ui_listbox.h`）：`SetCapsuleRows`、`SetContainerPadding`、`SetItemSelectionInset`、`SetRowFeedbackBorder`、`SetDeferredItemFeedback` + `DrawItemFeedback`、`SetFillViewportHeight`、`SetWheelStepScale`、`SetVisibleRowRange`、`SetScrollbarOverlay`、`DoAutoSpacing`；入场动画 `CQmListEntryAnimation` + `QmListBoxShouldStartEntryAnimation`（`src/game/client/QmUi/QmListEntryAnimation.h`）；反馈色 `Ui()->QmControlTheme().m_Selected`；悬浮描边 `ui_token::feedback::HOVER_BORDER`；行档 `ui_token::list::RowRadius/RowInsetX/ContainerPadding/PaddedRowsHeight/ROW_SPACING/GRID_SPACING`。`SetWheelStepScale` 只缩放该列表的滚轮位移，不改键盘 PageUp/Down 翻页行数、不改全局 `qm_ui_scroll_step`——高行网格（皮肤列表）用来把「每格两行」降为「每格一行」。`SetDeferredItemFeedback` 让调用方在 `DoNextItem` 之后用**卡片矩形**调 `DrawItemFeedback`（多列网格的格子矩形里含行距与行内退，按格子画反馈会比卡片大一圈）；`SetFillViewportHeight` 让容器**始终占满调用方给的矩形**：条目不足一屏、或最后一行只剩半行，都不收高度（收高度会让面板底部留一条没画到的空档，截图画廊两种情形都撞上过）。**卡片尺寸只由列宽决定**：`QmResolveGalleryRowHeight(CellWidth, CaptionHeight, PreviewAspect)` 按「预览图固定宽高比 + 说明行固定高」算行高，窗口变宽卡片等比变大；面板高度只决定这一屏放得下几行，**末行可以只露一截**（滚动列表的正常形态），超出的内容交给滚动条。禁止按可用高度反推或均分行高去「铺满面板」——那会把卡片拉伸变形，一屏反而只放得下两行（实机反馈「为啥面板只允许两行」）。多列网格的**格间距取 `GRID_SPACING`（12，一档真正的间距）**，不要沿用行胶囊那半档 `ROW_SPACING`（2.5）——卡片是独立视觉件，半档缝会让整片网格贴成一整块；卡片内的文件夹图标先定**墨迹高**（预览区高度的 34%，夹在 18~64）再按 `QmIconInkFontSize` 反算字号，不能拿字号上限当尺寸（旧实现卡 26，大卡片里图标又小又空）。
- 禁止的反面写法：自绘行背景；给选中态再加一圈描边；把行距折进行高后 `DoAutoSpacing(0)`（组件已按行推行距）；容器内边距与 `PaddedRowsHeight` 不成对（组件会把留白兜底缩回 0）；行列表打开 `SetRowFeedbackBorder`（整行大的白框会把列表读成一排按钮，只有卡片形态才开）；行内并排动作图标自己写 `VSplitRight` 固定槽——走 `ui_widget::CIconActionStrip`（`UiIconActionStrip.h`），文字可用宽度取 `ReserveWidth()`（跟着图标缓动，配合细档缩字号才是连续的悬浮收缩；只有排版确实不能逐帧变宽时才用 `ReserveWidthTarget`），图标条按反馈矩形内缘排版（按整格右缘排会溢出悬浮背景）；并排图标同字号却不按墨迹归一（见 §3.12b）；多列卡片网格把选中/悬浮反馈画在**格子**上而不是卡片上（格子里含行距与行内退，底衬比卡片大一圈，实机看着就是"没对齐"）；卡片内部再套一层暗底板 + 第二层内边距（缩略图像被塞进小框里，卡片最多「卡面 + 内容 + 一行说明」三层）；多列网格沿用 `ROW_SPACING` 当格间距（2.5px 的缝让卡片贴成一整块）；卡片内图标写死字号或字号上限（应按墨迹高反算，否则大卡片里图标明显偏小）。

### 3.12b 标签缩字号与并排图标（font fitting / action icon strip）

- 定义：标签宽度不足时**缩字号是连续的**，档位由标签自己声明：默认 `m_FontSizeStep = 1`（历史整数档，逐值兼容），悬浮收缩这类可用宽度逐帧变化的场合设 `0.5`。档位只是精度，不是循环步数。并排动作图标按**字形墨迹高**归一，不是按 em 字号归一。
- 唯一实现：缩字号算法 `ResolveFittedFontSize`（`src/game/client/ui_font_fit.h`），唯一调用点 `CalcFontSizeCursorHeightAndBoundingBox`（`ui.cpp`）；档位字段 `SLabelProperties::m_FontSizeStep`（`ui.h`）；墨迹档 `ui_token::control::ActionIconInkHeight`（`UiTokens.h`），字形墨迹比与反算 `QmIconInkHeightRatio` / `QmIconInkFontSize`（`qm_icon.h`，数值为随包 Phosphor TTF 的 fontTools 实测：方框类 0.750、星形 0.844）；图标盒与格间距仍由 `ui_token::control::IconBox` 统一。接入点 `RenderSettingsTeeSkinList`（`QmCardCatalogTee.cpp`）、`CMenus::DoButton_Favorite`（`menus.cpp`，可选 `IconFontSize`）。
- 禁止的反面写法：把 `Size--` 或逐档线性递减写回 `DoLabel`；用「禁用缩字号」代替连续化（那是把动画删掉，不是修抖动）；用 `ReserveWidthTarget` 掐掉宽度缓动却不提供别的连续反馈；同字号并排方框与星形（墨迹差 12.5%，肉眼就是一高一矮）；给收藏星写死 `12.0f` 而不跟正文字号派生；让细档位变成线性试探（必须走二分，否则每帧每标签的测量次数翻倍）；**二分中间档用任意浮点字号**（每个都是一次性物理字号，会反复光栅化该标签的全部字形，候选必须先量化到档位网格）；把只占第一行的图标占位从第二行元数据宽度里再减一次（悬浮时队列按钮出现，作者名就会在右侧还空着的时候被截掉）；给元数据行也开缩字号（辅助小字缩过头会糊，且它本就不参与悬浮收缩）。

### 3.12c 滚动步长与速度（wheel step）

- 定义：用户只有 `qm_ui_scroll_step` 一个滚轮距离入口（设置页「界面动画 → 滚动步长」，默认 10，范围 2–100）。它是全站倍率，不是某个面的绝对像素；每个滚动面按自己的语义单位（行、格、面板区）声明基准与上限，再乘 `StepScale = QmWheelStepScale()`。内部解耦不会新增用户可调的逐面速度项。
- 唯一实现：`QmWheelStepScale` + `QmResolveWheelLineCount` + `QmResolveScrollPolicy`（`QmUi/QmScroll.cpp`）产出 `SQmScrollConfig::m_WheelScale`，`QmScrollRegionParamsFromPolicy` / `QmScrollRegionParamsForSize`（`ui_scrollregion.h`）写入 `m_ScrollUnit`；行型面（控制台、聊天历史）只通过 `QmResolveWheelLineCount` 取每档行数。
- 档位表（`RowExtent = 行高 + 行距`，`RowsPerStep` 由 `CListBox::DoStart` 传入，皮肤/资源网格是 2）：

  | 滚动面 | 档位 | 每格位移 |
  |---|---|---|
  | 面板级（设置主区、wiki/统计/控制台整页容器） | `SETTINGS_PAGE/OUTER`、`QmScrollRegionParamsForSize` | `10 × StepScale × QmPanelWheelStepMultiplier()`（默认 10×1×12 = 120） |
  | 行列表 / 卡片内列表 | `MENU_LIST`、`SETTINGS_INNER` | `RowExtent × RowsPerStep × StepScale` |
  | 网格（皮肤、资源、筛选） | `SETTINGS_GRID`、`GRID`、`FILTER_GRID` | `RowExtent × RowsPerStep × StepScale` |
  | 弹层列表 | `POPUP_LIST` | `RowExtent × 3 × StepScale` |
  | 控制台日志 | 行型面 | `QmResolveWheelLineCount(5, StepScale, 可见行数, 8) × 行高` |
  | 聊天历史 | 行型面 | `QmResolveWheelLineCount(3, StepScale, 可见行数, 8)` 行 |

  `CListBox::SetWheelStepScale` 是**单列表**倍率，只乘该列表的 `m_WheelScale`（皮肤列表行高含预览图，取 0.7）。按住 Alt 滚轮额外 `× QmScrollAltMultiplier()`(3)。
- 手感参数与距离无关：`qm_ui_scroll_feel`（原版 / 平滑 / 弹簧）、`qm_ui_scroll_spring_response`、`qm_ui_scroll_spring_damping`、`qm_ui_scroll_bounce` 只改动画曲线与回弹，不改每格位移。默认档以配置默认值为准（当前原版）；原版与平滑保留为可切回的兜底档。
- 禁止的反面写法：调用点直接写绝对滚动像素；用单列表倍率去补全局配置该改的值（会让不同页面失去等比关系）；改 `DoStart` 的 `RowsPerScroll` 来调滚轮速度（它会连带把键盘 PageUp/Down 的 `RowsPerScroll×4` 一起改掉）；面板级再乘一次 `QmPanelWheelStepMultiplier`（`QmSettingsScrollConfig` 已乘过）；给控制台或聊天新增逐面速度配置，或继续用 `qm_ui_scroll_step / 行高` 求行数。

### 3.12d 游戏内 HUD 背景模糊接入（in-game blur scope）

- 定义：菜单壳层会在 `CMenus::OnRender` 里开 `CUiScopedGaussianBlur`，菜单内所有半透明 `CUIRect::Draw` 因此自动模糊；**游戏内组件不在该作用域内，必须自己开**。计分板、提示、IME、投票框都这么做。
- 唯一实现：`CUiScopedGaussianBlur`（`ui.h` / `ui.cpp`）→ `CUi::BeginGaussianBlurScope`；`CUIRect::Draw`（`ui_rect.cpp`）在 `Color.a ∈ (0,1)` 时调 `DrawRectBackdrop` → `RenderGaussianBlur`。HUD 组件只负责开作用域 + 画半透明面板，不手写模糊调用。
- 禁止的反面写法：组件里直接 `Ui()->RenderGaussianBlur(...)` 再配 `Graphics()->DrawRect(...)`（绕过统一表面链，`DrawRectBackdrop` 不会因为作用域而触发）；把「背景不透明度配置」当模糊强度传进 `RenderGaussianBlur`（透明度应由 `Color.a` 决定，模糊层保持全强度）；游戏内组件依赖菜单壳层的作用域（它根本不存在）。TClient 状态栏曾三条全占。

### 3.12e 媒体封面（唱片自转 / cover spin）

- 定义：圆形专辑封面在**播放时**匀速自转、**暂停时定格在当前角度**（不回正、不跳变）；动效等级 0 不转。HUD 媒体岛与计分板媒体条是同一套外观，必须是同一套转速。
- 唯一实现：`SQmMediaCoverSpin` + `QM_MEDIA_COVER_SPIN_PERIOD_SECONDS`（`components/qmclient/media_cover_spin.h`）。时间以秒传入（调用方用 `time_get()/time_freq()` 折算成 double 再转 float），单帧增量上限 0.1s——界面隐藏后第一帧不会突然补上几十度。绘制用 `Graphics()->QuadsSetRotation(angle)` 绕 quad 中心旋转，画完立即归零。
- 禁止的反面写法：用帧数递增角度（帧率一变转速就变）；暂停时把角度归零（复播会从 0 重新起步）；给无封面的占位音符也加旋转；HUD 与计分板各写一份速率（两处封面会明显不同步）。

### 3.12f 媒体源选择（session picking）

- 定义：同时存在多个媒体会话时，按「正在播放 > 暂停 > 未知 > 停止/关闭」选源，同档保留系统当前会话（不要为了「换而换」来回跳）。状态自称在播、但时间轴超过 5 秒没有更新且时长明确（>1s）的会话，按暂停参与选源——浏览器视频停了以后常见这种「假播放」。该判定**只影响选源**，不改写实际播放状态。
- 唯一实现：`SystemMediaControls::PickMediaSession` / `MediaTimelineLooksStale` / `ESessionActivity`（`system_media_controls.h`）；Windows SMTC 后端在 `ThreadMain` 里枚举 `GetSessions()` 后调用，MPRIS 后端本来就是同一套优先级。
- 禁止的反面写法：只信 `Manager.GetCurrentSession()`（浏览器的会话在视频停下后仍会占着「当前」，要等下一次媒体事件、典型是切歌，才让位）；按会话列表顺序取第一个（顺序不保证）；把「时间轴停住」直接当成暂停画出来（会误伤不上报时间轴的播放器）。

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
| `docs/归档/2026-10-13-UI颜色token迁移总览.md` | Token 迁移账本与防回潮审计；本文件是它的规则入口 |
| `docs/归档/2026-10-10-共享UI布局与交互统一执行方案.md` | 布局与交互组件化方案；本文件补充「术语 ↔ 入口 ↔ 反面写法」三层结构 |
| `docs/规格/2026-10-21-弹窗与二级菜单交互规范.md` | 浮层交互细节；本文件 §3.15/§3.19 只定归口与对比 |
| `AGENTS.md` | 「按需读取」与「提交同步」两条引用本文件，作为 UI 改动的强制入口 |
