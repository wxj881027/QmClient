/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "countryflags.h"
#include "menus.h"
#include "skins.h"

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
#include <game/client/components/menu_background.h>
#include <game/client/components/message_gradient.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/settings_resource_preview.h>
#include <game/client/components/qmclient/tee_color_code.h>
#include <game/client/components/qmclient/tee_hue_cycle.h>
#include <game/client/components/qmclient/tee_skin_apply.h>
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

	bool PerfDebugEnabled()
	{
		return QmPerfEnabled();
	}

	int64_t PerfDebugStartTime()
	{
		return PerfDebugEnabled() ? time_get() : 0;
	}

	double PerfDebugElapsedMs(int64_t StartTime)
	{
		if(StartTime == 0)
			return 0.0;
		return (time_get() - StartTime) * 1000.0 / time_freq();
	}

	void LogPerfStage(IClient *pClient, const char *pStage, const double DurationMs, const bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/menu", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);
	}

}

bool CMenus::DoMessageGradientLine(CChat &Chat, CUIRect *pView, int Tab, const char *pLabelTextId, const char *pLabel, unsigned *pBaseColor, char *pGradient, int GradientSize, ColorRGBA DefaultColor, CButtonContainer *pResetButton, CButtonContainer *pAddButton, CButtonContainer *pRemoveButton, unsigned *pColorValues, bool CheckBoxSpacing, int *pCheckBoxValue, float LineHeight, float LineSpacing, float BodySize, float ButtonHeight)
{
	const float ResolvedButtonHeight = ButtonHeight > 0.0f ? ButtonHeight : LineHeight;
	const float ColorLineHeight = std::max(LineHeight, ResolvedButtonHeight);
	const float BottomMargin = LineSpacing;
	const float ColorButtonSize = ResolvedButtonHeight;
	const float ColorButtonSpacing = LineSpacing;
	const float ChangeButtonSize = ResolvedButtonHeight;
	SSettingsContentMetrics Metrics;
	Metrics.m_UiScale = std::clamp(LineHeight / ui_token::settings::ROW_HEIGHT, 0.5f, 1.5f);
	Metrics.m_LineHeight = LineHeight;
	Metrics.m_ButtonHeight = ResolvedButtonHeight;
	Metrics.m_BodySize = BodySize;
	Metrics.m_LineSpacing = LineSpacing;

	bool Changed = false;
	const SSettingsColorRowLayout TopLayout = ResolveSettingsColorRowLayout(*pView, Metrics, CheckBoxSpacing && pCheckBoxValue == nullptr);
	pView->y += TopLayout.m_ConsumedHeight;
	pView->h = std::max(0.0f, pView->h - TopLayout.m_ConsumedHeight);
	CUIRect Label = TopLayout.m_LabelRect;
	Label.w = TopLayout.m_ColorButtonRect.x + TopLayout.m_ColorButtonRect.w - Label.x;

	if(pCheckBoxValue != nullptr)
	{
		SLabelProperties LabelProps;
		if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, Tab, Tab, pCheckBoxValue, pLabelTextId, pLabel, *pCheckBoxValue, &Label, LabelProps, true, BodySize))
		{
			*pCheckBoxValue ^= 1;
			Changed = true;
		}
	}
	if(pCheckBoxValue == nullptr)
		DoSettingsMenuLabel(SETTINGS_APPEARANCE, Tab, Tab, pLabelTextId, &Label, pLabel, BodySize, TEXTALIGN_ML);

	if(DoSettingsButton_Menu(SETTINGS_APPEARANCE, Tab, Tab, pResetButton, "appearance-chat-gradient-reset", Localize("Reset"), 0, &TopLayout.m_ResetButtonRect, Metrics, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), 0.1f))
	{
		*pBaseColor = color_cast<ColorHSLA>(DefaultColor).Pack(false);
		CMessageGradient::Reset(pGradient, GradientSize);
		Changed = true;
	}

	int NumColors = CMessageGradient::Unpack(pGradient, pColorValues, CMessageGradient::MAX_COLORS);
	if(NumColors <= 0)
	{
		NumColors = 1;
		pColorValues[0] = *pBaseColor;
	}

	CUIRect ColorLine;
	pView->HSplitTop(ColorLineHeight, &ColorLine, pView);
	CUIRect ColorArea = ColorLine;
	if(CheckBoxSpacing)
		ColorArea.VSplitLeft(ColorLine.h + 5.0f, nullptr, &ColorArea);
	ColorArea.VSplitRight(ChangeButtonSize * 2.0f + ColorButtonSpacing, &ColorArea, &ColorLine);

	for(int ColorIndex = 0; ColorIndex < NumColors; ++ColorIndex)
	{
		CUIRect ColorButton;
		ColorArea.VSplitLeft(ColorButtonSize, &ColorButton, &ColorArea);
		ColorButton.HMargin((ColorButton.h - ColorButtonSize) / 2.0f, &ColorButton);
		if(ColorIndex < NumColors - 1)
			ColorArea.VSplitLeft(ColorButtonSpacing, nullptr, &ColorArea);
		const unsigned OldColor = pColorValues[ColorIndex];
		const ColorHSLA PickedColor = DoButton_ColorPicker(&ColorButton, &pColorValues[ColorIndex], false);
		pColorValues[ColorIndex] = PickedColor.Pack(false);
		if(pColorValues[ColorIndex] != OldColor)
		{
			*pBaseColor = pColorValues[0];
			if(NumColors == 1)
				CMessageGradient::Reset(pGradient, GradientSize);
			else
				CMessageGradient::Pack(pColorValues, NumColors, pGradient, GradientSize);
			Changed = true;
		}
	}

	CUIRect RemoveButton, AddButton;
	ColorLine.VSplitLeft(ChangeButtonSize, &RemoveButton, &ColorLine);
	ColorLine.VSplitLeft(ColorButtonSpacing, nullptr, &ColorLine);
	ColorLine.VSplitLeft(ChangeButtonSize, &AddButton, nullptr);
	RemoveButton.HMargin((RemoveButton.h - ChangeButtonSize) / 2.0f, &RemoveButton);
	AddButton.HMargin((AddButton.h - ChangeButtonSize) / 2.0f, &AddButton);
	const bool CanRemoveColor = NumColors > CMessageGradient::MIN_COLORS;
	const bool CanAddColor = NumColors < CMessageGradient::MAX_COLORS;
	if(DoButton_Menu_QmIcon(pRemoveButton, EQmIcon::MINUS, FONT_ICON_MINUS, CanRemoveColor ? 0 : -1, &RemoveButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), nullptr, BodySize) && CanRemoveColor)
	{
		--NumColors;
		*pBaseColor = pColorValues[0];
		if(NumColors == 1)
			CMessageGradient::Reset(pGradient, GradientSize);
		else
			CMessageGradient::Pack(pColorValues, NumColors, pGradient, GradientSize);
		Changed = true;
	}
	if(DoButton_Menu_QmIcon(pAddButton, EQmIcon::PLUS, FONT_ICON_PLUS, CanAddColor ? 0 : -1, &AddButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), nullptr, BodySize) && CanAddColor)
	{
		pColorValues[NumColors] = pColorValues[NumColors - 1];
		++NumColors;
		CMessageGradient::Pack(pColorValues, NumColors, pGradient, GradientSize);
		Changed = true;
	}

	pView->HSplitTop(BottomMargin, nullptr, pView);
	if(Changed)
	{
		Chat.RebuildChat();
		ConfigManager()->Save();
	}
	return Changed;
}

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

	CScrollRegion gs_LanguageScrollRegion;
	bool gs_LanguageScrollToSelected = false;
	std::array<unsigned char, QM_LANGUAGE_ROW_CACHE_CAPACITY> gs_aLanguageRowIds{};
	std::array<CUIElement, QM_LANGUAGE_ROW_CACHE_CAPACITY> gs_aLanguageLabelElements;
	bool gs_LanguageLabelElementsInit = false;
	float gs_LanguageLabelWidth = -1.0f;
	float gs_LanguageLabelFontSize = -1.0f;
	bool gs_LanguagePageCacheComplete = false;

	char gs_aLanguageCacheLanguageFile[IO_MAX_PATH_LENGTH] = {};

	void EnsureLanguagePageCacheInit(CUi *pUi)
	{
		if(!gs_LanguageLabelElementsInit || !gs_aLanguageLabelElements[0].IsRegistered())
		{
			for(CUIElement &LabelElement : gs_aLanguageLabelElements)
				LabelElement.Init(pUi, 1);
			gs_LanguageLabelElementsInit = true;
		}
	}

	void LayoutLanguagePageBaseRects(float MainViewWidth, CUIRect &List)
	{
		CUIRect MainView;
		MainView.x = 0.0f;
		MainView.y = 0.0f;
		MainView.w = MainViewWidth;
		MainView.h = 600.0f;
		List = MainView;
	}

	float LanguageListLabelWidth(const CUIRect &ListRect, const SSettingsContentMetrics &Metrics)
	{
		SQmScrollRequest ScrollRequest;
		ScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;
		const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest, Metrics.m_UiScale);
		CUIRect ScrollClip = ListRect;
		ScrollClip.VSplitRight(ScrollPolicy.m_Style.m_ScrollbarWidth, &ScrollClip, nullptr);
		CUIRect ItemRect = ScrollClip;
		CUIRect Label;
		ItemRect.h = Metrics.m_ListRowHeight;
		ItemRect.VSplitLeft(ItemRect.h * 2.0f, nullptr, &Label);
		return Label.w;
	}

	bool UseLanguagePageCache()
	{
		return g_Localization.Languages().size() <= QM_LANGUAGE_ROW_CACHE_CAPACITY;
	}

	const char *SettingsPageName(const int Page)
	{
		switch(Page)
		{
		case CMenus::SETTINGS_LANGUAGE: return "language";
		case CMenus::SETTINGS_GENERAL: return "general";
		case CMenus::SETTINGS_PLAYER: return "player";
		case CMenus::SETTINGS_TEE: return "tee";
		case CMenus::SETTINGS_APPEARANCE: return "appearance";
		case CMenus::SETTINGS_CONTROLS: return "controls";
		case CMenus::SETTINGS_GRAPHICS: return "graphics";
		case CMenus::SETTINGS_SOUND: return "sound";
		case CMenus::SETTINGS_DDNET: return "ddnet";
		case CMenus::SETTINGS_ASSETS: return "assets";
		case CMenus::SETTINGS_TCLIENT: return "tclient";
		case CMenus::SETTINGS_QMCLIENT: return "qmclient";
		case CMenus::SETTINGS_SEARCH: return "search";
		case CMenus::SETTINGS_PROFILES: return "profiles";
		case CMenus::SETTINGS_CONFIGS: return "configs";
		case CMenus::SETTINGS_CONTRIBUTORS: return "contributors";
		default: return "unknown";
		}
	}

	void LogSettingsSectionPerf(IClient *pClient, int Page, int Tab, const char *pSectionId, double DurationMs, const char *pDirtyReason, int TextNew, int TextReused)
	{
		char aPayload[256];
		char aTab[16];
		const char *pTab = nullptr;
		if(Tab >= 0)
		{
			str_format(aTab, sizeof(aTab), "%d", Tab);
			pTab = aTab;
		}
		const char *pPageName = SettingsPageName(Page);
		str_format(aPayload, sizeof(aPayload), "event=section page=%s section=%s dur_ms=%.3f visible=%d dirty=%s text_new=%d text_reused=%d",
			pPageName, pSectionId != nullptr ? pSectionId : "unknown", DurationMs, 1, pDirtyReason != nullptr ? pDirtyReason : "unknown", TextNew, TextReused);
		QmPerfLogPayload("perf/section", aPayload, pClient, pPageName, pTab);
	}

	static bool ApplyBackgroundEntitiesInputValue(CLineInput &Input)
	{
		char aNormalized[IO_MAX_PATH_LENGTH];
		const bool Changed = BuildBackgroundEntitiesCommitValueFromInput(Input.GetString(), g_Config.m_ClBackgroundEntities, aNormalized, sizeof(aNormalized));
		if(Changed)
			str_copy(g_Config.m_ClBackgroundEntities, aNormalized, sizeof(g_Config.m_ClBackgroundEntities));
		if(Input.IsActive())
			Input.Deactivate();
		return Changed;
	}

	static void SyncBackgroundEntitiesInput(CLineInput &Input, char *pSync, int SyncSize)
	{
		char aNormalizedConfig[IO_MAX_PATH_LENGTH];
		BuildBackgroundEntitiesValueFromInput(g_Config.m_ClBackgroundEntities, aNormalizedConfig, sizeof(aNormalizedConfig));
		if(str_comp(pSync, aNormalizedConfig) != 0)
		{
			if(!Input.IsActive())
				Input.Set(aNormalizedConfig);
		}
		str_copy(pSync, aNormalizedConfig, SyncSize);
	}

	static bool CommitBackgroundEntitiesInputIfActive(CLineInput &Input, char *pSync, int SyncSize)
	{
		if(!Input.IsActive())
			return false;

		const bool Changed = ApplyBackgroundEntitiesInputValue(Input);
		SyncBackgroundEntitiesInput(Input, pSync, SyncSize);
		return Changed;
	}

	static bool ToggleCurrentMapBackground(CLineInput &Input)
	{
		const bool UseCurrentMap = IsCurrentMapBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities);
		Input.Deactivate();
		if(UseCurrentMap)
			g_Config.m_ClBackgroundEntities[0] = '\0';
		else
			str_copy(g_Config.m_ClBackgroundEntities, CURRENT_MAP);
		return true;
	}

}

void CMenus::ClearSettingsLanguageRowCache()
{
	if(gs_LanguageLabelElementsInit)
	{
		for(CUIElement &LabelElement : gs_aLanguageLabelElements)
			Ui()->ResetUIElement(LabelElement);
	}
	gs_LanguageLabelWidth = -1.0f;
	gs_LanguageLabelFontSize = -1.0f;
	gs_LanguagePageCacheComplete = false;
	gs_aLanguageCacheLanguageFile[0] = '\0';
}

void CMenus::RenderSettingsGeneral(CUIRect MainView)
{
	CPerfTimer RenderTimer;
	CScopedSettingsTextPerfStats TextStats(this);
	const SSettingsContentMetrics GeneralMetrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = GeneralMetrics.m_UiScale;
	const float BodySize = GeneralMetrics.m_BodySize;
	const SSettingsListCardGeometry GeneralLanguageGeometry = ResolveSettingsGeneralLanguageListGeometry((int)g_Localization.Languages().size(), GeneralMetrics);
	const SSettingsListCardGeometry GeneralThemeGeometry = ResolveSettingsGeneralThemeListGeometry((int)GameClient()->m_MenuBackground.GetThemes().size(), GeneralMetrics);
	const float GeneralLanguageListHeight = GeneralLanguageGeometry.m_ContentHeight;
	const float GeneralThemeListHeight = GeneralThemeGeometry.m_ContentHeight;
	const auto IsGeneralDynamicCameraEnabled = []() {
		return g_Config.m_ClDyncam != 0 || g_Config.m_ClMouseFollowfactor > 0;
	};
	const float GeneralGameContentHeight = ResolveSettingsGeneralGameContentHeight(GeneralMetrics, IsGeneralDynamicCameraEnabled());
	const SSettingsPageLayoutFrame GeneralPage = SettingsPageLayout(MainView, UiScale);
	const IUiContext GeneralCardCtx = SettingsUiContext("settings_general", UiScale);
	const SSettingsCardDeckVisualOptions GeneralVisualOptions = SettingsCardDeckVisualOptions();
	static CScrollRegion s_GeneralSettingsScrollRegion;

	const qm_card_registry::SCardDefault *pGameDefault = qm_card_registry::FindByStableId("deck:general-game");
	const qm_card_registry::SCardDefault *pLanguageDefault = qm_card_registry::FindByStableId("deck:general-language");
	const qm_card_registry::SCardDefault *pClientDefault = qm_card_registry::FindByStableId("deck:general-client");
	const qm_card_registry::SCardDefault *pRecordingDefault = qm_card_registry::FindByStableId("deck:general-recording");
	const qm_card_registry::SCardDefault *pConfigFilesDefault = qm_card_registry::FindByStableId("deck:tclient-info-files");
	dbg_assert(pGameDefault != nullptr && pLanguageDefault != nullptr && pClientDefault != nullptr && pRecordingDefault != nullptr && pConfigFilesDefault != nullptr, "general settings cards must be registered");
	if(pGameDefault == nullptr || pLanguageDefault == nullptr || pClientDefault == nullptr || pRecordingDefault == nullptr || pConfigFilesDefault == nullptr)
		return;

	const auto DoNumericField = [this, GeneralCardCtx, BodySize](const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel, int Min, int Max, unsigned Flags, const char *pSuffix = "") {
		ui_widget::SNumericFieldOptions Options;
		Options.m_pLabel = pLabel;
		Options.m_pSuffix = pSuffix;
		Options.m_pScale = &CUi::ms_LinearScrollbarScale;
		Options.m_Flags = Flags;
		Options.m_FontSize = BodySize;
		Options.m_LabelAlign = TEXTALIGN_ML;
		Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;
		if(PrepareSettingsNumericFieldLabel(SETTINGS_GENERAL, -1, -1, pTextId, Rect, pLabel, Flags, Options))
			return false;
		return ui_widget::NumericField(GeneralCardCtx, GetSettingsNumericFieldState(pId), pId, pOption, Min, Max, Rect, Options);
	};

	const bool RenderOnly = Ui()->RenderOnly();
	const auto BuildDefinitions = [this, pGameDefault, pLanguageDefault, pClientDefault, pRecordingDefault, pConfigFilesDefault, GeneralMetrics, BodySize, GeneralGameContentHeight, GeneralLanguageListHeight, GeneralThemeListHeight, DoNumericField, IsGeneralDynamicCameraEnabled](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(5);
		const SSettingsCardSpec GameSpec{pGameDefault->m_pStableId, Localize(pGameDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pGameDefault)};
		const SSettingsCardSpec LanguageSpec{pLanguageDefault->m_pStableId, Localize(pLanguageDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pLanguageDefault)};
		const SSettingsCardSpec ClientSpec{pClientDefault->m_pStableId, Localize(pClientDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pClientDefault)};
		const SSettingsCardSpec RecordingSpec{pRecordingDefault->m_pStableId, Localize(pRecordingDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pRecordingDefault)};
		const auto AddCard = [&vCards](const SSettingsCardSpec &Spec, float ContentHeight, FSettingsCardRender Render) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			Definition.m_Measure = [ContentHeight](float) { return ContentHeight; };
			Definition.m_Render = Render;
			vCards.push_back(Definition);
		};

		AddCard(GameSpec, GeneralGameContentHeight, [this, GeneralMetrics, BodySize](CUIRect Content) {
			CUIRect Button;
			Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
			const bool IsDyncam = g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0;
			if(DoSettingsButton_CheckBox(SETTINGS_GENERAL, -1, &g_Config.m_ClDyncam, "general-dynamic-camera", Localize("Dynamic Camera"), IsDyncam, &Button))
			{
				if(IsDyncam)
				{
					g_Config.m_ClDyncam = 0;
					g_Config.m_ClMouseFollowfactor = 0;
				}
				else
					g_Config.m_ClDyncam = 1;
			}
			if(g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0)
			{
				Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
				Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
				if(DoSettingsButton_CheckBox(SETTINGS_GENERAL, -1, &g_Config.m_ClDyncamSmoothness, "general-smooth-dynamic-camera", Localize("Smooth Dynamic Camera"), g_Config.m_ClDyncamSmoothness, &Button))
				{
					if(g_Config.m_ClDyncamSmoothness)
						g_Config.m_ClDyncamSmoothness = 0;
					else
					{
						g_Config.m_ClDyncamSmoothness = 50;
						g_Config.m_ClDyncamStabilizing = 50;
					}
				}
			}
			Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
			Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
			if(DoSettingsButton_CheckBox(SETTINGS_GENERAL, -1, &g_Config.m_ClAutoswitchWeapons, "general-switch-weapon-pickup", Localize("Switch weapon on pickup"), g_Config.m_ClAutoswitchWeapons, &Button))
				g_Config.m_ClAutoswitchWeapons ^= 1;
			Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
			Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
			if(DoSettingsButton_CheckBox(SETTINGS_GENERAL, -1, &g_Config.m_ClAutoswitchWeaponsOutOfAmmo, "general-switch-weapon-out-of-ammo", Localize("Switch weapon when out of ammo"), g_Config.m_ClAutoswitchWeaponsOutOfAmmo, &Button))
				g_Config.m_ClAutoswitchWeaponsOutOfAmmo ^= 1;
		});
		vCards.back().m_Measure = [GeneralMetrics, IsGeneralDynamicCameraEnabled](float) {
			return ResolveSettingsGeneralGameContentHeight(GeneralMetrics, IsGeneralDynamicCameraEnabled());
		};
		vCards.back().m_MeasureRevision = IsGeneralDynamicCameraEnabled() ? 1 : 0;
		vCards.back().m_PreLayoutInput = [this, GeneralMetrics, IsGeneralDynamicCameraEnabled](CUIRect Content) {
			if(m_MenuTextPlanCollecting)
				return false;
			CUIRect Button;
			Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
			if(!Ui()->DoButtonLogic(&g_Config.m_ClDyncam, 0, &Button, BUTTONFLAG_LEFT))
				return false;
			if(IsGeneralDynamicCameraEnabled())
			{
				g_Config.m_ClDyncam = 0;
				g_Config.m_ClMouseFollowfactor = 0;
			}
			else
				g_Config.m_ClDyncam = 1;
			return true;
		};

		AddCard(LanguageSpec, GeneralLanguageListHeight, [this, GeneralMetrics, GeneralLanguageListHeight](CUIRect Content) {
			Content.h = std::min(Content.h, GeneralLanguageListHeight);
			PrepareLanguagePageCache(Content.w, false);
			RenderLanguageSelection(Content, &GeneralMetrics);
		});

		const float ClientContentHeight = ResolveSettingsGeneralClientContentHeight(GeneralMetrics, GeneralThemeListHeight);
		AddCard(ClientSpec, ClientContentHeight, [this, DoNumericField, GeneralMetrics, GeneralThemeListHeight](CUIRect Content) {
			CUIRect Button;
			char aBuf[128 + IO_MAX_PATH_LENGTH];
			Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
			if(DoSettingsButton_CheckBox(SETTINGS_GENERAL, -1, &g_Config.m_ClSkipStartMenu, "general-skip-main-menu", Localize("Skip the main menu"), g_Config.m_ClSkipStartMenu, &Button))
				g_Config.m_ClSkipStartMenu ^= 1;
			Content.HSplitTop(GeneralMetrics.m_SectionGap, nullptr, &Content);
			Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
			str_copy(aBuf, " ");
			str_append(aBuf, Localize("Hz", "Hertz"));
			DoNumericField("general-refresh-rate", &g_Config.m_ClRefreshRate, &g_Config.m_ClRefreshRate, Button, Localize("Update Rate"), 10, 10000, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE, aBuf);
			Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
			Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
			static int s_LowerRefreshRate;
			if(DoSettingsButton_CheckBox(SETTINGS_GENERAL, -1, &s_LowerRefreshRate, "general-lower-refresh-rate", Localize("Save power by lowering update rate (higher input latency)"), g_Config.m_ClRefreshRate <= 480 && g_Config.m_ClRefreshRate != 0, &Button))
				g_Config.m_ClRefreshRate = g_Config.m_ClRefreshRate > 480 || g_Config.m_ClRefreshRate == 0 ? 480 : 0;
			Content.HSplitTop(GeneralMetrics.m_SectionGap, nullptr, &Content);
			static CButtonContainer s_SettingsButtonId, s_SavesButtonId, s_ConfigButtonId, s_ThemesButtonId;
			const auto DoOpenButton = [this, GeneralMetrics](CButtonContainer &Id, const char *pTextId, const char *pText, const char *pPath, bool CreateDirectory, const char *pTooltip, const CUIRect &ButtonRect) {
				if(DoSettingsButton_Menu(SETTINGS_GENERAL, -1, -1, &Id, pTextId, pText, 0, &ButtonRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), 0.0f, GeneralMetrics.m_BodySize))
				{
					char aPath[IO_MAX_PATH_LENGTH];
					Storage()->GetCompletePath(IStorage::TYPE_SAVE, pPath, aPath, sizeof(aPath));
					if(CreateDirectory)
						Storage()->CreateFolder(pPath, IStorage::TYPE_SAVE);
					Client()->ViewFile(aPath);
				}
				GameClient()->m_Tooltips.DoToolTip(&Id, &ButtonRect, pTooltip);
			};
			for(int RowIndex = 0; RowIndex < 2; ++RowIndex)
			{
				CUIRect Row, LeftButton, RightButton;
				Content.HSplitTop(GeneralMetrics.m_ButtonHeight, &Row, &Content);
				Row.VSplitMid(&LeftButton, &RightButton, GeneralMetrics.m_LineSpacing);
				if(RowIndex == 0)
				{
					DoOpenButton(s_SettingsButtonId, "general-settings-file", Localize("Settings file"), s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, false, Localize("Open the settings file"), LeftButton);
					DoOpenButton(s_SavesButtonId, "general-saves-file", Localize("Saves file"), SAVES_FILE, false, Localize("Open the saves file"), RightButton);
				}
				else
				{
					DoOpenButton(s_ConfigButtonId, "general-config-directory", Localize("Config directory"), "", false, Localize("Open the directory that contains the configuration and user files"), LeftButton);
					DoOpenButton(s_ThemesButtonId, "general-themes-directory", Localize("Themes directory"), "themes", true, Localize("Open the directory to add custom themes"), RightButton);
				}
				if(RowIndex == 0)
					Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
			}
			Content.HSplitTop(GeneralMetrics.m_SectionGap, nullptr, &Content);
			Content.h = std::min(Content.h, GeneralThemeListHeight);
			RenderThemeSelection(Content, &GeneralMetrics);
		});

		SSettingsCardDefinition RecordingDefinition;
		RecordingDefinition.m_Spec = RecordingSpec;
		RecordingDefinition.m_MeasureRevision =
			((uint64_t)(g_Config.m_ClAutoDemoRecord != 0) << 0) |
			((uint64_t)(g_Config.m_ClAutoScreenshot != 0) << 1) |
			((uint64_t)(g_Config.m_ClAutoStatboardScreenshot != 0) << 2) |
			((uint64_t)(g_Config.m_ClAutoCSV != 0) << 3);
		RecordingDefinition.m_Measure = [GeneralMetrics](float) {
			const int EnabledRows =
				(g_Config.m_ClAutoDemoRecord != 0) +
				(g_Config.m_ClAutoScreenshot != 0) +
				(g_Config.m_ClAutoStatboardScreenshot != 0) +
				(g_Config.m_ClAutoCSV != 0);
			return 4.0f * GeneralMetrics.m_RowStep + EnabledRows * (GeneralMetrics.m_RowStep + GeneralMetrics.m_LineSpacing);
		};
		RecordingDefinition.m_VisibilityController = true;
		RecordingDefinition.m_PreLayoutInput = [this, GeneralMetrics](CUIRect Content) {
			if(m_MenuTextPlanCollecting)
				return false;
			bool Changed = false;
			CUIRect Button;
			const auto ProcessToggle = [this, &Content, &Button, &Changed, GeneralMetrics](int *pEnabled) {
				Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
				if(Ui()->DoButtonLogic(pEnabled, 0, &Button, BUTTONFLAG_LEFT))
				{
					*pEnabled ^= 1;
					Changed = true;
				}
				Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
				if(*pEnabled)
				{
					Content.HSplitTop(GeneralMetrics.m_LineHeight, nullptr, &Content);
					Content.HSplitTop(GeneralMetrics.m_SectionGap, nullptr, &Content);
				}
			};
			ProcessToggle(&g_Config.m_ClAutoDemoRecord);
			ProcessToggle(&g_Config.m_ClAutoScreenshot);
			ProcessToggle(&g_Config.m_ClAutoStatboardScreenshot);
			ProcessToggle(&g_Config.m_ClAutoCSV);
			return Changed;
		};
		RecordingDefinition.m_Render = [this, DoNumericField, GeneralMetrics](CUIRect Content) {
			CUIRect Button;
			const auto DoAutoRecord = [this, &Content, &Button, DoNumericField, GeneralMetrics](int *pEnabled, int *pMax, const char *pToggleId, const char *pToggleText, const char *pMaxId, const char *pMaxText) {
				Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
				DoSettingsButton_CheckBox(SETTINGS_GENERAL, -1, -1, pEnabled, pToggleId, pToggleText, *pEnabled, &Button, SLabelProperties{}, false);
				Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
				if(*pEnabled)
				{
					Content.HSplitTop(GeneralMetrics.m_LineHeight, &Button, &Content);
					DoNumericField(pMaxId, pMax, pMax, Button, pMaxText, 1, 1000, CUi::SCROLLBAR_OPTION_INFINITE);
					Content.HSplitTop(GeneralMetrics.m_SectionGap, nullptr, &Content);
				}
			};
			DoAutoRecord(&g_Config.m_ClAutoDemoRecord, &g_Config.m_ClAutoDemoMax, "general-auto-demo-record", Localize("Automatically record demos"), "general-auto-demo-max", Localize("Max demos"));
			DoAutoRecord(&g_Config.m_ClAutoScreenshot, &g_Config.m_ClAutoScreenshotMax, "general-auto-screenshot", Localize("Automatically take game over screenshot"), "general-auto-screenshot-max", Localize("Max Screenshots"));
			DoAutoRecord(&g_Config.m_ClAutoStatboardScreenshot, &g_Config.m_ClAutoStatboardScreenshotMax, "general-auto-statboard-screenshot", Localize("Automatically take statboard screenshot"), "general-auto-statboard-screenshot-max", Localize("Max Screenshots"));
			DoAutoRecord(&g_Config.m_ClAutoCSV, &g_Config.m_ClAutoCSVMax, "general-auto-csv", Localize("Automatically create statboard csv"), "general-auto-csv-max", Localize("Max CSVs"));
		};
		vCards.push_back(std::move(RecordingDefinition));

		// 配置文件卡：原 TClient 信息 tab 的入口，并入常规页。
		SSettingsCardDefinition ConfigFilesDefinition;
		ConfigFilesDefinition.m_Spec = {pConfigFilesDefault->m_pStableId, Localize(pConfigFilesDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pConfigFilesDefault)};
		ConfigFilesDefinition.m_Measure = [GeneralMetrics](float) {
			return ResolveSettingsRowsHeight(2, GeneralMetrics.m_ButtonHeight, GeneralMetrics.m_LineSpacing);
		};
		ConfigFilesDefinition.m_Render = [this, GeneralMetrics](CUIRect Content) {
			static CButtonContainer s_Config, s_Profiles, s_Warlist, s_Chatbinds;
			const auto OpenFile = [this](ConfigDomain Domain) {
				char aBuf[IO_MAX_PATH_LENGTH];
				Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[Domain].m_aConfigPath, aBuf, sizeof(aBuf));
				Client()->ViewFile(aBuf);
			};
			const auto DoFileButton = [this, GeneralMetrics, &OpenFile](CButtonContainer &Id, const char *pTextId, const char *pText, const CUIRect &ButtonRect, ConfigDomain Domain) {
				if(DoSettingsButton_Menu(SETTINGS_GENERAL, -1, -1, &Id, pTextId, pText, 0, &ButtonRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), 0.0f, GeneralMetrics.m_BodySize))
					OpenFile(Domain);
			};
			CUIRect Row, LeftButton, RightButton;
			Content.HSplitTop(GeneralMetrics.m_ButtonHeight, &Row, &Content);
			Row.VSplitMid(&LeftButton, &RightButton, GeneralMetrics.m_LineSpacing);
			DoFileButton(s_Config, "tclient-files-qmclient-settings", Localize("QmClient Settings"), LeftButton, ConfigDomain::QMCLIENT);
			DoFileButton(s_Profiles, "tclient-files-profiles", Localize("Profiles"), RightButton, ConfigDomain::TCLIENTPROFILES);
			Content.HSplitTop(GeneralMetrics.m_LineSpacing, nullptr, &Content);
			Content.HSplitTop(GeneralMetrics.m_ButtonHeight, &Row, &Content);
			Row.VSplitMid(&LeftButton, &RightButton, GeneralMetrics.m_LineSpacing);
			DoFileButton(s_Warlist, "tclient-files-warlist", Localize("War List"), LeftButton, ConfigDomain::TCLIENTWARLIST);
			DoFileButton(s_Chatbinds, "tclient-files-chatbinds", Localize("Chat Binds"), RightButton, ConfigDomain::TCLIENTCHATBINDS);
		};
		vCards.push_back(std::move(ConfigFilesDefinition));
	};
	const uint64_t GeneralToggleMask =
		((uint64_t)(g_Config.m_ClAutoDemoRecord != 0) << 0) |
		((uint64_t)(g_Config.m_ClAutoScreenshot != 0) << 1) |
		((uint64_t)(g_Config.m_ClAutoStatboardScreenshot != 0) << 2) |
		((uint64_t)(g_Config.m_ClAutoCSV != 0) << 3) |
		((uint64_t)(IsGeneralDynamicCameraEnabled() ? 1 : 0) << 4);
	const uint64_t GeneralLayoutRevision = ResolveSettingsGeneralLayoutRevision(RenderOnly, GeneralToggleMask, MainView.h, (int)g_Localization.Languages().size(), (int)GameClient()->m_MenuBackground.GetThemes().size());
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, GeneralLayoutRevision);

	const SQmScrollRequest ScrollRequest{EQmScrollProfile::SETTINGS_OUTER};
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest, UiScale, 0.0f);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	CQmScrollState &ScrollState = s_GeneralSettingsScrollRegion.State();
	// Deck 通过同一个 region 消费该状态；显式取得它以固定页面唯一的滚动状态所有权。
	(void)ScrollState;
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = Ui()->MouseX();
	InputState.m_MouseY = Ui()->MouseY();
	InputState.m_MousePressed = Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = Ui()->MouseButton(0);
	InputState.m_MouseReleased = !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = Input()->ModifierIsPressed();
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = RenderOnly ? nullptr : &ScrollParams;
	const SSettingsCardDeckResult DeckResult = SettingsCardDeckForRenderPass().RenderCached(GeneralCardCtx, GeneralPage, "general", DefinitionsRevision, BuildDefinitions, SettingsCardOrderModelForRenderPass(), RenderOnly ? nullptr : &s_GeneralSettingsScrollRegion, InputState, SettingsCardMotionSpec(), GeneralVisualOptions);
	if(!RenderOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();

	LogSettingsSectionPerf(Client(), SETTINGS_GENERAL, -1, "general_page", RenderTimer.ElapsedMs(), "static_text", TextStats.Stats().m_New, TextStats.Stats().m_Reused);
	LogPerfStage(Client(), "general_page_total", RenderTimer.ElapsedMs(), false, "page=general");
}

void CMenus::SetNeedSendInfo()
{
	SetNeedSendInfo(m_Dummy);
}

void CMenus::SetNeedSendInfo(bool Dummy)
{
	bool &NeedSendInfo = Dummy ? m_NeedSendDummyinfo : m_NeedSendinfo;
	NeedSendInfo = true;
}

CUi::EPopupMenuFunctionResult CMenus::PopupSettingsCountrySelection(void *pContext, CUIRect View, bool Active)
{
	SPopupSettingsCountrySelectionContext *pPopupContext = static_cast<SPopupSettingsCountrySelectionContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;
	CUi *pUi = pMenus->Ui();

	static CListBox s_ListBox;
	static int64_t s_PopupOpenTime = 0;
	static std::string s_LastFilter;
	s_ListBox.SetActive(Active);
	s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);
	s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);

	if(pPopupContext->m_New)
	{
		pPopupContext->m_New = false;
		pPopupContext->m_FilterInput.Clear();
		s_ListBox.ScrollToSelected();
		s_PopupOpenTime = time_get();
		s_LastFilter.clear();
	}
	else if(s_LastFilter != pPopupContext->m_FilterInput.GetString())
	{
		s_LastFilter = pPopupContext->m_FilterInput.GetString();
		s_PopupOpenTime = time_get();
	}

	IUiContext HeaderCtx;
	HeaderCtx.m_pUi = pUi;
	static ui_widget::SSecondaryPanelLabel s_Title;
	static CButtonContainer s_CloseButton;
	ui_widget::CSecondaryPanel Panel(HeaderCtx, View, Active, ui_widget::ResolveSecondaryPanelMetrics(pUi->Screen()->w), {});
	if(Panel.Header(s_Title, s_CloseButton, Localize("Choose country flag")))
		return CUi::POPUP_CLOSE_CURRENT_AND_DESCENDANTS;
	CUIRect SearchRect, GridArea = Panel.ContentRect();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(pUi->Screen()->w);
	GridArea.HSplitTop(Metrics.m_LineHeight, &SearchRect, &GridArea);
	GridArea.HSplitTop(Metrics.m_LineSpacing, nullptr, &GridArea);

	IUiContext SearchCtx;
	SearchCtx.m_pUi = pUi;
	SearchCtx.m_pAnim = &pMenus->GameClient()->UiRuntimeV2()->AnimRuntime();
	SearchCtx.m_pTree = &pMenus->GameClient()->UiRuntimeV2()->Tree();
	SearchCtx.m_ScopeHash = MakeUiScopeHash("settings_country_flag_popup_search");
	SearchCtx.m_FrameDt = pMenus->GameClient()->UiRuntimeV2()->FrameDt();
	ui_widget::SInputFieldOptions SearchOptions;
	SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	SearchOptions.m_pPlaceholder = Localize("Search country flag…");
	SearchOptions.m_Clearable = true;
	ui_widget::InputField(SearchCtx, &pPopupContext->m_FilterInput, SearchRect, SearchOptions);

	struct SFilteredFlag
	{
		const CCountryFlags::CCountryFlag *m_pEntry;
		std::optional<std::pair<int, int>> m_Match;
	};
	static std::vector<SFilteredFlag> s_vFiltered;
	s_vFiltered.clear();
	for(size_t i = 0; i < pMenus->GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = pMenus->GameClient()->m_CountryFlags.GetByIndex(i);
		if(!pPopupContext->m_FilterInput.IsEmpty())
		{
			const char *pMatchEnd = nullptr;
			const char *pMatchStart = str_utf8_find_nocase(Entry.m_aCountryCodeString, pPopupContext->m_FilterInput.GetString(), &pMatchEnd);
			if(pMatchStart != nullptr)
			{
				s_vFiltered.push_back({&Entry, std::make_pair((int)(pMatchStart - Entry.m_aCountryCodeString), (int)(pMatchEnd - pMatchStart))});
			}
		}
		else
		{
			s_vFiltered.push_back({&Entry, std::nullopt});
		}
	}

	const int Columns = std::clamp((int)(GridArea.w / 54.0f), 1, 14);
	int SelectedIndex = -1;
	for(size_t i = 0; i < s_vFiltered.size(); ++i)
	{
		if(s_vFiltered[i].m_pEntry->m_CountryCode == pPopupContext->m_Selection)
		{
			SelectedIndex = (int)i;
			break;
		}
	}

	s_ListBox.DoStart(44.0f, s_vFiltered.size(), Columns, 1, SelectedIndex, &GridArea, false);

	for(size_t i = 0; i < s_vFiltered.size(); ++i)
	{
		const SFilteredFlag &Filtered = s_vFiltered[i];
		const CCountryFlags::CCountryFlag *pEntry = Filtered.m_pEntry;
		const bool IsSelected = pEntry->m_CountryCode == pPopupContext->m_Selection;
		const CListboxItem Item = s_ListBox.DoNextItem(pEntry, IsSelected);
		if(!Item.m_Visible)
			continue;

		if(IsSelected)
		{
			DrawRoundedSurface(pUi, Item.m_Rect, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::BORDER_FOCUS, ui_token::radius::BASE, 1.0f);
		}
		else if(pUi->MouseInside(&Item.m_Rect))
		{
			DrawRoundedSurface(pUi, Item.m_Rect, ui_token::color::LIST_ITEM_HOVER, ColorRGBA(0, 0, 0, 0), ui_token::radius::BASE);
		}

		CUIRect FlagRect, Label;
		Item.m_Rect.Margin(5.0f, &FlagRect);
		FlagRect.HSplitBottom(12.0f, &FlagRect, &Label);
		Label.HSplitTop(2.0f, nullptr, &Label);
		const float OldWidth = FlagRect.w;
		FlagRect.w = FlagRect.h * 2.0f;
		FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
		int64_t FlagAnimStartTime = s_PopupOpenTime;
		if(s_PopupOpenTime > 0)
		{
			const int Col = (int)(i % Columns);
			const int Row = (int)(i / Columns) % 6;
			const float StaggerDelay = Col * 0.006f + Row * 0.015f;
			FlagAnimStartTime = s_PopupOpenTime + (int64_t)(StaggerDelay * time_freq());
		}
		pMenus->GameClient()->m_CountryFlags.Render(pEntry->m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h, FlagAnimStartTime);

		SLabelProperties Props;
		if(Filtered.m_Match.has_value())
		{
			const auto [MatchStart, MatchLen] = Filtered.m_Match.value();
			Props.m_vColorSplits.emplace_back(MatchStart, MatchLen, ui_token::color::ACCENT_PRIMARY);
		}
		pUi->DoLabel(&Label, pEntry->m_aCountryCodeString, 10.0f, TEXTALIGN_MC, Props);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(NewSelected >= 0 && (size_t)NewSelected < s_vFiltered.size())
	{
		pPopupContext->m_Selection = s_vFiltered[NewSelected].m_pEntry->m_CountryCode;
	}
	if(s_ListBox.WasItemSelected() || s_ListBox.WasItemActivated())
	{
		if(NewSelected >= 0 && (size_t)NewSelected < s_vFiltered.size() && QmCommitCountrySelection(pPopupContext->m_pCountry, pPopupContext->m_Selection))
		{
			pMenus->SetNeedSendInfo();
			pMenus->m_TeeEntranceStartTime = time_get();
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::RenderSettingsTeeIdentity(CUIRect MainView, CUIRect *pFlagButton, float BodySize, bool StackFields)
{
	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;
	int *pCountry = nullptr;
	if(!m_Dummy)
	{
		pCountry = &g_Config.m_PlayerCountry;
		s_NameInput.SetBuffer(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_NameInput.SetEmptyText(Client()->PlayerName());
		s_ClanInput.SetBuffer(g_Config.m_PlayerClan, sizeof(g_Config.m_PlayerClan));
	}
	else
	{
		pCountry = &g_Config.m_ClDummyCountry;
		s_NameInput.SetBuffer(g_Config.m_ClDummyName, sizeof(g_Config.m_ClDummyName));
		s_NameInput.SetEmptyText(Client()->DummyName());
		s_ClanInput.SetBuffer(g_Config.m_ClDummyClan, sizeof(g_Config.m_ClDummyClan));
	}

	SSettingsContentMetrics Metrics = CurrentSettingsContentMetrics();
	Metrics.m_InputHeight = StackFields ? Metrics.m_InputHeight : MainView.h;
	const SSettingsTeeIdentityFieldsLayout Fields = ResolveSettingsTeeIdentityFieldsLayout(MainView, Metrics, StackFields);
	CUIRect NameLabel = Fields.m_NameLabel;
	CUIRect NameInputRect = Fields.m_NameInput;
	CUIRect ClanLabel = Fields.m_ClanLabel;
	CUIRect ClanInput = Fields.m_ClanInput;
	CUIRect FlagButton = Fields.m_FlagButton;

	CUIElement &NameLabelElement = SettingsTextElement(SETTINGS_TEE, -1, "tee-name-label");
	DoSettingsLabelStreamed(NameLabelElement, &NameLabel, Localize("Name"), BodySize, TEXTALIGN_ML);
	CUIElement &ClanLabelElement = SettingsTextElement(SETTINGS_TEE, -1, "tee-clan-label");
	DoSettingsLabelStreamed(ClanLabelElement, &ClanLabel, Localize("Clan"), BodySize, TEXTALIGN_ML);
	IUiContext TeeIdentityTextInputCtx;
	TeeIdentityTextInputCtx.m_pUi = Ui();
	TeeIdentityTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	TeeIdentityTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	TeeIdentityTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tee_identity_text_inputs");
	TeeIdentityTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	if(ui_widget::InputField(TeeIdentityTextInputCtx, &s_NameInput, NameInputRect, Client()->PlayerName(), BodySize))
		SetNeedSendInfo(m_Dummy);
	if(ui_widget::InputField(TeeIdentityTextInputCtx, &s_ClanInput, ClanInput, "", BodySize))
		SetNeedSendInfo();

	static CButtonContainer s_FlagButton;
	if(DoButton_Menu(&s_FlagButton, "", 0, &FlagButton))
	{
		static SPopupMenuId s_PopupCountryId;
		static SPopupSettingsCountrySelectionContext s_PopupCountryContext;
		s_PopupCountryContext.m_pMenus = this;
		s_PopupCountryContext.m_pCountry = pCountry;
		s_PopupCountryContext.m_Selection = *pCountry;
		s_PopupCountryContext.m_New = true;
		const SPopupMenuProperties PopupProps = ui_widget::SecondaryPanelProperties();
		const CUIRect PanelRect = ResolveSettingsSecondaryPanelRect(*Ui()->Screen());
		Ui()->DoPopupMenu(&s_PopupCountryId, FlagButton.x, FlagButton.y + FlagButton.h, PanelRect.w, PanelRect.h, &s_PopupCountryContext, PopupSettingsCountrySelection, PopupProps);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_FlagButton, &FlagButton, Localize("Choose country flag"));

	CUIRect FlagIcon = FlagButton;
	const float OldWidth = FlagIcon.w;
	FlagIcon.w = FlagIcon.h * 2.0f;
	FlagIcon.x += (OldWidth - FlagIcon.w) / 2.0f;
	GameClient()->m_CountryFlags.Render(*pCountry, ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_FlagButton ? 1.0f : 0.85f), FlagIcon.x, FlagIcon.y, FlagIcon.w, FlagIcon.h, m_TeeEntranceStartTime);
	if(pFlagButton != nullptr)
		*pFlagButton = FlagButton;
}

void CMenus::RenderSettingsPlayer(CUIRect MainView)
{
	CPerfTimer RenderTimer;
	CScopedSettingsTextPerfStats TextStats(this);
	const SSettingsContentMetrics PlayerMetrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = PlayerMetrics.m_UiScale;
	const float BodySize = PlayerMetrics.m_BodySize;
	const IUiContext PlayerCardCtx = SettingsUiContext("settings_player", UiScale);
	const SSettingsCardDeckVisualOptions PlayerVisualOptions = SettingsCardDeckVisualOptions();
	static CScrollRegion s_PlayerSettingsScrollRegion;
	const qm_card_registry::SCardDefault *pIdentityDefault = qm_card_registry::FindByStableId("deck:player-identity");
	const qm_card_registry::SCardDefault *pCountryDefault = qm_card_registry::FindByStableId("deck:player-country");
	dbg_assert(pIdentityDefault != nullptr && pCountryDefault != nullptr, "player settings cards must be registered");
	if(pIdentityDefault == nullptr || pCountryDefault == nullptr)
		return;

	CUIRect TabBar, PlayerTab, DummyTab, ChangeInfo;
	const SSettingsSubTabLayoutFrame PlayerSubTabs = ResolveSettingsSubTabLayout(MainView, UiScale);
	TabBar = PlayerSubTabs.m_TabBarRect;
	MainView = PlayerSubTabs.m_ContentRect;
	TabBar.VSplitMid(&TabBar, &ChangeInfo, 20.0f);
	TabBar.VSplitMid(&PlayerTab, &DummyTab);
	static CButtonContainer s_PlayerTabButton;
	static CButtonContainer s_DummyTabButton;
	// 胶囊 Tabbar：容器与滑块先画，页签文字随后，滑块压在文字之下。
	const CUIRect aPlayerTabSlots[] = {PlayerTab, DummyTab};
	ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash("settings_player_dummy_tabs_capsule"), aPlayerTabSlots, std::size(aPlayerTabSlots), m_Dummy ? 1 : 0, SettingsCapsuleTabBarStyle());
	if(DoButton_MenuTab(&s_PlayerTabButton, Localize("Player"), !m_Dummy, &PlayerTab, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true))
	{
		m_Dummy = false;
		m_TeeEntranceStartTime = time_get();
	}
	if(DoButton_MenuTab(&s_DummyTabButton, Localize("Dummy"), m_Dummy, &DummyTab, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true))
	{
		m_Dummy = true;
		m_TeeEntranceStartTime = time_get();
	}
	// 子 Tab 已由设置壳层的 Card Deck 统一处理入场；页面内部不再叠加横向位移动效。
	const auto DrawAnimatedContent = [](CUIRect Content, auto &&DrawContent) { DrawContent(Content); };

	int *pCountry = m_Dummy ? &g_Config.m_ClDummyCountry : &g_Config.m_PlayerCountry;
	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;
	const IUiContext PlayerIdentityTextInputCtx = SettingsUiContext("settings_player_identity_text_inputs", UiScale);
	if(!m_Dummy)
	{
		s_NameInput.SetBuffer(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_NameInput.SetEmptyText(Client()->PlayerName());
		s_ClanInput.SetBuffer(g_Config.m_PlayerClan, sizeof(g_Config.m_PlayerClan));
	}
	else
	{
		s_NameInput.SetBuffer(g_Config.m_ClDummyName, sizeof(g_Config.m_ClDummyName));
		s_NameInput.SetEmptyText(Client()->DummyName());
		s_ClanInput.SetBuffer(g_Config.m_ClDummyClan, sizeof(g_Config.m_ClDummyClan));
	}

	static CLineInputBuffered<25> s_FlagFilterInput;
	class CCountryFlagEntry
	{
	public:
		const CCountryFlags::CCountryFlag *m_pFlag;
		std::optional<std::pair<int, int>> m_NameMatch;
	};
	static std::vector<CCountryFlagEntry> s_vFilteredFlags;
	s_vFilteredFlags.clear();
	s_vFilteredFlags.reserve(GameClient()->m_CountryFlags.Num());
	for(size_t i = 0; i < GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = GameClient()->m_CountryFlags.GetByIndex(i);
		if(!s_FlagFilterInput.IsEmpty())
		{
			const char *pNameMatchEnd;
			const char *pNameMatchStart = str_utf8_find_nocase(Entry.m_aCountryCodeString, s_FlagFilterInput.GetString(), &pNameMatchEnd);
			if(pNameMatchStart != nullptr)
			{
				s_vFilteredFlags.emplace_back(&Entry, std::make_pair<int, int>(pNameMatchStart - Entry.m_aCountryCodeString, pNameMatchEnd - pNameMatchStart));
			}
		}
		else
		{
			s_vFilteredFlags.emplace_back(&Entry, std::nullopt);
		}
	}
	const IUiContext PlayerFlagSearchCtx = SettingsUiContext("settings_player_flag_search", UiScale);

	const bool RenderOnly = Ui()->RenderOnly();
	const auto BuildDefinitions = [this, pIdentityDefault, pCountryDefault, DrawAnimatedContent, PlayerIdentityTextInputCtx, PlayerFlagSearchCtx, pCountry, UiScale, BodySize, PlayerMetrics](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(2);
		const SSettingsCardSpec IdentitySpec{pIdentityDefault->m_pStableId, Localize(pIdentityDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pIdentityDefault)};
		const SSettingsCardSpec CountrySpec{pCountryDefault->m_pStableId, Localize(pCountryDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pCountryDefault)};
		const auto AddCard = [&vCards](const SSettingsCardSpec &Spec, float ContentHeight, FSettingsCardRender Render) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			Definition.m_Measure = [ContentHeight](float) { return ContentHeight; };
			Definition.m_Render = Render;
			vCards.push_back(Definition);
		};
		AddCard(IdentitySpec, ResolveSettingsRowsHeight(2, PlayerMetrics.m_LineHeight, PlayerMetrics.m_LineSpacing), [this, DrawAnimatedContent, PlayerIdentityTextInputCtx, UiScale, BodySize, PlayerMetrics](CUIRect Content) {
			CUIRect Label, NameRow, ClanRow;
			char aBuf[128];
			Content.HSplitTop(PlayerMetrics.m_LineHeight, &NameRow, &Content);
			DrawAnimatedContent(NameRow, [this, &Label, &PlayerIdentityTextInputCtx, &aBuf, UiScale, BodySize](CUIRect Row) {
				Row.VSplitLeft(80.0f * UiScale, &Label, &Row);
				Row.VSplitLeft(150.0f * UiScale, &Row, nullptr);
				str_format(aBuf, sizeof(aBuf), "%s:", Localize("Name"));
				Ui()->DoLabel(&Label, aBuf, BodySize, TEXTALIGN_ML);
				if(ui_widget::InputField(PlayerIdentityTextInputCtx, &s_NameInput, Row, Client()->PlayerName(), BodySize))
					SetNeedSendInfo(m_Dummy);
			});
			Content.HSplitTop(PlayerMetrics.m_LineSpacing, nullptr, &Content);
			Content.HSplitTop(PlayerMetrics.m_LineHeight, &ClanRow, &Content);
			DrawAnimatedContent(ClanRow, [this, &Label, &PlayerIdentityTextInputCtx, &aBuf, UiScale, BodySize](CUIRect Row) {
				Row.VSplitLeft(80.0f * UiScale, &Label, &Row);
				Row.VSplitLeft(150.0f * UiScale, &Row, nullptr);
				str_format(aBuf, sizeof(aBuf), "%s:", Localize("Clan"));
				Ui()->DoLabel(&Label, aBuf, BodySize, TEXTALIGN_ML);
				if(ui_widget::InputField(PlayerIdentityTextInputCtx, &s_ClanInput, Row, "", BodySize))
					SetNeedSendInfo();
			});
		});
		AddCard(CountrySpec, 520.0f * UiScale, [this, pCountry, PlayerFlagSearchCtx, UiScale, BodySize](CUIRect Content) {
			const bool MenuUiPerfEnabled = QmPerfEnabled();
			const auto MenuUiStartTime = MenuUiPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
			CUIRect QuickSearch, Label;
			Content.HSplitBottom(20.0f * UiScale, &Content, &QuickSearch);
			Content.HSplitBottom(5.0f * UiScale, &Content, nullptr);
			QuickSearch.VSplitLeft(minimum(220.0f * UiScale, QuickSearch.w), &QuickSearch, nullptr);
			int SelectedOld = -1;
			static CListBox s_ListBox;
			s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);
			s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
			s_ListBox.DoStart(48.0f * UiScale, s_vFilteredFlags.size(), 10, 2, SelectedOld, &Content);
			int VisibleFlags = 0;
			for(size_t i = 0; i < s_vFilteredFlags.size(); i++)
			{
				const CCountryFlagEntry &Entry = s_vFilteredFlags[i];
				if(Entry.m_pFlag->m_CountryCode == *pCountry)
					SelectedOld = i;
				const CListboxItem Item = s_ListBox.DoNextItem(&Entry.m_pFlag->m_CountryCode, SelectedOld >= 0 && (size_t)SelectedOld == i);
				if(!Item.m_Visible)
					continue;
				++VisibleFlags;
				CUIRect FlagRect;
				Item.m_Rect.Margin(5.0f * UiScale, &FlagRect);
				FlagRect.HSplitBottom(12.0f * UiScale, &FlagRect, &Label);
				Label.HSplitTop(2.0f * UiScale, nullptr, &Label);
				const float OldWidth = FlagRect.w;
				FlagRect.w = FlagRect.h * 2.0f;
				FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
				GameClient()->m_CountryFlags.Render(Entry.m_pFlag->m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);
				if(Entry.m_pFlag->m_Texture.IsValid() || Entry.m_pFlag->m_CountryCode == -1)
				{
					SLabelProperties Props;
					Props.m_MaxWidth = Label.w - 5.0f;
					if(Entry.m_NameMatch.has_value())
					{
						const auto [MatchStart, MatchLength] = Entry.m_NameMatch.value();
						Props.m_vColorSplits.emplace_back(MatchStart, MatchLength, ColorRGBA(0.4f, 0.4f, 1.0f, 1.0f));
					}
					Ui()->DoLabel(&Label, Entry.m_pFlag->m_aCountryCodeString, 10.0f * UiScale, TEXTALIGN_MC, Props);
				}
			}
			const int NewSelected = s_ListBox.DoEnd();
			const bool FlagListScrollActive = QmMenuUiScrollPerfActive(s_ListBox.WheelConsumedThisFrame(), s_ListBox.ScrollbarActive(), s_ListBox.ScrollbarAnimating());
			if(FlagListScrollActive)
			{
				StartSettingsPerfScrollWindow("flags_grid_scroll", SettingsPerfContextName(), "settings:player", "none");
				SQmMenuUiFramePerf MenuUiPerf;
				MenuUiPerf.m_pPage = "settings:player";
				MenuUiPerf.m_pOperation = "flags_grid_scroll";
				MenuUiPerf.m_ItemsTotal = (int)s_vFilteredFlags.size();
				MenuUiPerf.m_ItemsVisible = VisibleFlags;
				MenuUiPerf.m_ItemsProcessed = VisibleFlags;
				MenuUiPerf.m_ItemsSkipped = maximum(0, (int)s_vFilteredFlags.size() - VisibleFlags);
				MenuUiPerf.m_UiMs = MenuUiPerfEnabled ? (float)std::chrono::duration<double, std::milli>(time_get_nanoseconds() - MenuUiStartTime).count() : -1.0f;
				QmLogMenuUiFramePerf(MenuUiPerf, Client());
			}
			if(SelectedOld != NewSelected && NewSelected >= 0 && NewSelected < (int)s_vFilteredFlags.size())
			{
				*pCountry = s_vFilteredFlags[NewSelected].m_pFlag->m_CountryCode;
				SetNeedSendInfo();
			}
			ui_widget::SInputFieldOptions FlagSearchOptions;
			FlagSearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
			FlagSearchOptions.m_Clearable = true;
			FlagSearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();
			FlagSearchOptions.m_FontSize = BodySize;
			ui_widget::InputField(PlayerFlagSearchCtx, &s_FlagFilterInput, QuickSearch, FlagSearchOptions);
		});
	};
	const uint64_t PlayerLayoutRevision =
		((uint64_t)(RenderOnly ? 1 : 0) << 63) |
		((uint64_t)(m_Dummy ? 1 : 0) << 62);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, PlayerLayoutRevision);

	const SSettingsPageLayoutFrame PlayerPage = SettingsPageLayout(MainView, UiScale);
	const SQmScrollRequest ScrollRequest{EQmScrollProfile::SETTINGS_OUTER};
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest, UiScale, 0.0f);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	CQmScrollState &ScrollState = s_PlayerSettingsScrollRegion.State();
	// Deck 通过同一个 region 消费该状态；显式取得它以固定页面唯一的滚动状态所有权。
	(void)ScrollState;
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = Ui()->MouseX();
	InputState.m_MouseY = Ui()->MouseY();
	InputState.m_MousePressed = Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = Ui()->MouseButton(0);
	InputState.m_MouseReleased = !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = Input()->ModifierIsPressed();
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = RenderOnly ? nullptr : &ScrollParams;
	const SSettingsCardDeckResult DeckResult = SettingsCardDeckForRenderPass().RenderCached(PlayerCardCtx, PlayerPage, "player", DefinitionsRevision, BuildDefinitions, SettingsCardOrderModelForRenderPass(), RenderOnly ? nullptr : &s_PlayerSettingsScrollRegion, InputState, SettingsCardMotionSpec(), PlayerVisualOptions);
	if(!RenderOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
	LogSettingsSectionPerf(Client(), SETTINGS_PLAYER, -1, "player_page", RenderTimer.ElapsedMs(), "static_text", TextStats.Stats().m_New, TextStats.Stats().m_Reused);
	LogPerfStage(Client(), "player_page_total", RenderTimer.ElapsedMs(), false, "page=player");
}

void CMenus::RenderSettingsTee(CUIRect MainView)
{
	CPerfTimer RenderTimer;
	CScopedSettingsTextPerfStats TextStats(this);
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const bool RenderOnly = Ui()->RenderOnly();
	const SSettingsSubTabLayoutFrame SubTabs = ResolveSettingsSubTabLayout(MainView, Metrics.m_UiScale);
	MainView = SubTabs.m_ContentRect;
	CUIRect TabBar = SubTabs.m_TabBarRect;
	TabBar.w = std::min(TabBar.w, 280.0f * Metrics.m_UiScale);
	CUIRect aTabs[2];
	TabBar.VSplitMid(&aTabs[0], &aTabs[1]);
	static int s_SubTab = 0;
	static CButtonContainer s_aTabs[2];
	const char *apLabels[] = {Localize("Tee"), Localize("Profiles")};
	ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash("settings_tee_sub_tabs_capsule"), aTabs, std::size(aTabs), s_SubTab, SettingsCapsuleTabBarStyle());
	for(int Tab = 0; Tab < 2; ++Tab)
	{
		if(DoButton_MenuTab(&s_aTabs[Tab], apLabels[Tab], s_SubTab == Tab, &aTabs[Tab], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true))
			s_SubTab = Tab;
	}
	if(!m_MenuTextPlanCollecting && !RenderOnly)
	{
		const uint64_t DisplayKey = (static_cast<uint64_t>(SETTINGS_TEE) << 32) | static_cast<uint64_t>(s_SubTab + 1);
		if(m_SettingsCardDeckDisplayState.EnterView(DisplayKey))
		{
			m_SettingsCardDeck.BeginDisplayCycle(++m_SettingsCardDeckDisplayCycle, true);
			m_TeeEntranceStartTime = time_get();
		}
	}
	if(s_SubTab == 1)
	{
		CommitSettingsTeeSkinEdits();
		RenderSettingsTClientProfiles(MainView);
		return;
	}

	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, Metrics.m_UiScale);
	const IUiContext CardCtx = SettingsUiContext("settings_tee", Metrics.m_UiScale);
	SSettingsCardDeckVisualOptions VisualOptions = SettingsCardDeckVisualOptions();
	VisualOptions.m_LeadingFullWidthCards = 2;
	qm_card_catalog::SQmCardBuildContext CardBuild;
	CardBuild.m_pMenus = this;
	CardBuild.m_ReadOnly = RenderOnly;
	CardBuild.m_Page = Page;
	CardBuild.m_Metrics = Metrics;
	CardBuild.m_LabelWidth = 142.0f * Metrics.m_UiScale;
	CardBuild.m_UiContext = CardCtx;
	const auto BuildDefinitions = [CardBuild](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::TeeCardStableIds(), vCards);
	};
	const uint64_t ContentRevision = (static_cast<uint64_t>(MainView.h * 100.0f) << 1) | static_cast<uint64_t>(RenderOnly);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, ContentRevision);
	static CScrollRegion s_ScrollRegion;
	const SQmScrollRequest ScrollRequest{EQmScrollProfile::SETTINGS_OUTER};
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(ScrollRequest, Metrics.m_UiScale, 0.0f));
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = Ui()->MouseX();
	InputState.m_MouseY = Ui()->MouseY();
	InputState.m_MousePressed = Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = Ui()->MouseButton(0);
	InputState.m_MouseReleased = !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = Input()->ModifierIsPressed();
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = RenderOnly ? nullptr : &ScrollParams;
	const SSettingsCardDeckResult Result = SettingsCardDeckForRenderPass().RenderCached(CardCtx, Page, "tee", DefinitionsRevision, BuildDefinitions, SettingsCardOrderModelForRenderPass(), RenderOnly ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), VisualOptions);
	if(!RenderOnly && Result.m_OrderChanged)
		SaveSettingsCardOrderModel();
	LogSettingsSectionPerf(Client(), SETTINGS_TEE, -1, "tee_page", RenderTimer.ElapsedMs(), "static_text", TextStats.Stats().m_New, TextStats.Stats().m_Reused);
	LogPerfStage(Client(), "tee_page_total", RenderTimer.ElapsedMs(), false, "page=tee");
}

void CMenus::RenderSettingsGraphics(CUIRect MainView)
{
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

	const float ViewWidth = MainView.w;
	const SSettingsContentMetrics GraphicsMetrics = ResolveSettingsContentMetrics(ViewWidth);
	const float UiScale = GraphicsMetrics.m_UiScale;
	const float BodySize = GraphicsMetrics.m_BodySize;

	static CScrollRegion s_GraphicsScrollRegion;
	const char *pDeckTab = "graphics";
	const SSettingsPageLayoutFrame GraphicsPage = SettingsPageLayout(MainView, UiScale);
	const IUiContext GraphicsCardCtx = SettingsUiContext("settings_graphics", UiScale);
	const SSettingsCardDeckVisualOptions GraphicsVisualOptions = SettingsCardDeckVisualOptions();
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
	constexpr int GpuInfoFixedRows = 4; // 当前渲染器、硬件标题、选择说明与系统设置按钮。
#else
	constexpr int GpuInfoFixedRows = 2; // 当前渲染器与硬件标题。
#endif
	const int GpuRowCount = GpuList.m_CanSelect ? (GpuList.m_vGpus.size() > 1 ? 1 : 0) : GpuInfoFixedRows + std::max(1, static_cast<int>(GpuList.m_vGpus.size()));
	const int GraphicsBackendRowCount = (FoundBackendCount > 1 ? 1 : 0) + GpuRowCount;
	const qm_card_registry::SCardDefault *pDisplayDefault = qm_card_registry::FindByStableId("deck:graphics-display");
	const qm_card_registry::SCardDefault *pVisualDefault = qm_card_registry::FindByStableId("deck:graphics-visual");
	const qm_card_registry::SCardDefault *pIconsDefault = qm_card_registry::FindByStableId("deck:graphics-icons");
	const qm_card_registry::SCardDefault *pModesDefault = qm_card_registry::FindByStableId("deck:graphics-modes");
	const qm_card_registry::SCardDefault *pInteractionDefault = qm_card_registry::FindByStableId("deck:graphics-interaction");
	dbg_assert(pDisplayDefault != nullptr && pVisualDefault != nullptr && pIconsDefault != nullptr && pModesDefault != nullptr && pInteractionDefault != nullptr, "graphics settings cards must be registered");
	if(pDisplayDefault == nullptr || pVisualDefault == nullptr || pIconsDefault == nullptr || pModesDefault == nullptr || pInteractionDefault == nullptr)
		return;

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
		const SSettingsCardSpec DisplaySpec{pDisplayDefault->m_pStableId, Localize(pDisplayDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDisplayDefault)};
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
		const int NewWindowMode = DoSettingsDropDown(&WindowModeDropDown, OldWindowMode, apWindowModes, std::size(apWindowModes), s_WindowModeDropDownState, WindowModeDropDownProps);
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
				const int NewScreen = DoSettingsDropDown(&ScreenDropDown, g_Config.m_GfxScreen, s_vpScreenNames.data(), s_vpScreenNames.size(), s_ScreenDropDownState, ScreenDropDownProps);
				if(NewScreen != g_Config.m_GfxScreen)
					Graphics()->SwitchWindowScreen(NewScreen, true);
			}

			Button = NextRow();
			str_format(aBuf, sizeof(aBuf), "%s (%s)", Localize("V-Sync"), Localize("may cause delay"));
			if(DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_GfxVsync, "graphics-vsync-delay-warning", aBuf, g_Config.m_GfxVsync, &Button))
			{
				Graphics()->SetVSync(!g_Config.m_GfxVsync);
			}

			const auto DoGraphicsChoiceRow = [this, GraphicsMetrics, GraphicsPage](CUIRect Row, const char *pLabel, const char *pId, const char **ppNames, size_t Count, int Current, CUi::SDropDownState &State, CScrollRegion &ScrollRegion, auto &&OnChanged) {
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
				const int NewValue = DoSettingsDropDown(&DropDown, Current, ppNames, Count, State, DropDownProps);
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
			DoGraphicsChoiceRow(Button, aBuf, "graphics-fsaa-samples", s_apFsaaSampleNames, std::size(s_apFsaaSampleNames), FsaaSampleIndex, s_FsaaSampleDropDownState, s_FsaaSampleDropDownScrollRegion, [](int NewValue) {
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
				DoGraphicsChoiceRow(Row, Localize("Graphics mode"), "graphics-mode", apGraphicsModes, std::size(apGraphicsModes), CurrentGraphicsMode, s_BackendDropDownState, s_BackendDropDownScrollRegion, [this](int NewValue) {
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
			if(Graphics()->GetGpus().m_CanSelect && Graphics()->GetGpus().m_vGpus.size() > 1)
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
				DoGraphicsChoiceRow(Row, Localize("Graphics card"), "graphics-card-title", s_vpGpuNames.data(), s_vpGpuNames.size(), Selected, s_GpuDropDownState, s_GpuDropDownScrollRegion, [this, &GpuList](int NewValue) {
					if(NewValue == 0)
						str_copy(g_Config.m_GfxGpuName, "auto");
					else
						str_copy(g_Config.m_GfxGpuName, GpuList.m_vGpus[NewValue - 1].m_aName);
					s_GfxGpuChanged = true;
					CheckSettings = true;
				});
			}
			else if(!Graphics()->GetGpus().m_CanSelect)
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
				InfoLabel(Localize("Detected graphics adapters"));
				if(GpuInfo.m_vGpus.empty())
					InfoLabel(Localize("No graphics adapters detected"));
				for(const auto &Gpu : GpuInfo.m_vGpus)
					InfoLabel(Gpu.m_aName);
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
			const unsigned OldUiAccentColor = g_Config.m_QmUiAccentColor;
			CUIRect UiAccentColorRow = Rows.NextButton();
			DoLine_ColorPicker(&s_UiAccentColorResetId, ColorMetrics, &UiAccentColorRow, Localize("Interface accent color"), &g_Config.m_QmUiAccentColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmUiAccentColor)), false, nullptr, false);
			if(OldUiAccentColor != g_Config.m_QmUiAccentColor)
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
			const int TextMode = DoSettingsDropDown(&TextModeControl, g_Config.m_QmUiTextColorMode, apTextModes, std::size(apTextModes), s_TextColorModeState, TextModeProps);
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
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, GraphicsLayoutRevision);

	if(!m_MenuTextPlanCollecting && !Ui()->RenderOnly() && !m_SettingsCardFocusStableId.empty())
	{
		m_SettingsCardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());
		m_SettingsCardFocusStableId.clear();
	}
	const SQmScrollRequest ScrollRequest{EQmScrollProfile::SETTINGS_OUTER};
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest, UiScale, 0.0f);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	CQmScrollState &ScrollState = s_GraphicsScrollRegion.State();
	// Deck 通过同一个 region 消费该状态；显式取得它以固定页面唯一的滚动状态所有权。
	(void)ScrollState;
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = Ui()->MouseX();
	InputState.m_MouseY = Ui()->MouseY();
	InputState.m_MousePressed = Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = Ui()->MouseButton(0);
	InputState.m_MouseReleased = !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = Input()->ModifierIsPressed();
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = RenderOnly ? nullptr : &ScrollParams;
	const SSettingsCardDeckResult DeckResult = SettingsCardDeckForRenderPass().RenderCached(GraphicsCardCtx, GraphicsPage, pDeckTab, DefinitionsRevision, BuildDefinitions, SettingsCardOrderModelForRenderPass(), RenderOnly ? nullptr : &s_GraphicsScrollRegion, InputState, SettingsCardMotionSpec(), GraphicsVisualOptions);
	if(!RenderOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();

	if(CheckSettings)
	{
		m_NeedRestartGraphics = !(s_GfxFsaaSamples == g_Config.m_GfxFsaaSamples &&
					  !s_GfxBackendChanged &&
					  !s_GfxGpuChanged);
	}
}

namespace
{
	struct SAudioPackEntry
	{
		char m_aName[64];
		int m_FileCount;
	};

	struct SAudioPackScanUser
	{
		IStorage *m_pStorage;
		std::vector<SAudioPackEntry> *m_pPacks;
	};
} // namespace

static int AudioPackFileScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	if(IsDir || pName[0] == '.')
		return 0;

	if(str_endswith(pName, ".wv") || str_endswith(pName, ".opus"))
	{
		int *pCount = static_cast<int *>(pUser);
		(*pCount)++;
	}

	return 0;
}

static int AudioPackScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	if(!IsDir || pName[0] == '.' || str_comp(pName, "default") == 0)
		return 0;

	auto *pData = static_cast<SAudioPackScanUser *>(pUser);
	SAudioPackEntry Entry{};
	str_copy(Entry.m_aName, pName, sizeof(Entry.m_aName));

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "audio/%s", pName);
	pData->m_pStorage->ListDirectory(IStorage::TYPE_ALL, aPath, AudioPackFileScan, &Entry.m_FileCount);

	if(Entry.m_FileCount == 0)
	{
		str_format(aPath, sizeof(aPath), "audio/%s/audio", pName);
		pData->m_pStorage->ListDirectory(IStorage::TYPE_ALL, aPath, AudioPackFileScan, &Entry.m_FileCount);
	}

	pData->m_pPacks->push_back(Entry);
	return 0;
}

static void RefreshAudioPacks(IStorage *pStorage, std::vector<SAudioPackEntry> &vPacks)
{
	vPacks.clear();

	SAudioPackEntry Default{};
	str_copy(Default.m_aName, "default", sizeof(Default.m_aName));
	pStorage->ListDirectory(IStorage::TYPE_ALL, "audio", AudioPackFileScan, &Default.m_FileCount);
	vPacks.push_back(Default);

	SAudioPackScanUser User{pStorage, &vPacks};
	pStorage->ListDirectory(IStorage::TYPE_ALL, "audio", AudioPackScan, &User);

	if(vPacks.size() > 1)
	{
		std::sort(vPacks.begin() + 1, vPacks.end(), [](const SAudioPackEntry &A, const SAudioPackEntry &B) {
			return str_comp(A.m_aName, B.m_aName) < 0;
		});
	}
}

static std::vector<SAudioPackEntry> gs_vAudioPacks;
static bool gs_AudioPacksInit = false;

static void RefreshSharedAudioPacks(IStorage *pStorage)
{
	RefreshAudioPacks(pStorage, gs_vAudioPacks);
	gs_AudioPacksInit = true;
}

static void EnsureSharedAudioPacks(IStorage *pStorage)
{
	if(!gs_AudioPacksInit)
		RefreshSharedAudioPacks(pStorage);
}

static int FindAudioPackIndexByName(const std::vector<SAudioPackEntry> &vPacks, const char *pPackName)
{
	for(size_t i = 0; i < vPacks.size(); ++i)
	{
		if(str_comp(vPacks[i].m_aName, pPackName) == 0)
			return (int)i;
	}
	return -1;
}

void CMenus::AudioPackEditorOpen(const char *pPackName)
{
	AudioPackEditorStopPreview();
	g_Config.m_UiSettingsPage = SETTINGS_SOUND;
	m_AudioPackEditorState.m_Open = true;
	m_AudioPackEditorState.m_Initialized = false;
	m_AudioPackEditorState.m_SelectedSlotIndex = 0;
	m_AudioPackEditorState.m_SelectedCandidateIndex = -1;
	m_AudioPackEditorState.m_StatusIsError = false;
	m_AudioPackEditorState.m_aStatusMessage[0] = '\0';
	m_AudioPackEditorState.m_FilterInput.Clear();
	m_AudioPackEditorState.m_CandidateFilterInput.Clear();
	m_AudioPackEditorState.m_SourcePathInput.Clear();
	if(pPackName != nullptr && pPackName[0] != '\0')
		m_AudioPackEditorState.m_PackNameInput.Set(pPackName);
	else
		m_AudioPackEditorState.m_PackNameInput.Set("default");
}

void CMenus::AudioPackEditorClose()
{
	AudioPackEditorStopPreview();
	m_AudioPackEditorState.m_Open = false;
	m_AudioPackEditorState.m_Initialized = false;
	m_AudioPackEditorState.m_SelectedCandidateIndex = -1;
	m_AudioPackEditorState.m_vCandidateEntries.clear();
}

void CMenus::AudioPackEditorSetStatus(const char *pMessage, bool IsError)
{
	m_AudioPackEditorState.m_StatusIsError = IsError;
	str_copy(m_AudioPackEditorState.m_aStatusMessage, pMessage != nullptr ? pMessage : "", sizeof(m_AudioPackEditorState.m_aStatusMessage));
}

namespace
{
	struct SAudioPackCandidateScanContext
	{
		IStorage *m_pStorage = nullptr;
		std::set<std::string> *m_pEntries = nullptr;
		char m_aScanRoot[IO_MAX_PATH_LENGTH] = "";
		char m_aOutputPrefix[IO_MAX_PATH_LENGTH] = "";
		char m_aRelativePath[IO_MAX_PATH_LENGTH] = "";
	};

}

namespace
{

	static int AudioPackCandidateScanCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
	{
		(void)StorageType;

		auto *pContext = static_cast<SAudioPackCandidateScanContext *>(pUser);
		if(!str_comp(pInfo->m_pName, ".") || !str_comp(pInfo->m_pName, ".."))
			return 0;

		char aRelativePath[IO_MAX_PATH_LENGTH];
		if(pContext->m_aRelativePath[0] != '\0')
			str_format(aRelativePath, sizeof(aRelativePath), "%s/%s", pContext->m_aRelativePath, pInfo->m_pName);
		else
			str_copy(aRelativePath, pInfo->m_pName);

		char aScanPath[IO_MAX_PATH_LENGTH];
		str_format(aScanPath, sizeof(aScanPath), "%s/%s", pContext->m_aScanRoot, aRelativePath);

		if(IsDir)
		{
			if(pInfo->m_pName[0] == '.')
				return 0;

			SAudioPackCandidateScanContext NextContext = *pContext;
			str_copy(NextContext.m_aRelativePath, aRelativePath, sizeof(NextContext.m_aRelativePath));
			pContext->m_pStorage->ListDirectoryInfo(IStorage::TYPE_ALL, aScanPath, AudioPackCandidateScanCallback, &NextContext);
			return 0;
		}

		std::string CandidatePath;
		if(CMenus::TryBuildAudioPackCandidatePathFromScan(pContext->m_aOutputPrefix, aRelativePath, CandidatePath))
			pContext->m_pEntries->insert(std::move(CandidatePath));

		return 0;
	}

	static const char *ResolveAudioPackEditorPackName(const CLineInputBuffered<64> &PackNameInput, const char *pFallbackPackName)
	{
		if(PackNameInput.GetString()[0] != '\0')
			return PackNameInput.GetString();
		return pFallbackPackName != nullptr ? pFallbackPackName : "";
	}

	static void ResolveAudioPackEditorCurrentFilePath(IStorage *pStorage, const char *pPackName, const CMenus::SAudioPackSlot &Slot, char *pOut, int OutSize)
	{
		pOut[0] = '\0';

		char aDirectPath[IO_MAX_PATH_LENGTH];
		char aLegacyPath[IO_MAX_PATH_LENGTH];
		char aBuiltinPath[IO_MAX_PATH_LENGTH];

		str_copy(aDirectPath, CMenus::BuildAudioPackExportPath(pPackName, Slot.m_pRelativePath).c_str(), sizeof(aDirectPath));
		str_format(aLegacyPath, sizeof(aLegacyPath), "audio/%s/audio/%s", pPackName, Slot.m_pRelativePath);
		str_copy(aBuiltinPath, CMenus::BuildAudioPackBuiltinCandidatePath(Slot.m_pRelativePath).c_str(), sizeof(aBuiltinPath));

		if(pStorage->FileExists(aDirectPath, IStorage::TYPE_ALL))
			str_copy(pOut, aDirectPath, OutSize);
		else if(pStorage->FileExists(aLegacyPath, IStorage::TYPE_ALL))
			str_copy(pOut, aLegacyPath, OutSize);
		else if(pStorage->FileExists(aBuiltinPath, IStorage::TYPE_ALL))
			str_copy(pOut, aBuiltinPath, OutSize);
	}

}

void CMenus::AudioPackEditorRefreshCandidates()
{
	std::set<std::string> vCandidatePaths;
	for(const auto &ScanRoot : BuildAudioPackCandidateScanRoots())
	{
		if(!Storage()->FolderExists(ScanRoot.m_pScanRoot, IStorage::TYPE_ALL))
			continue;

		SAudioPackCandidateScanContext Context;
		Context.m_pStorage = Storage();
		Context.m_pEntries = &vCandidatePaths;
		str_copy(Context.m_aScanRoot, ScanRoot.m_pScanRoot, sizeof(Context.m_aScanRoot));
		str_copy(Context.m_aOutputPrefix, ScanRoot.m_pOutputPrefix, sizeof(Context.m_aOutputPrefix));
		Storage()->ListDirectoryInfo(IStorage::TYPE_ALL, ScanRoot.m_pScanRoot, AudioPackCandidateScanCallback, &Context);
	}

	std::vector<std::string> vPaths(vCandidatePaths.begin(), vCandidatePaths.end());

	char aCurrentPath[IO_MAX_PATH_LENGTH] = "";
	const auto vSlots = BuildAudioPackSlots();
	if(!vSlots.empty())
	{
		m_AudioPackEditorState.m_SelectedSlotIndex = std::clamp(m_AudioPackEditorState.m_SelectedSlotIndex, 0, (int)vSlots.size() - 1);
		ResolveAudioPackEditorCurrentFilePath(Storage(), ResolveAudioPackEditorPackName(m_AudioPackEditorState.m_PackNameInput, g_Config.m_SndPack), vSlots[m_AudioPackEditorState.m_SelectedSlotIndex], aCurrentPath, sizeof(aCurrentPath));
	}

	std::string SelectedPath;
	if(m_AudioPackEditorState.m_SelectedCandidateIndex >= 0 && m_AudioPackEditorState.m_SelectedCandidateIndex < (int)m_AudioPackEditorState.m_vCandidateEntries.size())
		SelectedPath = m_AudioPackEditorState.m_vCandidateEntries[m_AudioPackEditorState.m_SelectedCandidateIndex].m_Path;

	m_AudioPackEditorState.m_vCandidateEntries = BuildAudioPackCandidateEntries(vPaths, ResolveAudioPackEditorPackName(m_AudioPackEditorState.m_PackNameInput, g_Config.m_SndPack), aCurrentPath);

	int SelectedIndex = FindAudioPackCandidateEntryIndex(m_AudioPackEditorState.m_vCandidateEntries, aCurrentPath);
	if(SelectedIndex < 0 && !SelectedPath.empty())
		SelectedIndex = FindAudioPackCandidateEntryIndex(m_AudioPackEditorState.m_vCandidateEntries, SelectedPath.c_str());
	if(SelectedIndex < 0 && !m_AudioPackEditorState.m_vCandidateEntries.empty())
		SelectedIndex = 0;
	m_AudioPackEditorState.m_SelectedCandidateIndex = SelectedIndex;
}

void CMenus::AudioPackEditorStopPreview()
{
	if(m_AudioPackEditorState.m_PreviewSampleId >= 0)
	{
		Sound()->Stop(m_AudioPackEditorState.m_PreviewSampleId);
		Sound()->UnloadSample(m_AudioPackEditorState.m_PreviewSampleId);
		m_AudioPackEditorState.m_PreviewSampleId = -1;
	}
}

bool CMenus::AudioPackEditorPlayPreview(const char *pFilename, int StorageType)
{
	if(pFilename == nullptr || pFilename[0] == '\0')
		return false;

	AudioPackEditorStopPreview();

	const int SampleId = Sound()->LoadWV(pFilename, StorageType);
	if(SampleId < 0)
		return false;

	m_AudioPackEditorState.m_PreviewSampleId = SampleId;
	GameClient()->m_Sounds.PlaySample(CSounds::CHN_GUI, SampleId, ISound::FLAG_PREVIEW, 1.0f);
	return true;
}

bool CMenus::AudioPackEditorEnsureStorageDirectories(const char *pStoragePath)
{
	if(pStoragePath == nullptr || pStoragePath[0] == '\0')
		return false;

	std::string CurrentDirectory;
	for(const char *pCursor = pStoragePath; *pCursor != '\0'; ++pCursor)
	{
		if(*pCursor == '/')
		{
			if(!CurrentDirectory.empty() && !Storage()->FolderExists(CurrentDirectory.c_str(), IStorage::TYPE_SAVE) &&
				!Storage()->CreateFolder(CurrentDirectory.c_str(), IStorage::TYPE_SAVE))
			{
				return false;
			}
		}
		else
		{
			CurrentDirectory.push_back(*pCursor);
		}
	}

	return true;
}

bool CMenus::AudioPackEditorCopyFileToStorage(const char *pSourcePath, int SourceStorageType, const char *pStoragePath)
{
	if(pSourcePath == nullptr || pSourcePath[0] == '\0' || pStoragePath == nullptr || pStoragePath[0] == '\0')
		return false;
	if(!Storage()->FileExists(pSourcePath, SourceStorageType))
		return false;
	if(!AudioPackEditorEnsureStorageDirectories(pStoragePath))
		return false;

	IOHANDLE SourceFile = Storage()->OpenFile(pSourcePath, IOFLAG_READ, SourceStorageType);
	if(!SourceFile)
		return false;

	void *pData = nullptr;
	unsigned DataSize = 0;
	const bool ReadOk = io_read_all(SourceFile, &pData, &DataSize);
	io_close(SourceFile);
	if(!ReadOk || pData == nullptr)
	{
		if(pData != nullptr)
			free(pData);
		return false;
	}

	IOHANDLE DestFile = Storage()->OpenFile(pStoragePath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!DestFile)
	{
		free(pData);
		return false;
	}

	const bool WriteOk = io_write(DestFile, pData, DataSize) == DataSize;
	io_close(DestFile);
	free(pData);
	return WriteOk;
}

bool CMenus::AudioPackEditorCopyAbsoluteFileToStorage(const char *pSourcePath, const char *pStoragePath)
{
	return AudioPackEditorCopyFileToStorage(pSourcePath, IStorage::TYPE_ABSOLUTE, pStoragePath);
}

void CMenus::RenderAudioPackEditorScreen(CUIRect MainView)
{
	const SSettingsContentMetrics EditorMetrics = ResolveSettingsContentMetrics(MainView.w);
	const float EditorFontSize = EditorMetrics.m_BodySize;
	const float EditorSecondaryFontSize = maximum(9.0f, EditorFontSize - 2.0f);
	const float EditorLineSize = EditorMetrics.m_LineHeight;
	const float EditorMarginSmall = EditorMetrics.m_LineSpacing;
	const float EditorMarginExtraSmall = maximum(2.0f, EditorMarginSmall * 0.5f);

	if(!m_AudioPackEditorState.m_Open)
		return;

	if(!m_AudioPackEditorState.m_Initialized)
	{
		AudioPackEditorRefreshCandidates();
		m_AudioPackEditorState.m_Initialized = true;
	}

	if(Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		AudioPackEditorClose();
		return;
	}

	IUiContext AudioPackSlotSearchCtx;
	AudioPackSlotSearchCtx.m_pUi = Ui();
	AudioPackSlotSearchCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	AudioPackSlotSearchCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	AudioPackSlotSearchCtx.m_ScopeHash = MakeUiScopeHash("settings_audio_pack_slot_search");
	AudioPackSlotSearchCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	IUiContext AudioPackCandidateSearchCtx;
	AudioPackCandidateSearchCtx.m_pUi = Ui();
	AudioPackCandidateSearchCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	AudioPackCandidateSearchCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	AudioPackCandidateSearchCtx.m_ScopeHash = MakeUiScopeHash("settings_audio_pack_candidate_search");
	AudioPackCandidateSearchCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	IUiContext AudioPackEditorTextInputCtx;
	AudioPackEditorTextInputCtx.m_pUi = Ui();
	AudioPackEditorTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
	AudioPackEditorTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
	AudioPackEditorTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_audio_pack_text_inputs");
	AudioPackEditorTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();

	const auto vAllSlots = BuildAudioPackSlots();
	if(vAllSlots.empty())
	{
		DoSettingsMenuLabel(SETTINGS_SOUND, -1, -1, "audio_no_slots_label", &MainView, Localize("No audio slots found."), EditorFontSize, TEXTALIGN_MC);
		return;
	}

	m_AudioPackEditorState.m_SelectedSlotIndex = std::clamp(m_AudioPackEditorState.m_SelectedSlotIndex, 0, (int)vAllSlots.size() - 1);

	CUIRect EditorRect = MainView;
	EditorRect.Margin(8.0f, &EditorRect);
	EditorRect.Draw(ColorRGBA(0.10f, 0.11f, 0.15f, 1.0f), IGraphics::CORNER_ALL, ui_token::radius::CARD);

	CUIRect WorkRect;
	EditorRect.Margin(8.0f, &WorkRect);

	CUIRect TopPanel, TopBarRow1, TopBarRow2, ContentRow, StatusRow;
	WorkRect.HSplitTop(EditorLineSize * 2.0f + EditorMarginSmall + 8.0f, &TopPanel, &ContentRow);
	TopPanel.HSplitTop(EditorLineSize + 4.0f, &TopBarRow1, &TopPanel);
	TopPanel.HSplitTop(EditorMarginExtraSmall, nullptr, &TopPanel);
	TopBarRow2 = TopPanel;
	ContentRow.HSplitBottom(EditorLineSize + EditorMarginSmall, &ContentRow, &StatusRow);

	static CButtonContainer s_AudioPackEditorCloseButton;
	static CButtonContainer s_AudioPackEditorRefreshButton;
	static CButtonContainer s_AudioPackEditorPreviewButton;
	static CButtonContainer s_AudioPackEditorExportButton;
	static CButtonContainer s_AudioPackEditorImportPreviewButton;
	static CListBox s_AudioPackEditorSlotListBox;
	static CListBox s_AudioPackEditorCandidateListBox;
	static std::vector<int> s_vAudioPackEditorSlotItemIds;

	auto SplitLeftSafe = [](CUIRect &Source, float Wanted, CUIRect *pLeft, CUIRect *pRight) {
		const float Cut = minimum(Wanted, Source.w);
		Source.VSplitLeft(Cut, pLeft, pRight);
	};
	auto SplitRightSafe = [](CUIRect &Source, float Wanted, CUIRect *pLeft, CUIRect *pRight) {
		const float Cut = minimum(Wanted, Source.w);
		Source.VSplitRight(Cut, pLeft, pRight);
	};

	CUIRect CloseButton, PackRow, TitleRow, RefreshButton;
	SplitLeftSafe(TopBarRow1, 28.0f, &CloseButton, &TopBarRow1);
	SplitLeftSafe(TopBarRow1, EditorMarginSmall, nullptr, &TopBarRow1);
	PackRow = TopBarRow1;

	constexpr float TopButtonPadding = 18.0f;
	const float RefreshW = minimum(122.0f, maximum(74.0f, TextRender()->TextWidth(EditorFontSize, Localize("Reload"), -1, -1.0f) + TopButtonPadding));
	SplitRightSafe(TopBarRow2, RefreshW, &TitleRow, &RefreshButton);

	if(Ui()->DoButton_QmIcon(&s_AudioPackEditorCloseButton, EQmIcon::CLOSE, FONT_ICON_XMARK, 0, &CloseButton, IGraphics::CORNER_ALL))
	{
		AudioPackEditorClose();
		return;
	}

	CUIRect PackLabel, PackInput;
	PackRow.VSplitLeft(90.0f, &PackLabel, &PackInput);
	DoSettingsMenuLabel(SETTINGS_SOUND, -1, -1, "audio_pack_name_label", &PackLabel, Localize("Pack name"), EditorFontSize, TEXTALIGN_ML);
	if(ui_widget::InputField(AudioPackEditorTextInputCtx, &m_AudioPackEditorState.m_PackNameInput, PackInput, Localize("Pack name"), EditorFontSize))
		AudioPackEditorRefreshCandidates();

	DoSettingsMenuLabel(SETTINGS_SOUND, -1, -1, "audio_pack_edit_title", &TitleRow, Localize("Edit audio pack"), EditorFontSize, TEXTALIGN_ML);
	if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackEditorRefreshButton, "sound-audio-pack-editor-reload", Localize("Reload"), 0, &RefreshButton))
		AudioPackEditorRefreshCandidates();

	ContentRow.HSplitTop(EditorMarginSmall, nullptr, &ContentRow);

	CUIRect SlotColumn, CandidateColumn, DetailColumn;
	ContentRow.VSplitLeft(260.0f, &SlotColumn, &ContentRow);
	ContentRow.VSplitLeft(8.0f, nullptr, &ContentRow);
	ContentRow.VSplitLeft(320.0f, &CandidateColumn, &ContentRow);
	ContentRow.VSplitLeft(8.0f, nullptr, &ContentRow);
	DetailColumn = ContentRow;

	CUIRect SlotSearchRow, SlotListRow;
	SlotColumn.HSplitTop(EditorLineSize, &SlotSearchRow, &SlotColumn);
	SlotColumn.HSplitTop(EditorMarginSmall, nullptr, &SlotColumn);
	SlotListRow = SlotColumn;
	DoSettingsMenuLabel(SETTINGS_SOUND, -1, -1, "audio_pack_slot_search_label", &SlotSearchRow, Localize("Search"), EditorFontSize, TEXTALIGN_ML);
	CUIRect SlotSearchInput;
	SlotSearchRow.VSplitLeft(80.0f, nullptr, &SlotSearchInput);
	ui_widget::InputField(AudioPackSlotSearchCtx, &m_AudioPackEditorState.m_FilterInput, SlotSearchInput, EditorFontSize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());

	std::vector<int> vVisibleSlotIndices;
	vVisibleSlotIndices.reserve(vAllSlots.size());
	const char *pSlotFilter = m_AudioPackEditorState.m_FilterInput.GetString();
	for(int SlotIndex = 0; SlotIndex < (int)vAllSlots.size(); ++SlotIndex)
	{
		const auto &Slot = vAllSlots[SlotIndex];
		if(pSlotFilter[0] != '\0' &&
			!str_find_nocase(Slot.m_pDisplayName, pSlotFilter) &&
			!str_find_nocase(Slot.m_pSetName, pSlotFilter) &&
			!str_find_nocase(Slot.m_pRelativePath, pSlotFilter))
		{
			continue;
		}
		vVisibleSlotIndices.push_back(SlotIndex);
	}

	if(!vVisibleSlotIndices.empty())
	{
		if(std::find(vVisibleSlotIndices.begin(), vVisibleSlotIndices.end(), m_AudioPackEditorState.m_SelectedSlotIndex) == vVisibleSlotIndices.end())
		{
			m_AudioPackEditorState.m_SelectedSlotIndex = vVisibleSlotIndices.front();
			AudioPackEditorRefreshCandidates();
		}
	}

	s_AudioPackEditorSlotListBox.DoHeader(&SlotListRow, Localize("Audio slots"), EditorLineSize, EditorMarginExtraSmall);
	int SelectedVisibleSlot = 0;
	for(int Index = 0; Index < (int)vVisibleSlotIndices.size(); ++Index)
	{
		if(vVisibleSlotIndices[Index] == m_AudioPackEditorState.m_SelectedSlotIndex)
		{
			SelectedVisibleSlot = Index;
			break;
		}
	}
	const int OldSelectedVisibleSlot = SelectedVisibleSlot;
	s_vAudioPackEditorSlotItemIds.resize(vAllSlots.size());
	s_AudioPackEditorSlotListBox.DoStart(EditorLineSize, vVisibleSlotIndices.size(), 1, 6, SelectedVisibleSlot);
	for(int VisibleIndex = 0; VisibleIndex < (int)vVisibleSlotIndices.size(); ++VisibleIndex)
	{
		const int SlotIndex = vVisibleSlotIndices[VisibleIndex];
		const auto &Slot = vAllSlots[SlotIndex];
		const CListboxItem Item = s_AudioPackEditorSlotListBox.DoNextItem(&s_vAudioPackEditorSlotItemIds[SlotIndex], SelectedVisibleSlot == VisibleIndex);
		if(!Item.m_Visible)
			continue;

		char aLabel[256];
		if(Slot.m_VariantCount > 1)
			str_format(aLabel, sizeof(aLabel), "%s [%d/%d]", Slot.m_pSetName, Slot.m_VariantIndex + 1, Slot.m_VariantCount);
		else
			str_copy(aLabel, Slot.m_pSetName, sizeof(aLabel));
		Ui()->DoLabel(&Item.m_Rect, aLabel, EditorFontSize, TEXTALIGN_ML);
	}
	SelectedVisibleSlot = s_AudioPackEditorSlotListBox.DoEnd();
	if(SelectedVisibleSlot != OldSelectedVisibleSlot && SelectedVisibleSlot >= 0 && SelectedVisibleSlot < (int)vVisibleSlotIndices.size())
	{
		m_AudioPackEditorState.m_SelectedSlotIndex = vVisibleSlotIndices[SelectedVisibleSlot];
		AudioPackEditorRefreshCandidates();
	}

	CUIRect CandidateSearchRow, CandidateListRow;
	CandidateColumn.HSplitTop(EditorLineSize, &CandidateSearchRow, &CandidateColumn);
	CandidateColumn.HSplitTop(EditorMarginSmall, nullptr, &CandidateColumn);
	CandidateListRow = CandidateColumn;
	DoSettingsMenuLabel(SETTINGS_SOUND, -1, -1, "audio_pack_candidate_search_label", &CandidateSearchRow, Localize("Search"), EditorFontSize, TEXTALIGN_ML);
	CUIRect CandidateSearchInput;
	CandidateSearchRow.VSplitLeft(80.0f, nullptr, &CandidateSearchInput);
	ui_widget::InputField(AudioPackCandidateSearchCtx, &m_AudioPackEditorState.m_CandidateFilterInput, CandidateSearchInput, EditorFontSize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());

	std::vector<int> vVisibleCandidateIndices;
	vVisibleCandidateIndices.reserve(m_AudioPackEditorState.m_vCandidateEntries.size());
	const char *pCandidateFilter = m_AudioPackEditorState.m_CandidateFilterInput.GetString();
	for(int CandidateIndex = 0; CandidateIndex < (int)m_AudioPackEditorState.m_vCandidateEntries.size(); ++CandidateIndex)
	{
		const auto &Entry = m_AudioPackEditorState.m_vCandidateEntries[CandidateIndex];
		if(pCandidateFilter[0] != '\0' &&
			!str_find_nocase(Entry.m_DisplayName.c_str(), pCandidateFilter) &&
			!str_find_nocase(Entry.m_Path.c_str(), pCandidateFilter))
		{
			continue;
		}
		vVisibleCandidateIndices.push_back(CandidateIndex);
	}

	if(!vVisibleCandidateIndices.empty())
	{
		if(std::find(vVisibleCandidateIndices.begin(), vVisibleCandidateIndices.end(), m_AudioPackEditorState.m_SelectedCandidateIndex) == vVisibleCandidateIndices.end())
			m_AudioPackEditorState.m_SelectedCandidateIndex = vVisibleCandidateIndices.front();
	}
	else
	{
		m_AudioPackEditorState.m_SelectedCandidateIndex = -1;
	}

	s_AudioPackEditorCandidateListBox.DoHeader(&CandidateListRow, Localize("Candidate files"), EditorLineSize, EditorMarginExtraSmall);
	int SelectedVisibleCandidate = 0;
	for(int Index = 0; Index < (int)vVisibleCandidateIndices.size(); ++Index)
	{
		if(vVisibleCandidateIndices[Index] == m_AudioPackEditorState.m_SelectedCandidateIndex)
		{
			SelectedVisibleCandidate = Index;
			break;
		}
	}
	const int OldSelectedVisibleCandidate = SelectedVisibleCandidate;
	s_AudioPackEditorCandidateListBox.DoStart(EditorLineSize, vVisibleCandidateIndices.size(), 1, 6, SelectedVisibleCandidate);
	for(int VisibleIndex = 0; VisibleIndex < (int)vVisibleCandidateIndices.size(); ++VisibleIndex)
	{
		const int CandidateIndex = vVisibleCandidateIndices[VisibleIndex];
		const auto &Entry = m_AudioPackEditorState.m_vCandidateEntries[CandidateIndex];
		const CListboxItem Item = s_AudioPackEditorCandidateListBox.DoNextItem(&Entry, SelectedVisibleCandidate == VisibleIndex);
		if(!Item.m_Visible)
			continue;

		char aLabel[IO_MAX_PATH_LENGTH + 64];
		if(Entry.m_IsCurrentFile)
			str_format(aLabel, sizeof(aLabel), "%s (%s)", Entry.m_DisplayName.c_str(), Localize("Current file"));
		else if(Entry.m_IsCurrentPackFile)
			str_format(aLabel, sizeof(aLabel), "%s (%s)", Entry.m_DisplayName.c_str(), Localize("Pack name"));
		else
			str_copy(aLabel, Entry.m_DisplayName.c_str(), sizeof(aLabel));
		Ui()->DoLabel(&Item.m_Rect, aLabel, EditorFontSize, TEXTALIGN_ML);
	}
	SelectedVisibleCandidate = s_AudioPackEditorCandidateListBox.DoEnd();
	if(SelectedVisibleCandidate != OldSelectedVisibleCandidate && SelectedVisibleCandidate >= 0 && SelectedVisibleCandidate < (int)vVisibleCandidateIndices.size())
		m_AudioPackEditorState.m_SelectedCandidateIndex = vVisibleCandidateIndices[SelectedVisibleCandidate];

	const auto &SelectedSlot = vAllSlots[m_AudioPackEditorState.m_SelectedSlotIndex];
	const char *pPackName = ResolveAudioPackEditorPackName(m_AudioPackEditorState.m_PackNameInput, g_Config.m_SndPack);
	char aCurrentPath[IO_MAX_PATH_LENGTH] = "";
	ResolveAudioPackEditorCurrentFilePath(Storage(), pPackName, SelectedSlot, aCurrentPath, sizeof(aCurrentPath));

	const char *pSelectedCandidatePath = "";
	if(m_AudioPackEditorState.m_SelectedCandidateIndex >= 0 && m_AudioPackEditorState.m_SelectedCandidateIndex < (int)m_AudioPackEditorState.m_vCandidateEntries.size())
		pSelectedCandidatePath = m_AudioPackEditorState.m_vCandidateEntries[m_AudioPackEditorState.m_SelectedCandidateIndex].m_Path.c_str();

	CUIRect DetailRow;
	DetailColumn.HSplitTop(EditorLineSize, &DetailRow, &DetailColumn);
	char aSlotLabel[256];
	if(SelectedSlot.m_VariantCount > 1)
		str_format(aSlotLabel, sizeof(aSlotLabel), "%s [%d/%d]", SelectedSlot.m_pSetName, SelectedSlot.m_VariantIndex + 1, SelectedSlot.m_VariantCount);
	else
		str_copy(aSlotLabel, SelectedSlot.m_pSetName, sizeof(aSlotLabel));
	Ui()->DoLabel(&DetailRow, aSlotLabel, EditorFontSize, TEXTALIGN_ML);

	DetailColumn.HSplitTop(EditorLineSize, &DetailRow, &DetailColumn);
	char aRelativeLabel[256];
	str_format(aRelativeLabel, sizeof(aRelativeLabel), "%s: %s", Localize("Relative path"), SelectedSlot.m_pRelativePath);
	Ui()->DoLabel(&DetailRow, aRelativeLabel, EditorSecondaryFontSize, TEXTALIGN_ML);

	DetailColumn.HSplitTop(EditorLineSize, &DetailRow, &DetailColumn);
	char aCurrentLabel[IO_MAX_PATH_LENGTH + 32];
	if(aCurrentPath[0] != '\0')
		str_format(aCurrentLabel, sizeof(aCurrentLabel), "%s: %s", Localize("Current file"), aCurrentPath);
	else
		str_format(aCurrentLabel, sizeof(aCurrentLabel), "%s: %s", Localize("Current file"), Localize("Default"));
	Ui()->DoLabel(&DetailRow, aCurrentLabel, EditorSecondaryFontSize, TEXTALIGN_ML);

	DetailColumn.HSplitTop(EditorLineSize, &DetailRow, &DetailColumn);
	char aSelectedLabel[IO_MAX_PATH_LENGTH + 48];
	if(pSelectedCandidatePath[0] != '\0')
		str_format(aSelectedLabel, sizeof(aSelectedLabel), "%s: %s", Localize("Selected candidate"), pSelectedCandidatePath);
	else
		str_format(aSelectedLabel, sizeof(aSelectedLabel), "%s: %s", Localize("Selected candidate"), Localize("Default"));
	Ui()->DoLabel(&DetailRow, aSelectedLabel, EditorSecondaryFontSize, TEXTALIGN_ML);

	DetailColumn.HSplitTop(EditorMarginSmall * 2.0f, nullptr, &DetailColumn);
	CUIRect ManualRow;
	DetailColumn.HSplitTop(EditorLineSize, &ManualRow, &DetailColumn);
	DoSettingsMenuLabel(SETTINGS_SOUND, -1, -1, "audio_manual_source_label", &ManualRow, Localize("Manual source file"), EditorFontSize, TEXTALIGN_ML);
	CUIRect ManualInput;
	ManualRow.VSplitLeft(120.0f, nullptr, &ManualInput);
	ui_widget::InputField(AudioPackEditorTextInputCtx, &m_AudioPackEditorState.m_SourcePathInput, ManualInput, Localize("Manual source file"), EditorFontSize);

	DetailColumn.HSplitTop(EditorMarginSmall, nullptr, &DetailColumn);
	CUIRect ActionRowTop, ActionRowBottom;
	DetailColumn.HSplitTop(EditorLineSize, &ActionRowTop, &DetailColumn);
	DetailColumn.HSplitTop(EditorMarginSmall, nullptr, &DetailColumn);
	DetailColumn.HSplitTop(EditorLineSize, &ActionRowBottom, &DetailColumn);
	CUIRect PreviewButton, ImportPreviewButton, ExportButton;
	ActionRowTop.VSplitMid(&PreviewButton, &ImportPreviewButton, 8.0f);
	ExportButton = ActionRowBottom;

	if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackEditorPreviewButton, "sound-audio-pack-editor-preview-selected", Localize("Preview selected file"), 0, &PreviewButton))
	{
		if(pSelectedCandidatePath[0] == '\0')
		{
			AudioPackEditorSetStatus(Localize("No candidate file selected."), true);
		}
		else if(!AudioPackEditorPlayPreview(pSelectedCandidatePath, IStorage::TYPE_ALL))
		{
			AudioPackEditorSetStatus(Localize("Failed to preview candidate file."), true);
		}
		else
		{
			AudioPackEditorSetStatus("", false);
		}
	}

	if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackEditorImportPreviewButton, "sound-audio-pack-editor-preview-import", Localize("Preview import file"), 0, &ImportPreviewButton))
	{
		const char *pManualPath = m_AudioPackEditorState.m_SourcePathInput.GetString();
		const std::string PreviewPath = ResolveAudioPackPreviewPath("", pManualPath);
		if(PreviewPath.empty())
		{
			AudioPackEditorSetStatus(Localize("Source file is empty."), true);
		}
		else if(!Storage()->FileExists(PreviewPath.c_str(), IStorage::TYPE_ABSOLUTE))
		{
			AudioPackEditorSetStatus(Localize("Source file does not exist."), true);
		}
		else if(!str_endswith(PreviewPath.c_str(), ".wv"))
		{
			AudioPackEditorSetStatus(Localize("Only .wv files are supported right now."), true);
		}
		else if(!AudioPackEditorPlayPreview(PreviewPath.c_str(), IStorage::TYPE_ABSOLUTE))
		{
			AudioPackEditorSetStatus(Localize("Failed to preview import file."), true);
		}
		else
		{
			AudioPackEditorSetStatus("", false);
		}
	}

	if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackEditorExportButton, "sound-audio-pack-editor-export-selected", Localize("Export selected file"), 0, &ExportButton))
	{
		const char *pManualPath = m_AudioPackEditorState.m_SourcePathInput.GetString();
		const std::string SourcePath = ResolveAudioPackExportSourcePath(pSelectedCandidatePath, pManualPath);
		const bool UseManualSource = pManualPath[0] != '\0';
		int SourceStorageType = IStorage::TYPE_ALL;

		if(SourcePath.empty())
		{
			AudioPackEditorSetStatus(UseManualSource ? Localize("Source file is empty.") : Localize("No candidate file selected."), true);
		}
		else
		{
			if(UseManualSource)
			{
				SourceStorageType = IStorage::TYPE_ABSOLUTE;
				if(!Storage()->FileExists(SourcePath.c_str(), SourceStorageType))
				{
					AudioPackEditorSetStatus(Localize("Source file does not exist."), true);
					goto AudioPackExportDone;
				}
				if(!str_endswith(SourcePath.c_str(), ".wv"))
				{
					AudioPackEditorSetStatus(Localize("Only .wv files are supported right now."), true);
					goto AudioPackExportDone;
				}
			}

			const std::string ExportPath = BuildAudioPackExportPath(pPackName, SelectedSlot.m_pRelativePath);
			if(AudioPackEditorCopyFileToStorage(SourcePath.c_str(), SourceStorageType, ExportPath.c_str()))
			{
				str_copy(g_Config.m_SndPack, pPackName, sizeof(g_Config.m_SndPack));
				RefreshSharedAudioPacks(Storage());
				if(GameClient()->m_Sounds.Reload())
				{
					UpdateMusicState();
					AudioPackEditorSetStatus(Localize("Audio pack exported."), false);
				}
				else
				{
					AudioPackEditorSetStatus(Localize("Audio file was exported, but reload failed. Restart sound to apply it."), true);
				}
				AudioPackEditorRefreshCandidates();
			}
			else
			{
				AudioPackEditorSetStatus(Localize("Failed to export audio pack file."), true);
			}
		}
	}
AudioPackExportDone:

	if(m_AudioPackEditorState.m_aStatusMessage[0] != '\0')
	{
		TextRender()->TextColor(m_AudioPackEditorState.m_StatusIsError ? ColorRGBA(1.0f, 0.35f, 0.35f, 1.0f) : ColorRGBA(0.45f, 1.0f, 0.55f, 1.0f));
		Ui()->DoLabel(&StatusRow, m_AudioPackEditorState.m_aStatusMessage, EditorSecondaryFontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}

void CMenus::RenderSettingsSound(CUIRect MainView)
{
	if(m_AudioPackEditorState.m_Open)
	{
		RenderAudioPackEditorScreen(MainView);
		return;
	}

	static bool s_SndPackInit = false;
	static char s_aSndPack[sizeof(g_Config.m_SndPack)] = "";

	if(!s_SndPackInit)
	{
		str_copy(s_aSndPack, g_Config.m_SndPack, sizeof(s_aSndPack));
		s_SndPackInit = true;
	}

	const SSettingsContentMetrics SoundMetrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = SoundMetrics.m_UiScale;
	const float BodySize = SoundMetrics.m_BodySize;
	const float LineHeight = SoundMetrics.m_LineHeight;
	const float LineSpacing = SoundMetrics.m_LineSpacing;
	static CScrollRegion s_SoundScrollRegion;
	const SSettingsPageLayoutFrame SoundPage = SettingsPageLayout(MainView, UiScale);
	const IUiContext SoundCardCtx = SettingsUiContext("settings_sound", UiScale);
	const SSettingsCardDeckVisualOptions SoundVisualOptions = SettingsCardDeckVisualOptions();
	const auto DoSoundNumericField = [this, SoundCardCtx, BodySize](const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel) {
		ui_widget::SNumericFieldOptions Options;
		Options.m_pLabel = pLabel;
		Options.m_pSuffix = "%";
		Options.m_pScale = &CUi::ms_LogarithmicScrollbarScale;
		Options.m_FontSize = BodySize;
		Options.m_LabelAlign = TEXTALIGN_ML;
		if(PrepareSettingsNumericFieldLabel(SETTINGS_SOUND, -1, -1, pTextId, Rect, pLabel, 0u, Options))
			return false;
		return ui_widget::NumericField(SoundCardCtx, GetSettingsNumericFieldState(pId), pId, pOption, 0, 100, Rect, Options);
	};
	const qm_card_registry::SCardDefault *pToggleDefault = qm_card_registry::FindByStableId("deck:sound-toggle");
	const qm_card_registry::SCardDefault *pVolumeDefault = qm_card_registry::FindByStableId("deck:sound-volume");
	const qm_card_registry::SCardDefault *pAudioPackDefault = qm_card_registry::FindByStableId("deck:sound-audio-pack");
	dbg_assert(pToggleDefault != nullptr && pVolumeDefault != nullptr && pAudioPackDefault != nullptr, "sound settings cards must be registered");
	if(pToggleDefault == nullptr || pVolumeDefault == nullptr || pAudioPackDefault == nullptr)
		return;

	const float CardChromeHeight = BuildSettingsCardFrame({0.0f, 0.0f, 1.0f, 0.0f}, {nullptr, nullptr, "subtitle"}, 0.0f, UiScale).m_Rect.h;
	const float ToggleChromeHeight = CardChromeHeight;
	const float VolumeChromeHeight = CardChromeHeight;
	const float AudioPackChromeHeight = CardChromeHeight;
	EnsureSharedAudioPacks(Storage());
	const int ToggleRowCount = g_Config.m_SndEnable ? 10 : 1;
	const float SoundToggleCardHeight = ToggleChromeHeight + LineHeight * ToggleRowCount + LineSpacing * maximum(0, ToggleRowCount - 1);
	const float SoundVolumeCardHeight = VolumeChromeHeight + LineHeight * 5.0f + LineSpacing * 4.0f;
	const int AudioPackCount = (int)gs_vAudioPacks.size();
	const SSettingsListCardGeometry SoundAudioPackGeometry = ResolveSettingsSoundAudioPackGeometry(AudioPackCount, SoundMetrics);
	const float SoundAudioPackContentHeight = SoundAudioPackGeometry.m_ContentHeight;
	const float SoundAudioPackCardHeight = AudioPackChromeHeight + SoundAudioPackContentHeight;
	const bool RenderOnly = Ui()->RenderOnly();
	const auto BuildDefinitions = [this, pToggleDefault, pVolumeDefault, pAudioPackDefault, SoundToggleCardHeight, ToggleChromeHeight, SoundVolumeCardHeight, VolumeChromeHeight, SoundAudioPackCardHeight, AudioPackChromeHeight, SoundMetrics, LineHeight, LineSpacing, BodySize, DoSoundNumericField](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(3);
		const SSettingsCardSpec ToggleSpec{pToggleDefault->m_pStableId, Localize(pToggleDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pToggleDefault)};
		const SSettingsCardSpec VolumeSpec{pVolumeDefault->m_pStableId, Localize(pVolumeDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pVolumeDefault)};
		const SSettingsCardSpec AudioPackSpec{pAudioPackDefault->m_pStableId, Localize(pAudioPackDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pAudioPackDefault)};
		const auto AddCard = [&vCards](const SSettingsCardSpec &Spec, float TotalHeight, float ChromeHeight, FSettingsCardRender Render, std::function<bool()> IsVisible = {}, bool VisibilityController = false, FSettingsCardPreLayoutInput PreLayoutInput = {}, uint64_t MeasureRevision = 0) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			Definition.m_Measure = [TotalHeight, ChromeHeight](float) {
				return maximum(0.0f, TotalHeight - ChromeHeight);
			};
			Definition.m_Render = std::move(Render);
			Definition.m_PreLayoutInput = std::move(PreLayoutInput);
			Definition.m_IsVisible = std::move(IsVisible);
			Definition.m_VisibilityController = VisibilityController;
			Definition.m_MeasureRevision = MeasureRevision;
			vCards.push_back(std::move(Definition));
		};
		const auto ProcessSoundToggleInput = [this, LineHeight](CUIRect ContentRect) {
			if(m_MenuTextPlanCollecting)
				return false;
			CUIRect MainView = ContentRect;
			CUIRect Button;
			MainView.HSplitTop(LineHeight, &Button, &MainView);
			if(Ui()->DoButtonLogic(&g_Config.m_SndEnable, 0, &Button, BUTTONFLAG_LEFT))
			{
				g_Config.m_SndEnable ^= 1;
				UpdateMusicState();
				return true;
			}
			return false;
		};
		AddCard(ToggleSpec, SoundToggleCardHeight, ToggleChromeHeight, [this, LineHeight, LineSpacing](CUIRect ContentRect) {
		CUIRect MainView = ContentRect;
		CUIRect Button;

		MainView.HSplitTop(LineHeight, &Button, &MainView);
		DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, -1, &g_Config.m_SndEnable, "Use sounds", Localize("Use sounds"), g_Config.m_SndEnable, &Button, SLabelProperties{}, false);

		m_NeedRestartSound = g_Config.m_SndEnable && !Sound()->IsSoundEnabled();
		if(!g_Config.m_SndEnable)
		{
			const bool PackChanged = str_comp(g_Config.m_SndPack, s_aSndPack) != 0;
			m_NeedRestartSound = m_NeedRestartSound || PackChanged;
			return;
		}

		MainView.HSplitTop(LineSpacing, nullptr, &MainView);
		MainView.HSplitTop(LineHeight, &Button, &MainView);
		if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndMusic, "Play background music", Localize("Play background music"), g_Config.m_SndMusic, &Button))
		{
			g_Config.m_SndMusic ^= 1;
			UpdateMusicState();
		}

		MainView.HSplitTop(LineSpacing, nullptr, &MainView);
		MainView.HSplitTop(LineHeight, &Button, &MainView);
		if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndNonactiveMute, "Mute when not active", Localize("Mute when not active"), g_Config.m_SndNonactiveMute, &Button))
			g_Config.m_SndNonactiveMute ^= 1;

		MainView.HSplitTop(LineSpacing, nullptr, &MainView);
		MainView.HSplitTop(LineHeight, &Button, &MainView);
		if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndGame, "Enable game sounds", Localize("Enable game sounds"), g_Config.m_SndGame, &Button))
			g_Config.m_SndGame ^= 1;

		MainView.HSplitTop(LineSpacing, nullptr, &MainView);
		MainView.HSplitTop(LineHeight, &Button, &MainView);
		if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndGun, "Enable gun sound", Localize("Enable gun sound"), g_Config.m_SndGun, &Button))
			g_Config.m_SndGun ^= 1;

		MainView.HSplitTop(LineSpacing, nullptr, &MainView);
		MainView.HSplitTop(LineHeight, &Button, &MainView);
		if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndLongPain, "Enable shout when holding fire while in water", Localize("Enable shout when holding fire while in water"), g_Config.m_SndLongPain, &Button))
			g_Config.m_SndLongPain ^= 1;

		MainView.HSplitTop(LineSpacing, nullptr, &MainView);
		MainView.HSplitTop(LineHeight, &Button, &MainView);
		if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndServerMessage, "Enable server message sound", Localize("Enable server message sound"), g_Config.m_SndServerMessage, &Button))
			g_Config.m_SndServerMessage ^= 1;

		MainView.HSplitTop(LineSpacing, nullptr, &MainView);
		MainView.HSplitTop(LineHeight, &Button, &MainView);
		if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndChat, "Enable regular chat sound", Localize("Enable regular chat sound"), g_Config.m_SndChat, &Button))
			g_Config.m_SndChat ^= 1;

		MainView.HSplitTop(LineSpacing, nullptr, &MainView);
		MainView.HSplitTop(LineHeight, &Button, &MainView);
		if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndTeamChat, "Enable team chat sound", Localize("Enable team chat sound"), g_Config.m_SndTeamChat, &Button))
			g_Config.m_SndTeamChat ^= 1;

		MainView.HSplitTop(LineSpacing, nullptr, &MainView);
		MainView.HSplitTop(LineHeight, &Button, &MainView);
		if(DoSettingsButton_CheckBox(SETTINGS_SOUND, -1, &g_Config.m_SndHighlight, "Enable highlighted chat sound", Localize("Enable highlighted chat sound"), g_Config.m_SndHighlight, &Button))
			g_Config.m_SndHighlight ^= 1; }, {}, true, ProcessSoundToggleInput, g_Config.m_SndEnable);
		vCards.back().m_Measure = [LineHeight, LineSpacing](float) {
			const int RowCount = g_Config.m_SndEnable ? 10 : 1;
			return LineHeight * RowCount + LineSpacing * maximum(0, RowCount - 1);
		};

		AddCard(VolumeSpec, SoundVolumeCardHeight, VolumeChromeHeight, [DoSoundNumericField, LineHeight, LineSpacing](CUIRect ContentRect) {
			CUIRect MainView = ContentRect;
			CUIRect VolumeButton;
			MainView.HSplitTop(LineHeight, &VolumeButton, &MainView);
			DoSoundNumericField("sound-volume", &g_Config.m_SndVolume, &g_Config.m_SndVolume, VolumeButton, Localize("Sound volume"));
			MainView.HSplitTop(LineSpacing, nullptr, &MainView);
			MainView.HSplitTop(LineHeight, &VolumeButton, &MainView);
			DoSoundNumericField("sound-game-volume", &g_Config.m_SndGameVolume, &g_Config.m_SndGameVolume, VolumeButton, Localize("Game sound volume"));
			MainView.HSplitTop(LineSpacing, nullptr, &MainView);
			MainView.HSplitTop(LineHeight, &VolumeButton, &MainView);
			DoSoundNumericField("sound-chat-volume", &g_Config.m_SndChatVolume, &g_Config.m_SndChatVolume, VolumeButton, Localize("Chat sound volume"));
			MainView.HSplitTop(LineSpacing, nullptr, &MainView);
			MainView.HSplitTop(LineHeight, &VolumeButton, &MainView);
			DoSoundNumericField("sound-map-volume", &g_Config.m_SndMapVolume, &g_Config.m_SndMapVolume, VolumeButton, Localize("Map sound volume"));
			MainView.HSplitTop(LineSpacing, nullptr, &MainView);
			MainView.HSplitTop(LineHeight, &VolumeButton, &MainView);
			DoSoundNumericField("sound-background-music-volume", &g_Config.m_SndBackgroundMusicVolume, &g_Config.m_SndBackgroundMusicVolume, VolumeButton, Localize("Background music volume")); }, []() { return g_Config.m_SndEnable != 0; });
		AddCard(AudioPackSpec, SoundAudioPackCardHeight, AudioPackChromeHeight, [this, SoundMetrics, LineHeight, LineSpacing, BodySize](CUIRect ContentRect) {
			CUIRect AudioPackView = ContentRect;
			static CButtonContainer s_AudioPackRefreshButton;
			static CButtonContainer s_AudioPackEditorButton;
			static CButtonContainer s_AudioPackDirectoryButton;
			static CListBox s_AudioPackListBox;
			EnsureSharedAudioPacks(Storage());

			auto RefreshAudioPackState = [&]() {
				RefreshSharedAudioPacks(Storage());
				if(g_Config.m_SndPack[0] == '\0')
					str_copy(g_Config.m_SndPack, "default", sizeof(g_Config.m_SndPack));
				if(m_AudioPackEditorState.m_PackNameInput.IsEmpty())
					m_AudioPackEditorState.m_PackNameInput.Set(g_Config.m_SndPack);
			};

			if(g_Config.m_SndPack[0] == '\0')
				str_copy(g_Config.m_SndPack, "default", sizeof(g_Config.m_SndPack));

			auto FindSelectedPackIndex = [&]() {
				int Result = FindAudioPackIndexByName(gs_vAudioPacks, g_Config.m_SndPack);
				if(Result < 0)
				{
					RefreshSharedAudioPacks(Storage());
					Result = FindAudioPackIndexByName(gs_vAudioPacks, g_Config.m_SndPack);
				}
				if(Result < 0)
				{
					str_copy(g_Config.m_SndPack, "default", sizeof(g_Config.m_SndPack));
					Result = 0;
				}
				return Result;
			};
			int SelectedPack = FindSelectedPackIndex();

			CUIRect AudioPackContent;
			AudioPackView.Margin(2.0f, &AudioPackContent);
			CUIRect HeaderRow, ListRow;
			AudioPackContent.HSplitTop(LineHeight, &HeaderRow, &AudioPackContent);
			AudioPackContent.HSplitTop(LineSpacing, nullptr, &AudioPackContent);
			ListRow = AudioPackContent;

			const float RefreshButtonW = LineHeight + LineSpacing;
			const float EditButtonW = minimum(168.0f, maximum(114.0f, TextRender()->TextWidth(BodySize, Localize("Edit audio pack"), -1, -1.0f) + 22.0f));
			const float DirectoryButtonW = minimum(176.0f, maximum(122.0f, TextRender()->TextWidth(BodySize, Localize("Audio pack directory"), -1, -1.0f) + 22.0f));
			CUIRect EditButton;
			CUIRect DirectoryButton;
			CUIRect RefreshButton;
			HeaderRow.VSplitRight(RefreshButtonW, &HeaderRow, &RefreshButton);
			RefreshButton.VMargin(2.0f, &RefreshButton);
			if(Ui()->DoButton_QmIcon(&s_AudioPackRefreshButton, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &RefreshButton, BUTTONFLAG_LEFT))
			{
				RefreshAudioPackState();
				SelectedPack = FindSelectedPackIndex();
			}
			HeaderRow.VSplitLeft(EditButtonW, &EditButton, &HeaderRow);
			EditButton.VMargin(2.0f, &EditButton);
			if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackEditorButton, "sound-edit-audio-pack", Localize("Edit audio pack"), 0, &EditButton))
				AudioPackEditorOpen(g_Config.m_SndPack);
			HeaderRow.VSplitLeft(6.0f, nullptr, &HeaderRow);
			HeaderRow.VSplitLeft(DirectoryButtonW, &DirectoryButton, &HeaderRow);
			DirectoryButton.VMargin(2.0f, &DirectoryButton);
			if(DoSettingsButton_Menu(SETTINGS_SOUND, -1, -1, &s_AudioPackDirectoryButton, "sound-audio-pack-directory", Localize("Audio pack directory"), 0, &DirectoryButton))
			{
				char aBuf[IO_MAX_PATH_LENGTH];
				Storage()->GetCompletePath(IStorage::TYPE_SAVE, "audio", aBuf, sizeof(aBuf));
				Client()->ViewFile(aBuf);
			}

			DrawRoundedSurface(Ui(), ListRow, ColorRGBA(0.0f, 0.0f, 0.0f, 0.12f), ColorRGBA(), 6.0f);
			ListRow.Margin(6.0f, &ListRow);

			const int OldSelectedPack = SelectedPack;
			s_AudioPackListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
			s_AudioPackListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);
			s_AudioPackListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_HOVER);
			s_AudioPackListBox.DoStart(LineHeight + LineSpacing, gs_vAudioPacks.size(), 1, 4, SelectedPack, &ListRow, false);

			// 数量徽章在行内居中，使用紧凑尺寸并保留上下间隙。
			const float BadgeFontSize = SoundMetrics.m_SmallSize;
			const float BadgeWidth = 28.0f * SoundMetrics.m_UiScale;
			const float BadgeHeight = SoundMetrics.m_BadgeHeight;

			for(size_t i = 0; i < gs_vAudioPacks.size(); ++i)
			{
				const SAudioPackEntry &Entry = gs_vAudioPacks[i];
				const CListboxItem Item = s_AudioPackListBox.DoNextItem(&Entry, SelectedPack == (int)i);
				if(!Item.m_Visible)
					continue;

				char aLabel[128];
				if(str_comp(Entry.m_aName, "default") == 0)
				{
					str_copy(aLabel, Localize("Default"), sizeof(aLabel));
				}
				else
				{
					str_copy(aLabel, Entry.m_aName, sizeof(aLabel));
				}

				CUIRect NameRect, BadgeRect;
				Item.m_Rect.VSplitRight(BadgeWidth, &NameRect, &BadgeRect);
				NameRect.VMargin(6.0f, &NameRect);
				BadgeRect.HMargin((BadgeRect.h - BadgeHeight) * 0.5f, &BadgeRect);

				char aBadge[32];
				str_format(aBadge, sizeof(aBadge), "%d", Entry.m_FileCount);
				DrawRoundedSurface(Ui(), BadgeRect, SelectedPack == (int)i ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), ColorRGBA(), ui_token::radius::TIGHT);

				Ui()->DoLabel(&NameRect, aLabel, BodySize, TEXTALIGN_ML);
				Ui()->DoLabel(&BadgeRect, aBadge, BadgeFontSize, TEXTALIGN_MC);
			}

			SelectedPack = s_AudioPackListBox.DoEnd();
			if(SelectedPack != OldSelectedPack && SelectedPack >= 0 && SelectedPack < (int)gs_vAudioPacks.size())
			{
				str_copy(g_Config.m_SndPack, gs_vAudioPacks[SelectedPack].m_aName, sizeof(g_Config.m_SndPack));
				if(GameClient()->m_Sounds.Reload())
				{
					str_copy(s_aSndPack, g_Config.m_SndPack, sizeof(s_aSndPack));
					UpdateMusicState();
				}
				if(!m_AudioPackEditorState.m_PackNameInput.IsActive())
					m_AudioPackEditorState.m_PackNameInput.Set(g_Config.m_SndPack);
			} }, []() { return g_Config.m_SndEnable != 0; });
	};
	const uint64_t SoundLayoutRevision = ResolveSettingsSoundLayoutRevision(RenderOnly, g_Config.m_SndEnable != 0, AudioPackCount);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, SoundLayoutRevision);

	const SQmScrollRequest ScrollRequest{EQmScrollProfile::SETTINGS_OUTER};
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest, UiScale, 0.0f);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	CQmScrollState &ScrollState = s_SoundScrollRegion.State();
	// Deck 通过同一个 region 消费该状态；显式取得它以固定页面唯一的滚动状态所有权。
	(void)ScrollState;
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = Ui()->MouseX();
	InputState.m_MouseY = Ui()->MouseY();
	InputState.m_MousePressed = Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = Ui()->MouseButton(0);
	InputState.m_MouseReleased = !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = Input()->ModifierIsPressed();
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = RenderOnly ? nullptr : &ScrollParams;
	const SSettingsCardDeckResult DeckResult = SettingsCardDeckForRenderPass().RenderCached(SoundCardCtx, SoundPage, "sound", DefinitionsRevision, BuildDefinitions, SettingsCardOrderModelForRenderPass(), RenderOnly ? nullptr : &s_SoundScrollRegion, InputState, SettingsCardMotionSpec(), SoundVisualOptions);
	if(!RenderOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
	const bool PackChanged = str_comp(g_Config.m_SndPack, s_aSndPack) != 0;
	m_NeedRestartSound = m_NeedRestartSound || PackChanged;
}

void CMenus::PrepareLanguagePageCache(float MainViewWidth, bool ForceComplete)
{
	EnsureLanguagePageCacheInit(Ui());
	if(!UseLanguagePageCache())
	{
		gs_aLanguageCacheLanguageFile[0] = '\0';
		gs_LanguagePageCacheComplete = false;
		return;
	}

	CUIRect List;
	LayoutLanguagePageBaseRects(MainViewWidth, List);
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainViewWidth);

	const float LabelWidth = LanguageListLabelWidth(List, Metrics);
	const bool LanguageChanged = str_comp(gs_aLanguageCacheLanguageFile, g_Config.m_ClLanguagefile) != 0;
	const bool LabelWidthChanged = absolute(gs_LanguageLabelWidth - LabelWidth) > 0.01f;
	const bool FontSizeChanged = absolute(gs_LanguageLabelFontSize - Metrics.m_BodySize) > 0.01f;
	if(LanguageChanged || LabelWidthChanged || FontSizeChanged)
		gs_LanguagePageCacheComplete = false;
	bool LabelCacheInvalid = g_Localization.Languages().size() > QM_LANGUAGE_ROW_CACHE_CAPACITY;
	if(ForceComplete && !LabelCacheInvalid)
	{
		for(size_t i = 0; i < g_Localization.Languages().size(); ++i)
		{
			CUIElement &LabelElement = SettingsTextElement(SETTINGS_LANGUAGE, -1, g_Localization.Languages()[i].m_Filename.c_str());
			if(!LabelElement.Rect(0)->m_UITextContainer.Valid())
			{
				LabelCacheInvalid = true;
				break;
			}
		}
	}
	if(!LanguageChanged &&
		!LabelCacheInvalid &&
		!LabelWidthChanged &&
		!FontSizeChanged &&
		gs_LanguagePageCacheComplete)
	{
		return;
	}

	CUIRect ScrollClip = List;
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest, Metrics.m_UiScale);
	ScrollClip.VSplitRight(ScrollPolicy.m_Style.m_ScrollbarWidth, &ScrollClip, nullptr);
	CUIRect Content = ScrollClip;
	for(size_t i = 0; i < g_Localization.Languages().size(); ++i)
	{
		const auto &Language = g_Localization.Languages()[i];
		CUIRect ItemRect;
		Content.HSplitTop(Metrics.m_ListRowHeight, &ItemRect, &Content);

		CUIRect FlagRect, Label;
		ItemRect.VSplitLeft(ItemRect.h * 2.0f, &FlagRect, &Label);
		CUIElement &LabelElement = SettingsTextElement(SETTINGS_LANGUAGE, -1, Language.m_Filename.c_str());
		CUIElement::SUIElementRect &RectEl = *LabelElement.Rect(0);
		const bool ColorChanged = RectEl.m_TextColor != TextRender()->GetTextColor() || RectEl.m_TextOutlineColor != TextRender()->GetTextOutlineColor();
		const bool TextChanged = RectEl.m_Text != Language.m_Name.c_str();
		const bool SizeChanged = RectEl.m_Width != Label.w || RectEl.m_Height != Label.h;
		const bool NeedsTextContainer = !RectEl.m_UITextContainer.Valid() || ColorChanged || TextChanged || SizeChanged;
		if(!ForceComplete && NeedsTextContainer && !SettingsWarmupConsumeBudget(m_SettingsFrameBudget, ESettingsWarmupCost::TEXT_CONTAINER))
			return;
		DoSettingsLabelStreamed(LabelElement, &Label, Language.m_Name.c_str(), Metrics.m_BodySize, TEXTALIGN_ML, {}, -1, nullptr, false);
	}

	gs_LanguageLabelWidth = LabelWidth;
	gs_LanguageLabelFontSize = Metrics.m_BodySize;
	str_copy(gs_aLanguageCacheLanguageFile, g_Config.m_ClLanguagefile, sizeof(gs_aLanguageCacheLanguageFile));
	gs_LanguagePageCacheComplete = true;
}

void CMenus::RenderLanguageSettings(CUIRect MainView)
{
	CPerfTimer RenderTimer;
	const char *pCreditsText = Localize("English translation by the DDNet Team", "Translation credits: Add your own name here when you update translations");
	const int NumLanguages = (int)g_Localization.Languages().size();
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	EnsureLanguagePageCacheInit(Ui());

	CUIRect Header, CreditsButton, List;
	MainView.HSplitTop(Metrics.m_ButtonHeight, &Header, &List);
	List.HSplitTop(Metrics.m_LineSpacing, nullptr, &List);
	Header.VSplitRight(130.0f * Metrics.m_UiScale, nullptr, &CreditsButton);
	PrepareLanguagePageCache(List.w, true);
	static CButtonContainer s_CreditsButton;
	static CUi::SMessagePopupContext s_CreditsPopup;
	if(DoSettingsButton_Menu(SETTINGS_LANGUAGE, -1, -1, &s_CreditsButton, "language-credits", Localize("Credits"), 0, &CreditsButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), 0.0f, Metrics.m_BodySize))
	{
		str_copy(s_CreditsPopup.m_aMessage, pCreditsText, sizeof(s_CreditsPopup.m_aMessage));
		s_CreditsPopup.DefaultColor(TextRender());
		Ui()->ShowPopupMessage(CreditsButton.x, CreditsButton.y + CreditsButton.h + 5.0f, &s_CreditsPopup);
	}

	{
		CPerfTimer StageTimer;
		RenderLanguageSelection(List);
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "page=language languages=%d", NumLanguages);
		LogPerfStage(Client(), "language_list_total", StageTimer.ElapsedMs(), false, aExtra);
	}
	LogPerfStage(Client(), "language_page_total", RenderTimer.ElapsedMs(), false, "page=language");
}

bool CMenus::RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)
{
	const bool MenuUiPerfEnabled = QmPerfEnabled();
	const auto MenuUiStartTime = MenuUiPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	static int s_SelectedLanguage = -2; // -2 = unloaded, -1 = unset
	EnsureLanguagePageCacheInit(Ui());
	const bool UseCache = UseLanguagePageCache();
	const SSettingsContentMetrics Metrics = pMetrics != nullptr ? *pMetrics : ResolveSettingsContentMetrics(MainView.w);

	if(s_SelectedLanguage == -2)
	{
		s_SelectedLanguage = -1;
		for(size_t i = 0; i < g_Localization.Languages().size(); i++)
		{
			if(str_comp(g_Localization.Languages()[i].m_Filename.c_str(), g_Config.m_ClLanguagefile) == 0)
			{
				s_SelectedLanguage = i;
				gs_LanguageScrollToSelected = true;
				break;
			}
		}
	}

	const int SelectedOld = s_SelectedLanguage;
	bool Activated = false;

	vec2 ScrollOffset(0.0f, 0.0f);
	static float s_PrevLanguageScrollY = 0.0f;
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;
	ScrollRequest.m_RowExtent = Metrics.m_ListRowHeight;
	ScrollRequest.m_RowsPerStep = 3;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	ScrollParams.m_WheelOwnerPriority = EUiWheelOwnerPriority::COMPOSITE_CONTROL;
	SSettingsScrollRegionFrame ScrollFrame = BeginSettingsScrollRegion(gs_LanguageScrollRegion, &MainView, ScrollParams, s_PrevLanguageScrollY);
	ScrollOffset = ScrollFrame.m_BeginOffset;

	CUIRect Content = MainView;
	Content.y += ScrollOffset.y;
	int VisibleLanguages = 0;
	int CacheHits = 0;
	int CacheMisses = 0;
	for(size_t i = 0; i < g_Localization.Languages().size(); ++i)
	{
		const auto &Language = g_Localization.Languages()[i];
		CUIRect ItemRect;
		Content.HSplitTop(Metrics.m_ListRowHeight, &ItemRect, &Content);
		const bool Selected = s_SelectedLanguage == (int)i;
		const bool Visible = gs_LanguageScrollRegion.AddRect(ItemRect, gs_LanguageScrollToSelected && Selected);
		if(!Visible)
			continue;
		++VisibleLanguages;

		void *pRowId = UseCache ? static_cast<void *>(&gs_aLanguageRowIds[i]) : const_cast<char *>(Language.m_Filename.c_str());
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
		const int ButtonResult = Ui()->DoButtonLogic(pRowId, 0, &ItemRect, BUTTONFLAG_LEFT);
		if(ButtonResult)
		{
			s_SelectedLanguage = i;
			Activated = true;
		}

		if(Selected)
			DrawRoundedSurface(Ui(), ItemRect, Ui()->ScaleBackgroundAlpha(ui_token::color::LIST_ITEM_SELECTED), ColorRGBA(), 5.0f);
		if(Ui()->HotItem() == pRowId)
			DrawRoundedSurface(Ui(), ItemRect, Ui()->ScaleBackgroundAlpha(ui_token::color::LIST_ITEM_HOVER), ColorRGBA(), 5.0f);

		CUIRect FlagRect, Label;
		ItemRect.VSplitLeft(ItemRect.h * 2.0f, &FlagRect, &Label);
		FlagRect.VMargin(6.0f, &FlagRect);
		FlagRect.HMargin(3.0f, &FlagRect);
		GameClient()->m_CountryFlags.Render(Language.m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);
		if(UseCache)
		{
			CUIElement &LabelElement = SettingsTextElement(SETTINGS_LANGUAGE, -1, Language.m_Filename.c_str());
			if(LabelElement.Rect(0)->m_UITextContainer.Valid())
				++CacheHits;
			else
				++CacheMisses;
			DoSettingsLabelStreamed(LabelElement, &Label, Language.m_Name.c_str(), Metrics.m_BodySize, TEXTALIGN_ML);
		}
		else
			Ui()->DoLabel(&Label, Language.m_Name.c_str(), Metrics.m_BodySize, TEXTALIGN_ML);
	}
	gs_LanguageScrollToSelected = false;
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = Content.y;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	FinishSettingsScrollRegion(gs_LanguageScrollRegion, ScrollFrame, &ScrollRegion, SETTINGS_LANGUAGE);
	s_PrevLanguageScrollY = ScrollFrame.m_FinalOffsetY;
	const bool LanguageScrollActive = QmMenuUiScrollPerfActive(gs_LanguageScrollRegion.WheelConsumedThisFrame(), gs_LanguageScrollRegion.Active(), gs_LanguageScrollRegion.Animating());
	if(LanguageScrollActive)
	{
		StartSettingsPerfScrollWindow("language_list_scroll", SettingsPerfContextName(), "settings:language", "none");
		SQmMenuUiFramePerf MenuUiPerf;
		MenuUiPerf.m_pPage = "settings:language";
		MenuUiPerf.m_pOperation = "language_list_scroll";
		MenuUiPerf.m_ItemsTotal = (int)g_Localization.Languages().size();
		MenuUiPerf.m_ItemsVisible = VisibleLanguages;
		MenuUiPerf.m_ItemsProcessed = VisibleLanguages;
		MenuUiPerf.m_ItemsSkipped = maximum(0, MenuUiPerf.m_ItemsTotal - VisibleLanguages);
		MenuUiPerf.m_UiMs = MenuUiPerfEnabled ? (float)std::chrono::duration<double, std::milli>(time_get_nanoseconds() - MenuUiStartTime).count() : -1.0f;
		MenuUiPerf.m_CacheHits = CacheHits;
		MenuUiPerf.m_CacheMisses = CacheMisses;
		QmLogMenuUiFramePerf(MenuUiPerf, Client());
	}

	if(SelectedOld != s_SelectedLanguage)
	{
		str_copy(g_Config.m_ClLanguagefile, g_Localization.Languages()[s_SelectedLanguage].m_Filename.c_str());
		GameClient()->OnLanguageChange();
	}

	return Activated;
}

void CMenus::RenderSettings(CUIRect MainView)
{
	const bool CollectingMenuTextPlan = m_MenuTextPlanCollecting;
	const bool SettingsPerfEnabled = PerfDebugEnabled() && !CollectingMenuTextPlan;
	const int64_t SettingsRenderStartTime = PerfDebugStartTime();
	// This handles cases where old config files have an invalid page index
	if(!CollectingMenuTextPlan)
	{
		m_SettingsScrollActive = Input()->KeyPress(KEY_MOUSE_WHEEL_UP) ||
					 Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) ||
					 Input()->KeyPress(KEY_MOUSE_WHEEL_LEFT) ||
					 Input()->KeyPress(KEY_MOUSE_WHEEL_RIGHT);
	}
	if(g_Config.m_UiSettingsPage < 0 || g_Config.m_UiSettingsPage >= SETTINGS_LENGTH)
		g_Config.m_UiSettingsPage = SETTINGS_GENERAL;
	if(g_Config.m_UiSettingsPage == SETTINGS_CONFIGS)
	{
		g_Config.m_UiSettingsPage = SETTINGS_QMCLIENT;
		m_QmClientSettingsTab = QMCLIENT_SETTINGS_TAB_CONFIG;
	}
	else
	{
		g_Config.m_UiSettingsPage = SettingsCanonicalPage(g_Config.m_UiSettingsPage);
	}
	if(!CollectingMenuTextPlan && g_Config.m_UiSettingsPage != SETTINGS_ASSETS && (m_AssetsEditorState.m_Open || m_AssetsEditorState.m_Initialized))
		AssetsEditorCloseNow();
	if(!CollectingMenuTextPlan && g_Config.m_UiSettingsPage != SETTINGS_SOUND && (m_AudioPackEditorState.m_Open || m_AudioPackEditorState.m_Initialized))
		AudioPackEditorClose();

	static bool s_SettingsTransitionInitialized = false;
	static int s_PrevSettingsPage = SETTINGS_GENERAL;

	// render background
	const int64_t ShellLayoutStartTime = PerfDebugStartTime();
	CUIRect Button, TabBar, RestartBar;
	const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound || m_NeedRestartUpdate;
	const SSettingsShellLayoutFrame Shell = ResolveSettingsShellLayout(MainView, NeedRestart ? 30.0f : 0.0f);
	m_SettingsShellLayout = Shell;
	m_SettingsContentMetrics = ResolveSettingsContentMetrics(Shell.m_ContentRect.w);
	m_SettingsShellLayoutValid = true;
	MainView = Shell.m_ContentRect;
	TabBar = Shell.m_TabBarRect;
	if(NeedRestart)
		RestartBar = Shell.m_RestartBarRect;
	if(!CollectingMenuTextPlan)
	{
		TabBar.Draw(SettingsTabbarColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);
		Shell.m_ContentPanelRect.Draw(MenuPanelColor(), IGraphics::CORNER_ALL, ui_token::radius::CARD);
	}
	const float PreviousDropDownFontSize = Ui()->DropDownFontSize();
	Ui()->SetDropDownFontSize(m_SettingsContentMetrics.m_BodySize);

	TabBar.Margin(10.0f, &TabBar);
	TabBar.HSplitTop(38.0f, &Button, &TabBar);
	DoSettingsMenuLabel(SETTINGS_GENERAL, -1, -1, "settings-shell-title", &Button, Localize("Settings"), ui_token::font::HEADLINE_LG, TEXTALIGN_MC);
	if(SettingsPerfEnabled)
	{
		char aSettingsPerfTab[16];
		const char *pSettingsPerfTab = "none";
		if(g_Config.m_UiSettingsPage == SETTINGS_QMCLIENT)
		{
			str_format(aSettingsPerfTab, sizeof(aSettingsPerfTab), "%d", m_QmClientSettingsTab);
			pSettingsPerfTab = aSettingsPerfTab;
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
		{
			str_format(aSettingsPerfTab, sizeof(aSettingsPerfTab), "%d", m_TClientSettingsTab);
			pSettingsPerfTab = aSettingsPerfTab;
		}
		char aShellExtra[192];
		str_format(aShellExtra, sizeof(aShellExtra), "context=%s page=%s tab=%s operation=%s restart=%d",
			SettingsPerfContextName(), SettingsPageName(g_Config.m_UiSettingsPage), pSettingsPerfTab, SettingsPerfActiveOperation(), NeedRestart ? 1 : 0);
		LogPerfStage(Client(), "settings_shell_layout", PerfDebugElapsedMs(ShellLayoutStartTime), false, aShellExtra);
	}

	const float SettingsTabBarButtonWidth = std::max(0.0f, TabBar.w - 20.0f);
	PrepareSettingsTabLabelCache(MainView.w, SettingsTabBarButtonWidth);

	{
		CPerfTimer StageTimer;
		static constexpr int s_aSettingsTabOrder[] = {
			SETTINGS_GENERAL,
			SETTINGS_TEE,
			SETTINGS_APPEARANCE,
			SETTINGS_CONTROLS,
			SETTINGS_GRAPHICS,
			SETTINGS_SOUND,
			SETTINGS_ASSETS,
			SETTINGS_DDNET,
			SETTINGS_TCLIENT,
			SETTINGS_QMCLIENT,
			SETTINGS_SEARCH,
			SETTINGS_CONTRIBUTORS,
		};
		// 竖排页签（设置页左栏）保持原来的分块选中/悬停底色，不做胶囊滑块。
		const ColorRGBA SettingsNavigationSelected = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.42f);
		const ColorRGBA SettingsNavigationHover = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.20f);
		for(int i : s_aSettingsTabOrder)
		{
			if(!SettingsPageVisibleInRightTabBar(i))
				continue;
			const bool Active = g_Config.m_UiSettingsPage == i;
			TabBar.HSplitTop(ui_token::settings::TAB_GAP, nullptr, &TabBar);
			TabBar.HSplitTop(ui_token::settings::TAB_HEIGHT, &Button, &TabBar);
			if(DoButton_MenuTab(&m_aSettingsTabButtons[i], m_apSettingsTabs[i], Active, &Button, IGraphics::CORNER_ALL, &m_aAnimatorsSettingsTab[i], nullptr, &SettingsNavigationSelected, &SettingsNavigationHover, 10.0f, nullptr, &m_aSettingsTabLabelElements[i]))
				g_Config.m_UiSettingsPage = i;
		}

		if(SettingsPerfEnabled)
		{
			char aSettingsPerfTab[16];
			const char *pSettingsPerfTab = "none";
			if(g_Config.m_UiSettingsPage == SETTINGS_QMCLIENT)
			{
				str_format(aSettingsPerfTab, sizeof(aSettingsPerfTab), "%d", m_QmClientSettingsTab);
				pSettingsPerfTab = aSettingsPerfTab;
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
			{
				str_format(aSettingsPerfTab, sizeof(aSettingsPerfTab), "%d", m_TClientSettingsTab);
				pSettingsPerfTab = aSettingsPerfTab;
			}
			char aTabBarExtra[128];
			str_format(aTabBarExtra, sizeof(aTabBarExtra), "context=%s page=%s tab=%s operation=%s", SettingsPerfContextName(), SettingsPageName(g_Config.m_UiSettingsPage), pSettingsPerfTab, SettingsPerfActiveOperation());
			LogPerfStage(Client(), "settings_tabbar", StageTimer.ElapsedMs(), false, aTabBarExtra);
		}
	}
	const uint64_t SettingsDisplayViewKey = ResolveSettingsCardDisplayViewKey(g_Config.m_UiSettingsPage, m_Dummy ? 1 : 0, m_AppearanceSettingsTab, m_TClientSettingsTab, m_QmClientSettingsTab, m_CreditsSettingsTab);
	if(!CollectingMenuTextPlan && g_Config.m_UiSettingsPage != SETTINGS_TEE && m_SettingsCardDeckDisplayState.EnterView(SettingsDisplayViewKey))
	{
		m_SettingsCardDeck.BeginDisplayCycle(++m_SettingsCardDeckDisplayCycle, true);
	}

	if(!CollectingMenuTextPlan)
	{
		if(!s_SettingsTransitionInitialized)
		{
			s_PrevSettingsPage = g_Config.m_UiSettingsPage;
			s_SettingsTransitionInitialized = true;
			if(g_Config.m_UiSettingsPage == SETTINGS_TEE || g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
				m_TeeEntranceStartTime = time_get();
		}
		else if(g_Config.m_UiSettingsPage != s_PrevSettingsPage)
		{
			CommitSettingsTeeSkinEdits();
			if(s_PrevSettingsPage == SETTINGS_TEE && g_Config.m_UiSettingsPage != SETTINGS_TEE)
				FinalizeTeeListDrainPerfSession();
			if(g_Config.m_UiSettingsPage == SETTINGS_TEE || g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
				m_TeeEntranceStartTime = time_get();
			if(PerfDebugEnabled())
			{
				char aPayload[160];
				str_format(aPayload, sizeof(aPayload), "event=page_switch from=%s to=%s dur_ms=%.3f source=settings_page_switch",
					SettingsPageName(s_PrevSettingsPage), SettingsPageName(g_Config.m_UiSettingsPage), 0.0);
				QmPerfLogPayload("perf/interaction", aPayload, Client(), "settings");
			}
			char aWindowTab[16];
			const char *pWindowTab = "none";
			if(g_Config.m_UiSettingsPage == SETTINGS_QMCLIENT)
			{
				str_format(aWindowTab, sizeof(aWindowTab), "%d", m_QmClientSettingsTab);
				pWindowTab = aWindowTab;
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
			{
				str_format(aWindowTab, sizeof(aWindowTab), "%d", m_TClientSettingsTab);
				pWindowTab = aWindowTab;
			}
			if(m_SettingsPerfLastPage != -1)
				StartSettingsPerfFixedWindow("settings_tab_switch", SettingsPerfContextName(), CurrentQmUiPerfPage(), pWindowTab, 30);
			// 设置页卡片使用稳定位置呈现。整页位移会在半透明卡片移动时露出壳层底色，
			// 表现为仅在开启动效后出现的背景闪烁。
			s_PrevSettingsPage = g_Config.m_UiSettingsPage;
		}
	}

	CUIRect ContentView = MainView;
	m_SettingsPageSwitchActive = false;

	{
		CPerfTimer StageTimer;
		std::optional<CScopedMenuTextVisibleGuard> TextVisibleGuard;
		if(!CollectingMenuTextPlan)
			TextVisibleGuard.emplace(this);
		const int PreviousTextContextPage = m_SettingsTextContextPage;
		const int PreviousTextContextTab = m_SettingsTextContextTab;
		const int PreviousTextContextSubtab = m_SettingsTextContextSubtab;
		m_SettingsTextContextPage = g_Config.m_UiSettingsPage;
		m_SettingsTextContextTab = g_Config.m_UiSettingsPage == SETTINGS_TCLIENT ? m_TClientSettingsTab : (g_Config.m_UiSettingsPage == SETTINGS_QMCLIENT ? m_QmClientSettingsTab : -1);
		m_SettingsTextContextSubtab = m_SettingsTextContextTab;
		int NumSections = 0;
		int NumSectionsVisible = 0;
		if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
			RenderSettingsGeneral(ContentView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_TEE)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_TEE);
			if(Client()->IsSixup())
				RenderSettingsTee7(ContentView);
			else
				RenderSettingsTee(ContentView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_APPEARANCE)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_APPEARANCE);
			RenderSettingsAppearance(ContentView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_CONTROLS)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_CONTROLS);
			m_MenusSettingsControls.Render(ContentView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_GRAPHICS)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GRAPHICS);
			RenderSettingsGraphics(ContentView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_SOUND)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_SOUND);
			RenderSettingsSound(ContentView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_DDNET)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_DDNET);
			RenderSettingsDDNet(ContentView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_ASSETS)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_ASSETS);
			RenderSettingsCustom(ContentView);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(13);
			RenderSettingsTClient(ContentView, CollectingMenuTextPlan);
			if(!CollectingMenuTextPlan)
				m_SettingsRuntimeMetadata.m_LastTClientTab = m_TClientSettingsTab;
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_QMCLIENT)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(15);
			RenderSettingsQmClient(ContentView, CollectingMenuTextPlan);
			if(!CollectingMenuTextPlan)
				m_SettingsRuntimeMetadata.m_LastQmTab = m_QmClientSettingsTab;
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_SEARCH)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(15);
			RenderSettingsGlobalSearch(ContentView, CollectingMenuTextPlan);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_CONTRIBUTORS)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(15);
			RenderSettingsContributors(ContentView, CollectingMenuTextPlan);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_PROFILES)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(14);
			RenderSettingsTClientProfiles(ContentView);
		}
		else
		{
			dbg_assert_failed("ui_settings_page invalid");
		}
		char aContentTab[16];
		const char *pTab = "none";
		if(g_Config.m_UiSettingsPage == SETTINGS_QMCLIENT)
		{
			str_format(aContentTab, sizeof(aContentTab), "%d", m_QmClientSettingsTab);
			pTab = aContentTab;
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
		{
			str_format(aContentTab, sizeof(aContentTab), "%d", m_TClientSettingsTab);
			pTab = aContentTab;
		}
		if(SettingsPerfEnabled)
		{
			char aContentExtra[192];
			str_format(aContentExtra, sizeof(aContentExtra), "context=%s page=%s transition=0 sections=%d sections_visible=%d tab=%s operation=%s", SettingsPerfContextName(), SettingsPageName(g_Config.m_UiSettingsPage), NumSections, NumSectionsVisible, pTab, SettingsPerfActiveOperation());
			LogPerfStage(Client(), "settings_page_content", StageTimer.ElapsedMs(), false, aContentExtra);
		}
		m_SettingsTextContextPage = PreviousTextContextPage;
		m_SettingsTextContextTab = PreviousTextContextTab;
		m_SettingsTextContextSubtab = PreviousTextContextSubtab;
	}

	if(!CollectingMenuTextPlan && m_SettingsPerfLastPage == g_Config.m_UiSettingsPage)
	{
		if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT && m_SettingsPerfLastTClientTab != -1 && m_SettingsPerfLastTClientTab != m_TClientSettingsTab)
		{
			char aTab[16];
			str_format(aTab, sizeof(aTab), "%d", m_TClientSettingsTab);
			StartSettingsPerfFixedWindow("settings_subtab_switch", SettingsPerfContextName(), CurrentQmUiPerfPage(), aTab, 30);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_QMCLIENT && m_SettingsPerfLastQmClientTab != -1 && m_SettingsPerfLastQmClientTab != m_QmClientSettingsTab)
		{
			char aTab[16];
			str_format(aTab, sizeof(aTab), "%d", m_QmClientSettingsTab);
			StartSettingsPerfFixedWindow("settings_subtab_switch", SettingsPerfContextName(), CurrentQmUiPerfPage(), aTab, 30);
		}
	}
	if(!CollectingMenuTextPlan)
	{
		m_SettingsPerfLastPage = g_Config.m_UiSettingsPage;
		m_SettingsPerfLastTClientTab = m_TClientSettingsTab;
		m_SettingsPerfLastQmClientTab = m_QmClientSettingsTab;

		m_SettingsRuntimeMetadata.m_LastPage = g_Config.m_UiSettingsPage;
		m_SettingsRuntimeMetadata.m_Valid = true;
	}

	{
		const int64_t StageStartTime = PerfDebugStartTime();
		if(SettingsPerfEnabled)
		{
			char aSettingsPerfTab[16];
			const char *pSettingsPerfTab = "none";
			if(g_Config.m_UiSettingsPage == SETTINGS_QMCLIENT)
			{
				str_format(aSettingsPerfTab, sizeof(aSettingsPerfTab), "%d", m_QmClientSettingsTab);
				pSettingsPerfTab = aSettingsPerfTab;
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
			{
				str_format(aSettingsPerfTab, sizeof(aSettingsPerfTab), "%d", m_TClientSettingsTab);
				pSettingsPerfTab = aSettingsPerfTab;
			}
			char aOverlayExtra[160];
			str_format(aOverlayExtra, sizeof(aOverlayExtra), "context=%s page=%s tab=%s operation=%s active=0",
				SettingsPerfContextName(), SettingsPageName(g_Config.m_UiSettingsPage), pSettingsPerfTab, SettingsPerfActiveOperation());
			LogPerfStage(Client(), "settings_transition_overlay", PerfDebugElapsedMs(StageStartTime), false, aOverlayExtra);
		}
	}
	if(NeedRestart)
	{
		const int64_t StageStartTime = PerfDebugStartTime();
		const SSettingsContentMetrics RestartMetrics = ResolveSettingsContentMetrics(MainView.w);
		CUIRect RestartWarning, RestartButton;
		RestartBar.VSplitRight(125.0f * RestartMetrics.m_UiScale, &RestartWarning, &RestartButton);
		RestartWarning.VSplitRight(RestartMetrics.m_SectionGap, &RestartWarning, nullptr);
		if(m_NeedRestartUpdate)
		{
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			DoSettingsMenuLabel(g_Config.m_UiSettingsPage, -1, -1, "settings-restart-update-warning", &RestartWarning, Localize("DDNet Client needs to be restarted to complete update!"), RestartMetrics.m_HeadlineSize, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			DoSettingsMenuLabel(g_Config.m_UiSettingsPage, -1, -1, "settings-restart-required-warning", &RestartWarning, Localize("You must restart the game for all settings to take effect."), RestartMetrics.m_HeadlineSize, TEXTALIGN_ML);
		}

		static CButtonContainer s_RestartButton;
		if(DoSettingsButton_Menu(g_Config.m_UiSettingsPage, -1, -1, &s_RestartButton, "settings-restart-button", Localize("Restart"), 0, &RestartButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), 0.0f, RestartMetrics.m_BodySize))
		{
			if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
			{
				m_Popup = POPUP_RESTART;
			}
			else
			{
				Client()->Restart();
			}
		}
		if(SettingsPerfEnabled)
		{
			char aSettingsPerfTab[16];
			const char *pSettingsPerfTab = "none";
			if(g_Config.m_UiSettingsPage == SETTINGS_QMCLIENT)
			{
				str_format(aSettingsPerfTab, sizeof(aSettingsPerfTab), "%d", m_QmClientSettingsTab);
				pSettingsPerfTab = aSettingsPerfTab;
			}
			else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
			{
				str_format(aSettingsPerfTab, sizeof(aSettingsPerfTab), "%d", m_TClientSettingsTab);
				pSettingsPerfTab = aSettingsPerfTab;
			}
			char aRestartExtra[160];
			str_format(aRestartExtra, sizeof(aRestartExtra), "context=%s page=%s tab=%s operation=%s",
				SettingsPerfContextName(), SettingsPageName(g_Config.m_UiSettingsPage), pSettingsPerfTab, SettingsPerfActiveOperation());
			LogPerfStage(Client(), "settings_restart_bar", PerfDebugElapsedMs(StageStartTime), false, aRestartExtra);
		}
	}
	if(SettingsPerfEnabled)
	{
		char aSettingsPerfTab[16];
		const char *pSettingsPerfTab = "none";
		if(g_Config.m_UiSettingsPage == SETTINGS_QMCLIENT)
		{
			str_format(aSettingsPerfTab, sizeof(aSettingsPerfTab), "%d", m_QmClientSettingsTab);
			pSettingsPerfTab = aSettingsPerfTab;
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_TCLIENT)
		{
			str_format(aSettingsPerfTab, sizeof(aSettingsPerfTab), "%d", m_TClientSettingsTab);
			pSettingsPerfTab = aSettingsPerfTab;
		}
		char aRenderTotalExtra[160];
		str_format(aRenderTotalExtra, sizeof(aRenderTotalExtra), "context=%s page=%s tab=%s operation=%s",
			SettingsPerfContextName(), SettingsPageName(g_Config.m_UiSettingsPage), pSettingsPerfTab, SettingsPerfActiveOperation());
		LogPerfStage(Client(), "settings_render_total", PerfDebugElapsedMs(SettingsRenderStartTime), false, aRenderTotalExtra);
	}
	Ui()->SetDropDownFontSize(PreviousDropDownFontSize);
	m_SettingsShellLayoutValid = false;
}

bool CMenus::RenderHslaScrollbars(CUIRect *pRect, unsigned int *pColor, bool Alpha, float DarkestLight, const SSettingsContentMetrics &Metrics)
{
	const unsigned PrevPackedColor = *pColor;
	ColorHSLA Color(*pColor, Alpha);
	const ColorHSLA OriginalColor = Color;
	const char *apLabels[] = {Localize("Hue"), Localize("Sat."), Localize("Lht."), Localize("Alpha")};
	const float SizePerEntry = Metrics.m_LineHeight;
	const float MarginPerEntry = Metrics.m_LineSpacing;
	const float PreviewMargin = std::max(1.0f, Metrics.m_LineSpacing * 0.5f);
	const float RowsHeight = ResolveSettingsHslaRowsHeight(Metrics, Alpha);
	const float PreviewHeight = std::min(RowsHeight, Metrics.m_LineHeight * 2.0f + Metrics.m_LineSpacing);
	const float OffY = std::max(0.0f, RowsHeight - PreviewHeight);

	CUIRect Preview;
	pRect->VSplitLeft(PreviewHeight, &Preview, pRect);
	pRect->VSplitLeft(Metrics.m_LineSpacing, nullptr, pRect);
	Preview.HSplitTop(OffY / 2.0f, nullptr, &Preview);
	Preview.HSplitTop(PreviewHeight, &Preview, nullptr);

	Preview.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, ui_token::radius::BASE + PreviewMargin);
	Preview.Margin(PreviewMargin, &Preview);
	Preview.Draw(color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight)), IGraphics::CORNER_ALL, ui_token::radius::BASE + PreviewMargin);

	auto &&RenderHueRect = [&](CUIRect *pColorRect) {
		float CurXOff = pColorRect->x;
		const float SizeColor = pColorRect->w / 6;

		// red to yellow
		{
			IGraphics::CColorVertex aColors[4] = {
				IGraphics::CColorVertex(0, ColorRGBA(1, 0, 0, 1)),
				IGraphics::CColorVertex(1, ColorRGBA(1, 1, 0, 1)),
				IGraphics::CColorVertex(2, ColorRGBA(1, 0, 0, 1)),
				IGraphics::CColorVertex(3, ColorRGBA(1, 1, 0, 1)),
			};
			Graphics()->SetColorVertex(aColors, std::size(aColors));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		// yellow to green
		CurXOff += SizeColor;
		{
			IGraphics::CColorVertex aColors[4] = {
				IGraphics::CColorVertex(0, ColorRGBA(1, 1, 0, 1)),
				IGraphics::CColorVertex(1, ColorRGBA(0, 1, 0, 1)),
				IGraphics::CColorVertex(2, ColorRGBA(1, 1, 0, 1)),
				IGraphics::CColorVertex(3, ColorRGBA(0, 1, 0, 1)),
			};
			Graphics()->SetColorVertex(aColors, std::size(aColors));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// green to turquoise
		{
			IGraphics::CColorVertex aColors[4] = {
				IGraphics::CColorVertex(0, ColorRGBA(0, 1, 0, 1)),
				IGraphics::CColorVertex(1, ColorRGBA(0, 1, 1, 1)),
				IGraphics::CColorVertex(2, ColorRGBA(0, 1, 0, 1)),
				IGraphics::CColorVertex(3, ColorRGBA(0, 1, 1, 1)),
			};
			Graphics()->SetColorVertex(aColors, std::size(aColors));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// turquoise to blue
		{
			IGraphics::CColorVertex aColors[4] = {
				IGraphics::CColorVertex(0, ColorRGBA(0, 1, 1, 1)),
				IGraphics::CColorVertex(1, ColorRGBA(0, 0, 1, 1)),
				IGraphics::CColorVertex(2, ColorRGBA(0, 1, 1, 1)),
				IGraphics::CColorVertex(3, ColorRGBA(0, 0, 1, 1)),
			};
			Graphics()->SetColorVertex(aColors, std::size(aColors));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// blue to purple
		{
			IGraphics::CColorVertex aColors[4] = {
				IGraphics::CColorVertex(0, ColorRGBA(0, 0, 1, 1)),
				IGraphics::CColorVertex(1, ColorRGBA(1, 0, 1, 1)),
				IGraphics::CColorVertex(2, ColorRGBA(0, 0, 1, 1)),
				IGraphics::CColorVertex(3, ColorRGBA(1, 0, 1, 1)),
			};
			Graphics()->SetColorVertex(aColors, std::size(aColors));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// purple to red
		{
			IGraphics::CColorVertex aColors[4] = {
				IGraphics::CColorVertex(0, ColorRGBA(1, 0, 1, 1)),
				IGraphics::CColorVertex(1, ColorRGBA(1, 0, 0, 1)),
				IGraphics::CColorVertex(2, ColorRGBA(1, 0, 1, 1)),
				IGraphics::CColorVertex(3, ColorRGBA(1, 0, 0, 1)),
			};
			Graphics()->SetColorVertex(aColors, std::size(aColors));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}
	};

	auto &&RenderSaturationRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.s = 0.0f;
		RightColor.s = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderLightingRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.l = DarkestLight;
		RightColor.l = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderAlphaRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColorFull) {
		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(0.0f));
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(1.0f));

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	const int EntryCount = 3 + Alpha;
	for(int i = 0; i < EntryCount; i++)
	{
		CUIRect Button, Label;
		pRect->HSplitTop(SizePerEntry, &Button, pRect);
		if(i + 1 < EntryCount)
			pRect->HSplitTop(MarginPerEntry, nullptr, pRect);
		const float MinimumLabelWidth = 72.0f * Metrics.m_UiScale;
		const float MaximumLabelWidth = std::max(MinimumLabelWidth, pRect->w - 80.0f * Metrics.m_UiScale);
		const float LabelWidth = std::clamp(pRect->w * 0.36f, MinimumLabelWidth, MaximumLabelWidth);
		Button.VSplitLeft(LabelWidth, &Label, &Button);
		Label.VMargin(Metrics.m_LineSpacing, &Label);

		Button.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, ui_token::radius::TIGHT);

		CUIRect Rail;
		Button.Margin(2.0f, &Rail);

		char aBuf[32];

		// Hue
		if(i == 0)
			str_format(aBuf, sizeof(aBuf), "%s: %.1f° (%03d)", apLabels[i], Color[i] * 360.0f, round_to_int(Color[i] * 255.0f));
		// Lht
		else if(i == 2)
		{
			// handle internal light clamping, see `UnclampLighting`
			float Lht = DarkestLight + Color[i] * (1.0f - DarkestLight);
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Lht * 100.0f, round_to_int(Color[i] * 255.0f));
		}
		// Sat and Alpha
		else
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Color[i] * 100.0f, round_to_int(Color[i] * 255.0f));
		Ui()->DoLabel(&Label, aBuf, Metrics.m_BodySize, TEXTALIGN_ML);

		ColorRGBA HandleColor;
		Graphics()->TextureClear();
		Graphics()->TrianglesBegin();
		if(i == 0)
		{
			RenderHueRect(&Rail);
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f));
		}
		else if(i == 1)
		{
			RenderSaturationRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f));
		}
		else if(i == 2)
		{
			RenderLightingRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight));
		}
		else if(i == 3)
		{
			RenderAlphaRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight)));
			HandleColor = color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight));
		}
		Graphics()->TrianglesEnd();

		Color[i] = Ui()->DoScrollbarH(&((char *)pColor)[i], &Button, Color[i], &HandleColor);
	}

	if(OriginalColor != Color)
	{
		*pColor = Color.Pack(Alpha);
	}
	return PrevPackedColor != *pColor;
}

void CMenus::RenderSettingsAppearance(CUIRect MainView)
{
	const SSettingsContentMetrics AppearanceMetrics = ResolveSettingsContentMetrics(MainView.w);
	const float AppearanceUiScale = AppearanceMetrics.m_UiScale;
	const float AppearanceBodySize = AppearanceMetrics.m_BodySize;

	CUIRect TabBar, LeftView, RightView, Button;

	const SSettingsSubTabLayoutFrame AppearanceSubTabs = ResolveSettingsSubTabLayout(MainView, AppearanceUiScale);
	TabBar = AppearanceSubTabs.m_TabBarRect;
	MainView = AppearanceSubTabs.m_ContentRect;
	const float TabWidth = TabBar.w / (float)NUMBER_OF_APPEARANCE_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_APPEARANCE_TABS] = {};
	static const char *s_apAppearanceTabNames[NUMBER_OF_APPEARANCE_TABS] = {};
	static char s_aAppearanceLanguageFile[IO_MAX_PATH_LENGTH] = {};
	static bool s_AppearanceTabNamesInitialized = false;
	if(!s_AppearanceTabNamesInitialized || str_comp(s_aAppearanceLanguageFile, g_Config.m_ClLanguagefile) != 0)
	{
		s_AppearanceTabNamesInitialized = true;
		str_copy(s_aAppearanceLanguageFile, g_Config.m_ClLanguagefile, sizeof(s_aAppearanceLanguageFile));
		s_apAppearanceTabNames[APPEARANCE_TAB_HUD] = Localize("HUD");
		s_apAppearanceTabNames[APPEARANCE_TAB_CHAT] = Localize("Chat");
		s_apAppearanceTabNames[APPEARANCE_TAB_NAME_PLATE] = Localize("Name Plate");
		s_apAppearanceTabNames[APPEARANCE_TAB_HOOK_COLLISION] = Localize("Hook Collisions");
		s_apAppearanceTabNames[APPEARANCE_TAB_INFO_MESSAGES] = Localize("Info Messages");
		s_apAppearanceTabNames[APPEARANCE_TAB_LASER] = Localize("Laser");
	}

	// 胶囊 Tabbar：槽位先算完，再画容器与滑块，最后画页签文字 —— 滑块压在文字之下。
	CUIRect aAppearanceTabSlots[NUMBER_OF_APPEARANCE_TABS];
	CUIRect AppearanceTabsRemainder = TabBar;
	for(int Tab = APPEARANCE_TAB_HUD; Tab < NUMBER_OF_APPEARANCE_TABS; ++Tab)
		AppearanceTabsRemainder.VSplitLeft(TabWidth, &aAppearanceTabSlots[Tab], &AppearanceTabsRemainder);
	const int ActiveAppearanceTab = std::clamp(m_AppearanceSettingsTab, (int)APPEARANCE_TAB_HUD, (int)NUMBER_OF_APPEARANCE_TABS - 1);
	const IUiContext AppearanceTabBarCtx = TabBarUiContext();
	ui_widget::CapsuleTabBarChrome(AppearanceTabBarCtx, MakeUiScopeHash("settings_appearance_tabs_capsule"), ui_widget::CapsuleTabBarRowRect(aAppearanceTabSlots, NUMBER_OF_APPEARANCE_TABS), &aAppearanceTabSlots[ActiveAppearanceTab], SettingsCapsuleTabBarStyle());

	for(int Tab = APPEARANCE_TAB_HUD; Tab < NUMBER_OF_APPEARANCE_TABS; ++Tab)
	{
		if(DoButton_MenuTab(&s_aPageTabs[Tab], s_apAppearanceTabNames[Tab], m_AppearanceSettingsTab == Tab, &aAppearanceTabSlots[Tab], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true))
		{
			m_AppearanceSettingsTab = Tab;
		}
	}

	const float LineSize = AppearanceMetrics.m_LineHeight;
	const float ColorPickerRowHeight = AppearanceMetrics.m_ButtonHeight + AppearanceMetrics.m_LineSpacing;
	const float HeadlineFontSize = AppearanceMetrics.m_HeadlineSize;
	const float HeadlineHeight = AppearanceMetrics.m_LineHeight + AppearanceMetrics.m_LineSpacing * 2.0f;
	const float MarginSmall = AppearanceMetrics.m_LineSpacing;
	const float MarginBetweenViews = AppearanceMetrics.m_SectionGap * 2.0f;

	CUIRect ContentView = MainView;
	auto DoAppearanceHeading = [this](CUIRect &View, const char *pTextId, const char *pText, float FontSize, float LineHeight) {
		CUIRect Heading;
		View.HSplitTop(LineHeight, &Heading, &View);
		CUIElement &HeadingElement = SettingsTextElement(SETTINGS_APPEARANCE, m_AppearanceSettingsTab, pTextId);
		DoSettingsLabelStreamed(HeadingElement, &Heading, pText, FontSize, TEXTALIGN_ML);
	};
	const SSettingsPageLayoutFrame AppearancePage = SettingsPageLayout(ContentView, AppearanceUiScale);
	const IUiContext AppearanceCardCtx = SettingsUiContext("settings_appearance", AppearanceUiScale);
	const SSettingsCardDeckVisualOptions AppearanceVisualOptions = SettingsCardDeckVisualOptions();
	static std::array<CScrollRegion, NUMBER_OF_APPEARANCE_TABS> s_AppearanceSettingsCardScrollRegions;
	const auto DoAppearanceNumericField = [this, AppearanceCardCtx, AppearanceBodySize](int Tab, const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel, int Min, int Max, const IScrollbarScale *pScale = &CUi::ms_LinearScrollbarScale, unsigned Flags = 0u, const char *pSuffix = "", const char *pMaxText = nullptr) {
		ui_widget::SNumericFieldOptions Options;
		Options.m_pLabel = pLabel;
		Options.m_pSuffix = pSuffix;
		Options.m_pScale = pScale;
		Options.m_Flags = Flags;
		Options.m_pMaxText = pMaxText;
		Options.m_FontSize = AppearanceBodySize;
		Options.m_LabelAlign = TEXTALIGN_ML;
		Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;
		if(PrepareSettingsNumericFieldLabel(SETTINGS_APPEARANCE, Tab, -1, pTextId, Rect, pLabel, Flags, Options))
			return false;
		return ui_widget::NumericField(AppearanceCardCtx, GetSettingsNumericFieldState(pId), pId, pOption, Min, Max, Rect, Options);
	};
	const char *const aAppearanceIds[] = {
		"deck:appearance-hud-main", "deck:appearance-hud-ddrace", "deck:appearance-chat-settings", "deck:appearance-chat-messages", "deck:appearance-chat-preview", "deck:appearance-name-plate-settings", "deck:appearance-name-plate-preview", "deck:appearance-hook-collision-main", "deck:appearance-hook-collision-preview", "deck:appearance-info-messages", "deck:appearance-laser-enhanced", "deck:appearance-laser-colors", "deck:appearance-laser-preview"};
	std::array<const qm_card_registry::SCardDefault *, std::size(aAppearanceIds)> aAppearanceDefaults{};
	for(size_t i = 0; i < std::size(aAppearanceIds); ++i)
	{
		aAppearanceDefaults[i] = qm_card_registry::FindByStableId(aAppearanceIds[i]);
		dbg_assert(aAppearanceDefaults[i] != nullptr, "appearance settings card must be registered");
		if(aAppearanceDefaults[i] == nullptr)
			return;
	}
	const auto CardSpec = [aAppearanceDefaults](size_t Index) { return SSettingsCardSpec{aAppearanceDefaults[Index]->m_pStableId, Localize(aAppearanceDefaults[Index]->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*aAppearanceDefaults[Index])}; };
	const bool RenderOnly = Ui()->RenderOnly();
	const char *pAppearanceDeckTab = m_AppearanceSettingsTab == APPEARANCE_TAB_HUD            ? "appearance-hud" :
					 m_AppearanceSettingsTab == APPEARANCE_TAB_CHAT           ? "appearance-chat" :
					 m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE     ? "appearance-name-plate" :
					 m_AppearanceSettingsTab == APPEARANCE_TAB_HOOK_COLLISION ? "appearance-hook-collision" :
					 m_AppearanceSettingsTab == APPEARANCE_TAB_INFO_MESSAGES  ? "appearance-info-messages" :
												    "appearance-laser";
	const auto ResolveNamePlatePreviewMeasureRevision = [this]() {
		const int aInputs[] = {
			g_Config.m_QmNameplateShowScope,
			g_Config.m_ClNamePlatesClan,
			g_Config.m_ClNamePlatesFriendMark,
			g_Config.m_ClNamePlatesIds,
			g_Config.m_ClNamePlatesIdsSeparateLine,
			g_Config.m_ClNamePlatesStrong,
			g_Config.m_Debug,
			g_Config.m_ClNamePlatesSize,
			g_Config.m_ClNamePlatesClanSize,
			g_Config.m_ClNamePlatesIdsSize,
			g_Config.m_ClNamePlatesCoordsSize,
			g_Config.m_ClDirectionSize,
			g_Config.m_ClNamePlatesStrongSize,
			g_Config.m_QmNameplateCoords,
			g_Config.m_QmNameplateCoordsOwn,
			g_Config.m_QmNameplateCoordX,
			g_Config.m_QmNameplateCoordY,
			g_Config.m_ClShowDirection,
			g_Config.m_QmNameplateHookStrongWeakScope,
			g_Config.m_QmNameplateFreeMove,
			g_Config.m_QmNameplateFreeMoveX,
			g_Config.m_QmNameplateFreeMoveY,
			g_Config.m_ClDummy,
			g_Config.m_ClNamePlatesOffset,
			g_Config.m_QmNameplateNameOffsetX,
			g_Config.m_QmNameplateNameOffsetY,
			g_Config.m_QmNameplateClanOffsetX,
			g_Config.m_QmNameplateClanOffsetY,
			g_Config.m_QmNameplateHookOffsetX,
			g_Config.m_QmNameplateHookOffsetY,
			g_Config.m_QmNameplateCoordsOffsetX,
			g_Config.m_QmNameplateCoordsOffsetY,
			g_Config.m_QmNameplateKeysOffsetX,
			g_Config.m_QmNameplateKeysOffsetY,
			g_Config.m_ClNamePlatesTeamcolors,
			g_Config.m_QmNameplateTextEffects,
			g_Config.m_QmNameplateTextBorderRange,
			g_Config.m_QmNameplateTextGlowRange,
		};
		uint64_t Revision = 1469598103934665603ull;
		for(const int Input : aInputs)
			Revision = (Revision ^ static_cast<uint64_t>(static_cast<uint32_t>(Input))) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(g_Config.m_TcCustomFont)) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(g_Config.m_TcCustomFontCjk)) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(g_Config.m_TcCustomFontIcons)) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(Client()->PlayerName())) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(Client()->DummyName())) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(g_Config.m_PlayerClan)) * 1099511628211ull;
		Revision = (Revision ^ str_quickhash(g_Config.m_ClDummyClan)) * 1099511628211ull;
		return Revision;
	};
	const uint64_t NamePlatePreviewMeasureRevision = m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE ? ResolveNamePlatePreviewMeasureRevision() : 0;
	const auto BuildDefinitions = [=, this](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(std::size(aAppearanceIds));
		const auto AddCard = [&vCards, &CardSpec](size_t Index, float ContentHeight, FSettingsCardRender Render) {
			const SSettingsCardSpec Spec = CardSpec(Index);
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			Definition.m_Measure = [ContentHeight](float) { return maximum(0.0f, ContentHeight); };
			Definition.m_Render = std::move(Render);
			Definition.m_MeasureRevision = static_cast<uint64_t>(maximum(0.0f, ContentHeight) * 1000.0f);
			vCards.push_back(std::move(Definition));
		};
		const auto AddMeasuredCard = [&vCards, &CardSpec](size_t Index, FSettingsCardMeasure Measure, FSettingsCardRender Render, uint64_t MeasureRevision) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = CardSpec(Index);
			Definition.m_Measure = std::move(Measure);
			Definition.m_Render = std::move(Render);
			Definition.m_MeasureRevision = MeasureRevision;
			vCards.push_back(std::move(Definition));
		};

		if(m_AppearanceSettingsTab == APPEARANCE_TAB_HUD)
		{
			CPerfTimer HudShellTimer;
			LogPerfStage(Client(), "appearance_hud_text_cache", HudShellTimer.ElapsedMs(), false, "page=appearance tab=hud section=text_cache");
			LogPerfStage(Client(), "appearance_hud_tab_shell", HudShellTimer.ElapsedMs(), false, "page=appearance tab=hud");
			const float HudLeftMinCardHeight = ResolveSettingsRowsHeight(6, LineSize, MarginSmall) + MarginSmall + MarginBetweenViews + HeadlineHeight + MarginSmall + ColorPickerRowHeight * 4.0f;
			const auto ResolveHudRightMinCardHeight = [LineSize, MarginSmall]() {
				const int HudRightCheckboxRowCount = 8 + (g_Config.m_ClShowhudDDRace ? 2 : 0);
				return ResolveSettingsRowsHeight(HudRightCheckboxRowCount, LineSize, MarginSmall) + MarginSmall * 3.0f + (g_Config.m_ClShowFreezeBars ? LineSize : 0.0f);
			};
			AddCard(0, HudLeftMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				CPerfTimer HudCoreTimer;
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhud, "appearance-show-ingame-hud", Localize("Show ingame HUD"), &g_Config.m_ClShowhud, &LeftView, LineSize, MarginSmall, AppearanceBodySize);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhudHealthAmmo, "appearance-show-health-shields-ammo", Localize("Show health, shields and ammo"), &g_Config.m_ClShowhudHealthAmmo, &LeftView, LineSize, MarginSmall, AppearanceBodySize);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhudScore, "appearance-show-score", Localize("Show score"), &g_Config.m_ClShowhudScore, &LeftView, LineSize, MarginSmall, AppearanceBodySize);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowLocalTimeAlways, "appearance-show-local-time-always", Localize("Show local time always"), &g_Config.m_ClShowLocalTimeAlways, &LeftView, LineSize, MarginSmall, AppearanceBodySize);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClSpecCursor, "appearance-show-spectator-cursor", Localize("Show spectator cursor"), &g_Config.m_ClSpecCursor, &LeftView, LineSize, MarginSmall, AppearanceBodySize);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowVotesAfterVoting, "appearance-show-votes-after-voting", Localize("Show votes window after voting"), &g_Config.m_ClShowVotesAfterVoting, &LeftView, LineSize, MarginSmall, AppearanceBodySize);
				LeftView.HSplitTop(MarginBetweenViews, nullptr, &LeftView);
				CUIRect ScoreboardTitle;
				LeftView.HSplitTop(HeadlineHeight, &ScoreboardTitle, &LeftView);
				CUIElement &ScoreboardTitleText = SettingsTextElement(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, "appearance-scoreboard-title");
				DoSettingsLabelStreamed(ScoreboardTitleText, &ScoreboardTitle, Localize("Scoreboard"), HeadlineFontSize, TEXTALIGN_ML);
				LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
				ColorRGBA GreenDefault(0.78f, 1.0f, 0.8f, 1.0f);
				static CButtonContainer s_AuthedColor, s_SameClanColor, s_FriendsListFriendColor, s_FriendsListClanColor;
				DoLine_ColorPicker(&s_AuthedColor, AppearanceMetrics, &LeftView, Localize("Authed name color in scoreboard"), &g_Config.m_ClAuthedPlayerColor, GreenDefault, false);
				DoLine_ColorPicker(&s_SameClanColor, AppearanceMetrics, &LeftView, Localize("Same clan color in scoreboard"), &g_Config.m_ClSameClanColor, GreenDefault, false);
				DoLine_ColorPicker(&s_FriendsListFriendColor, AppearanceMetrics, &LeftView, Localize("Friend color in friends list"), &g_Config.m_ClFriendsListFriendColor, ColorRGBA(0.949f, 0.806f, 0.368f), false);
				DoLine_ColorPicker(&s_FriendsListClanColor, AppearanceMetrics, &LeftView, Localize("Clan color in friends list"), &g_Config.m_ClFriendsListClanColor, ColorRGBA(0.336f, 0.231f, 0.867f), false);
				LogPerfStage(Client(), "appearance_hud_core_section", HudCoreTimer.ElapsedMs(), false, "page=appearance tab=hud section=core");
			});
			AddCard(1, ResolveHudRightMinCardHeight(), [=, this](CUIRect ContentRect) mutable {
				CUIRect RightView = ContentRect;
				CPerfTimer HudDdraceTimer;
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowIds, "appearance-show-client-ids", Localize("Show client IDs (scoreboard, chat, spectating)"), &g_Config.m_ClShowIds, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhudDDRace, "appearance-show-ddrace-hud", Localize("Show DDRace HUD"), &g_Config.m_ClShowhudDDRace, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				if(g_Config.m_ClShowhudDDRace)
				{
					DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClHudRainbowColors, "appearance-hud-rainbow-colors", Localize("HUD rainbow colors"), &g_Config.m_ClHudRainbowColors, &RightView, LineSize, MarginSmall, AppearanceBodySize);
					DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhudJumpsIndicator, "appearance-show-jumps-indicator", Localize("Show jumps indicator"), &g_Config.m_ClShowhudJumpsIndicator, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				}
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhudSpectatorCount, "appearance-show-spectator-count", Localize("Show number of spectators"), &g_Config.m_ClShowhudSpectatorCount, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhudDummyActions, "appearance-show-dummy-actions", Localize("Show dummy actions"), &g_Config.m_ClShowhudDummyActions, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				// 卡键/锤子/分身控制/分身同步四个状态开关与自定义 bind 状态列表已迁移到 QmClient → HUD → DDRace HUD Pro 卡片
				RightView.HSplitTop(MarginSmall, nullptr, &RightView);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhudPlayerPosition, "appearance-show-player-position", Localize("Show player position"), &g_Config.m_ClShowhudPlayerPosition, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhudPlayerSpeed, "appearance-show-player-speed", Localize("Show player speed"), &g_Config.m_ClShowhudPlayerSpeed, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowhudPlayerAngle, "appearance-show-player-target-angle", Localize("Show player target angle"), &g_Config.m_ClShowhudPlayerAngle, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				LogPerfStage(Client(), "appearance_hud_ddrace_section", HudDdraceTimer.ElapsedMs(), false, "page=appearance tab=hud section=ddrace");
				CPerfTimer HudFreezeBarsTimer;
				RightView.HSplitTop(MarginSmall, nullptr, &RightView);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD, &g_Config.m_ClShowFreezeBars, "appearance-show-freeze-bars", Localize("Show freeze bars"), &g_Config.m_ClShowFreezeBars, &RightView, LineSize, MarginSmall, AppearanceBodySize);
				if(g_Config.m_ClShowFreezeBars)
				{
					RightView.HSplitTop(LineSize, &Button, &RightView);
					DoAppearanceNumericField(APPEARANCE_TAB_HUD, "appearance-freeze-bars-alpha-inside-freeze", &g_Config.m_ClFreezeBarsAlphaInsideFreeze, &g_Config.m_ClFreezeBarsAlphaInsideFreeze, Button, Localize("Opacity of freeze bars inside freeze"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
				LogPerfStage(Client(), "appearance_hud_freeze_bars_section", HudFreezeBarsTimer.ElapsedMs(), false, "page=appearance tab=hud section=freeze_bars");
			});
			vCards.back().m_Measure = [ResolveHudRightMinCardHeight](float) { return ResolveHudRightMinCardHeight(); };
			vCards.back().m_MeasureRevision = static_cast<uint64_t>(g_Config.m_ClShowhudDDRace != 0) | (static_cast<uint64_t>(g_Config.m_ClShowFreezeBars != 0) << 1);
			vCards.back().m_PreLayoutInput = [this, LineSize, MarginSmall](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CUIRect RightView = Content;
				CUIRect Row;
				const auto NextRow = [&]() {
					RightView.HSplitTop(LineSize, &Row, &RightView);
					RightView.HSplitTop(MarginSmall, nullptr, &RightView);
					return Row;
				};
				const auto ProcessToggle = [this](CUIRect ToggleRow, int *pValue) {
					if(!Ui()->DoButtonLogic(pValue, 0, &ToggleRow, BUTTONFLAG_LEFT))
						return false;
					*pValue ^= 1;
					return true;
				};
				NextRow();
				bool Changed = ProcessToggle(NextRow(), &g_Config.m_ClShowhudDDRace);
				if(g_Config.m_ClShowhudDDRace)
				{
					NextRow();
					NextRow();
				}
				for(int RowIndex = 0; RowIndex < 2; ++RowIndex)
					NextRow();
				RightView.HSplitTop(MarginSmall, nullptr, &RightView);
				for(int RowIndex = 0; RowIndex < 3; ++RowIndex)
					NextRow();
				RightView.HSplitTop(MarginSmall, nullptr, &RightView);
				Changed = ProcessToggle(NextRow(), &g_Config.m_ClShowFreezeBars) || Changed;
				return Changed;
			};
		}
		else if(m_AppearanceSettingsTab == APPEARANCE_TAB_CHAT)
		{
			CChat *pChat = &GameClient()->m_Chat;
			static int s_AppearanceAlwaysShowChat = 0;
			// ***** Chat ***** //
			const auto ResolveChatSettingsMinCardHeight = [LineSize, MarginSmall, ColorPickerRowHeight]() {
				const int ChatSettingsRowCount = 9 + (g_Config.m_ClShowChat != 0 ? 1 : 0) + (g_Config.m_QmChatLogAutoSave != 0 ? 1 : 0);
				return ResolveSettingsRowsHeight(ChatSettingsRowCount, LineSize, MarginSmall) + MarginSmall + ColorPickerRowHeight;
			};
			SSettingsCardDefinition ChatSettingsDefinition;
			ChatSettingsDefinition.m_Spec = CardSpec(2);
			ChatSettingsDefinition.m_Measure = [ResolveChatSettingsMinCardHeight](float) { return ResolveChatSettingsMinCardHeight(); };
			ChatSettingsDefinition.m_Render = [=, this](CUIRect ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				const auto NextChatRow = [&](CUIRect &Row) {
					LeftView.HSplitTop(LineSize, &Row, &LeftView);
					LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
				};
				const auto DoChatCheckBox = [&](const void *pId, const char *pTextId, const char *pText, int *pValue) {
					DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, pId, pTextId, pText, pValue, &LeftView, LineSize, 0.0f, AppearanceBodySize);
					LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
				};
				// General chat settings
				NextChatRow(Button);
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_ClShowChat, "appearance-show-chat", Localize("Show chat"), g_Config.m_ClShowChat, &Button))
				{
					g_Config.m_ClShowChat = g_Config.m_ClShowChat ? 0 : 1;
				}
				if(g_Config.m_ClShowChat)
				{
					NextChatRow(Button);
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &s_AppearanceAlwaysShowChat, "appearance-always-show-chat", Localize("Always show chat"), g_Config.m_ClShowChat == 2, &Button))
						g_Config.m_ClShowChat = g_Config.m_ClShowChat != 2 ? 2 : 1;
				}

				DoChatCheckBox(&g_Config.m_ClChatTeamColors, "appearance-chat-team-colors", Localize("Show names in chat in team colors"), &g_Config.m_ClChatTeamColors);
				DoChatCheckBox(&g_Config.m_ClShowChatFriends, "appearance-chat-friends-only", Localize("Show only chat messages from friends"), &g_Config.m_ClShowChatFriends);
				DoChatCheckBox(&g_Config.m_ClShowChatTeamMembersOnly, "appearance-chat-team-members-only", Localize("Show only chat messages from team members"), &g_Config.m_ClShowChatTeamMembersOnly);
				DoChatCheckBox(&g_Config.m_QmChatSaveDraft, "appearance-chat-save-draft", Localize("Save unsent chat draft"), &g_Config.m_QmChatSaveDraft);
				DoChatCheckBox(&g_Config.m_QmChatLogAutoSave, "appearance-chat-log-auto-save", Localize("Auto save chat log"), &g_Config.m_QmChatLogAutoSave);
				if(g_Config.m_QmChatLogAutoSave)
				{
					NextChatRow(Button);
					DoAppearanceNumericField(APPEARANCE_TAB_CHAT, "appearance-chat-log-keep-days", &g_Config.m_QmChatLogKeepDays, &g_Config.m_QmChatLogKeepDays, Button, Localize("Chat log retention days"), 0, 3650, &CUi::ms_LinearScrollbarScale, 0, "", Localize("Days"));
				}

				if(DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_ClChatOld, "appearance-use-old-chat-style", Localize("Use old chat style"), &g_Config.m_ClChatOld, &LeftView, LineSize, 0.0f, AppearanceBodySize))
					GameClient()->m_Chat.RebuildChat();
				LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

				// Profanity censor checkbox intentionally stays disabled here.

				NextChatRow(Button);
				if(DoAppearanceNumericField(APPEARANCE_TAB_CHAT, "appearance-chat-font-size", &g_Config.m_ClChatFontSize, &g_Config.m_ClChatFontSize, Button, Localize("Chat font size"), 10, 100))
				{
					pChat->EnsureCoherentWidth();
					pChat->RebuildChat();
				}

				NextChatRow(Button);
				if(DoAppearanceNumericField(APPEARANCE_TAB_CHAT, "appearance-chat-width", &g_Config.m_ClChatWidth, &g_Config.m_ClChatWidth, Button, Localize("Chat width"), 120, 400))
				{
					pChat->EnsureCoherentFontSize();
					pChat->RebuildChat();
				}

				static CButtonContainer s_BackgroundColor;
				DoLine_ColorPicker(&s_BackgroundColor, AppearanceMetrics, &LeftView, Localize("Chat background color"), &g_Config.m_ClChatBackgroundColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::ClChatBackgroundColor, true)), false, nullptr, true);
			};
			ChatSettingsDefinition.m_MeasureRevision =
				(static_cast<uint64_t>(g_Config.m_ClShowChat != 0) << 0) |
				(static_cast<uint64_t>(g_Config.m_QmChatLogAutoSave != 0) << 1);
			ChatSettingsDefinition.m_PreLayoutInput = [this, LineSize, MarginSmall](CUIRect ContentRect) {
				if(m_MenuTextPlanCollecting)
					return false;
				auto NextRow = [LineSize, MarginSmall](CUIRect &Content, CUIRect &Row) {
					Content.HSplitTop(LineSize, &Row, &Content);
					Content.HSplitTop(MarginSmall, nullptr, &Content);
				};
				CUIRect Row;
				NextRow(ContentRect, Row);
				if(Ui()->DoButtonLogic(&g_Config.m_ClShowChat, 0, &Row, BUTTONFLAG_LEFT))
				{
					g_Config.m_ClShowChat = g_Config.m_ClShowChat ? 0 : 1;
					return true;
				}
				if(g_Config.m_ClShowChat)
					NextRow(ContentRect, Row);
				for(int i = 0; i < 4; ++i)
					NextRow(ContentRect, Row);
				NextRow(ContentRect, Row);
				if(!Ui()->DoButtonLogic(&g_Config.m_QmChatLogAutoSave, 0, &Row, BUTTONFLAG_LEFT))
					return false;
				g_Config.m_QmChatLogAutoSave ^= 1;
				return true;
			};
			vCards.push_back(std::move(ChatSettingsDefinition));
			const float ChatMessagesMinCardHeight = ResolveAppearanceChatMessagesHeight(AppearanceMetrics);
			AddCard(3, ChatMessagesMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				char aBuf[128];
				CUIRect RightView = ContentRect;
				// ***** Messages ***** //
				// Message Colors and extra settings
				static CButtonContainer s_SystemMessageReset, s_SystemMessageAdd, s_SystemMessageRemove;
				static unsigned s_aSystemMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-system-message", Localize("System message"), &g_Config.m_ClMessageSystemColor, g_Config.m_ClMessageSystemGradient, sizeof(g_Config.m_ClMessageSystemGradient), ColorRGBA(1.0f, 1.0f, 0.5f), &s_SystemMessageReset, &s_SystemMessageAdd, &s_SystemMessageRemove, s_aSystemMessageColorValues, true, &g_Config.m_ClShowChatSystem, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);
				if(DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT, &g_Config.m_QmChatHideSystemPrefix, "appearance-chat-hide-system-prefix", Localize("Hide system message prefix"), &g_Config.m_QmChatHideSystemPrefix, &RightView, LineSize, MarginSmall, AppearanceBodySize))
				{
					pChat->RebuildChat();
					ConfigManager()->Save();
				}

				static CButtonContainer s_HighlightedMessageReset, s_HighlightedMessageAdd, s_HighlightedMessageRemove;
				static unsigned s_aHighlightedMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-highlighted-message", Localize("Highlighted message"), &g_Config.m_ClMessageHighlightColor, g_Config.m_ClMessageHighlightGradient, sizeof(g_Config.m_ClMessageHighlightGradient), ColorRGBA(1.0f, 0.5f, 0.5f), &s_HighlightedMessageReset, &s_HighlightedMessageAdd, &s_HighlightedMessageRemove, s_aHighlightedMessageColorValues, true, nullptr, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_TeamMessageReset, s_TeamMessageAdd, s_TeamMessageRemove;
				static unsigned s_aTeamMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-team-message", Localize("Team message"), &g_Config.m_ClMessageTeamColor, g_Config.m_ClMessageTeamGradient, sizeof(g_Config.m_ClMessageTeamGradient), ColorRGBA(0.65f, 1.0f, 0.65f), &s_TeamMessageReset, &s_TeamMessageAdd, &s_TeamMessageRemove, s_aTeamMessageColorValues, true, nullptr, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_FriendMessageReset, s_FriendMessageAdd, s_FriendMessageRemove;
				static unsigned s_aFriendMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-friend-message", Localize("Friend message"), &g_Config.m_ClMessageFriendColor, g_Config.m_ClMessageFriendGradient, sizeof(g_Config.m_ClMessageFriendGradient), ColorRGBA(1.0f, 0.137f, 0.137f), &s_FriendMessageReset, &s_FriendMessageAdd, &s_FriendMessageRemove, s_aFriendMessageColorValues, true, &g_Config.m_ClMessageFriend, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_NormalMessageReset, s_NormalMessageAdd, s_NormalMessageRemove;
				static unsigned s_aNormalMessageColorValues[CMessageGradient::MAX_COLORS];
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-normal-message", Localize("Normal message"), &g_Config.m_ClMessageColor, g_Config.m_ClMessageGradient, sizeof(g_Config.m_ClMessageGradient), ColorRGBA(1.0f, 1.0f, 1.0f), &s_NormalMessageReset, &s_NormalMessageAdd, &s_NormalMessageRemove, s_aNormalMessageColorValues, true, nullptr, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				str_format(aBuf, sizeof(aBuf), "%s (echo)", Localize("Client message"));
				static CButtonContainer s_ClientMessageReset, s_ClientMessageAdd, s_ClientMessageRemove;
				static unsigned s_aClientMessageColorValues[CMessageGradient::MAX_COLORS];
				// TClient
				DoMessageGradientLine(*pChat, &RightView, APPEARANCE_TAB_CHAT, "appearance-chat-client-message", aBuf, &g_Config.m_ClMessageClientColor, g_Config.m_ClMessageClientGradient, sizeof(g_Config.m_ClMessageClientGradient), ColorRGBA(0.5f, 0.78f, 1.0f), &s_ClientMessageReset, &s_ClientMessageAdd, &s_ClientMessageRemove, s_aClientMessageColorValues, true, &g_Config.m_TcShowChatClient, LineSize, MarginSmall, AppearanceBodySize, AppearanceMetrics.m_ButtonHeight);

				static CButtonContainer s_FriendMessageHeartReset;
				const unsigned OldFriendMessageHeartColor = g_Config.m_ClMessageFriendHeartColor;
				DoLine_ColorPicker(&s_FriendMessageHeartReset, AppearanceMetrics, &RightView, Localize("Friend heart"), &g_Config.m_ClMessageFriendHeartColor, ColorRGBA(1.0f, 0.0f, 0.0f), true);
				if(g_Config.m_ClMessageFriendHeartColor != OldFriendMessageHeartColor)
				{
					pChat->RebuildChat();
					ConfigManager()->Save();
				}
			});
			const auto MeasureChatPreview = [this, pChat, MarginSmall](float ContentWidth) {
				const float RealFontSize = pChat->FontSize() * 2.0f;
				const float RealMsgPaddingX = (!g_Config.m_ClChatOld ? pChat->MessagePaddingX() : 0.0f) * 2.0f;
				const float RealMsgPaddingY = (!g_Config.m_ClChatOld ? pChat->MessagePaddingY() : 0.0f) * 2.0f;
				const float RealMsgPaddingTee = (!g_Config.m_ClChatOld ? pChat->MessageTeeSize() + CChat::MESSAGE_TEE_PADDING_RIGHT : 0.0f) * 2.0f;
				const float ConfiguredLineWidth = g_Config.m_ClChatWidth * 2.0f - RealMsgPaddingX * 1.5f - RealMsgPaddingTee;
				const float CardLineWidth = maximum(RealFontSize, ContentWidth - 2.0f * MarginSmall - RealMsgPaddingX * 1.5f - RealMsgPaddingTee);
				const float LineWidth = maximum(RealFontSize, minimum(ConfiguredLineWidth, CardLineWidth));
				char aPlayerName[64];
				str_copy(aPlayerName, Client()->PlayerName());
				float Height = 2.0f * MarginSmall;
				const auto AddPreviewLine = [&](const char *pName, const char *pText, bool AppendNameSeparator = true) {
					char aLine[384];
					str_format(aLine, sizeof(aLine), "%s%s%s", pName, AppendNameSeparator && pName[0] != '\0' ? ": " : "", pText);
					Height += maximum(RealFontSize, TextRender()->TextBoundingBox(RealFontSize, aLine, -1, LineWidth).m_H) + RealMsgPaddingY;
				};
				if(g_Config.m_ClShowChatSystem)
				{
					char aSystemText[128];
					str_format(aSystemText, sizeof(aSystemText), "'%s' entered and joined the game", aPlayerName);
					AddPreviewLine(CChat::SystemMessageNamePrefix(g_Config.m_QmChatHideSystemPrefix != 0), aSystemText, false);
				}
				if(!g_Config.m_ClShowChatFriends)
				{
					if(!g_Config.m_ClShowChatTeamMembersOnly)
					{
						char aHighlightText[128];
						str_format(aHighlightText, sizeof(aHighlightText), "Hey, how are you %s?", aPlayerName);
						AddPreviewLine("Random Tee", aHighlightText);
					}
					AddPreviewLine("Your Teammate", "Let's speedrun this!");
				}
				if(!g_Config.m_ClShowChatTeamMembersOnly)
					AddPreviewLine("Friend", "Hello there");
				if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
					AddPreviewLine("Spammer", "Hey fools, I'm spamming here!");
				if(g_Config.m_TcShowChatClient)
					AddPreviewLine("", "Echo command executed");
				return maximum(Height, 2.0f * MarginSmall + RealFontSize + RealMsgPaddingY);
			};
			const uint64_t ChatPreviewMeasureRevision =
				(static_cast<uint64_t>(g_Config.m_ClChatOld != 0) << 0) |
				(static_cast<uint64_t>(g_Config.m_ClShowChatSystem != 0) << 1) |
				(static_cast<uint64_t>(g_Config.m_ClShowChatFriends != 0) << 2) |
				(static_cast<uint64_t>(g_Config.m_ClShowChatTeamMembersOnly != 0) << 3) |
				(static_cast<uint64_t>(g_Config.m_TcShowChatClient != 0) << 4) |
				(static_cast<uint64_t>(g_Config.m_QmChatHideSystemPrefix != 0) << 5) |
				(static_cast<uint64_t>(std::clamp(g_Config.m_ClChatFontSize, 0, 255)) << 8) |
				(static_cast<uint64_t>(std::clamp(g_Config.m_ClChatWidth, 0, 1023)) << 16);
			AddMeasuredCard(4, MeasureChatPreview, [=, this](CUIRect ContentRect) mutable {
			char aBuf[128];
			CUIRect PreviewView = ContentRect;
			// ***** Chat Preview ***** //
			PreviewView.Draw(ColorRGBA(1, 1, 1, 0.1f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
			PreviewView.Margin(MarginSmall, &PreviewView);

			ColorRGBA SystemColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
			ColorRGBA HighlightedColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
			ColorRGBA TeamColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
			ColorRGBA FriendColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
			ColorRGBA FriendHeartColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageFriendHeartColor));
			ColorRGBA NormalColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageColor));
			ColorRGBA ClientColor = color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageClientColor));
			ColorRGBA DefaultNameColor(0.8f, 0.8f, 0.8f, 1.0f);

			const float RealFontSize = pChat->FontSize() * 2;
			const float RealMsgPaddingX = (!g_Config.m_ClChatOld ? pChat->MessagePaddingX() : 0) * 2;
			const float RealMsgPaddingY = (!g_Config.m_ClChatOld ? pChat->MessagePaddingY() : 0) * 2;
			const float RealMsgPaddingTee = (!g_Config.m_ClChatOld ? pChat->MessageTeeSize() + CChat::MESSAGE_TEE_PADDING_RIGHT : 0) * 2;
			const float RealOffsetY = RealFontSize + RealMsgPaddingY;

			const float X = RealMsgPaddingX / 2.0f + PreviewView.x;
			float Y = PreviewView.y;
			const float ConfiguredLineWidth = g_Config.m_ClChatWidth * 2.0f - RealMsgPaddingX * 1.5f - RealMsgPaddingTee;
			const float CardLineWidth = maximum(RealFontSize, PreviewView.w - RealMsgPaddingX * 1.5f - RealMsgPaddingTee);
			float LineWidth = maximum(RealFontSize, minimum(ConfiguredLineWidth, CardLineWidth));

			str_copy(aBuf, Client()->PlayerName());

			const CAnimState *pIdleState = CAnimState::GetIdle();
			const float RealTeeSize = pChat->MessageTeeSize() * 2;
			const float RealTeeSizeHalved = pChat->MessageTeeSize();
			constexpr float TWSkinUnreliableOffset = -0.25f;
			const float OffsetTeeY = RealTeeSizeHalved;
			const float FullHeightMinusTee = RealOffsetY - RealTeeSize;

			struct SPreviewLine
			{
				int m_ClientId;
				bool m_Team;
				char m_aName[64];
				char m_aText[256];
				bool m_Friend;
				bool m_Player;
				bool m_Client;
				bool m_Highlighted;
				int m_TimesRepeated;

				CTeeRenderInfo m_RenderInfo;
			};

			static std::vector<SPreviewLine> s_vLines;

			enum ELineFlag
			{
				FLAG_TEAM = 1 << 0,
				FLAG_FRIEND = 1 << 1,
				FLAG_HIGHLIGHT = 1 << 2,
				FLAG_CLIENT = 1 << 3
			};
			enum
			{
				PREVIEW_SYS,
				PREVIEW_HIGHLIGHT,
				PREVIEW_TEAM,
				PREVIEW_FRIEND,
				PREVIEW_SPAMMER,
				PREVIEW_CLIENT
			};
			auto &&SetPreviewLine = [](int Index, int ClientId, const char *pName, const char *pText, int Flag, int Repeats) {
				SPreviewLine *pLine;
				if((int)s_vLines.size() <= Index)
				{
					s_vLines.emplace_back();
					pLine = &s_vLines.back();
				}
				else
				{
					pLine = &s_vLines[Index];
				}
				pLine->m_ClientId = ClientId;
				pLine->m_Team = Flag & FLAG_TEAM;
				pLine->m_Friend = Flag & FLAG_FRIEND;
				pLine->m_Player = ClientId >= 0;
				pLine->m_Highlighted = Flag & FLAG_HIGHLIGHT;
				pLine->m_Client = Flag & FLAG_CLIENT;
				pLine->m_TimesRepeated = Repeats;
				str_copy(pLine->m_aName, pName);
				str_copy(pLine->m_aText, pText);
			};
			auto &&SetLineSkin = [RealTeeSize](int Index, const CSkin *pSkin) {
				if(Index >= (int)s_vLines.size())
					return;
				s_vLines[Index].m_RenderInfo.m_Size = RealTeeSize;
				s_vLines[Index].m_RenderInfo.Apply(pSkin);
			};

			auto &&RenderPreview = [&](int LineIndex, int x, int y, bool Render = true) {
				if(LineIndex >= (int)s_vLines.size())
					return vec2(0, 0);
				CTextCursor LocalCursor;
				LocalCursor.SetPosition(vec2(x, y));
				LocalCursor.m_FontSize = RealFontSize;
				LocalCursor.m_Flags = Render ? TEXTFLAG_RENDER : 0;
				LocalCursor.m_LineWidth = LineWidth;
				const auto &Line = s_vLines[LineIndex];

				char aClientId[16] = "";
				if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
				{
					GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_FORCE);
				}

				char aCount[12];
				if(Line.m_ClientId < 0)
					str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
				else
					str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);

				if(Line.m_Player)
				{
					LocalCursor.m_X += RealMsgPaddingTee;

					if(Line.m_Friend && g_Config.m_ClMessageFriend)
					{
						if(Render)
							TextRender()->TextColor(FriendHeartColor);
						TextRender()->TextEx(&LocalCursor, "♥ ", -1);
					}
				}

				ColorRGBA NameColor;
				if(Line.m_Team)
					NameColor = CalculateNameColor(color_cast<ColorHSLA>(TeamColor));
				else if(Line.m_Player)
					NameColor = DefaultNameColor;
				else if(Line.m_Client)
					NameColor = ClientColor;
				else
					NameColor = SystemColor;

				if(Render)
					TextRender()->TextColor(NameColor);

				TextRender()->TextEx(&LocalCursor, aClientId);
				TextRender()->TextEx(&LocalCursor, Line.m_aName);

				if(Line.m_TimesRepeated > 0)
				{
					if(Render)
						TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
					TextRender()->TextEx(&LocalCursor, aCount, -1);
				}

				if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
				{
					if(Render)
						TextRender()->TextColor(NameColor);
					TextRender()->TextEx(&LocalCursor, ": ", -1);
				}

				CTextCursor AppendCursor = LocalCursor;
				AppendCursor.m_LongestLineWidth = 0.0f;
				if(!g_Config.m_ClChatOld)
				{
					AppendCursor.m_StartX = LocalCursor.m_X;
					AppendCursor.m_LineWidth -= LocalCursor.m_LongestLineWidth;
				}

				if(Render)
				{
					if(Line.m_Highlighted)
						TextRender()->TextColor(HighlightedColor);
					else if(Line.m_Friend && g_Config.m_ClMessageFriend)
						TextRender()->TextColor(FriendColor);
					else if(Line.m_Team)
						TextRender()->TextColor(TeamColor);
					else if(Line.m_Player)
						TextRender()->TextColor(NormalColor);
				}

				if(Line.m_Highlighted)
					CMessageGradient::AddTextSplits(AppendCursor, Line.m_aText, g_Config.m_ClMessageHighlightGradient, HighlightedColor);
				else if(Line.m_Friend && g_Config.m_ClMessageFriend)
					CMessageGradient::AddTextSplits(AppendCursor, Line.m_aText, g_Config.m_ClMessageFriendGradient, FriendColor);
				else if(Line.m_Team)
					CMessageGradient::AddTextSplits(AppendCursor, Line.m_aText, g_Config.m_ClMessageTeamGradient, TeamColor);
				else if(Line.m_Player)
					CMessageGradient::AddTextSplits(AppendCursor, Line.m_aText, g_Config.m_ClMessageGradient, NormalColor);
				else if(Line.m_Client)
					CMessageGradient::AddTextSplits(AppendCursor, Line.m_aText, g_Config.m_ClMessageClientGradient, ClientColor);
				else
					CMessageGradient::AddTextSplits(AppendCursor, Line.m_aText, g_Config.m_ClMessageSystemGradient, SystemColor);
				TextRender()->TextEx(&AppendCursor, Line.m_aText, -1);
				AppendCursor.m_vColorSplits.clear();
				if(Render)
					TextRender()->TextColor(TextRender()->DefaultTextColor());

				return vec2{LocalCursor.m_LongestLineWidth + AppendCursor.m_LongestLineWidth, AppendCursor.Height() + RealMsgPaddingY};
			};

			// Set preview lines
			{
				char aLineBuilder[128];

				str_format(aLineBuilder, sizeof(aLineBuilder), "'%s' entered and joined the game", aBuf);
				SetPreviewLine(PREVIEW_SYS, -1, CChat::SystemMessageNamePrefix(g_Config.m_QmChatHideSystemPrefix != 0), aLineBuilder, 0, 0);

				str_format(aLineBuilder, sizeof(aLineBuilder), "Hey, how are you %s?", aBuf);
				SetPreviewLine(PREVIEW_HIGHLIGHT, 7, "Random Tee", aLineBuilder, FLAG_HIGHLIGHT, 0);

				SetPreviewLine(PREVIEW_TEAM, 11, "Your Teammate", "Let's speedrun this!", FLAG_TEAM, 0);
				SetPreviewLine(PREVIEW_FRIEND, 8, "Friend", "Hello there", FLAG_FRIEND, 0);
				SetPreviewLine(PREVIEW_SPAMMER, 9, "Spammer", "Hey fools, I'm spamming here!", 0, 5);
				SetPreviewLine(PREVIEW_CLIENT, -1, "— ", "Echo command executed", FLAG_CLIENT, 0);
			}

			SetLineSkin(1, GameClient()->m_Skins.Find("pinky"));
			SetLineSkin(2, GameClient()->m_Skins.Find("default"));
			SetLineSkin(3, GameClient()->m_Skins.Find("cammostripes"));
			SetLineSkin(4, GameClient()->m_Skins.Find("beast"));

			// Backgrounds first
			if(!g_Config.m_ClChatOld)
			{
				const ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true));

				float TempY = Y;
				const float RealBackgroundRounding = pChat->MessageRounding() * 2.0f;

				auto &&RenderMessageBackground = [&](int LineIndex) {
					const auto Size = RenderPreview(LineIndex, 0, 0, false);
					const CUIRect MessageBackground{PreviewView.x, TempY - RealMsgPaddingY / 2.0f, PreviewView.w, Size.y};
					DrawRoundedSurface(Ui(), MessageBackground, BackgroundColor, ColorRGBA(), RealBackgroundRounding);
					return Size.y;
				};

				if(g_Config.m_ClShowChatSystem)
				{
					TempY += RenderMessageBackground(PREVIEW_SYS);
				}

				if(!g_Config.m_ClShowChatFriends)
				{
					if(!g_Config.m_ClShowChatTeamMembersOnly)
						TempY += RenderMessageBackground(PREVIEW_HIGHLIGHT);
					TempY += RenderMessageBackground(PREVIEW_TEAM);
				}

				if(!g_Config.m_ClShowChatTeamMembersOnly)
					TempY += RenderMessageBackground(PREVIEW_FRIEND);

				if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
				{
					TempY += RenderMessageBackground(PREVIEW_SPAMMER);
				}

				if(g_Config.m_TcShowChatClient)
				{
					TempY += RenderMessageBackground(PREVIEW_CLIENT);
				}

			}

			// System
			if(g_Config.m_ClShowChatSystem)
			{
				Y += RenderPreview(PREVIEW_SYS, X, Y).y;
			}

			if(!g_Config.m_ClShowChatFriends)
			{
				// Highlighted
				if(!g_Config.m_ClChatOld && !g_Config.m_ClShowChatTeamMembersOnly)
					RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_HIGHLIGHT].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
				if(!g_Config.m_ClShowChatTeamMembersOnly)
					Y += RenderPreview(PREVIEW_HIGHLIGHT, X, Y).y;

				// Team
				if(!g_Config.m_ClChatOld)
					RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_TEAM].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
				Y += RenderPreview(PREVIEW_TEAM, X, Y).y;
			}

			// Friend
			if(!g_Config.m_ClChatOld && !g_Config.m_ClShowChatTeamMembersOnly)
				RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_FRIEND].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
			if(!g_Config.m_ClShowChatTeamMembersOnly)
				Y += RenderPreview(PREVIEW_FRIEND, X, Y).y;

			// Normal
			if(!g_Config.m_ClShowChatFriends && !g_Config.m_ClShowChatTeamMembersOnly)
			{
				if(!g_Config.m_ClChatOld)
					RenderTools()->RenderTee(pIdleState, &s_vLines[PREVIEW_SPAMMER].m_RenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), vec2(X + RealTeeSizeHalved, Y + OffsetTeeY + FullHeightMinusTee / 2.0f + TWSkinUnreliableOffset));
				Y += RenderPreview(PREVIEW_SPAMMER, X, Y).y;
			}
			// Client
			if(g_Config.m_TcShowChatClient)
			{
				Y += RenderPreview(PREVIEW_CLIENT, X, Y).y;
			}

					TextRender()->TextColor(TextRender()->DefaultTextColor());
					PreviewView.y = maximum(PreviewView.y, Y + MarginSmall); }, ChatPreviewMeasureRevision);
		}
		else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)
		{
			qm_card_catalog::SQmCardBuildContext CardBuild;
			CardBuild.m_pMenus = this;
			CardBuild.m_ReadOnly = RenderOnly;
			CardBuild.m_Page = AppearancePage;
			CardBuild.m_Metrics = AppearanceMetrics;
			CardBuild.m_UiContext = AppearanceCardCtx;
			qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::NameplateCardStableIds(), vCards);
			const float NamePlatePreviewMinAreaHeight = std::clamp(190.0f * AppearanceUiScale, 160.0f, 210.0f);
			const float NamePlatePreviewControlsHeight = ResolveSettingsRowsHeight(2, LineSize, MarginSmall) + MarginSmall + AppearanceMetrics.m_ButtonHeight;
			const auto ResolveNamePlatePreviewCardHeight = [this, NamePlatePreviewMinAreaHeight, NamePlatePreviewControlsHeight, MarginSmall](float) {
				return maximum(NamePlatePreviewMinAreaHeight, GameClient()->m_NamePlates.MeasurePreviewAreaHeight()) + MarginSmall + NamePlatePreviewControlsHeight;
			};

			AddMeasuredCard(6, ResolveNamePlatePreviewCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect RightView = ContentRect;
				CUIRect PreviewArea, Controls;
				RightView.HSplitBottom(NamePlatePreviewControlsHeight, &PreviewArea, &Controls);
				PreviewArea.HSplitBottom(MarginSmall, &PreviewArea, nullptr);
				PreviewArea.Draw(ui_token::color::SURFACE_OVERLAY, IGraphics::CORNER_ALL, ui_token::radius::CARD);
				const auto NextPreviewControl = [&](CUIRect &Row) {
					Controls.HSplitTop(LineSize, &Row, &Controls);
					if(Controls.h > 0.0f)
						Controls.HSplitTop(MarginSmall, nullptr, &Controls);
				};

				NextPreviewControl(Button);
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, &m_DummyNamePlatePreview, g_Config.m_ClDummy ? "appearance-preview-player-nameplate" : "appearance-preview-dummy-nameplate", g_Config.m_ClDummy ? Localize("Preview player nameplate") : Localize("Preview dummy nameplate"), m_DummyNamePlatePreview, &Button))
					m_DummyNamePlatePreview = !m_DummyNamePlatePreview;

				NextPreviewControl(Button);
				const bool NameplateFreeMoveEnabled = g_Config.m_QmNameplateFreeMove != 0 || g_Config.m_QmNameplateFreeMoveX != 0 || g_Config.m_QmNameplateFreeMoveY != 0;
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, &g_Config.m_QmNameplateFreeMove, "appearance-nameplate-free-move", Localize("Free move"), NameplateFreeMoveEnabled, &Button))
				{
					const int NewValue = NameplateFreeMoveEnabled ? 0 : 1;
					g_Config.m_QmNameplateFreeMove = NewValue;
					g_Config.m_QmNameplateFreeMoveX = 0;
					g_Config.m_QmNameplateFreeMoveY = 0;
				}

				Controls.HSplitTop(AppearanceMetrics.m_ButtonHeight, &Button, &Controls);
				if(Controls.h > 0.0f)
					Controls.HSplitTop(MarginSmall, nullptr, &Controls);
				static CButtonContainer s_NameplateResetLayoutButton;
				if(DoSettingsButton_Menu(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, APPEARANCE_TAB_NAME_PLATE, &s_NameplateResetLayoutButton, "appearance-nameplate-reset-layout", Localize("Reset layout"), 0, &Button))
				{
					g_Config.m_QmNameplateKeysOffsetX = 0;
					g_Config.m_QmNameplateKeysOffsetY = 0;
					g_Config.m_QmNameplateCoordsOffsetX = 0;
					g_Config.m_QmNameplateCoordsOffsetY = 0;
					g_Config.m_QmNameplateHookOffsetX = 0;
					g_Config.m_QmNameplateHookOffsetY = 0;
					g_Config.m_QmNameplateClanOffsetX = 0;
					g_Config.m_QmNameplateClanOffsetY = 0;
					g_Config.m_QmNameplateNameOffsetX = 0;
					g_Config.m_QmNameplateNameOffsetY = 0;
				}
				int Dummy = g_Config.m_ClDummy != (m_DummyNamePlatePreview ? 1 : 0);
				GameClient()->m_NamePlates.RenderNamePlatePreview(PreviewArea, Dummy); }, NamePlatePreviewMeasureRevision);
		}
		else if(m_AppearanceSettingsTab == APPEARANCE_TAB_HOOK_COLLISION)
		{
			static int s_AppearanceAlwaysShowHookCollOwn = 0;
			static int s_AppearanceAlwaysShowHookCollOther = 0;
			const auto ResolveHookCollisionLeftMinCardHeight = [LineSize, MarginSmall, HeadlineHeight, ColorPickerRowHeight]() {
				const int RowCount = 5 + (g_Config.m_ClShowHookCollOwn != 0 ? 1 : 0) + (g_Config.m_ClShowHookCollOther != 0 ? 1 : 0);
				return ResolveSettingsRowsHeight(RowCount, LineSize, MarginSmall) + MarginSmall + HeadlineHeight + ColorPickerRowHeight * 4.0f;
			};
			const float HookCollisionRightMinCardHeight =
				(50.0f + 4.0f * MarginSmall) * 5.0f + LineSize;
			AddCard(7, ResolveHookCollisionLeftMinCardHeight(), [=, this](CUIRect ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				int RowsRemaining = 5 + (g_Config.m_ClShowHookCollOwn != 0 ? 1 : 0) + (g_Config.m_ClShowHookCollOther != 0 ? 1 : 0);
				const auto NextRow = [&]() {
					CUIRect Row;
					LeftView.HSplitTop(LineSize, &Row, &LeftView);
					if(--RowsRemaining > 0)
						LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
					return Row;
				};

				// General hookline settings
				Button = NextRow();
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &g_Config.m_ClShowHookCollOwn, "appearance-show-own-hook-collision", Localize("Show own player's hook collision line"), g_Config.m_ClShowHookCollOwn, &Button))
				{
					g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn ? 0 : 1;
				}
				Button = NextRow();
				if(g_Config.m_ClShowHookCollOwn)
				{
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &s_AppearanceAlwaysShowHookCollOwn, "appearance-always-show-own-hook-collision", Localize("Always show own player's hook collision line"), g_Config.m_ClShowHookCollOwn == 2, &Button))
						g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn != 2 ? 2 : 1;
				}

				Button = NextRow();
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &g_Config.m_ClShowHookCollOther, "appearance-show-other-hook-collision", Localize("Show other players' hook collision lines"), g_Config.m_ClShowHookCollOther, &Button))
				{
					g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther >= 1 ? 0 : 1;
				}
				Button = NextRow();
				if(g_Config.m_ClShowHookCollOther)
				{
					if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &s_AppearanceAlwaysShowHookCollOther, "appearance-always-show-other-hook-collision", Localize("Always show other players' hook collision lines"), g_Config.m_ClShowHookCollOther == 2, &Button))
						g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther != 2 ? 2 : 1;
				}

				Button = NextRow();
				DoAppearanceNumericField(APPEARANCE_TAB_HOOK_COLLISION, "appearance-hook-collision-own-width", &g_Config.m_ClHookCollSize, &g_Config.m_ClHookCollSize, Button, Localize("Width of your own hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale);

				Button = NextRow();
				DoAppearanceNumericField(APPEARANCE_TAB_HOOK_COLLISION, "appearance-hook-collision-other-width", &g_Config.m_ClHookCollSizeOther, &g_Config.m_ClHookCollSizeOther, Button, Localize("Width of others' hook collision line"), 0, 20, &CUi::ms_LinearScrollbarScale);

				Button = NextRow();
				DoAppearanceNumericField(APPEARANCE_TAB_HOOK_COLLISION, "appearance-hook-collision-opacity", &g_Config.m_ClHookCollAlpha, &g_Config.m_ClHookCollAlpha, Button, Localize("Hook collision line opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

				static CButtonContainer s_HookCollNoCollResetId, s_HookCollHookableCollResetId, s_HookCollTeeCollResetId, s_HookCollTipColorResetId;
				static int s_HookCollToolTip;

				DoAppearanceHeading(LeftView, "appearance-hook-collision-colors-title", Localize("Colors of the hook collision line, in case of a possible collision with:"), HeadlineFontSize, HeadlineHeight);

				Ui()->RegisterPassiveHotItem(&s_HookCollToolTip, &LeftView);
				GameClient()->m_Tooltips.DoToolTip(&s_HookCollToolTip, &LeftView, Localize("Your movements are not taken into account when calculating the line colors"));
				DoLine_ColorPicker(&s_HookCollNoCollResetId, AppearanceMetrics, &LeftView, Localize("Nothing hookable"), &g_Config.m_ClHookCollColorNoColl, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f), false);
				DoLine_ColorPicker(&s_HookCollHookableCollResetId, AppearanceMetrics, &LeftView, Localize("Something hookable"), &g_Config.m_ClHookCollColorHookableColl, ColorRGBA(130.0f / 255.0f, 232.0f / 255.0f, 160.0f / 255.0f, 1.0f), false);
				DoLine_ColorPicker(&s_HookCollTeeCollResetId, AppearanceMetrics, &LeftView, Localize("A Tee"), &g_Config.m_ClHookCollColorTeeColl, ColorRGBA(1.0f, 1.0f, 0.0f, 1.0f), false);
				DoLine_ColorPicker(&s_HookCollTipColorResetId, AppearanceMetrics, &LeftView, Localize("Hook line tip"), &g_Config.m_ClHookCollTipColor, ColorRGBA(1.0f, 1.0f, 0.0f, 0.5f), false, nullptr, true);
			});
			vCards.back().m_Measure = [ResolveHookCollisionLeftMinCardHeight](float) { return ResolveHookCollisionLeftMinCardHeight(); };
			vCards.back().m_MeasureRevision = static_cast<uint64_t>(g_Config.m_ClShowHookCollOwn != 0) | (static_cast<uint64_t>(g_Config.m_ClShowHookCollOther != 0) << 1);
			vCards.back().m_PreLayoutInput = [this, LineSize, MarginSmall](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CUIRect LeftView = Content;
				int RowsRemaining = 5 + (g_Config.m_ClShowHookCollOwn != 0 ? 1 : 0) + (g_Config.m_ClShowHookCollOther != 0 ? 1 : 0);
				const auto NextRow = [&]() {
					CUIRect Row;
					LeftView.HSplitTop(LineSize, &Row, &LeftView);
					if(--RowsRemaining > 0)
						LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
					return Row;
				};
				bool Changed = false;
				CUIRect OwnToggleRow = NextRow();
				if(Ui()->DoButtonLogic(&g_Config.m_ClShowHookCollOwn, g_Config.m_ClShowHookCollOwn != 0, &OwnToggleRow, BUTTONFLAG_LEFT))
				{
					g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn != 0 ? 0 : 1;
					Changed = true;
				}
				if(g_Config.m_ClShowHookCollOwn)
				{
					CUIRect AlwaysShowRow = NextRow();
					if(Ui()->DoButtonLogic(&s_AppearanceAlwaysShowHookCollOwn, g_Config.m_ClShowHookCollOwn == 2, &AlwaysShowRow, BUTTONFLAG_LEFT))
					{
						g_Config.m_ClShowHookCollOwn = g_Config.m_ClShowHookCollOwn != 2 ? 2 : 1;
						Changed = true;
					}
				}
				CUIRect OtherToggleRow = NextRow();
				if(Ui()->DoButtonLogic(&g_Config.m_ClShowHookCollOther, g_Config.m_ClShowHookCollOther != 0, &OtherToggleRow, BUTTONFLAG_LEFT))
				{
					g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther != 0 ? 0 : 1;
					Changed = true;
				}
				if(g_Config.m_ClShowHookCollOther)
				{
					CUIRect AlwaysShowRow = NextRow();
					if(Ui()->DoButtonLogic(&s_AppearanceAlwaysShowHookCollOther, g_Config.m_ClShowHookCollOther == 2, &AlwaysShowRow, BUTTONFLAG_LEFT))
					{
						g_Config.m_ClShowHookCollOther = g_Config.m_ClShowHookCollOther != 2 ? 2 : 1;
						Changed = true;
					}
				}
				NextRow();
				NextRow();
				NextRow();
				return Changed;
			};
			AddCard(8, HookCollisionRightMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect RightView = ContentRect;
				// ***** Hook collisions preview ***** //
				auto DoHookCollision = [this](const vec2 &Pos, const float &Length, const int &Size, const ColorRGBA &Color, const ColorRGBA &TipColor, const bool &Invert) {
					ColorRGBA ColorModified = Color;
					ColorRGBA TipColorModified = TipColor;
					if(Invert)
						ColorModified = color_invert(ColorModified);
					ColorModified = ColorModified.WithAlpha((float)g_Config.m_ClHookCollAlpha / 100);
					TipColorModified = TipColor.WithMultipliedAlpha((float)g_Config.m_ClHookCollAlpha / 100);
					Graphics()->TextureClear();
					if(Size > 0)
					{
						Graphics()->QuadsBegin();
						Graphics()->SetColor(ColorModified);
						float LineWidth = 0.5f + (float)(Size - 1) * 0.25f;
						IGraphics::CQuadItem QuadItem(Pos.x, Pos.y - LineWidth, Length, LineWidth * 2.f);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
						if(TipColor.a > 0.0f)
						{
							Graphics()->SetColor(TipColorModified);
							IGraphics::CQuadItem TipQuadItem(Pos.x + Length, Pos.y - LineWidth, 15.f, LineWidth * 2.f);
							Graphics()->QuadsDrawTL(&TipQuadItem, 1);
						}
						Graphics()->QuadsEnd();
					}
					else
					{
						Graphics()->LinesBegin();
						Graphics()->SetColor(ColorModified);
						IGraphics::CLineItem LineItem(Pos.x, Pos.y, Pos.x + Length, Pos.y);
						Graphics()->LinesDraw(&LineItem, 1);
						if(TipColor.a > 0.0f)
						{
							Graphics()->SetColor(TipColorModified);
							IGraphics::CLineItem TipLineItem(Pos.x + Length, Pos.y, Pos.x + Length + 15.f, Pos.y);
							Graphics()->LinesDraw(&TipLineItem, 1);
						}
						Graphics()->LinesEnd();
					}
				};

				CTeeRenderInfo OwnSkinInfo;
				OwnSkinInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
				OwnSkinInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
				OwnSkinInfo.m_Size = 50.0f;

				CTeeRenderInfo DummySkinInfo;
				DummySkinInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClDummySkin));
				DummySkinInfo.ApplyColors(g_Config.m_ClDummyUseCustomColor, g_Config.m_ClDummyColorBody, g_Config.m_ClDummyColorFeet);
				DummySkinInfo.m_Size = 50.0f;

				vec2 TeeRenderPos, DummyRenderPos;

				const float LineLength = 150.f;
				const float LeftMargin = 30.f;

				const int TileScale = 32.0f;

				// 预览 tile 取图：优先预览专用小图（避免菜单会话加载完整 entities），
				// 完整 entities 已在显存时沿用原路径；空白/缺失/解码中则跳过绘制。
				const auto DrawHookPreviewTile = [&](int TileIndex, const CUIRect &Rect) {
					IGraphics::CTextureHandle TileTexture;
					const auto Source = GameClient()->m_MapImages.GetHookPreviewTileSource(TileIndex, TileTexture);
					if(Source == CMapImages::EHookPreviewTileSource::SMALL_TEXTURE)
					{
						Graphics()->TextureClear();
						Graphics()->TextureSet(TileTexture);
						Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
						Graphics()->QuadsBegin();
						Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
						IGraphics::CQuadItem QuadItem(Rect.x, Rect.y, TileScale, TileScale);
						Graphics()->QuadsDrawTL(&QuadItem, 1);
						Graphics()->QuadsEnd();
					}
					else if(Source == CMapImages::EHookPreviewTileSource::FULL_ENTITIES_LOADED)
					{
						Graphics()->TextureClear();
						Graphics()->TextureSet(GameClient()->m_MapImages.GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH));
						Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
						RenderMap()->RenderTile(Rect.x, Rect.y, TileIndex, TileScale, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
					}
				};

				// Toggled via checkbox later, inverts some previews
				static bool s_HookCollPressed = false;

				CUIRect PreviewColl;
				const auto PrepareHookPreviewRow = [&](CUIRect &Preview) {
					Preview.Draw(ui_token::color::SURFACE_OVERLAY, IGraphics::CORNER_ALL, ui_token::radius::CARD);
					Preview.Margin(MarginSmall, &Preview);
				};

				// ***** Unhookable Tile Preview *****
				CUIRect PreviewNoColl;
				RightView.HSplitTop(50.0f, &PreviewNoColl, &RightView);
				PrepareHookPreviewRow(PreviewNoColl);
				RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewNoColl.x + LeftMargin, PreviewNoColl.y + PreviewNoColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewNoColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorNoColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				CUIRect NoHookTileRect;
				PreviewNoColl.VSplitRight(LineLength, &PreviewNoColl, &NoHookTileRect);
				NoHookTileRect.VSplitLeft(50.0f, &NoHookTileRect, nullptr);
				NoHookTileRect.Margin(10.0f, &NoHookTileRect);

				// Render unhookable tile
				DrawHookPreviewTile(TILE_NOHOOK, NoHookTileRect);

				// ***** Hookable Tile Preview *****
				RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
				PrepareHookPreviewRow(PreviewColl);
				RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorHookableColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				CUIRect HookTileRect;
				PreviewColl.VSplitRight(LineLength, &PreviewColl, &HookTileRect);
				HookTileRect.VSplitLeft(50.0f, &HookTileRect, nullptr);
				HookTileRect.Margin(10.0f, &HookTileRect);

				// Render hookable tile
				DrawHookPreviewTile(TILE_SOLID, HookTileRect);

				// ***** Hook Dummy Preview *****
				RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
				PrepareHookPreviewRow(PreviewColl);
				RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), s_HookCollPressed);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				// ***** Hook Dummy Reverse Preview *****
				RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
				PrepareHookPreviewRow(PreviewColl);
				RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DummyRenderPos = vec2(PreviewColl.x + PreviewColl.w - LineLength - 5.f + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSizeOther, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorTeeColl)), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), false);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), DummyRenderPos);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &DummySkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				// ***** Hook tip preview *****
				RightView.HSplitTop(50.0f, &PreviewColl, &RightView);
				PrepareHookPreviewRow(PreviewColl);
				RightView.HSplitTop(4 * MarginSmall, nullptr, &RightView);
				TeeRenderPos = vec2(PreviewColl.x + LeftMargin, PreviewColl.y + PreviewColl.h / 2.0f);
				DoHookCollision(TeeRenderPos, PreviewColl.w - LineLength - 15.f, g_Config.m_ClHookCollSize, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollColorNoColl)), color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClHookCollTipColor, true)), s_HookCollPressed);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &OwnSkinInfo, 0, vec2(1.0f, 0.0f), TeeRenderPos);

				// ***** Preview +hookcoll pressed toggle *****
				RightView.HSplitTop(LineSize, &Button, &RightView);
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION, &s_HookCollPressed, "appearance-preview-hook-collisions-pressed", Localize("Preview 'Hook collisions' being pressed"), s_HookCollPressed, &Button))
					s_HookCollPressed = !s_HookCollPressed;
			});
		}
		else if(m_AppearanceSettingsTab == APPEARANCE_TAB_INFO_MESSAGES)
		{
			const float InfoMessagesMinCardHeight = ResolveSettingsRowsHeight(2, LineSize, MarginSmall) + MarginSmall + ColorPickerRowHeight * 2.0f;
			AddCard(9, InfoMessagesMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect LeftView = ContentRect;
				int RowsRemaining = 2;
				const auto NextRow = [&]() {
					CUIRect Row;
					LeftView.HSplitTop(LineSize, &Row, &LeftView);
					if(--RowsRemaining > 0)
						LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
					return Row;
				};

				// General info messages settings
				Button = NextRow();
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_INFO_MESSAGES, &g_Config.m_ClShowKillMessages, "appearance-show-kill-messages", Localize("Show kill messages"), g_Config.m_ClShowKillMessages, &Button))
				{
					g_Config.m_ClShowKillMessages ^= 1;
				}

				Button = NextRow();
				if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_INFO_MESSAGES, &g_Config.m_ClShowFinishMessages, "appearance-show-finish-messages", Localize("Show finish messages"), g_Config.m_ClShowFinishMessages, &Button))
				{
					g_Config.m_ClShowFinishMessages ^= 1;
				}
				LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

				static CButtonContainer s_KillMessageNormalColorId, s_KillMessageHighlightColorId;
				DoLine_ColorPicker(&s_KillMessageNormalColorId, AppearanceMetrics, &LeftView, Localize("Normal Color"), &g_Config.m_ClKillMessageNormalColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
				DoLine_ColorPicker(&s_KillMessageHighlightColorId, AppearanceMetrics, &LeftView, Localize("Highlight Color"), &g_Config.m_ClKillMessageHighlightColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
			});
		}
		else if(m_AppearanceSettingsTab == APPEARANCE_TAB_LASER)
		{
			const float LaserPreviewHeight = std::clamp(56.0f * AppearanceUiScale, 40.0f, 56.0f);
			const auto ResolveLaserEnhancedMinCardHeight = [AppearanceMetrics]() {
				return ResolveAppearanceLaserEnhancedHeight(AppearanceMetrics, g_Config.m_QmLaserEnhanced != 0);
			};
			const float LaserColorMinCardHeight = ResolveAppearanceLaserColorsHeight(AppearanceMetrics);
			const float LaserPreviewMinCardHeight = (LaserPreviewHeight + 2.0f * MarginSmall) * 5.0f;
			AddCard(10, ResolveLaserEnhancedMinCardHeight(), [=, this](CUIRect ContentRect) mutable {
				CUIRect EnhancedCardContent = ContentRect;

				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, &g_Config.m_QmLaserEnhanced, "appearance-laser-effect-enhancement", Localize("Laser effect enhancement"), &g_Config.m_QmLaserEnhanced, &EnhancedCardContent, LineSize, 0.0f, AppearanceBodySize);
				EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
				EnhancedCardContent.HSplitTop(LineSize, &Button, &EnhancedCardContent);
				DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-glow-intensity", &g_Config.m_QmLaserGlowIntensity, &g_Config.m_QmLaserGlowIntensity, Button, Localize("Glow intensity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
				EnhancedCardContent.HSplitTop(LineSize, &Button, &EnhancedCardContent);
				DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-size", &g_Config.m_QmLaserSize, &g_Config.m_QmLaserSize, Button, Localize("Laser size"), 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
				EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
				EnhancedCardContent.HSplitTop(LineSize, &Button, &EnhancedCardContent);
				DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-opacity", &g_Config.m_QmLaserAlpha, &g_Config.m_QmLaserAlpha, Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
				DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, &g_Config.m_QmLaserRoundCaps, "appearance-laser-rounded-caps", Localize("Rounded caps"), &g_Config.m_QmLaserRoundCaps, &EnhancedCardContent, LineSize, 0.0f, AppearanceBodySize);
				if(g_Config.m_QmLaserEnhanced)
				{
					EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
					EnhancedCardContent.HSplitTop(LineSize, &Button, &EnhancedCardContent);
					DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-pulse-speed", &g_Config.m_QmLaserPulseSpeed, &g_Config.m_QmLaserPulseSpeed, Button, Localize("Pulse speed"), 10, 500);
					EnhancedCardContent.HSplitTop(MarginSmall, nullptr, &EnhancedCardContent);
					EnhancedCardContent.HSplitTop(LineSize, &Button, &EnhancedCardContent);
					DoAppearanceNumericField(APPEARANCE_TAB_LASER, "appearance-laser-pulse-amplitude", &g_Config.m_QmLaserPulseAmplitude, &g_Config.m_QmLaserPulseAmplitude, Button, Localize("Pulse amplitude"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
			});
			vCards.back().m_Measure = [ResolveLaserEnhancedMinCardHeight](float) { return ResolveLaserEnhancedMinCardHeight(); };
			vCards.back().m_MeasureRevision = static_cast<uint64_t>(g_Config.m_QmLaserEnhanced != 0);
			vCards.back().m_PreLayoutInput = [this, LineSize](CUIRect Content) {
				if(m_MenuTextPlanCollecting)
					return false;
				CUIRect ToggleRow;
				Content.HSplitTop(LineSize, &ToggleRow, &Content);
				if(!Ui()->DoButtonLogic(&g_Config.m_QmLaserEnhanced, 0, &ToggleRow, BUTTONFLAG_LEFT))
					return false;
				g_Config.m_QmLaserEnhanced ^= 1;
				return true;
			};
			AddCard(11, LaserColorMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect ColorCardContent = ContentRect;
				// ***** Weapons ***** //
				DoAppearanceHeading(ColorCardContent, "appearance-weapons-title", Localize("Weapons"), HeadlineFontSize, HeadlineHeight);
				ColorCardContent.HSplitTop(MarginSmall, nullptr, &ColorCardContent);

				// General weapon laser settings
				static CButtonContainer s_LaserRifleOutResetId, s_LaserRifleInResetId, s_LaserShotgunOutResetId, s_LaserShotgunInResetId;

				ColorHSLA LaserRifleOutlineColor = DoLine_ColorPicker(&s_LaserRifleOutResetId, AppearanceMetrics, &ColorCardContent, Localize("Rifle Laser Outline Color"), &g_Config.m_ClLaserRifleOutlineColor, ColorRGBA(0.074402f, 0.074402f, 0.247166f, 1.0f), false);
				ColorHSLA LaserRifleInnerColor = DoLine_ColorPicker(&s_LaserRifleInResetId, AppearanceMetrics, &ColorCardContent, Localize("Rifle Laser Inner Color"), &g_Config.m_ClLaserRifleInnerColor, ColorRGBA(0.498039f, 0.498039f, 1.0f, 1.0f), false);
				ColorHSLA LaserShotgunOutlineColor = DoLine_ColorPicker(&s_LaserShotgunOutResetId, AppearanceMetrics, &ColorCardContent, Localize("Shotgun Laser Outline Color"), &g_Config.m_ClLaserShotgunOutlineColor, ColorRGBA(0.125490f, 0.098039f, 0.043137f, 1.0f), false);
				ColorHSLA LaserShotgunInnerColor = DoLine_ColorPicker(&s_LaserShotgunInResetId, AppearanceMetrics, &ColorCardContent, Localize("Shotgun Laser Inner Color"), &g_Config.m_ClLaserShotgunInnerColor, ColorRGBA(0.570588f, 0.417647f, 0.252941f, 1.0f), false);

				// ***** Entities ***** //
				ColorCardContent.HSplitTop(10.0f, nullptr, &ColorCardContent);
				DoAppearanceHeading(ColorCardContent, "appearance-entities-title", Localize("Entities"), HeadlineFontSize, HeadlineHeight);
				ColorCardContent.HSplitTop(MarginSmall, nullptr, &ColorCardContent);

				// General entity laser settings
				static CButtonContainer s_LaserDoorOutResetId, s_LaserDoorInResetId, s_LaserFreezeOutResetId, s_LaserFreezeInResetId, s_LaserDraggerOutResetId, s_LaserDraggerInResetId;

				ColorHSLA LaserDoorOutlineColor = DoLine_ColorPicker(&s_LaserDoorOutResetId, AppearanceMetrics, &ColorCardContent, Localize("Door Laser Outline Color"), &g_Config.m_ClLaserDoorOutlineColor, ColorRGBA(0.0f, 0.131372f, 0.096078f, 1.0f), false);
				ColorHSLA LaserDoorInnerColor = DoLine_ColorPicker(&s_LaserDoorInResetId, AppearanceMetrics, &ColorCardContent, Localize("Door Laser Inner Color"), &g_Config.m_ClLaserDoorInnerColor, ColorRGBA(0.262745f, 0.760784f, 0.639215f, 1.0f), false);
				ColorHSLA LaserFreezeOutlineColor = DoLine_ColorPicker(&s_LaserFreezeOutResetId, AppearanceMetrics, &ColorCardContent, Localize("Freeze Laser Outline Color"), &g_Config.m_ClLaserFreezeOutlineColor, ColorRGBA(0.131372f, 0.123529f, 0.182352f, 1.0f), false);
				ColorHSLA LaserFreezeInnerColor = DoLine_ColorPicker(&s_LaserFreezeInResetId, AppearanceMetrics, &ColorCardContent, Localize("Freeze Laser Inner Color"), &g_Config.m_ClLaserFreezeInnerColor, ColorRGBA(0.482352f, 0.443137f, 0.564705f, 1.0f), false);
				ColorHSLA LaserDraggerOutlineColor = DoLine_ColorPicker(&s_LaserDraggerOutResetId, AppearanceMetrics, &ColorCardContent, Localize("Dragger Outline Color"), &g_Config.m_ClLaserDraggerOutlineColor, ColorRGBA(0.1640625f, 0.015625f, 0.015625f, 1.0f), false);
				ColorHSLA LaserDraggerInnerColor = DoLine_ColorPicker(&s_LaserDraggerInResetId, AppearanceMetrics, &ColorCardContent, Localize("Dragger Inner Color"), &g_Config.m_ClLaserDraggerInnerColor, ColorRGBA(.8666666f, .3725490f, .3725490f, 1.0f), false);

				static CButtonContainer s_AllToRifleResetId, s_AllToDefaultResetId;

				ColorCardContent.HSplitTop(4 * MarginSmall, nullptr, &ColorCardContent);
				ColorCardContent.HSplitTop(AppearanceMetrics.m_ButtonHeight, &Button, &ColorCardContent);
				if(DoSettingsButton_Menu(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, APPEARANCE_TAB_LASER, &s_AllToRifleResetId, "appearance-laser-set-all-to-rifle", Localize("Set all to Rifle"), 0, &Button))
				{
					g_Config.m_ClLaserShotgunOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
					g_Config.m_ClLaserShotgunInnerColor = g_Config.m_ClLaserRifleInnerColor;
					g_Config.m_ClLaserDoorOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
					g_Config.m_ClLaserDoorInnerColor = g_Config.m_ClLaserRifleInnerColor;
					g_Config.m_ClLaserFreezeOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
					g_Config.m_ClLaserFreezeInnerColor = g_Config.m_ClLaserRifleInnerColor;
					g_Config.m_ClLaserDraggerOutlineColor = g_Config.m_ClLaserRifleOutlineColor;
					g_Config.m_ClLaserDraggerInnerColor = g_Config.m_ClLaserRifleInnerColor;
				}

				// values taken from the CL commands
				ColorCardContent.HSplitTop(2 * MarginSmall, nullptr, &ColorCardContent);
				ColorCardContent.HSplitTop(AppearanceMetrics.m_ButtonHeight, &Button, &ColorCardContent);
				if(DoSettingsButton_Menu(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, APPEARANCE_TAB_LASER, &s_AllToDefaultResetId, "appearance-laser-reset-defaults", Localize("Reset to defaults"), 0, &Button))
				{
					g_Config.m_ClLaserRifleOutlineColor = 11176233;
					g_Config.m_ClLaserRifleInnerColor = 11206591;
					g_Config.m_ClLaserShotgunOutlineColor = 1866773;
					g_Config.m_ClLaserShotgunInnerColor = 1467241;
					g_Config.m_ClLaserDoorOutlineColor = 7667473;
					g_Config.m_ClLaserDoorInnerColor = 7701379;
					g_Config.m_ClLaserFreezeOutlineColor = 11613223;
					g_Config.m_ClLaserFreezeInnerColor = 12001153;
					g_Config.m_ClLaserDraggerOutlineColor = 57618;
					g_Config.m_ClLaserDraggerInnerColor = 42398;
				}
			});
			AddCard(12, LaserPreviewMinCardHeight, [=, this](CUIRect ContentRect) mutable {
				CUIRect PreviewCardContent = ContentRect;
				const ColorHSLA LaserRifleOutlineColor = ColorHSLA(g_Config.m_ClLaserRifleOutlineColor);
				const ColorHSLA LaserRifleInnerColor = ColorHSLA(g_Config.m_ClLaserRifleInnerColor);
				const ColorHSLA LaserShotgunOutlineColor = ColorHSLA(g_Config.m_ClLaserShotgunOutlineColor);
				const ColorHSLA LaserShotgunInnerColor = ColorHSLA(g_Config.m_ClLaserShotgunInnerColor);
				const ColorHSLA LaserDoorOutlineColor = ColorHSLA(g_Config.m_ClLaserDoorOutlineColor);
				const ColorHSLA LaserDoorInnerColor = ColorHSLA(g_Config.m_ClLaserDoorInnerColor);
				const ColorHSLA LaserFreezeOutlineColor = ColorHSLA(g_Config.m_ClLaserFreezeOutlineColor);
				const ColorHSLA LaserFreezeInnerColor = ColorHSLA(g_Config.m_ClLaserFreezeInnerColor);
				const ColorHSLA LaserDraggerOutlineColor = ColorHSLA(g_Config.m_ClLaserDraggerOutlineColor);
				const ColorHSLA LaserDraggerInnerColor = ColorHSLA(g_Config.m_ClLaserDraggerInnerColor);
				// ***** Laser Preview ***** //

				CUIRect LaserPreviewRect;
				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * MarginSmall, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserRifleOutlineColor, LaserRifleInnerColor, LASERTYPE_RIFLE);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * MarginSmall, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserShotgunOutlineColor, LaserShotgunInnerColor, LASERTYPE_SHOTGUN);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * MarginSmall, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserDoorOutlineColor, LaserDoorInnerColor, LASERTYPE_DOOR);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * MarginSmall, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserFreezeOutlineColor, LaserFreezeInnerColor, LASERTYPE_FREEZE);

				PreviewCardContent.HSplitTop(LaserPreviewHeight, &LaserPreviewRect, &PreviewCardContent);
				PreviewCardContent.HSplitTop(2 * MarginSmall, nullptr, &PreviewCardContent);
				LaserPreviewRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, ui_token::radius::BASE);
				DoLaserPreview(&LaserPreviewRect, LaserDraggerOutlineColor, LaserDraggerInnerColor, LASERTYPE_DRAGGER);
			});
		}
	};
	uint64_t AppearanceLayoutRevision = static_cast<uint64_t>(m_AppearanceSettingsTab & 0xff);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ NamePlatePreviewMeasureRevision;
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_QmNameplateEffectAutoLod != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(RenderOnly ? 1 : 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowhudDDRace != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowFreezeBars != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowChat != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_QmChatLogAutoSave != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClChatOld != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowChatSystem != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowChatFriends != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowChatTeamMembersOnly != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_TcShowChatClient != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(std::clamp(g_Config.m_ClChatFontSize, 0, 255));
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(std::clamp(g_Config.m_ClChatWidth, 0, 1023));
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(str_quickhash(Client()->PlayerName()));
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ qm_card_catalog::NameplateMeasureContentRevision();
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowHookCollOwn != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClShowHookCollOther != 0);
	AppearanceLayoutRevision = AppearanceLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_QmLaserEnhanced != 0);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, AppearanceLayoutRevision);

	const SQmScrollRequest ScrollRequest{EQmScrollProfile::SETTINGS_OUTER};
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest, AppearanceUiScale, 0.0f);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	CQmScrollState &ScrollState = s_AppearanceSettingsCardScrollRegions[m_AppearanceSettingsTab].State();
	// Deck 通过同一个 region 消费该状态；显式取得它以固定页面唯一的滚动状态所有权。
	(void)ScrollState;
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = Ui()->MouseX();
	InputState.m_MouseY = Ui()->MouseY();
	InputState.m_MousePressed = Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = Ui()->MouseButton(0);
	InputState.m_MouseReleased = !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = Input()->ModifierIsPressed();
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = RenderOnly ? nullptr : &ScrollParams;
	const SSettingsCardDeckResult DeckResult = SettingsCardDeckForRenderPass().RenderCached(AppearanceCardCtx, AppearancePage, pAppearanceDeckTab, DefinitionsRevision, BuildDefinitions, SettingsCardOrderModelForRenderPass(), RenderOnly ? nullptr : &s_AppearanceSettingsCardScrollRegions[m_AppearanceSettingsTab], InputState, SettingsCardMotionSpec(), AppearanceVisualOptions);
	if(!RenderOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

void CMenus::RenderSettingsDDNet(CUIRect MainView)
{
	CPerfTimer ShellTimer;
	CUIRect Button, Left, Right, LeftLeft, Label;
	LogPerfStage(Client(), "ddnet_tab_shell", ShellTimer.ElapsedMs(), false, "page=ddnet");

	const SSettingsContentMetrics DDNetMetrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = DDNetMetrics.m_UiScale;
	const float BodySize = DDNetMetrics.m_BodySize;
	const float DDNetRowPitch = DDNetMetrics.m_LineHeight + DDNetMetrics.m_LineSpacing;
	const auto SplitDDNetRow = [DDNetMetrics](CUIRect &View, CUIRect *pRow) {
		View.HSplitTop(DDNetMetrics.m_LineHeight, pRow, &View);
		View.HSplitTop(DDNetMetrics.m_LineSpacing, nullptr, &View);
	};
	static CScrollRegion s_DDNetSettingsCardScrollRegion;
	const SSettingsPageLayoutFrame DDNetPage = SettingsPageLayout(MainView, UiScale);
	const IUiContext DDNetCardCtx = SettingsUiContext("settings_ddnet", UiScale);
	const SSettingsCardDeckVisualOptions DDNetVisualOptions = SettingsCardDeckVisualOptions();
	const auto DoDDNetNumericField = [this, DDNetCardCtx, BodySize](const char *pTextId, const void *pId, int *pOption, const CUIRect &Rect, const char *pLabel, int Min, int Max, const IScrollbarScale *pScale = &CUi::ms_LinearScrollbarScale, unsigned Flags = 0u, const char *pSuffix = "", const char *pMaxText = nullptr) {
		ui_widget::SNumericFieldOptions Options;
		Options.m_pLabel = pLabel;
		Options.m_pSuffix = pSuffix;
		Options.m_pScale = pScale;
		Options.m_Flags = Flags;
		Options.m_pMaxText = pMaxText;
		Options.m_FontSize = BodySize;
		Options.m_LabelAlign = TEXTALIGN_ML;
		Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ? ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT : ui_widget::EInputCommitPolicy::LIVE;
		if(PrepareSettingsNumericFieldLabel(SETTINGS_DDNET, -1, -1, pTextId, Rect, pLabel, Flags, Options))
			return false;
		return ui_widget::NumericField(DDNetCardCtx, GetSettingsNumericFieldState(pId), pId, pOption, Min, Max, Rect, Options);
	};
	const qm_card_registry::SCardDefault *pDemoDefault = qm_card_registry::FindByStableId("deck:ddnet-demo");
	const qm_card_registry::SCardDefault *pGameplayDefault = qm_card_registry::FindByStableId("deck:ddnet-gameplay");
	const qm_card_registry::SCardDefault *pBackgroundDefault = qm_card_registry::FindByStableId("deck:ddnet-background");
	const qm_card_registry::SCardDefault *pMiscellaneousDefault = qm_card_registry::FindByStableId("deck:ddnet-miscellaneous");
	dbg_assert(pDemoDefault != nullptr && pGameplayDefault != nullptr && pBackgroundDefault != nullptr && pMiscellaneousDefault != nullptr, "DDNet settings cards must be registered");
	if(pDemoDefault == nullptr || pGameplayDefault == nullptr || pBackgroundDefault == nullptr || pMiscellaneousDefault == nullptr)
		return;
	const float CardChromeHeight = BuildSettingsCardFrame({0.0f, 0.0f, 1.0f, 0.0f}, {nullptr, nullptr, "subtitle"}, 0.0f, UiScale).m_Rect.h;
	const bool RenderOnly = Ui()->RenderOnly();
	const auto BuildDefinitions = [=, this](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(4);
		const SSettingsCardSpec DemoSpec{pDemoDefault->m_pStableId, Localize(pDemoDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pDemoDefault)};
		const SSettingsCardSpec GameplaySpec{pGameplayDefault->m_pStableId, Localize(pGameplayDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pGameplayDefault)};
		const SSettingsCardSpec BackgroundSpec{pBackgroundDefault->m_pStableId, Localize(pBackgroundDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pBackgroundDefault)};
		const SSettingsCardSpec MiscellaneousSpec{pMiscellaneousDefault->m_pStableId, Localize(pMiscellaneousDefault->m_pTitle), qm_card_registry::ResolveLocalizedDescription(*pMiscellaneousDefault)};
		const auto AddCard = [&vCards](const SSettingsCardSpec &Spec, float MinHeight, float ChromeHeight, FSettingsCardRender Render) {
			SSettingsCardDefinition Definition;
			Definition.m_Spec = Spec;
			// MinHeight 已经按当前开关状态计算，测量阶段不能依赖上一帧绘制结果。
			Definition.m_Measure = [MinHeight, ChromeHeight](float) { return maximum(0.0f, MinHeight - ChromeHeight); };
			Definition.m_Render = std::move(Render);
			Definition.m_MeasureRevision = static_cast<uint64_t>(maximum(0.0f, MinHeight) * 1000.0f);
			vCards.push_back(std::move(Definition));
		};
		const auto ProcessDemoPreLayoutInput = [this, SplitDDNetRow](CUIRect ContentRect) {
			if(m_MenuTextPlanCollecting)
				return false;
			CUIRect Left = ContentRect;
			CUIRect Right;
			CUIRect Button;
			bool Changed = false;
			SplitDDNetRow(Left, &Button);
			SplitDDNetRow(Left, &Button);
			if(Ui()->DoButtonLogic(&g_Config.m_ClReplays, 0, &Button, BUTTONFLAG_LEFT))
			{
				g_Config.m_ClReplays ^= 1;
				if(Client()->State() == IClient::STATE_ONLINE)
					Client()->DemoRecorder_UpdateReplayRecorder();
				Changed = true;
			}
			if(g_Config.m_ClReplays)
			{
				SplitDDNetRow(Left, &Button);
				SplitDDNetRow(Left, &Button);
			}
			Right = Left;
			SplitDDNetRow(Right, &Button);
			if(Ui()->DoButtonLogic(&g_Config.m_ClRaceGhost, 0, &Button, BUTTONFLAG_LEFT))
			{
				g_Config.m_ClRaceGhost ^= 1;
				Changed = true;
			}
			if(g_Config.m_ClRaceGhost)
			{
				SplitDDNetRow(Right, &Button);
				SplitDDNetRow(Right, &Button);
				SplitDDNetRow(Right, &Button);
				if(Ui()->DoButtonLogic(&g_Config.m_ClRaceSaveGhost, 0, &Button, BUTTONFLAG_LEFT))
				{
					g_Config.m_ClRaceSaveGhost ^= 1;
					Changed = true;
				}
			}
			return Changed;
		};
		const auto ProcessGameplayPreLayoutInput = [this, SplitDDNetRow, DDNetMetrics, UiScale](CUIRect ContentRect) {
			if(m_MenuTextPlanCollecting)
				return false;
			CUIRect Gameplay = ContentRect;
			CUIRect PreLayoutButton;
			CUIRect GameplayRow;
			CUIRect TextEntitiesLabel;
			SplitDDNetRow(Gameplay, &PreLayoutButton);
			SplitDDNetRow(Gameplay, &GameplayRow);
			GameplayRow.VSplitLeft(std::clamp(GameplayRow.w * 0.38f, 140.0f * UiScale, 240.0f * UiScale), &TextEntitiesLabel, &PreLayoutButton);
			PreLayoutButton.VSplitLeft(DDNetMetrics.m_LineSpacing, nullptr, &PreLayoutButton);
			if(Ui()->DoButtonLogic(&g_Config.m_ClTextEntities, 0, &TextEntitiesLabel, BUTTONFLAG_LEFT))
			{
				g_Config.m_ClTextEntities ^= 1;
				return true;
			}
			for(int Row = 0; Row < 6; ++Row)
				SplitDDNetRow(Gameplay, &PreLayoutButton);
			SplitDDNetRow(Gameplay, &PreLayoutButton);
			if(!Ui()->DoButtonLogic(&g_Config.m_ClAntiPing, 0, &PreLayoutButton, BUTTONFLAG_LEFT))
				return false;
			g_Config.m_ClAntiPing ^= 1;
			return true;
		};

		// demo
		const bool ReplaysLayout = g_Config.m_ClReplays != 0;
		const bool RaceGhostLayout = g_Config.m_ClRaceGhost != 0;
		const bool RaceSaveGhostLayout = g_Config.m_ClRaceSaveGhost != 0;
		const float DemoRows = ResolveDDNetDemoRows(ReplaysLayout, RaceGhostLayout, RaceSaveGhostLayout);
		const float DemoMinCardHeight = CardChromeHeight + DDNetRowPitch * DemoRows;
		AddCard(DemoSpec, DemoMinCardHeight, CardChromeHeight, [=, this](CUIRect ContentRect) mutable {
			CPerfTimer DemoSectionTimer;
			CUIRect Demo = ContentRect;
			Left = Demo;

			SplitDDNetRow(Left, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAutoRaceRecord, "Save the best demo of each race", Localize("Save the best demo of each race"), g_Config.m_ClAutoRaceRecord, &Button))
			{
				g_Config.m_ClAutoRaceRecord ^= 1;
			}

			SplitDDNetRow(Left, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClReplays, "Enable replays", Localize("Enable replays"), g_Config.m_ClReplays, &Button))
			{
				g_Config.m_ClReplays ^= 1;
				if(Client()->State() == IClient::STATE_ONLINE)
				{
					Client()->DemoRecorder_UpdateReplayRecorder();
				}
			}

			if(g_Config.m_ClReplays)
			{
				SplitDDNetRow(Left, &Button);
				DoDDNetNumericField("ddnet-replay-default-length", &g_Config.m_ClReplayLength, &g_Config.m_ClReplayLength, Button, Localize("Default length"), 10, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);

				SplitDDNetRow(Left, &Button);
				DoDDNetNumericField("ddnet-esc-replay-minutes", &g_Config.m_ClEscReplayLengthMinutes, &g_Config.m_ClEscReplayLengthMinutes, Button, Localize("ESC replay minutes"), 1, 60, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
			}

			// 回放与幽灵选项属于同一组，沿同一列排列，避免窄列截断中文标签。
			Right = Left;
			SplitDDNetRow(Right, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceGhost, "Enable ghost", Localize("Enable ghost"), g_Config.m_ClRaceGhost, &Button))
			{
				g_Config.m_ClRaceGhost ^= 1;
			}
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClRaceGhost, &Button, Localize("When you cross the start line, show a ghost tee replicating the movements of your best time"));

			if(g_Config.m_ClRaceGhost)
			{
				SplitDDNetRow(Right, &Button);
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceShowGhost, "Show ghost", Localize("Show ghost"), g_Config.m_ClRaceShowGhost, &Button))
				{
					g_Config.m_ClRaceShowGhost ^= 1;
				}

				SplitDDNetRow(Right, &Button);
				DoDDNetNumericField("ddnet-race-ghost-opacity", &g_Config.m_ClRaceGhostAlpha, &g_Config.m_ClRaceGhostAlpha, Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

				SplitDDNetRow(Right, &Button);
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceSaveGhost, "Save ghost", Localize("Save ghost"), g_Config.m_ClRaceSaveGhost, &Button))
				{
					g_Config.m_ClRaceSaveGhost ^= 1;
				}

				if(g_Config.m_ClRaceSaveGhost)
				{
					SplitDDNetRow(Right, &Button);
					if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceGhostSaveBest, "Only save improvements", Localize("Only save improvements"), g_Config.m_ClRaceGhostSaveBest, &Button))
					{
						g_Config.m_ClRaceGhostSaveBest ^= 1;
					}
				}
			}
			LogPerfStage(Client(), "ddnet_demo_section", DemoSectionTimer.ElapsedMs(), false, "page=ddnet section=demo");
		});
		vCards.back().m_Measure = [DDNetRowPitch](float) {
			return DDNetRowPitch * ResolveDDNetDemoRows(g_Config.m_ClReplays != 0, g_Config.m_ClRaceGhost != 0, g_Config.m_ClRaceSaveGhost != 0);
		};
		vCards.back().m_MeasureRevision =
			((uint64_t)(g_Config.m_ClReplays != 0) << 0) |
			((uint64_t)(g_Config.m_ClRaceGhost != 0) << 1) |
			((uint64_t)(g_Config.m_ClRaceSaveGhost != 0) << 2);
		vCards.back().m_PreLayoutInput = ProcessDemoPreLayoutInput;

		// gameplay
		const bool TextEntitiesLayout = g_Config.m_ClTextEntities != 0;
		const bool AntiPingLayout = g_Config.m_ClAntiPing != 0;
		const float GameplayMinCardHeight = CardChromeHeight + DDNetRowPitch * ResolveDDNetGameplayRows(TextEntitiesLayout, AntiPingLayout);
		AddCard(GameplaySpec, GameplayMinCardHeight, CardChromeHeight, [=, this](CUIRect ContentRect) mutable {
			CPerfTimer GameplaySectionTimer;
			CUIRect Gameplay = ContentRect;
			CUIRect GameplayRow;

			SplitDDNetRow(Gameplay, &Button);
			DoDDNetNumericField("ddnet-overlay-entities", &g_Config.m_ClOverlayEntities, &g_Config.m_ClOverlayEntities, Button, Localize("Overlay entities"), 0, 100);

			SplitDDNetRow(Gameplay, &GameplayRow);
			GameplayRow.VSplitLeft(std::clamp(GameplayRow.w * 0.38f, 140.0f * UiScale, 240.0f * UiScale), &LeftLeft, &Button);
			Button.VSplitLeft(DDNetMetrics.m_LineSpacing, nullptr, &Button);

			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClTextEntities, "Show text entities", Localize("Show text entities"), g_Config.m_ClTextEntities, &LeftLeft))
				g_Config.m_ClTextEntities ^= 1;

			if(g_Config.m_ClTextEntities)
			{
				if(DoDDNetNumericField("ddnet-text-entities-size", &g_Config.m_ClTextEntitiesSize, &g_Config.m_ClTextEntitiesSize, Button, Localize("Size"), 20, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_DELAYUPDATE))
					GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
			}

			SplitDDNetRow(Gameplay, &GameplayRow);
			GameplayRow.VSplitLeft(std::clamp(GameplayRow.w * 0.38f, 140.0f * UiScale, 240.0f * UiScale), &LeftLeft, &Button);
			Button.VSplitLeft(DDNetMetrics.m_LineSpacing, nullptr, &Button);

			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClShowOthers, "Show others", Localize("Show others"), g_Config.m_ClShowOthers == SHOW_OTHERS_ON, &LeftLeft))
				g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ON ? SHOW_OTHERS_ON : SHOW_OTHERS_OFF;

			DoDDNetNumericField("ddnet-show-others-opacity", &g_Config.m_ClShowOthersAlpha, &g_Config.m_ClShowOthersAlpha, Button, Localize("Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");

			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowOthersAlpha, &Button, Localize("Adjust the opacity of entities belonging to other teams, such as tees and name plates"));

			SplitDDNetRow(Gameplay, &Button);
			static int s_ShowOwnTeamId = 0;
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &s_ShowOwnTeamId, "Show others (own team only)", Localize("Show others (own team only)"), g_Config.m_ClShowOthers == SHOW_OTHERS_ONLY_TEAM, &Button))
			{
				g_Config.m_ClShowOthers = g_Config.m_ClShowOthers != SHOW_OTHERS_ONLY_TEAM ? SHOW_OTHERS_ONLY_TEAM : SHOW_OTHERS_OFF;
			}

			SplitDDNetRow(Gameplay, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClShowQuads, "Show background quads", Localize("Show background quads"), g_Config.m_ClShowQuads, &Button))
			{
				g_Config.m_ClShowQuads ^= 1;
			}
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClShowQuads, &Button, Localize("Quads are used for background decoration"));

			SplitDDNetRow(Gameplay, &Button);
			if(DoDDNetNumericField("ddnet-default-zoom", &g_Config.m_ClDefaultZoom, &g_Config.m_ClDefaultZoom, Button, Localize("Default zoom"), 0, 20))
				GameClient()->m_Camera.SetZoom(CCamera::ZoomStepsToValue(g_Config.m_ClDefaultZoom - 10), g_Config.m_ClSmoothZoomTime, true);

			SplitDDNetRow(Gameplay, &Button);
			DoDDNetNumericField("ddnet-prediction-margin", &g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, Button, Localize("Prediction margin"), 1, 300, &CUi::ms_LinearScrollbarScale, 0u, "");

			SplitDDNetRow(Gameplay, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClPredictEvents, "Predict events (experimental)", Localize("Predict events (experimental)"), g_Config.m_ClPredictEvents, &Button))
			{
				g_Config.m_ClPredictEvents ^= 1;
			}

			SplitDDNetRow(Gameplay, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAntiPing, "AntiPing (latency compensation)", Localize("AntiPing (latency compensation)"), g_Config.m_ClAntiPing, &Button))
			{
				g_Config.m_ClAntiPing ^= 1;
			}
			GameClient()->m_Tooltips.DoToolTip(&g_Config.m_ClAntiPing, &Button, Localize("Try to predict other entities to reduce lag feeling at high latency"));

			if(g_Config.m_ClAntiPing)
			{
				SplitDDNetRow(Gameplay, &Button);
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAntiPingPlayers, "AntiPing: predict other players", Localize("AntiPing: predict other players"), g_Config.m_ClAntiPingPlayers, &Button))
				{
					g_Config.m_ClAntiPingPlayers ^= 1;
				}

				SplitDDNetRow(Gameplay, &Button);
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAntiPingWeapons, "AntiPing: predict weapons", Localize("AntiPing: predict weapons"), g_Config.m_ClAntiPingWeapons, &Button))
				{
					g_Config.m_ClAntiPingWeapons ^= 1;
				}

				SplitDDNetRow(Gameplay, &Button);
				if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClAntiPingGrenade, "AntiPing: predict grenade path", Localize("AntiPing: predict grenade path"), g_Config.m_ClAntiPingGrenade, &Button))
				{
					g_Config.m_ClAntiPingGrenade ^= 1;
				}
			}
			LogPerfStage(Client(), "ddnet_gameplay_section", GameplaySectionTimer.ElapsedMs(), false, "page=ddnet section=gameplay");
		});
		vCards.back().m_Measure = [DDNetRowPitch](float) {
			return DDNetRowPitch * ResolveDDNetGameplayRows(g_Config.m_ClTextEntities != 0, g_Config.m_ClAntiPing != 0);
		};
		vCards.back().m_MeasureRevision =
			((uint64_t)(g_Config.m_ClTextEntities != 0) << 0) |
			((uint64_t)(g_Config.m_ClAntiPing != 0) << 1);
		vCards.back().m_PreLayoutInput = ProcessGameplayPreLayoutInput;

		const float DDNetColorPickerRowHeight = DDNetMetrics.m_ButtonHeight + DDNetMetrics.m_LineSpacing;
		const float BackgroundMinCardHeight = CardChromeHeight + DDNetColorPickerRowHeight * 2.0f + DDNetRowPitch * 3.0f + 2.0f;
		CPerfTimer ControlsSectionTimer;
		AddCard(BackgroundSpec, BackgroundMinCardHeight, CardChromeHeight, [=, this](CUIRect ContentRect) mutable {
			CUIRect Background = ContentRect;

			// background
			ColorRGBA GreyDefault(0.5f, 0.5f, 0.5f, 1);

			static CButtonContainer s_ResetId1;
			DoLine_ColorPicker(&s_ResetId1, DDNetMetrics, &Background, Localize("Regular background color"), &g_Config.m_ClBackgroundColor, GreyDefault, false);

			static CButtonContainer s_ResetId2;
			DoLine_ColorPicker(&s_ResetId2, DDNetMetrics, &Background, Localize("Entities background color"), &g_Config.m_ClBackgroundEntitiesColor, GreyDefault, false);

			CUIRect EditBox, ReloadButton;
			SplitDDNetRow(Background, &Label);
			Background.HSplitTop(2.0f, nullptr, &Background);
			Label.VSplitLeft(100.0f, &Label, &EditBox);
			EditBox.VSplitRight(60.0f, &EditBox, &Button);
			Button.VSplitMid(&ReloadButton, &Button, 5.0f);
			EditBox.VSplitRight(5.0f, &EditBox, nullptr);

			DoSettingsMenuLabel(SETTINGS_DDNET, -1, -1, "ddnet-background-map-label", &Label, Localize("Map"), BodySize, TEXTALIGN_ML);

			static CLineInput s_BackgroundEntitiesInput(g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities));
			static char s_aBackgroundEntitiesSync[sizeof(g_Config.m_ClBackgroundEntities)] = "";
			const bool WasInputActive = s_BackgroundEntitiesInput.IsActive();
			IUiContext DDNetBackgroundEntitiesTextInputCtx;
			DDNetBackgroundEntitiesTextInputCtx.m_pUi = Ui();
			DDNetBackgroundEntitiesTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_ddnet_background_entities_text_input");
			const bool InputCommitted = ui_widget::InputField(DDNetBackgroundEntitiesTextInputCtx, &s_BackgroundEntitiesInput, EditBox, nullptr, BodySize);
			bool BackgroundChanged = false;
			if(InputCommitted)
				BackgroundChanged = ApplyBackgroundEntitiesInputValue(s_BackgroundEntitiesInput);
			else if(ShouldCommitBackgroundEntitiesInputOnBlur(WasInputActive, s_BackgroundEntitiesInput.IsActive(), s_BackgroundEntitiesInput.GetString(), s_aBackgroundEntitiesSync))
				BackgroundChanged = ApplyBackgroundEntitiesInputValue(s_BackgroundEntitiesInput);
			SyncBackgroundEntitiesInput(s_BackgroundEntitiesInput, s_aBackgroundEntitiesSync, sizeof(s_aBackgroundEntitiesSync));

			static CButtonContainer s_BackgroundEntitiesMapPicker, s_BackgroundEntitiesReload;

			if(Ui()->DoButton_QmIcon(&s_BackgroundEntitiesReload, EQmIcon::ARROW_ROTATE_RIGHT, FONT_ICON_ARROW_ROTATE_RIGHT, 0, &ReloadButton, BUTTONFLAG_LEFT))
			{
				CommitBackgroundEntitiesInputIfActive(s_BackgroundEntitiesInput, s_aBackgroundEntitiesSync, sizeof(s_aBackgroundEntitiesSync));
				g_Config.m_ClBackgroundEntities[0] = '\0';
				s_BackgroundEntitiesInput.Set("");
				s_aBackgroundEntitiesSync[0] = '\0';
				BackgroundChanged = true;
			}

			if(Ui()->DoButton_QmIcon(&s_BackgroundEntitiesMapPicker, EQmIcon::FOLDER, FONT_ICON_FOLDER, 0, &Button, BUTTONFLAG_LEFT))
			{
				BackgroundChanged |= CommitBackgroundEntitiesInputIfActive(s_BackgroundEntitiesInput, s_aBackgroundEntitiesSync, sizeof(s_aBackgroundEntitiesSync));
				static SPopupMenuId s_PopupMapPickerId;
				static CPopupMapPickerContext s_PopupMapPickerContext;
				s_PopupMapPickerContext.m_pMenus = this;
				s_PopupMapPickerContext.m_aCurrentMapFolder[0] = '\0';
				str_copy(s_PopupMapPickerContext.m_aRootPath, "maps", sizeof(s_PopupMapPickerContext.m_aRootPath));
				str_copy(s_PopupMapPickerContext.m_aFallbackRootPath, "mapres", sizeof(s_PopupMapPickerContext.m_aFallbackRootPath));
				s_PopupMapPickerContext.m_aValuePrefix[0] = '\0';
				str_copy(s_PopupMapPickerContext.m_aFallbackValuePrefix, "mapres", sizeof(s_PopupMapPickerContext.m_aFallbackValuePrefix));
				s_PopupMapPickerContext.m_pTargetConfig = g_Config.m_ClBackgroundEntities;
				s_PopupMapPickerContext.m_TargetConfigSize = sizeof(g_Config.m_ClBackgroundEntities);
				s_PopupMapPickerContext.MapListPopulate();
				const SQmDropdownPopupPolicy PopupPolicy = QmResolveDropdownPopupPolicy((int)s_PopupMapPickerContext.m_vMaps.size(), 20.0f, 0.0f, false, 0.0f, CUi::PopupMenuContentInset(), 1);
				SPopupMenuProperties PopupProps;
				PopupProps.m_BlockUnderlyingScroll = true;
				PopupProps.m_CenterInViewport = true;
				PopupProps.m_BlockUnderlyingPointerInput = true;
				PopupProps.m_Animate = true;
				Ui()->DoPopupMenu(&s_PopupMapPickerId, Ui()->MouseX(), Ui()->MouseY(), 300.0f, PopupPolicy.m_PreferredHeight, &s_PopupMapPickerContext, PopupMapPicker, PopupProps);
			}

			SplitDDNetRow(Background, &Button);
			const bool UseCurrentMap = IsCurrentMapBackgroundEntitiesValue(g_Config.m_ClBackgroundEntities);
			static int s_UseCurrentMapId = 0;
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &s_UseCurrentMapId, "Use current map as background", Localize("Use current map as background"), UseCurrentMap, &Button))
			{
				BackgroundChanged |= CommitBackgroundEntitiesInputIfActive(s_BackgroundEntitiesInput, s_aBackgroundEntitiesSync, sizeof(s_aBackgroundEntitiesSync));
				BackgroundChanged |= ToggleCurrentMapBackground(s_BackgroundEntitiesInput);
				SyncBackgroundEntitiesInput(s_BackgroundEntitiesInput, s_aBackgroundEntitiesSync, sizeof(s_aBackgroundEntitiesSync));
			}

			if(BackgroundChanged)
				GameClient()->m_Background.LoadBackground();

			SplitDDNetRow(Background, &Button);
			if(DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClBackgroundShowTilesLayers, "Show tiles layers from BG map", Localize("Show tiles layers from BG map"), g_Config.m_ClBackgroundShowTilesLayers, &Button))
				g_Config.m_ClBackgroundShowTilesLayers ^= 1;
		});

		float MiscellaneousMinContentHeight = DDNetRowPitch * 3.0f + 5.0f + 2.0f;
#if defined(CONF_FAMILY_WINDOWS)
		MiscellaneousMinContentHeight += DDNetRowPitch + 10.0f;
#endif
		const float MiscellaneousMinCardHeight = CardChromeHeight + MiscellaneousMinContentHeight;
		AddCard(MiscellaneousSpec, MiscellaneousMinCardHeight, CardChromeHeight, [=, this](CUIRect ContentRect) mutable {
			CUIRect Miscellaneous = ContentRect;
			// miscellaneous
			static CButtonContainer s_ButtonTimeout;
			SplitDDNetRow(Miscellaneous, &Button);
			if(DoSettingsButton_Menu(SETTINGS_DDNET, -1, -1, &s_ButtonTimeout, "ddnet-new-random-timeout-code", Localize("New random timeout code"), 0, &Button))
			{
				Client()->GenerateTimeoutSeed();
			}

			Miscellaneous.HSplitTop(5.0f, nullptr, &Miscellaneous);
			SplitDDNetRow(Miscellaneous, &Label);
			Miscellaneous.HSplitTop(2.0f, nullptr, &Miscellaneous);
			CUIElement &RunOnJoinLabelElement = SettingsTextElement(SETTINGS_DDNET, -1, "ddnet-run-on-join-label");
			DoSettingsLabelStreamed(RunOnJoinLabelElement, &Label, Localize("Run on join"), BodySize, TEXTALIGN_ML);
			SplitDDNetRow(Miscellaneous, &Button);
			static CLineInput s_RunOnJoinInput(g_Config.m_ClRunOnJoin, sizeof(g_Config.m_ClRunOnJoin));
			s_RunOnJoinInput.SetEmptyText(Localize("Chat command (e.g. showall 1)"));
			IUiContext DDNetRunOnJoinTextInputCtx;
			DDNetRunOnJoinTextInputCtx.m_pUi = Ui();
			DDNetRunOnJoinTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_ddnet_run_on_join_text_input");
			ui_widget::InputField(DDNetRunOnJoinTextInputCtx, &s_RunOnJoinInput, Button, nullptr, BodySize);

#if defined(CONF_FAMILY_WINDOWS)
			static CButtonContainer s_ButtonUnregisterShell;
			Miscellaneous.HSplitTop(10.0f, nullptr, &Miscellaneous);
			SplitDDNetRow(Miscellaneous, &Button);
			if(DoSettingsButton_Menu(SETTINGS_DDNET, -1, -1, &s_ButtonUnregisterShell, "ddnet-unregister-protocol-file-extensions", Localize("Unregister protocol and file extensions"), 0, &Button))
			{
				Client()->ShellUnregister();
			}
#endif
		});
		LogPerfStage(Client(), "ddnet_controls_section", ControlsSectionTimer.ElapsedMs(), false, "page=ddnet section=background_misc");
	};
	uint64_t DDNetLayoutRevision = static_cast<uint64_t>(RenderOnly ? 1 : 0);
	DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClReplays != 0);
	DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClRaceGhost != 0);
	DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClRaceSaveGhost != 0);
	DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClTextEntities != 0);
	DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClAntiPing != 0);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, DDNetLayoutRevision);

	const SQmScrollRequest ScrollRequest{EQmScrollProfile::SETTINGS_OUTER};
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest, UiScale, 0.0f);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	CQmScrollState &ScrollState = s_DDNetSettingsCardScrollRegion.State();
	(void)ScrollState;
	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = Ui()->MouseX();
	InputState.m_MouseY = Ui()->MouseY();
	InputState.m_MousePressed = Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = Ui()->MouseButton(0);
	InputState.m_MouseReleased = !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = Input()->ModifierIsPressed();
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = RenderOnly ? nullptr : &ScrollParams;
	const SSettingsCardDeckResult DeckResult = SettingsCardDeckForRenderPass().RenderCached(DDNetCardCtx, DDNetPage, "ddnet", DefinitionsRevision, BuildDefinitions, SettingsCardOrderModelForRenderPass(), RenderOnly ? nullptr : &s_DDNetSettingsCardScrollRegion, InputState, SettingsCardMotionSpec(), DDNetVisualOptions);
	if(!RenderOnly && DeckResult.m_OrderChanged)
		SaveSettingsCardOrderModel();
}

CUi::EPopupMenuFunctionResult CMenus::PopupSkinQueuePresetRename(void *pContext, CUIRect View, bool Active)
{
	CSkinQueuePresetRenamePopupContext *pPopupContext = static_cast<CSkinQueuePresetRenamePopupContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;
	if(pMenus == nullptr)
		return CUi::POPUP_CLOSE_CURRENT;
	if(pPopupContext->m_Dummy < 0 || pPopupContext->m_Dummy > 1)
		return CUi::POPUP_CLOSE_CURRENT;

	const auto &vPresets = pMenus->GameClient()->m_Skins.SkinQueuePresets(pPopupContext->m_Dummy);
	if(pPopupContext->m_PresetIndex < 0 || pPopupContext->m_PresetIndex >= (int)vPresets.size())
		return CUi::POPUP_CLOSE_CURRENT;

	const float FontSize = 10.0f;
	View.Margin(5.0f, &View);

	CUIRect Label, Input, Buttons, Cancel, Confirm;
	View.HSplitTop(12.0f, &Label, &View);
	pMenus->DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee-skin-queue-new-preset-name", &Label, Localize("New preset name"), FontSize, TEXTALIGN_ML);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Input, &View);
	IUiContext SkinQueuePresetRenameTextInputCtx;
	SkinQueuePresetRenameTextInputCtx.m_pUi = pMenus->Ui();
	SkinQueuePresetRenameTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_skin_queue_preset_rename_text_input");
	ui_widget::InputField(SkinQueuePresetRenameTextInputCtx, &pPopupContext->m_NameInput, Input, nullptr, FontSize + 1.0f);

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Buttons, &View);
	Buttons.VSplitMid(&Cancel, &Confirm, 3.0f);

	const bool CancelPressed = pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_CancelButton, Localize("Cancel"), &Cancel, FontSize, TEXTALIGN_MC) || (Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE));
	if(CancelPressed)
		return CUi::POPUP_CLOSE_CURRENT;

	const bool ConfirmPressed = pMenus->Ui()->DoButton_PopupMenu(&pPopupContext->m_ConfirmButton, Localize("Rename"), &Confirm, FontSize, TEXTALIGN_MC) || (Active && pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
	if(ConfirmPressed)
	{
		if(pMenus->GameClient()->m_Skins.RenameSkinQueuePreset((size_t)pPopupContext->m_PresetIndex, pPopupContext->m_NameInput.GetString(), pPopupContext->m_Dummy))
			return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CMenus::PopupMapPicker(void *pContext, CUIRect View, bool Active)
{
	CPopupMapPickerContext *pPopupContext = static_cast<CPopupMapPickerContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;

	static CListBox s_ListBox;
	s_ListBox.SetActive(Active);
	s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);
	s_ListBox.SetScrollProfile(EQmScrollProfile::POPUP_LIST);
	s_ListBox.DoStart(20.0f, pPopupContext->m_vMaps.size(), 1, 3, -1, &View, false);

	int MapIndex = 0;
	for(auto &Map : pPopupContext->m_vMaps)
	{
		const int ItemIndex = MapIndex++;
		const CListboxItem Item = s_ListBox.DoNextItem(&Map, ItemIndex == pPopupContext->m_Selection);
		if(!Item.m_Visible)
			continue;

		CUIRect Label, Icon;
		Item.m_Rect.VSplitLeft(20.0f, &Icon, &Label);

		char aLabelText[IO_MAX_PATH_LENGTH];
		if(Map.m_aValuePrefix[0] != '\0')
			str_format(aLabelText, sizeof(aLabelText), "%s/%s", Map.m_aValuePrefix, Map.m_aFilename);
		else
			str_copy(aLabelText, Map.m_aFilename);
		if(Map.m_IsDirectory)
			str_append(aLabelText, "/", sizeof(aLabelText));

		EQmIcon IconType;
		const char *pIconType;
		if(!Map.m_IsDirectory)
		{
			IconType = EQmIcon::MAP;
			pIconType = FONT_ICON_MAP;
		}
		else
		{
			if(!str_comp(Map.m_aFilename, ".."))
			{
				IconType = EQmIcon::FOLDER_TREE;
				pIconType = FONT_ICON_FOLDER_TREE;
			}
			else
			{
				IconType = EQmIcon::FOLDER;
				pIconType = FONT_ICON_FOLDER;
			}
		}

		pMenus->Ui()->DoLabel_QmIcon(&Icon, IconType, pIconType, 12.0f, TEXTALIGN_ML);

		pMenus->Ui()->DoLabel(&Label, aLabelText, 10.0f, TEXTALIGN_ML);
	}

	const int NewSelected = s_ListBox.DoEnd();
	pPopupContext->m_Selection = NewSelected >= 0 && NewSelected < (int)pPopupContext->m_vMaps.size() ? NewSelected : -1;
	if((s_ListBox.WasItemSelected() || s_ListBox.WasItemActivated()) && pPopupContext->m_Selection >= 0)
	{
		const CMapListItem &SelectedItem = pPopupContext->m_vMaps[pPopupContext->m_Selection];

		if(SelectedItem.m_IsDirectory)
		{
			if(!str_comp(SelectedItem.m_aFilename, ".."))
			{
				dbg_assert(fs_parent_dir(pPopupContext->m_aCurrentMapFolder) == 0, "Parent folder item selected but there is no parent folder");
			}
			else
			{
				str_append(pPopupContext->m_aCurrentMapFolder, "/", sizeof(pPopupContext->m_aCurrentMapFolder));
				str_append(pPopupContext->m_aCurrentMapFolder, SelectedItem.m_aFilename, sizeof(pPopupContext->m_aCurrentMapFolder));
			}
			pPopupContext->MapListPopulate();
		}
		else
		{
			char aSelectedValue[IO_MAX_PATH_LENGTH];
			char aRelativeValue[IO_MAX_PATH_LENGTH];
			if(pPopupContext->m_aCurrentMapFolder[0] != '\0')
				str_format(aRelativeValue, sizeof(aRelativeValue), "%s/%s", pPopupContext->m_aCurrentMapFolder, SelectedItem.m_aFilename);
			else
				str_copy(aRelativeValue, SelectedItem.m_aFilename);
			const char *pValuePrefix = SelectedItem.m_aValuePrefix[0] != '\0' ? SelectedItem.m_aValuePrefix : pPopupContext->m_aValuePrefix;
			if(pValuePrefix[0] != '\0')
				str_format(aSelectedValue, sizeof(aSelectedValue), "%s/%s", pValuePrefix, aRelativeValue);
			else
				str_copy(aSelectedValue, aRelativeValue);

			char *pTargetConfig = pPopupContext->m_pTargetConfig != nullptr ? pPopupContext->m_pTargetConfig : g_Config.m_ClBackgroundEntities;
			const int TargetConfigSize = pPopupContext->m_TargetConfigSize > 0 ? pPopupContext->m_TargetConfigSize : (int)sizeof(g_Config.m_ClBackgroundEntities);
			BuildBackgroundEntitiesValueFromInput(aSelectedValue, pTargetConfig, TargetConfigSize);
			pMenus->Ui()->SetActiveItem(nullptr);
			pMenus->GameClient()->m_Background.LoadBackground();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::CPopupMapPickerContext::MapListPopulate()
{
	m_vMaps.clear();
	const auto ListRoot = [&](const char *pRootPath, const char *pValuePrefix) {
		if(pRootPath == nullptr || pRootPath[0] == '\0')
			return;
		str_copy(m_aListingValuePrefix, pValuePrefix != nullptr ? pValuePrefix : "", sizeof(m_aListingValuePrefix));
		char aTemp[IO_MAX_PATH_LENGTH];
		if(m_aCurrentMapFolder[0] != '\0')
			str_format(aTemp, sizeof(aTemp), "%s/%s", pRootPath, m_aCurrentMapFolder);
		else
			str_copy(aTemp, pRootPath);
		m_pMenus->Storage()->ListDirectoryInfo(IStorage::TYPE_ALL, aTemp, MapListFetchCallback, this);
	};

	ListRoot(m_aRootPath[0] != '\0' ? m_aRootPath : "maps", m_aValuePrefix);
	m_aListingValuePrefix[0] = '\0';
	std::stable_sort(m_vMaps.begin(), m_vMaps.end(), CompareFilenameAscending);
	m_Selection = -1;
}

int CMenus::CPopupMapPickerContext::MapListFetchCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	(void)StorageType;
	CPopupMapPickerContext *pRealUser = (CPopupMapPickerContext *)pUser;
	const bool IsBackgroundFile = FindBackgroundFileExtension(pInfo->m_pName) != nullptr;
	if((!IsDir && !IsBackgroundFile) || !str_comp(pInfo->m_pName, ".") || (!str_comp(pInfo->m_pName, "..") && (!str_comp(pRealUser->m_aCurrentMapFolder, ""))))
		return 0;
	for(const CMapListItem &ExistingItem : pRealUser->m_vMaps)
	{
		if(ExistingItem.m_IsDirectory == (bool)IsDir && str_comp(ExistingItem.m_aValuePrefix, pRealUser->m_aListingValuePrefix) == 0 && str_comp(ExistingItem.m_aFilename, pInfo->m_pName) == 0)
			return 0;
	}

	CMapListItem Item;
	str_copy(Item.m_aFilename, pInfo->m_pName);
	str_copy(Item.m_aValuePrefix, pRealUser->m_aListingValuePrefix, sizeof(Item.m_aValuePrefix));
	Item.m_IsDirectory = IsDir;

	pRealUser->m_vMaps.emplace_back(Item);

	return 0;
}
