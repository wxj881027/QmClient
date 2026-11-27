/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/log.h>
#include <base/math.h>
#include <base/os.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/client/backend/graphics_backend_contract.h>
#include <engine/external/tinyexpr.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmPasswordIcon.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/QmToast.h>
#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsIconOptions.h>
#include <game/client/QmUi/SettingsLabelMetadata.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTextCaret.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogInternal.h>
#include <game/client/QmUi/cards/QmCardCatalogTeeMetrics.h>
#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/message_gradient.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/settings_resource_preview.h>
#include <game/client/components/qmclient/tee_color_code.h>
#include <game/client/components/qmclient/tee_hue_cycle.h>
#include <game/client/components/qmclient/tee_skin_apply.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace std::chrono_literals;

namespace
{
	// UI 图标风格分段控件：段索引 -> 配置值。Thin 未随包（值 2 仍兼容，渲染为 Light），
	// 不再提供按钮，因此索引与配置值不是同一个序列。绘制与点击路径必须共用这一张表——
	// 历史上点击路径残留了含 Thin 的 6 项旧表，导致点击整体错位一位（点「双色调」选中「轻体」）。
	constexpr int s_aIconWeightValues[] = {4, 0, 1, 3};

	int QmIconWeightSegmentIndex(const int Weight)
	{
		const int Normalized = NormalizeQmIconWeight(Weight);
		for(int i = 0; i < (int)std::size(s_aIconWeightValues); ++i)
			if(s_aIconWeightValues[i] == Normalized)
				return i;
		// 配置值不在按钮序列里（例如仍是 Thin）：高亮 Light。
		return 0;
	}

	void LogPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/menu", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}
}

uint64_t CMenus::BuildGraphicsSettingsCards(const qm_card_catalog::SQmCardBuildContext &Ctx, std::vector<SSettingsCardDefinition> *pCards)
{
	// 冷启动时这个函数一次要 600ms，先拆开看是静态初始化（枚举显示模式）
	// 还是后面的卡片定义构建。
	const auto QmGfxNowMs = [] { return time_get() * 1000.0 / (double)time_freq(); };
	const double QmGfxEntryMs = QmGfxNowMs();
	static bool CheckSettings;
	CheckSettings = false;
	static const int MAX_RESOLUTIONS = 256;
	static CVideoMode s_aModes[MAX_RESOLUTIONS];
	static int s_NumNodes = Graphics()->GetVideoModes(s_aModes, MAX_RESOLUTIONS, g_Config.m_GfxScreen);
	static int s_GfxFsaaSamples = g_Config.m_GfxFsaaSamples;
	static bool s_GfxBackendChanged = false;
	static bool s_GfxGpuChanged = false;

	static int s_InitDisplayAllVideoModes = g_Config.m_GfxDisplayAllVideoModes;

	static bool s_WasInit = false;
	static bool s_ModesReload = false;
	if(!s_WasInit)
	{
		s_WasInit = true;

		Graphics()->AddWindowPropChangeListener([]() {
			s_ModesReload = true;
		});
	}

	if(s_ModesReload || g_Config.m_GfxDisplayAllVideoModes != s_InitDisplayAllVideoModes)
	{
		s_NumNodes = Graphics()->GetVideoModes(s_aModes, MAX_RESOLUTIONS, g_Config.m_GfxScreen);
		s_ModesReload = false;
		s_InitDisplayAllVideoModes = g_Config.m_GfxDisplayAllVideoModes;
	}

	if(pCards == nullptr)
		dbg_msg("perf/page", "graphics_prepare static_init=%.1fms", QmGfxNowMs() - QmGfxEntryMs);
	const SSettingsContentMetrics GraphicsMetrics = Ctx.m_Metrics;
	const float UiScale = GraphicsMetrics.m_UiScale;
	const float BodySize = GraphicsMetrics.m_BodySize;

	const IUiContext GraphicsCardCtx = Ctx.m_UiContext;
	const auto DoGraphicsNumericField = [this, GraphicsCardCtx, BodySize](const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel, int Min, int Max, const IScrollbarScale *pScale = &CUi::ms_LinearScrollbarScale, const char *pSuffix = "", unsigned Flags = 0u, int InputMin = -1, int InputMax = -1) {
		ui_widget::SNumericFieldOptions Options;
		Options.m_pLabel = pLabel;
		Options.m_pSuffix = pSuffix;
		Options.m_pScale = pScale;
		Options.m_Flags = Flags;
		Options.m_InputMin = InputMin;
		Options.m_InputMax = InputMax;
		Options.m_FontSize = BodySize;
		Options.m_LabelAlign = TEXTALIGN_ML;
		Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;
		if(PrepareSettingsNumericFieldLabel(SETTINGS_GRAPHICS, -1, -1, pTextId, Rect, pLabel, Flags, Options))
			return false;
		return ui_widget::NumericField(GraphicsCardCtx, GetSettingsNumericFieldState(pId), pId, pOption, Min, Max, Rect, Options);
	};
	static int s_GraphicsModeDriverBlocked = -1;
	static bool s_ShowGraphicsMode = false;
	if(s_GraphicsModeDriverBlocked != g_Config.m_GfxDriverIsBlocked)
	{
		s_ShowGraphicsMode = qm_card_catalog::HasGraphicsModeChoice(g_Config.m_GfxDriverIsBlocked != 0, [this](EBackendType Backend, EGraphicsDriverAgeType Age) {
			int Major = 0, Minor = 0, Patch = 0;
			const char *pBackendName = "";
			return Graphics()->GetDriverVersion(Age, Major, Minor, Patch, pBackendName, Backend);
		});
		s_GraphicsModeDriverBlocked = g_Config.m_GfxDriverIsBlocked;
	}
	const bool ShowGraphicsMode = s_ShowGraphicsMode;
	const auto &GpuList = Graphics()->GetGpus();
	const int OldWindowMode = g_Config.m_GfxFullscreen ? (g_Config.m_GfxFullscreen == 1 ? 4 : (g_Config.m_GfxFullscreen == 2 ? 3 : 2)) : (g_Config.m_GfxBorderless ? 1 : 0);
#if defined(CONF_FAMILY_WINDOWS)
	constexpr bool HasSystemGpuSettings = true;
#else
	constexpr bool HasSystemGpuSettings = false;
#endif
	const auto GpuControl = ResolveSettingsGpuControl(GpuList.m_vGpus.size(), GpuList.m_CanSelect, HasSystemGpuSettings);
	const int GpuRowCount = GpuControl.RowCount();
	const int GraphicsBackendRowCount = (ShowGraphicsMode ? 1 : 0) + GpuRowCount + 3;
	const qm_card_registry::SCardDefault *pDisplayDefault = qm_card_registry::FindByStableId("deck:graphics-display");
	const qm_card_registry::SCardDefault *pCardEffectsDefault = qm_card_registry::FindByStableId("deck:graphics-card-effects");
	const qm_card_registry::SCardDefault *pThemeDefault = qm_card_registry::FindByStableId("deck:graphics-theme");
	const qm_card_registry::SCardDefault *pIconsDefault = qm_card_registry::FindByStableId("deck:graphics-icons");
	const qm_card_registry::SCardDefault *pModesDefault = qm_card_registry::FindByStableId("deck:graphics-modes");
	const qm_card_registry::SCardDefault *pInteractionDefault = qm_card_registry::FindByStableId("deck:graphics-interaction");
	dbg_assert(pDisplayDefault != nullptr && pCardEffectsDefault != nullptr && pThemeDefault != nullptr && pIconsDefault != nullptr && pModesDefault != nullptr && pInteractionDefault != nullptr, "graphics settings cards must be registered");
	if(pDisplayDefault == nullptr || pCardEffectsDefault == nullptr || pThemeDefault == nullptr || pIconsDefault == nullptr || pModesDefault == nullptr || pInteractionDefault == nullptr)
		return 0;

	const SSettingsListCardGeometry GraphicsModesGeometry = ResolveSettingsGraphicsModesGeometry(s_NumNodes, GraphicsMetrics);
	// 显示模式卡片包含窗口模式、当前模式和实际模式列表；测量时为前两项
	// 保留行高与间距，避免 card deck 将列表挤压成一行。
	const float GraphicsModesTargetContentHeight = GraphicsModesGeometry.m_ContentHeight;
	// 动态高度只交给 Card Deck 处理。页面私有动画会让 measure revision 每帧变化，
	// 与 Deck 的高度轨道叠加后产生双重缓动和背景闪动。
	const uint64_t GraphicsModesMeasureRevision = static_cast<uint64_t>(std::max(0, s_NumNodes));
	const bool BackgroundRefreshCustomVisible = g_Config.m_QmBackgroundRefreshMode == 1;
	const int GraphicsDisplayRowCount = 8 + (BackgroundRefreshCustomVisible ? 1 : 0) + (Graphics()->GetNumScreens() > 1 ? 1 : 0) + GraphicsBackendRowCount;
	uint64_t GraphicsDisplayMeasureRevision = (static_cast<uint64_t>(std::max(0, GraphicsDisplayRowCount)) << 32) ^ static_cast<uint64_t>(std::max(0, OldWindowMode));
	GraphicsDisplayMeasureRevision = GraphicsDisplayMeasureRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_QmBackgroundRefreshMode);
	GraphicsDisplayMeasureRevision = GraphicsDisplayMeasureRevision * 1099511628211ULL ^ static_cast<uint64_t>(GpuList.m_CanSelect);
	GraphicsDisplayMeasureRevision = GraphicsDisplayMeasureRevision * 1099511628211ULL ^ str_quickhash(Graphics()->GetRendererString());
	GraphicsDisplayMeasureRevision = GraphicsDisplayMeasureRevision * 1099511628211ULL ^ str_quickhash(Graphics()->GetVersionString());
	for(const auto &Gpu : GpuList.m_vGpus)
		GraphicsDisplayMeasureRevision = GraphicsDisplayMeasureRevision * 1099511628211ULL ^ str_quickhash(Gpu.m_aName);
	const bool GraphicsVisualTextCustomVisible = g_Config.m_QmUiTextColorMode == 3;
	const uint64_t GraphicsVisualMeasureRevision = static_cast<uint64_t>(GraphicsVisualTextCustomVisible);
	static CButtonContainer s_aGraphicsIconColorButtons[3];
	static CButtonContainer s_aGraphicsIconWeightButtons[4];
	static CButtonContainer s_aGraphicsPasswordIconButtons[2];
	static CButtonContainer s_aGraphicsBlurModeButtons[3];
	static CButtonContainer s_aGraphicsTextColorModeButtons[4];
	static CButtonContainer s_aGraphicsSignalColorModeButtons[3];
	static CButtonContainer s_GraphicsIconCustomColorResetId;
	static CButtonContainer s_GraphicsFriendIconColorResetId;
	static CButtonContainer s_GraphicsFavoriteIconColorResetId;
	static CButtonContainer s_GraphicsSignalIconColorResetId;

	const bool RenderOnly = Ui()->RenderOnly();
	const auto BuildDefinitions = [this, Ctx, pModesDefault, pDisplayDefault, pCardEffectsDefault, pThemeDefault, pIconsDefault, pInteractionDefault, GraphicsModesTargetContentHeight, GraphicsVisualMeasureRevision, GraphicsModesMeasureRevision, GraphicsDisplayMeasureRevision, GraphicsDisplayRowCount, GraphicsBackendRowCount, ShowGraphicsMode, OldWindowMode, GraphicsMetrics, BodySize, DoGraphicsNumericField](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(6);
		const SSettingsCardSpec ModesSpec{pModesDefault->m_pStableId, Localize(pModesDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pModesDefault)};
		// 副标题以注册表描述为唯一来源，与其他卡片一致；不再对本卡单独关闭副标题。
		const SSettingsCardSpec DisplaySpec{pDisplayDefault->m_pStableId, Localize(pDisplayDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDisplayDefault)};
		const SSettingsCardSpec CardEffectsSpec{pCardEffectsDefault->m_pStableId, Localize(pCardEffectsDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pCardEffectsDefault)};
		const SSettingsCardSpec ThemeSpec{pThemeDefault->m_pStableId, Localize(pThemeDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pThemeDefault)};
		const SSettingsCardSpec IconsSpec{pIconsDefault->m_pStableId, Localize(pIconsDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pIconsDefault)};
		const SSettingsCardSpec InteractionSpec{pInteractionDefault->m_pStableId, Localize(pInteractionDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pInteractionDefault)};
		const auto AddCard = [&vCards, &Ctx](const SSettingsCardSpec &Spec, const FSettingsCardRender &Render) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			Definition.m_Render = Render;
			Definition.m_RenderMeasured = [Render](CUIRect &Content) { Render(Content); };

			vCards.push_back(std::move(Definition));
		};

		AddCard(ModesSpec, [this, GraphicsMetrics, OldWindowMode, GraphicsModesTargetContentHeight](CUIRect ContentRect) {
		char aBuf[128];
		CSettingsContentLayoutFlow Flow(ContentRect);
		CUIRect ModeList;
		CUIRect WindowModeRow;
		CUIRect ModeLabel;
		static CListBox s_ListBox;
		const float RowHeightResList = GraphicsMetrics.m_ListRowHeight;
		const float FontSizeResListHeader = GraphicsMetrics.m_BodySize;
		// 列表行与音频包列表等设置页列表同档：行文字用正文字号，小字只留给
		// 单位、角标与说明行；胶囊行内边距由同一字号派生（SetCapsuleRows）。
		const float FontSizeResList = GraphicsMetrics.m_BodySize;
		if(Flow.Control("graphics-window-mode", GraphicsMetrics.m_LineHeight, WindowModeRow)) {
		const char *apWindowModes[] = {Localize("Windowed"), Localize("Windowed borderless"), Localize("Windowed fullscreen"), Localize("Desktop fullscreen"), Localize("Fullscreen")};
		// 与其它设置行统一：左侧是行标题，右侧才是下拉框。
		CUIRect WindowModeLabel, WindowModeDropDown;
		SplitSettingsControlRow(WindowModeRow, GraphicsMetrics, &WindowModeLabel, &WindowModeDropDown, ESettingsControlWidth::LONG_CHOICE);
		DoSettingsMenuLabel(SETTINGS_GRAPHICS, -1, -1, "graphics-window-mode", &WindowModeLabel, Localize("Game window mode"), GraphicsMetrics.m_BodySize, TEXTALIGN_ML);
		static CUi::SDropDownState s_WindowModeDropDownState;
		static CScrollRegion s_WindowModeDropDownScrollRegion;
		s_WindowModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WindowModeDropDownScrollRegion;
		CUi::SDropDownProperties WindowModeDropDownProps;
		WindowModeDropDownProps.m_pPopupViewport = &m_SettingsPopupViewport;
		// 同一行已经有标题，再弹配置名/长说明的悬浮提示只会挡住列表，这里不挂提示。
		const int NewWindowMode = DoSettingsDropDown(&WindowModeDropDown, OldWindowMode, apWindowModes, std::size(apWindowModes), s_WindowModeDropDownState, WindowModeDropDownProps, nullptr);
		if(OldWindowMode != NewWindowMode)
		{
			if(NewWindowMode == 0)
				Graphics()->SetWindowParams(0, false);
			else if(NewWindowMode == 1)
				Graphics()->SetWindowParams(0, true);
			else if(NewWindowMode == 2)
				Graphics()->SetWindowParams(3, false);
			else if(NewWindowMode == 3)
				Graphics()->SetWindowParams(2, false);
			else if(NewWindowMode == 4)
				Graphics()->SetWindowParams(1, false);
		}

		}
		if(Flow.Control("graphics-current-mode", GraphicsMetrics.m_LineHeight, ModeLabel, ui_token::settings::RowGap(GraphicsMetrics.m_LineSpacing)))
		{
			int G = std::gcd(g_Config.m_GfxScreenWidth, g_Config.m_GfxScreenHeight);
			const int AspectGcd = G > 0 ? G : 1;
			const float RawHiDPIScale = Graphics()->ScreenHiDPIScale();
			const float HiDPIScale = std::isfinite(RawHiDPIScale) && RawHiDPIScale > 0.0f ? RawHiDPIScale : 1.0f;
			str_format(aBuf, sizeof(aBuf), "%dx%d @%dhz (%d:%d)", (int)(g_Config.m_GfxScreenWidth * HiDPIScale), (int)(g_Config.m_GfxScreenHeight * HiDPIScale), g_Config.m_GfxScreenRefreshRate, g_Config.m_GfxScreenWidth / AspectGcd, g_Config.m_GfxScreenHeight / AspectGcd);
			// 和上面的窗口模式行共用同一套列布局：标签在左列，数值从下拉框的左边开始。
			CUIRect CurrentLabel, CurrentValue;
			SplitSettingsControlRow(ModeLabel, GraphicsMetrics, &CurrentLabel, &CurrentValue, ESettingsControlWidth::LONG_CHOICE);
			DoSettingsMenuLabel(SETTINGS_GRAPHICS, -1, -1, "graphics-current-mode-label", &CurrentLabel, Localize("Current resolution"), FontSizeResListHeader, TEXTALIGN_ML);
			Ui()->DoLabel(&CurrentValue, aBuf, FontSizeResListHeader, TEXTALIGN_ML);
		}

		if(Flow.Control("graphics-mode-list", std::max(0.0f, GraphicsModesTargetContentHeight - 2.0f * (GraphicsMetrics.m_LineHeight + GraphicsMetrics.m_LineSpacing)), ModeList, ui_token::settings::RowGap(GraphicsMetrics.m_LineSpacing)))
		{
			int SelectedOld = -1;
			s_ListBox.SetActive(!Ui()->IsPopupOpen());
			s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
			s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
			// 统一行外观：胶囊整行 + token 行距 + 按字号派生的行内内边距；
			// 可见行上限 8，超过就出覆盖式滚动条（细轨不占行宽，不再整条隐藏）。
			s_ListBox.SetCapsuleRows(FontSizeResList);
			// 卡片测高已把两档容器内边距算进列表高度（ResolveSettingsGraphicsModesGeometry
			// 用 PaddedRowsHeight），这里显式声明同一个档位，组件才会在内边距内侧排版。
			s_ListBox.SetContainerPadding(ui_token::list::ContainerPadding(RowHeightResList));
			s_ListBox.SetVisibleRowRange(1, ui_token::list::MAX_VISIBLE_ROWS);
			s_ListBox.DoStart(RowHeightResList, s_NumNodes, 1, 3, SelectedOld, &ModeList);

			for(int i = 0; i < s_NumNodes; ++i)
			{
				if(g_Config.m_GfxScreenWidth == s_aModes[i].m_WindowWidth &&
					g_Config.m_GfxScreenHeight == s_aModes[i].m_WindowHeight &&
					g_Config.m_GfxScreenRefreshRate == s_aModes[i].m_RefreshRate)
				{
					SelectedOld = i;
				}

				const CListboxItem Item = s_ListBox.DoNextItem(&s_aModes[i], SelectedOld == i);
				if(!Item.m_Visible)
					continue;

				int G = std::gcd(s_aModes[i].m_WindowWidth, s_aModes[i].m_WindowHeight);
				str_format(aBuf, sizeof(aBuf), "%dx%d @%dhz (%d:%d)", s_aModes[i].m_CanvasWidth, s_aModes[i].m_CanvasHeight, s_aModes[i].m_RefreshRate, s_aModes[i].m_WindowWidth / G, s_aModes[i].m_WindowHeight / G);
				// 行内容区已含 token 左右内边距，文字不再贴着胶囊边缘。
				Ui()->DoLabel(&Item.m_ContentRect, aBuf, FontSizeResList, TEXTALIGN_ML);
			}

			const int NewSelected = s_ListBox.DoEnd();
			if(SelectedOld != NewSelected && NewSelected >= 0 && NewSelected < s_NumNodes)
			{
				g_Config.m_GfxScreenWidth = s_aModes[NewSelected].m_WindowWidth;
				g_Config.m_GfxScreenHeight = s_aModes[NewSelected].m_WindowHeight;
				g_Config.m_GfxScreenRefreshRate = s_aModes[NewSelected].m_RefreshRate;
				Graphics()->ResizeToScreen();
			}
		} });
		const FSettingsCardRenderMeasured RenderDisplay = [this, GraphicsMetrics, ShowGraphicsMode, BodySize, DoGraphicsNumericField, OldWindowMode](CUIRect &ContentRect) {
			char aBuf[128];
			CSettingsContentLayoutFlow Flow(ContentRect);
			const auto RowAction = [&](const char *pId, float Height, auto &&Action) {
				CUIRect Row;
				if(Flow.Control(pId, Height, Row))
					Action(Row);
				Flow.Space(ui_token::settings::RowGap(GraphicsMetrics.m_LineSpacing));
			};
			if(Graphics()->GetNumScreens() > 1)
			{
				RowAction("graphics-screen", GraphicsMetrics.m_LineHeight, [&](CUIRect ScreenDropDown) {
					const int NumScreens = Graphics()->GetNumScreens();
					static std::vector<std::string> s_vScreenNames;
					static std::vector<const char *> s_vpScreenNames;
					static char s_aScreenNamesCacheLanguage[sizeof(g_Config.m_ClLanguagefile)] = {};
					const bool RefreshScreenNames = s_vScreenNames.size() != (size_t)NumScreens || str_comp(s_aScreenNamesCacheLanguage, g_Config.m_ClLanguagefile) != 0;
					if(RefreshScreenNames)
					{
						s_vScreenNames.resize(NumScreens);
						for(int i = 0; i < NumScreens; ++i)
						{
							str_format(aBuf, sizeof(aBuf), "%s %d: %s", Localize("Screen"), i, Graphics()->GetScreenName(i));
							s_vScreenNames[i] = aBuf;
						}
						str_copy(s_aScreenNamesCacheLanguage, g_Config.m_ClLanguagefile);
					}
					s_vpScreenNames.resize(NumScreens);
					for(int i = 0; i < NumScreens; ++i)
						s_vpScreenNames[i] = s_vScreenNames[i].c_str();

					static CUi::SDropDownState s_ScreenDropDownState;
					static CScrollRegion s_ScreenDropDownScrollRegion;
					s_ScreenDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ScreenDropDownScrollRegion;
					CUi::SDropDownProperties ScreenDropDownProps;
					ScreenDropDownProps.m_pPopupViewport = &m_SettingsPopupViewport;
					const int NewScreen = DoSettingsDropDown(&ScreenDropDown, g_Config.m_GfxScreen, s_vpScreenNames.data(), s_vpScreenNames.size(), s_ScreenDropDownState, ScreenDropDownProps, &g_Config.m_GfxScreen);
					if(NewScreen != g_Config.m_GfxScreen)
						Graphics()->SwitchWindowScreen(NewScreen, true);
				});
			}

			RowAction("graphics-vsync", GraphicsMetrics.m_LineHeight, [&](CUIRect Button) {
				str_format(aBuf, sizeof(aBuf), "%s (%s)", Localize("V-Sync"), Localize("may cause delay"));
				if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_GfxVsync, "graphics-vsync-delay-warning", aBuf, g_Config.m_GfxVsync, &Button))
				{
					Client()->QueueVSyncChange(!g_Config.m_GfxVsync);
				}
			});

			const auto DoGraphicsChoiceRow = [this, GraphicsMetrics](CUIRect Row, const char *pLabel, const char *pId, const char **ppNames, size_t Count, int Current, CUi::SDropDownState &State, CScrollRegion &ScrollRegion, const void *pConfigValue, auto &&OnChanged) {
				if(ppNames == nullptr || Count == 0)
					return;
				for(size_t Index = 0; Index < Count; ++Index)
					if(ppNames[Index] == nullptr)
						return;
				Current = std::clamp(Current, 0, static_cast<int>(Count) - 1);
				CUIRect Label, DropDown;
				SplitSettingsControlRow(Row, GraphicsMetrics, &Label, &DropDown, ESettingsControlWidth::LONG_CHOICE);
				DoSettingsMenuLabel(SETTINGS_GRAPHICS, -1, -1, pId, &Label, pLabel, GraphicsMetrics.m_BodySize, TEXTALIGN_ML);
				State.m_SelectionPopupContext.m_pScrollRegion = &ScrollRegion;
				CUi::SDropDownProperties DropDownProps;
				DropDownProps.m_pPopupViewport = &m_SettingsPopupViewport;
				const int NewValue = DoSettingsDropDown(&DropDown, Current, ppNames, Count, State, DropDownProps, pConfigValue, nullptr, &Row);
				if(NewValue != Current)
					OnChanged(NewValue);
			};

			RowAction("graphics-fsaa", GraphicsMetrics.m_LineHeight, [&](CUIRect Button) {
				str_format(aBuf, sizeof(aBuf), "%s (%s)", Localize("FSAA samples"), Localize("may cause delay"));
				// 配置的有效值域是 0 到 64 的 2 次幂。设置页仅记录目标值，
				// 在图形重启时协商后端支持的样本数，避免选择时重建交换链闪屏。
				static constexpr int s_aFsaaSamples[] = {0, 2, 4, 8, 16, 32, 64};
				static char s_aFsaaSampleNames[std::size(s_aFsaaSamples)][8];
				static const char *s_apFsaaSampleNames[std::size(s_aFsaaSamples)];
				static char s_aFsaaSampleNamesCacheLanguage[sizeof(g_Config.m_ClLanguagefile)] = {};
				if(str_comp(s_aFsaaSampleNamesCacheLanguage, g_Config.m_ClLanguagefile) != 0)
				{
					for(size_t i = 0; i < std::size(s_aFsaaSamples); ++i)
					{
						if(s_aFsaaSamples[i] == 0)
							str_copy(s_aFsaaSampleNames[i], Localize("Off"));
						else
							str_format(s_aFsaaSampleNames[i], sizeof(s_aFsaaSampleNames[i]), "%dx", s_aFsaaSamples[i]);
						s_apFsaaSampleNames[i] = s_aFsaaSampleNames[i];
					}
					str_copy(s_aFsaaSampleNamesCacheLanguage, g_Config.m_ClLanguagefile);
				}
				static CUi::SDropDownState s_FsaaSampleDropDownState;
				static CScrollRegion s_FsaaSampleDropDownScrollRegion;
				int FsaaSampleIndex = 0;
				for(size_t i = 1; i < std::size(s_aFsaaSamples); ++i)
				{
					if(g_Config.m_GfxFsaaSamples == s_aFsaaSamples[i])
					{
						FsaaSampleIndex = (int)i;
						break;
					}
				}
				DoGraphicsChoiceRow(Button, aBuf, "graphics-fsaa-samples", s_apFsaaSampleNames, std::size(s_apFsaaSampleNames), FsaaSampleIndex, s_FsaaSampleDropDownState, s_FsaaSampleDropDownScrollRegion, &g_Config.m_GfxFsaaSamples, [](int NewValue) {
					g_Config.m_GfxFsaaSamples = s_aFsaaSamples[NewValue];
					// 多重采样会重建交换链；设置页只记录目标值，统一在重启图形后应用，避免选择时闪屏。
					CheckSettings = true;
				});
			});

			RowAction("graphics-high-detail", GraphicsMetrics.m_LineHeight, [&](CUIRect Button) {
				if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_GfxHighDetail, "High Detail", Localize("High Detail"), g_Config.m_GfxHighDetail, &Button))
					g_Config.m_GfxHighDetail ^= 1;
			});

			RowAction("graphics-refresh-rate", GraphicsMetrics.m_LineHeight, [&](CUIRect Button) {
				str_copy(aBuf, " ");
				str_append(aBuf, Localize("Hz", "Hertz"));
				DoGraphicsNumericField("graphics-refresh-rate", &g_Config.m_GfxRefreshRate, &g_Config.m_GfxRefreshRate, Button, Localize("Render frame rate limit"), 10, 10000, &CUi::ms_LinearScrollbarScale, aBuf, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, 0, 10000);
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_GfxRefreshRate, &Button, Localize("Frames drawn per second when V-Sync is off; 0 = unlimited"));
			});

			// 客户端更新频率：游戏逻辑、输入处理与渲染共用这条主循环节拍。
			// 和上面的渲染帧率上限放在一起，两者的从属关系才看得出来
			// （更新频率设得低时，渲染帧率上限不再单独生效）。
			RowAction("graphics-client-update-rate", GraphicsMetrics.m_LineHeight, [&](CUIRect Button) {
				str_copy(aBuf, " ");
				str_append(aBuf, Localize("Hz", "Hertz"));
				DoGraphicsNumericField("graphics-client-update-rate", &g_Config.m_ClRefreshRate, &g_Config.m_ClRefreshRate, Button, Localize("Client update rate"), 10, 10000, &CUi::ms_LinearScrollbarScale, aBuf, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, 0, 10000);
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClRefreshRate, &Button, Localize("Logic and input ticks per second; rendering follows this beat."));
			});

			RowAction("graphics-lower-update-rate", GraphicsMetrics.m_LineHeight, [&](CUIRect Button) {
				static int s_LowerUpdateRate;
				Ui()->DoConfigTooltip(&s_LowerUpdateRate, &Button, &g_Config.m_ClRefreshRate);
				if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &s_LowerUpdateRate, "graphics-lower-update-rate", Localize("Cap update rate to save power"), g_Config.m_ClRefreshRate <= 480 && g_Config.m_ClRefreshRate != 0, &Button))
					g_Config.m_ClRefreshRate = g_Config.m_ClRefreshRate > 480 || g_Config.m_ClRefreshRate == 0 ? 480 : 0;
				if(const auto *pMetadata = SettingsLabelMetadata("graphics-lower-update-rate"))
					GameClient()->m_Tooltips.DoToolTip(&s_LowerUpdateRate, &Button, Localize(pMetadata->m_pTip));
			});

			// 后台限帧：窗口失焦时主循环的节拍。跟随显示器每帧读 gfx_screen_refresh_rate，
			// 换显示器或改刷新率后自动跟上；自定义沿用 cl_refresh_rate_inactive；不限关闭后台节流。
			CUIRect BackgroundRefreshRow;
			if(Flow.Control("graphics-background-refresh", GraphicsMetrics.m_LineHeight, BackgroundRefreshRow))
			{
				static CButtonContainer s_aBackgroundRefreshButtons[3];
				const char *apBackgroundRefreshLabels[] = {
					Localize("Follow display"),
					Localize("Custom"),
					Localize("Unlimited"),
				};
				CUIRect BackgroundRefreshLabel, BackgroundRefreshSegments;
				SplitSettingsControlRow(BackgroundRefreshRow, GraphicsMetrics, &BackgroundRefreshLabel, &BackgroundRefreshSegments, ESettingsControlWidth::LONG_CHOICE);
				Ui()->DoLabel(&BackgroundRefreshLabel, Localize("Background frame rate"), BodySize, TEXTALIGN_ML);
				const int CurrentMode = std::clamp(g_Config.m_QmBackgroundRefreshMode, 0, 2);
				const int NewMode = DoSegmentedChoice(s_aBackgroundRefreshButtons, apBackgroundRefreshLabels, 3, CurrentMode, BackgroundRefreshSegments, ui_token::radius::BASE, nullptr, BodySize, &g_Config.m_QmBackgroundRefreshMode);
				if(NewMode != CurrentMode)
					g_Config.m_QmBackgroundRefreshMode = NewMode;
			}

			// 自定义频率是从属行：其它模式下不存在、不占高，显隐走统一布局过渡。
			static SettingsDependentRows s_BackgroundRefreshLimitRows;
			if(const auto BackgroundRefreshLimitRows = s_BackgroundRefreshLimitRows.Reveal(Ui(), Flow, g_Config.m_QmBackgroundRefreshMode == 1, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
			{
				CUIRect BackgroundRefreshLimitRow;
				if(Flow.Control("graphics-background-refresh-limit", GraphicsMetrics.m_LineHeight, BackgroundRefreshLimitRow, ui_token::settings::SubRowGap(GraphicsMetrics.m_LineSpacing)))
				{
					char aRefreshSuffix[32];
					str_copy(aRefreshSuffix, " ");
					str_append(aRefreshSuffix, Localize("Hz", "Hertz"));
					DoGraphicsNumericField("graphics-background-refresh-limit", &g_Config.m_ClRefreshRateInactive, &g_Config.m_ClRefreshRateInactive, BackgroundRefreshLimitRow, Localize("Background frame rate limit"), 10, 10000, &CUi::ms_LinearScrollbarScale, aRefreshSuffix, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, 0, 10000);
					GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClRefreshRateInactive, &BackgroundRefreshLimitRow, Localize("Updates per second while the window is inactive; 0 = unlimited"));
				}
			}
			Flow.Space(ui_token::settings::RowGap(GraphicsMetrics.m_LineSpacing));

			if(ShowGraphicsMode)
			{
				RowAction("graphics-backend", GraphicsMetrics.m_LineHeight, [&](CUIRect Row) {
					static CUi::SDropDownState s_BackendDropDownState;
					static CScrollRegion s_BackendDropDownScrollRegion;
					if(!Ui()->RenderOnly() && graphics_backend::IsKnownUnavailableBackendName(g_Config.m_GfxBackend))
					{
						str_copy(g_Config.m_GfxBackend, "OpenGL");
						g_Config.m_GfxGLMajor = 0;
						g_Config.m_GfxGLMinor = 0;
						g_Config.m_GfxGLPatch = 0;
					}
					const char *apGraphicsModes[] = {Localize("Compatibility mode"), Localize("Performance mode")};
					int CurrentGraphicsMode = g_Config.m_QmGraphicsMode;
					if(CurrentGraphicsMode != graphics_backend::GRAPHICS_MODE_PERFORMANCE && CurrentGraphicsMode != graphics_backend::GRAPHICS_MODE_COMPATIBILITY)
					{
						const char *pPerformanceBackend = graphics_backend::BackendNameForGraphicsMode(graphics_backend::GRAPHICS_MODE_PERFORMANCE);
						CurrentGraphicsMode = str_comp_nocase(g_Config.m_GfxBackend, pPerformanceBackend) == 0 ? graphics_backend::GRAPHICS_MODE_PERFORMANCE : graphics_backend::GRAPHICS_MODE_COMPATIBILITY;
					}
					DoGraphicsChoiceRow(Row, Localize("Graphics mode"), "graphics-mode", apGraphicsModes, std::size(apGraphicsModes), CurrentGraphicsMode, s_BackendDropDownState, s_BackendDropDownScrollRegion, &g_Config.m_QmGraphicsMode, [this](int NewValue) {
						if(NewValue != graphics_backend::GRAPHICS_MODE_PERFORMANCE && NewValue != graphics_backend::GRAPHICS_MODE_COMPATIBILITY)
							return;
						g_Config.m_QmGraphicsMode = NewValue;
						str_copy(g_Config.m_GfxBackend, graphics_backend::BackendNameForGraphicsMode(NewValue));
						if(NewValue == graphics_backend::GRAPHICS_MODE_PERFORMANCE && str_comp_nocase(g_Config.m_GfxBackend, "Vulkan") == 0)
							g_Config.m_QmVulkanApiVersion = 14;
						g_Config.m_GfxGLMajor = 0;
						g_Config.m_GfxGLMinor = 0;
						g_Config.m_GfxGLPatch = 0;
						s_GfxBackendChanged = true;
						CheckSettings = true;
						InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::BACKEND_CHANGED);
					});
				});
			}
			const auto InfoLabel = [this, &Flow, GraphicsMetrics, BodySize](const char *pId, const char *pText) {
				// 走和其它设置行同一条声明与绘制路径：行矩形、字号、左边缘都一致，
				// 不会出现「图形模式」和「图形硬件详情」首个字差几个像素的问题。
				CUIRect Row;
				const bool HasRow = Flow.Control(pId, GraphicsMetrics.m_LineHeight, Row);
				Flow.Space(ui_token::settings::RowGap(GraphicsMetrics.m_LineSpacing));
				if(!HasRow)
					return;
				SLabelProperties Props;
				Props.m_MaxWidth = Row.w;
				Props.m_MinimumFontSize = 9.0f;
				DoSettingsMenuLabel(SETTINGS_GRAPHICS, -1, -1, pId, &Row, pText, BodySize, TEXTALIGN_ML, Props);
			};
			int Major = 0, Minor = 0, Patch = 0;
			const char *pBackend = "";
			Graphics()->GetDetectedContextVersion(Major, Minor, Patch, pBackend);
			const auto RuntimeInfo = ResolveSettingsGraphicsRuntimeInfo(Graphics()->GetVersionString(), pBackend, Major, Minor, Patch);
			// 显卡 / 驱动 / 后端压成一段，用「 - 」分隔；逐行加前缀会把卡片撑得很高。
			char aHardwareInfo[1024];
			str_format(aHardwareInfo, sizeof(aHardwareInfo), "%s - %s - %s",
				Graphics()->GetRendererString(),
				RuntimeInfo.m_Driver.empty() ? Localize("Unknown") : RuntimeInfo.m_Driver.c_str(),
				RuntimeInfo.m_Backend.empty() ? Localize("Unknown") : RuntimeInfo.m_Backend.c_str());
			InfoLabel("graphics-hardware-title", Localize("Graphics hardware details"));
			InfoLabel("graphics-hardware-info", aHardwareInfo);
			if(ResolveSettingsGpuControl(Graphics()->GetGpus().m_vGpus.size(), Graphics()->GetGpus().m_CanSelect, HasSystemGpuSettings).m_ShowSelector)
			{
				RowAction("graphics-gpu", GraphicsMetrics.m_LineHeight, [&](CUIRect Row) {
					const auto &GpuList = Graphics()->GetGpus();
					static CUi::SDropDownState s_GpuDropDownState;
					static CScrollRegion s_GpuDropDownScrollRegion;
					static std::vector<std::string> s_vGpuNames;
					static std::vector<const char *> s_vpGpuNames;
					s_vGpuNames.clear();
					char aAutoGpuName[256];
					str_format(aAutoGpuName, sizeof(aAutoGpuName), "%s (%s)", Localize("auto"), GpuList.m_AutoGpu.m_aName);
					s_vGpuNames.emplace_back(aAutoGpuName);
					for(const auto &Gpu : GpuList.m_vGpus)
						s_vGpuNames.emplace_back(Gpu.m_aName);
					s_vpGpuNames.clear();
					for(const auto &Name : s_vGpuNames)
						s_vpGpuNames.push_back(Name.c_str());
					int Selected = 0;
					for(size_t i = 1; i < s_vGpuNames.size(); ++i)
						if(str_comp(g_Config.m_GfxGpuName, GpuList.m_vGpus[i - 1].m_aName) == 0)
							Selected = (int)i;
					DoGraphicsChoiceRow(Row, Localize("Graphics card"), "graphics-card-title", s_vpGpuNames.data(), s_vpGpuNames.size(), Selected, s_GpuDropDownState, s_GpuDropDownScrollRegion, g_Config.m_GfxGpuName, [this, &GpuList](int NewValue) {
						if(NewValue == 0)
							str_copy(g_Config.m_GfxGpuName, "auto");
						else
							str_copy(g_Config.m_GfxGpuName, GpuList.m_vGpus[NewValue - 1].m_aName);
						s_GfxGpuChanged = true;
						CheckSettings = true;
					});
				});
			}
			if(ResolveSettingsGpuControl(Graphics()->GetGpus().m_vGpus.size(), Graphics()->GetGpus().m_CanSelect, HasSystemGpuSettings).m_ShowSystemSettings)
			{
#if defined(CONF_FAMILY_WINDOWS)
				RowAction("graphics-system-gpu-settings", GraphicsMetrics.m_ButtonHeight, [&](CUIRect SystemSettingsRow) {
					static CButtonContainer s_SystemGraphicsSettings;

					if(DoSettingsButton_Menu(SETTINGS_GRAPHICS, -1, -1, &s_SystemGraphicsSettings, "graphics-system-gpu-settings", Localize("System graphics settings"), 0, &SystemSettingsRow) && !Ui()->RenderOnly())
					{
						// 打不开系统设置页只影响这一次点击，不是需要阅读详情的错误：
						// 用非阻断失败提示，用户再点一次就是重试。
						if(!os_open_link("ms-settings:display-advancedgraphics"))
							qm_ui::Toast(EQmToastKind::FAILED, Localize("Could not open system graphics settings."));
					}
				});

#endif
			}
		};
		AddCard(DisplaySpec, [RenderDisplay](CUIRect Content) { RenderDisplay(Content); });
		vCards.back().m_RenderMeasured = RenderDisplay;
		// 「卡片效果」：只留卡片自身的背景、边框与标题效果；圆角曲线 / 曲率 / 胶囊端头
		// 属于整块界面共通的形状，已并入「界面主题」，同一类形状不再分散在两卡里。
		AddCard(CardEffectsSpec, [this, GraphicsMetrics, BodySize](CUIRect ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			const auto RowAction = [&](const char *pId, float Height, auto &&Action) {
				CUIRect Row;
				if(Flow.Control(pId, Height, Row))
					Action(Row);
				Flow.Space(ui_token::settings::RowGap(GraphicsMetrics.m_LineSpacing));
			};
			SSettingsContentMetrics ColorMetrics = GraphicsMetrics;
			ColorMetrics.m_LineSpacing = 0.0f;

			RowAction("graphics-card-borders", GraphicsMetrics.m_LineHeight, [&](CUIRect Button) {
				if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiCardBorders, "show-settings-card-borders", Localize("Show settings card borders"), g_Config.m_QmUiCardBorders, &Button))
					g_Config.m_QmUiCardBorders ^= 1;
			});

			RowAction("graphics-card-surface", GraphicsMetrics.m_ButtonHeight, [&](CUIRect UiCardColorRow) {
				static CButtonContainer s_UiCardColorResetId;
				if(DoLine_AlphaColorPicker(&s_UiCardColorResetId, ColorMetrics, &UiCardColorRow, Localize("Settings card background"), &g_Config.m_QmUiCardColor, &g_Config.m_QmUiCardOpacity, DefaultConfig::QmUiCardColor, DefaultConfig::QmUiCardOpacity))
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
			});

			RowAction("graphics-card-border-color", GraphicsMetrics.m_ButtonHeight, [&](CUIRect CardBorderColorRow) {
				static CButtonContainer s_CardBorderColorResetId;
				const unsigned OldCardBorderColor = g_Config.m_QmUiCardBorderColor;
				DoLine_ColorPicker(&s_CardBorderColorResetId, ColorMetrics, &CardBorderColorRow, Localize("Settings card border color"), &g_Config.m_QmUiCardBorderColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmUiCardBorderColor, true)), false, nullptr, true, false);
				if(OldCardBorderColor != g_Config.m_QmUiCardBorderColor)
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
			});

			RowAction("graphics-rainbow-card-titles", GraphicsMetrics.m_LineHeight, [&](CUIRect Button) {
				if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiCardRainbowTitles, "rainbow-card-titles", Localize("Rainbow card titles"), g_Config.m_QmUiCardRainbowTitles, &Button))
					g_Config.m_QmUiCardRainbowTitles ^= 1;
			});
		});

		// 「界面主题」：整块界面共用的颜色与文字。模糊开关与模式也在这里——它们决定的是
		// 界面背后那一层玻璃，不属于卡片自己的外观。
		AddCard(ThemeSpec, [this, GraphicsMetrics, BodySize, DoGraphicsNumericField](CUIRect ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			const auto RowAction = [&](const char *pId, float Height, auto &&Action) {
				CUIRect Row;
				if(Flow.Control(pId, Height, Row))
					Action(Row);
				Flow.Space(ui_token::settings::RowGap(GraphicsMetrics.m_LineSpacing));
			};
			const bool TextCustomColorVisible = g_Config.m_QmUiTextColorMode == 3;
			SSettingsContentMetrics ColorMetrics = GraphicsMetrics;
			ColorMetrics.m_LineSpacing = 0.0f;

			// 圆角曲线四档：原圆弧是旧外观（同时关掉沿边生长与曲线），其余三档都是
			// 连续曲率，区别只在曲率沿弧长怎么分布。小控件受 25% 生长上限约束，
			// 实际会退化成圆弧，所以四档的差别只在卡片和面板这类大表面上看得出来。
			RowAction("graphics-corner-curve", GraphicsMetrics.m_LineHeight, [&](CUIRect CornerCurveRow) {
				static CButtonContainer s_aCornerCurveButtons[5];
				const char *apCornerCurveLabels[] = {
					Localize("Classic arc"),
					Localize("Superellipse"),
					Localize("Quintic Bezier"),
					Localize("Conic Bezier"),
					Localize("Apple"),
				};
				CUIRect CornerCurveLabel, CornerCurveSegments;
				SplitSettingsControlRow(CornerCurveRow, GraphicsMetrics, &CornerCurveLabel, &CornerCurveSegments, ESettingsControlWidth::LONG_CHOICE);
				Ui()->DoLabel(&CornerCurveLabel, Localize("Corner curve"), BodySize, TEXTALIGN_ML);
				// 五档选择是自绘控件，不经过 NumericField / DropDown 的配置绑定，
				// 这里给整行补登记配置说明：悬停标题或右侧控件都能看到提示。
				static const char s_CornerCurveHelpId[] = "graphics-corner-curve-help";
				GameClient()->m_Tooltips.DoSettingsToolTipForConfig(s_CornerCurveHelpId, &CornerCurveRow, &g_Config.m_QmUiCornerCurve, &CornerCurveLabel);
				const int CurrentCurve = g_Config.m_QmUiCornerCurve;
				const int PickedCurve = DoSegmentedChoice(s_aCornerCurveButtons, apCornerCurveLabels, 5, CurrentCurve, CornerCurveSegments);
				if(PickedCurve != CurrentCurve)
					g_Config.m_QmUiCornerCurve = PickedCurve;
			});

			// 圆角强度与胶囊端头：原圆弧档没有可调项，两行都不占位；
			// 数值不随切档清空，切回连续曲率档时沿用上一次的值。
			if(g_Config.m_QmUiCornerCurve != 0)
			{
				RowAction("graphics-corner-curvature", GraphicsMetrics.m_LineHeight, [&](CUIRect CornerCurvatureRow) {
					// 强度按档位分开记忆：每档一个配置，切档时各自回到自己的值。
					int *pSmoothing = QmCornerSmoothingForCurve(g_Config.m_QmUiCornerCurve);
					DoGraphicsNumericField("graphics-corner-curvature", pSmoothing, pSmoothing, CornerCurvatureRow, Localize("Corner curvature"), 0, 100, &CUi::ms_LinearScrollbarScale, "%", CUi::SCROLLBAR_OPTION_DELAYUPDATE);
				});

				// 胶囊端头两档：普通胶囊 = 正圆弧端头；丝滑 = 只把直边靠端帽的一段往外鼓一点点，
				// 端帽本身仍是精确正圆，所以不会出现端头被拉平、胶囊变方。
				// 只对「有一段直边」的胶囊生效，正圆（滑块圆点、单选按钮）不受影响。
				RowAction("graphics-capsule-style", GraphicsMetrics.m_LineHeight, [&](CUIRect CapsuleStyleRow) {
					static CButtonContainer s_aCapsuleStyleButtons[3];
					const char *apCapsuleStyleLabels[] = {
						Localize("Plain capsule"),
						Localize("Silky capsule"),
						Localize("Apple capsule"),
					};
					CUIRect CapsuleStyleLabel, CapsuleStyleSegments;
					SplitSettingsControlRow(CapsuleStyleRow, GraphicsMetrics, &CapsuleStyleLabel, &CapsuleStyleSegments, ESettingsControlWidth::LONG_CHOICE);
					Ui()->DoLabel(&CapsuleStyleLabel, Localize("Capsule ends"), BodySize, TEXTALIGN_ML);
					static const char s_CapsuleStyleHelpId[] = "graphics-capsule-style-help";
					GameClient()->m_Tooltips.DoSettingsToolTipForConfig(s_CapsuleStyleHelpId, &CapsuleStyleRow, &g_Config.m_QmUiCapsuleStyle, &CapsuleStyleLabel);
					const int CurrentStyle = g_Config.m_QmUiCapsuleStyle;
					const int PickedStyle = DoSegmentedChoice(s_aCapsuleStyleButtons, apCapsuleStyleLabels, 3, CurrentStyle, CapsuleStyleSegments);
					if(PickedStyle != CurrentStyle)
						g_Config.m_QmUiCapsuleStyle = PickedStyle;
				});

				// 苹果胶囊端部强度：只在这一档出现，独立于上方的「圆角曲率」。
				// 胶囊的端头是半圆，和卡片圆角不是同一类形状，共用一个滑杆两边都调不准。
				// 显隐走设置页统一的布局过渡，和「个性调整」里那些子项同一套动画。
				static SettingsDependentRows s_CapsuleStrengthRows;
				if(const auto CapsuleStrengthRows = s_CapsuleStrengthRows.Reveal(Ui(), Flow, g_Config.m_QmUiCapsuleStyle == 2, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
				{
					RowAction("graphics-capsule-strength", GraphicsMetrics.m_LineHeight, [&](CUIRect CapsuleStrengthRow) {
						DoGraphicsNumericField("graphics-capsule-strength", &g_Config.m_QmUiCapsuleAppleStrength, &g_Config.m_QmUiCapsuleAppleStrength, CapsuleStrengthRow, Localize("Capsule end strength"), 0, 100, &CUi::ms_LinearScrollbarScale, "%", CUi::SCROLLBAR_OPTION_DELAYUPDATE);
					});
				}
			}

			// 主控行与从属行必须共用一条行距链：主控行不再自带行尾间距，
			// 间距全部由「主控行 -> 从属行」的 SubRowGap 与「从属行 -> 下一行」的 RowGap 给出，
			// 否则 RowAction 的尾距叠加 SubRowGap 会出现双倍空隙（实测界面主题卡首屏）。
			CUIRect BlurToggleRow;
			if(Flow.Control("graphics-backdrop-blur", GraphicsMetrics.m_LineHeight, BlurToggleRow))
			{
				const bool BlurSupported = Graphics()->IsBackbufferCaptureSupported() && Graphics()->IsRenderTargetGaussianBlurSupported();
				char aBlurLabel[192];
				str_format(aBlurLabel, sizeof(aBlurLabel), "%s%s%s%s", Localize("Enable backdrop blur"), BlurSupported ? "" : " (", BlurSupported ? "" : Localize("Unavailable"), BlurSupported ? "" : ")");
				if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmGaussianBlur, "enable-backdrop-blur", aBlurLabel, g_Config.m_QmGaussianBlur, &BlurToggleRow))
					g_Config.m_QmGaussianBlur ^= 1;
			}

			// 「模糊模式」是「启用背景模糊」的从属行：缩进一档表明从属关系，关闭模糊时不出现，
			// 显隐走设置页统一的布局过渡，下面的颜色行跟着让位（与胶囊端部强度同一套机制）。
			// 模糊关掉时模式本身没有任何作用，留着只会让人以为它还在生效。
			static SettingsDependentRows s_BlurModeRows;
			if(const auto BlurModeRows = s_BlurModeRows.Reveal(Ui(), Flow, g_Config.m_QmGaussianBlur != 0, GameClient()->UiRuntimeV2()->FrameDt(), g_Config.m_QmUiMotionLevel))
			{
				CUIRect BlurModeRow;
				if(Flow.Control("graphics-blur-mode", GraphicsMetrics.m_LineHeight, BlurModeRow, ui_token::settings::SubRowGap(GraphicsMetrics.m_LineSpacing)))
				{
					const float Indent = ui_token::settings::row::SUB.m_Indent * GraphicsMetrics.m_UiScale;
					BlurModeRow.x += Indent;
					BlurModeRow.w = maximum(0.0f, BlurModeRow.w - Indent);
					const char *apBlurModeLabels[] = {Localize("Gaussian"), Localize("Kawase"), Localize("Dual Kawase")};
					const char *apBlurModeTooltips[] = {
						Localize("Smooth and accurate blur with the highest GPU cost."),
						Localize("Fast, lightweight blur with a softer approximation."),
						Localize("Multi-resolution blur with a stronger result and balanced GPU cost."),
					};
					CUIRect BlurModeLabel, BlurModeSegments;
					SplitSettingsControlRow(BlurModeRow, GraphicsMetrics, &BlurModeLabel, &BlurModeSegments, ESettingsControlWidth::LONG_CHOICE);
					Ui()->DoLabel(&BlurModeLabel, Localize("Blur mode"), BodySize, TEXTALIGN_ML);
					const int NewBlurMode = DoSegmentedChoice(s_aGraphicsBlurModeButtons, apBlurModeLabels, (int)std::size(apBlurModeLabels), g_Config.m_QmBlurMode, BlurModeSegments);
					if(NewBlurMode != g_Config.m_QmBlurMode)
						g_Config.m_QmBlurMode = NewBlurMode;
					// 整行 tooltip 跟随当前模式，保留原先逐段的说明文案。
					GameClient()->m_Tooltips.DoToolTip(&s_aGraphicsBlurModeButtons[0], &BlurModeRow, apBlurModeTooltips[std::clamp(g_Config.m_QmBlurMode, 0, (int)std::size(apBlurModeTooltips) - 1)]);
				}
			}
			// 从属行收起时也保留这一档行距：显隐只影响中间那半档，下一行不会跳。
			Flow.Space(ui_token::settings::RowGap(GraphicsMetrics.m_LineSpacing));

			// 聚焦光晕与背景模糊同属「界面效果」，两行挨着放。光晕颜色取强调色，
			// 这里只有开关，不额外给光晕开一条颜色配置（见 UI 视觉分组与自定义边界）。
			RowAction("graphics-focus-glow", GraphicsMetrics.m_LineHeight, [&](CUIRect Button) {
				if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiFocusGlow, "enable-focus-glow", Localize("Enable focus glow"), g_Config.m_QmUiFocusGlow, &Button))
					g_Config.m_QmUiFocusGlow ^= 1;
			});

			RowAction("graphics-surface", GraphicsMetrics.m_ButtonHeight, [&](CUIRect UiColorRow) {
				static CButtonContainer s_UiColorResetId;

				if(DoLine_AlphaColorPicker(&s_UiColorResetId, ColorMetrics, &UiColorRow, Localize("Interface surface"), &g_Config.m_QmUiColor, &g_Config.m_QmUiOpacity, DefaultConfig::QmUiColor, DefaultConfig::QmUiOpacity))
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
			});

			RowAction("graphics-accent", GraphicsMetrics.m_ButtonHeight, [&](CUIRect UiAccentColorRow) {
				static CButtonContainer s_UiAccentColorResetId;

				if(DoLine_AlphaColorPicker(&s_UiAccentColorResetId, ColorMetrics, &UiAccentColorRow, Localize("Interface accent color"), &g_Config.m_QmUiAccentColor, &g_Config.m_QmUiAccentOpacity, DefaultConfig::QmUiAccentColor, DefaultConfig::QmUiAccentOpacity))
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
			});

			// 控件底与弹层底各只留一行：按钮/输入框同属「控件」，展开列表/二级菜单同属「弹层」，
			// 分两行配置时用户得同时调两处才能改对一块区域，还容易调成两种底色。
			RowAction("graphics-control-surface", GraphicsMetrics.m_ButtonHeight, [&](CUIRect ControlColorRow) {
				static CButtonContainer s_ControlColorResetId;

				if(DoLine_AlphaColorPicker(&s_ControlColorResetId, ColorMetrics, &ControlColorRow, Localize("Control background color"), &g_Config.m_QmUiControlColor, &g_Config.m_QmUiControlOpacity, DefaultConfig::QmUiControlColor, DefaultConfig::QmUiControlOpacity))
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
			});

			RowAction("graphics-layer-surface", GraphicsMetrics.m_ButtonHeight, [&](CUIRect LayerColorRow) {
				static CButtonContainer s_LayerColorResetId;

				if(DoLine_AlphaColorPicker(&s_LayerColorResetId, ColorMetrics, &LayerColorRow, Localize("Popup background color"), &g_Config.m_QmUiLayerColor, &g_Config.m_QmUiLayerOpacity, DefaultConfig::QmUiLayerColor, DefaultConfig::QmUiLayerOpacity))
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
			});

			// 文本颜色模式不再有「自动」档：模式就是四种明确颜色，用胶囊分段控件直接摆在
			// 行里，选哪个一眼可见；自动对比会在浅色胶囊/深色卡片之间翻转，且用户无法从
			// 设置里判断某块文字最终是什么颜色。
			RowAction("graphics-text-mode", GraphicsMetrics.m_LineHeight, [&](CUIRect TextModeRow) {
				const char *apTextModes[] = {Localize("White"), Localize("Black"), Localize("Rainbow"), Localize("Custom")};
				const SSettingsRadioRowLayout TextModeLayout = ResolveSettingsRadioRowLayout(TextModeRow, (int)std::size(apTextModes), GraphicsMetrics);
				Ui()->DoLabel(&TextModeLayout.m_LabelRect, Localize("Text color mode"), GraphicsMetrics.m_BodySize, TEXTALIGN_ML);
				const int TextMode = DoSegmentedChoice(s_aGraphicsTextColorModeButtons, apTextModes, (int)std::size(apTextModes), g_Config.m_QmUiTextColorMode, TextModeLayout.m_ButtonsRect);
				if(TextMode != g_Config.m_QmUiTextColorMode)
				{
					g_Config.m_QmUiTextColorMode = TextMode;
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
				}
			});
			CUIRect TextCustomColorRow;
			if(TextCustomColorVisible && Flow.Control("graphics-custom-text-color", GraphicsMetrics.m_ButtonHeight, TextCustomColorRow))
			{
				static CButtonContainer s_TextCustomColorResetId;
				const unsigned PreviousTextColor = g_Config.m_QmUiTextCustomColor;
				DoLine_ColorPicker(&s_TextCustomColorResetId, ColorMetrics, &TextCustomColorRow, Localize("Custom text color"), &g_Config.m_QmUiTextCustomColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmUiTextCustomColor)), false, nullptr, false);
				if(PreviousTextColor != g_Config.m_QmUiTextCustomColor)
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
			}
			if(TextCustomColorVisible)
				Flow.Space(ui_token::settings::RowGap(GraphicsMetrics.m_LineSpacing));
		});
		AddCard(IconsSpec, [this, GraphicsMetrics, BodySize](CUIRect ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			const auto RowAction = [&](const char *pId, float Height, auto &&Action) {
				CUIRect Row;
				if(Flow.Control(pId, Height, Row))
					Action(Row);
				Flow.Space(ui_token::settings::RowGap(GraphicsMetrics.m_LineSpacing));
			};
			const bool CustomColor = qm_icon_settings::CustomColorEnabled(g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled);
			const auto DoIconChoiceRow = [this, GraphicsMetrics, BodySize](CUIRect Row, const char *pLabel, const char *const *ppLabels, int Count, int Current, CButtonContainer *pButtons, auto &&OnChanged) {
				const SSettingsRadioRowLayout Layout = ResolveSettingsRadioRowLayout(Row, Count, GraphicsMetrics);
				CUIRect Label = Layout.m_LabelRect;
				CUIRect Segments = Layout.m_ButtonsRect;
				Ui()->DoLabel(&Label, pLabel, BodySize, TEXTALIGN_ML);
				const int ClickedSegment = DoSegmentedChoice(pButtons, ppLabels, Count, Current, Segments);
				if(ClickedSegment != Current)
					OnChanged(ClickedSegment);
			};
			const char *apIconColorLabels[] = {Localize("White"), Localize("Black"), Localize("Rainbow")};
			// Thin 未随包字体，不再提供该样式；weight 2 配置值仍兼容（渲染为 Light）。
			const char *apIconWeightLabels[] = {Localize("Light"), Localize("Regular"), Localize("Bold"), Localize("Fill")};
			const int IconWeightIndex = QmIconWeightSegmentIndex(g_Config.m_QmUiIconWeight);
			RowAction("graphics-icon-color", GraphicsMetrics.m_LineHeight, [&](CUIRect Row) {
				DoIconChoiceRow(Row, Localize("UI icon color"), apIconColorLabels, std::size(apIconColorLabels), qm_icon_settings::PresetIndex(g_Config.m_QmUiIconColor), s_aGraphicsIconColorButtons, [this](int NewValue) {
					qm_icon_settings::SelectPreset(NewValue, g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled);
					Client()->OnWindowResize();
				});
			});

			RowAction("graphics-icon-custom-toggle", GraphicsMetrics.m_LineHeight, [&](CUIRect CustomToggleRow) {
				if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiIconCustomColorEnabled, "graphics-custom-icon-color", Localize("Use custom UI icon color"), CustomColor, &CustomToggleRow))
				{
					qm_icon_settings::ToggleCustomColor(g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled);
					Client()->OnWindowResize();
				}
			});
			float SectionReveal = 0.0f;
			if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, ContentRect, "deck:graphics-icons", GraphicsMetrics, &SectionReveal))
				return;
			const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

			if(CustomColor)
			{
				SSettingsContentMetrics ColorMetrics = GraphicsMetrics;
				ColorMetrics.m_LineSpacing = 0.0f;
				RowAction("graphics-icon-custom-color", GraphicsMetrics.m_ButtonHeight, [&](CUIRect CustomColorRow) {
					DoLine_ColorPicker(&s_GraphicsIconCustomColorResetId, ColorMetrics, &CustomColorRow, Localize("UI icon custom color"), &g_Config.m_QmUiIconCustomColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmUiIconCustomColor)), false, nullptr, false, false);
				});
			}
			SSettingsContentMetrics SemanticColorMetrics = GraphicsMetrics;
			SemanticColorMetrics.m_LineSpacing = 0.0f;

			RowAction("graphics-friend-icon-color", GraphicsMetrics.m_ButtonHeight, [&](CUIRect FriendColorRow) {
				DoLine_ColorPicker(&s_GraphicsFriendIconColorResetId, SemanticColorMetrics, &FriendColorRow, Localize("Friend icon color"), &g_Config.m_QmUiFriendIconColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmUiFriendIconColor)), false, nullptr, false, false);
			});

			RowAction("graphics-friend-icon-filled", GraphicsMetrics.m_LineHeight, [&](CUIRect FriendFilledRow) {
				if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiFriendIconFilled, "graphics-solid-friend-icon", Localize("Solid friend icon"), g_Config.m_QmUiFriendIconFilled, &FriendFilledRow))
					g_Config.m_QmUiFriendIconFilled ^= 1;
			});

			RowAction("graphics-favorite-icon-color", GraphicsMetrics.m_ButtonHeight, [&](CUIRect FavoriteColorRow) {
				DoLine_ColorPicker(&s_GraphicsFavoriteIconColorResetId, SemanticColorMetrics, &FavoriteColorRow, Localize("Favorite icon color"), &g_Config.m_QmUiFavoriteIconColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmUiFavoriteIconColor)), false, nullptr, false, false);
			});

			RowAction("graphics-favorite-icon-filled", GraphicsMetrics.m_LineHeight, [&](CUIRect FavoriteFilledRow) {
				if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiFavoriteIconFilled, "graphics-solid-favorite-icon", Localize("Solid favorite icon"), g_Config.m_QmUiFavoriteIconFilled, &FavoriteFilledRow))
					g_Config.m_QmUiFavoriteIconFilled ^= 1;
			});

			// 灵动岛连接质量图标独立颜色：档位语义色 / 跟随全局图标色 / 自定义色。
			RowAction("graphics-signal-icon-color-mode", GraphicsMetrics.m_LineHeight, [&](CUIRect Row) {
				const char *apSignalColorModeLabels[] = {Localize("Quality tier"), Localize("Follow UI icon color"), Localize("Custom")};
				DoIconChoiceRow(Row, Localize("Connection icon color"), apSignalColorModeLabels, std::size(apSignalColorModeLabels), QmSignalIconColorMode() - 1, s_aGraphicsSignalColorModeButtons, [](int NewValue) {
					g_Config.m_QmHudIslandSignalColorMode = NewValue + 1;
				});
			});
			if(QmSignalIconColorMode() == QM_SIGNAL_COLOR_CUSTOM)
			{
				RowAction("graphics-signal-icon-color", GraphicsMetrics.m_ButtonHeight, [&](CUIRect SignalColorRow) {
					DoLine_ColorPicker(&s_GraphicsSignalIconColorResetId, SemanticColorMetrics, &SignalColorRow, Localize("Connection icon custom color"), &g_Config.m_QmHudIslandSignalColor, color_cast<ColorRGBA>(ColorHSLA(CConfig::ms_QmHudIslandSignalColor)), false, nullptr, false, false);
				});
			}
			RowAction("graphics-icon-weight", GraphicsMetrics.m_LineHeight, [&](CUIRect Row) {
				DoIconChoiceRow(Row, Localize("UI icon style"), apIconWeightLabels, std::size(apIconWeightLabels), IconWeightIndex, s_aGraphicsIconWeightButtons, [this](int NewValue) {
					const int NewWeight = s_aIconWeightValues[NewValue];
					if(NewWeight == NormalizeQmIconWeight(g_Config.m_QmUiIconWeight))
						return;
					g_Config.m_QmUiIconWeight = NewWeight;
					GameClient()->SyncQmUiIconWeight();
				});
			});
			// 密码显隐图标的切换动画：淡化沿用旧的双字形交叉，形变走成对烘焙帧条。
			RowAction("graphics-password-icon-animation", GraphicsMetrics.m_LineHeight, [&](CUIRect Row) {
				const char *apPasswordIconLabels[] = {Localize("Fade"), Localize("Morph")};
				DoIconChoiceRow(Row, Localize("Password icon animation"), apPasswordIconLabels, std::size(apPasswordIconLabels), qm_password_icon::ResolveStyle(g_Config.m_QmUiPasswordIconStyle), s_aGraphicsPasswordIconButtons, [](int NewValue) {
					g_Config.m_QmUiPasswordIconStyle = NewValue;
				});
			});
		});
		AddCard(InteractionSpec, [this, GraphicsMetrics, BodySize, DoGraphicsNumericField](CUIRect ContentRect) {
			CSettingsContentLayoutFlow Flow(ContentRect);
			const auto RowAction = [&](const char *pId, float Height, auto &&Action) {
				CUIRect Row;
				if(Flow.Control(pId, Height, Row))
					Action(Row);
				Flow.Space(ui_token::settings::RowGap(GraphicsMetrics.m_LineSpacing));
			};

			RowAction("graphics-motion", GraphicsMetrics.m_LineHeight, [&](CUIRect MotionRow) {
				CUIRect Label, Segments;
				SplitSettingsControlRow(MotionRow, GraphicsMetrics, &Label, &Segments, ESettingsControlWidth::LONG_CHOICE);
				DoSettingsLabel(SETTINGS_GRAPHICS, -1, "graphics-ui-motion-level-label", &Label, Localize("UI motion level"), BodySize, TEXTALIGN_ML);
				static CButtonContainer s_aMotionButtons[3];
				const char *apMotionLabels[] = {Localize("Off"), Localize("Reduced"), Localize("Full")};
				const int NewMotionLevel = DoSegmentedChoice(s_aMotionButtons, apMotionLabels, 3, g_Config.m_QmUiMotionLevel, Segments);
				if(NewMotionLevel != g_Config.m_QmUiMotionLevel)
					g_Config.m_QmUiMotionLevel = NewMotionLevel;
				const char *pMotionDescription = nullptr;
				if(g_Config.m_QmUiMotionLevel == 0)
					pMotionDescription = Localize("Off: disables every interface animation");
				else if(g_Config.m_QmUiMotionLevel == 1)
					pMotionDescription = Localize("Reduced: uses shorter transitions everywhere");
				else
					pMotionDescription = Localize("Full: uses the complete transition everywhere");

				char aMotionTip[256];
				str_format(aMotionTip, sizeof(aMotionTip), "%s\n%s", Localize("This is the master switch for all interface animations"), pMotionDescription);
				GameClient()->m_Tooltips.DoToolTip(&s_aMotionButtons[0], &MotionRow, aMotionTip);
			});

			// 菜单按钮悬浮反馈：主菜单那排按钮的悬浮表现二选一。
			// 缩放会让整块按钮连同贴图一起胀缩，描边只点亮边框，几何不动。
			RowAction("graphics-menu-hover", GraphicsMetrics.m_LineHeight, [&](CUIRect HoverRow) {
				CUIRect Label, Segments;
				SplitSettingsControlRow(HoverRow, GraphicsMetrics, &Label, &Segments, ESettingsControlWidth::LONG_CHOICE);
				DoSettingsLabel(SETTINGS_GRAPHICS, -1, "graphics-menu-hover-label", &Label, Localize("Menu button hover"), BodySize, TEXTALIGN_ML);
				static CButtonContainer s_aHoverStyleButtons[2];
				const char *apHoverLabels[] = {Localize("Shrink"), Localize("Outline")};
				const int CurrentHover = std::clamp(g_Config.m_QmUiHoverStyle, 0, 1);
				const int NewHover = DoSegmentedChoice(s_aHoverStyleButtons, apHoverLabels, 2, CurrentHover, Segments);
				if(NewHover != CurrentHover)
					g_Config.m_QmUiHoverStyle = NewHover;
			});

			// 输入光标样式：设置页输入框、控制台、聊天输入共用同一个光标组件（qm_text_caret），
			// 这里只切形态，不改任何一处的绘制路径。位置紧跟动效强度——两者都是「整块界面
			// 的通用观感」，不属于某张卡的专属参数，所以留在个性调整之外常显。
			RowAction("graphics-text-caret-style", GraphicsMetrics.m_LineHeight, [&](CUIRect CaretStyleRow) {
				CUIRect Label, Segments;
				SplitSettingsControlRow(CaretStyleRow, GraphicsMetrics, &Label, &Segments, ESettingsControlWidth::LONG_CHOICE);
				DoSettingsLabel(SETTINGS_GRAPHICS, -1, "graphics-text-caret-style-label", &Label, Localize("Text caret style"), BodySize, TEXTALIGN_ML);
				static CButtonContainer s_aCaretStyleButtons[3];
				const char *apCaretStyleLabels[] = {Localize("Bar"), Localize("Underscore"), Localize("Block")};
				const int CaretStyleCount = (int)std::size(apCaretStyleLabels);
				// 越界值收敛到最近档：配置可能来自旧 cfg 或被外部写入，分段控件不能拿到非法下标。
				const int CurrentCaretStyle = std::clamp(g_Config.m_QmTextCaretStyle, qm_text_caret::STYLE_MIN, qm_text_caret::STYLE_MAX);
				const int NewCaretStyle = DoSegmentedChoice(s_aCaretStyleButtons, apCaretStyleLabels, CaretStyleCount, CurrentCaretStyle, Segments);
				if(NewCaretStyle != CurrentCaretStyle)
					g_Config.m_QmTextCaretStyle = NewCaretStyle;
				const char *apCaretStyleTips[] = {
					Localize("Bar: a thin vertical line between characters"),
					Localize("Underscore: a line under the character that follows"),
					Localize("Block: fills the character cell behind the next character"),
				};
				GameClient()->m_Tooltips.DoToolTip(&s_aCaretStyleButtons[0], &CaretStyleRow, apCaretStyleTips[std::clamp(NewCaretStyle, 0, CaretStyleCount - 1)]);
			});

			// ---- 面板入场动效 ----
			// 两处都是「形态偏好」，不是第二套动效开关：全局总闸仍是上面的 UI motion level。
			// ESC 菜单壳层从顶部滑入/滑离的弹簧手感（控制台 qm_menu_slide_feel 同源）。
			RowAction("graphics-esc-slide-feel", GraphicsMetrics.m_LineHeight, [&](CUIRect SlideRow) {
				CUIRect Label, Segments;
				SplitSettingsControlRow(SlideRow, GraphicsMetrics, &Label, &Segments, ESettingsControlWidth::LONG_CHOICE);
				DoSettingsLabel(SETTINGS_GRAPHICS, -1, "graphics-esc-slide-feel-label", &Label, Localize("ESC menu slide"), BodySize, TEXTALIGN_ML);
				static CButtonContainer s_aEscSlideButtons[3];
				const char *apFeelLabels[] = {Localize("Soft"), Localize("Standard"), Localize("Bouncy")};
				const int CurrentFeel = std::clamp(g_Config.m_QmMenuSlideFeel, 0, 2);
				const int NewFeel = DoSegmentedChoice(s_aEscSlideButtons, apFeelLabels, 3, CurrentFeel, Segments);
				if(NewFeel != CurrentFeel)
					g_Config.m_QmMenuSlideFeel = NewFeel;
				GameClient()->m_Tooltips.DoToolTip(&s_aEscSlideButtons[0], &SlideRow, Localize("Elastic feel of the ESC menu sliding in from the top"));
			});

			// 控制台从顶部下拉展开、原路收回，同一条弹簧轨道支持中途折返。
			RowAction("graphics-console-slide-feel", GraphicsMetrics.m_LineHeight, [&](CUIRect ConsoleRow) {
				CUIRect Label, Segments;
				SplitSettingsControlRow(ConsoleRow, GraphicsMetrics, &Label, &Segments, ESettingsControlWidth::LONG_CHOICE);
				DoSettingsLabel(SETTINGS_GRAPHICS, -1, "graphics-console-slide-feel-label", &Label, Localize("Console slide"), BodySize, TEXTALIGN_ML);
				static CButtonContainer s_aConsoleSlideButtons[3];
				const char *apFeelLabels[] = {Localize("Soft"), Localize("Standard"), Localize("Bouncy")};
				const int CurrentFeel = std::clamp(g_Config.m_QmConsoleSlideFeel, 0, 2);
				const int NewFeel = DoSegmentedChoice(s_aConsoleSlideButtons, apFeelLabels, 3, CurrentFeel, Segments);
				if(NewFeel != CurrentFeel)
					g_Config.m_QmConsoleSlideFeel = NewFeel;
			});

			// 计分板面板：从略小尺寸弹到过冲峰值再回落到 100%。
			RowAction("graphics-scoreboard-pop-feel", GraphicsMetrics.m_LineHeight, [&](CUIRect PopRow) {
				CUIRect Label, Segments;
				SplitSettingsControlRow(PopRow, GraphicsMetrics, &Label, &Segments, ESettingsControlWidth::LONG_CHOICE);
				DoSettingsLabel(SETTINGS_GRAPHICS, -1, "graphics-scoreboard-pop-feel-label", &Label, Localize("Scoreboard entrance"), BodySize, TEXTALIGN_ML);
				static CButtonContainer s_aScoreboardPopButtons[3];
				const char *apFeelLabels[] = {Localize("Soft"), Localize("Standard"), Localize("Bouncy")};
				const int CurrentFeel = std::clamp(g_Config.m_QmScoreboardPopFeel, 0, 2);
				const int NewFeel = DoSegmentedChoice(s_aScoreboardPopButtons, apFeelLabels, 3, CurrentFeel, Segments);
				if(NewFeel != CurrentFeel)
					g_Config.m_QmScoreboardPopFeel = NewFeel;
			});

			// 过冲百分比直接决定阻尼比：10% 就是先到 110% 再回落到 100%，0% 则无回弹。
			RowAction("graphics-scoreboard-overshoot", GraphicsMetrics.m_LineHeight, [&](CUIRect OvershootRow) {
				DoGraphicsNumericField("graphics-scoreboard-overshoot", &g_Config.m_QmScoreboardOvershoot, &g_Config.m_QmScoreboardOvershoot,
					OvershootRow, Localize("Entrance overshoot"), 0, 15, &CUi::ms_LinearScrollbarScale, "%", CUi::SCROLLBAR_OPTION_DELAYUPDATE);
			});

			// 内部文本依次出场：先标题、再逐行玩家、最后页脚区块；只调透明度，不改命中矩形。
			RowAction("graphics-scoreboard-text-entry", GraphicsMetrics.m_LineHeight, [&](CUIRect TextEntryRow) {
				CUIRect Label, Segments;
				SplitSettingsControlRow(TextEntryRow, GraphicsMetrics, &Label, &Segments, ESettingsControlWidth::LONG_CHOICE);
				DoSettingsLabel(SETTINGS_GRAPHICS, -1, "graphics-scoreboard-text-entry-label", &Label, Localize("Scoreboard text entry"), BodySize, TEXTALIGN_ML);
				static CButtonContainer s_aTextEntryButtons[3];
				const char *apTextEntryLabels[] = {Localize("Together"), Localize("By row"), Localize("Cascade")};
				const int CurrentEntry = std::clamp(g_Config.m_QmScoreboardTextEntry, 0, 2);
				const int NewEntry = DoSegmentedChoice(s_aTextEntryButtons, apTextEntryLabels, 3, CurrentEntry, Segments);
				if(NewEntry != CurrentEntry)
					g_Config.m_QmScoreboardTextEntry = NewEntry;
			});

			float SectionReveal = 0.0f;
			if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, ContentRect, "deck:graphics-interaction", GraphicsMetrics, &SectionReveal))
				return;
			const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

			RowAction("graphics-ScrollFeel", GraphicsMetrics.m_LineHeight, [&](CUIRect ScrollFeelRow) {
				CUIRect Label, Segments;
				SplitSettingsControlRow(ScrollFeelRow, GraphicsMetrics, &Label, &Segments, ESettingsControlWidth::LONG_CHOICE);
				DoSettingsLabel(SETTINGS_GRAPHICS, -1, "graphics-scroll-feel-label", &Label, Localize("Scroll feel"), BodySize, TEXTALIGN_ML);
				static CButtonContainer s_aScrollFeelButtons[3];
				// 展示顺序（原版 → 顺滑 → 弹簧）由这里显式映射，不依赖枚举值大小；
				// 枚举把原版钉在 0（零初始化语义），生产默认档由配置默认值决定。
				static constexpr EQmScrollFeel s_aScrollFeelOrder[] = {EQmScrollFeel::ORIGINAL, EQmScrollFeel::SMOOTH, EQmScrollFeel::SPRING};
				const char *apScrollFeelLabels[] = {Localize("Original"), Localize("Smooth"), Localize("Spring")};
				int CurrentScrollFeel = 1;
				for(int i = 0; i < 3; ++i)
					if(s_aScrollFeelOrder[i] == static_cast<EQmScrollFeel>(g_Config.m_QmUiScrollFeel))
						CurrentScrollFeel = i;
				const int NewScrollFeel = DoSegmentedChoice(s_aScrollFeelButtons, apScrollFeelLabels, 3, CurrentScrollFeel, Segments);
				if(NewScrollFeel != CurrentScrollFeel)
					g_Config.m_QmUiScrollFeel = static_cast<int>(s_aScrollFeelOrder[NewScrollFeel]);
				// 整行 tooltip 跟随当前档位：三档的差别只在"怎么走向目标"，
				// 所以文案直接说明这一点，而不是重复档位名。
				const char *apScrollFeelTooltips[] = {
					Localize("Every wheel tick restarts from rest, matching the original client"),
					Localize("Consecutive wheel ticks carry their velocity into one continuous glide"),
					Localize("A damped spring drives the offset; reversing slows it before it turns"),
				};
				GameClient()->m_Tooltips.DoToolTip(&s_aScrollFeelButtons[0], &ScrollFeelRow, apScrollFeelTooltips[std::clamp(NewScrollFeel, 0, (int)std::size(apScrollFeelTooltips) - 1)]);
			});

			// 弹簧档的三个参数只在选中弹簧时出现：另外两档是固定时长补间，不读它们，
			// 常驻会让"改了没反应"变成新的困惑源。
			if(static_cast<EQmScrollFeel>(g_Config.m_QmUiScrollFeel) == EQmScrollFeel::SPRING)
			{
				RowAction("graphics-ScrollSpringResponse", GraphicsMetrics.m_LineHeight, [&](CUIRect SpringResponseRow) {
					DoGraphicsNumericField("graphics-scroll-spring-response", &g_Config.m_QmUiScrollSpringResponse, &g_Config.m_QmUiScrollSpringResponse, SpringResponseRow, Localize("Spring response"), 80, 600, &CUi::ms_LinearScrollbarScale, "ms");
					GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmUiScrollSpringResponse, &SpringResponseRow, Localize("Spring settle time; lower is snappier, higher feels heavier"));
				});

				RowAction("graphics-ScrollSpringDamping", GraphicsMetrics.m_LineHeight, [&](CUIRect SpringDampingRow) {
					DoGraphicsNumericField("graphics-scroll-spring-damping", &g_Config.m_QmUiScrollSpringDamping, &g_Config.m_QmUiScrollSpringDamping, SpringDampingRow, Localize("Spring damping"), 50, 100, &CUi::ms_LinearScrollbarScale, "%");
					GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmUiScrollSpringDamping, &SpringDampingRow, Localize("Spring firmness; 100 stops without overshoot, lower bounces past"));
				});

				RowAction("graphics-ScrollBounce", GraphicsMetrics.m_LineHeight, [&](CUIRect ScrollBounceRow) {
					DoGraphicsNumericField("graphics-scroll-bounce", &g_Config.m_QmUiScrollBounce, &g_Config.m_QmUiScrollBounce, ScrollBounceRow, Localize("End bounce"), 0, 64);
					GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmUiScrollBounce, &ScrollBounceRow, Localize("How far a wheel or drag can push past an end, in UI pixels; 0 disables"));
				});
			}

			RowAction("graphics-ScrollStep", GraphicsMetrics.m_LineHeight, [&](CUIRect ScrollStepRow) {
				DoGraphicsNumericField("graphics-scroll-step", &g_Config.m_QmUiScrollStep, &g_Config.m_QmUiScrollStep, ScrollStepRow, Localize("Scroll step"), 2, 100);
				GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmUiScrollStep, &ScrollStepRow, Localize("Wheel distance per tick; hold Alt for 3x"));
			});
		});
	};
	uint64_t GraphicsLayoutRevision = GraphicsModesMeasureRevision;
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(GraphicsDisplayRowCount);
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(GraphicsBackendRowCount);
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(OldWindowMode);
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(RenderOnly ? 1 : 0);
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ GraphicsVisualMeasureRevision;
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ GraphicsDisplayMeasureRevision;
	// 手感档位决定弹簧参数两行是否参与布局，属于"影响行集合"的状态，必须参与
	// 高度测量缓存键；否则切到弹簧档后行已渲染、卡片高度却还是旧的，内容被裁。
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(std::clamp(g_Config.m_QmUiScrollFeel, 0, 2));
	// 同理：原圆弧档不显示「圆角强度」与「胶囊端头」两行，切档会改变行集合。
	// 四档选择是自绘控件、不走配置绑定，没有别的失效路径，漏了这一项就会在
	// 从原圆弧档切回来时按旧高度裁剪，或切到原圆弧档时留下两条空行。
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(std::clamp(g_Config.m_QmUiCornerCurve, 0, 4));
	// 胶囊端头现在是固定两项、不随强度增减行，所以强度只影响重测次数，不影响行集合；
	// 这里仍取「是否为零」，避免拖动滑杆时每帧换缓存键。
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(*QmCornerSmoothingForCurve(g_Config.m_QmUiCornerCurve) > 0 ? 1 : 0);
	// 苹果胶囊档会多出一行端部强度，切档必须让高度缓存失效。
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(std::clamp(g_Config.m_QmUiCapsuleStyle, 0, 2));
	if(pCards != nullptr)
	{
		BuildDefinitions(*pCards);
		const auto UpdateRestartStatus = [this]() {
			if(!Ui()->RenderOnly() && CheckSettings)
			{
				m_NeedRestartGraphics = !(s_GfxFsaaSamples == g_Config.m_GfxFsaaSamples && !s_GfxBackendChanged && !s_GfxGpuChanged);
				CheckSettings = false;
			}
		};
		for(auto &Card : *pCards)
		{
			if(Card.m_Render)
				Card.m_Render = [Render = std::move(Card.m_Render), UpdateRestartStatus](CUIRect Content) {
					Render(Content);
					UpdateRestartStatus();
				};
			if(Card.m_RenderMeasured)
				Card.m_RenderMeasured = [Render = std::move(Card.m_RenderMeasured), UpdateRestartStatus](CUIRect &Content) {
					Render(Content);
					UpdateRestartStatus();
				};
			qm_card_catalog::UseDeclaredModuleContent(Ctx, Card);
		}
	}
	if(pCards == nullptr)
		dbg_msg("perf/page", "graphics_prepare total=%.1fms", QmGfxNowMs() - QmGfxEntryMs);
	return GraphicsLayoutRevision;
}
