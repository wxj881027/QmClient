/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/log.h>
#include <base/math.h>
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
#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsIconOptions.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
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

namespace
{
	void FormatQmGraphicsBackendDisplayName(char *pBuf, int BufSize, const char *pBackendName, int Major, int Minor, int Patch, bool IsDefault)
	{
		const char *pSafeBackendName = pBackendName != nullptr ? pBackendName : "";
		char aBackendDisplayName[128];
		if(str_comp_nocase(pSafeBackendName, "OpenGL") == 0)
		{
			if(Major == 0)
				str_format(aBackendDisplayName, sizeof(aBackendDisplayName), "OpenGL (%s)", Localize("auto"));
			else
				str_format(aBackendDisplayName, sizeof(aBackendDisplayName), "OpenGL %d.%d", Major, Minor);
		}
		else if(str_comp_nocase(pSafeBackendName, "Vulkan") == 0)
			str_copy(aBackendDisplayName, "Vulkan");
		else if(str_comp_nocase(pSafeBackendName, "GLES") == 0)
		{
			if(Major == 0)
				str_format(aBackendDisplayName, sizeof(aBackendDisplayName), "GLES (%s)", Localize("auto"));
			else
				str_format(aBackendDisplayName, sizeof(aBackendDisplayName), "GLES %d.%d", Major, Minor);
		}
		else if(str_comp_nocase(pSafeBackendName, "Metal") == 0)
		{
			str_copy(aBackendDisplayName, "Metal");
		}
		else
		{
			str_format(aBackendDisplayName, sizeof(aBackendDisplayName), "%s (%d.%d.%d)", pSafeBackendName, Major, Minor, Patch);
		}

		str_format(pBuf, BufSize, "%s%s%s", aBackendDisplayName, IsDefault ? " - " : "", IsDefault ? Localize("default") : "");
	}
}

uint64_t CMenus::BuildGraphicsSettingsCards(const qm_card_catalog::SQmCardBuildContext &Ctx, std::vector<SSettingsCardDefinition> *pCards)
{
	const SSettingsPageLayoutFrame GraphicsPage = Ctx.m_Page;
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
	struct SMenuBackendInfo
	{
		EBackendType m_BackendType = BACKEND_TYPE_AUTO;
		int m_Major = 0;
		int m_Minor = 0;
		int m_Patch = 0;
		const char *m_pBackendName = "";
		bool m_Found = false;
	};
	static std::vector<SMenuBackendInfo> s_vSupportedBackendInfos;
	static std::vector<std::string> s_vSupportedBackendNames;
	static bool s_BackendListCacheValid = false;
	static int s_BackendListCacheDriverBlocked = -1;
	static char s_aBackendListCacheLanguage[sizeof(g_Config.m_ClLanguagefile)] = {};
	if(!s_BackendListCacheValid ||
		s_BackendListCacheDriverBlocked != g_Config.m_GfxDriverIsBlocked ||
		str_comp(s_aBackendListCacheLanguage, g_Config.m_ClLanguagefile) != 0)
	{
		s_vSupportedBackendInfos.clear();
		s_vSupportedBackendNames.clear();
		for(uint32_t i = 0; i < BACKEND_TYPE_COUNT; ++i)
		{
			if(EBackendType(i) == BACKEND_TYPE_AUTO)
				continue;
			const size_t BackendStartIndex = s_vSupportedBackendInfos.size();
			for(uint32_t n = 0; n < GRAPHICS_DRIVER_AGE_TYPE_COUNT; ++n)
			{
				SMenuBackendInfo Info;
				Info.m_BackendType = EBackendType(i);
				if(Graphics()->GetDriverVersion(EGraphicsDriverAgeType(n), Info.m_Major, Info.m_Minor, Info.m_Patch, Info.m_pBackendName, EBackendType(i)))
				{
					// 被屏蔽的 OpenGL 驱动仅保留 legacy 选项。
					if(EBackendType(i) != BACKEND_TYPE_OPENGL || EGraphicsDriverAgeType(n) == GRAPHICS_DRIVER_AGE_TYPE_LEGACY || g_Config.m_GfxDriverIsBlocked == 0)
					{
						Info.m_Found = true;
						char aTmpBackendName[256];
						const bool IsDefault = str_comp_nocase(Info.m_pBackendName, DefaultConfig::GfxBackend) == 0 && Info.m_Major == DefaultConfig::GfxGLMajor && Info.m_Minor == DefaultConfig::GfxGLMinor && Info.m_Patch == DefaultConfig::GfxGLPatch;
						FormatQmGraphicsBackendDisplayName(aTmpBackendName, sizeof(aTmpBackendName), Info.m_pBackendName, Info.m_Major, Info.m_Minor, Info.m_Patch, IsDefault);
						s_vSupportedBackendInfos.push_back(Info);
						s_vSupportedBackendNames.emplace_back(aTmpBackendName);
					}
				}
			}
			const bool SupportsAutomaticVersion =
				(EBackendType(i) == BACKEND_TYPE_OPENGL && g_Config.m_GfxDriverIsBlocked == 0) ||
				EBackendType(i) == BACKEND_TYPE_OPENGL_ES;
			if(SupportsAutomaticVersion && s_vSupportedBackendInfos.size() > BackendStartIndex)
			{
				SMenuBackendInfo AutoInfo;
				AutoInfo.m_BackendType = EBackendType(i);
				AutoInfo.m_pBackendName = EBackendType(i) == BACKEND_TYPE_OPENGL ? "OpenGL" : "GLES";
				AutoInfo.m_Found = true;
				char aAutoBackendName[256];
				const bool IsAutoDefault = str_comp_nocase(AutoInfo.m_pBackendName, DefaultConfig::GfxBackend) == 0 && DefaultConfig::GfxGLMajor == 0;
				FormatQmGraphicsBackendDisplayName(aAutoBackendName, sizeof(aAutoBackendName), AutoInfo.m_pBackendName, 0, 0, 0, IsAutoDefault);
				s_vSupportedBackendInfos.insert(s_vSupportedBackendInfos.begin() + BackendStartIndex, AutoInfo);
				s_vSupportedBackendNames.insert(s_vSupportedBackendNames.begin() + BackendStartIndex, aAutoBackendName);

				const SOpenGLVersion PreferredVersion = AutoOpenGLProbeVersion(EBackendType(i));
				bool PreferredVersionExists = false;
				for(size_t BackendIndex = BackendStartIndex + 1; BackendIndex < s_vSupportedBackendInfos.size(); ++BackendIndex)
				{
					const auto &Candidate = s_vSupportedBackendInfos[BackendIndex];
					if(str_comp_nocase(Candidate.m_pBackendName, AutoInfo.m_pBackendName) == 0 && Candidate.m_Major == PreferredVersion.m_Major && Candidate.m_Minor == PreferredVersion.m_Minor && Candidate.m_Patch == PreferredVersion.m_Patch)
					{
						PreferredVersionExists = true;
						break;
					}
				}
				if(!PreferredVersionExists)
				{
					SMenuBackendInfo PreferredInfo;
					PreferredInfo.m_BackendType = EBackendType(i);
					PreferredInfo.m_pBackendName = AutoInfo.m_pBackendName;
					PreferredInfo.m_Major = PreferredVersion.m_Major;
					PreferredInfo.m_Minor = PreferredVersion.m_Minor;
					PreferredInfo.m_Patch = PreferredVersion.m_Patch;
					PreferredInfo.m_Found = true;
					char aPreferredBackendName[256];
					FormatQmGraphicsBackendDisplayName(aPreferredBackendName, sizeof(aPreferredBackendName), PreferredInfo.m_pBackendName, PreferredInfo.m_Major, PreferredInfo.m_Minor, PreferredInfo.m_Patch, false);
					s_vSupportedBackendInfos.insert(s_vSupportedBackendInfos.begin() + BackendStartIndex + 1, PreferredInfo);
					s_vSupportedBackendNames.insert(s_vSupportedBackendNames.begin() + BackendStartIndex + 1, aPreferredBackendName);
				}
			}
		}
		s_BackendListCacheValid = true;
		s_BackendListCacheDriverBlocked = g_Config.m_GfxDriverIsBlocked;
		str_copy(s_aBackendListCacheLanguage, g_Config.m_ClLanguagefile);
	}
	const uint32_t FoundBackendCount = (uint32_t)s_vSupportedBackendInfos.size();
	const auto &GpuList = Graphics()->GetGpus();
	const int OldWindowMode = g_Config.m_GfxFullscreen ? (g_Config.m_GfxFullscreen == 1 ? 4 : (g_Config.m_GfxFullscreen == 2 ? 3 : 2)) : (g_Config.m_GfxBorderless ? 1 : 0);
#if defined(CONF_FAMILY_WINDOWS)
	constexpr bool HasSystemGpuSettings = true;
#else
	constexpr bool HasSystemGpuSettings = false;
#endif
	const auto GpuControl = ResolveSettingsGpuControl(GpuList.m_vGpus.size(), GpuList.m_CanSelect, HasSystemGpuSettings);
	const int GpuRowCount = GpuControl.RowCount();
	const int GraphicsBackendRowCount = (FoundBackendCount > 1 ? 1 : 0) + GpuRowCount;
	const qm_card_registry::SCardDefault *pDisplayDefault = qm_card_registry::FindByStableId("deck:graphics-display");
	const qm_card_registry::SCardDefault *pVisualDefault = qm_card_registry::FindByStableId("deck:graphics-visual");
	const qm_card_registry::SCardDefault *pIconsDefault = qm_card_registry::FindByStableId("deck:graphics-icons");
	const qm_card_registry::SCardDefault *pModesDefault = qm_card_registry::FindByStableId("deck:graphics-modes");
	const qm_card_registry::SCardDefault *pInteractionDefault = qm_card_registry::FindByStableId("deck:graphics-interaction");
	dbg_assert(pDisplayDefault != nullptr && pVisualDefault != nullptr && pIconsDefault != nullptr && pModesDefault != nullptr && pInteractionDefault != nullptr, "graphics settings cards must be registered");
	if(pDisplayDefault == nullptr || pVisualDefault == nullptr || pIconsDefault == nullptr || pModesDefault == nullptr || pInteractionDefault == nullptr)
		return 0;

	const float CardChromeHeight = BuildSettingsCardFrame({0.0f, 0.0f, 1.0f, 0.0f}, {nullptr, nullptr, "subtitle"}, 0.0f, UiScale).m_Rect.h;
	const float DisplayChromeHeight = CardChromeHeight;
	const float VisualChromeHeight = CardChromeHeight;
	const float IconsChromeHeight = CardChromeHeight;
	const float ModesChromeHeight = CardChromeHeight;
	const float InteractionChromeHeight = CardChromeHeight;
	const SSettingsListCardGeometry GraphicsModesGeometry = ResolveSettingsGraphicsModesGeometry(s_NumNodes, GraphicsMetrics);
	// 显示模式卡片包含窗口模式、当前模式和实际模式列表；测量时为前两项
	// 保留行高与间距，避免 card deck 将列表挤压成一行。
	const float GraphicsModesTargetContentHeight = GraphicsModesGeometry.m_ContentHeight;
	// 动态高度只交给 Card Deck 处理。页面私有动画会让 measure revision 每帧变化，
	// 与 Deck 的高度轨道叠加后产生双重缓动和背景闪动。
	const uint64_t GraphicsModesMeasureRevision = static_cast<uint64_t>(std::max(0, s_NumNodes));
	const float GraphicsModesMinCardHeight = ModesChromeHeight + GraphicsModesTargetContentHeight;
	const int GraphicsDisplayRowCount = 5 + (Graphics()->GetNumScreens() > 1 ? 1 : 0) + GraphicsBackendRowCount;
	const float GraphicsDisplayContentHeight = ResolveSettingsRowsHeight(GraphicsDisplayRowCount, GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_LineSpacing);
	const float GraphicsDisplayMinCardHeight = DisplayChromeHeight + GraphicsDisplayContentHeight;
	uint64_t GraphicsDisplayMeasureRevision = (static_cast<uint64_t>(std::max(0, GraphicsDisplayRowCount)) << 32) ^ static_cast<uint64_t>(std::max(0, OldWindowMode));
	GraphicsDisplayMeasureRevision = GraphicsDisplayMeasureRevision * 1099511628211ULL ^ static_cast<uint64_t>(GpuList.m_CanSelect);
	GraphicsDisplayMeasureRevision = GraphicsDisplayMeasureRevision * 1099511628211ULL ^ str_quickhash(GpuList.m_AutoGpu.m_aName);
	for(const auto &Gpu : GpuList.m_vGpus)
		GraphicsDisplayMeasureRevision = GraphicsDisplayMeasureRevision * 1099511628211ULL ^ str_quickhash(Gpu.m_aName);
	const bool GraphicsVisualTextCustomVisible = g_Config.m_QmUiTextColorMode == 3;
	const float GraphicsVisualContentHeight = ResolveSettingsContentFlowHeight(GraphicsMetrics, {MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight, GraphicsVisualTextCustomVisible),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_LineHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_LineHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_LineHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_ButtonHeight),
													    MakeSettingsContentFlowEntry(GraphicsMetrics.m_LineHeight)});
	const uint64_t GraphicsVisualMeasureRevision = static_cast<uint64_t>(GraphicsVisualTextCustomVisible);
	const float GraphicsVisualMinCardHeight = VisualChromeHeight + GraphicsVisualContentHeight;
	const float GraphicsIconsContentHeight = ResolveSettingsContentFlowHeight(GraphicsMetrics, {GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_LineHeight});
	const float GraphicsIconsMinCardHeight = IconsChromeHeight + GraphicsIconsContentHeight;
	const float GraphicsInteractionContentHeight = ResolveSettingsContentFlowHeight(GraphicsMetrics, {GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_LineHeight});
	const float GraphicsInteractionMinCardHeight = InteractionChromeHeight + GraphicsInteractionContentHeight;
	static CButtonContainer s_aGraphicsIconColorButtons[3];
	static CButtonContainer s_aGraphicsIconWeightButtons[4];
	static CButtonContainer s_aGraphicsBlurModeButtons[3];
	static CButtonContainer s_GraphicsIconCustomColorResetId;
	static CButtonContainer s_GraphicsFriendIconColorResetId;
	static CButtonContainer s_GraphicsFavoriteIconColorResetId;

	const bool RenderOnly = Ui()->RenderOnly();
	const auto BuildDefinitions = [this, pModesDefault, pDisplayDefault, pVisualDefault, pIconsDefault, pInteractionDefault, GraphicsPage, GraphicsModesMinCardHeight, ModesChromeHeight, GraphicsDisplayMinCardHeight, DisplayChromeHeight, GraphicsVisualMinCardHeight, VisualChromeHeight, GraphicsVisualMeasureRevision, GraphicsIconsMinCardHeight, IconsChromeHeight, GraphicsInteractionMinCardHeight, InteractionChromeHeight, GraphicsModesMeasureRevision, GraphicsDisplayMeasureRevision, GraphicsDisplayRowCount, GraphicsBackendRowCount, FoundBackendCount, OldWindowMode, GraphicsMetrics, BodySize, DoGraphicsNumericField](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(5);
		const SSettingsCardSpec ModesSpec{pModesDefault->m_pStableId, Localize(pModesDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pModesDefault)};
		const SSettingsCardSpec DisplaySpec{pDisplayDefault->m_pStableId, Localize(pDisplayDefault->m_pTitle), nullptr};
		const SSettingsCardSpec VisualSpec{pVisualDefault->m_pStableId, Localize(pVisualDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pVisualDefault)};
		const SSettingsCardSpec IconsSpec{pIconsDefault->m_pStableId, Localize(pIconsDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pIconsDefault)};
		const SSettingsCardSpec InteractionSpec{pInteractionDefault->m_pStableId, Localize(pInteractionDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pInteractionDefault)};
		const auto AddCard = [&vCards](const SSettingsCardSpec &Spec, float MinHeight, float ChromeHeight, FSettingsCardRender Render, uint64_t MeasureRevision = 0) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			Definition.m_Measure = [MinHeight, ChromeHeight](float) {
				return maximum(0.0f, MinHeight - ChromeHeight);
			};
			Definition.m_Render = std::move(Render);
			Definition.m_MeasureRevision = MeasureRevision;
			vCards.push_back(std::move(Definition));
		};

		AddCard(ModesSpec, GraphicsModesMinCardHeight, ModesChromeHeight, [this, GraphicsMetrics, GraphicsPage, OldWindowMode](CUIRect ContentRect) {
		char aBuf[128];
		CUIRect ModeList = ContentRect;
		CUIRect WindowModeDropDown;
		CUIRect ModeLabel;
		ModeList.HSplitTop(GraphicsMetrics.m_LineHeight, &WindowModeDropDown, &ModeList);
		ModeList.HSplitTop(GraphicsMetrics.m_LineSpacing, nullptr, &ModeList);
		ModeList.HSplitTop(GraphicsMetrics.m_LineHeight, &ModeLabel, &ModeList); // current display mode
		ModeList.HSplitTop(GraphicsMetrics.m_LineSpacing, nullptr, &ModeList);
		static CListBox s_ListBox;
		const float RowHeightResList = GraphicsMetrics.m_ListRowHeight;
		const float FontSizeResListHeader = GraphicsMetrics.m_BodySize;
		const float FontSizeResList = GraphicsMetrics.m_SmallSize;
		const char *apWindowModes[] = {Localize("Windowed"), Localize("Windowed borderless"), Localize("Windowed fullscreen"), Localize("Desktop fullscreen"), Localize("Fullscreen")};
		static CUi::SDropDownState s_WindowModeDropDownState;
		static CScrollRegion s_WindowModeDropDownScrollRegion;
		s_WindowModeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_WindowModeDropDownScrollRegion;
		CUi::SDropDownProperties WindowModeDropDownProps;
		WindowModeDropDownProps.m_pPopupViewport = &GraphicsPage.m_ScrollViewport;
		const int NewWindowMode = DoSettingsDropDown(&WindowModeDropDown, OldWindowMode, apWindowModes, std::size(apWindowModes), s_WindowModeDropDownState, WindowModeDropDownProps, &g_Config.m_GfxFullscreen, &g_Config.m_GfxBorderless);
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

		{
			int G = std::gcd(g_Config.m_GfxScreenWidth, g_Config.m_GfxScreenHeight);
			const int AspectGcd = G > 0 ? G : 1;
			const float RawHiDPIScale = Graphics()->ScreenHiDPIScale();
			const float HiDPIScale = std::isfinite(RawHiDPIScale) && RawHiDPIScale > 0.0f ? RawHiDPIScale : 1.0f;
			str_format(aBuf, sizeof(aBuf), "%s: %dx%d @%dhz %d bit (%d:%d)", Localize("Current"), (int)(g_Config.m_GfxScreenWidth * HiDPIScale), (int)(g_Config.m_GfxScreenHeight * HiDPIScale), g_Config.m_GfxScreenRefreshRate, g_Config.m_GfxColorDepth, g_Config.m_GfxScreenWidth / AspectGcd, g_Config.m_GfxScreenHeight / AspectGcd);
			Ui()->DoLabel(&ModeLabel, aBuf, FontSizeResListHeader, TEXTALIGN_MC);
		}

		{
			int SelectedOld = -1;
			s_ListBox.SetActive(!Ui()->IsPopupOpen());
			s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
			s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
			s_ListBox.SetHideScrollbar(true);
			s_ListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_HOVER);
			s_ListBox.DoStart(RowHeightResList, s_NumNodes, 1, 3, SelectedOld, &ModeList);

			for(int i = 0; i < s_NumNodes; ++i)
			{
				const int Depth = s_aModes[i].m_Red + s_aModes[i].m_Green + s_aModes[i].m_Blue > 16 ? 24 : 16;
				if(g_Config.m_GfxColorDepth == Depth &&
					g_Config.m_GfxScreenWidth == s_aModes[i].m_WindowWidth &&
					g_Config.m_GfxScreenHeight == s_aModes[i].m_WindowHeight &&
					g_Config.m_GfxScreenRefreshRate == s_aModes[i].m_RefreshRate)
				{
					SelectedOld = i;
				}

				const CListboxItem Item = s_ListBox.DoNextItem(&s_aModes[i], SelectedOld == i);
				if(!Item.m_Visible)
					continue;

				int G = std::gcd(s_aModes[i].m_WindowWidth, s_aModes[i].m_WindowHeight);
				str_format(aBuf, sizeof(aBuf), " %dx%d @%dhz %d bit (%d:%d)", s_aModes[i].m_CanvasWidth, s_aModes[i].m_CanvasHeight, s_aModes[i].m_RefreshRate, Depth, s_aModes[i].m_WindowWidth / G, s_aModes[i].m_WindowHeight / G);
				Ui()->DoLabel(&Item.m_Rect, aBuf, FontSizeResList, TEXTALIGN_ML);
			}

			const int NewSelected = s_ListBox.DoEnd();
			if(SelectedOld != NewSelected && NewSelected >= 0 && NewSelected < s_NumNodes)
			{
				const int Depth = s_aModes[NewSelected].m_Red + s_aModes[NewSelected].m_Green + s_aModes[NewSelected].m_Blue > 16 ? 24 : 16;
				g_Config.m_GfxColorDepth = Depth;
				g_Config.m_GfxScreenWidth = s_aModes[NewSelected].m_WindowWidth;
				g_Config.m_GfxScreenHeight = s_aModes[NewSelected].m_WindowHeight;
				g_Config.m_GfxScreenRefreshRate = s_aModes[NewSelected].m_RefreshRate;
				Graphics()->ResizeToScreen();
			}
		} }, GraphicsModesMeasureRevision);
		const FSettingsCardRenderMeasured RenderDisplay = [this, GraphicsMetrics, GraphicsPage, FoundBackendCount, BodySize, DoGraphicsNumericField, OldWindowMode](CUIRect &ContentRect) {
			CUIRect Button;
			char aBuf[128];
			CUIRect &CardView = ContentRect;
			CSettingsContentRowFlow Rows(CardView, GraphicsMetrics);
			const auto NextRow = [&]() {
				return Rows.NextLine();
			};
			if(Graphics()->GetNumScreens() > 1)
			{
				CUIRect ScreenDropDown = NextRow();

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
				ScreenDropDownProps.m_pPopupViewport = &GraphicsPage.m_ScrollViewport;
				const int NewScreen = DoSettingsDropDown(&ScreenDropDown, g_Config.m_GfxScreen, s_vpScreenNames.data(), s_vpScreenNames.size(), s_ScreenDropDownState, ScreenDropDownProps, &g_Config.m_GfxScreen);
				if(NewScreen != g_Config.m_GfxScreen)
					Graphics()->SwitchWindowScreen(NewScreen, true);
			}

			Button = NextRow();
			str_format(aBuf, sizeof(aBuf), "%s (%s)", Localize("V-Sync"), Localize("may cause delay"));
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_GfxVsync, "graphics-vsync-delay-warning", aBuf, g_Config.m_GfxVsync, &Button))
			{
				Graphics()->SetVSync(!g_Config.m_GfxVsync);
			}

			const auto DoGraphicsChoiceRow = [this, GraphicsMetrics, GraphicsPage](CUIRect Row, const char *pLabel, const char *pId, const char **ppNames, size_t Count, int Current, CUi::SDropDownState &State, CScrollRegion &ScrollRegion, const void *pConfigValue, auto &&OnChanged) {
				if(ppNames == nullptr || Count == 0)
					return;
				for(size_t Index = 0; Index < Count; ++Index)
					if(ppNames[Index] == nullptr)
						return;
				Current = std::clamp(Current, 0, static_cast<int>(Count) - 1);
				CUIRect Label, DropDown;
				Row.VSplitLeft(std::clamp(Row.w * 0.38f, 120.0f * GraphicsMetrics.m_UiScale, 220.0f * GraphicsMetrics.m_UiScale), &Label, &DropDown);
				DropDown.VSplitLeft(GraphicsMetrics.m_LineSpacing, nullptr, &DropDown);
				DoSettingsMenuLabel(SETTINGS_GRAPHICS, -1, -1, pId, &Label, pLabel, GraphicsMetrics.m_BodySize, TEXTALIGN_ML);
				State.m_SelectionPopupContext.m_pScrollRegion = &ScrollRegion;
				CUi::SDropDownProperties DropDownProps;
				DropDownProps.m_pPopupViewport = &GraphicsPage.m_ScrollViewport;
				const int NewValue = DoSettingsDropDown(&DropDown, Current, ppNames, Count, State, DropDownProps, pConfigValue, nullptr, &Row);
				if(NewValue != Current)
					OnChanged(NewValue);
			};

			Button = NextRow();
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

			Button = NextRow();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_GfxHighDetail, "High Detail", Localize("High Detail"), g_Config.m_GfxHighDetail, &Button))
				g_Config.m_GfxHighDetail ^= 1;
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_GfxHighDetail, &Button, Localize("Allows maps to render with more detail"));

			Button = NextRow();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_ClShowfps, "Show FPS", Localize("Show FPS"), g_Config.m_ClShowfps, &Button))
				g_Config.m_ClShowfps ^= 1;
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowfps, &Button, Localize("Renders your frame rate in the top right"));

			Button = NextRow();
			str_copy(aBuf, " ");
			str_append(aBuf, Localize("Hz", "Hertz"));
			DoGraphicsNumericField("graphics-refresh-rate", &g_Config.m_GfxRefreshRate, &g_Config.m_GfxRefreshRate, Button, Localize("Refresh Rate"), 10, 10000, &CUi::ms_LinearScrollbarScale, aBuf, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, 0, 10000);

			if(FoundBackendCount > 1)
			{
				CUIRect Row = NextRow();
				static CUi::SDropDownState s_BackendDropDownState;
				static CScrollRegion s_BackendDropDownScrollRegion;
				static std::vector<const char *> s_vpGraphicsBackendNames;
				static std::vector<SMenuBackendInfo> s_vGraphicsBackendInfos;
				static std::string s_CustomBackendId;
				static std::string s_CustomBackendDisplayName;
				static std::string s_ActiveBackendDisplayName;
				s_vpGraphicsBackendNames.clear();
				s_vGraphicsBackendInfos.clear();
				for(size_t i = 0; i < s_vSupportedBackendNames.size(); ++i)
				{
					s_vpGraphicsBackendNames.push_back(s_vSupportedBackendNames[i].c_str());
					s_vGraphicsBackendInfos.push_back(s_vSupportedBackendInfos[i]);
				}
				int Selected = -1;
				for(size_t i = 0; i < s_vSupportedBackendInfos.size(); ++i)
				{
					if(graphics_backend::MatchesConfiguredBackend(s_vSupportedBackendInfos[i].m_BackendType, s_vSupportedBackendInfos[i].m_pBackendName, s_vSupportedBackendInfos[i].m_Major, s_vSupportedBackendInfos[i].m_Minor, s_vSupportedBackendInfos[i].m_Patch, g_Config.m_GfxBackend, g_Config.m_GfxGLMajor, g_Config.m_GfxGLMinor, g_Config.m_GfxGLPatch))
						Selected = (int)i;
				}
				if(Selected < 0)
				{
					if(!Ui()->RenderOnly() && graphics_backend::IsKnownUnavailableBackendName(g_Config.m_GfxBackend))
					{
						str_copy(g_Config.m_GfxBackend, "OpenGL");
						g_Config.m_GfxGLMajor = 0;
						g_Config.m_GfxGLMinor = 0;
						g_Config.m_GfxGLPatch = 0;
						for(size_t i = 0; i < s_vSupportedBackendInfos.size(); ++i)
						{
							if(graphics_backend::MatchesConfiguredBackend(s_vSupportedBackendInfos[i].m_BackendType, s_vSupportedBackendInfos[i].m_pBackendName, s_vSupportedBackendInfos[i].m_Major, s_vSupportedBackendInfos[i].m_Minor, s_vSupportedBackendInfos[i].m_Patch, g_Config.m_GfxBackend, g_Config.m_GfxGLMajor, g_Config.m_GfxGLMinor, g_Config.m_GfxGLPatch))
							{
								Selected = (int)i;
								break;
							}
						}
					}
				}
				if(Selected < 0)
				{
					Selected = ResolveSettingsSelectionWithCustomFallback(Selected, (int)s_vGraphicsBackendInfos.size());
					char aBackendDisplayName[128];
					char aCustomDisplayName[192];
					FormatQmGraphicsBackendDisplayName(aBackendDisplayName, sizeof(aBackendDisplayName), g_Config.m_GfxBackend, g_Config.m_GfxGLMajor, g_Config.m_GfxGLMinor, g_Config.m_GfxGLPatch, false);
					str_format(aCustomDisplayName, sizeof(aCustomDisplayName), "%s (%s)", Localize("custom"), aBackendDisplayName);
					s_CustomBackendId = g_Config.m_GfxBackend;
					s_CustomBackendDisplayName = aCustomDisplayName;
					SMenuBackendInfo CustomInfo;
					CustomInfo.m_BackendType = graphics_backend::ParseBackendName(g_Config.m_GfxBackend, BACKEND_TYPE_AUTO);
					CustomInfo.m_pBackendName = s_CustomBackendId.c_str();
					CustomInfo.m_Major = g_Config.m_GfxGLMajor;
					CustomInfo.m_Minor = g_Config.m_GfxGLMinor;
					CustomInfo.m_Patch = g_Config.m_GfxGLPatch;
					s_vGraphicsBackendInfos.push_back(CustomInfo);
					s_vpGraphicsBackendNames.push_back(s_CustomBackendDisplayName.c_str());
				}
				int DetectedMajor = 0;
				int DetectedMinor = 0;
				int DetectedPatch = 0;
				const char *pDetectedBackendName = "";
				const bool HasDetectedContextVersion = Graphics()->GetDetectedContextVersion(DetectedMajor, DetectedMinor, DetectedPatch, pDetectedBackendName);
				if(Selected >= 0 && s_vGraphicsBackendInfos[Selected].m_Major == 0 && HasDetectedContextVersion && str_comp_nocase(s_vGraphicsBackendInfos[Selected].m_pBackendName, pDetectedBackendName) == 0)
				{
					char aBackendDisplayName[128];
					str_format(aBackendDisplayName, sizeof(aBackendDisplayName), "%s (%s: %d.%d)", pDetectedBackendName, Localize("auto"), DetectedMajor, DetectedMinor);
					s_ActiveBackendDisplayName = aBackendDisplayName;
					s_vpGraphicsBackendNames[Selected] = s_ActiveBackendDisplayName.c_str();
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
			}
			if(ResolveSettingsGpuControl(Graphics()->GetGpus().m_vGpus.size(), Graphics()->GetGpus().m_CanSelect, HasSystemGpuSettings).m_ShowSelector)
			{
				CUIRect Row = NextRow();
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
			}
			else if(ResolveSettingsGpuControl(Graphics()->GetGpus().m_vGpus.size(), Graphics()->GetGpus().m_CanSelect, HasSystemGpuSettings).m_ShowSystemSettings)
			{
				const auto &GpuInfo = Graphics()->GetGpus();
				const auto InfoLabel = [this, &Rows, &CardView, BodySize](const char *pText) {
					const float Width = std::max(1.0f, CardView.w);
					const float Height = ResolveSettingsAutoRowHeight(BodySize, TextRender()->TextBoundingBox(BodySize, pText, -1, Width).m_H + BodySize * 0.25f);
					const CUIRect Row = Rows.Next(Height);
					SLabelProperties Props;
					Props.m_MaxWidth = Width;
					Props.m_EnableWidthCheck = false;
					Ui()->DoLabel(&Row, pText, BodySize, TEXTALIGN_ML, Props);
				};
				char aCurrentGpu[512];
				str_format(aCurrentGpu, sizeof(aCurrentGpu), "%s: %s", Localize("Current GPU"), GpuInfo.m_AutoGpu.m_aName[0] ? GpuInfo.m_AutoGpu.m_aName : Localize("Unknown"));
				InfoLabel(aCurrentGpu);
#if defined(CONF_FAMILY_WINDOWS)
				InfoLabel(Localize("Choose a GPU for this app in system graphics settings, then restart the client."));
				static CButtonContainer s_SystemGraphicsSettings;
				const CUIRect SystemSettingsRow = Rows.NextButton();
				if(DoSettingsButton_Menu(SETTINGS_GRAPHICS, -1, -1, &s_SystemGraphicsSettings, "graphics-system-gpu-settings", Localize("System graphics settings"), 0, &SystemSettingsRow) && !Ui()->RenderOnly())
				{
					if(!open_link("ms-settings:display-advancedgraphics"))
						PopupMessage(Localize("Error"), Localize("Could not open system graphics settings."), Localize("Ok"));
				}
#endif
			}
		};
		AddCard(DisplaySpec, GraphicsDisplayMinCardHeight, DisplayChromeHeight, [RenderDisplay](CUIRect Content) { RenderDisplay(Content); }, GraphicsDisplayMeasureRevision);
		vCards.back().m_RenderMeasured = RenderDisplay;
		vCards.back().m_Measure = [this, RenderDisplay](float Width) { return qm_card_catalog::QmCardRenderHook::MeasureContent(this, RenderDisplay, Width); };
		AddCard(VisualSpec, GraphicsVisualMinCardHeight, VisualChromeHeight, [this, GraphicsMetrics, GraphicsPage, BodySize, DoGraphicsNumericField](CUIRect ContentRect) {
			CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);
			const bool TextCustomColorVisible = g_Config.m_QmUiTextColorMode == 3;
			SSettingsContentMetrics ColorMetrics = GraphicsMetrics;
			ColorMetrics.m_LineSpacing = 0.0f;
			static CButtonContainer s_UiColorResetId;
			CUIRect UiColorRow = Rows.NextButton();
			if(DoLine_AlphaColorPicker(&s_UiColorResetId, ColorMetrics, &UiColorRow, Localize("Interface surface"), &g_Config.m_QmUiColor, &g_Config.m_QmUiOpacity, DefaultConfig::QmUiColor, DefaultConfig::QmUiOpacity))
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);

			static CButtonContainer s_UiAccentColorResetId;
			CUIRect UiAccentColorRow = Rows.NextButton();
			if(DoLine_AlphaColorPicker(&s_UiAccentColorResetId, ColorMetrics, &UiAccentColorRow, Localize("Interface accent color"), &g_Config.m_QmUiAccentColor, &g_Config.m_QmUiAccentOpacity, DefaultConfig::QmUiAccentColor, DefaultConfig::QmUiAccentOpacity))
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);

			static CButtonContainer s_UiSelectedColorResetId;
			const unsigned OldUiSelectedColor = g_Config.m_QmUiSelectedColor;
			CUIRect UiSelectedColorRow = Rows.NextButton();
			DoLine_ColorPicker(&s_UiSelectedColorResetId, ColorMetrics, &UiSelectedColorRow, Localize("Selected item color"), &g_Config.m_QmUiSelectedColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmUiSelectedColor)), false, nullptr, false);
			if(OldUiSelectedColor != g_Config.m_QmUiSelectedColor)
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);

			static CButtonContainer s_UiCardColorResetId;
			CUIRect UiCardColorRow = Rows.NextButton();
			if(DoLine_AlphaColorPicker(&s_UiCardColorResetId, ColorMetrics, &UiCardColorRow, Localize("Settings card background"), &g_Config.m_QmUiCardColor, &g_Config.m_QmUiCardOpacity, DefaultConfig::QmUiCardColor, DefaultConfig::QmUiCardOpacity))
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);

			static CButtonContainer s_DropdownColorResetId;
			CUIRect DropdownColorRow = Rows.NextButton();
			if(DoLine_AlphaColorPicker(&s_DropdownColorResetId, ColorMetrics, &DropdownColorRow, Localize("Button background color"), &g_Config.m_QmUiDropdownColor, &g_Config.m_QmUiDropdownOpacity, DefaultConfig::QmUiDropdownColor, DefaultConfig::QmUiDropdownOpacity))
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);

			static CButtonContainer s_InputColorResetId;
			CUIRect InputColorRow = Rows.NextButton();
			if(DoLine_AlphaColorPicker(&s_InputColorResetId, ColorMetrics, &InputColorRow, Localize("Input background color"), &g_Config.m_QmUiInputColor, &g_Config.m_QmUiInputOpacity, DefaultConfig::QmUiInputColor, DefaultConfig::QmUiInputOpacity))
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);

			static CButtonContainer s_DropdownListColorResetId;
			CUIRect DropdownListColorRow = Rows.NextButton();
			if(DoLine_AlphaColorPicker(&s_DropdownListColorResetId, ColorMetrics, &DropdownListColorRow, Localize("Expanded dropdown background color"), &g_Config.m_QmUiDropdownListColor, &g_Config.m_QmUiDropdownListOpacity, DefaultConfig::QmUiDropdownListColor, DefaultConfig::QmUiDropdownListOpacity))
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);

			static CButtonContainer s_PopupColorResetId;
			CUIRect PopupColorRow = Rows.NextButton();
			if(DoLine_AlphaColorPicker(&s_PopupColorResetId, ColorMetrics, &PopupColorRow, Localize("Secondary menu background color"), &g_Config.m_QmUiPopupColor, &g_Config.m_QmUiPopupOpacity, DefaultConfig::QmUiPopupColor, DefaultConfig::QmUiPopupOpacity))
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
			CUIRect TextModeRow = Rows.NextButton();
			CUIRect TextModeLabel, TextModeControl;
			TextModeRow.VSplitMid(&TextModeLabel, &TextModeControl, GraphicsMetrics.m_LineSpacing);
			DoSettingsMenuLabel(SETTINGS_GRAPHICS, -1, -1, "text-color-mode", &TextModeLabel, Localize("Text color mode"), GraphicsMetrics.m_BodySize, TEXTALIGN_ML);
			static CUi::SDropDownState s_TextColorModeState;
			static CScrollRegion s_TextColorModeScroll;
			s_TextColorModeState.m_SelectionPopupContext.m_pScrollRegion = &s_TextColorModeScroll;
			const char *apTextModes[] = {Localize("Auto"), Localize("White"), Localize("Black"), Localize("Custom")};
			CUi::SDropDownProperties TextModeProps;
			TextModeProps.m_pPopupViewport = &GraphicsPage.m_ScrollViewport;
			const int TextMode = DoSettingsDropDown(&TextModeControl, g_Config.m_QmUiTextColorMode, apTextModes, std::size(apTextModes), s_TextColorModeState, TextModeProps, &g_Config.m_QmUiTextColorMode, nullptr, &TextModeRow);
			if(TextMode != g_Config.m_QmUiTextColorMode)
			{
				g_Config.m_QmUiTextColorMode = TextMode;
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
			}
			CUIRect TextCustomColorRow = Rows.NextIf(TextCustomColorVisible, GraphicsMetrics.m_ButtonHeight);
			if(TextCustomColorVisible)
			{
				static CButtonContainer s_TextCustomColorResetId;
				const unsigned PreviousTextColor = g_Config.m_QmUiTextCustomColor;
				DoLine_ColorPicker(&s_TextCustomColorResetId, ColorMetrics, &TextCustomColorRow, Localize("Custom text color"), &g_Config.m_QmUiTextCustomColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmUiTextCustomColor)), false, nullptr, false);
				if(PreviousTextColor != g_Config.m_QmUiTextCustomColor)
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);
			}
			static CButtonContainer s_FocusColorResetId;
			const unsigned OldFocusColor = g_Config.m_QmUiFocusColor;
			CUIRect FocusColorRow = Rows.NextButton();
			SSettingsContentMetrics FocusColorMetrics = ColorMetrics;
			FocusColorMetrics.m_LineSpacing = 0.0f;
			DoLine_ColorPicker(&s_FocusColorResetId, FocusColorMetrics, &FocusColorRow, Localize("Text input focus ring color"), &g_Config.m_QmUiFocusColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmUiFocusColor)), false, nullptr, false);
			GameClient()->m_Tooltips.DoToolTip(&s_FocusColorResetId, &FocusColorRow, Localize("Used by active shared text and numeric input fields"));
			if(OldFocusColor != g_Config.m_QmUiFocusColor)
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);

			static CButtonContainer s_ScoreboardColorResetId;
			CUIRect ScoreboardColorRow = Rows.NextButton();
			if(DoLine_AlphaColorPicker(&s_ScoreboardColorResetId, ColorMetrics, &ScoreboardColorRow, Localize("Scoreboard surface"), &g_Config.m_QmScoreboardColor, &g_Config.m_QmScoreboardOpacity, DefaultConfig::QmScoreboardColor, DefaultConfig::QmScoreboardOpacity))
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);

			CUIRect Button = Rows.NextLine();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmGaussianBlur, "enable-backdrop-blur", Localize("Enable backdrop blur"), g_Config.m_QmGaussianBlur, &Button))
				g_Config.m_QmGaussianBlur ^= 1;

			const char *apBlurModeLabels[] = {Localize("Gaussian"), Localize("Kawase"), Localize("Dual Kawase")};
			const char *apBlurModeTooltips[] = {
				Localize("Smooth and accurate blur with the highest GPU cost."),
				Localize("Fast, lightweight blur with a softer approximation."),
				Localize("Multi-resolution blur with a stronger result and balanced GPU cost."),
			};
			CUIRect BlurModeLabel, BlurModeSegments;
			Button = Rows.NextLine();
			Button.VSplitLeft(std::clamp(Button.w * 0.36f, 96.0f, 150.0f), &BlurModeLabel, &BlurModeSegments);
			BlurModeSegments.VSplitLeft(8.0f, nullptr, &BlurModeSegments);
			Ui()->DoLabel(&BlurModeLabel, Localize("Blur mode"), BodySize, TEXTALIGN_ML);
			const int NewBlurMode = DoSegmentedChoice(s_aGraphicsBlurModeButtons, apBlurModeLabels, (int)std::size(apBlurModeLabels), g_Config.m_QmBlurMode, BlurModeSegments);
			if(NewBlurMode != g_Config.m_QmBlurMode)
				g_Config.m_QmBlurMode = NewBlurMode;
			// 整行 tooltip 跟随当前模式，保留原先逐段的说明文案。
			GameClient()->m_Tooltips.DoToolTip(&s_aGraphicsBlurModeButtons[0], &Button, apBlurModeTooltips[std::clamp(g_Config.m_QmBlurMode, 0, (int)std::size(apBlurModeTooltips) - 1)]);

			Button = Rows.NextLine();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiCardBorders, "show-settings-card-borders", Localize("Show settings card borders"), g_Config.m_QmUiCardBorders, &Button))
				g_Config.m_QmUiCardBorders ^= 1;

			static CButtonContainer s_CardBorderColorResetId;
			const unsigned OldCardBorderColor = g_Config.m_QmUiCardBorderColor;
			CUIRect CardBorderColorRow = Rows.NextButton();
			DoLine_ColorPicker(&s_CardBorderColorResetId, ColorMetrics, &CardBorderColorRow, Localize("Settings card border color"), &g_Config.m_QmUiCardBorderColor, ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f), false, nullptr, true, false);
			if(OldCardBorderColor != g_Config.m_QmUiCardBorderColor)
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::CONFIG_HASH_CHANGED);

			Button = Rows.NextLine();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiCardRainbowTitles, "rainbow-card-titles", Localize("Rainbow card titles"), g_Config.m_QmUiCardRainbowTitles, &Button))
				g_Config.m_QmUiCardRainbowTitles ^= 1; }, GraphicsVisualMeasureRevision);
		AddCard(IconsSpec, GraphicsIconsMinCardHeight, IconsChromeHeight, [this, GraphicsMetrics, BodySize](CUIRect ContentRect) {
			CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);
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
			DoIconChoiceRow(Rows.Next(ResolveSettingsRadioRowLayout(ContentRect, 3, GraphicsMetrics).m_Height), Localize("UI icon color"), apIconColorLabels, std::size(apIconColorLabels), qm_icon_settings::PresetIndex(g_Config.m_QmUiIconColor), s_aGraphicsIconColorButtons, [this](int NewValue) {
				qm_icon_settings::SelectPreset(NewValue, g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled);
				Client()->OnWindowResize();
			});
			CUIRect CustomToggleRow = Rows.NextLine();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiIconCustomColorEnabled, "graphics-custom-icon-color", Localize("Use custom UI icon color"), CustomColor, &CustomToggleRow))
			{
				qm_icon_settings::ToggleCustomColor(g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled);
				Client()->OnWindowResize();
			}
			if(CustomColor)
			{
				SSettingsContentMetrics ColorMetrics = GraphicsMetrics;
				ColorMetrics.m_LineSpacing = 0.0f;
				CUIRect CustomColorRow = Rows.NextButton();
				DoLine_ColorPicker(&s_GraphicsIconCustomColorResetId, ColorMetrics, &CustomColorRow, Localize("UI icon custom color"), &g_Config.m_QmUiIconCustomColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false, nullptr, false, false);
			}
			SSettingsContentMetrics SemanticColorMetrics = GraphicsMetrics;
			SemanticColorMetrics.m_LineSpacing = 0.0f;
			CUIRect FriendColorRow = Rows.NextButton();
			DoLine_ColorPicker(&s_GraphicsFriendIconColorResetId, SemanticColorMetrics, &FriendColorRow, Localize("Friend icon color"), &g_Config.m_QmUiFriendIconColor, color_cast<ColorRGBA>(ColorHSLA(0x00D1AB)), false, nullptr, false, false);
			CUIRect FavoriteColorRow = Rows.NextButton();
			DoLine_ColorPicker(&s_GraphicsFavoriteIconColorResetId, SemanticColorMetrics, &FavoriteColorRow, Localize("Favorite icon color"), &g_Config.m_QmUiFavoriteIconColor, color_cast<ColorRGBA>(ColorHSLA(0x21FFA6)), false, nullptr, false, false);
			DoIconChoiceRow(Rows.Next(ResolveSettingsRadioRowLayout(ContentRect, 4, GraphicsMetrics).m_Height), Localize("UI icon style"), apIconWeightLabels, std::size(apIconWeightLabels), IconWeightIndex, s_aGraphicsIconWeightButtons, [this](int NewValue) {
				const int NewWeight = s_aIconWeightValues[NewValue];
				if(NewWeight == NormalizeQmIconWeight(g_Config.m_QmUiIconWeight))
					return;
				g_Config.m_QmUiIconWeight = NewWeight;
				GameClient()->SyncQmUiIconWeight();
			});
		});
		vCards.back().m_Measure = [GraphicsMetrics](float Width) {
			return qm_icon_settings::ContentHeight(GraphicsMetrics, qm_icon_settings::CustomColorEnabled(g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled), Width);
		};
		vCards.back().m_MeasureRevision = static_cast<uint64_t>(qm_icon_settings::CustomColorEnabled(g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled));
		vCards.back().m_PreLayoutInput = [this, GraphicsMetrics](CUIRect ContentRect) {
			if(m_MenuTextPlanCollecting)
				return false;
			bool Changed = false;
			const bool CustomColor = qm_icon_settings::CustomColorEnabled(g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled);
			const auto ProcessChoiceRow = [this, GraphicsMetrics, &Changed](CUIRect Row, int Current, int Count, CButtonContainer *pButtons, auto &&OnChanged) {
				const SSettingsRadioRowLayout Layout = ResolveSettingsRadioRowLayout(Row, Count, GraphicsMetrics);
				CUIRect Segments = Layout.m_ButtonsRect;
				// 预布局只接手点击，轨道与滑块由正式渲染阶段绘制。
				CUIRect aSegmentSlots[8];
				CUIRect SegmentsRemainder = Segments;
				const int SegmentCount = std::clamp(Count, 0, (int)std::size(aSegmentSlots));
				for(int i = 0; i < SegmentCount; ++i)
					SegmentsRemainder.VSplitLeft(SegmentsRemainder.w / (SegmentCount - i), &aSegmentSlots[i], &SegmentsRemainder);
				for(int i = 0; i < SegmentCount; ++i)
				{
					if(Ui()->DoButtonLogic(&pButtons[i], Current == i, &aSegmentSlots[i], BUTTONFLAG_LEFT))
					{
						OnChanged(i);
						Changed = true;
					}
				}
			};
			CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);
			CUIRect Row = Rows.Next(ResolveSettingsRadioRowLayout(ContentRect, 3, GraphicsMetrics).m_Height);
			ProcessChoiceRow(Row, qm_icon_settings::PresetIndex(g_Config.m_QmUiIconColor), 3, s_aGraphicsIconColorButtons, [this](int NewValue) {
				qm_icon_settings::SelectPreset(NewValue, g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled);
				Client()->OnWindowResize();
			});
			CUIRect CustomToggleRow = Rows.NextLine();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiIconCustomColorEnabled, "graphics-custom-icon-color", Localize("Use custom UI icon color"), CustomColor, &CustomToggleRow))
			{
				qm_icon_settings::ToggleCustomColor(g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled);
				Client()->OnWindowResize();
				Changed = true;
			}
			if(CustomColor)
			{
				const unsigned int OldCustomColor = g_Config.m_QmUiIconCustomColor;
				SSettingsContentMetrics ColorMetrics = GraphicsMetrics;
				ColorMetrics.m_LineSpacing = 0.0f;
				CUIRect CustomColorRow = Rows.NextButton();
				DoLine_ColorPicker(&s_GraphicsIconCustomColorResetId, ColorMetrics, &CustomColorRow, Localize("UI icon custom color"), &g_Config.m_QmUiIconCustomColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false, nullptr, false, false);
				Changed = Changed || OldCustomColor != g_Config.m_QmUiIconCustomColor;
			}
			SSettingsContentMetrics SemanticColorMetrics = GraphicsMetrics;
			SemanticColorMetrics.m_LineSpacing = 0.0f;
			const unsigned OldFriendColor = g_Config.m_QmUiFriendIconColor;
			CUIRect FriendColorRow = Rows.NextButton();
			DoLine_ColorPicker(&s_GraphicsFriendIconColorResetId, SemanticColorMetrics, &FriendColorRow, Localize("Friend icon color"), &g_Config.m_QmUiFriendIconColor, color_cast<ColorRGBA>(ColorHSLA(0x00D1AB)), false, nullptr, false, false);
			const unsigned OldFavoriteColor = g_Config.m_QmUiFavoriteIconColor;
			CUIRect FavoriteColorRow = Rows.NextButton();
			DoLine_ColorPicker(&s_GraphicsFavoriteIconColorResetId, SemanticColorMetrics, &FavoriteColorRow, Localize("Favorite icon color"), &g_Config.m_QmUiFavoriteIconColor, color_cast<ColorRGBA>(ColorHSLA(0x21FFA6)), false, nullptr, false, false);
			Changed = Changed || OldFriendColor != g_Config.m_QmUiFriendIconColor || OldFavoriteColor != g_Config.m_QmUiFavoriteIconColor;
			const int IconWeightIndex = QmIconWeightSegmentIndex(g_Config.m_QmUiIconWeight);
			Row = Rows.Next(ResolveSettingsRadioRowLayout(ContentRect, 4, GraphicsMetrics).m_Height);
			ProcessChoiceRow(Row, IconWeightIndex, 4, s_aGraphicsIconWeightButtons, [this](int NewValue) {
				const int NewWeight = s_aIconWeightValues[NewValue];
				if(NewWeight == NormalizeQmIconWeight(g_Config.m_QmUiIconWeight))
					return;
				g_Config.m_QmUiIconWeight = NewWeight;
				GameClient()->SyncQmUiIconWeight();
			});
			return Changed;
		};
		AddCard(InteractionSpec, GraphicsInteractionMinCardHeight, InteractionChromeHeight, [this, GraphicsMetrics, BodySize](CUIRect ContentRect) {
			CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);
			CUIRect MotionRow = Rows.NextLine();
			CUIRect Label, Segments;
			MotionRow.VSplitLeft(std::clamp(MotionRow.w * 0.36f, 96.0f, 150.0f), &Label, &Segments);
			Segments.VSplitLeft(GraphicsMetrics.m_LineSpacing, nullptr, &Segments);
			DoSettingsLabel(SETTINGS_GRAPHICS, -1, "graphics-ui-motion-level-label", &Label, Localize("UI motion level"), BodySize, TEXTALIGN_ML);
			static CButtonContainer s_aMotionButtons[3];
			const char *apMotionLabels[] = {Localize("Off"), Localize("Reduced"), Localize("Full")};
			const int NewMotionLevel = DoSegmentedChoice(s_aMotionButtons, apMotionLabels, 3, g_Config.m_QmUiMotionLevel, Segments);
			if(NewMotionLevel != g_Config.m_QmUiMotionLevel)
				g_Config.m_QmUiMotionLevel = NewMotionLevel;
			const char *pMotionDescription = nullptr;
			if(g_Config.m_QmUiMotionLevel == 0)
				pMotionDescription = Localize("Off: disables all interface animations while preserving the options below");
			else if(g_Config.m_QmUiMotionLevel == 1)
				pMotionDescription = Localize("Reduced: uses shorter transitions and disables presentation animations");
			else
				pMotionDescription = Localize("Full: each enabled animation category uses its complete transition");
			CUIRect MotionDescription = Rows.NextLine();
			Ui()->DoLabel(&MotionDescription, pMotionDescription, GraphicsMetrics.m_SmallSize, TEXTALIGN_ML);
			GameClient()->m_Tooltips.DoToolTip(&s_aMotionButtons[0], &MotionRow, Localize("This is the master switch for all interface animation categories"));

			CUIRect ListEntryAnimations = Rows.NextLine();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiListEntryAnimations, "settings-card-list-entry-animations", Localize("Card list entry animation"), g_Config.m_QmUiListEntryAnimations, &ListEntryAnimations))
				g_Config.m_QmUiListEntryAnimations ^= 1;
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmUiListEntryAnimations, &ListEntryAnimations, Localize("Animate settings card lists when entering a page"));

			CUIRect CardHeightAnimations = Rows.NextLine();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiCardHeightAnimations, "settings-card-height-animations", Localize("Card height animation"), g_Config.m_QmUiCardHeightAnimations, &CardHeightAnimations))
				g_Config.m_QmUiCardHeightAnimations ^= 1;
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmUiCardHeightAnimations, &CardHeightAnimations, Localize("Animate settings card expand and collapse height changes"));

			CUIRect CardReflowAnimations = Rows.NextLine();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiCardReflowAnimations, "settings-card-reflow-animations", Localize("Card reflow animation"), g_Config.m_QmUiCardReflowAnimations, &CardReflowAnimations))
				g_Config.m_QmUiCardReflowAnimations ^= 1;
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmUiCardReflowAnimations, &CardReflowAnimations, Localize("Animate settings card reorder and layout reflow"));

			CUIRect PresentationAnimations = Rows.NextLine();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmExtraAnimations, "presentation-animations", Localize("Presentation animations"), g_Config.m_QmExtraAnimations, &PresentationAnimations))
				g_Config.m_QmExtraAnimations ^= 1;
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmExtraAnimations, &PresentationAnimations, Localize("Animate chat box, emote selector, scoreboard, and spectate selection"));

			CUIRect PopupBlurRow = Rows.NextLine();
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmUiPopupBlur, "settings-popup-blur", Localize("Popup & dropdown blur"), g_Config.m_QmUiPopupBlur, &PopupBlurRow))
				g_Config.m_QmUiPopupBlur ^= 1;
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_QmUiPopupBlur, &PopupBlurRow, Localize("Apply frosted glass blur behind secondary popups and dropdowns"));
		});
	};
	uint64_t GraphicsLayoutRevision = GraphicsModesMeasureRevision;
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(GraphicsDisplayRowCount);
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(GraphicsBackendRowCount);
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(FoundBackendCount);
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(OldWindowMode);
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(RenderOnly ? 1 : 0);
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ GraphicsVisualMeasureRevision;
	GraphicsLayoutRevision = GraphicsLayoutRevision * 1099511628211ULL ^ GraphicsDisplayMeasureRevision;
	if(pCards != nullptr)
	{
		BuildDefinitions(*pCards);
		const auto UpdateRestartStatus = [this]() {
			if(CheckSettings)
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
		}
	}
	return GraphicsLayoutRevision;
}
