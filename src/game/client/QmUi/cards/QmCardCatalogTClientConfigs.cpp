#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/warning.h>

#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsFontSelection.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/font_download_storage.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/section_loader.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL3/SDL_audio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace qm_tclient_cards;

qm_card_catalog::STClientCardResult CMenus::RunTClientConfigsCard(const qm_card_catalog::SQmCardBuildContext &Ctx, const char *pStableId, CUIRect &Content, qm_card_catalog::ETClientCardPass Pass)
{
	CUIRect MainView = Ctx.m_Page.m_ContentViewport;
	ApplyTClientContentMetrics(Ctx.m_Metrics);
	CPerfTimer RenderTimer;
	const bool ReadOnly = Ctx.m_ReadOnly || Pass != qm_card_catalog::ETClientCardPass::RENDER;
	const float UiScale = SettingsPageUiScale(MainView.w);
	const SSettingsContentMetrics ConfigMetrics = ResolveSettingsContentMetrics(MainView.w);
	const SSettingsPageLayoutFrame Page = Ctx.m_Page;
	std::unique_ptr<CUiRenderOnlyGuard> pRenderOnlyGuard;
	if(ReadOnly && !Ui()->RenderOnly())
		pRenderOnlyGuard = std::make_unique<CUiRenderOnlyGuard>(Ui());
	IUiContext ConfigsCtx = SettingsUiContext("settings_tclient_configs", UiScale);
	if(ReadOnly)
	{
		ConfigsCtx.m_pAnim = nullptr;
		ConfigsCtx.m_pTree = nullptr;
	}
	// hi hello, this is a relatively self-contained mess, sorry if you're forking or need to modify this -Tater
	// 你好, 这是一个相对独立的混乱，如果你要分叉或需要修改它，抱歉 -Tater
	struct SIntStage
	{
		int m_Value;
	};
	struct SStrStage
	{
		std::string m_Value;
	};
	struct SColStage
	{
		unsigned m_Value;
	};
	enum class EConfigSource
	{
		DDNET,
		TCLIENT,
		QM,
	};
	static std::unordered_map<const SConfigVariable *, SIntStage> s_StagedInts;
	static std::unordered_map<const SConfigVariable *, SStrStage> s_StagedStrs;
	static std::unordered_map<const SConfigVariable *, SColStage> s_StagedCols;

	struct SIntState
	{
		CLineInputNumber m_Input;
		int m_LastValue = 0;
		bool m_Inited = false;
	};
	struct SStrState
	{
		CLineInputBuffered<512> m_Input;
		bool m_Inited = false;
	};
	struct SColState
	{
		unsigned m_LastValue = 0;
		unsigned m_Working = 0;
		bool m_Inited = false;
	};
	static std::unordered_map<const SConfigVariable *, SIntState> s_IntInputs;
	static std::unordered_map<const SConfigVariable *, SStrState> s_StrInputs;
	static std::unordered_map<const SConfigVariable *, SColState> s_ColInputs;
	static std::unordered_map<const SConfigVariable *, SIntState> s_ReadOnlyIntInputs;
	static std::unordered_map<const SConfigVariable *, SStrState> s_ReadOnlyStrInputs;
	static std::unordered_map<const SConfigVariable *, SColState> s_ReadOnlyColInputs;
	auto &IntInputs = ReadOnly ? s_ReadOnlyIntInputs : s_IntInputs;
	auto &StrInputs = ReadOnly ? s_ReadOnlyStrInputs : s_StrInputs;
	auto &ColInputs = ReadOnly ? s_ReadOnlyColInputs : s_ColInputs;

	auto ClearStagedAndCaches = [&]() {
		s_StagedInts.clear();
		s_StagedStrs.clear();
		s_StagedCols.clear();
		s_IntInputs.clear();
		s_StrInputs.clear();
		s_ColInputs.clear();
	};
	auto SortStagedKeys = [](auto &Staged) {
		std::vector<const SConfigVariable *> vKeys;
		vKeys.reserve(Staged.size());
		for(const auto &Entry : Staged)
			vKeys.push_back(Entry.first);
		std::sort(vKeys.begin(), vKeys.end(), [](const SConfigVariable *pLeft, const SConfigVariable *pRight) {
			return str_comp(pLeft->m_pScriptName, pRight->m_pScriptName) < 0;
		});
		return vKeys;
	};

	size_t ChangesCount = 0;

	static CLineInputBuffered<128> s_SearchInput;
	static int s_TcUiTagVisual = 0;
	static int s_TcUiTagHud = 0;
	static int s_TcUiTagInput = 0;
	static int s_TcUiTagChat = 0;
	static int s_TcUiTagAudio = 0;
	static int s_TcUiTagAutomation = 0;
	static int s_TcUiTagSocial = 0;
	static int s_TcUiTagCamera = 0;
	static int s_TcUiTagGameplay = 0;
	static int s_TcUiTagMisc = 0;

	ChangesCount = s_StagedInts.size() + s_StagedStrs.size() + s_StagedCols.size();
	// 顶部工具行的「应用更改 / 清除更改 / 更改数」整组：测量与渲染共用同一份按钮文字宽度缓存。
	static ui_widget::SSettingsActionGroupState s_ConfigActions;
	const char *apConfigActionLabels[] = {Localize("Apply Changes"), Localize("Clear Changes")};
	// 按整行宽度量出动作组的自然尺寸：按钮按内容取宽，只有极窄行才会被压缩。
	auto MeasureConfigActions = [&](const float RowWidth) {
		const CUIRect Reference{0.0f, 0.0f, std::max(0.0f, RowWidth), ConfigMetrics.m_LineHeight};
		return ui_widget::PrepareSettingsActionGroup(*Ui(), s_ConfigActions, Reference, apConfigActionLabels, 2, ConfigMetrics, ESettingsActionGroupWidth::CONTENT);
	};
	// 计数文本与动作按钮同字号，宽度参与工具行的右对齐测量；右侧留一个间距单位收尾，
	// 避免字体取整把最后一位数字省略成省略号。
	auto MeasureChangesCounter = [&]() {
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("Changes: %d"), (int)ChangesCount);
		return TextRender()->TextWidth(ConfigMetrics.m_BodySize, aBuf) + ui_token::spacing::XS;
	};
	auto RenderActions = [&](const CUIRect &Actions) {
		CPerfTimer ActionsTimer;
		CUIRect ApplyBtn, ClearBtn, Counter;
		const auto ActionLayout = ui_widget::PrepareSettingsActionGroup(*Ui(), s_ConfigActions, Actions, apConfigActionLabels, 2, ConfigMetrics, ESettingsActionGroupWidth::CONTENT);
		ApplyBtn = ActionLayout.m_aButtons[0];
		ClearBtn = ActionLayout.m_aButtons[1];
		// 计数文本紧跟两颗按钮，占满动作区剩余宽度，整组因此贴住工具行右边缘。
		const float CounterX = ClearBtn.x + ClearBtn.w + ConfigMetrics.m_LineSpacing;
		Counter = {CounterX, Actions.y, std::max(0.0f, Actions.x + Actions.w - CounterX), Actions.h};

		static CButtonContainer s_ApplyBtn, s_ClearBtn;
		int DisabledStyle = ChangesCount > 0 ? 0 : -1;
		const bool ApplyClicked = DoTClientSettingsButton_Menu(&s_ApplyBtn, "tclient-config-apply-changes", apConfigActionLabels[0], DisabledStyle, &ApplyBtn);
		if(ChangesCount > 0 && ApplyClicked)
		{
			for(const SConfigVariable *pVar : SortStagedKeys(s_StagedInts))
			{
				const auto Entry = s_StagedInts.find(pVar);
				dbg_assert(Entry != s_StagedInts.end(), "missing staged int");
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %d", pVar->m_pScriptName, Entry->second.m_Value);
				Console()->ExecuteLine(aCmd);
			}
			for(const SConfigVariable *pVar : SortStagedKeys(s_StagedStrs))
			{
				const auto Entry = s_StagedStrs.find(pVar);
				dbg_assert(Entry != s_StagedStrs.end(), "missing staged string");
				char aEsc[1024];
				aEsc[0] = '\0';
				char *pDst = aEsc;
				str_escape(&pDst, Entry->second.m_Value.c_str(), aEsc + sizeof(aEsc));
				char aCmd[1200];
				str_format(aCmd, sizeof(aCmd), "%s \"%s\"", pVar->m_pScriptName, aEsc);
				Console()->ExecuteLine(aCmd);
			}
			for(const SConfigVariable *pVar : SortStagedKeys(s_StagedCols))
			{
				const auto Entry = s_StagedCols.find(pVar);
				dbg_assert(Entry != s_StagedCols.end(), "missing staged color");
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %u", pVar->m_pScriptName, Entry->second.m_Value);
				Console()->ExecuteLine(aCmd);
			}
			ClearStagedAndCaches();
		}
		const bool ClearClicked = DoTClientSettingsButton_Menu(&s_ClearBtn, "tclient-config-clear-changes", apConfigActionLabels[1], DisabledStyle, &ClearBtn);
		if(ChangesCount > 0 && ClearClicked)
		{
			ClearStagedAndCaches();
		}

		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("Changes: %d"), (int)ChangesCount);
		DoTClientLabel(Ui(), &Counter, aBuf, ConfigMetrics.m_BodySize, TEXTALIGN_ML);
		LogTClientPerfStageEx("tclient_configs", "actions", ETClientSettingsPerfStage::INTERACTIVE_LAYER, ActionsTimer.ElapsedMs(), false, aBuf);
	};

	// 顶部工具行：搜索框在左，应用/清除/更改数整组贴右；行高与行内间距全部取自内容 metrics。
	auto RenderToolbarRow = [&](CUIRect Row) {
		const auto ActionsMeasure = MeasureConfigActions(Row.w);
		float ActionsWidth = 0.0f;
		if(ActionsMeasure.m_Count > 0)
		{
			const CUIRect &LastAction = ActionsMeasure.m_aButtons[ActionsMeasure.m_Count - 1];
			ActionsWidth = LastAction.x + LastAction.w + ConfigMetrics.m_LineSpacing + MeasureChangesCounter();
		}
		// 窄行优先保住搜索区的标签列与控件下限，动作组随之压缩，两段不会互相挤压。
		const float SearchMinimum = ResolveSettingsInlineRowMinimumWidth(ConfigMetrics.m_LabelWidth + 2.0f * ConfigMetrics.m_ButtonHeight, ConfigMetrics.m_SectionGap, 1);
		ActionsWidth = std::clamp(ActionsWidth, 0.0f, std::max(0.0f, Row.w - SearchMinimum - ConfigMetrics.m_LineSpacing));
		const CUIRect SearchArea{Row.x, Row.y, std::max(0.0f, Row.w - ActionsWidth - ConfigMetrics.m_LineSpacing), Row.h};
		const CUIRect Actions{SearchArea.x + SearchArea.w + ConfigMetrics.m_LineSpacing, Row.y, ActionsWidth, Row.h};
		CUIRect SearchLabel, SearchEdit;
		SplitSettingsControlRow(SearchArea, ConfigMetrics, &SearchLabel, &SearchEdit);
		DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &SearchLabel, Localize("Search"), FontSize, TEXTALIGN_ML);
		IUiContext TClientConfigSearchCtx;
		TClientConfigSearchCtx.m_pUi = Ui();
		TClientConfigSearchCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
		TClientConfigSearchCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
		TClientConfigSearchCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_config_search");
		TClientConfigSearchCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
		ui_widget::InputField(TClientConfigSearchCtx, &s_SearchInput, SearchEdit, EditBoxFontSize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
		RenderActions(Actions);
	};
	auto RenderFilters = [&](CUIRect &Content) {
		CSettingsContentLayoutFlow Flow(Content);
		// 复用服务器筛选面板的写法：短筛选按钮左侧带圆形勾选指示，一排横看完。
		const auto FilterEntry = [](int *pValue, const char *pTextId) {
			SSettingsToggleEntry Result{pValue, pTextId, Localize(pTextId)};
			Result.m_ShowCheckmark = true;
			return Result;
		};
		DoSettingsToggleButtonGroup(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Content,
			{
				// TClient 的配置项已迁到「栖梦」配置名下，这一项筛选项不再保留；配置变量本身不动。
				FilterEntry(&g_Config.m_QmUiShowDDNet, "DDNet"),
				FilterEntry(&g_Config.m_QmUiShowQm, "QmClient"),
			},
			ConfigMetrics, Flow.Input() && !ReadOnly);
		DoSettingsToggleButtonGroup(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Content,
			{
				FilterEntry(&g_Config.m_QmUiCompactList, "Compact"),
				FilterEntry(&g_Config.m_QmUiOnlyModified, "Modified"),
			},
			ConfigMetrics, Flow.Input() && !ReadOnly);
		DoSettingsToggleButtonGroup(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, Content,
			{
				FilterEntry(&s_TcUiTagVisual, "Visual"),
				FilterEntry(&s_TcUiTagHud, "HUD"),
				FilterEntry(&s_TcUiTagInput, "Input"),
				FilterEntry(&s_TcUiTagChat, "Chat"),
				FilterEntry(&s_TcUiTagAudio, "Audio"),
				FilterEntry(&s_TcUiTagAutomation, "Auto"),
				FilterEntry(&s_TcUiTagSocial, "Social"),
				FilterEntry(&s_TcUiTagCamera, "Camera"),
				FilterEntry(&s_TcUiTagGameplay, "Gameplay"),
				FilterEntry(&s_TcUiTagMisc, "Misc"),
			},
			ConfigMetrics, Flow.Input() && !ReadOnly);
	};

	auto RenderList = [&](CUIRect ListArea) {
		// 行内留白与标签缩进都从内容 metrics 派生：行距一个单位，标签缩进两个单位。
		const float RowInset = ConfigMetrics.m_LineSpacing;
		const float LabelIndent = 2.0f * ConfigMetrics.m_LineSpacing;
		const int FlagMask = CFGFLAG_CLIENT;
		auto BuildConfigTagMask = [](const char *pScriptName) {
			unsigned int Mask = 0;
			for(EConfigTag Tag : ConfigTagsManager()->GetTagsForVariable(pScriptName))
				Mask |= 1u << static_cast<unsigned int>(Tag);
			return Mask;
		};
		static std::vector<const SConfigVariable *> s_vAllClientVars;
		if(s_vAllClientVars.empty())
		{
			auto Collector = [](const SConfigVariable *pVar, void *pUserData) {
				auto *pVec = static_cast<std::vector<const SConfigVariable *> *>(pUserData);
				pVec->push_back(pVar);
			};
			std::vector<const SConfigVariable *> vCollectedVars;
			ConfigManager()->PossibleConfigVariables("", FlagMask, Collector, &vCollectedVars);
			s_vAllClientVars = std::move(vCollectedVars);
			std::sort(s_vAllClientVars.begin(), s_vAllClientVars.end(), [](const SConfigVariable *a, const SConfigVariable *b) {
				if(a->m_ConfigDomain != b->m_ConfigDomain)
					return a->m_ConfigDomain < b->m_ConfigDomain;
				return str_comp(a->m_pScriptName, b->m_pScriptName) < 0;
			});
		}
		static std::vector<unsigned int> s_vAllClientVarTagMasks;
		if(s_vAllClientVarTagMasks.size() != s_vAllClientVars.size())
		{
			s_vAllClientVarTagMasks.resize(s_vAllClientVars.size());
			for(size_t i = 0; i < s_vAllClientVars.size(); ++i)
				s_vAllClientVarTagMasks[i] = BuildConfigTagMask(s_vAllClientVars[i]->m_pScriptName);
		}

		auto GetConfigSource = [&](const SConfigVariable *pVar) {
			// v3 起所有变量合并进单一 QMCLIENT 域，按前缀区分来源
			const char *pName = pVar->m_pScriptName ? pVar->m_pScriptName : "";
			if(str_startswith(pName, "qm_"))
				return EConfigSource::QM;
			if(str_startswith(pName, "tc_"))
				return EConfigSource::TCLIENT;
			return EConfigSource::DDNET;
		};

		auto SourceEnabled = [&](EConfigSource Source) {
			switch(Source)
			{
			case EConfigSource::DDNET: return g_Config.m_QmUiShowDDNet != 0;
			case EConfigSource::TCLIENT: return g_Config.m_QmUiShowTClient != 0;
			case EConfigSource::QM: return g_Config.m_QmUiShowQm != 0;
			default: return false;
			}
		};

		// Tags filter check
		auto TagEnabled = [&](EConfigTag Tag) -> bool {
			switch(Tag)
			{
			case EConfigTag::VISUAL: return s_TcUiTagVisual != 0;
			case EConfigTag::HUD: return s_TcUiTagHud != 0;
			case EConfigTag::INPUT: return s_TcUiTagInput != 0;
			case EConfigTag::CHAT: return s_TcUiTagChat != 0;
			case EConfigTag::AUDIO: return s_TcUiTagAudio != 0;
			case EConfigTag::AUTOMATION: return s_TcUiTagAutomation != 0;
			case EConfigTag::SOCIAL: return s_TcUiTagSocial != 0;
			case EConfigTag::CAMERA: return s_TcUiTagCamera != 0;
			case EConfigTag::GAMEPLAY: return s_TcUiTagGameplay != 0;
			case EConfigTag::MISC: return s_TcUiTagMisc != 0;
			default: return true;
			}
		};

		// Check if any tag filter is enabled
		bool AnyTagEnabled = s_TcUiTagVisual || s_TcUiTagHud || s_TcUiTagInput ||
				     s_TcUiTagChat || s_TcUiTagAudio || s_TcUiTagAutomation ||
				     s_TcUiTagSocial || s_TcUiTagCamera || s_TcUiTagGameplay ||
				     s_TcUiTagMisc;

		const char *pSearch = s_SearchInput.GetString();

		auto IsEffectiveDefaultVar = [&](const SConfigVariable *p) -> bool {
			if(p->m_Type == SConfigVariable::VAR_INT)
			{
				const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(p);
				auto Iter = s_StagedInts.find(p);
				int v = Iter != s_StagedInts.end() ? Iter->second.m_Value : *pInt->m_pVariable;
				return v == pInt->m_Default;
			}
			if(p->m_Type == SConfigVariable::VAR_STRING)
			{
				const SStringConfigVariable *pString = static_cast<const SStringConfigVariable *>(p);
				auto Iter = s_StagedStrs.find(p);
				const char *v = Iter != s_StagedStrs.end() ? Iter->second.m_Value.c_str() : pString->m_pStr;
				return str_comp(v, pString->m_pDefault) == 0;
			}
			if(p->m_Type == SConfigVariable::VAR_COLOR)
			{
				const SColorConfigVariable *pColor = static_cast<const SColorConfigVariable *>(p);
				auto Iter = s_StagedCols.find(p);
				unsigned v = Iter != s_StagedCols.end() ? Iter->second.m_Value : *pColor->m_pVariable;
				return v == pColor->m_Default;
			}
			return true;
		};

		auto BuildLocalizedConfigHelpText = [](const SConfigVariable *pVar) {
			const char *pHelpKey = pVar->m_pHelpLocalizeKey ? pVar->m_pHelpLocalizeKey : (pVar->m_pHelp ? pVar->m_pHelp : "");
			const char *pHelpText = pHelpKey[0] != '\0' ? Localize(pHelpKey) : "";
			char aHelp[512];
			if(pVar->m_Type == SConfigVariable::VAR_INT)
			{
				const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
				if(pInt->m_Min == pInt->m_Max)
					str_format(aHelp, sizeof(aHelp), "%s (%s: %d)", pHelpText, Localize("default"), pInt->m_Default);
				else if(pInt->m_Max == 0)
					str_format(aHelp, sizeof(aHelp), "%s (%s: %d, %s: %d)", pHelpText, Localize("default"), pInt->m_Default, Localize("minimum"), pInt->m_Min);
				else
					str_format(aHelp, sizeof(aHelp), "%s (%s: %d, %s: %d, %s: %d)", pHelpText, Localize("default"), pInt->m_Default, Localize("minimum"), pInt->m_Min, Localize("maximum"), pInt->m_Max);
			}
			else if(pVar->m_Type == SConfigVariable::VAR_COLOR)
			{
				const SColorConfigVariable *pColor = static_cast<const SColorConfigVariable *>(pVar);
				str_format(aHelp, sizeof(aHelp), "%s (%s: $%0*X)", pHelpText, Localize("default"), pColor->m_Alpha ? 8 : 6, color_cast<ColorRGBA>(ColorHSLA(pColor->m_Default, pColor->m_Alpha)).Pack(pColor->m_Alpha));
			}
			else if(pVar->m_Type == SConfigVariable::VAR_STRING)
			{
				const SStringConfigVariable *pString = static_cast<const SStringConfigVariable *>(pVar);
				str_format(aHelp, sizeof(aHelp), "%s (%s: \"%s\", %s: %d)", pHelpText, Localize("default"), pString->m_pDefault, Localize("maximum"), (int)pString->m_MaxSize - 1);
			}
			else
				str_copy(aHelp, pHelpText);
			return std::string(aHelp);
		};

		unsigned int SelectedTagMask = 0;
		for(int Tag = 1; Tag < static_cast<int>(EConfigTag::NUM_TAGS); ++Tag)
		{
			const EConfigTag TagValue = static_cast<EConfigTag>(Tag);
			if(TagEnabled(TagValue))
				SelectedTagMask |= 1u << static_cast<unsigned int>(TagValue);
		}
		const unsigned int MiscTagMask = 1u << static_cast<unsigned int>(EConfigTag::MISC);
		const int DomainMask = (g_Config.m_QmUiShowDDNet != 0 ? 1 : 0) |
				       (g_Config.m_QmUiShowTClient != 0 ? 2 : 0) |
				       (g_Config.m_QmUiShowQm != 0 ? 4 : 0);
		static std::vector<const SConfigVariable *> s_vFilteredConfigs;
		static std::string s_CachedConfigSearch;
		static int s_CachedConfigDomainMask = -1;
		static int s_CachedConfigChangesCount = -1;
		static int s_CachedConfigOnlyModified = -1;
		static unsigned int s_CachedConfigTagMask = 0;
		static size_t s_CachedConfigVarCount = 0;
		static uint64_t s_CachedConfigLanguageHash = 0;
		const uint64_t ConfigLanguageHash = str_quickhash(g_Config.m_ClLanguagefile);
		if(s_CachedConfigSearch != (pSearch ? pSearch : "") ||
			s_CachedConfigDomainMask != DomainMask ||
			s_CachedConfigChangesCount != ChangesCount ||
			s_CachedConfigOnlyModified != g_Config.m_QmUiOnlyModified ||
			s_CachedConfigTagMask != SelectedTagMask ||
			s_CachedConfigVarCount != s_vAllClientVars.size() ||
			s_CachedConfigLanguageHash != ConfigLanguageHash)
		{
			s_vFilteredConfigs.clear();
			s_vFilteredConfigs.reserve(s_vAllClientVars.size());
			for(size_t i = 0; i < s_vAllClientVars.size(); ++i)
			{
				const SConfigVariable *pVar = s_vAllClientVars[i];
				if(!SourceEnabled(GetConfigSource(pVar)))
					continue;
				if(g_Config.m_QmUiOnlyModified && IsEffectiveDefaultVar(pVar))
					continue;
				if(pSearch && pSearch[0])
				{
					const char *pName = pVar->m_pScriptName ? pVar->m_pScriptName : "";
					const char *pHelp = pVar->m_pHelp ? pVar->m_pHelp : "";
					std::string LocalizedHelp = BuildLocalizedConfigHelpText(pVar);
					if(!str_find_nocase(pName, pSearch) && !str_find_nocase(pHelp, pSearch) && !str_find_nocase(LocalizedHelp.c_str(), pSearch))
						continue;
				}
				if(AnyTagEnabled)
				{
					const unsigned int VarTagMask = s_vAllClientVarTagMasks[i];
					if((VarTagMask & SelectedTagMask) == 0 && !(VarTagMask == 0 && (SelectedTagMask & MiscTagMask) != 0))
						continue;
				}
				s_vFilteredConfigs.push_back(pVar);
			}
			s_CachedConfigSearch = pSearch ? pSearch : "";
			s_CachedConfigDomainMask = DomainMask;
			s_CachedConfigChangesCount = ChangesCount;
			s_CachedConfigOnlyModified = g_Config.m_QmUiOnlyModified;
			s_CachedConfigTagMask = SelectedTagMask;
			s_CachedConfigVarCount = s_vAllClientVars.size();
			s_CachedConfigLanguageHash = ConfigLanguageHash;
		}
		const std::vector<const SConfigVariable *> &vpFiltered = s_vFilteredConfigs;

		static CScrollRegion s_ConfigListScrollRegion;
		static CScrollRegion s_ConfigListReadOnlyScrollRegion;
		CScrollRegion &ConfigListScrollRegion = ReadOnly ? s_ConfigListReadOnlyScrollRegion : s_ConfigListScrollRegion;
		vec2 ScrollOffset(0.0f, 0.0f);
		SQmScrollRequest ConfigListScrollRequest;
		ConfigListScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;
		const float ConfigInlineMinWidth = ResolveSettingsInlineRowMinimumWidth(ConfigMetrics.m_LabelWidth + 2.0f * ConfigMetrics.m_ButtonHeight, ConfigMetrics.m_SectionGap, 1);
		const bool StackedConfigRows = ListArea.w < ConfigInlineMinWidth;
		const float ConfigHelpHeight = std::max(MarginSmall, FontSize - 2.0f);
		const SSettingsConfigRowMetrics ConfigRowMetrics = ResolveSettingsConfigRowMetrics(g_Config.m_QmUiCompactList != 0, StackedConfigRows, LineSize, MarginSmall, ConfigHelpHeight, ColorPickerLineSize, ConfigMetrics.m_LineSpacing);
		const float ConfigRowHeight = ConfigRowMetrics.m_RowHeight;
		ConfigListScrollRequest.m_RowExtent = ConfigRowHeight;
		CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(QmResolveScrollPolicy(ConfigListScrollRequest, UiScale, 0.0f));
		CPerfTimer ListTimer;
		static float s_PrevConfigsScrollY = 0.0f;
		static float s_PrevConfigsReadOnlyScrollY = 0.0f;
		float &PrevConfigsScrollY = ReadOnly ? s_PrevConfigsReadOnlyScrollY : s_PrevConfigsScrollY;
		SSettingsScrollRegionFrame ScrollFrame = BeginSettingsScrollRegion(ConfigListScrollRegion, &ListArea, ScrollParams, PrevConfigsScrollY);
		ScrollOffset = ScrollFrame.m_BeginOffset;

		ListArea.y += ScrollOffset.y;
		// 右侧留出与行距同宽的空带，列表不贴到滚动条上。
		ListArea.VSplitRight(RowInset, &ListArea, nullptr);
		CUIRect Content = ListArea;

		auto SourceName = [](EConfigSource Source) {
			switch(Source)
			{
			case EConfigSource::DDNET: return "DDNet";
			case EConfigSource::TCLIENT: return "TClient";
			case EConfigSource::QM: return "QmClient";
			default: return "Other";
			}
		};

		bool HasCurrentSource = false;
		EConfigSource CurrentSource = EConfigSource::DDNET;
		for(const SConfigVariable *pVar : vpFiltered)
		{
			const EConfigSource Source = GetConfigSource(pVar);
			if(!HasCurrentSource || Source != CurrentSource)
			{
				HasCurrentSource = true;
				CurrentSource = Source;
				CUIRect Header;
				Content.HSplitTop(HeadlineHeight, &Header, &Content);
				if(ConfigListScrollRegion.AddRect(Header))
					Ui()->DoLabel(&Header, SourceName(CurrentSource), HeadlineFontSize, TEXTALIGN_ML);
				Content.HSplitTop(MarginSmall, nullptr, &Content);
			}

			CUIRect RowItem;
			const float RowHeight = ConfigRowHeight;
			ConsumeSettingsMeasuredRow(Content, RowHeight, &RowItem);
			Content.HSplitTop(MarginExtraSmall, nullptr, &Content);
			const bool Visible = ConfigListScrollRegion.AddRect(RowItem);
			if(!Visible)
				continue;

			const bool Modified = !IsEffectiveDefaultVar(pVar);
			const ColorRGBA BgModified = ColorRGBA(1.0f, 0.8f, 0.0f, 0.15f);
			const ColorRGBA BgNormal = ui_token::color::PREVIEW_BACKDROP;
			RowItem.Draw(Modified ? BgModified : BgNormal, IGraphics::CORNER_ALL, ui_token::radius::BASE);

			CUIRect RowContent;
			RowItem.Margin(RowInset, &RowContent);

			CUIRect TopLine, Below;
			IUiContext TClientConfigTextInputCtx;
			TClientConfigTextInputCtx.m_pUi = Ui();
			TClientConfigTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();
			TClientConfigTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();
			TClientConfigTextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_config_text_inputs");
			TClientConfigTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();
			if(g_Config.m_QmUiCompactList)
			{
				const float UsedHeight = ConfigRowMetrics.m_ControlBlockHeight;
				TopLine = RowContent;
				TopLine.h = UsedHeight;
				TopLine.y = round_to_int(RowContent.y + (RowContent.h - UsedHeight) / 2.0f);
				Below = RowContent;
			}
			else
			{
				RowContent.HSplitTop(ConfigRowMetrics.m_ControlBlockHeight, &TopLine, &Below);
			}
			CUIRect NameLine, Right;
			if(StackedConfigRows)
			{
				TopLine.HSplitTop(LineSize, &NameLine, &TopLine);
				TopLine.HSplitTop(MarginSmall, nullptr, &TopLine);
				TopLine.HSplitTop(ConfigRowMetrics.m_ControlLineHeight, &Right, nullptr);
			}
			else
				TopLine.VSplitRight(std::clamp(TopLine.w * 0.46f, 170.0f, 320.0f), &NameLine, &Right);
			NameLine.VSplitLeft(std::min(LabelIndent, NameLine.w), nullptr, &NameLine);

			Ui()->DoLabel(&NameLine, pVar->m_pScriptName, FontSize, TEXTALIGN_ML);

			CUIRect Controls, ResetRect;
			Right.VSplitRight(std::min(100.0f, std::max(0.0f, Right.w * 0.25f)), &Controls, &ResetRect);
			Controls.h = LineSize;
			Controls.y = Right.y + (Right.h - LineSize) / 2.0f;
			ResetRect.h = LineSize;
			ResetRect.y = Controls.y;
			Controls.VSplitRight(MarginSmall, &Controls, nullptr);

			if(!g_Config.m_QmUiCompactList)
			{
				CUIRect Help;
				Below.HSplitTop(ConfigRowMetrics.m_HelpGap, nullptr, &Below);
				Help = Below;
				Help.VSplitLeft(LabelIndent, nullptr, &Help);
				const std::string LocalizedHelp = BuildLocalizedConfigHelpText(pVar);
				Ui()->DoLabel(&Help, LocalizedHelp.c_str(), ConfigHelpHeight, TEXTALIGN_ML);
			}

			static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ResetBtns;
			if(Modified && pVar->m_Type != SConfigVariable::VAR_COLOR)
			{
				CButtonContainer &ResetBtn = s_ResetBtns[pVar];
				if(DoTClientSettingsButton_Menu(&ResetBtn, "tclient-config-reset", Localize("Reset"), 0, &ResetRect))
				{
					if(pVar->m_Type == SConfigVariable::VAR_INT)
					{
						const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
						s_StagedInts[pVar] = {pInt->m_Default};
					}
					else if(pVar->m_Type == SConfigVariable::VAR_STRING)
					{
						const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(pVar);
						s_StagedStrs[pVar] = {std::string(pStr->m_pDefault)};
					}
				}
			}

			if(pVar->m_Type == SConfigVariable::VAR_INT)
			{
				const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
				// treat 0 1 ints as checkboxes
				Ui()->DoConfigTooltip(pVar, &Right, pInt->m_pVariable);
				GameClient()->m_Tooltips.DoToolTipForRect(pVar, &Right, BuildLocalizedConfigHelpText(pVar).c_str());
				if(pInt->m_Min == 0 && pInt->m_Max == 1)
				{
					const auto StagedInt = s_StagedInts.find(pVar);
					const int Effective = StagedInt != s_StagedInts.end() ? StagedInt->second.m_Value : *pInt->m_pVariable;
					if(DoButton_CheckBox(pVar, "", Effective, &Controls))
					{
						const int NewVal = Effective ? 0 : 1;
						if(NewVal == *pInt->m_pVariable)
							s_StagedInts.erase(pVar);
						else
							s_StagedInts[pVar] = {NewVal};
					}
				}
				else
				{
					SIntState &State = IntInputs[pVar];
					const auto StagedInt = s_StagedInts.find(pVar);
					const int Effective = StagedInt != s_StagedInts.end() ? StagedInt->second.m_Value : *pInt->m_pVariable;
					if(!State.m_Inited)
					{
						State.m_Input.SetInteger(Effective);
						State.m_LastValue = Effective;
						State.m_Inited = true;
					}
					else if(!State.m_Input.IsActive() && State.m_LastValue != Effective)
					{
						State.m_Input.SetInteger(Effective);
						State.m_LastValue = Effective;
					}

					CUIRect InputBox, Dummy;
					Controls.VSplitLeft(60.0f, &InputBox, &Dummy);

					if(ui_widget::InputField(TClientConfigTextInputCtx, &State.m_Input, InputBox, nullptr, EditBoxFontSize))
					{
						int NewVal = State.m_Input.GetInteger();
						bool InRange = true;
						if(pInt->m_Min != pInt->m_Max)
						{
							if(NewVal < pInt->m_Min)
								InRange = false;
							if(pInt->m_Max != 0 && NewVal > pInt->m_Max)
								InRange = false;
						}
						if(InRange && NewVal != State.m_LastValue)
						{
							if(NewVal == *pInt->m_pVariable)
								s_StagedInts.erase(pVar);
							else
								s_StagedInts[pVar] = {NewVal};
							State.m_LastValue = NewVal;
						}
					}
				}
			}
			else if(pVar->m_Type == SConfigVariable::VAR_STRING)
			{
				const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(pVar);
				SStrState &State = StrInputs[pVar];
				Ui()->DoConfigTooltip(pVar, &Right, pStr->m_pStr);
				GameClient()->m_Tooltips.DoToolTipForRect(pVar, &Right, BuildLocalizedConfigHelpText(pVar).c_str());
				const auto StagedStr = s_StagedStrs.find(pVar);
				const char *Effective = StagedStr != s_StagedStrs.end() ? StagedStr->second.m_Value.c_str() : pStr->m_pStr;
				if(!State.m_Inited)
				{
					State.m_Input.Set(Effective);
					State.m_Inited = true;
				}
				else if(!State.m_Input.IsActive())
				{
					if(str_comp(State.m_Input.GetString(), Effective) != 0)
						State.m_Input.Set(Effective);
				}

				if(ui_widget::InputField(TClientConfigTextInputCtx, &State.m_Input, Controls, nullptr, EditBoxFontSize))
				{
					const char *NewVal = State.m_Input.GetString();
					if(str_comp(NewVal, pStr->m_pStr) == 0)
						s_StagedStrs.erase(pVar);
					else
						s_StagedStrs[pVar] = {std::string(NewVal)};
				}
			}
			else if(pVar->m_Type == SConfigVariable::VAR_COLOR)
			{
				const SColorConfigVariable *pCol = static_cast<const SColorConfigVariable *>(pVar);
				CUIRect ColorRect;
				Ui()->DoConfigTooltip(pVar, &Right, pCol->m_pVariable);
				GameClient()->m_Tooltips.DoToolTipForRect(pVar, &Right, BuildLocalizedConfigHelpText(pVar).c_str());
				ColorRect.x = Controls.x;
				ColorRect.h = ColorPickerLineSize;
				ColorRect.y = Right.y + (Right.h - ColorPickerLineSize) / 2.0f;
				ColorRect.w = ColorPickerLineSize + ui_token::spacing::SM + 60.0f;
				const ColorRGBA DefaultColor = color_cast<ColorRGBA>(ColorHSLA(pCol->m_Default, true).UnclampLighting(pCol->m_DarkestLighting));
				static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ColorResetIds;
				CButtonContainer &ResetId = s_ColorResetIds[pVar];

				SColState &ColState = ColInputs[pVar];
				const auto StagedCol = s_StagedCols.find(pVar);
				unsigned Effective = StagedCol != s_StagedCols.end() ? StagedCol->second.m_Value : *pCol->m_pVariable;
				if(!ColState.m_Inited)
				{
					ColState.m_Working = Effective;
					ColState.m_LastValue = Effective;
					ColState.m_Inited = true;
				}
				else
				{
					const bool EditingThis = Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == &ColState.m_Working;
					if(!EditingThis && ColState.m_Working != Effective)
					{
						ColState.m_Working = Effective;
						ColState.m_LastValue = Effective;
					}
				}

				DoLine_ColorPicker(&ResetId, CurrentSettingsContentMetrics(), &ColorRect, "", &ColState.m_Working, DefaultColor, false, nullptr, pCol->m_Alpha);
				if(ColState.m_Working != Effective)
				{
					if(ColState.m_Working == *pCol->m_pVariable)
						s_StagedCols.erase(pVar);
					else
						s_StagedCols[pVar] = {ColState.m_Working};
					ColState.m_LastValue = ColState.m_Working;
				}
			}
		}

		CUIRect EndPad{Content.x, Content.y, Content.w, RowInset};
		FinishSettingsScrollRegion(ConfigListScrollRegion, ScrollFrame, &EndPad);
		PrevConfigsScrollY = ScrollFrame.m_FinalOffsetY;
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "filtered=%d scroll_y=%.1f", (int)vpFiltered.size(), ScrollFrame.m_FinalOffsetY);
		LogTClientPerfStageEx("tclient_configs", "list", ETClientSettingsPerfStage::STATIC_LAYER, ListTimer.ElapsedMs(), false, aExtra);
	};

	const auto ConfigListViewportHeightForWidth = [ConfigMetrics](float ContentWidth) {
		const float ConfigInlineMinWidth = ResolveSettingsInlineRowMinimumWidth(ConfigMetrics.m_LabelWidth + 2.0f * ConfigMetrics.m_ButtonHeight, ConfigMetrics.m_SectionGap, 1);
		const bool StackedRows = ContentWidth < ConfigInlineMinWidth;
		const float HelpHeight = std::max(MarginSmall, FontSize - 2.0f);
		const SSettingsConfigRowMetrics RowMetrics = ResolveSettingsConfigRowMetrics(g_Config.m_QmUiCompactList != 0, StackedRows, ConfigMetrics.m_LineHeight, ConfigMetrics.m_LineSpacing, HelpHeight, ConfigMetrics.m_ButtonHeight, ConfigMetrics.m_LineSpacing);
		return std::max(RowMetrics.m_RowHeight * 2.0f, ContentWidth * 0.52f);
	};
	const auto RenderConfigCard = [&](CUIRect &Content) {
		CSettingsContentLayoutFlow Flow(Content);
		CUIRect Row;
		// 整页从上到下只有三块：工具行（搜索 + 应用/清除/更改数）、筛选按钮组、配置项列表。
		// 小节标题不再渲染，块与块之间的间距统一取内容 metrics 的区块间距。
		if(Flow.Control("tclient-config-toolbar", ConfigMetrics.m_LineHeight, Row))
			RenderToolbarRow(Row);
		Flow.Space(ConfigMetrics.m_SectionGap);
		RenderFilters(Content);
		// 筛选按钮组的最后一排自带一个行距，这里只补到区块间距的量，
		// 让「工具行 → 筛选组」和「筛选组 → 列表」两处间距在视觉上一致。
		Flow.Space(ConfigMetrics.m_SectionGap - ConfigMetrics.m_LineSpacing);
		if(Flow.Control("tclient-config-list", ConfigListViewportHeightForWidth(Content.w), Row))
			RenderList(Row);
	};
	const uint64_t CardRevision = static_cast<uint64_t>(g_Config.m_QmUiCompactList != 0);
	if(Pass == qm_card_catalog::ETClientCardPass::REVISION)
		return {0.0f, CardRevision};
	if(str_comp(pStableId, "deck:tclient-configs-actions") == 0)
	{
		const float SavedY = Content.y;
		RenderConfigCard(Content);
		return {Content.y - SavedY, CardRevision};
	}
	return {0.0f, CardRevision};
}
