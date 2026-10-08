#include "QmCardCatalog.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsToggleGrid.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/keyword_reply_rules.h>
#include <game/client/qm_icon.h>
#include <game/localization.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace FontIcons;

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

static std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> s_vKeywordRuleRows;
static bool s_KeywordRuleRowsInited = false;
static CButtonContainer s_KeywordAddRuleButton;
static std::vector<CButtonContainer> s_vKeywordRemoveRuleButtons;
static uint64_t s_KeywordRulesLayoutRevision = 1;
static size_t s_KeywordRulesLayoutCount = 0;
static bool s_KeywordRulesLayoutHalfFilled = false;
static char s_aKeywordRulesConfigCache[sizeof(g_Config.m_QmKeywordReplyRules)] = {};

static void UpdateKeywordRulesLayoutState(size_t RuleCount, bool HalfFilled)
{
	if(s_KeywordRulesLayoutCount == RuleCount && s_KeywordRulesLayoutHalfFilled == HalfFilled)
		return;
	s_KeywordRulesLayoutCount = RuleCount;
	s_KeywordRulesLayoutHalfFilled = HalfFilled;
	++s_KeywordRulesLayoutRevision;
}

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

static std::unique_ptr<SAutoReplyRuleInputRow> CreateAutoReplyRuleInputRow(const char *pTrigger = "", const char *pReply = "", bool AutoRename = false, bool Regex = false)
{
	auto pRow = std::make_unique<SAutoReplyRuleInputRow>();
	pRow->m_TriggerInput.Set(pTrigger);
	pRow->m_ReplyInput.Set(pReply);
	pRow->m_AutoRename = AutoRename ? 1 : 0;
	pRow->m_Regex = Regex ? 1 : 0;
	return pRow;
}

static void ParseAutoReplyRules(const char *pRules, std::vector<SAutoReplyRulePlain> &vOutRules)
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
		(void)AutoRename;
		(void)RegexRule;
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


static bool AutoReplyRowsMatchRules(const std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, const std::vector<SAutoReplyRulePlain> &vRules)
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

static bool IsAutoReplyRuleRowHalfFilled(const SAutoReplyRuleInputRow &Row)
{
	char aTrigger[512];
	char aReply[256];
	const bool HasTrigger = CopyTrimmedString(Row.m_TriggerInput.GetString(), aTrigger, sizeof(aTrigger));
	const bool HasReply = CopyTrimmedString(Row.m_ReplyInput.GetString(), aReply, sizeof(aReply));
	return HasTrigger != HasReply;
}

static void BuildAutoReplyRulesFromRows(const std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, char *pOutRules, size_t OutRulesSize)
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

static int s_KeywordReplyTab = 0;

static void SyncKeywordRuleRows()
{
	auto SyncRuleRowsFromConfig = [](std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, bool &Inited, const char *pConfigRules) {
		std::vector<SAutoReplyRulePlain> vParsedRules;
		ParseAutoReplyRules(pConfigRules, vParsedRules);
		const auto RebuildRows = [&]() {
			vRows.clear();
			for(const auto &Rule : vParsedRules)
				vRows.push_back(CreateAutoReplyRuleInputRow(Rule.m_Keywords.c_str(), Rule.m_Reply.c_str(), Rule.m_AutoRename, Rule.m_Regex));
		};
		bool HasActiveInput = false;
		for(const auto &pRule : vRows)
		{
			if(pRule->m_TriggerInput.IsActive() || pRule->m_ReplyInput.IsActive())
			{
				HasActiveInput = true;
				break;
			}
		}
		const bool RowsMatch = Inited && AutoReplyRowsMatchRules(vRows, vParsedRules);
		if(!Inited || (!HasActiveInput && !RowsMatch))
		{
			RebuildRows();
			Inited = true;
			const bool HalfFilled = std::any_of(vRows.begin(), vRows.end(), [](const auto &pRule) { return IsAutoReplyRuleRowHalfFilled(*pRule); });
			UpdateKeywordRulesLayoutState(vRows.size(), HalfFilled);
			return true;
		}
		return RowsMatch;
	};
	if(QmKeywordReplyRules::EditorConfigChanged(s_KeywordRuleRowsInited, s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules))
	{
		char aDecodedRules[sizeof(g_Config.m_QmKeywordReplyRules)];
		QmKeywordReplyRules::DecodeFromConfig(g_Config.m_QmKeywordReplyRules, aDecodedRules, sizeof(aDecodedRules));
		if(SyncRuleRowsFromConfig(s_vKeywordRuleRows, s_KeywordRuleRowsInited, aDecodedRules))
			str_copy(s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules, sizeof(s_aKeywordRulesConfigCache));
	}
}

void qm_card_catalog::FillKeywordReplyLayoutState(SQmFunctionCardLayoutState &State)
{
	SyncKeywordRuleRows();
	State.m_KeywordRulesRevision = s_KeywordRulesLayoutRevision;
	State.m_KeywordRulesCount = s_KeywordRulesLayoutCount;
	State.m_KeywordRulesHalfFilled = s_KeywordRulesLayoutHalfFilled;
}

void CMenus::RenderQmFunctionKeywordReplyContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	const float SmallSize = CurrentSettingsContentMetrics().m_SmallSize;
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_keyword_reply_text_inputs", UiScale);
	CUIRect Row, LabelColumn, ControlColumn;
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	const char *apTabs[] = {Localize("Rule list"), Localize("Reply content"), Localize("Trigger limits")};
	float MinTabWidth = 0.0f;
	for(const char *pTab : apTabs)
		MinTabWidth = std::max(MinTabWidth, TextRender()->TextWidth(BodySize, pTab) + LineHeight);
	const SSettingsToggleGrid Tabs = ResolveSettingsToggleGrid(Content.w, MinTabWidth, LineHeight, LineSpacing, 3, 3);
	CUIRect TabsArea;
	Content.HSplitTop(Tabs.Height() + LineSpacing, &TabsArea, &Content);
	static CButtonContainer s_aTabButtons[3];
	for(int Index = 0; Index < 3; ++Index)
	{
		const CUIRect Tab = Tabs.Cell(TabsArea, Index);
		if(ReadOnly)
			Ui()->DoLabel(&Tab, apTabs[Index], BodySize, TEXTALIGN_MC);
		else if(DoButtonLineSize_Menu(&s_aTabButtons[Index], apTabs[Index], s_KeywordReplyTab == Index, &Tab, LineHeight))
		{
			if(s_KeywordReplyTab != Index)
			{
				// 页签只切换可见控件，不重新解析配置或销毁未完成的编辑行。
				for(const auto &pRule : s_vKeywordRuleRows)
				{
					pRule->m_TriggerInput.Deactivate();
					pRule->m_ReplyInput.Deactivate();
				}
				s_KeywordReplyTab = Index;
				++s_KeywordRulesLayoutRevision;
			}
		}
	}
	SyncKeywordRuleRows();
	if(s_KeywordReplyTab == 2)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-auto-reply-cooldown", &LabelColumn, Localize("Auto reply cooldown"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		static int s_QmAutoReplyCooldownInputId;
		RenderQmSettingsSliderWithValueInput(&s_QmAutoReplyCooldownInputId, ControlColumn, &g_Config.m_QmAutoReplyCooldown, 0, 30, "s", PrewarmOnly);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		auto RenderCheckbox = [this, &Content, &Row, LineHeight, LineSpacing, PrewarmOnly](const void *pId, const char *pText, int *pValue) {
			Content.HSplitTop(LineHeight, &Row, &Content);
			RenderQmFunctionCheckbox(pId, pText, Localize(pText), pValue, &Row, PrewarmOnly);
			Content.HSplitTop(LineSpacing, nullptr, &Content);
		};
		RenderCheckbox(&g_Config.m_QmKeywordReplyEnabled, Localizable("Enable keyword reply"), &g_Config.m_QmKeywordReplyEnabled);
		RenderCheckbox(&g_Config.m_QmKeywordReplyUseDummy, Localizable("Reply with dummy"), &g_Config.m_QmKeywordReplyUseDummy);
		return;
	}

	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &LabelColumn, &ControlColumn);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-rules", &LabelColumn, Localize("Keyword rules"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
	CUIRect AddRuleButtonRect;
	ControlColumn.VSplitRight(maximum(LineHeight, 24.0f * UiScale), &ControlColumn, &AddRuleButtonRect);
	AddRuleButtonRect.VMargin((AddRuleButtonRect.w - AddRuleButtonRect.h) * 0.5f, &AddRuleButtonRect);
	QmKeywordReplyRules::SEditorChanges Changes;
	if(s_KeywordReplyTab == 0 && !ReadOnly && DoButton_Menu_QmIcon(&s_KeywordAddRuleButton, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &AddRuleButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL))
	{
		auto pNewRule = CreateAutoReplyRuleInputRow();
		pNewRule->m_TriggerInput.Activate(EInputPriority::UI);
		s_vKeywordRuleRows.push_back(std::move(pNewRule));
		UpdateKeywordRulesLayoutState(s_vKeywordRuleRows.size(), s_KeywordRulesLayoutHalfFilled);
		Changes.m_Added = true;
	}
	s_vKeywordRemoveRuleButtons.resize(s_vKeywordRuleRows.size());
	Content.HSplitTop(LineSpacing, nullptr, &Content);

	const char *pRenameLabel = Localize("Rename");
	const char *pRegexLabel = Localize("Regex");
	const float OptionSpacing = std::max(4.0f, 4.0f * UiScale);
	auto RenderRuleOption = [this, PrewarmOnly, BodySize](const char *pTextId, const char *pText, int *pValue, const CUIRect &Rect) {
		SLabelProperties Props;
		Props.m_DisallowNewline = true;
		Props.m_StopAtEnd = true;
		const bool ProcessInput = !PrewarmOnly && !Ui()->RenderOnly();
		const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pValue, pTextId, pText, *pValue, &Rect, Props, ProcessInput, BodySize) != 0;
		if(Changed)
			*pValue ^= 1;
		return Changed;
	};

	for(size_t i = 0; i < s_vKeywordRuleRows.size();)
	{
		auto &pRule = s_vKeywordRuleRows[i];
		pRule->m_TriggerInput.SetEmptyText("");
		pRule->m_ReplyInput.SetEmptyText("");
		Content.HSplitTop(LineHeight, &Row, &Content);
		CUIRect InputColumn, RemoveButtonRect;
		Row.VSplitRight(std::min(Row.w, std::max(LineHeight, 24.0f * UiScale)), &InputColumn, &RemoveButtonRect);
		InputColumn.VSplitRight(std::min(InputColumn.w, OptionSpacing), &InputColumn, nullptr);
		if(s_KeywordReplyTab == 0)
		{
			Changes.m_TriggerText |= ui_widget::InputField(TextInputCtx, &pRule->m_TriggerInput, InputColumn, Localize("Keyword rules"), BodySize);
		}
		else
		{
			SLabelProperties Props;
			Props.m_MaxWidth = InputColumn.w;
			Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&InputColumn, pRule->m_TriggerInput.GetString(), BodySize, TEXTALIGN_ML, Props);
		}
		const bool RemoveClicked = s_KeywordReplyTab == 0 && !ReadOnly && DoButton_Menu_QmIcon(&s_vKeywordRemoveRuleButtons[i], EQmIcon::MINUS, FONT_ICON_MINUS, 0, &RemoveButtonRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
		if(s_KeywordReplyTab == 0)
		{
			const float MinOptionWidth = std::max(TextRender()->TextWidth(BodySize, pRenameLabel), TextRender()->TextWidth(BodySize, pRegexLabel)) + LineHeight * 2.0f;
			const SSettingsToggleGrid Options = ResolveSettingsToggleGrid(Content.w, MinOptionWidth, LineHeight, LineSpacing, 2, 2);
			CUIRect OptionsArea;
			Content.HSplitTop(Options.Height(), &OptionsArea, &Content);
			Changes.m_Rename |= RenderRuleOption("Rename", pRenameLabel, &pRule->m_AutoRename, Options.Cell(OptionsArea, 0));
			Changes.m_Regex |= RenderRuleOption("Regex", pRegexLabel, &pRule->m_Regex, Options.Cell(OptionsArea, 1));
		}
		else
		{
			Content.HSplitTop(LineHeight, &Row, &Content);
			Changes.m_ReplyText |= ui_widget::InputField(TextInputCtx, &pRule->m_ReplyInput, Row, Localize("Reply content"), BodySize);
		}
		Content.HSplitTop(LineSpacing, nullptr, &Content);
		if(RemoveClicked)
		{
			s_vKeywordRuleRows.erase(s_vKeywordRuleRows.begin() + i);
			s_vKeywordRemoveRuleButtons.erase(s_vKeywordRemoveRuleButtons.begin() + i);
			const bool HalfFilled = std::any_of(s_vKeywordRuleRows.begin(), s_vKeywordRuleRows.end(), [](const auto &pRule) { return IsAutoReplyRuleRowHalfFilled(*pRule); });
			UpdateKeywordRulesLayoutState(s_vKeywordRuleRows.size(), HalfFilled);
			Changes.m_Removed = true;
			continue;
		}
		++i;
	}

	if(Changes.ShouldCommit(Ui()->RenderOnly()))
	{
		char aEncodedRules[sizeof(g_Config.m_QmKeywordReplyRules)];
		BuildAutoReplyRulesFromRows(s_vKeywordRuleRows, aEncodedRules, sizeof(aEncodedRules));
		QmKeywordReplyRules::EncodeForConfig(aEncodedRules, g_Config.m_QmKeywordReplyRules, sizeof(g_Config.m_QmKeywordReplyRules));
		str_copy(s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules, sizeof(s_aKeywordRulesConfigCache));
	}
	if(Changes.Any())
	{
		const bool HalfFilled = std::any_of(s_vKeywordRuleRows.begin(), s_vKeywordRuleRows.end(), [](const auto &pRule) { return IsAutoReplyRuleRowHalfFilled(*pRule); });
		UpdateKeywordRulesLayoutState(s_vKeywordRuleRows.size(), HalfFilled);
	}
	if(!s_KeywordRulesLayoutHalfFilled)
		return;
	Content.HSplitTop(LineHeight, &Row, &Content);
	TextRender()->TextColor(1.0f, 0.2f, 0.2f, 1.0f);
	DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-rule-validation", &Row, Localize("Both sides of keyword rules must be filled"), SmallSize, TEXTALIGN_ML, {}, (int)Row.w);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}
