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
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>

using namespace qm_tclient_cards;
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsFontSelection.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiSurfaceText.h>
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

#include <SDL_audio.h>

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

using namespace qm_tclient_cards;

int CMenus::DoTClientSettingsButton_CheckBox(const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect)
{
	return DoSettingsButton_CheckBox(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, pId, pTextId, pText, Checked, pRect, TClientFixedLabelProperties(TCLIENT_BODY_FONT_SIZE, pRect->w), true, TCLIENT_BODY_FONT_SIZE);
}

int CMenus::DoTClientSettingsButton_Menu(CButtonContainer *pButtonContainer, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, int Flags, int Corners, float Rounding)
{
	return DoSettingsButton_Menu(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, pButtonContainer, pTextId, pText, Checked, pRect, Flags, Corners, Rounding, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), 0.0f, TCLIENT_BODY_FONT_SIZE);
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
typedef struct
{
	const char *m_pName;
	const char *m_pCommand;
	int m_KeyId;
	int m_ModifierCombination;
} CKeyInfo;

using namespace FontIcons;

namespace
{
	bool LoadTClientOrderFromGlobalCardModel(const char *pConfig, std::vector<std::string> &vLeftOrder, std::vector<std::string> &vRightOrder)
	{
		if(pConfig == nullptr || pConfig[0] == '\0')
			return false;

		qm_card_order::CModel Model;
		if(!Model.LoadExplicit(pConfig, qm_card_registry::BuildDefaultEntries()))
			return false;

		std::vector<std::string> vParsedLeftOrder = Model.StableIdOrder("tclient:", "tclient", 1);
		std::vector<std::string> vParsedRightOrder = Model.StableIdOrder("tclient:", "tclient", 2);
		if(vParsedLeftOrder.empty() && vParsedRightOrder.empty())
			return false;

		vLeftOrder = std::move(vParsedLeftOrder);
		vRightOrder = std::move(vParsedRightOrder);
		return true;
	}

	void LoadTClientOrderFromLegacyCardOrder(const char *pConfig, std::vector<std::string> &vLeftOrder, std::vector<std::string> &vRightOrder)
	{
		if(pConfig == nullptr || pConfig[0] == '\0')
			return;
		vLeftOrder.clear();
		vRightOrder.clear();
		std::vector<std::pair<int, std::string>> vLeftEntries;
		std::vector<std::pair<int, std::string>> vRightEntries;
		const char *p = pConfig;
		char aToken[128];
		while((p = str_next_token(p, ";", aToken, sizeof(aToken))) != nullptr)
		{
			if(aToken[0] == '\0')
				continue;
			char aId[80];
			char aCol[16];
			char aOrder[16];
			const char *pLastColon = nullptr;
			for(const char *pIt = aToken; *pIt != '\0'; ++pIt)
			{
				if(*pIt == ':')
					pLastColon = pIt;
			}
			if(pLastColon == nullptr)
				continue;
			const char *pSecondLastColon = nullptr;
			for(const char *pIt = aToken; pIt < pLastColon; ++pIt)
			{
				if(*pIt == ':')
					pSecondLastColon = pIt;
			}
			if(pSecondLastColon == nullptr)
				continue;
			const int IdLen = (int)(pSecondLastColon - aToken);
			const int ColumnLen = (int)(pLastColon - pSecondLastColon - 1);
			if(IdLen <= 0 || IdLen >= (int)sizeof(aId) || ColumnLen <= 0 || ColumnLen >= (int)sizeof(aCol))
				continue;
			str_copy(aId, aToken, IdLen + 1);
			str_copy(aCol, pSecondLastColon + 1, ColumnLen + 1);
			str_copy(aOrder, pLastColon + 1, sizeof(aOrder));
			int Col = 0;
			if(!str_toint(aCol, &Col))
				continue;
			int Order = 0;
			if(!str_toint(aOrder, &Order) || Order < 0)
				continue;
			if(Col == 0)
				vLeftEntries.emplace_back(Order, aId);
			else if(Col == 1)
				vRightEntries.emplace_back(Order, aId);
		}
		const auto OrderLess = [](const auto &a, const auto &b) {
			return a.first < b.first;
		};
		std::stable_sort(vLeftEntries.begin(), vLeftEntries.end(), OrderLess);
		std::stable_sort(vRightEntries.begin(), vRightEntries.end(), OrderLess);
		for(const auto &Entry : vLeftEntries)
			vLeftOrder.push_back(Entry.second);
		for(const auto &Entry : vRightEntries)
			vRightOrder.push_back(Entry.second);
	}

	bool SerializeMergedTClientGlobalCardOrder(const char *pExistingGlobalOrder, const std::vector<std::string> &vLeftOrder, const std::vector<std::string> &vRightOrder, char *pOut, int OutSize)
	{
		std::vector<qm_card_order::SEntry> vEntries;
		vEntries.reserve(vLeftOrder.size() + vRightOrder.size());
		auto AppendOrder = [&](const std::vector<std::string> &vOrder, int Column) {
			for(size_t i = 0; i < vOrder.size(); ++i)
			{
				if(vOrder[i].empty())
					continue;
				vEntries.push_back({vOrder[i].c_str(), "tclient", Column, (int)i});
			}
		};
		AppendOrder(vLeftOrder, 1);
		AppendOrder(vRightOrder, 2);
		return qm_card_order::SerializeMergedReplacingPrefix(pExistingGlobalOrder, "tclient:", vEntries, pOut, OutSize);
	}
}

[[maybe_unused]] static float s_Time = 0.0f;
[[maybe_unused]] static bool s_StartedTime = false;

extern std::unordered_map<std::string, CBindSlot> g_CommandBindCache;
extern bool g_CommandBindCacheInitialized;

namespace
{
	int CanonicalizePersistedTClientTab(int Tab)
	{
		if(Tab < 0 || Tab >= NUMBER_OF_TCLIENT_TABS)
			return TCLIENT_TAB_SETTINGS;
		return Tab;
	}

	int CanonicalizePersistedQmClientTab(int Tab)
	{
		// 贡献者子页签并入顶层贡献者页后枚举值只作占位；持久化里的旧值回落到首个子页签。
		if(Tab == CMenus::QMCLIENT_SETTINGS_TAB_CONTRIBUTORS)
			return CMenus::QMCLIENT_SETTINGS_TAB_VISUAL;
		if(Tab < 0 || Tab >= CMenus::NUMBER_OF_QMCLIENT_SETTINGS_TABS)
			return CMenus::QMCLIENT_SETTINGS_TAB_VISUAL;
		return Tab;
	}

	static CSectionLoader s_VisualFontLoader;
	static CSectionLoader s_RightSectionLoader;

	uint64_t HashTClientSettingsConfig()
	{
		uint64_t Hash = 1469598103934665603ull;
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) \
	if(str_startswith(#ScriptName, "qm_")) \
		Hash = HashValueFnv1a64(Hash, g_Config.m_##Name);
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) \
	if(str_startswith(#ScriptName, "qm_")) \
		Hash = HashValueFnv1a64(Hash, g_Config.m_##Name);
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) \
	if(str_startswith(#ScriptName, "qm_")) \
		Hash = HashStringFnv1a64(Hash, g_Config.m_##Name);
#define SET_CONFIG_DOMAIN(ConfigDomain) ;
#include <engine/shared/config_includes.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
#undef SET_CONFIG_DOMAIN
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmAutoMargin);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmJellyTee);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmJellyTeeDuration);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmJellyTeeOthers);
		Hash = HashValueFnv1a64(Hash, g_Config.m_QmJellyTeeStrength);
		return Hash;
	}

	SSettingsSectionCacheRuntimeKey MakeSettingsSectionRuntimeKey(CUIRect View, IGraphics *pGraphics, bool IncludeConfigHash = true)
	{
		SSettingsSectionCacheRuntimeKey RuntimeKey;
		RuntimeKey.m_ViewportWidth = SettingsRuntimeCacheDimensionKey(View.w);
		RuntimeKey.m_ViewportHeight = SettingsRuntimeCacheDimensionKey(View.h);
		RuntimeKey.m_ConfigHash = IncludeConfigHash ? HashTClientSettingsConfig() : 0;
		RuntimeKey.m_LanguageHash = str_quickhash(g_Config.m_ClLanguagefile);
		// 分类字体（中文/图标符号）与各分类可变字重改变文本外观，需一并计入字体缓存键。
		uint64_t FontHash = HashStringFnv1a64(1469598103934665603ull, g_Config.m_QmCustomFont);
		FontHash = HashStringFnv1a64(FontHash, g_Config.m_QmCustomFontCjk);
		FontHash = HashStringFnv1a64(FontHash, g_Config.m_QmCustomFontIcons);
		FontHash = HashValueFnv1a64(FontHash, (uint64_t)g_Config.m_QmCustomFontWeight);
		FontHash = HashValueFnv1a64(FontHash, (uint64_t)g_Config.m_QmCustomFontWeightCjk);
		RuntimeKey.m_FontHash = FontHash;
		RuntimeKey.m_BackendHash = str_quickhash(g_Config.m_GfxBackend);
		if(pGraphics)
		{
			RuntimeKey.m_UiScale = SettingsRuntimeCachePositiveRoundedKey(pGraphics->ScreenHiDPIScale() * std::clamp(g_Config.m_QmUiScale, 50, 200));
			RuntimeKey.m_WindowHash = HashValueFnv1a64(1469598103934665603ull, pGraphics->WindowWidth());
			RuntimeKey.m_WindowHash = HashValueFnv1a64(RuntimeKey.m_WindowHash, pGraphics->WindowHeight());
		}
		return RuntimeKey;
	}

	SSettingsRuntimeCacheKey ToSettingsRuntimeCacheKey(const SSettingsSectionCacheRuntimeKey &RuntimeKey)
	{
		SSettingsRuntimeCacheKey Key;
		Key.m_LanguageHash = RuntimeKey.m_LanguageHash;
		Key.m_FontGeneration = RuntimeKey.m_FontHash;
		Key.m_BackendGeneration = RuntimeKey.m_BackendHash;
		Key.m_WindowWidth = RuntimeKey.m_ViewportWidth;
		Key.m_WindowHeight = RuntimeKey.m_ViewportHeight;
		Key.m_UiScale = RuntimeKey.m_UiScale;
		Key.m_ConfigHash = RuntimeKey.m_ConfigHash;
		return Key;
	}

}

int CMenus::DoTClientSettingsButton_CheckBoxAutoVMarginAndSet(const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float VMargin)
{
	return DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_TCLIENT, m_TClientSettingsTab, pId, pTextId, pText, pValue, pRect, VMargin, 0.0f, FontSize);
}

static constexpr const char *SETTINGS_RUNTIME_CACHE_METADATA_FILE = "qmclient/settings_section_cache_metadata.cfg";

CUIRect TClientSettingsContentView(CUIRect MainView, CUIRect *pTabBar = nullptr)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
	const SSettingsSubTabLayoutFrame SubTabs = ResolveSettingsSubTabLayout(MainView, Metrics.m_UiScale);
	if(pTabBar != nullptr)
		*pTabBar = SubTabs.m_TabBarRect;
	return SubTabs.m_ContentRect;
}

void CMenus::BuildTClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab)
{
	Tab = CanonicalizePersistedTClientTab(Tab);
	const int PreviousTab = m_TClientSettingsTab;
	const int PreviousSettingsPage = g_Config.m_UiSettingsPage;
	const bool PreviousCollecting = m_MenuTextPlanCollecting;
	std::vector<SMenuTextPlanItem> *pPreviousCollection = m_pMenuTextPlanCollection;
	const bool PreviousPendingActive = m_MenuTextPlanPendingActive;
	SMenuTextPlanItem PreviousPendingItem;
	if(PreviousPendingActive)
		PreviousPendingItem = m_MenuTextPlanPendingItem;

	g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;
	m_TClientSettingsTab = Tab;
	m_MenuTextPlanCollecting = true;
	m_pMenuTextPlanCollection = &vItems;
	m_MenuTextPlanPendingActive = false;
	Ui()->BeginRenderOnly();
	RenderSettings(MainView);
	Ui()->EndRenderOnly();
	if(PreviousPendingActive)
		m_MenuTextPlanPendingItem = PreviousPendingItem;
	m_MenuTextPlanPendingActive = PreviousPendingActive;
	m_pMenuTextPlanCollection = pPreviousCollection;
	m_MenuTextPlanCollecting = PreviousCollecting;
	m_TClientSettingsTab = PreviousTab;
	g_Config.m_UiSettingsPage = PreviousSettingsPage;
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SAutoReplyRulePlain
{
	std::string m_Keywords;
	std::string m_Reply;
	bool m_AutoRename = false;
	bool m_Regex = false;
};

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SAutoReplyRuleInputRow
{
	char m_aTrigger[512] = "";
	char m_aReply[256] = "";
	int m_AutoRename = 0;
	int m_Regex = 0;
	CLineInput m_TriggerInput;
	CLineInput m_ReplyInput;

	SAutoReplyRuleInputRow()
	{
		m_TriggerInput.SetBuffer(m_aTrigger, sizeof(m_aTrigger));
		m_ReplyInput.SetBuffer(m_aReply, sizeof(m_aReply));
	}
};

static char *ParseAutoReplyRulePrefixes(char *pLine, bool &OutAutoRename, bool &OutRegex, bool &OutHasExplicitRenameFlag, bool &OutHasExplicitRegexFlag)
{
	OutAutoRename = false;
	OutRegex = false;
	OutHasExplicitRenameFlag = false;
	OutHasExplicitRegexFlag = false;

	char *pTrimmedLine = (char *)str_utf8_skip_whitespaces(pLine);
	while(true)
	{
		const char *pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[rename]");
		if(!pAfterPrefix)
			pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[r]");
		if(pAfterPrefix)
		{
			OutAutoRename = true;
			OutHasExplicitRenameFlag = true;
			pTrimmedLine = (char *)str_utf8_skip_whitespaces(pAfterPrefix);
			continue;
		}

		pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[regex]");
		if(!pAfterPrefix)
			pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[re]");
		if(!pAfterPrefix)
			pAfterPrefix = str_startswith_nocase(pTrimmedLine, "[rx]");
		if(pAfterPrefix)
		{
			OutRegex = true;
			OutHasExplicitRegexFlag = true;
			pTrimmedLine = (char *)str_utf8_skip_whitespaces(pAfterPrefix);
			continue;
		}

		break;
	}

	return pTrimmedLine;
}

static bool CopyTrimmedString(const char *pSrc, char *pOut, size_t OutSize)
{
	pOut[0] = '\0';
	if(!pSrc)
		return false;

	char aBuf[1024];
	str_copy(aBuf, pSrc, sizeof(aBuf));
	char *pTrimmed = (char *)str_utf8_skip_whitespaces(aBuf);
	str_utf8_trim_right(pTrimmed);
	str_copy(pOut, pTrimmed, OutSize);
	return pOut[0] != '\0';
}

[[maybe_unused]] static std::unique_ptr<SAutoReplyRuleInputRow> CreateAutoReplyRuleInputRow(const char *pTrigger = "", const char *pReply = "", bool AutoRename = false, bool Regex = false)
{
	auto pRow = std::make_unique<SAutoReplyRuleInputRow>();
	pRow->m_TriggerInput.Set(pTrigger);
	pRow->m_ReplyInput.Set(pReply);
	pRow->m_AutoRename = AutoRename ? 1 : 0;
	pRow->m_Regex = Regex ? 1 : 0;
	return pRow;
}

[[maybe_unused]] static void ParseAutoReplyRules(const char *pRules, std::vector<SAutoReplyRulePlain> &vOutRules)
{
	vOutRules.clear();
	if(!pRules || pRules[0] == '\0')
		return;

	const char *pCursor = pRules;
	while(*pCursor)
	{
		char aLine[1024];
		int LineLen = 0;
		while(*pCursor && *pCursor != '\n' && *pCursor != '\r')
		{
			if(LineLen < (int)sizeof(aLine) - 1)
				aLine[LineLen++] = *pCursor;
			pCursor++;
		}
		aLine[LineLen] = '\0';

		while(*pCursor == '\n' || *pCursor == '\r')
			pCursor++;

		char *pLine = (char *)str_utf8_skip_whitespaces(aLine);
		str_utf8_trim_right(pLine);
		if(pLine[0] == '\0' || pLine[0] == '#')
			continue;

		bool AutoRename = false;
		bool RegexRule = false;
		bool HasExplicitRenameFlag = false;
		bool HasExplicitRegexFlag = false;
		char *pRuleText = ParseAutoReplyRulePrefixes(pLine, AutoRename, RegexRule, HasExplicitRenameFlag, HasExplicitRegexFlag);
		(void)HasExplicitRenameFlag;
		(void)HasExplicitRegexFlag;

		const char *pArrowConst = str_find(pRuleText, "=>");
		if(!pArrowConst)
			continue;

		char *pArrow = pRuleText + (pArrowConst - pRuleText);
		*pArrow = '\0';
		pArrow += 2;

		char *pKeywords = (char *)str_utf8_skip_whitespaces(pRuleText);
		str_utf8_trim_right(pKeywords);
		char *pReply = (char *)str_utf8_skip_whitespaces(pArrow);
		str_utf8_trim_right(pReply);
		if(pKeywords[0] == '\0' || pReply[0] == '\0')
			continue;

		vOutRules.push_back({pKeywords, pReply, AutoRename, RegexRule});
	}
}

[[maybe_unused]] static bool AutoReplyRowsMatchRules(const std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, const std::vector<SAutoReplyRulePlain> &vRules)
{
	std::vector<SAutoReplyRulePlain> vCompleteRows;
	vCompleteRows.reserve(vRows.size());
	for(const auto &pRow : vRows)
	{
		char aTrigger[512];
		char aReply[256];
		const bool HasTrigger = CopyTrimmedString(pRow->m_TriggerInput.GetString(), aTrigger, sizeof(aTrigger));
		const bool HasReply = CopyTrimmedString(pRow->m_ReplyInput.GetString(), aReply, sizeof(aReply));
		if(!(HasTrigger && HasReply))
			continue;
		vCompleteRows.push_back({aTrigger, aReply, pRow->m_AutoRename != 0, pRow->m_Regex != 0});
	}

	if(vCompleteRows.size() != vRules.size())
		return false;

	for(size_t i = 0; i < vCompleteRows.size(); ++i)
	{
		if(str_comp(vCompleteRows[i].m_Keywords.c_str(), vRules[i].m_Keywords.c_str()) != 0 ||
			str_comp(vCompleteRows[i].m_Reply.c_str(), vRules[i].m_Reply.c_str()) != 0 ||
			vCompleteRows[i].m_AutoRename != vRules[i].m_AutoRename ||
			vCompleteRows[i].m_Regex != vRules[i].m_Regex)
			return false;
	}
	return true;
}

[[maybe_unused]] static bool IsAutoReplyRuleRowHalfFilled(const SAutoReplyRuleInputRow &Row)
{
	char aTrigger[512];
	char aReply[256];
	const bool HasTrigger = CopyTrimmedString(Row.m_TriggerInput.GetString(), aTrigger, sizeof(aTrigger));
	const bool HasReply = CopyTrimmedString(Row.m_ReplyInput.GetString(), aReply, sizeof(aReply));
	return HasTrigger != HasReply;
}

[[maybe_unused]] static void BuildAutoReplyRulesFromRows(const std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, char *pOutRules, size_t OutRulesSize)
{
	pOutRules[0] = '\0';
	for(const auto &pRow : vRows)
	{
		char aTrigger[512];
		char aReply[256];
		const bool HasTrigger = CopyTrimmedString(pRow->m_TriggerInput.GetString(), aTrigger, sizeof(aTrigger));
		const bool HasReply = CopyTrimmedString(pRow->m_ReplyInput.GetString(), aReply, sizeof(aReply));
		if(!(HasTrigger && HasReply))
			continue;

		if(pOutRules[0] != '\0')
			str_append(pOutRules, "\n", OutRulesSize);
		if(pRow->m_AutoRename != 0)
			str_append(pOutRules, "[rename] ", OutRulesSize);
		if(pRow->m_Regex != 0)
			str_append(pOutRules, "[regex] ", OutRulesSize);
		str_append(pOutRules, aTrigger, OutRulesSize);
		str_append(pOutRules, "=>", OutRulesSize);
		str_append(pOutRules, aReply, OutRulesSize);
	}
}

[[maybe_unused]] static float CalcQiaFenInputHeight(ITextRender *pTextRender, const char *pText, float Width, float TextFontSize, float LineSpacing, float MinHeight)
{
	const float VPadding = 2.0f;
	const float LineWidth = maximum(1.0f, Width - VPadding * 2.0f);
	const char *pMeasureText = (pText && pText[0] != '\0') ? pText : " ";
	const STextBoundingBox Box = pTextRender->TextBoundingBox(TextFontSize, pMeasureText, -1, LineWidth, LineSpacing);
	return maximum(MinHeight, Box.m_H + VPadding * 2.0f);
}

bool CMenus::DoLine_KeyReader(CUIRect &View, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pName, const char *pCommand)
{
	CBindSlot Bind(0, 0);
	if(g_CommandBindCacheInitialized)
	{
		const auto It = g_CommandBindCache.find(pCommand);
		if(It != g_CommandBindCache.end())
			Bind = It->second;
	}
	else
	{
		for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT; Mod++)
		{
			for(int KeyId = 0; KeyId < KEY_LAST; KeyId++)
			{
				const char *pBind = GameClient()->m_Binds.Get(KeyId, Mod);
				if(!pBind[0])
					continue;

				if(str_comp(pBind, pCommand) == 0)
				{
					Bind.m_Key = KeyId;
					Bind.m_ModifierMask = Mod;
					break;
				}
			}
		}
	}

	CUIRect KeyButton, KeyLabel;
	View.HSplitTop(LineSize, &KeyButton, &View);
	KeyButton.VSplitMid(&KeyLabel, &KeyButton);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s:", pName);
	DoTClientLabel(Ui(), &KeyLabel, aBuf, FontSize, TEXTALIGN_ML);

	View.HSplitTop(MarginExtraSmall, nullptr, &View);

	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&ReaderButton, &ClearButton, &KeyButton, Bind, false, TCLIENT_BODY_FONT_SIZE);
	if(Result.m_Bind != Bind)
	{
		if(Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Bind.m_Key, "", false, Bind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
		g_CommandBindCacheInitialized = false;
		return true;
	}
	return false;
}

bool CMenus::DoSliderWithScaledValue(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, int Scale, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix)
{
	ui_widget::SNumericFieldState *pState = GetSettingsNumericFieldState(pId);

	ui_widget::SNumericFieldOptions Options;
	Options.m_pLabel = pStr;
	Options.m_pSuffix = pSuffix;
	Options.m_pScale = pScale;
	Options.m_Flags = Flags;
	Options.m_FontSize = CurrentSettingsContentMetrics().m_BodySize;
	Options.m_LabelAlign = TEXTALIGN_ML;
	Options.m_ValueMultiplier = Scale;

	IUiContext InputCtx = SettingsUiContext("tclient_slider_input", Options.m_FontSize / ui_token::font::BODY);

	return ui_widget::NumericField(InputCtx, pState, pId, pOption, Min, Max, *pRect, Options);
}

int CMenus::DoButtonLineSize_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, float ButtonLineSize, bool Fake, const char *pImageName, int Corners, float Rounding, float FontFactor, ColorRGBA Color, float FontSize)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
	CUIRect Text = *pRect;

	ui_widget::SButtonSurfaceOptions Options;
	Options.m_Enabled = !Fake && Checked >= 0;
	Options.m_Selected = Checked > 0;
	Options.m_Corners = Corners;
	Options.m_Radius = Rounding;
	Color = ui_widget::DrawButtonSurface(ui_widget::ControlContext(Ui()), pButtonContainer, *pRect, Options);
	CUiScopedSurfaceText SurfaceText(TextRender(), Color);

	Text.HMargin((Text.h - ButtonLineSize) / 2.0f, &Text);
	Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
	Text.HMargin((Text.h * FontFactor) / 2.0f, &Text);
	const float EffectiveFontSize = FontSize > 0.0f ? FontSize : CurrentSettingsContentMetrics().m_BodySize;
	if(FontSize > 0.0f)
		DoTClientLabel(Ui(), &Text, pText, EffectiveFontSize, TEXTALIGN_MC);
	else
		Ui()->DoLabel(&Text, pText, EffectiveFontSize, TEXTALIGN_MC);

	if(Fake)
		return 0;

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::RenderDevSkin(vec2 RenderPos, float Size, const char *pSkinName, const char *pBackupSkin, bool CustomColors, int FeetColor, int BodyColor, int Emote, bool Rainbow, bool Cute, ColorRGBA ColorFeet, ColorRGBA ColorBody)
{
	bool WhiteFeetTemp = g_Config.m_QmWhiteFeet;
	g_Config.m_QmWhiteFeet = false;

	float DefTick = std::fmod(s_Time, 1.0f);

	CTeeRenderInfo SkinInfo;
	const CSkin *pSkin = GameClient()->m_Skins.Find(pSkinName);
	if(str_comp(pSkin->GetName(), pSkinName) != 0)
		pSkin = GameClient()->m_Skins.Find(pBackupSkin);

	SkinInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
	SkinInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
	SkinInfo.m_SkinMetrics = pSkin->m_Metrics;
	SkinInfo.m_CustomColoredSkin = CustomColors;
	if(SkinInfo.m_CustomColoredSkin)
	{
		SkinInfo.m_ColorBody = color_cast<ColorRGBA>(ColorHSLA(BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		SkinInfo.m_ColorFeet = color_cast<ColorRGBA>(ColorHSLA(FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		if(ColorFeet.a != 0.0f)
		{
			SkinInfo.m_ColorBody = ColorBody;
			SkinInfo.m_ColorFeet = ColorFeet;
		}
	}
	else
	{
		SkinInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
		SkinInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
	}
	if(Rainbow)
	{
		ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(DefTick, 1.0f, 0.5f));
		SkinInfo.m_ColorBody = Col;
		SkinInfo.m_ColorFeet = Col;
	}
	SkinInfo.m_Size = Size;
	const CAnimState *pIdleState = CAnimState::GetIdle();
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &SkinInfo, OffsetToMid);
	vec2 TeeRenderPos(RenderPos.x, RenderPos.y + OffsetToMid.y);
	if(Cute)
		RenderTeeCute(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos, true);
	else
		RenderTools()->RenderTee(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos);
	g_Config.m_QmWhiteFeet = WhiteFeetTemp;
}

void CMenus::RenderTeeCute(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, bool CuteEyes, float Alpha)
{
	Dir = Ui()->MousePos() - Pos;
	if(pInfo->m_Size > 0.0f)
		Dir /= pInfo->m_Size;
	const float Length = length(Dir);
	if(Length > 1.0f)
		Dir /= Length;
	if(CuteEyes && Length < 0.4f)
		Emote = 2;
	RenderTools()->RenderTee(pAnim, pInfo, Emote, Dir, Pos, Alpha);
}

void CMenus::RenderFontIcon(const CUIRect Rect, const char *pText, float Size, int Align)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	Ui()->DoLabel(&Rect, pText, Size, Align);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

void CMenus::RenderFontIcon_QmIcon(const CUIRect Rect, EQmIcon Icon, const char *pFallbackIcon, float Size, int Align)
{
	// 图集优先；图集未就绪时 DoLabel_QmIcon 内部回退到 pFallbackIcon 字形。
	Ui()->DoLabel_QmIcon(&Rect, Icon, pFallbackIcon, Size, Align);
}

int CMenus::DoButtonNoRect_FontIcon(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners)
{
	return ui_widget::DoIconButton(ui_widget::ControlContext(Ui()), pButtonContainer, EQmIcon::COUNT, pText, Checked, *pRect, BUTTONFLAG_LEFT, Corners, true, std::nullopt, false, true);
}

int CMenus::DoButtonNoRect_QmIcon(CButtonContainer *pButtonContainer, EQmIcon Icon, const char *pFallbackIcon, int Checked, const CUIRect *pRect, int Corners)
{
	return ui_widget::DoIconButton(ui_widget::ControlContext(Ui()), pButtonContainer, Icon, pFallbackIcon, Checked, *pRect, BUTTONFLAG_LEFT, Corners, true, std::nullopt, false, true);
}

void CMenus::PopupConfirmRemoveWarType()
{
	GameClient()->m_WarList.RemoveWarType(m_pRemoveWarType->m_aWarName);
	++s_TClientWarListFilterRevision;
	m_pRemoveWarType = nullptr;
}

void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	if(!ReadOnly)
		EnsureSettingsBindCache();

	ApplyTClientContentMetrics(MainView.w);
	CPerfTimer RenderTimer;
	if(!ReadOnly)
	{
		s_Time += Client()->RenderFrameTime() * (1.0f / 100.0f);
		if(!s_StartedTime)
		{
			s_StartedTime = true;
			s_Time = (float)rand() / (float)RAND_MAX;
		}
	}

	CUIRect TabBar;
	int ActiveTab = m_TClientSettingsTab;
	if(ActiveTab < 0 || ActiveTab >= NUMBER_OF_TCLIENT_TABS)
		ActiveTab = TCLIENT_TAB_SETTINGS;
	if(!ReadOnly)
		m_TClientSettingsTab = ActiveTab;

	MainView = TClientSettingsContentView(MainView, &TabBar);
	const float TabWidth = TabBar.w / NUMBER_OF_TCLIENT_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_TCLIENT_TABS] = {};
	static const char *s_apTClientTabNames[NUMBER_OF_TCLIENT_TABS] = {};
	static char s_aTClientLanguageFile[IO_MAX_PATH_LENGTH] = {};
	static bool s_TClientTabNamesInitialized = false;
	if(!s_TClientTabNamesInitialized || str_comp(s_aTClientLanguageFile, g_Config.m_ClLanguagefile) != 0)
	{
		s_TClientTabNamesInitialized = true;
		str_copy(s_aTClientLanguageFile, g_Config.m_ClLanguagefile, sizeof(s_aTClientLanguageFile));
		if(!ReadOnly)
		{
			s_VisualFontLoader.InvalidateCache(ESettingsCacheDirtyReason::LANGUAGE);
			s_RightSectionLoader.InvalidateCache(ESettingsCacheDirtyReason::LANGUAGE);
		}
		s_apTClientTabNames[TCLIENT_TAB_SETTINGS] = Localize("Settings");
		s_apTClientTabNames[TCLIENT_TAB_BINDWHEEL] = Localize("Bind Wheel");
		s_apTClientTabNames[TCLIENT_TAB_WARLIST] = Localize("War List");
		s_apTClientTabNames[TCLIENT_TAB_BINDCHAT] = Localize("Chat Binds");
		s_apTClientTabNames[TCLIENT_TAB_STATUSBAR] = Localize("Status Bar");
	}

	// 胶囊 Tabbar：可见页签槽位先算完，再画容器与滑块，最后画页签文字。
	CUIRect aTClientTabSlots[NUMBER_OF_TCLIENT_TABS];
	int aTClientTabPages[NUMBER_OF_TCLIENT_TABS];
	int NumTClientTabs = 0;
	int ActiveTClientTab = -1;
	{
		CUIRect TabsRemainder = TabBar;
		for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
		{
			TabsRemainder.VSplitLeft(TabWidth, &aTClientTabSlots[NumTClientTabs], &TabsRemainder);
			aTClientTabPages[NumTClientTabs] = Tab;
			if(ActiveTab == Tab)
				ActiveTClientTab = NumTClientTabs;
			++NumTClientTabs;
		}
	}
	if(NumTClientTabs > 0)
	{
		const IUiContext TClientTabBarCtx = TabBarUiContext();
		ui_widget::CapsuleTabBarChrome(TClientTabBarCtx, MakeUiScopeHash("settings_tclient_tabs_capsule"), ui_widget::CapsuleTabBarRowRect(aTClientTabSlots, NumTClientTabs), ActiveTClientTab >= 0 ? &aTClientTabSlots[ActiveTClientTab] : nullptr, SettingsCapsuleTabBarStyle());
		for(int TabIndex = 0; TabIndex < NumTClientTabs; ++TabIndex)
		{
			const int Tab = aTClientTabPages[TabIndex];
			if(DoButton_MenuTab(&s_aPageTabs[Tab], s_apTClientTabNames[Tab], ActiveTab == Tab, &aTClientTabSlots[TabIndex], IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f, nullptr, nullptr, -1.0f, true) && !ReadOnly)
			{
				m_TClientSettingsTab = Tab;
				ActiveTab = Tab;
			}
		}
	}

	CUIRect ContentView = MainView;
	// 子 Tab 的入场由设置 Card Deck 统一处理，避免和页面级位移动效叠加。
	const bool TransitionActive = false;

	{
		CPerfTimer StageTimer;
		if(ActiveTab == TCLIENT_TAB_SETTINGS)
		{
			RenderSettingsTClientSettings(ContentView, ReadOnly);
		}
		if(ActiveTab == TCLIENT_TAB_BINDCHAT)
			RenderSettingsTClientChatBinds(ContentView, ReadOnly);
		if(ActiveTab == TCLIENT_TAB_BINDWHEEL)
			RenderSettingsTClientBindWheel(ContentView, ReadOnly);
		if(ActiveTab == TCLIENT_TAB_WARLIST)
			RenderSettingsTClientWarList(ContentView, ReadOnly);
		if(ActiveTab == TCLIENT_TAB_STATUSBAR)
			RenderSettingsTClientStatusBar(ContentView, ReadOnly);
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "tab=%d transition=%d", ActiveTab, TransitionActive ? 1 : 0);
		LogTClientPerfStageEx("tclient_tab", nullptr, ETClientSettingsPerfStage::TAB_SHELL, StageTimer.ElapsedMs(), TransitionActive, aExtra);
		const char *pTabShellStage = nullptr;
		switch(ActiveTab)
		{
		case TCLIENT_TAB_BINDCHAT: pTabShellStage = "tclient_tab_3_shell"; break;
		case TCLIENT_TAB_STATUSBAR: pTabShellStage = "tclient_tab_4_shell"; break;
		default: break;
		}
		if(pTabShellStage != nullptr)
			LogTClientPerfStage(pTabShellStage, StageTimer.ElapsedMs(), TransitionActive, aExtra);
		LogTClientPerfStage("tclient_tab_content", StageTimer.ElapsedMs(), TransitionActive, aExtra);
	}

	char aExtra[96];
	str_format(aExtra, sizeof(aExtra), "tab=%d transition=%d", ActiveTab, TransitionActive ? 1 : 0);
	LogTClientPerfStage("tclient_page_total", RenderTimer.ElapsedMs(), false, aExtra);
	if(!ReadOnly)
	{
		m_SettingsRuntimeMetadata.m_LastTClientTab = ActiveTab;
		m_SettingsRuntimeMetadata.m_Valid = true;
	}
}

std::vector<SSettingsSection> CMenus::BuildTClientLeftCacheSections()
{
	std::vector<SSettingsSection> vSections;
	vSections.push_back(BuildTClientThemeCacheSection());
	vSections.push_back(BuildTClientCursorCacheSection());
	vSections.push_back(BuildTClientAutoReplyCacheSection());
	vSections.push_back(BuildTClientPetCacheSection());
	return vSections;
}

std::vector<SSettingsSection> CMenus::BuildTClientRightCacheSections()
{
	std::vector<SSettingsSection> vSections;
	vSections.push_back(BuildTClientHudCacheSection());
	return vSections;
}

void CMenus::InvalidateTClientSettingsRuntimeCacheSections(ESettingsCacheDirtyReason Reason)
{
	s_VisualFontLoader.InvalidateCache(Reason);
	s_RightSectionLoader.InvalidateCache(Reason);
}

void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient", PrewarmOnly);
}

void CMenus::LoadSettingsRuntimeCacheMetadata()
{
	SSessionUiCache SessionCache;
	CSectionLoader::LoadSessionCache(SessionCache, SETTINGS_RUNTIME_CACHE_METADATA_FILE, Storage());
	m_SettingsRuntimeMetadata = {};
	const CUIRect CacheView = Ui()->Screen() != nullptr ? TClientSettingsContentView(*Ui()->Screen()) : CUIRect{0.0f, 0.0f, 0.0f, 0.0f};
	const SSettingsRuntimeCacheKey CurrentRuntimeKey = ToSettingsRuntimeCacheKey(MakeSettingsSectionRuntimeKey(CacheView, Graphics()));
	const SSettingsRuntimeCacheKey PersistedRuntimeKey = ToSettingsRuntimeCacheKey(SessionCache.m_RuntimeKey);
	const bool RuntimeKeyMatches = SettingsRuntimeCacheKeyMatches(CurrentRuntimeKey, PersistedRuntimeKey);
	m_SettingsRuntimeMetadata.m_LastPage = SessionCache.m_LastSettingsPage;
	m_SettingsRuntimeMetadata.m_LastTClientTab = CanonicalizePersistedTClientTab(SessionCache.m_LastTClientTab >= 0 ? SessionCache.m_LastTClientTab : 0);
	m_SettingsRuntimeMetadata.m_LastQmTab = CanonicalizePersistedQmClientTab(SessionCache.m_LastQmTab >= 0 ? SessionCache.m_LastQmTab : 0);
	m_SettingsRuntimeMetadata.m_LastScrollPage = SessionCache.m_Valid && RuntimeKeyMatches ? SETTINGS_TCLIENT : -1;
	m_SettingsRuntimeMetadata.m_LastScrollY = RuntimeKeyMatches ? SessionCache.m_LastScrollY : 0.0f;
	m_SettingsRuntimeMetadata.m_RuntimeKey = CurrentRuntimeKey;
	m_SettingsRuntimeMetadata.m_Valid = SessionCache.m_Valid && RuntimeKeyMatches;
	if(m_SettingsRuntimeMetadata.m_LastPage == SETTINGS_CONFIGS)
	{
		m_SettingsRuntimeMetadata.m_LastPage = SETTINGS_QMCLIENT;
		m_SettingsRuntimeMetadata.m_LastQmTab = QMCLIENT_SETTINGS_TAB_CONFIG;
	}
	if(SessionCache.m_LastTClientTab >= 0)
		m_TClientSettingsTab = CanonicalizePersistedTClientTab(SessionCache.m_LastTClientTab);
	m_SettingsTClientCurrentScrollY = RuntimeKeyMatches ? SessionCache.m_LastScrollY : 0.0f;
	m_SettingsTClientScrollRestorePending = SessionCache.m_Valid && RuntimeKeyMatches;
	if(SessionCache.m_LastQmTab >= 0)
		m_QmClientSettingsTab = CanonicalizePersistedQmClientTab(SessionCache.m_LastQmTab);
}

void CMenus::SaveSettingsRuntimeCacheMetadata()
{
	if(m_SettingsRuntimeMetadata.m_LastPage < 0 && g_Config.m_UiSettingsPage >= 0)
		m_SettingsRuntimeMetadata.m_LastPage = g_Config.m_UiSettingsPage;
	m_SettingsRuntimeMetadata.m_LastQmTab = CanonicalizePersistedQmClientTab(m_QmClientSettingsTab);
	m_SettingsRuntimeMetadata.m_LastTClientTab = CanonicalizePersistedTClientTab(m_TClientSettingsTab);
	if(m_SettingsRuntimeMetadata.m_LastPage >= 0)
		m_SettingsRuntimeMetadata.m_Valid = true;
	SSessionUiCache SessionCache;
	CUIRect CacheView = Ui()->Screen() != nullptr ? TClientSettingsContentView(*Ui()->Screen()) : CUIRect{0.0f, 0.0f, 0.0f, 0.0f};
	SessionCache.m_RuntimeKey = MakeSettingsSectionRuntimeKey(CacheView, Graphics());
	m_SettingsRuntimeMetadata.m_RuntimeKey = ToSettingsRuntimeCacheKey(SessionCache.m_RuntimeKey);
	SessionCache.m_LastSettingsPage = m_SettingsRuntimeMetadata.m_LastPage;
	SessionCache.m_LastTClientTab = m_SettingsRuntimeMetadata.m_LastTClientTab;
	SessionCache.m_LastQmTab = m_SettingsRuntimeMetadata.m_LastQmTab;
	SessionCache.m_LastScrollY = m_SettingsRuntimeMetadata.m_LastScrollPage == SETTINGS_TCLIENT ? m_SettingsRuntimeMetadata.m_LastScrollY : 0.0f;
	SessionCache.m_Valid = m_SettingsRuntimeMetadata.m_Valid;
	CSectionLoader::SaveSessionCache(SessionCache, SETTINGS_RUNTIME_CACHE_METADATA_FILE, Storage());
}

void CMenus::RenderSettingsTClientBindWheel(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-bind-wheel", PrewarmOnly);
}

void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-chat-binds", PrewarmOnly);
}

void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-warlist", PrewarmOnly);
}

void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-status-bar", PrewarmOnly);
}

void CMenus::RenderSettingsTClientProfiles(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-profiles", PrewarmOnly);
}

void CMenus::RenderSettingsTClientConfigs(CUIRect MainView, bool PrewarmOnly)
{
	RenderSettingsCatalogPage(MainView, "tclient-configs", PrewarmOnly);
}
