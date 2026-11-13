#include "menus_qmclient_internal.h"
#include "search_answers.h"
#include "settings_global_search_cache.h"

#include <base/secure.h>
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
#include <game/client/QmUi/SettingsSearchReveal.h>
#include <game/client/QmUi/SettingsSearchSession.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalogSearch.h>
#include <game/client/QmUi/cards/QmCardMeasureRevision.h>
#include <game/client/QmUi/cards/QmSearchAnswerCard.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace qm_settings_menu;

namespace
{
	using SQmGlobalSearchCard = qm_card_registry::SCardSearchResult;
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
	if(!ReadOnly && !Ui()->DrawingSuppressed() && m_GlobalSearchHeaderSession.NeedsEntry(m_SettingsCardDeckDisplayCycle))
		m_GlobalSearchHeaderSession.Enter(m_SettingsCardDeckDisplayCycle, secure_rand_below(m_GlobalSearchHeaderSession.ChoiceCount()), secure_rand_below(qm_search_header::NUM_LOGOS));
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
	static CSettingsGlobalSearchCache s_GlobalSearchCache;
	static CSettingsCardDeck s_GlobalSearchPrewarmDeck;
	static std::shared_ptr<qm_card_catalog::SSearchAnswerCardState> s_GlobalSearchAnswerCardState;
	static qm_card_catalog::SQmCardBuildContext s_GlobalSearchCardBuild;
	static qm_card_catalog::SQmFunctionCardLayoutState s_GlobalSearchFunctionCardLayout;
	static std::unordered_map<std::string, CButtonContainer> s_GlobalSearchActionButtons;
	// 分类布局只用于解析搜索和导航；以下两个模型仅属于搜索页，不持久化。
	const qm_card_order::CModel &CardOrderModel = SettingsCardOrderModel();
	CSettingsCardDeck &CardDeck = Ui()->MeasurementOnly() ? s_GlobalSearchPrewarmDeck : m_SettingsCardDeck;
	const char *pLanguage = g_Config.m_ClLanguagefile;
	const bool CacheCurrent = s_GlobalSearchCache.TryUpdate(pModuleSearch, pLanguage, Client()->IsSixup(), m_MenuTextPoolGeneration, CardOrderModel);
	if(!s_GlobalSearchCache.HasSnapshot())
		return;
	// 暂时显示完整旧结果，准备成功前只阻断结果交互，输入框仍可继续编辑。
	const bool ResultsReadOnly = ReadOnly || !CacheCurrent;
	qm_card_order::CModel &DeckOrderModel = s_GlobalSearchCache.OrderModel(Ui()->MeasurementOnly());
	const std::vector<SQmGlobalSearchCard> &SearchVisibleGlobalCards = s_GlobalSearchCache.Results().m_vAllVisibleCards;
	const int SearchMatchedGlobalCardCount = (int)SearchVisibleGlobalCards.size();
	const auto &SearchAnswers = s_GlobalSearchCache.Results().m_Answers;
	const int SearchAnswerCount = SearchAnswers.m_pAnswer != nullptr ? 1 : (int)SearchAnswers.m_vCandidates.size();
	s_GlobalSearchFunctionCardLayout = ResolveFunctionCardLayoutState();
	qm_card_catalog::SQmCardBuildContext StandardPrepare;
	StandardPrepare.m_pMenus = this;
	StandardPrepare.m_ReadOnly = ResultsReadOnly;
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
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchCache.Revision();
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (Client()->IsSixup() ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ (ResultsReadOnly ? 1u : 0u);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::MeasureModuleCardsRevision(s_GlobalSearchFunctionCardLayout);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::MeasureContentRevision(g_Localization.Languages().size(), GameClient()->m_MenuBackground.GetThemes().size());
	if(std::any_of(SearchVisibleGlobalCards.begin(), SearchVisibleGlobalCards.end(), [](const SQmGlobalSearchCard &Card) { return str_startswith(Card.m_pStableId, "deck:controls-") != nullptr; }))
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ qm_card_catalog::QmCardRenderHook::PrepareControlsCards(this, MainView.w, ResultsReadOnly, Ui()->MeasurementOnly() ? nullptr : &s_GlobalSearchScrollRegion);
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchFunctionCardLayout.m_BlockWordsRevision;
	CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ s_GlobalSearchFunctionCardLayout.m_KeywordRulesRevision;
	for(const SQmGlobalSearchCard &Card : SearchVisibleGlobalCards)
		CardLayoutRevision = CardLayoutRevision * 1099511628211ULL ^ str_quickhash(Card.m_pStableId);
	const uint64_t DefinitionsRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, CardLayoutRevision);

	const SQmSettingsCardStyle CardStyle = QmSettingsCardStyle(UiScale);
	s_GlobalSearchCardBuild = {};
	s_GlobalSearchCardBuild.m_pMenus = this;
	s_GlobalSearchCardBuild.m_ReadOnly = ResultsReadOnly;
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
	if(!ResultsReadOnly)
	{
		if(HasSearchQuery)
		{
			try
			{
				std::vector<qm_settings_reveal::SPreview> vPreviews;
				vPreviews.reserve(SearchVisibleGlobalCards.size());
				for(const SQmGlobalSearchCard &Card : SearchVisibleGlobalCards)
				{
					if(!Card.m_vMatchKeys.empty())
						vPreviews.push_back({Card.m_pStableId, Card.m_vMatchKeys});
				}
				qm_settings_reveal::SetPreviews(vPreviews);
			}
			catch(const std::bad_alloc &)
			{
				// 候选未备齐时保留旧预览及原期限，下帧重新准备。
			}
			catch(const std::length_error &)
			{
			}
		}
		else
		{
			qm_settings_reveal::Clear();
		}
	}
	// s_GlobalSearchCardBuild 是函数内静态对象，直接引用即可，无需附加捕获。
	const auto BuildDefinitions = [this, UiScale, BodySize, SmallSize, LineHeight, LineSpacing, Metrics, SearchMatchedGlobalCardCount, HasSearchQuery, ReadOnly = ResultsReadOnly, SearchCtx, &SearchVisibleGlobalCards, &SearchAnswers, SearchAnswerCount](std::vector<SSettingsCardDefinition> &vCards) {
		vCards.reserve(SearchMatchedGlobalCardCount + 2);
		// 搜索输入卡片已脱离卡组，在 RenderCached 之前手动绘制并定位。
		if(SearchAnswerCount > 0)
		{
			if(!s_GlobalSearchAnswerCardState)
				s_GlobalSearchAnswerCardState = qm_card_catalog::CreateSearchAnswerCardState();
			SSettingsCardDefinition AnswerCard;
			qm_card_catalog::BuildSearchAnswerCard({Ui(), TextRender(), Input(), Storage(), Client()}, SearchAnswers, ReadOnly, Metrics, [this](const char *pQuestion) { m_GlobalCardSearchInput.Set(pQuestion); }, AnswerCard, s_GlobalSearchAnswerCardState);
			vCards.push_back(std::move(AnswerCard));
		}

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
		EmptyCard.m_IsVisible = [HasSearchQuery, SearchMatchedGlobalCardCount, SearchAnswerCount] { return HasSearchQuery && SearchMatchedGlobalCardCount == 0 && SearchAnswerCount == 0; };
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
				Definition.m_RenderMeasured = [this, Description = Card.m_Description, TargetTab = std::string(Card.m_Target.m_pTab != nullptr ? Card.m_Target.m_pTab : ""), HasTargetTab = Card.m_Target.m_pTab != nullptr, TargetStableId = Card.m_Target.m_pStableId, StableId = Card.m_pStableId, Keys = Card.m_vMatchKeys, pOpenButton, ReadOnly, LineHeight, LineSpacing, SmallSize, Metrics](CUIRect &Content) {
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
						if(!NavigateToSettingsCard({HasTargetTab ? TargetTab.c_str() : nullptr, TargetStableId}, StableId, Keys))
							return;
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
			Definition.m_LeadingHeaderAction = [this, TargetTab = std::string(Card.m_Target.m_pTab != nullptr ? Card.m_Target.m_pTab : ""), HasTargetTab = Card.m_Target.m_pTab != nullptr, TargetStableId = Card.m_Target.m_pStableId, StableId = Card.m_pStableId, Keys = Card.m_vMatchKeys, pLocateButton, ReadOnly, UiScale, SmallSize, SearchCtx](const SSettingsCardFrame &Frame, bool) {
				if(ReadOnly)
					return;
				// 定位使用 Deck 预留的最左操作区，不再覆盖宽度或说明按钮。
				const char *pLabel = Localize("Locate");
				const CUIRect LocateButton = Frame.m_LeadingHeaderActionRect;
				if(LocateButton.w <= 0.0f || LocateButton.h <= 0.0f)
					return;
				if(Ui()->DoButtonLogic(pLocateButton, 0, &LocateButton, BUTTONFLAG_LEFT))
				{
					if(!NavigateToSettingsCard({HasTargetTab ? TargetTab.c_str() : nullptr, TargetStableId}, StableId, Keys))
						return;
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
				}
				else
				{
					DrawRoundedSurface(Ui(), LocateButton, ChromeColor, ChromeColor, Radius);
					Ui()->DoLabel(&LocateButton, pLabel, SmallSize, TEXTALIGN_MC);
				}
				GameClient()->m_Tooltips.DoSmallToolTip(pLocateButton, &LocateButton, pLabel, SmallSize);
			};
			vCards.push_back(std::move(Definition));
		}
	};

	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	const CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	const SCardMotionSpec Motion = SettingsCardMotionSpec();

	// —— 搜索输入卡片：脱离卡组手动绘制 ——
	// 无搜索内容时卡片中心位于视口中线；输入内容后置顶并把下方空间让位给结果卡片，
	// 两种状态间的过渡沿用卡组让位动画的时长与缓动。
	static CButtonContainer s_GlobalSearchInputCollapseButton;
	static bool s_GlobalSearchInputCollapsed = false;
	// 进入「搜索」页时先展开输入卡片再聚焦：折叠态会让下面的一次性标志被静默丢弃，
	// 而折叠状态既不持久化也没有复位点，点过一次折叠箭头后自动聚焦就永久失效。
	// 回退 PR #425 时丢掉了这段展开逻辑，只留下带折叠判据的消费分支。
	if(!ReadOnly && m_FocusSettingsSearchOnEntry)
		s_GlobalSearchInputCollapsed = false;
	const float SearchInputHeight = LineHeight * 1.2f;
	const float SearchInputFontSize = BodySize * 1.2f;
	const float InputContentHeight = s_GlobalSearchInputCollapsed ? 0.0f : SearchInputHeight + LineHeight + LineSpacing;
	const SSettingsCardSpec InputSpec{"deck:global-search-input", Localize("Feature Search"), qm_card_registry::ResolveLocalizedDescription("deck:global-search-input")};
	const float InputChromeHeight = BuildSettingsCardFrame({0.0f, 0.0f, Page.m_ContentViewport.w, 0.0f}, InputSpec, InputContentHeight, UiScale).m_Rect.h;
	const float SubtitleWidth = qm_search_header::SubtitleWidth(Page.m_ContentViewport);
	const float SubtitleHeight = !HasSearchQuery && SubtitleWidth > 0.0f ?
					     TextRender()->TextBoundingBox(14.0f * UiScale, m_GlobalSearchHeaderSession.Text(), -1, SubtitleWidth).m_H + 8.0f * UiScale :
					     0.0f;
	const auto HeaderLayout = qm_search_header::Layout(Page.m_ContentViewport, InputChromeHeight, UiScale, HasSearchQuery, m_GlobalSearchHeaderSession.LogoIndex(), SubtitleHeight);
	const float TargetOffsetY = HeaderLayout.m_InputOffsetY;
	float InputOffsetY = TargetOffsetY;
	CUiV2AnimationRuntime *pSearchAnimRuntime = ReadOnly ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();
	if(pSearchAnimRuntime != nullptr)
	{
		const uint64_t InputOffsetKey = BuildUiAnimNodeKey(str_quickhash("global-search"), str_quickhash("global-search-input-offset"));
		InputOffsetY = ResolveUiAnimValue(*pSearchAnimRuntime, InputOffsetKey, EUiAnimProperty::POS_Y, TargetOffsetY, Motion.m_ReflowDuration, EEasing::EASE_OUT);
	}
	if(!HasSearchQuery)
	{
		Ui()->ClipEnable(&Page.m_ContentViewport);
		qm_search_header::Render(Ui(), Graphics(), TextRender(), HeaderLayout, m_GlobalSearchHeaderSession.Text(), UiScale, InputOffsetY - TargetOffsetY);
		Ui()->ClipDisable();
	}
	// 进入「搜索」页第一帧自动聚焦输入框：同时建立 ActiveItem 与 CLineInput 激活态，
	// 之后由 DoEditBox 的激活维持逻辑接管；一次性消费，点击其他控件可正常移走焦点。
	// 页签在按下帧切换页面，同一帧的这次按下会让 DoEditBox 判成「点击输入框外部」并
	// 立刻释放刚建立的焦点；推迟到按下事件已消费的下一帧再聚焦，避免聚焦被同一次点击撤销。
	if(!ReadOnly && m_FocusSettingsSearchOnEntry && !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive() &&
		!s_GlobalSearchInputCollapsed && !Ui()->MouseButtonClicked(0))
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
		const auto RenderInputContent = [this, UiScale, SearchInputHeight, SearchInputFontSize, SmallSize, LineHeight, LineSpacing, SearchMatchedGlobalCardCount, SearchAnswerCount, HasSearchQuery, ReadOnly](CUIRect Content) {
			if(Content.h <= 0.0f || Content.w <= 0.0f)
				return;
			CUIRect Row;
			Content.HSplitTop(SearchInputHeight, &Row, &Content);
			IUiContext InputCtx = SettingsUiContext("settings_global_search", UiScale);
			if(ReadOnly)
			{
				InputCtx.m_pAnim = nullptr;
				InputCtx.m_pTree = nullptr;
			}
			ui_widget::InputField(InputCtx, &m_GlobalCardSearchInput, Row, SearchInputFontSize, !ReadOnly && !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
			Content.HSplitTop(LineSpacing * 0.65f, nullptr, &Content);
			Content.HSplitTop(LineHeight, &Row, &Content);
			char aSearchHint[192];
			if(!HasSearchQuery)
				str_copy(aSearchHint, "搜索功能或提问，例如：截图在哪里");
			else
				str_format(aSearchHint, sizeof(aSearchHint), "%d 个设置卡片 · %d 个常见问题", SearchMatchedGlobalCardCount, SearchAnswerCount);
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
		// 搜索输入卡只有折叠按钮、没有宽度切换：ⓘ 直接占宽度按钮的位置，紧贴折叠按钮。
		// 视觉选项必须从卡片配置构造：手搓空对象会丢掉卡片背景色、透明度、边框与彩虹标题，
		// 退回主题面板色，卡片看起来和同一页的其它卡片不是一套配色。
		SSettingsCardDeckVisualOptions InputVisualOptions = SettingsCardDeckVisualOptions();
		InputVisualOptions.m_HasWidthButton = false;
		SettingsCard(SearchCtx, InputSlot, InputSpec, InputVisualState, InputVisualOptions, [InputContentHeight](float) { return InputContentHeight; }, RenderInputContent, InputHeaderAction);
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
	InputState.m_MouseX = ResultsReadOnly ? 0.0f : Ui()->MouseX();
	InputState.m_MouseY = ResultsReadOnly ? 0.0f : Ui()->MouseY();
	InputState.m_MousePressed = !ResultsReadOnly && Ui()->MouseButtonClicked(0);
	InputState.m_MouseDown = !ResultsReadOnly && Ui()->MouseButton(0);
	InputState.m_MouseReleased = !ResultsReadOnly && !InputState.m_MouseDown && Ui()->LastMouseButton(0);
	InputState.m_CtrlPressed = false;
	// 搜索结果按匹配顺序排列，不修改分类页的持久布局。
	InputState.m_AllowHeaderDrag = false;
	InputState.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
	InputState.m_pScrollParams = Ui()->MeasurementOnly() ? nullptr : &ScrollParams;
	const uint64_t MeasurementEnvironmentRevision = ResolveSettingsCardDefinitionsRevision(m_SettingsCardDeckDisplayCycle, m_MenuTextPoolGeneration, MainView.w, 0);
	auto VisualOptions = SettingsCardDeckVisualOptions();
	// 答案默认占据整行，先于左右两列设置卡片。
	VisualOptions.m_LeadingFullWidthCards = 2;
	const auto RenderResults = [&] {
		CardDeck.RenderCached(SearchCtx, DeckPage, "global-search", DefinitionsRevision, BuildDefinitions, DeckOrderModel, Ui()->MeasurementOnly() ? nullptr : &s_GlobalSearchScrollRegion, InputState, Motion, VisualOptions, MeasurementEnvironmentRevision);
	};
	if(CacheCurrent)
		RenderResults();
	else
	{
		const CUiScopedBackgroundRender PendingResults(Ui());
		RenderResults();
	}
}

void CMenus::ReleaseQmClientSettingsSearchInput()
{
	ReleaseSettingsSearchInput(Ui(), &m_GlobalCardSearchInput);
	m_GlobalSearchHeaderSession.Leave();
	// 离页和关闭菜单只释放输入焦点，查询及匹配缓存保留到用户清空或重启。
	m_FocusSettingsSearchOnEntry = g_Config.m_UiSettingsPage == SETTINGS_SEARCH;
}
