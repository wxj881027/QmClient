#include "QmCardCatalog.h"

#include "QmKeywordReplyCardPersistence.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsChoiceGrid.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/components/menus.h>
#include <game/client/qm_icon.h>
#include <game/localization.h>

#include <algorithm>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace FontIcons;

using qm_keyword_reply_card::ESaveResult;

using SAutoReplyRulePlain = QmKeywordReplyRules::SRule;

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SAutoReplyRuleInputRow
{
	std::unique_ptr<char[]> m_pExtendedTrigger;
	std::unique_ptr<char[]> m_pExtendedReply;
	int m_AutoRename = 0;
	int m_Regex = 0;
	std::string m_TriggerId;
	std::string m_OptionsId;
	std::string m_ReplyId;
	CButtonContainer m_RemoveButton;
	CLineInputBuffered<512> m_TriggerInput;
	CLineInputBuffered<256> m_ReplyInput;
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
		if(s_pKeywordTabDropDown == nullptr)
			return false;
		const auto &Popup = s_pKeywordTabDropDown->m_SelectionPopupContext;
		return Popup.m_SelectionIndex >= 0 && Popup.m_pCommit && !Popup.m_pCommit->m_Cancelled;
	}
}
static uint64_t s_KeywordRulesLayoutRevision = 1;
static size_t s_KeywordRulesLayoutCount = 0;
static bool s_KeywordRulesLayoutHalfFilled = false;
static char s_aKeywordRulesConfigCache[sizeof(g_Config.m_QmKeywordReplyRules)] = {};
// 每条完整规则至少需要 a=>b 和两字节的换行转义，末行结尾只占一个 NUL。
static constexpr size_t MAX_KEYWORD_RULE_ROWS = (sizeof(g_Config.m_QmKeywordReplyRules) + 1) / (sizeof("a=>b\\n") - 1);

static void UpdateKeywordRulesLayoutState(size_t RuleCount, bool HalfFilled)
{
	if(s_KeywordRulesLayoutCount == RuleCount && s_KeywordRulesLayoutHalfFilled == HalfFilled)
		return;
	s_KeywordRulesLayoutCount = RuleCount;
	s_KeywordRulesLayoutHalfFilled = HalfFilled;
	++s_KeywordRulesLayoutRevision;
}

static void SetRuleInput(CLineInput &Input, std::unique_ptr<char[]> &pExtendedBuffer, const char *pText)
{
	const size_t RequiredSize = static_cast<size_t>(str_length(pText)) + 1;
	if(RequiredSize > Input.GetMaxSize())
	{
		// 外部配置可包含较长字段。只为需要的字段扩容，短字段仍使用行内缓冲区。
		pExtendedBuffer = std::make_unique<char[]>(RequiredSize);
		Input.SetBuffer(pExtendedBuffer.get(), RequiredSize);
	}
	Input.Set(pText);
}

static void ReleaseRuleInputs(CUi &Ui, SAutoReplyRuleInputRow &Rule)
{
	Ui.ReleaseActiveTextInput(&Rule.m_TriggerInput);
	Ui.ReleaseActiveTextInput(&Rule.m_ReplyInput);
}

static std::unique_ptr<SAutoReplyRuleInputRow> CreateAutoReplyRuleInputRow(const char *pTrigger = "", const char *pReply = "", bool AutoRename = false, bool Regex = false)
{
	auto pRow = std::make_unique<SAutoReplyRuleInputRow>();
	const std::string Prefix = "keyword-rule-" + std::to_string(s_NextKeywordRuleId++);
	pRow->m_TriggerId = Prefix + "-trigger";
	pRow->m_OptionsId = Prefix + "-options";
	pRow->m_ReplyId = Prefix + "-reply";
	SetRuleInput(pRow->m_TriggerInput, pRow->m_pExtendedTrigger, pTrigger);
	SetRuleInput(pRow->m_ReplyInput, pRow->m_pExtendedReply, pReply);
	pRow->m_AutoRename = AutoRename ? 1 : 0;
	pRow->m_Regex = Regex ? 1 : 0;
	return pRow;
}

static std::vector<SAutoReplyRulePlain> CollectCompleteAutoReplyRules(const std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows)
{
	std::vector<SAutoReplyRulePlain> vRules;
	vRules.reserve(vRows.size());
	for(const auto &pRow : vRows)
	{
		SAutoReplyRulePlain Rule{QmKeywordReplyRules::Trim(pRow->m_TriggerInput.GetString()), QmKeywordReplyRules::Trim(pRow->m_ReplyInput.GetString()), pRow->m_AutoRename != 0, pRow->m_Regex != 0};
		if(!Rule.m_Keywords.empty() && !Rule.m_Reply.empty())
			vRules.push_back(std::move(Rule));
	}
	return vRules;
}

static bool IsAutoReplyRuleRowHalfFilled(const SAutoReplyRuleInputRow &Row)
{
	const bool HasTrigger = *str_utf8_skip_whitespaces(Row.m_TriggerInput.GetString()) != '\0';
	const bool HasReply = *str_utf8_skip_whitespaces(Row.m_ReplyInput.GetString()) != '\0';
	return HasTrigger != HasReply;
}

static ESaveResult SaveAutoReplyRulesFromRows(const std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows) try
{
	return qm_keyword_reply_card::SaveRules(CollectCompleteAutoReplyRules(vRows), g_Config.m_QmKeywordReplyRules);
}
catch(const std::bad_alloc &)
{
	return ESaveResult::UNAVAILABLE;
}
catch(const std::length_error &)
{
	return ESaveResult::UNAVAILABLE;
}

static ESaveResult s_KeywordRulesSaveResult = ESaveResult::SAVED;

static int s_KeywordReplyTab = 0;

static bool SyncKeywordRuleRows(CUi &Ui)
{
	if(!QmKeywordReplyRules::EditorConfigChanged(s_KeywordRuleRowsInited, s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules))
		return true;

	char aDecodedRules[sizeof(g_Config.m_QmKeywordReplyRules)];
	QmKeywordReplyRules::DecodeFromConfig(g_Config.m_QmKeywordReplyRules, aDecodedRules, sizeof(aDecodedRules));
	const auto vParsedRules = QmKeywordReplyRules::ParseDecoded(aDecodedRules);
	if(!s_KeywordRuleRowsInited || !qm_keyword_reply_card::CompleteRulesMatch(CollectCompleteAutoReplyRules(s_vKeywordRuleRows), vParsedRules))
	{
		const bool HasActiveInput = std::any_of(s_vKeywordRuleRows.begin(), s_vKeywordRuleRows.end(), [](const auto &pRule) {
			return pRule->m_TriggerInput.IsActive() || pRule->m_ReplyInput.IsActive();
		});
		// 等焦点释放后载入外部配置，这期间旧表单不能写回。
		if(HasActiveInput)
			return false;

		std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> vReplacement;
		vReplacement.reserve(vParsedRules.size());
		for(const auto &Rule : vParsedRules)
			vReplacement.push_back(CreateAutoReplyRuleInputRow(Rule.m_Keywords.c_str(), Rule.m_Reply.c_str(), Rule.m_AutoRename, Rule.m_Regex));
		for(const auto &pRule : s_vKeywordRuleRows)
			ReleaseRuleInputs(Ui, *pRule);
		s_vKeywordRuleRows.swap(vReplacement);
		s_KeywordRuleRowsInited = true;
		if(s_KeywordRulesSaveResult != ESaveResult::SAVED)
			++s_KeywordRulesLayoutRevision;
		s_KeywordRulesSaveResult = ESaveResult::SAVED;
		const bool HalfFilled = std::any_of(s_vKeywordRuleRows.begin(), s_vKeywordRuleRows.end(), [](const auto &pRule) { return IsAutoReplyRuleRowHalfFilled(*pRule); });
		UpdateKeywordRulesLayoutState(s_vKeywordRuleRows.size(), HalfFilled);
	}

	str_copy(s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules, sizeof(s_aKeywordRulesConfigCache));
	return true;
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
	const bool CanSaveRules = CanCommit && SyncKeywordRuleRows(*Ui());
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
					ReleaseRuleInputs(*Ui(), *pRule);
				s_KeywordReplyTab = NewTab;
			}
		}
	}
	Flow.Space(ui_token::settings::RowGap(LineSpacing));
	if(s_KeywordReplyTab == 2)
	{
		if(Flow.Control("keyword-cooldown", LineHeight, Row))
		{
			SplitSettingsControlRow(Row, CurrentSettingsContentMetrics(), &LabelColumn, &ControlColumn);
			DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-auto-reply-cooldown", &LabelColumn, Localize("Auto reply cooldown"), BodySize, TEXTALIGN_ML, {}, (int)LabelColumn.w);
			static int s_QmAutoReplyCooldownInputId;
			RenderQmSettingsSliderWithValueInput(&s_QmAutoReplyCooldownInputId, ControlColumn, &g_Config.m_QmAutoReplyCooldown, 0, 30, "s", !CanCommit);
		}
		Flow.Space(ui_token::settings::RowGap(LineSpacing));
		auto Checkbox = [&](const char *pId, const char *pText, int *pValue) {
			if(Flow.Control(pId, LineHeight, Row))
				RenderQmFunctionCheckbox(pValue, pText, Localize(pText), pValue, &Row, !CanCommit);
			Flow.Space(ui_token::settings::RowGap(LineSpacing));
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
		const bool CanAddRule = s_vKeywordRuleRows.size() < MAX_KEYWORD_RULE_ROWS;
		if(s_KeywordReplyTab == 0 && DoButton_Menu_QmIcon(&s_KeywordAddRuleButton, EQmIcon::PLUS, FONT_ICON_PLUS, CanAddRule ? 0 : -1, &Add, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, ui_token::radius::PILL) && CanCommit)
		{
			s_vKeywordRuleRows.push_back(CreateAutoReplyRuleInputRow());
			// 容器接收规则后，同时登记 UI 焦点和文本输入。
			CLineInput &TriggerInput = s_vKeywordRuleRows.back()->m_TriggerInput;
			Ui()->SetActiveItem(&TriggerInput);
			TriggerInput.Activate(EInputPriority::UI);
			Changes.m_Added = true;
		}
	}
	Flow.Space(ui_token::settings::RowGap(LineSpacing));
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
		Flow.Space(ui_token::settings::RowGap(LineSpacing));
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
		Flow.Space(ui_token::settings::RowGap(LineSpacing));
		if(RemoveClicked)
		{
			ReleaseRuleInputs(*Ui(), *pRule);
			s_vKeywordRuleRows.erase(s_vKeywordRuleRows.begin() + Index);
			Changes.m_Removed = true;
		}
		else
			++Index;
	}
	if(CanSaveRules && Changes.ShouldCommit(false))
	{
		const ESaveResult SaveResult = SaveAutoReplyRulesFromRows(s_vKeywordRuleRows);
		if(s_KeywordRulesSaveResult != SaveResult)
			++s_KeywordRulesLayoutRevision;
		s_KeywordRulesSaveResult = SaveResult;
		if(SaveResult == ESaveResult::SAVED)
			str_copy(s_aKeywordRulesConfigCache, g_Config.m_QmKeywordReplyRules, sizeof(s_aKeywordRulesConfigCache));
	}
	if(CanCommit && Changes.Any())
	{
		const bool HalfFilled = std::any_of(s_vKeywordRuleRows.begin(), s_vKeywordRuleRows.end(), [](const auto &pRule) { return IsAutoReplyRuleRowHalfFilled(*pRule); });
		UpdateKeywordRulesLayoutState(s_vKeywordRuleRows.size(), HalfFilled);
	}
	const char *pValidation = nullptr;
	switch(s_KeywordRulesSaveResult)
	{
	case ESaveResult::SAVED:
		if(s_KeywordRulesLayoutHalfFilled)
			pValidation = Localize("Both sides of keyword rules must be filled");
		break;
	case ESaveResult::TOO_LONG:
		pValidation = Localize("Keyword rules are too long. Changes were not saved; shorten or remove a rule.");
		break;
	case ESaveResult::UNREPRESENTABLE:
		pValidation = Localize("A keyword rule could not be saved. Check the trigger for => or a reserved prefix.");
		break;
	case ESaveResult::UNAVAILABLE:
		pValidation = Localize("Could not save keyword rules. Edit a rule to retry.");
		break;
	}
	if(pValidation)
		Flow.Text("keyword-validation", pValidation, 0.0f, SmallSize, ui_token::settings::RowGap(LineSpacing), [this](const char *pText, float Width, float Size) { return TextRender()->TextBoundingBox(Size, pText, -1, Width).m_H; }, [this, SmallSize](const CUIRect &Rect, const char *pText) {
				TextRender()->TextColor(1.0f, 0.2f, 0.2f, 1.0f);
				DoSettingsMenuLabel(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_FUNCTION, QMCLIENT_SETTINGS_TAB_FUNCTION, "qmclient-keyword-reply-rule-validation", &Rect, pText, SmallSize, TEXTALIGN_ML, {}, (int)Rect.w);
				TextRender()->TextColor(TextRender()->DefaultTextColor()); }, LineHeight);
}
