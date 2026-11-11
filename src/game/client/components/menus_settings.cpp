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
#include <game/client/QmUi/QmListEntryAnimation.h>
#include <game/client/QmUi/QmLocalizedLabels.h>
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
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace std::chrono_literals;

namespace
{

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

namespace
{

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

	// 语言页列表的行与容器几何：预热缓存（PrepareLanguagePageCache）与逐帧绘制
	// 共用这一组事实，避免「测量一套、绘制另一套」让文字容器的尺寸键每帧都对不上。
	// LabelWidth 只依赖列表宽度与字号、不依赖容器高度——预热缓存只拿得到宽度，
	// 缓存键必须能只由宽度复现，否则缓存会永久失效、每帧重建文字容器。
	struct SLanguageListRowLayout
	{
		float m_RowHeight = 0.0f;
		float m_RowSpacing = 0.0f;
		float m_RowInsetX = 0.0f;
		float m_FlagWidth = 0.0f;
		float m_ScrollbarWidth = 0.0f;
	};

	SLanguageListRowLayout ResolveLanguageListRowLayout(const SSettingsContentMetrics &Metrics)
	{
		SLanguageListRowLayout Layout;
		Layout.m_RowHeight = Metrics.m_ListRowHeight;
		// 行距只取列表 token 的半档：与 CListBox 胶囊行（图形页分辨率列表、
		// Tee 队列/预设）同一数值，行与行之间只留一条读得出的缝。
		Layout.m_RowSpacing = ui_token::list::ROW_SPACING;
		// 行内左右内退与弹层条目同源（max(8, 字号 × 0.75)），文字不再贴着胶囊边缘。
		Layout.m_RowInsetX = ui_token::list::RowInsetX(Metrics.m_BodySize);
		// 国旗仍占两倍行高的宽度（现有比例），文字紧随其后。
		Layout.m_FlagWidth = Layout.m_RowHeight * 2.0f;
		SQmScrollRequest ScrollRequest;
		ScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;
		Layout.m_ScrollbarWidth = QmResolveScrollPolicy(ScrollRequest, Metrics.m_UiScale).m_Style.m_ScrollbarWidth;
		return Layout;
	}

	// 行矩形：整行包含胶囊背景、命中区和行距之间的行宽，滚动条列始终预留，
	// 滚动条出现/消失时文字宽度不横跳，缓存键也随之稳定。
	CUIRect LanguageListRowRect(const CUIRect &ListRect, const SLanguageListRowLayout &Layout)
	{
		CUIRect RowRect = ListRect;
		RowRect.w = maximum(0.0f, RowRect.w - Layout.m_ScrollbarWidth);
		return RowRect;
	}

	// 行内容区：整行按行内退左右各让一段，国旗与文字整体右移（相对位置不变）。
	CUIRect LanguageListRowContentRect(const CUIRect &RowRect, const SLanguageListRowLayout &Layout)
	{
		CUIRect Content = RowRect;
		Content.x += Layout.m_RowInsetX;
		Content.w = maximum(0.0f, Content.w - 2.0f * Layout.m_RowInsetX);
		return Content;
	}

	float LanguageListLabelWidth(const CUIRect &ListRect, const SSettingsContentMetrics &Metrics)
	{
		const SLanguageListRowLayout Layout = ResolveLanguageListRowLayout(Metrics);
		const CUIRect Content = LanguageListRowContentRect(LanguageListRowRect(ListRect, Layout), Layout);
		return maximum(0.0f, Content.w - Layout.m_FlagWidth);
	}

	// 可见行数 = 容器可用高度能完整容纳的行数（首行之前、末行之后都不留行距）。
	// 容器高度再严格按「可见行数 × 行高 + (可见行数 - 1) × 行距」反推，首/末行
	// 胶囊因此不会被容器圆角或卡片裁剪切掉一角。
	int ResolveLanguageListVisibleRows(const float AvailableHeight, const int ItemCount, const SLanguageListRowLayout &Layout)
	{
		if(ItemCount <= 0 || AvailableHeight <= 0.0f || Layout.m_RowHeight <= 0.0f)
			return 0;
		const float RowPitch = Layout.m_RowHeight + Layout.m_RowSpacing;
		const int FitRows = (int)std::floor((AvailableHeight + Layout.m_RowSpacing) / RowPitch);
		return std::clamp(FitRows, 1, ItemCount);
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
	const SSettingsPageLayoutFrame GeneralPage = SettingsPageLayout(MainView, UiScale);
	const IUiContext GeneralCardCtx = SettingsUiContext("settings_general", UiScale);
	const SSettingsCardDeckVisualOptions GeneralVisualOptions = SettingsCardDeckVisualOptions();
	static CScrollRegion s_GeneralSettingsScrollRegion;
	const bool RenderOnly = Ui()->RenderOnly();
	qm_card_catalog::SQmCardBuildContext CardBuild;
	CardBuild.m_pMenus = this;
	CardBuild.m_ReadOnly = RenderOnly;
	CardBuild.m_Page = GeneralPage;
	CardBuild.m_Metrics = GeneralMetrics;
	CardBuild.m_UiContext = GeneralCardCtx;
	const auto BuildDefinitions = [CardBuild](std::vector<SSettingsCardDefinition> &vCards) {
		qm_card_catalog::BuildCards(CardBuild, qm_card_catalog::GeneralCardStableIds(), vCards);
	};
	const uint64_t GeneralToggleMask = qm_card_catalog::GeneralMeasureContentRevision();
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
	InputState.m_pScrollParams = Ui()->MeasurementOnly() ? nullptr : &ScrollParams;
	const SSettingsCardDeckResult DeckResult = SettingsCardDeckForRenderPass().RenderCached(GeneralCardCtx, GeneralPage, "general", DefinitionsRevision, BuildDefinitions, SettingsCardOrderModelForRenderPass(), Ui()->MeasurementOnly() ? nullptr : &s_GeneralSettingsScrollRegion, InputState, SettingsCardMotionSpec(), GeneralVisualOptions);
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
	// 选中/悬停填充由 CListBox 统一绘制；这里只补选中描边，避免同一行画两遍。
	s_ListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_HOVER);

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
	// 国码文字跟随设置页小字，不再写死 10（窄视口小字会缩到 9，写死就比别处大一号）。
	const float CountryLabelSize = ResolveSettingsContentMetrics(GridArea.w).m_SmallSize;

	for(size_t i = 0; i < s_vFiltered.size(); ++i)
	{
		const SFilteredFlag &Filtered = s_vFiltered[i];
		const CCountryFlags::CCountryFlag *pEntry = Filtered.m_pEntry;
		const bool IsSelected = pEntry->m_CountryCode == pPopupContext->m_Selection;
		const CListboxItem Item = s_ListBox.DoNextItem(pEntry, IsSelected);
		if(!Item.m_Visible)
			continue;

		const float SelectedAlpha = s_ListBox.SelectionFeedbackAlpha((int)i);
		if(SelectedAlpha > 0.001f)
		{
			// 选中描边与资产卡/列表选中态同源（白 0.7），不用 BORDER_FOCUS 强调蓝；
			// 填充交给 CListBox 的选中轨道，这里不再叠第二层。
			DrawRoundedSurface(pUi, Item.m_Rect, ColorRGBA(), ui_token::color::NEUTRAL_WHITE.WithAlpha(ui_token::feedback::ICON_BORDER_ALPHA * SelectedAlpha), ui_token::radius::BASE, 1.0f);
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
		pUi->DoLabel(&Label, pEntry->m_aCountryCodeString, CountryLabelSize, TEXTALIGN_MC, Props);
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
	if(Ui()->DoButtonLogic(&s_FlagButton, 0, &FlagButton, BUTTONFLAG_LEFT))
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
	const auto PlayerMetrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = PlayerMetrics.m_UiScale;
	CUIRect TabBar, PlayerTab, DummyTab, ChangeInfo;
	const SSettingsSubTabLayoutFrame PlayerSubTabs = ResolveSettingsSubTabLayout(MainView, UiScale);
	TabBar = PlayerSubTabs.m_TabBarRect;
	MainView = PlayerSubTabs.m_ContentRect;
	TabBar.VSplitMid(&TabBar, &ChangeInfo, 20.0f);
	TabBar.VSplitMid(&PlayerTab, &DummyTab);
	static CButtonContainer s_PlayerTabButton;
	static CButtonContainer s_DummyTabButton;
	const CUIRect aPlayerTabSlots[] = {PlayerTab, DummyTab};
	const IUiContext TabCtx = TabBarUiContext();
	const int Pressed = ui_widget::CapsuleTabBarPressedIndex(TabCtx, aPlayerTabSlots, std::size(aPlayerTabSlots));
	m_Dummy = ui_widget::CapsuleTabBar(TabCtx, MakeUiScopeHash("settings_player_dummy_tabs_capsule"), aPlayerTabSlots, std::size(aPlayerTabSlots), m_Dummy ? 1 : 0, SettingsCapsuleTabBarStyle()) == 1;
	if(Pressed >= 0)
	{
		m_TeeEntranceStartTime = time_get();
	}
	DoButton_MenuTab(&s_PlayerTabButton, Localize("Player"), !m_Dummy, &PlayerTab, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true);
	DoButton_MenuTab(&s_DummyTabButton, Localize("Dummy"), m_Dummy, &DummyTab, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true);
	RenderSettingsCatalogPage(MainView, "player");
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
	s_SubTab = ui_widget::CapsuleTabBar(TabBarUiContext(), MakeUiScopeHash("settings_tee_sub_tabs_capsule"), aTabs, std::size(aTabs), s_SubTab, SettingsCapsuleTabBarStyle());
	for(int Tab = 0; Tab < 2; ++Tab)
	{
		DoButton_MenuTab(&s_aTabs[Tab], apLabels[Tab], s_SubTab == Tab, &aTabs[Tab], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true);
	}
	if(!m_MenuTextPlanCollecting && !Ui()->MeasurementOnly())
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
	InputState.m_pScrollParams = Ui()->MeasurementOnly() ? nullptr : &ScrollParams;
	const SSettingsCardDeckResult Result = SettingsCardDeckForRenderPass().RenderCached(CardCtx, Page, "tee", DefinitionsRevision, BuildDefinitions, SettingsCardOrderModelForRenderPass(), Ui()->MeasurementOnly() ? nullptr : &s_ScrollRegion, InputState, SettingsCardMotionSpec(), VisualOptions);
	if(!RenderOnly && Result.m_OrderChanged)
		SaveSettingsCardOrderModel();
	LogSettingsSectionPerf(Client(), SETTINGS_TEE, -1, "tee_page", RenderTimer.ElapsedMs(), "static_text", TextStats.Stats().m_New, TextStats.Stats().m_Reused);
	LogPerfStage(Client(), "tee_page_total", RenderTimer.ElapsedMs(), false, "page=tee");
}

void CMenus::RenderSettingsGraphics(CUIRect MainView)
{
	RenderSettingsCatalogPage(MainView, "graphics");
}

void CMenus::RenderSettingsSound(CUIRect MainView)
{
	if(m_AudioPackEditorState.m_Open)
	{
		RenderAudioPackEditorScreen(MainView);
		return;
	}
	RenderSettingsCatalogPage(MainView, "sound");
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
	const SLanguageListRowLayout RowLayout = ResolveLanguageListRowLayout(Metrics);

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

	CUIRect Content = LanguageListRowRect(List, RowLayout);
	for(size_t i = 0; i < g_Localization.Languages().size(); ++i)
	{
		const auto &Language = g_Localization.Languages()[i];
		CUIRect ItemRect;
		Content.HSplitTop(RowLayout.m_RowHeight, &ItemRect, &Content);
		if(i + 1 < g_Localization.Languages().size())
			Content.HSplitTop(RowLayout.m_RowSpacing, nullptr, &Content);

		CUIRect Label;
		CUIRect RowContent = LanguageListRowContentRect(ItemRect, RowLayout);
		RowContent.VSplitLeft(RowLayout.m_FlagWidth, nullptr, &Label);
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
	const SLanguageListRowLayout RowLayout = ResolveLanguageListRowLayout(Metrics);
	const int NumLanguages = (int)g_Localization.Languages().size();
	const int VisibleRows = ResolveLanguageListVisibleRows(MainView.h, NumLanguages, RowLayout);
	// 容器高度只按完整的行算：首/末行胶囊不会被容器圆角或卡片裁剪切掉一角。
	// 仅在父矩形连一行都放不下（窗口极窄）时才退回父矩形高度，避免画到卡片外。
	const float ContainerHeight = minimum(ui_token::list::RowsHeight(VisibleRows, RowLayout.m_RowHeight, RowLayout.m_RowSpacing), maximum(0.0f, MainView.h));
	const float ContainerRadius = ui_token::list::ContainerRadius(MainView.w, ContainerHeight);
	// 首/末行胶囊按「容器圆角 − 行胶囊圆角」横向内缩：胶囊直边与容器圆角切点对齐，
	// 端头不会从容器圆角外侧探出来（与 CListBox 胶囊行同一条公式）。
	const float CapsuleInsetX = maximum(0.0f, ContainerRadius - ui_token::list::RowRadius(RowLayout.m_RowHeight));
	if(VisibleRows > 0)
	{
		CUIRect Container = MainView;
		Container.h = ContainerHeight;
		DrawRoundedSurface(Ui(), Container, Ui()->ScaleBackgroundAlpha(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f)), ColorRGBA(), ContainerRadius);
	}
	CUIRect ScrollView = MainView;
	ScrollView.h = minimum(ScrollView.h, ContainerHeight);
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;
	ScrollRequest.m_RowExtent = RowLayout.m_RowHeight + RowLayout.m_RowSpacing;
	ScrollRequest.m_RowsPerStep = 3;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	ScrollParams.m_WheelOwnerPriority = EUiWheelOwnerPriority::COMPOSITE_CONTROL;
	// 轨道两端按容器圆角内缩：细轨端头不会从容器圆角外侧探出来。
	ScrollParams.m_ScrollbarTrackInset = ContainerRadius;
	SSettingsScrollRegionFrame ScrollFrame = BeginSettingsScrollRegion(gs_LanguageScrollRegion, &ScrollView, ScrollParams, s_PrevLanguageScrollY);
	ScrollOffset = ScrollFrame.m_BeginOffset;

	// 行宽按「始终预留滚动条列」的固定宽度算，与预热缓存的宽度键同源；滚动区 Begin
	// 会把传进去的容器矩形收窄到实际内容列（宽度随滚动条显隐变化），这里不再重复扣减，
	// 否则滚动条出现时行宽会被扣两次。
	CUIRect Content = LanguageListRowRect(MainView, RowLayout);
	Content.y += ScrollOffset.y;
	int VisibleLanguages = 0;
	int CacheHits = 0;
	int CacheMisses = 0;
	// 依次淡入 + 上浮：与服务器列表/浏览器过滤列表同一套入场语言（onetake
	// wordRise：opacity 领先位移）。语言卡片离开渲染超过间隙后重放。
	static CQmListEntryAnimation s_EntryAnimation;
	static CQmListRowFeedback s_SelectedFeedback;
	static CQmListRowFeedback s_HoverFeedback;
	static bool s_ScrollFeedbackSnap = false;
	// 设置分类切换时显式重放：间隙启发在该页同样不可靠（后台趟持续渲染），
	// 与服务器列表同款序号制，保证每次切回设置页都重放。
	static int s_EntryRestartSeq = -1;
	if(s_EntryRestartSeq != m_SettingsListEntryRestartSeq)
	{
		s_EntryAnimation.Reset();
		s_SelectedFeedback.Reset();
		s_HoverFeedback.Reset();
		s_ScrollFeedbackSnap = false;
		s_EntryRestartSeq = m_SettingsListEntryRestartSeq;
	}
	const int64_t EntryNow = time_get();
	const int64_t EntryFreq = maximum<int64_t>(1, time_freq());
	const bool EntryEnabled = g_Config.m_QmUiListEntryAnimations != 0 && g_Config.m_QmUiMotionLevel > 0;
	const float EntryElapsed = s_EntryAnimation.Resolve(EntryNow, EntryFreq, EntryEnabled, g_Config.m_QmUiMotionLevel,
		Ui()->MeasurementOnly() ? EQmListEntryRenderPass::MEASUREMENT : EQmListEntryRenderPass::INTERACTIVE);
	const bool ScrollAnimating = gs_LanguageScrollRegion.Animating();
	const bool SnapFeedback = g_Config.m_QmUiMotionLevel == 0 || ScrollAnimating || s_ScrollFeedbackSnap;
	s_ScrollFeedbackSnap = ScrollAnimating;
	const bool UpdateFeedback = !Ui()->AnimationsBlocked();
	const uint64_t FeedbackKey = BuildUiAnimNodeKey(MakeUiScopeHash("settings_language_row_feedback"), 0);
	s_SelectedFeedback.BeginFrame(Ui()->QmAnimationRuntime(), FeedbackKey, SnapFeedback, UpdateFeedback);
	s_HoverFeedback.BeginFrame(Ui()->QmAnimationRuntime(), FeedbackKey ^ 0x9E3779B97F4A7C15ULL, SnapFeedback, UpdateFeedback);
	int EntryVisibleRowIndex = 0;
	for(size_t i = 0; i < g_Localization.Languages().size(); ++i)
	{
		const auto &Language = g_Localization.Languages()[i];
		CUIRect ItemRect;
		Content.HSplitTop(RowLayout.m_RowHeight, &ItemRect, &Content);
		if(i + 1 < g_Localization.Languages().size())
			Content.HSplitTop(RowLayout.m_RowSpacing, nullptr, &Content);
		const bool Selected = s_SelectedLanguage == (int)i;
		const bool Visible = gs_LanguageScrollRegion.AddRect(ItemRect, gs_LanguageScrollToSelected && Selected, Selected);
		if(!Visible)
			continue;
		++VisibleLanguages;

		const SQmListEntryVisual EntryVisual = QmListEntryVisual(EntryElapsed, EntryVisibleRowIndex, g_Config.m_QmUiMotionLevel);
		++EntryVisibleRowIndex;
		std::optional<CUiScopedControlAlphaScale> EntryFade;
		if(EntryVisual.m_Fade < 0.999f)
			EntryFade.emplace(Ui(), EntryVisual.m_Fade);
		ItemRect.y += EntryVisual.m_OffsetY;

		void *pRowId = UseCache ? static_cast<void *>(&gs_aLanguageRowIds[i]) : const_cast<char *>(Language.m_Filename.c_str());
		Ui()->DoConfigTooltip(pRowId, &ItemRect, g_Config.m_ClLanguagefile);
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
		const int ButtonResult = Ui()->DoButtonLogic(pRowId, 0, &ItemRect, BUTTONFLAG_LEFT);
		if(ButtonResult)
		{
			s_SelectedLanguage = i;
			Activated = true;
		}

		if(Selected)
			s_SelectedFeedback.Observe((int)i);
		const float SelectedAlpha = s_SelectedFeedback.Alpha((int)i);
		if(Ui()->HotItem() == pRowId)
			s_HoverFeedback.Observe((int)i);
		const float HoverAlpha = s_HoverFeedback.Alpha((int)i);
		// 整行胶囊：圆角取自行高，宽高与绘制都走公共 DrawRoundedSurface；
		// 悬浮先画、选中后叠，选中行被鼠标压住时白色细描边仍在最上层。
		CUIRect CapsuleRect = ItemRect;
		CapsuleRect.x += CapsuleInsetX;
		CapsuleRect.w = maximum(0.0f, CapsuleRect.w - 2.0f * CapsuleInsetX);
		const float RowRadius = ui_token::list::RowRadius(RowLayout.m_RowHeight);
		if(HoverAlpha > 0.001f && !gs_LanguageScrollRegion.Animating())
			DrawRoundedSurface(Ui(), CapsuleRect, Ui()->ScaleBackgroundAlpha(ui_token::color::LIST_ITEM_HOVER).WithMultipliedAlpha(HoverAlpha), ColorRGBA(), RowRadius);
		if(SelectedAlpha > 0.001f)
			DrawRoundedSurface(Ui(), CapsuleRect, Ui()->ScaleBackgroundAlpha(ui_token::color::LIST_ITEM_SELECTED).WithMultipliedAlpha(SelectedAlpha), ui_token::feedback::ACTIVE_BORDER.WithMultipliedAlpha(SelectedAlpha), RowRadius, ui_token::feedback::ICON_BORDER_WIDTH);

		CUIRect FlagRect, Label;
		CUIRect RowContent = LanguageListRowContentRect(ItemRect, RowLayout);
		RowContent.VSplitLeft(RowLayout.m_FlagWidth, &FlagRect, &Label);
		FlagRect.VMargin(6.0f, &FlagRect);
		FlagRect.HMargin(3.0f, &FlagRect);
		// 国旗是裸纹理 quad（不经过 CUi 显隐系数），行淡入系数单独传入。
		GameClient()->m_CountryFlags.Render(Language.m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->ControlAlphaScale()), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);
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
	s_SelectedFeedback.EndFrame();
	s_HoverFeedback.EndFrame();
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
	// 手机端不呈现的页签（键鼠绑键、资源包等）也不能成为落地页：旧配置或搜索跳转会回落到通用页。
	if(SettingsPageHiddenForMobile(g_Config.m_UiSettingsPage, SettingsMobilePlatform()))
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
		const auto *pAnimation = Ui()->QmAnimationRuntime();
		m_QmSeasonalSidebar.Render(Ui(), Graphics(), TabBar, pAnimation != nullptr ? pAnimation->TimeSec() : 0.0);
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
		// 竖排页签（设置页左栏）保持原来的分块选中/悬停底色，只换胶囊端头形状，不加滑块。
		const ColorRGBA SettingsNavigationSelected = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.42f);
		const ColorRGBA SettingsNavigationHover = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiSelectedColor)).WithAlpha(0.20f);
		std::array<CUIRect, SETTINGS_LENGTH> aSlots;
		std::array<int, SETTINGS_LENGTH> aPages;
		// 手机端虚拟高度更小（UI 缩放更大），固定的页签高度会把右侧目录撑出面板。
		// 先算出可见页签需要的总高度，超出可用高度时按比例收缩行高与间距。
		int VisibleTabCount = 0;
		for(int i : s_aSettingsTabOrder)
		{
			if(SettingsPageVisibleInRightTabBar(i))
				++VisibleTabCount;
		}
		const float TabSlotNeeded = VisibleTabCount * (ui_token::settings::TAB_HEIGHT + ui_token::settings::TAB_GAP);
		const float TabSlotFit = (TabSlotNeeded > TabBar.h && TabSlotNeeded > 0.0f) ? TabBar.h / TabSlotNeeded : 1.0f;
		const float TabSlotHeight = ui_token::settings::TAB_HEIGHT * TabSlotFit;
		const float TabSlotGap = ui_token::settings::TAB_GAP * TabSlotFit;
		int Count = 0;
		for(int i : s_aSettingsTabOrder)
		{
			if(!SettingsPageVisibleInRightTabBar(i))
				continue;
			TabBar.HSplitTop(TabSlotGap, nullptr, &TabBar);
			TabBar.HSplitTop(TabSlotHeight, &aSlots[Count], &TabBar);
			aPages[Count++] = i;
		}
		const int Pressed = ui_widget::CapsuleTabBarPressedIndex(TabBarUiContext(), aSlots.data(), Count, 0.0f);
		if(Pressed >= 0)
			g_Config.m_UiSettingsPage = aPages[Pressed];
		for(int Index = 0; Index < Count; ++Index)
		{
			const int Page = aPages[Index];
			// 字号必须和预热缓存（menus.cpp 里用 TAB_FONT_SIZE 建缓存）同一个值：
			// 不传就走行高反推 17.6，与 13 不一致，缓存容器每帧被判脏、13 根本不生效。
			DoButton_MenuTab(&m_aSettingsTabButtons[Page], m_SettingsTabLabels.Get(Page), g_Config.m_UiSettingsPage == Page, &aSlots[Index], IGraphics::CORNER_ALL, &m_aAnimatorsSettingsTab[Page], nullptr, &SettingsNavigationSelected, &SettingsNavigationHover, ui_token::radius::PILL, nullptr, &m_aSettingsTabLabelElements[Page], ui_token::settings::TAB_FONT_SIZE);
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
	if(!CollectingMenuTextPlan && !Ui()->MeasurementOnly() && g_Config.m_UiSettingsPage != SETTINGS_TEE && m_SettingsCardDeckDisplayState.EnterView(SettingsDisplayViewKey))
	{
		m_SettingsCardDeck.BeginDisplayCycle(++m_SettingsCardDeckDisplayCycle, true);
		// 视图进入即请求自动聚焦：页号比较依赖 s_PrevSettingsPage 这个跨帧静态，
		// 菜单重新打开时它可能已经是「搜索」，那条分支就不会再置位，聚焦随之中断。
		if(g_Config.m_UiSettingsPage == SETTINGS_SEARCH)
			m_FocusSettingsSearchOnEntry = true;
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
			Ui()->CancelTouchInput();
			CommitSettingsTeeSkinEdits();
			// 分类切换序号：主题/语言列表据此重放入场动画。
			m_SettingsListEntryRestartSeq++;
			// 切入「搜索」页时请求自动聚焦搜索输入框（一次性标志，渲染侧消费）。
			if(g_Config.m_UiSettingsPage == SETTINGS_SEARCH)
				m_FocusSettingsSearchOnEntry = true;
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
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED0);
			RenderSettingsTClient(ContentView, CollectingMenuTextPlan);
			if(!CollectingMenuTextPlan)
				m_SettingsRuntimeMetadata.m_LastTClientTab = m_TClientSettingsTab;
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_QMCLIENT)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_RESERVED0);
			RenderSettingsQmClient(ContentView, CollectingMenuTextPlan);
			if(!CollectingMenuTextPlan)
				m_SettingsRuntimeMetadata.m_LastQmTab = m_QmClientSettingsTab;
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_SEARCH)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_RESERVED0);
			RenderSettingsGlobalSearch(ContentView, CollectingMenuTextPlan);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_CONTRIBUTORS)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_RESERVED1);
			RenderSettingsContributors(ContentView, CollectingMenuTextPlan);
		}
		else if(g_Config.m_UiSettingsPage == SETTINGS_PROFILES)
		{
			if(!CollectingMenuTextPlan)
				GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_RESERVED1);
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
	const auto AppearanceMetrics = ResolveSettingsContentMetrics(MainView.w);
	const float AppearanceUiScale = AppearanceMetrics.m_UiScale;
	CUIRect TabBar, LeftView, RightView, Button;

	const SSettingsSubTabLayoutFrame AppearanceSubTabs = ResolveSettingsSubTabLayout(MainView, AppearanceUiScale);
	TabBar = AppearanceSubTabs.m_TabBarRect;
	MainView = AppearanceSubTabs.m_ContentRect;
	const float TabWidth = TabBar.w / (float)NUMBER_OF_APPEARANCE_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_APPEARANCE_TABS] = {};
	static CQmLocalizedLabelCache s_AppearanceTabNames;
	s_AppearanceTabNames.Refresh(g_Config.m_ClLanguagefile,
		{
			Localizable("HUD"),
			Localizable("Chat"),
			Localizable("Name Plate"),
			Localizable("Hook Collisions"),
			Localizable("Info Messages"),
			Localizable("Laser"),
		});

	CUIRect aAppearanceTabSlots[NUMBER_OF_APPEARANCE_TABS];
	CUIRect AppearanceTabsRemainder = TabBar;
	for(int Tab = APPEARANCE_TAB_HUD; Tab < NUMBER_OF_APPEARANCE_TABS; ++Tab)
		AppearanceTabsRemainder.VSplitLeft(TabWidth, &aAppearanceTabSlots[Tab], &AppearanceTabsRemainder);
	const int ActiveAppearanceTab = std::clamp(m_AppearanceSettingsTab, (int)APPEARANCE_TAB_HUD, (int)NUMBER_OF_APPEARANCE_TABS - 1);
	const IUiContext AppearanceTabBarCtx = TabBarUiContext();
	m_AppearanceSettingsTab = ui_widget::CapsuleTabBar(AppearanceTabBarCtx, MakeUiScopeHash("settings_appearance_tabs_capsule"), aAppearanceTabSlots, NUMBER_OF_APPEARANCE_TABS, ActiveAppearanceTab, SettingsCapsuleTabBarStyle());

	for(int Tab = APPEARANCE_TAB_HUD; Tab < NUMBER_OF_APPEARANCE_TABS; ++Tab)
	{
		DoButton_MenuTab(&s_aPageTabs[Tab], s_AppearanceTabNames.Get(Tab), m_AppearanceSettingsTab == Tab, &aAppearanceTabSlots[Tab], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true);
	}

	static const char *const s_apTabs[] = {"appearance-hud", "appearance-chat", "appearance-name-plate", "appearance-hook-collision", "appearance-info-messages", "appearance-laser"};
	RenderSettingsCatalogPage(MainView, s_apTabs[std::clamp(m_AppearanceSettingsTab, 0, NUMBER_OF_APPEARANCE_TABS - 1)]);
}

void CMenus::RenderSettingsDDNet(CUIRect MainView)
{
	RenderSettingsCatalogPage(MainView, "ddnet");
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

	// 弹层字号取设置页小字，输入框取正文，二者不再各自写死 10 / 11。
	const SSettingsContentMetrics PopupMetrics = ResolveSettingsContentMetrics(View.w);
	const float FontSize = PopupMetrics.m_SmallSize;
	View.Margin(5.0f, &View);

	CUIRect Label, Input, Buttons, Cancel, Confirm;
	View.HSplitTop(12.0f, &Label, &View);
	pMenus->DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, "tee-skin-queue-new-preset-name", &Label, Localize("New preset name"), FontSize, TEXTALIGN_ML);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Input, &View);
	IUiContext SkinQueuePresetRenameTextInputCtx;
	SkinQueuePresetRenameTextInputCtx.m_pUi = pMenus->Ui();
	SkinQueuePresetRenameTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_skin_queue_preset_rename_text_input");
	ui_widget::InputField(SkinQueuePresetRenameTextInputCtx, &pPopupContext->m_NameInput, Input, nullptr, PopupMetrics.m_BodySize);

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
	// 行内图标与文件名同一字号：图标盒传 IconBox(字号)，绘制出来字形才等于文字。
	const float MapRowTextSize = ResolveSettingsContentMetrics(View.w).m_SmallSize;
	const float MapRowIconBox = ui_token::control::IconBox(MapRowTextSize);

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

		pMenus->Ui()->DoLabel_QmIcon(&Icon, IconType, pIconType, MapRowIconBox, TEXTALIGN_ML);

		pMenus->Ui()->DoLabel(&Label, aLabelText, MapRowTextSize, TEXTALIGN_ML);
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
