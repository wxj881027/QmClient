#include "menus_qmclient_internal.h"

#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/SettingsCardCollapseState.h>
#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsSearchSession.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardMeasureRevision.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace qm_settings_menu;

namespace
{
	using SQmGlobalSearchCard = qm_card_registry::SCardSearchResult;

	struct SQmGlobalSearchResults
	{
		std::vector<SQmGlobalSearchCard> m_vAllVisibleCards;
	};

	void CollectGlobalSearchResults(const char *pSearch, bool Sixup, const qm_card_order::CModel &Model, SQmGlobalSearchResults &Out)
	{
		Out.m_vAllVisibleCards.clear();
		// 无搜索内容时结果为空：搜索页默认只显示搜索输入卡片，不展示全量卡片。
		if(pSearch == nullptr || pSearch[0] == '\0')
			return;
		std::vector<SQmGlobalSearchCard> vCards = qm_card_registry::SearchCards(pSearch, Model);
		Out.m_vAllVisibleCards.reserve(vCards.size());
		for(SQmGlobalSearchCard &Card : vCards)
		{
			const char *pTab = Card.m_Target.m_pTab;
			if(pTab != nullptr && str_comp(pTab, "global-search") == 0)
				continue;
			if(pTab != nullptr && ((Sixup && str_comp(pTab, "tee") == 0) || (!Sixup && str_comp(pTab, "tee7") == 0)))
				continue;
			Out.m_vAllVisibleCards.push_back(std::move(Card));
		}
	}

}

void CMenus::RenderSettingsGlobalSearch(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsGlobalSearchContent(MainView, PrewarmOnly);
}

void CMenus::RenderSettingsGlobalSearchContent(CUIRect MainView, bool PrewarmOnly)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const float UiScale = Metrics.m_UiScale;
	const float BodySize = Metrics.m_BodySize;
	const float SmallSize = Metrics.m_SmallSize;
	const float LineHeight = Metrics.m_LineHeight;
	const float LineSpacing = Metrics.m_LineSpacing;
	const SSettingsPageLayoutFrame Page = SettingsPageLayout(MainView, UiScale);
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	IUiContext SearchCtx = SettingsUiContext("settings_global_search", UiScale);
	if(ReadOnly)
	{
		SearchCtx.m_pAnim = nullptr;
		SearchCtx.m_pTree = nullptr;
	}
	static CScrollRegion s_GlobalSearchScrollRegion;
	static std::array<bool, qm_module::QmModuleCount> s_aGlobalSearchCollapsed = {};
	static std::array<CButtonContainer, qm_module::QmModuleCount> s_aGlobalSearchCollapseButtons;
	qm_card_collapse::SyncQmModules(s_aGlobalSearchCollapsed);
	auto ToggleSearchCollapsed = [](void *pUser, qm_module::EQmModuleId Id) {
		bool *pCollapsed = static_cast<bool *>(pUser);
		if(pCollapsed == nullptr)
			return;
		const int Index = std::clamp((int)Id, 0, (int)qm_module::QmModuleCount - 1);
		if(qm_card_collapse::SetQmModuleCollapsed(Id, !pCollapsed[Index]))
			pCollapsed[Index] = !pCollapsed[Index];
	};

	CLineInputBuffered<128> &ModuleSearchInput = m_GlobalCardSearchInput;
	const char *pModuleSearch = ModuleSearchInput.GetString();
	struct SGlobalSearchCache
	{
		bool m_Valid = false;
		std::string m_Search;
		std::string m_Language;
		bool m_Sixup = false;
		uint64_t m_TextGeneration = 0;
		uint64_t m_LayoutRevision = 0;
		SQmGlobalSearchResults m_Results;
		qm_card_order::CModel m_Model;
	};
	static SGlobalSearchCache s_GlobalSearchCache;
	static qm_card_order::CModel s_GlobalSearchPrewarmOrderModel;
	static CSettingsCardDeck s_GlobalSearchPrewarmDeck;
	static qm_card_catalog::SQmCardBuildContext s_GlobalSearchCardBuild;
	static qm_card_catalog::SQmFunctionCardLayoutState s_GlobalSearchFunctionCardLayout;
	static std::unordered_map<std::string, CButtonContainer> s_GlobalSearchActionButtons;
	// 分类布局只用于解析搜索和导航；以下两个模型仅属于搜索页，不持久化。
	const qm_card_order::CModel &CardOrderModel = SettingsCardOrderModel();
	qm_card_order::CModel &DeckOrderModel = Ui()->MeasurementOnly() ? s_GlobalSearchPrewarmOrderModel : s_GlobalSearchCache.m_Model;
	CSettingsCardDeck &CardDeck = Ui()->MeasurementOnly() ? s_GlobalSearchPrewarmDeck : m_SettingsCardDeck;
	const char *pLanguage = g_Config.m_ClLanguagefile;
	const uint64_t LayoutRevision = CardOrderModel.LayoutRevision();
	if(!s_GlobalSearchCache.m_Valid || s_GlobalSearchCache.m_Search != (pModuleSearch != nullptr ? pModuleSearch : "") || s_GlobalSearchCache.m_Language != (pLanguage != nullptr ? pLanguage : "") || s_GlobalSearchCache.m_Sixup != Client()->IsSixup() || s_GlobalSearchCache.m_LayoutRevision != LayoutRevision || s_GlobalSearchCache.m_TextGeneration != m_MenuTextPoolGeneration)
	{
		s_GlobalSearchCache.m_Search = pModuleSearch != nullptr ? pModuleSearch : "";
		s_GlobalSearchCache.m_Language = pLanguage != nullptr ? pLanguage : "";
		s_GlobalSearchCache.m_Sixup = Client()->IsSixup();
		s_GlobalSearchCache.m_LayoutRevision = LayoutRevision;
		s_GlobalSearchCache.m_TextGeneration = m_MenuTextPoolGeneration;
		CollectGlobalSearchResults(pModuleSearch, s_GlobalSearchCache.m_Sixup, CardOrderModel, s_GlobalSearchCache.m_Results);
		auto &vResults = s_GlobalSearchCache.m_Results.m_vAllVisibleCards;
		std::vector<qm_card_order::SEntry> vEntries;
		vEntries.reserve(vResults.size() + 1);
		// 搜索输入卡片不再进入卡组：它脱离滚动流手动绘制（默认居中，搜索后置顶）。
		vEntries.push_back({"deck:global-search-empty", "global-search", 0, -1});
		for(size_t Index = 0; Index < vResults.size(); ++Index)
		{
			// 搜索结果卡片使用半宽：交替放入左右两列（0=整宽保留给搜索输入与空态卡片）。
			const int ResultColumn = Index % 2 == 0 ? 1 : 2;
			vEntries.push_back({vResults[Index].m_pStableId, "global-search", ResultColumn, (int)(Index / 2)});
		}
		s_GlobalSearchCache.m_Model.SetEntries(vEntries);
		s_GlobalSearchPrewarmOrderModel.SetEntries(std::move(vEntries));
		s_GlobalSearchCache.m_Valid = true;
	}
	const std::vector<SQmGlobalSearchCard> &SearchVisibleGlobalCards = s_GlobalSearchCache.m_Results.m_vAllVisibleCards;
	const int SearchMatchedGlobalCardCount = (int)SearchVisibleGlobalCards.size();
	s_GlobalSearchFunctionCardLayout = ResolveFunctionCardLayoutState(GameClient()->TClientComponent().GetFavoriteMaps().size());
	qm_card_catalog::SQmCardBuildContext StandardPrepare;
	StandardPrepare.m_pMenus = this;
	StandardPrepare.m_ReadOnly = ReadOnly;
	StandardPrepare.m_Page = Page;
	StandardPrepare.m_Metrics = Metrics;
	StandardPrepare.m_UiContext = SearchCtx;
	StandardPrepare.m_pScrollRegion = Ui()->MeasurementOnly() ? nullptr : &s_GlobalSearchScrollRegion;
	std::vector<const char *> vStandardIds;
	vStandardIds.reserve(SearchVisibleGlobalCards.size());
	for(const auto &Card : SearchVisibleGlobalCards)
		vStandardIds.push_back(Card.m_pStableId);
	uint64_t CardLayoutRevision = str_quickhash("global-search");
	CardLayoutRevision ^= qm_card_catalog::QmCardRenderHook::PrepareStandardCards(StandardPrepare, vStandardIds);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::QmCardRenderHook::PrepareTClientCards(StandardPrepare, vStandardIds);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ str_quickhash(pModuleSearch != nullptr ? pModuleSearch : "");
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ LayoutRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (Client()->IsSixup() ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (ReadOnly ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::MeasureModuleCardsRevision(s_GlobalSearchFunctionCardLayout);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::MeasureContentRevision(g_Localization.Languages().size(), GameClient()->m_MenuBackground.GetThemes().size());
	if(std::any_of(SearchVisibleGlobalCards.begin(), SearchVisibleGlobalCards.end(), [](const SQmGlobalSearchCard &Card) { return str_startswith(Card.m_pStableId, "deck:controls-") != nullptr; }))
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::QmCardRenderHook::PrepareControlsCards(this, MainView.w, ReadOnly, Ui()->MeasurementOnly() ? nullptr : &s_GlobalSearchScrollRegion);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchFunctionCardLayout.m_BlockWordsRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchFunctionCardLayout.m_KeywordRulesRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchFunctionCardLayout.m_FavoriteMapsRevision;
	for(const SQmGlobalSearchCard &Card : SearchVisibleGlobalCards)
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ str_quickhash(Card.m_pStableId);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	s_GlobalSearchCardBuild = {};
	s_GlobalSearchCardBuild.m_pMenus = this;
	s_GlobalSearchCardBuild.m_ReadOnly = ReadOnly;
	s_GlobalSearchCardBuild.m_Page = Page;
	s_GlobalSearchCardBuild.m_Metrics = Metrics;
	s_GlobalSearchCardBuild.m_LabelWidth = ResolveSettingsCardLabelWidth(Page.m_TwoColumns ? Page.m_aColumns[0].w : Page.m_ContentViewport.w, Metrics);
	s_GlobalSearchCardBuild.m_UiContext = SearchCtx;
	s_GlobalSearchCardBuild.m_pScrollRegion = Ui()->MeasurementOnly() ? nullptr : &s_GlobalSearchScrollRegion;
	s_GlobalSearchCardBuild.m_Padding = CardStyle.m_Padding;
	s_GlobalSearchCardBuild.m_CornerRadius = CardStyle.m_CornerRadius;
	s_GlobalSearchCardBuild.m_pCollapsed = s_aGlobalSearchCollapsed.data();
	s_GlobalSearchCardBuild.m_pCollapseButtons = s_aGlobalSearchCollapseButtons.data();
	s_GlobalSearchCardBuild.m_pToggleCollapsed = ToggleSearchCollapsed;
	s_GlobalSearchCardBuild.m_pToggleCollapsedUser = s_aGlobalSearchCollapsed.data();
	s_GlobalSearchCardBuild.m_pFunctionLayout = &s_GlobalSearchFunctionCardLayout;

	const bool HasSearchQuery = pModuleSearch != nullptr && pModuleSearch[0] != '\0';
	// s_GlobalSearchCardBuild 是函数内静态对象，直接引用即可，无需附加捕获。
	const auto BuildDefinitions = [this, UiScale, BodySize, SmallSize, LineHeight, LineSpacing, Metrics, SearchMatchedGlobalCardCount, HasSearchQuery, ReadOnly, SearchCtx, &SearchVisibleGlobalCards](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(SearchMatchedGlobalCardCount + 1);
		// 搜索输入卡片已脱离卡组，在 RenderCached 之前手动绘制并定位。

		SSettingsCardDefinition EmptyCard;
		EmptyCard.m_Spec = {"deck:global-search-empty", Localize("Search"), qm_card_registry::ResolveLocalizedDescription("deck:global-search-results")};
		EmptyCard.m_RenderMeasured = [this, SmallSize, LineHeight](CUIRect &Content) {
			CSettingsContentLayoutFlow Flow(Content);
			Flow.Text("search-empty", Localize("No matching features found. Try other keywords"), 0, SmallSize, 0, {}, [this, SmallSize](const CUIRect &Row, const char *pText) {
				SLabelProperties Props;
				Props.m_MaxWidth = Row.w;
				Props.m_EnableWidthCheck = false;
				Ui()->DoLabel(&Row, pText, SmallSize, TEXTALIGN_ML, Props); }, LineHeight);
		};
		qm_settings_content::UseDeclaredContent(EmptyCard, Ui(), TextRender());
		EmptyCard.m_IsVisible = [HasSearchQuery, SearchMatchedGlobalCardCount] { return HasSearchQuery && SearchMatchedGlobalCardCount == 0; };
		vCards.push_back(std::move(EmptyCard));

		for(const SQmGlobalSearchCard &Card : SearchVisibleGlobalCards)
		{
			SSettingsCardDefinition Definition;
			if(!qm_card_catalog::BuildCard(s_GlobalSearchCardBuild, Card.m_pStableId, Definition))
			{
				const qm_card_registry::SCardDefault *pDefault = qm_card_registry::FindByStableId(Card.m_pStableId);
				Definition.m_Spec = {Card.m_pStableId, pDefault != nullptr && pDefault->m_pTitle != nullptr ? Localize(pDefault->m_pTitle) : Localize("Global card"), qm_card_registry::ResolveLocalizedDescription(Card.m_pStableId)};
				// 兜底卡无法复用原页面的卡片构建器（构建闭包绑定在各自页面），
				// 退化为「描述 + 定位按钮」的导航卡，而不是只有一行定位。
				CButtonContainer *pOpenButton = &s_GlobalSearchActionButtons[std::string("open:") + Card.m_pStableId];
				Definition.m_RenderMeasured = [this, Description = Card.m_Description, Target = Card.m_Target, pOpenButton, ReadOnly, LineHeight, LineSpacing, SmallSize, Metrics](CUIRect &Content) {
					CSettingsContentLayoutFlow Flow(Content);
					CUIRect Row;
					Flow.Text("search-description", Description.c_str(), 0, SmallSize, LineSpacing * 0.65f, {}, [this, SmallSize](const CUIRect &DescriptionRow, const char *pText) {
						SLabelProperties Props;
						Props.m_MaxWidth = DescriptionRow.w;
						Props.m_EnableWidthCheck = false;
						Ui()->DoLabel(&DescriptionRow, pText, SmallSize, TEXTALIGN_ML, Props); }, LineHeight);
					if(!Flow.Control("search-locate", LineHeight, Row))
						return;
					const char *pLabel = Localize("Locate");
					const CUIRect LocateButton = ResolveSettingsActionGroupLayout(Row, 1, Metrics, 0.0f).m_aButtons[0];
					if(LocateButton.w <= 0.0f)
						return;
					if(!ReadOnly && Ui()->DoButtonLogic(pOpenButton, 0, &LocateButton, BUTTONFLAG_LEFT))
					{
						NavigateToSettingsCard(Target);
						Ui()->ReleaseActiveTextInput(&m_GlobalCardSearchInput);
						m_GlobalCardSearchInput.Deactivate();
						return;
					}
					// 完整按钮组件：圆角底 + 居中文本，而非纯文本。
					ui_widget::DrawButtonSurface(ui_widget::ControlContext(Ui()), pOpenButton, LocateButton);
					Ui()->DoLabel(&LocateButton, pLabel, SmallSize, TEXTALIGN_MC);
				};
				qm_settings_content::UseDeclaredContent(Definition, Ui(), TextRender());
				vCards.push_back(std::move(Definition));
				continue;
			}
			CButtonContainer *pLocateButton = &s_GlobalSearchActionButtons[std::string("locate:") + Card.m_pStableId];
			Definition.m_LeadingHeaderActionWidth = TextRender()->TextWidth(SmallSize, Localize("Locate")) + 16.0f * UiScale;
			Definition.m_LeadingHeaderAction = [this, Target = Card.m_Target, pLocateButton, ReadOnly, UiScale, SmallSize, SearchCtx](const SSettingsCardFrame &Frame, bool) {
				if(ReadOnly)
					return;
				// 定位使用 Deck 预留的最左操作区，不再覆盖宽度或说明按钮。
				const char *pLabel = Localize("Locate");
				const CUIRect LocateButton = Frame.m_LeadingHeaderActionRect;
				if(LocateButton.w <= 0.0f || LocateButton.h <= 0.0f)
					return;
				if(Ui()->DoButtonLogic(pLocateButton, 0, &LocateButton, BUTTONFLAG_LEFT))
				{
					NavigateToSettingsCard(Target);
					Ui()->ReleaseActiveTextInput(&m_GlobalCardSearchInput);
					m_GlobalCardSearchInput.Deactivate();
					return;
				}
				// 完整按钮组件：圆角底 + 居中文本，而非纯文本。
				const bool Hovered = Ui()->MouseHovered(&LocateButton);
				const float Radius = std::min(ui_token::radius::TIGHT * UiScale, std::min(LocateButton.w, LocateButton.h) * 0.25f);
				const ColorRGBA ChromeColor(1.0f, 1.0f, 1.0f, Hovered ? 0.28f : 0.18f);
				if(LocateButton.w < TextRender()->TextWidth(SmallSize, pLabel) + 16.0f * UiScale)
				{
					RenderSettingsCardHeaderIcon(SearchCtx, LocateButton, EQmIcon::ARROW_RIGHT, FontIcons::FONT_ICON_CHEVRON_RIGHT, 1.0f, pLocateButton);
					GameClient()->m_Tooltips.DoSmallToolTip(pLocateButton, &LocateButton, pLabel, SmallSize);
				}
				else
				{
					DrawRoundedSurface(Ui(), LocateButton, ChromeColor, ChromeColor, Radius);
					Ui()->DoLabel(&LocateButton, pLabel, SmallSize, TEXTALIGN_MC);
				}
			};
			vCards.push_back(std::move(Definition));
		}
	};

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	const SCardMotionSpec Motion = SettingsCardMotionSpec();

	// —— 搜索输入卡片：脱离卡组手动绘制 ——
	// 无搜索内容时在视口内垂直居中；输入内容后置顶并把下方空间让位给结果卡片，
	// 两种状态间的过渡沿用卡组让位动画的时长与缓动。
	static CButtonContainer s_GlobalSearchInputCollapseButton;
	static bool s_GlobalSearchInputCollapsed = false;
	const float InputContentHeight = s_GlobalSearchInputCollapsed ? 0.0f : 2.0f * LineHeight + LineSpacing;
	const SSettingsCardSpec InputSpec{"deck:global-search-input", Localize("Feature Search"), qm_card_registry::ResolveLocalizedDescription("deck:global-search-input")};
	const float InputChromeHeight = BuildSettingsCardFrame({0.0f, 0.0f, Page.m_ContentViewport.w, 0.0f}, InputSpec, InputContentHeight, UiScale).m_Rect.h;
	const float CenteredOffsetY = std::max(0.0f, (Page.m_ContentViewport.h - InputChromeHeight) * 0.5f);
	const float TargetOffsetY = HasSearchQuery ? 0.0f : CenteredOffsetY;
	float InputOffsetY = TargetOffsetY;
	CUiV2AnimationRuntime *pSearchAnimRuntime = ReadOnly ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();
	if(pSearchAnimRuntime != nullptr)
	{
		const uint64_t InputOffsetKey = BuildUiAnimNodeKey(str_quickhash("global-search"), str_quickhash("global-search-input-offset"));
		InputOffsetY = ResolveUiAnimValue(*pSearchAnimRuntime, InputOffsetKey, EUiAnimProperty::POS_Y, TargetOffsetY, Motion.m_ReflowDuration, EEasing::EASE_OUT);
	}
	// 进入「搜索」页第一帧自动聚焦输入框：同时建立 ActiveItem 与 CLineInput 激活态，
	// 之后由 DoEditBox 的激活维持逻辑接管；一次性消费，点击其他控件可正常移走焦点。
	if(!ReadOnly && m_FocusSettingsSearchOnEntry && !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive() && !s_GlobalSearchInputCollapsed)
	{
		m_FocusSettingsSearchOnEntry = false;
		Ui()->SetActiveItem(&m_GlobalCardSearchInput);
		m_GlobalCardSearchInput.Activate(EInputPriority::UI);
		m_GlobalCardSearchInput.SelectAll();
	}
	{
		const CUIRect InputSlot{Page.m_ContentViewport.x, Page.m_ContentViewport.y + InputOffsetY, Page.m_ContentViewport.w, 0.0f};
		SSettingsCardVisualState InputVisualState;
		InputVisualState.m_Collapsed = s_GlobalSearchInputCollapsed;
		InputVisualState.m_ClipContent = true;
		const auto RenderInputContent = [this, UiScale, BodySize, SmallSize, LineHeight, LineSpacing, SearchMatchedGlobalCardCount, ReadOnly](CUIRect Content) {
			if(Content.h <= 0.0f || Content.w <= 0.0f)
				return;
			CUIRect Row;
			Content.HSplitTop(LineHeight, &Row, &Content);
			IUiContext InputCtx = SettingsUiContext("settings_global_search", UiScale);
			if(ReadOnly)
			{
				InputCtx.m_pAnim = nullptr;
				InputCtx.m_pTree = nullptr;
			}
			ui_widget::InputField(InputCtx, &m_GlobalCardSearchInput, Row, BodySize, !ReadOnly && !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
			Content.HSplitTop(LineSpacing * 0.65f, nullptr, &Content);
			Content.HSplitTop(LineHeight, &Row, &Content);
			char aSearchHint[64];
			str_format(aSearchHint, sizeof(aSearchHint), Localize("Found %d global cards"), SearchMatchedGlobalCardCount);
			TextRender()->TextColor(ui_token::color::TEXT_SECONDARY);
			Ui()->DoLabel(&Row, aSearchHint, SmallSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		};
		const auto InputHeaderAction = [this, ReadOnly, UiScale, SearchCtx](const SSettingsCardFrame &Frame, bool) mutable {
			if(ReadOnly)
				return;
			if(Ui()->DoButtonLogic(&s_GlobalSearchInputCollapseButton, 0, &Frame.m_HandleRect, BUTTONFLAG_LEFT))
				s_GlobalSearchInputCollapsed = !s_GlobalSearchInputCollapsed;
			RenderSettingsCardCollapseButton(SearchCtx, Frame.m_HandleRect, s_GlobalSearchInputCollapsed, 1.0f, &s_GlobalSearchInputCollapseButton);
		};
		SettingsCard(SearchCtx, InputSlot, InputSpec, InputVisualState, SettingsCardDeckVisualOptions(), [InputContentHeight](float) { return InputContentHeight; }, RenderInputContent, InputHeaderAction);
	}

	// 结果卡组从输入卡片底部（含动画中的位置）开始，让位动画期间跟随卡片一起上移。
	SSettingsPageLayoutFrame DeckPage = Page;
	{
		const float DeckTopDelta = InputOffsetY + InputChromeHeight + Page.m_CardGap;
		const auto ShiftRectDown = [DeckTopDelta](CUIRect &Rect) {
			Rect.y += DeckTopDelta;
			Rect.h = std::max(0.0f, Rect.h - DeckTopDelta);
		};
		ShiftRectDown(DeckPage.m_ScrollViewport);
		ShiftRectDown(DeckPage.m_UnreservedScrollViewport);
		ShiftRectDown(DeckPage.m_ContentViewport);
		ShiftRectDown(DeckPage.m_aColumns[0]);
		ShiftRectDown(DeckPage.m_aColumns[1]);
	}

	SSettingsCardDeckInput InputState;
	InputState.m_MouseX = ReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = false;
	// 搜索结果按匹配顺序排列，不修改分类页的持久布局。
	InputState.m_AllowHeaderDrag = false;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = Ui()->MeasurementOnly() ? nullptr : &ScrollParams;
	const uint64_t MeasurementEnvironmentRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, 0);
	CardDeck.RenderCached(SearchCtx, DeckPage, "global-search", DefinitionsRevision, BuildDefinitions, DeckOrderModel, Ui()->MeasurementOnly() ? nullptr : &s_GlobalSearchScrollRegion, InputState, Motion, SettingsCardDeckVisualOptions(), MeasurementEnvironmentRevision);
}

void CMenus::ReleaseQmClientSettingsSearchInput()
{
	ReleaseSettingsSearchInput(Ui(), &m_GlobalCardSearchInput);
	// 离页和关闭菜单只释放输入焦点，查询及匹配缓存保留到用户清空或重启。
	m_FocusSettingsSearchOnEntry = g_Config.m_UiSettingsPage == SETTINGS_SEARCH;
}
