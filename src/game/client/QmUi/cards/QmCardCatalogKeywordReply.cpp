#include "QmCardCatalog.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsChoiceGrid.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
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

using SAutoReplyRulePlain = QmKeywordReplyRules::SRule;

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SAutoReplyRuleInputRow
{
	char m_aTrigger[512] = "";
	char m_aReply[256] = "";
	int m_AutoRename = 0;
	int m_Regex = 0;
	std::string m_TriggerId;
	std::string m_OptionsId;
	std::string m_ReplyId;
	CButtonContainer m_RemoveButton;
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
static uint64_t s_NextKeywordRuleId = 1;
static CButtonContainer s_aKeywordTabButtons[3];
static CUi::SDropDownState *s_pKeywordTabDropDown = nullptr;

namespace qm_card_catalog
{
	bool KeywordReplyHasPendingInput()
	{
		return s_pKeywordTabDropDown && s_pKeywordTabDropDown->m_SelectionPopupContext.m_SelectionIndex >= 0;
	}
}
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
	const std::string Prefix = "keyword-rule-" + std::to_string(s_NextKeywordRuleId++);
	pRow->m_TriggerId = Prefix + "-trigger";
	pRow->m_OptionsId = Prefix + "-options";
	pRow->m_ReplyId = Prefix + "-reply";
	pRow->m_TriggerInput.Set(pTrigger);
	pRow->m_ReplyInput.Set(pReply);
	pRow->m_AutoRename = AutoRename ? 1 : 0;
	pRow->m_Regex = Regex ? 1 : 0;
	return pRow;
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

static bool BuildAutoReplyRulesFromRows(const std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, char *pOutRules, size_t OutRulesSize)
{
	std::vector<QmKeywordReplyRules::SRule> vRules;
	for(const auto &pRow : vRows)
		vRules.push_back({QmKeywordReplyRules::Trim(pRow->m_TriggerInput.GetString()), QmKeywordReplyRules::Trim(pRow->m_ReplyInput.GetString()), pRow->m_AutoRename != 0, pRow->m_Regex != 0});
	return QmKeywordReplyRules::EncodeRules(vRules, pOutRules, OutRulesSize);
}

static bool s_KeywordRulesSaveFailed = false;

static int s_KeywordReplyTab = 0;

static void SyncKeywordRuleRows()
{
	auto SyncRuleRowsFromConfig = [](std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, bool &Inited, const char *pConfigRules) {
		const auto vParsedRules = QmKeywordReplyRules::ParseDecoded(pConfigRules);
		const auto RebuildRows = [&]() {
			vRows.clear();
			if(s_KeywordRulesSaveFailed)
				++s_KeywordRulesLayoutRevision;
			s_KeywordRulesSaveFailed = false;
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
	State.m_KeywordRulesRevision = s_KeywordRulesLayoutRevision;
	State.m_KeywordRulesCount = s_KeywordRulesLayoutCount;
	State.m_KeywordRulesHalfFilled = s_KeywordRulesLayoutHalfFilled;
}

void CMenus::RenderQmFunctionKeywordReplyContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const bool CanCommit = Flow.Input() && !PrewarmOnly && !Ui()->RenderOnly();
	const float SmallSize = CurrentSettingsContentMetrics().m_SmallSize;
	IUiContext TextInputCtx = SettingsUiContext("settings_qmclient_keyword_reply_text_inputs", UiScale);
	CUIRect Row, LabelColumn, ControlColumn;
	if(CanCommit)
		SyncKeywordRuleRows();
	const char *apTabs[] = {Localize("Rule list"), Localize("Reply content"), Localize("Trigger limits")};
	if(Flow.Control("keyword-tabs", LineHeight, Row))
	{
		const int NewTab = DoNavigationTabs(s_aKeywordTabButtons, apTabs, std::size(apTabs), s_KeywordReplyTab, Row, ui_token::radius::BASE, nullptr, BodySize);
		if(CanCommit)
		{
			s_pKeywordTabDropDown = &m_SettingsRadioStates.at(s_aKeywordTabButtons).m_DropDown;
			if(NewTab != s_KeywordReplyTab)
			{
				for(const auto &pRule : s_vKeywordRuleRows)
				{
					pRule->m_TriggerInput.Deactivate();
					pRule->m_ReplyInput.Deactivate();
				}
				s_KeywordReplyTab = NewTab;
			}
		}
	}
	Flow.Space(LineSpacing);
	if(s_KeywordReplyTab == 2)
	{
		if(Flow.Control("keyword-cooldown", LineHeight, Row))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-auto-reply-cooldown", &LabelColumn, Localize("Auto reply cooldown"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			static int s_QmAutoReplyCooldownInputId;
			RenderQmSettingsSliderWithValueInput(&s_QmAutoReplyCooldownInputId, ControlColumn, &g_Config.m_QmAutoReplyCooldown, 0, 30, "s", !CanCommit);
		}
		Flow.Space(LineSpacing);
		auto Checkbox = [&](const char *pId, const char *pText, int *pValue) {
			if(Flow.Control(pId, LineHeight, Row))
				RenderQmFunctionCheckbox(pValue, pText, Localize(pText), pValue, &Row, !CanCommit);
			Flow.Space(LineSpacing);
		};
		Checkbox("keyword-enabled", Localizable("Enable keyword reply"), &g_Config.m_QmKeywordReplyEnabled);
		Checkbox("keyword-dummy", Localizable("Reply with dummy"), &g_Config.m_QmKeywordReplyUseDummy);
		return;
	}

	QmKeywordReplyRules::SEditorChanges Changes;
	if(Flow.Control("keyword-rules-heading", LineHeight, Row))
	{
		SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
		DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-rules", &LabelColumn, Localize("Keyword rules"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
		CUIRect Add;
		ControlColumn.VSplitRight(std::min(ControlColumn.w, maximum(LineHeight, 24.0f * UiScale)), nullptr, &Add);
		Add.VMargin(std::max(0.0f, (Add.w - Add.h) * 0.5f), &Add);
		if(s_KeywordReplyTab == 0 && DoButton_Menu_QmIcon(&s_KeywordAddRuleButton, EQmIcon::PLUS, FONT_ICON_PLUS, 0, &Add, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL) && CanCommit)
		{
			auto pNewRule = CreateAutoReplyRuleInputRow();
			pNewRule->m_TriggerInput.Activate(EInputPriority::UI);
			s_vKeywordRuleRows.push_back(std::move(pNewRule));
			Changes.m_Added = true;
		}
	}
	Flow.Space(LineSpacing);
	const float OptionSpacing = std::max(4.0f, 4.0f * UiScale);
	const char *apOptions[] = {Localize("Rename"), Localize("Regex")};
	// 选项是两个独立开关；保持原网格尺寸，由真实译文决定窄宽换行。
	static CSettingsChoiceWidthCache s_OptionWidths;
	const float OptionWidth = s_OptionWidths.Resolve(apOptions, std::size(apOptions), BodySize, TextRender()->GlyphAtlasRevision(),
		[this](const char *pText, float Size) { return TextRender()->TextWidth(Size, pText); });
	const float MinOptionWidth = OptionWidth + LineHeight * 2.0f;
	const SSettingsChoiceGrid Options = ResolveSettingsToggleGrid(Content.w, MinOptionWidth, LineHeight, LineSpacing, 2, 2);
	for(size_t Index = 0; Index < s_vKeywordRuleRows.size();)
	{
		auto &pRule = s_vKeywordRuleRows[Index];
		bool RemoveClicked = false;
		if(Flow.Control(pRule->m_TriggerId.c_str(), LineHeight, Row))
		{
			CUIRect InputColumn, Remove;
			Row.VSplitRight(std::min(Row.w, std::max(LineHeight, 24.0f * UiScale)), &InputColumn, &Remove);
			InputColumn.VSplitRight(std::min(InputColumn.w, OptionSpacing), &InputColumn, nullptr);
			if(s_KeywordReplyTab == 0)
				Changes.m_TriggerText |= ui_widget::InputField(TextInputCtx, &pRule->m_TriggerInput, InputColumn, Localize("Keyword rules"), BodySize);
			else
			{
				SLabelProperties Props;
				Props.m_MaxWidth = InputColumn.w;
				Props.m_EllipsisAtEnd = true;
				Ui()->DoLabel(&InputColumn, pRule->m_TriggerInput.GetString(), BodySize, TEXTALIGN_ML, Props);
			}
			RemoveClicked = s_KeywordReplyTab == 0 && DoButton_Menu_QmIcon(&pRule->m_RemoveButton, EQmIcon::MINUS, FONT_ICON_MINUS, 0, &Remove, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL) && CanCommit;
		}
		Flow.Space(LineSpacing);
		if(s_KeywordReplyTab == 0)
		{
			if(Flow.Control(pRule->m_OptionsId.c_str(), Options.Height(), Row))
			{
				const auto Option = [&](int CellIndex, const char *pTextId, int *pValue) {
					const CUIRect Cell = Options.Cell(Row, CellIndex);
					SLabelProperties Props;
					Props.m_DisallowNewline = true;
					Props.m_StopAtEnd = true;
					const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, pValue, pTextId, apOptions[CellIndex], *pValue, &Cell, Props, !PrewarmOnly, BodySize) != 0;
					if(Changed && CanCommit)
						*pValue ^= 1;
					return Changed && CanCommit;
				};
				Changes.m_Rename |= Option(0, "Rename", &pRule->m_AutoRename);
				Changes.m_Regex |= Option(1, "Regex", &pRule->m_Regex);
			}
		}
		else if(Flow.Control(pRule->m_ReplyId.c_str(), LineHeight, Row))
			Changes.m_ReplyText |= ui_widget::InputField(TextInputCtx, &pRule->m_ReplyInput, Row, Localize("Reply content"), BodySize);
		Flow.Space(LineSpacing);
		if(RemoveClicked)
		{
			// 输入 owner 随规则释放前先退出，邻卡的按钮与行标识保持稳定。
			pRule->m_TriggerInput.Deactivate();
			pRule->m_ReplyInput.Deactivate();
			s_vKeywordRuleRows.erase(s_vKeywordRuleRows.begin() + Index);
			Changes.m_Removed = true;
		}
		else
			++Index;
	}
	if(CanCommit && Changes.ShouldCommit(false))
	{
		const bool SaveFailed = !BuildAutoReplyRulesFromRows(s_vKeywordRuleRows, g_Config.m_QmKeywordReplyRules, sizeof(g_Config.m_QmKeywordReplyRules));
		if(s_KeywordRulesSaveFailed != SaveFailed)
			++s_KeywordRulesLayoutRevision;
		s_KeywordRulesSaveFailed = SaveFailed;
		if(!SaveFailed)
			str_copy(s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules, sizeof(s_aKeywordRulesConfigCache));
	}
	if(CanCommit && Changes.Any())
	{
		const bool HalfFilled = std::any_of(s_vKeywordRuleRows.begin(), s_vKeywordRuleRows.end(), [](const auto &pRule) { return IsAutoReplyRuleRowHalfFilled(*pRule); });
		UpdateKeywordRulesLayoutState(s_vKeywordRuleRows.size(), HalfFilled);
	}
	if(s_KeywordRulesLayoutHalfFilled || s_KeywordRulesSaveFailed)
		Flow.Text("keyword-validation", s_KeywordRulesSaveFailed ? Localize("Keyword rules are too long. Changes were not saved; shorten or remove a rule.") : Localize("Both sides of keyword rules must be filled"), 0.0f, SmallSize, LineSpacing, [this](const char *pText, float Width, float Size) { return TextRender()->TextBoundingBox(Size, pText, -1, Width).m_H; }, [this, SmallSize](const CUIRect &Rect, const char *pText) {
				TextRender()->TextColor(1.0f, 0.2f, 0.2f, 1.0f);
				DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-rule-validation", &Rect, pText, SmallSize, TEXTALIGN_ML, {}, (int)Rect.w);
				TextRender()->TextColor(TextRender()->DefaultTextColor()); }, LineHeight);
}
