#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/engine.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/components/qmclient/translate_ui_settings.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL_audio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum
{
	TCLIENT_TAB_SETTINGS = 0,
	TCLIENT_TAB_BINDWHEEL,
	TCLIENT_TAB_WARLIST,
	TCLIENT_TAB_BINDCHAT,
	TCLIENT_TAB_STATUSBAR,
	TCLIENT_TAB_INFO,
	NUMBER_OF_TCLIENT_TABS
};

typedef struct
{
	const char *m_pName;
	const char *m_pCommand;
	int m_KeyId;
	int m_ModifierCombination;
} CKeyInfo;

using namespace FontIcons;

[[maybe_unused]] static float s_Time = 0.0f;
[[maybe_unused]] static bool s_StartedTime = false;

extern std::unordered_map<std::string, CBindSlot> g_CommandBindCache;
extern bool g_CommandBindCacheInitialized;

namespace
{

bool PerfDebugEnabled()
{
	return g_Config.m_QmPerfDebug != 0;
}

double PerfDebugThresholdMs()
{
	return g_Config.m_QmPerfDebugThresholdMs > 0 ? g_Config.m_QmPerfDebugThresholdMs : 1.0;
}

[[maybe_unused]] void LogQmPerfStage(const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
{
	if(!PerfDebugEnabled())
		return;
	if(DurationMs < PerfDebugThresholdMs())
		return;

	if(pExtra != nullptr && pExtra[0] != '\0')
		dbg_msg("perf/qmclient", "stage=%s duration_ms=%.3f %s", pStage, DurationMs, pExtra);
	else
		dbg_msg("perf/qmclient", "stage=%s duration_ms=%.3f", pStage, DurationMs);
}

void LogTClientPerfStage(const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)
{
	if(!PerfDebugEnabled())
		return;
	if(DurationMs < PerfDebugThresholdMs())
		return;

	if(pExtra != nullptr && pExtra[0] != '\0')
		dbg_msg("perf/tclient", "stage=%s duration_ms=%.3f %s", pStage, DurationMs, pExtra);
	else
		dbg_msg("perf/tclient", "stage=%s duration_ms=%.3f", pStage, DurationMs);
}

[[maybe_unused]] const char *QmSettingsTabName(int Tab)
{
	switch(Tab)
	{
	case 0: return "visuals";
	case 1: return "functions";
	case 2: return "hud";
	case 3: return "contributors";
	case 4: return "config";
	default: return "unknown";
	}
}

int gs_TClientSettingsDeferredFrames = 0;
int gs_TClientTabDeferredFrames = 0;
int gs_TClientDeferredTab = -1;
int gs_QmVisualDeferredFrames = 0;

void BeginDeferredTClientSettings()
{
	gs_TClientSettingsDeferredFrames = 6;
}

void BeginDeferredTClientTab(const int Tab)
{
	gs_TClientDeferredTab = Tab;
	switch(Tab)
	{
	case TCLIENT_TAB_SETTINGS:
		gs_TClientTabDeferredFrames = 5;
		break;
	case TCLIENT_TAB_BINDWHEEL:
		gs_TClientTabDeferredFrames = 4;
		break;
	case TCLIENT_TAB_STATUSBAR:
		gs_TClientTabDeferredFrames = 5;
		break;
	case TCLIENT_TAB_WARLIST:
		gs_TClientTabDeferredFrames = 4;
		break;
	case TCLIENT_TAB_BINDCHAT:
		gs_TClientTabDeferredFrames = 2;
		break;
	case TCLIENT_TAB_INFO:
		gs_TClientTabDeferredFrames = 4;
		break;
	default:
		gs_TClientTabDeferredFrames = 2;
		break;
	}
}

bool ShouldDeferTClientVisualStage(const float ScrollY, const int MinRemainingFrames)
{
	return gs_TClientSettingsDeferredFrames >= MinRemainingFrames && absolute(ScrollY) <= 1.0f;
}

[[maybe_unused]] bool ShouldDeferTClientTabContent(const int Tab)
{
	return gs_TClientDeferredTab == Tab && gs_TClientTabDeferredFrames > 0;
}

int GetDeferredTClientTabFrames(const int Tab)
{
	return gs_TClientDeferredTab == Tab ? gs_TClientTabDeferredFrames : 0;
}

void FinishDeferredTClientSettingsFrame()
{
	if(gs_TClientSettingsDeferredFrames > 0)
		--gs_TClientSettingsDeferredFrames;
}

void FinishDeferredTClientTabFrame(const int Tab)
{
	if(gs_TClientDeferredTab != Tab || gs_TClientTabDeferredFrames <= 0)
		return;
	--gs_TClientTabDeferredFrames;
	if(gs_TClientTabDeferredFrames <= 0)
		gs_TClientDeferredTab = -1;
}

[[maybe_unused]] void BeginDeferredQmVisualTab()
{
	gs_QmVisualDeferredFrames = 1;
}

[[maybe_unused]] bool ShouldDeferQmVisualHeavyStage(const int ActiveTab, const float ScrollY)
{
	return ActiveTab == 0 && gs_QmVisualDeferredFrames > 0 && absolute(ScrollY) <= 1.0f;
}

[[maybe_unused]] void FinishDeferredQmVisualFrame(const int ActiveTab)
{
	if(ActiveTab == 0 && gs_QmVisualDeferredFrames > 0)
		--gs_QmVisualDeferredFrames;
}

struct SSectionCullContext
{
	float m_ViewportTop;
	float m_ViewportBottom;
	float m_PrefetchPadding;
};

bool IsSectionVisible(const CUIRect &SectionRect, const SSectionCullContext &Context)
{
	return SectionRect.y + SectionRect.h >= Context.m_ViewportTop - Context.m_PrefetchPadding &&
		SectionRect.y <= Context.m_ViewportBottom + Context.m_PrefetchPadding;
}

uint64_t HashBytesFnv1a64(uint64_t Hash, const void *pData, size_t DataSize)
{
	const uint8_t *pBytes = static_cast<const uint8_t *>(pData);
	for(size_t i = 0; i < DataSize; ++i)
	{
		Hash ^= pBytes[i];
		Hash *= 1099511628211ull;
	}
	return Hash;
}

template<typename T>
uint64_t HashValueFnv1a64(uint64_t Hash, const T &Value)
{
	return HashBytesFnv1a64(Hash, &Value, sizeof(Value));
}

[[maybe_unused]] uint64_t HashStringFnv1a64(uint64_t Hash, const char *pString)
{
	return pString == nullptr ? Hash : HashBytesFnv1a64(Hash, pString, str_length(pString));
}

}

const float FontSize = 14.0f;
const float EditBoxFontSize = 12.0f;
const float LineSize = 20.0f;
const float ColorPickerLineSize = 25.0f;
const float HeadlineFontSize = 20.0f;
const float StandardFontSize = 14.0f;

const float HeadlineHeight = HeadlineFontSize + 0.0f;
const float Margin = 10.0f;
const float MarginSmall = 5.0f;
const float MarginExtraSmall = 2.5f;
const float MarginBetweenSections = 30.0f;
const float MarginBetweenViews = 30.0f;

const float ColorPickerLabelSize = 13.0f;
const float ColorPickerLineSpacing = 5.0f;

struct SAutoReplyRulePlain
{
	std::string m_Keywords;
	std::string m_Reply;
	bool m_AutoRename = false;
	bool m_Regex = false;
};

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

[[maybe_unused]] static bool DoEditBoxMultiLine(CUi *pUi, CLineInput *pLineInput, const CUIRect *pRect, float TextFontSize, float LineSpacing)
{
	const bool Inside = pUi->MouseHovered(pRect);
	const bool Active = pUi->ActiveItem() == pLineInput || pLineInput->IsActive();
	const bool Changed = pLineInput->WasChanged();
	const bool CursorChanged = pLineInput->WasCursorChanged();

	const float VSpacing = 2.0f;
	CUIRect Textbox;
	pRect->VMargin(VSpacing, &Textbox);
	const float LineWidth = Textbox.w;

	bool JustGotActive = false;
	if(pUi->CheckActiveItem(pLineInput))
	{
		if(pUi->MouseButton(0))
		{
			if(pLineInput->IsActive() && (pUi->Input()->HasComposition() || pUi->Input()->GetCandidateCount()))
			{
				pUi->Input()->StopTextInput();
				pUi->Input()->StartTextInput();
			}
		}
		else
		{
			pUi->SetActiveItem(nullptr);
		}
	}
	else if(pUi->HotItem() == pLineInput)
	{
		if(pUi->MouseButton(0))
		{
			if(!Active)
				JustGotActive = true;
			pUi->SetActiveItem(pLineInput);
		}
	}

	if(Inside && !pUi->MouseButton(0))
		pUi->SetHotItem(pLineInput);

	if(pUi->Enabled() && Active && !JustGotActive)
		pLineInput->Activate(EInputPriority::UI);
	else
		pLineInput->Deactivate();

	CLineInput::SMouseSelection *pMouseSelection = pLineInput->GetMouseSelection();
	if(Inside)
	{
		if(!pMouseSelection->m_Selecting && pUi->MouseButtonClicked(0))
		{
			pMouseSelection->m_Selecting = true;
			pMouseSelection->m_PressMouse = pUi->MousePos();
			pMouseSelection->m_Offset = vec2(0.0f, 0.0f);
		}
	}
	if(pMouseSelection->m_Selecting)
	{
		pMouseSelection->m_ReleaseMouse = pUi->MousePos();
		if(!pUi->MouseButton(0))
		{
			pMouseSelection->m_Selecting = false;
			if(Active)
				pUi->Input()->EnsureScreenKeyboardShown();
		}
	}

	pRect->Draw(CUi::ms_LightButtonColorFunction.GetColor(Active, pUi->HotItem() == pLineInput), IGraphics::CORNER_ALL, 3.0f);
	pUi->ClipEnable(pRect);
	pLineInput->Render(&Textbox, TextFontSize, TEXTALIGN_TL, Changed || CursorChanged, LineWidth, LineSpacing);
	pUi->ClipDisable();

	pLineInput->SetScrollOffset(0.0f);
	pLineInput->SetScrollOffsetChange(0.0f);

	return Changed;
}

static void SetFlag(int32_t &Flags, int n, bool Value)
{
	if(Value)
		Flags |= (1 << n);
	else
		Flags &= ~(1 << n);
}

static bool IsFlagSet(int32_t Flags, int n)
{
	return (Flags & (1 << n)) != 0;
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
	Ui()->DoLabel(&KeyLabel, aBuf, FontSize, TEXTALIGN_ML);

	View.HSplitTop(MarginExtraSmall, nullptr, &View);

	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&ReaderButton, &ClearButton, &KeyButton, Bind, false);
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
	const bool NoClampValue = Flags & CUi::SCROLLBAR_OPTION_NOCLAMPVALUE;

	int Value = *pOption;
	Min /= Scale;
	Max /= Scale;
	// Allow adjustment of slider options when ctrl is pressed (to avoid scrolling, or accidentally adjusting the value)
	int Increment = std::max(1, (Max - Min) / 35);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(pRect))
	{
		Value += Increment;
		Value = std::clamp(Value, Min, Max);
	}
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(pRect))
	{
		Value -= Increment;
		Value = std::clamp(Value, Min, Max);
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s: %i%s", pStr, Value * Scale, pSuffix);

	if(NoClampValue)
	{
		// Clamp the value internally for the scrollbar
		Value = std::clamp(Value, Min, Max);
	}

	CUIRect Label, ScrollBar;
	pRect->VSplitMid(&Label, &ScrollBar, minimum(10.0f, pRect->w * 0.05f));

	const float LabelFontSize = Label.h * CUi::ms_FontmodHeight * 0.8f;
	Ui()->DoLabel(&Label, aBuf, LabelFontSize, TEXTALIGN_ML);

	Value = pScale->ToAbsolute(Ui()->DoScrollbarH(pId, &ScrollBar, pScale->ToRelative(Value, Min, Max)), Min, Max);
	if(NoClampValue && ((Value == Min && *pOption < Min) || (Value == Max && *pOption > Max)))
	{
		Value = *pOption;
	}

	if(*pOption != Value)
	{
		*pOption = Value;
		return true;
	}
	return false;
}

bool CMenus::DoEditBoxWithLabel(CLineInput *LineInput, const CUIRect *pRect, const char *pLabel, const char *pDefault, char *pBuf, size_t BufSize)
{
	CUIRect Button, Label;
	pRect->VSplitLeft(210.0f, &Label, &Button);
	Ui()->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_ML);
	LineInput->SetBuffer(pBuf, BufSize);
	LineInput->SetEmptyText(pDefault);
	return Ui()->DoEditBox(LineInput, &Button, EditBoxFontSize);
}

int CMenus::DoButtonLineSize_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, float ButtonLineSize, bool Fake, const char *pImageName, int Corners, float Rounding, float FontFactor, ColorRGBA Color)
{
	CUIRect Text = *pRect;

	if(Checked)
		Color = ColorRGBA(0.6f, 0.6f, 0.6f, 0.5f);
	Color.a *= Ui()->ButtonColorMul(pButtonContainer);

	if(Fake)
		Color.a *= 0.5f;

	pRect->Draw(Color, Corners, Rounding);

	Text.HMargin((Text.h - ButtonLineSize) / 2.0f, &Text);
	Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
	Text.HMargin((Text.h * FontFactor) / 2.0f, &Text);
	Ui()->DoLabel(&Text, pText, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);

	if(Fake)
		return 0;

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::RenderDevSkin(vec2 RenderPos, float Size, const char *pSkinName, const char *pBackupSkin, bool CustomColors, int FeetColor, int BodyColor, int Emote, bool Rainbow, bool Cute, ColorRGBA ColorFeet, ColorRGBA ColorBody)
{
	bool WhiteFeetTemp = g_Config.m_TcWhiteFeet;
	g_Config.m_TcWhiteFeet = false;

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
	g_Config.m_TcWhiteFeet = WhiteFeetTemp;
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

int CMenus::DoButtonNoRect_FontIcon(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	TextRender()->TextColor(TextRender()->DefaultTextSelectionColor());
	if(Ui()->HotItem() == pButtonContainer)
	{
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	CUIRect Temp;
	pRect->HMargin(0.0f, &Temp);
	Ui()->DoLabel(&Temp, pText, Temp.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::PopupConfirmRemoveWarType()
{
	GameClient()->m_WarList.RemoveWarType(m_pRemoveWarType->m_aWarName);
	m_pRemoveWarType = nullptr;
}

void CMenus::RenderSettingsTClient(CUIRect MainView)
{
	CPerfTimer RenderTimer;
	s_Time += Client()->RenderFrameTime() * (1.0f / 100.0f);
	if(!s_StartedTime)
	{
		s_StartedTime = true;
		s_Time = (float)rand() / (float)RAND_MAX;
	}

	static int s_CurCustomTab = 0;
	static bool s_CustomTabTransitionInitialized = false;
	static int s_PrevCustomTab = 0;
	static float s_CustomTabTransitionDirection = 0.0f;
	const uint64_t TClientTabSwitchNode = UiAnimNodeKey("settings_tclient_tab_switch");

	CUIRect TabBar, Button;
	int TabCount = NUMBER_OF_TCLIENT_TABS;
	for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_TcTClientSettingsTabs, Tab))
		{
			TabCount--;
			if(s_CurCustomTab == Tab)
				s_CurCustomTab++;
		}
	}

	MainView.HSplitTop(LineSize, &TabBar, &MainView);
	const float TabWidth = TabBar.w / TabCount;
	static CButtonContainer s_aPageTabs[NUMBER_OF_TCLIENT_TABS] = {};
	static const char *s_apTClientTabNames[NUMBER_OF_TCLIENT_TABS] = {};
	static char s_aTClientLanguageFile[IO_MAX_PATH_LENGTH] = {};
	if(str_comp(s_aTClientLanguageFile, g_Config.m_ClLanguagefile) != 0)
	{
		str_copy(s_aTClientLanguageFile, g_Config.m_ClLanguagefile, sizeof(s_aTClientLanguageFile));
		s_apTClientTabNames[TCLIENT_TAB_SETTINGS] = Localize("Settings");
		s_apTClientTabNames[TCLIENT_TAB_BINDWHEEL] = Localize("Bind Wheel");
		s_apTClientTabNames[TCLIENT_TAB_WARLIST] = Localize("War List");
		s_apTClientTabNames[TCLIENT_TAB_BINDCHAT] = Localize("Chat Binds");
		s_apTClientTabNames[TCLIENT_TAB_STATUSBAR] = Localize("Status Bar");
		s_apTClientTabNames[TCLIENT_TAB_INFO] = Localize("Info");
	}

	for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_TcTClientSettingsTabs, Tab))
			continue;

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : Tab == NUMBER_OF_TCLIENT_TABS - 1 ? IGraphics::CORNER_R :
													 IGraphics::CORNER_NONE;
		if(DoButton_MenuTab(&s_aPageTabs[Tab], s_apTClientTabNames[Tab], s_CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurCustomTab = Tab;
	}

	MainView.HSplitTop(Margin, nullptr, &MainView);

	if(!s_CustomTabTransitionInitialized)
	{
		s_PrevCustomTab = s_CurCustomTab;
		s_CustomTabTransitionInitialized = true;
		BeginDeferredTClientTab(s_CurCustomTab);
		if(s_CurCustomTab == TCLIENT_TAB_SETTINGS)
			BeginDeferredTClientSettings();
	}
	else if(s_CurCustomTab != s_PrevCustomTab)
	{
		s_CustomTabTransitionDirection = s_CurCustomTab > s_PrevCustomTab ? 1.0f : -1.0f;
		TriggerUiSwitchAnimation(TClientTabSwitchNode, 0.18f);
		BeginDeferredTClientTab(s_CurCustomTab);
		if(s_CurCustomTab == TCLIENT_TAB_SETTINGS)
			BeginDeferredTClientSettings();
		else
			gs_TClientSettingsDeferredFrames = 0;
		s_PrevCustomTab = s_CurCustomTab;
	}

	CUIRect ContentView = MainView;
	const float TransitionStrength = ReadUiSwitchAnimation(TClientTabSwitchNode);
	const bool TransitionActive = TransitionStrength > 0.0f && s_CustomTabTransitionDirection != 0.0f;
	const CUIRect ContentClip = MainView;
	const float TransitionAlpha = UiSwitchAnimationAlpha(TransitionStrength);
	if(TransitionActive)
	{
		Ui()->ClipEnable(&ContentClip);
		ApplyUiSwitchOffset(ContentView, TransitionStrength, s_CustomTabTransitionDirection, false, 0.08f, 24.0f, 120.0f);
	}

	{
		CPerfTimer StageTimer;
		if(s_CurCustomTab == TCLIENT_TAB_SETTINGS)
		{
			RenderSettingsTClientSettings(ContentView);
			FinishDeferredTClientSettingsFrame();
		}
		if(s_CurCustomTab == TCLIENT_TAB_BINDCHAT)
			RenderSettingsTClientChatBinds(ContentView);
		if(s_CurCustomTab == TCLIENT_TAB_BINDWHEEL)
			RenderSettingsTClientBindWheel(ContentView);
		if(s_CurCustomTab == TCLIENT_TAB_WARLIST)
			RenderSettingsTClientWarList(ContentView);
		if(s_CurCustomTab == TCLIENT_TAB_STATUSBAR)
			RenderSettingsTClientStatusBar(ContentView);
		if(s_CurCustomTab == TCLIENT_TAB_INFO)
			RenderSettingsTClientInfo(ContentView);
		FinishDeferredTClientTabFrame(s_CurCustomTab);
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "tab=%d transition=%d", s_CurCustomTab, TransitionActive ? 1 : 0);
		LogTClientPerfStage("tclient_tab_content", StageTimer.ElapsedMs(), TransitionActive, aExtra);
	}

	if(TransitionActive && TransitionAlpha > 0.0f)
	{
		ContentClip.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha), IGraphics::CORNER_NONE, 0.0f);
	}
	if(TransitionActive)
	{
		Ui()->ClipDisable();
	}
	char aExtra[96];
	str_format(aExtra, sizeof(aExtra), "tab=%d transition=%d", s_CurCustomTab, TransitionActive ? 1 : 0);
	LogTClientPerfStage("tclient_page_total", RenderTimer.ElapsedMs(), false, aExtra);
}

void CMenus::RenderSettingsTClientSettings(CUIRect MainView)
{
	CPerfTimer RenderTimer;
	CUIRect Column, LeftView, RightView, Button, Label;
	const CUIRect Viewport = MainView;

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	auto LogSettingsStage = [&](const char *pStage, const CPerfTimer &Timer) {
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "scroll_y=%.1f", ScrollOffset.y);
		LogTClientPerfStage(pStage, Timer.ElapsedMs(), false, aExtra);
	};
	{
		CPerfTimer StageTimer;
		s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);
		LogSettingsStage("tclient_settings_scroll_begin", StageTimer);
	}

	static std::vector<CUIRect> s_SectionBoxes;
	static vec2 s_PrevScrollOffset(0.0f, 0.0f);
	const SSectionCullContext CullContext{
		Viewport.y,
		Viewport.y + Viewport.h,
		120.0f,
	};
	auto ShouldRenderSection = [&](const CUIRect &CurrentColumn, float TopPadding, float EstimatedHeight) {
		CUIRect SectionRect = CurrentColumn;
		if(TopPadding > 0.0f)
			SectionRect.HSplitTop(TopPadding, nullptr, &SectionRect);
		SectionRect.HSplitTop(EstimatedHeight, &SectionRect, nullptr);
		return IsSectionVisible(SectionRect, CullContext);
	};
	auto SkipSection = [&](CUIRect &CurrentColumn, float TopPadding, float EstimatedHeight) {
		CurrentColumn.HSplitTop(TopPadding + EstimatedHeight, nullptr, &CurrentColumn);
	};
	auto DrawSectionBox = [&](const CUIRect &BoxRect) {
		CUIRect Section = BoxRect;
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	};
	auto DrawCompactDeferredSection = [&](const CUIRect &FullRect, const char *pTitle, const char *pSummary) {
		CUIRect Section = FullRect;
		CUIRect Row;
		Section.HSplitTop(MarginBetweenSections, nullptr, &Section);
		Section.HSplitTop(HeadlineHeight, &Row, &Section);
		Ui()->DoLabel(&Row, pTitle, HeadlineFontSize, TEXTALIGN_ML);
		Section.HSplitTop(MarginSmall, nullptr, &Section);
		Section.HSplitTop(LineSize, &Row, &Section);
		Ui()->DoLabel(&Row, pSummary, FontSize, TEXTALIGN_ML);
	};
	[[maybe_unused]] auto CalcHudSectionHeight = [&]() {
		float Height = 0.0f;
		Height += HeadlineHeight + MarginSmall;
		Height += LineSize * 5.0f;
		Height += LineSize + MarginSmall;
		Height += LineSize * 3.0f;
		Height += LineSize;
		Height += LineSize + MarginSmall;
		Height += LineSize;
		Height += MarginExtraSmall;
		return Height;
	};
	[[maybe_unused]] auto CalcTeeStatusBarSectionHeight = [&]() {
		return HeadlineHeight + MarginSmall + LineSize * 7.0f;
	};
	[[maybe_unused]] static float s_InputSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_AntiLatencyToolsSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_AntiPingSmoothingSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_AutoExecuteSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_VotingSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_AutoReplySectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_PlayerIndicatorSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_PetSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_VisualFontSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_VisualNameplateSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_VisualEffectsSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_HudSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_TeeStatusBarSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_GhostToolsSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_RainbowSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_TeeTrailsSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_BackgroundDrawSectionCachedHeight = 0.0f;
	[[maybe_unused]] static float s_FinishNameSectionCachedHeight = 0.0f;
	const int SettingsDeferredFrames = gs_TClientSettingsDeferredFrames;
	const bool DeferVisualFontHeavyControls = ShouldDeferTClientVisualStage(ScrollOffset.y, 3);
	const bool DeferVisualNameplateHeavyControls = ShouldDeferTClientVisualStage(ScrollOffset.y, 2);
	const bool DeferVisualEffectsHeavyControls = ShouldDeferTClientVisualStage(ScrollOffset.y, 2);
	const bool DeferRightHeavySections = ShouldDeferTClientVisualStage(ScrollOffset.y, 2);
	const bool CompactVisualFontSection = SettingsDeferredFrames >= 6 && absolute(ScrollOffset.y) <= 1.0f;
	const bool CompactVisualNameplateSection = SettingsDeferredFrames >= 5 && absolute(ScrollOffset.y) <= 1.0f;
	const bool CompactVisualEffectsSection = SettingsDeferredFrames >= 3 && absolute(ScrollOffset.y) <= 1.0f;
	const bool CompactHudSection = SettingsDeferredFrames >= 4 && absolute(ScrollOffset.y) <= 1.0f;
	const bool CompactTeeStatusBarSection = SettingsDeferredFrames >= 3 && absolute(ScrollOffset.y) <= 1.0f;
	const bool CompactTileOutlinesSection = SettingsDeferredFrames >= 2 && absolute(ScrollOffset.y) <= 1.0f;

	MainView.y += ScrollOffset.y;

	MainView.VSplitRight(5.0f, &MainView, nullptr); // Padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // Padding for scrollbar

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	{
		CPerfTimer StageTimer;
		for(CUIRect &Section : s_SectionBoxes)
		{
			float Padding = MarginBetweenViews * 0.6666f;
			Section.w += Padding;
			Section.h += Padding;
			Section.x -= Padding * 0.5f;
			Section.y -= Padding * 0.5f;
			Section.y -= s_PrevScrollOffset.y - ScrollOffset.y;
			Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
		}
		LogSettingsStage("tclient_settings_section_boxes", StageTimer);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** LeftView ***** //
	{
		CPerfTimer LeftColumnTimer;
		CPerfTimer VisualSectionsTotalTimer;
		Column = LeftView;
		auto LayoutVisualFontSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect = CurrentColumn;
			CUIRect TmpLabel;
			auto ShouldRenderVisualBlock = [&](float Height) {
				return Render && ShouldRenderSection(CurrentColumn, 0.0f, Height);
			};
			[[maybe_unused]] auto SkipVisualBlock = [&](float Height) {
				SkipSection(CurrentColumn, 0.0f, Height);
			};
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpLabel, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Visual: Font & Cursor"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			const bool RenderFontDropdown = Render && ShouldRenderSection(CurrentColumn, 0.0f, LineSize);
			if(RenderFontDropdown)
			{
				CUIRect FontDropDownRect;
				CurrentColumn.HSplitTop(LineSize, &FontDropDownRect, &CurrentColumn);
				FontDropDownRect.VSplitLeft(100.0f, &Label, &FontDropDownRect);
				Ui()->DoLabel(&Label, Localize("Custom Font: "), FontSize, TEXTALIGN_ML);
				if(DeferVisualFontHeavyControls)
				{
					CPerfTimer FontSummaryTimer;
					const char *pCurrentFont = g_Config.m_TcCustomFont[0] != '\0' ? g_Config.m_TcCustomFont : Localize("Default");
					Ui()->DoLabel(&FontDropDownRect, pCurrentFont, FontSize, TEXTALIGN_ML);
					LogSettingsStage("tclient_settings_left_visual_font_dropdown_deferred", FontSummaryTimer);
				}
				else
				{
					static std::vector<std::string> s_FontDropDownNamesOwned;
					static std::vector<const char *> s_FontDropDownNames;
					static CUi::SDropDownState s_FontDropDownState;
					static CScrollRegion s_FontDropDownScrollRegion;
					s_FontDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_FontDropDownScrollRegion;
					s_FontDropDownState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
					const auto &CustomFaces = *TextRender()->GetCustomFaces();
					{
						CPerfTimer StageTimer;
						const bool FacesChanged = s_FontDropDownNamesOwned != CustomFaces;
						if(FacesChanged)
						{
							s_FontDropDownNamesOwned = CustomFaces;
							s_FontDropDownNames.clear();
							s_FontDropDownNames.reserve(s_FontDropDownNamesOwned.size());
							for(const auto &FaceName : s_FontDropDownNamesOwned)
								s_FontDropDownNames.push_back(FaceName.c_str());
						}
						char aExtra[96];
						str_format(aExtra, sizeof(aExtra), "faces=%d changed=%d", (int)CustomFaces.size(), FacesChanged ? 1 : 0);
						LogTClientPerfStage("tclient_font_faces_sync", StageTimer.ElapsedMs(), FacesChanged, aExtra);
					}
					{
						CPerfTimer FontDropDownTimer;
						int FontSelectedOld = -1;
						for(size_t i = 0; i < CustomFaces.size(); ++i)
						{
							if(str_find_nocase(g_Config.m_TcCustomFont, CustomFaces[i].c_str()))
								FontSelectedOld = i;
						}
						CUIRect FontDirectory;
						FontDropDownRect.VSplitRight(20.0f, &FontDropDownRect, &FontDirectory);
						FontDropDownRect.VSplitRight(MarginSmall, &FontDropDownRect, nullptr);

						const int FontSelectedNew = Ui()->DoDropDown(&FontDropDownRect, FontSelectedOld, s_FontDropDownNames.data(), s_FontDropDownNames.size(), s_FontDropDownState);
						if(FontSelectedOld != FontSelectedNew && FontSelectedNew >= 0 && (size_t)FontSelectedNew < s_FontDropDownNames.size())
						{
							str_copy(g_Config.m_TcCustomFont, s_FontDropDownNames[FontSelectedNew]);
							TextRender()->SetCustomFace(g_Config.m_TcCustomFont);
							TextRender()->OnPreWindowResize();
							GameClient()->OnWindowResize();
							GameClient()->Editor()->OnWindowResize();
							TextRender()->OnWindowResize();
							GameClient()->m_MapImages.SetTextureScale(101);
							GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
						}

						static CButtonContainer s_FontDirectoryId;
						if(Ui()->DoButton_FontIcon(&s_FontDirectoryId, FONT_ICON_FOLDER, 0, &FontDirectory, IGraphics::CORNER_ALL))
						{
							Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
							Storage()->CreateFolder("qmclient/fonts", IStorage::TYPE_SAVE);
							char aBuf[IO_MAX_PATH_LENGTH];
							Storage()->GetCompletePath(IStorage::TYPE_SAVE, "qmclient/fonts", aBuf, sizeof(aBuf));
							Client()->ViewFile(aBuf);
						}
						LogSettingsStage("tclient_settings_left_visual_font_dropdown", FontDropDownTimer);
					}
				}
			}
			else
			{
				SkipSection(CurrentColumn, 0.0f, LineSize);
			}

			if(ShouldRenderVisualBlock(MarginExtraSmall * 2.0f + LineSize))
			{
				CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
				CUIRect DropDownRect;
				CurrentColumn.HSplitTop(LineSize, &DropDownRect, &CurrentColumn);
				DropDownRect.VSplitLeft(120.0f, &Label, &DropDownRect);
				Ui()->DoLabel(&Label, Localize("Hammer Mode: "), FontSize, TEXTALIGN_ML);
				if(DeferVisualFontHeavyControls)
				{
					CPerfTimer HammerModeSummaryTimer;
					const char *pHammerMode = Localize("Normal", "Hammer Mode");
					if(g_Config.m_TcHammerRotatesWithCursor == 1)
						pHammerMode = Localize("Rotate with cursor", "Hammer Mode");
					else if(g_Config.m_TcHammerRotatesWithCursor == 2)
						pHammerMode = Localize("Rotate with cursor like gun", "Hammer Mode");
					Ui()->DoLabel(&DropDownRect, pHammerMode, FontSize, TEXTALIGN_ML);
					LogSettingsStage("tclient_settings_left_visual_hammer_mode_deferred", HammerModeSummaryTimer);
				}
				else
				{
					CPerfTimer HammerModeTimer;
					static std::vector<const char *> s_DropDownNames;
					s_DropDownNames = {Localize("Normal", "Hammer Mode"), Localize("Rotate with cursor", "Hammer Mode"), Localize("Rotate with cursor like gun", "Hammer Mode")};
					static CUi::SDropDownState s_DropDownState;
					static CScrollRegion s_DropDownScrollRegion;
					s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
					g_Config.m_TcHammerRotatesWithCursor = Ui()->DoDropDown(&DropDownRect, g_Config.m_TcHammerRotatesWithCursor, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState);
					LogSettingsStage("tclient_settings_left_visual_hammer_mode", HammerModeTimer);
				}
				CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			}
			else
			{
				SkipVisualBlock(MarginExtraSmall * 2.0f + LineSize);
			}

			if(ShouldRenderVisualBlock(LineSize * 2.0f))
			{
				if(DeferVisualFontHeavyControls)
				{
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %d%%", Localize("Ingame cursor scale"), g_Config.m_TcCursorScale);
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					if(g_Config.m_TcAnimateWheelTime > 0)
						str_format(aBuf, sizeof(aBuf), "%s: %dms", Localize("Wheel animate"), g_Config.m_TcAnimateWheelTime);
					else
						str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Wheel animate"), Localize("Off"));
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
				{
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					Ui()->DoScrollbarOption(&g_Config.m_TcCursorScale, &g_Config.m_TcCursorScale, &Button, Localize("Ingame cursor scale"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					if(g_Config.m_TcAnimateWheelTime > 0)
						Ui()->DoScrollbarOption(&g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, Localize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms");
					else
						Ui()->DoScrollbarOption(&g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, Localize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms (off)");
				}
			}
			else
			{
				SkipVisualBlock(LineSize * 2.0f);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};

		auto LayoutVisualNameplateSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect = CurrentColumn;
			CUIRect TmpLabel;
			auto ShouldRenderVisualBlock = [&](float Height) {
				return Render && ShouldRenderSection(CurrentColumn, 0.0f, Height);
			};
			[[maybe_unused]] auto SkipVisualBlock = [&](float Height) {
				SkipSection(CurrentColumn, 0.0f, Height);
			};
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpLabel, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Visual: Nameplates"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(ShouldRenderVisualBlock(LineSize * 7.0f))
			{
				CPerfTimer NameplateTimer;
				if(DeferVisualNameplateHeavyControls)
				{
					CUIRect SummaryLine;
					char aBuf[128];
					auto DrawSummaryToggle = [&](const char *pTitle, bool Enabled) {
						CurrentColumn.HSplitTop(LineSize, &SummaryLine, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %s", pTitle, Enabled ? Localize("On") : Localize("Off"));
						Ui()->DoLabel(&SummaryLine, aBuf, FontSize, TEXTALIGN_ML);
					};
					DrawSummaryToggle(Localize("Show ping colored circle in nameplates"), g_Config.m_TcNameplatePingCircle != 0);
					DrawSummaryToggle(Localize("Show country flags in nameplates"), g_Config.m_TcNameplateCountry != 0);
					DrawSummaryToggle(Localize("Show skin names in nameplate"), g_Config.m_TcNameplateSkins != 0);
					DrawSummaryToggle(Localize("Freeze stars"), g_Config.m_ClFreezeStars != 0);
					CurrentColumn.HSplitTop(LineSize, &SummaryLine, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Colored frozen skins"), g_Config.m_TcColorFreeze ? Localize("On") : Localize("Off"));
					Ui()->DoLabel(&SummaryLine, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(LineSize, &SummaryLine, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Freeze katana"), g_Config.m_TcFreezeKatana ? Localize("On") : Localize("Off"));
					Ui()->DoLabel(&SummaryLine, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(LineSize, &SummaryLine, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("White feet skin"), g_Config.m_TcWhiteFeet ? Localize("On") : Localize("Off"));
					Ui()->DoLabel(&SummaryLine, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
				{
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplatePingCircle, Localize("Show ping colored circle in nameplates"), &g_Config.m_TcNameplatePingCircle, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplateCountry, Localize("Show country flags in nameplates"), &g_Config.m_TcNameplateCountry, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplateSkins, Localize("Show skin names in nameplate"), &g_Config.m_TcNameplateSkins, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFreezeStars, Localize("Freeze stars"), &g_Config.m_ClFreezeStars, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcColorFreeze, Localize("Use colored skins for frozen tees"), &g_Config.m_TcColorFreeze, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFreezeKatana, Localize("Show katan on frozen players"), &g_Config.m_TcFreezeKatana, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWhiteFeet, Localize("Render all custom colored feet as white feet skin"), &g_Config.m_TcWhiteFeet, &CurrentColumn, LineSize);
				}
				LogSettingsStage("tclient_settings_left_visual_nameplates", NameplateTimer);
			}
			else
			{
				SkipSection(CurrentColumn, 0.0f, LineSize * 7.0f);
			}
			CUIRect FeetBox;
			const bool RenderWhiteFeetInput = ShouldRenderVisualBlock(LineSize + MarginExtraSmall);
			CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, RenderWhiteFeetInput ? &FeetBox : nullptr, &CurrentColumn);
			if(RenderWhiteFeetInput && g_Config.m_TcWhiteFeet)
			{
				if(DeferVisualNameplateHeavyControls)
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Current skin"), g_Config.m_TcWhiteFeetSkin[0] != '\0' ? g_Config.m_TcWhiteFeetSkin : "x_ninja");
					FeetBox.HSplitTop(MarginExtraSmall, nullptr, &FeetBox);
					Ui()->DoLabel(&FeetBox, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
				{
					FeetBox.HSplitTop(MarginExtraSmall, nullptr, &FeetBox);
					FeetBox.VSplitMid(&FeetBox, nullptr);
					static CLineInput s_WhiteFeet(g_Config.m_TcWhiteFeetSkin, sizeof(g_Config.m_TcWhiteFeetSkin));
					s_WhiteFeet.SetEmptyText("x_ninja");
					Ui()->DoEditBox(&s_WhiteFeet, &FeetBox, EditBoxFontSize);
				}
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};

		auto LayoutVisualEffectsSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect = CurrentColumn;
			CUIRect TmpLabel;
			CUIRect TmpButton;
			CUIRect TinyTeeConfig;
			auto ShouldRenderVisualBlock = [&](float Height) {
				return Render && ShouldRenderSection(CurrentColumn, 0.0f, Height);
			};
			auto SkipVisualBlock = [&](float Height) {
				SkipSection(CurrentColumn, 0.0f, Height);
			};
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpLabel, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Visual: Effects"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			if(ShouldRenderVisualBlock(22.0f))
			{
				if(DeferVisualEffectsHeavyControls)
				{
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					const char *pTinyTeeMode = !g_Config.m_TcTinyTees ? Localize("None") : (g_Config.m_TcTinyTeesOthers ? Localize("All") : Localize("Self"));
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Smaller tees"), pTinyTeeMode);
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
				{
					static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
					int Value = g_Config.m_TcTinyTees ? (g_Config.m_TcTinyTeesOthers ? 2 : 1) : 0;
					CPerfTimer TinyTeeModeTimer;
					if(DoLine_RadioMenu(CurrentColumn, Localize("Smaller tees"), s_vButtonContainers, {Localize("None"), Localize("Self"), Localize("All")}, {0, 1, 2}, Value))
					{
						g_Config.m_TcTinyTees = Value > 0 ? 1 : 0;
						g_Config.m_TcTinyTeesOthers = Value > 1 ? 1 : 0;
					}
					LogSettingsStage("tclient_settings_left_visual_tiny_tee_mode", TinyTeeModeTimer);
				}
			}
			else
			{
				SkipVisualBlock(22.0f);
			}
			const bool RenderTinyTeeSize = ShouldRenderVisualBlock(LineSize);
			CurrentColumn.HSplitTop(LineSize, RenderTinyTeeSize ? &TinyTeeConfig : &TmpButton, &CurrentColumn);
			if(RenderTinyTeeSize && g_Config.m_TcTinyTees > 0)
			{
				if(DeferVisualEffectsHeavyControls)
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "%s: %d%%", Localize("Tiny Tee Size"), g_Config.m_TcTinyTeeSize);
					Ui()->DoLabel(&TinyTeeConfig, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
				{
					CPerfTimer TinyTeeSizeTimer;
					Ui()->DoScrollbarOption(&g_Config.m_TcTinyTeeSize, &g_Config.m_TcTinyTeeSize, &TinyTeeConfig, Localize("Tiny Tee Size"), 85, 115);
					LogSettingsStage("tclient_settings_left_visual_tiny_tee_size", TinyTeeSizeTimer);
				}
			}

			if(ShouldRenderVisualBlock(LineSize))
			{
				CPerfTimer MainControlsTimer;
				if(DeferVisualEffectsHeavyControls)
				{
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Enable Jelly Tee"), g_Config.m_QmJellyTee ? Localize("On") : Localize("Off"));
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
				{
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTee, Localize("Enable Jelly Tee"), &g_Config.m_QmJellyTee, &CurrentColumn, LineSize);
				}
				LogSettingsStage("tclient_settings_left_visual_main_controls", MainControlsTimer);
			}
			else
				SkipVisualBlock(LineSize);
			if(g_Config.m_QmJellyTee)
			{
				if(ShouldRenderVisualBlock(LineSize * 3.0f))
				{
					if(DeferVisualEffectsHeavyControls)
					{
						char aBuf[128];
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Jelly others"), g_Config.m_QmJellyTeeOthers ? Localize("On") : Localize("Off"));
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Jelly strength"), g_Config.m_QmJellyTeeStrength);
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Jelly duration"), g_Config.m_QmJellyTeeDuration);
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
					}
					else
					{
						CPerfTimer JellyTimer;
						DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTeeOthers, Localize("Jelly others"), &g_Config.m_QmJellyTeeOthers, &CurrentColumn, LineSize);
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						Ui()->DoScrollbarOption(&g_Config.m_QmJellyTeeStrength, &g_Config.m_QmJellyTeeStrength, &Button, Localize("Jelly strength"), 0, 1000);
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						Ui()->DoScrollbarOption(&g_Config.m_QmJellyTeeDuration, &g_Config.m_QmJellyTeeDuration, &Button, Localize("Jelly duration"), 1, 500);
						LogSettingsStage("tclient_settings_left_visual_jelly", JellyTimer);
					}
				}
				else
				{
					SkipVisualBlock(LineSize * 3.0f);
				}
			}
			if(ShouldRenderVisualBlock(22.0f + LineSize))
			{
				if(DeferVisualEffectsHeavyControls)
				{
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					const char *pFlagMode = g_Config.m_TcFakeCtfFlags == 1 ? Localize("Red") : (g_Config.m_TcFakeCtfFlags == 2 ? Localize("Blue") : Localize("None"));
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Fake CTF flags"), pFlagMode);
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Show moving tiles in entities"), g_Config.m_TcMovingTilesEntities ? Localize("On") : Localize("Off"));
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
				{
					static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
					int Value = g_Config.m_TcFakeCtfFlags;
					CPerfTimer FakeFlagsTimer;
					if(DoLine_RadioMenu(CurrentColumn, Localize("Fake CTF flags"), s_vButtonContainers, {Localize("None"), Localize("Red"), Localize("Blue")}, {0, 1, 2}, Value))
						g_Config.m_TcFakeCtfFlags = Value;
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMovingTilesEntities, Localize("Show moving tiles in entities"), &g_Config.m_TcMovingTilesEntities, &CurrentColumn, LineSize);
					LogSettingsStage("tclient_settings_left_visual_fake_flags", FakeFlagsTimer);
				}
			}
			else
			{
				SkipVisualBlock(22.0f + LineSize);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutInputSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpButton;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpButton, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Input"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			if(Render)
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInput, Localize("Fast input (reduce visual latency)"), &g_Config.m_TcFastInput, &CurrentColumn, LineSize);
			else
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpButton, &CurrentColumn);
			if(Render)
				DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, Localize("Amount"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			if(Render)
			{
				if(g_Config.m_TcFastInput)
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, Localize("Fast input others"), &g_Config.m_TcFastInputOthers, &CurrentColumn, LineSize);
				else
					CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, Localize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &CurrentColumn, LineSize);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutAntiLatencyToolsSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpButton;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpButton, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Anti Latency Tools"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpButton, &CurrentColumn);
			if(Render)
				Ui()->DoScrollbarOption(&g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, Localize("Prediction Margin"), 10, 75, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			if(Render)
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRemoveAnti, Localize("Remove prediction & antiping in freeze"), &g_Config.m_TcRemoveAnti, &CurrentColumn, LineSize);
			else
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			if(g_Config.m_TcRemoveAnti)
			{
				if(Render)
				{
					if(g_Config.m_TcUnfreezeLagDelayTicks < g_Config.m_TcUnfreezeLagTicks)
						g_Config.m_TcUnfreezeLagDelayTicks = g_Config.m_TcUnfreezeLagTicks;
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					DoSliderWithScaledValue(&g_Config.m_TcUnfreezeLagTicks, &g_Config.m_TcUnfreezeLagTicks, &Button, Localize("Amount"), 100, 300, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					DoSliderWithScaledValue(&g_Config.m_TcUnfreezeLagDelayTicks, &g_Config.m_TcUnfreezeLagDelayTicks, &Button, Localize("Delay"), 100, 3000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
				}
				else
				{
					CurrentColumn.HSplitTop(LineSize * 2.0f, nullptr, &CurrentColumn);
				}
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize * 2.0f, nullptr, &CurrentColumn);
			}
			if(Render)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcUnpredOthersInFreeze, Localize("Dont predict other players if you are frozen"), &g_Config.m_TcUnpredOthersInFreeze, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPredMarginInFreeze, Localize("Adjust your prediction margin while frozen"), &g_Config.m_TcPredMarginInFreeze, &CurrentColumn, LineSize);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize * 2.0f, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpButton, &CurrentColumn);
			if(Render && g_Config.m_TcPredMarginInFreeze)
				Ui()->DoScrollbarOption(&g_Config.m_TcPredMarginInFreezeAmount, &g_Config.m_TcPredMarginInFreezeAmount, &Button, Localize("Frozen Margin"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "ms");
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutAntiPingSmoothingSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpButton;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpButton, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Anti Ping Smoothing"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			if(Render)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingImproved, Localize("Use new smoothing algorithm"), &g_Config.m_TcAntiPingImproved, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingStableDirection, Localize("Optimistic prediction along stable direction"), &g_Config.m_TcAntiPingStableDirection, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingNegativeBuffer, Localize("Negative stability buffer (for Gores)"), &g_Config.m_TcAntiPingNegativeBuffer, &CurrentColumn, LineSize);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize * 3.0f, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpButton, &CurrentColumn);
			if(Render)
				Ui()->DoScrollbarOption(&g_Config.m_TcAntiPingUncertaintyScale, &g_Config.m_TcAntiPingUncertaintyScale, &Button, Localize("Uncertainty duration"), 50, 400, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutAutoExecuteSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect Box;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Auto execute"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			const bool RenderBeforeConnectInput = Render && ShouldRenderSection(CurrentColumn, 0.0f, LineSize + MarginExtraSmall);
			CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, RenderBeforeConnectInput ? &Box : nullptr, &CurrentColumn);
			if(RenderBeforeConnectInput)
			{
				Box.VSplitMid(&Label, &Button);
				Ui()->DoLabel(&Label, Localize("Execute before connecting"), FontSize, TEXTALIGN_ML);
				static CLineInput s_LineInput(g_Config.m_TcExecuteOnConnect, sizeof(g_Config.m_TcExecuteOnConnect));
				Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);

			const bool RenderOnConnectInput = Render && ShouldRenderSection(CurrentColumn, 0.0f, LineSize + MarginExtraSmall);
			CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, RenderOnConnectInput ? &Box : nullptr, &CurrentColumn);
			if(RenderOnConnectInput)
			{
				Box.VSplitMid(&Label, &Button);
				Ui()->DoLabel(&Label, Localize("Execute on connect"), FontSize, TEXTALIGN_ML);
				static CLineInput s_LineInput(g_Config.m_TcExecuteOnJoin, sizeof(g_Config.m_TcExecuteOnJoin));
				Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
			}

			const bool RenderDelaySlider = Render && ShouldRenderSection(CurrentColumn, 0.0f, LineSize);
			CurrentColumn.HSplitTop(LineSize, RenderDelaySlider ? &Button : nullptr, &CurrentColumn);
			if(RenderDelaySlider)
				DoSliderWithScaledValue(&g_Config.m_TcExecuteOnJoinDelay, &g_Config.m_TcExecuteOnJoinDelay, &Button, Localize("Delay"), 140, 2000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutVotingSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect VoteMessage;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Voting"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(Render)
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoVoteWhenFar, Localize("Automatically vote no on map changes"), &g_Config.m_TcAutoVoteWhenFar, &CurrentColumn, LineSize);
			else
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoScrollbarOption(&g_Config.m_TcAutoVoteWhenFarTime, &g_Config.m_TcAutoVoteWhenFarTime, &Button, Localize("Minimum time"), 1, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, Localize(" min"));

			CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, Render ? &VoteMessage : &TmpRect, &CurrentColumn);
			if(Render)
			{
				VoteMessage.HSplitTop(MarginExtraSmall, nullptr, &VoteMessage);
				VoteMessage.VSplitMid(&Label, &VoteMessage);
				Ui()->DoLabel(&Label, Localize("Message to send in chat:"), FontSize, TEXTALIGN_ML);
				static CLineInput s_VoteMessage(g_Config.m_TcAutoVoteWhenFarMessage, sizeof(g_Config.m_TcAutoVoteWhenFarMessage));
				s_VoteMessage.SetEmptyText(Localize("Leave empty to disable"));
				Ui()->DoEditBox(&s_VoteMessage, &VoteMessage, EditBoxFontSize);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutPetSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect PetSkinBox;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Pet"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(Render)
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPetShow, Localize("Show the pet"), &g_Config.m_TcPetShow, &CurrentColumn, LineSize);
			else
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoScrollbarOption(&g_Config.m_TcPetSize, &g_Config.m_TcPetSize, &Button, Localize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoScrollbarOption(&g_Config.m_TcPetAlpha, &g_Config.m_TcPetAlpha, &Button, Localize("Pet alpha"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, Render ? &PetSkinBox : &TmpRect, &CurrentColumn);
			if(Render)
			{
				PetSkinBox.VSplitMid(&Label, &Button);
				Ui()->DoLabel(&Label, Localize("Pet Skin:"), FontSize, TEXTALIGN_ML);
				static CLineInput s_PetSkin(g_Config.m_TcPetSkin, sizeof(g_Config.m_TcPetSkin));
				Ui()->DoEditBox(&s_PetSkin, &Button, EditBoxFontSize);
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutAutoReplySection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect ReplyRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Auto reply"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(Render)
			{
				CPerfTimer MutedTimer;
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMuted, Localize("Automatically reply to muted players"), &g_Config.m_TcAutoReplyMuted, &CurrentColumn, LineSize);
				CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, &ReplyRect, &CurrentColumn);
				if(g_Config.m_TcAutoReplyMuted)
				{
					ReplyRect.HSplitTop(MarginExtraSmall, nullptr, &ReplyRect);
					static CLineInput s_MutedReply(g_Config.m_TcAutoReplyMutedMessage, sizeof(g_Config.m_TcAutoReplyMutedMessage));
					s_MutedReply.SetEmptyText(Localize("I muted you"));
					Ui()->DoEditBox(&s_MutedReply, &ReplyRect, EditBoxFontSize);
				}
				LogSettingsStage("tclient_settings_left_auto_reply_muted", MutedTimer);
			}
			else
				CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);

			if(Render)
			{
				CPerfTimer MinimizedTimer;
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMinimized, Localize("Automatically reply while the window is unfocused"), &g_Config.m_TcAutoReplyMinimized, &CurrentColumn, LineSize);
				CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, &ReplyRect, &CurrentColumn);
				if(g_Config.m_TcAutoReplyMinimized)
				{
					ReplyRect.HSplitTop(MarginExtraSmall, nullptr, &ReplyRect);
					static CLineInput s_MinimizedReply(g_Config.m_TcAutoReplyMinimizedMessage, sizeof(g_Config.m_TcAutoReplyMinimizedMessage));
					s_MinimizedReply.SetEmptyText(Localize("I am away from the game window"));
					Ui()->DoEditBox(&s_MinimizedReply, &ReplyRect, EditBoxFontSize);
				}
				LogSettingsStage("tclient_settings_left_auto_reply_minimized", MinimizedTimer);
			}
			else
				CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);

			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutPlayerIndicatorSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Player indicator"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(Render && ShouldRenderSection(CurrentColumn, 0.0f, LineSize * 6.0f))
			{
				CPerfTimer BaseTimer;
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPlayerIndicator, Localize("Show any enabled Indicators"), &g_Config.m_TcPlayerIndicator, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorHideVisible, Localize("Hide indicator for tees on your screen"), &g_Config.m_TcIndicatorHideVisible, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPlayerIndicatorFreeze, Localize("Show only freeze Players"), &g_Config.m_TcPlayerIndicatorFreeze, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorTeamOnly, Localize("Only show after joining a team"), &g_Config.m_TcIndicatorTeamOnly, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorTees, Localize("Render tiny tees instead of circles"), &g_Config.m_TcIndicatorTees, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicator, Localize("Use warlist groups for indicator"), &g_Config.m_TcWarListIndicator, &CurrentColumn, LineSize);
				LogSettingsStage("tclient_settings_left_player_indicator_base", BaseTimer);
			}
			else
				SkipSection(CurrentColumn, 0.0f, LineSize * 6.0f);

			if(Render && ShouldRenderSection(CurrentColumn, 0.0f, LineSize * 6.0f))
			{
				CPerfTimer DistanceTimer;
				CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
				Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorRadius, &g_Config.m_TcIndicatorRadius, &Button, Localize("Indicator size"), 1, 16);
				CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
				Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOpacity, &g_Config.m_TcIndicatorOpacity, &Button, Localize("Indicator opacity"), 0, 100);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorVariableDistance, Localize("Change indicator offset based on distance to other tees"), &g_Config.m_TcIndicatorVariableDistance, &CurrentColumn, LineSize);
				if(g_Config.m_TcIndicatorVariableDistance)
				{
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffset, &g_Config.m_TcIndicatorOffset, &Button, Localize("Indicator min offset"), 16, 200);
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffsetMax, &g_Config.m_TcIndicatorOffsetMax, &Button, Localize("Indicator max offset"), 16, 200);
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorMaxDistance, &g_Config.m_TcIndicatorMaxDistance, &Button, Localize("Indicator max distance"), 500, 7000);
				}
				else
				{
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffset, &g_Config.m_TcIndicatorOffset, &Button, Localize("Indicator offset"), 16, 200);
					CurrentColumn.HSplitTop(LineSize * 2, nullptr, &CurrentColumn);
				}
				LogSettingsStage("tclient_settings_left_player_indicator_distance", DistanceTimer);
			}
			else
			{
				SkipSection(CurrentColumn, 0.0f, LineSize * 6.0f);
			}

			if(Render && g_Config.m_TcWarListIndicator && ShouldRenderSection(CurrentColumn, 0.0f, LineSize * 4.0f))
			{
				CPerfTimer WarListTimer;
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorColors, Localize("Use warlist colors instead of regular colors"), &g_Config.m_TcWarListIndicatorColors, &CurrentColumn, LineSize);
				char aBuf[128];
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorAll, Localize("Show all warlist groups"), &g_Config.m_TcWarListIndicatorAll, &CurrentColumn, LineSize);
				str_format(aBuf, sizeof(aBuf), Localize("Show %s group"), GameClient()->m_WarList.m_WarTypes.at(1)->m_aWarName);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorEnemy, aBuf, &g_Config.m_TcWarListIndicatorEnemy, &CurrentColumn, LineSize);
				str_format(aBuf, sizeof(aBuf), Localize("Show %s group"), GameClient()->m_WarList.m_WarTypes.at(2)->m_aWarName);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorTeam, aBuf, &g_Config.m_TcWarListIndicatorTeam, &CurrentColumn, LineSize);
				LogSettingsStage("tclient_settings_left_player_indicator_warlist", WarListTimer);
			}
			else if(!Render || g_Config.m_TcWarListIndicator)
				SkipSection(CurrentColumn, 0.0f, LineSize * 4.0f);

			if(Render && (!g_Config.m_TcWarListIndicatorColors || !g_Config.m_TcWarListIndicator) &&
				ShouldRenderSection(CurrentColumn, 0.0f, (ColorPickerLineSize + ColorPickerLineSpacing) * 3.0f))
			{
				CPerfTimer ColorsTimer;
				static CButtonContainer s_IndicatorAliveColorId, s_IndicatorDeadColorId, s_IndicatorSavedColorId;
				DoLine_ColorPicker(&s_IndicatorAliveColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &CurrentColumn, Localize("Indicator alive color"), &g_Config.m_TcIndicatorAlive, ColorRGBA(0.0f, 0.0f, 0.0f), false);
				DoLine_ColorPicker(&s_IndicatorDeadColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &CurrentColumn, Localize("Indicator in freeze color"), &g_Config.m_TcIndicatorFreeze, ColorRGBA(0.0f, 0.0f, 0.0f), false);
				DoLine_ColorPicker(&s_IndicatorSavedColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &CurrentColumn, Localize("Indicator safe color"), &g_Config.m_TcIndicatorSaved, ColorRGBA(0.0f, 0.0f, 0.0f), false);
				LogSettingsStage("tclient_settings_left_player_indicator_colors", ColorsTimer);
			}
			else if(!Render || (!g_Config.m_TcWarListIndicatorColors || !g_Config.m_TcWarListIndicator))
				SkipSection(CurrentColumn, 0.0f, (ColorPickerLineSize + ColorPickerLineSpacing) * 3.0f);

			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};

	// ***** Visual: Font & Cursor ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutVisualFontSection(MeasuredColumn, false);
		if(IsSectionVisible(BoxRect, CullContext))
		{
			CPerfTimer VisualSectionTimer;
			DrawSectionBox(BoxRect);
			if(CompactVisualFontSection)
			{
				char aBuf[128];
				const char *pCurrentFont = g_Config.m_TcCustomFont[0] != '\0' ? g_Config.m_TcCustomFont : Localize("Default");
				str_format(aBuf, sizeof(aBuf), "%s | %s %d%%", pCurrentFont, Localize("Cursor"), g_Config.m_TcCursorScale);
				DrawCompactDeferredSection(BoxRect, Localize("Visual: Font & Cursor"), aBuf);
				s_VisualFontSectionCachedHeight = BoxRect.h;
				Column = MeasuredColumn;
			}
			else
			{
				BoxRect = LayoutVisualFontSection(Column, true);
				s_VisualFontSectionCachedHeight = BoxRect.h;
			}
			LogSettingsStage("tclient_settings_left_visual_font_section", VisualSectionTimer);
		}
		else
		{
			s_VisualFontSectionCachedHeight = BoxRect.h;
			Column = MeasuredColumn;
		}
	}

	// ***** Visual: Nameplates ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutVisualNameplateSection(MeasuredColumn, false);
		if(IsSectionVisible(BoxRect, CullContext))
		{
			CPerfTimer VisualSectionTimer;
			DrawSectionBox(BoxRect);
			if(CompactVisualNameplateSection)
			{
				char aBuf[160];
				str_format(aBuf, sizeof(aBuf), "%s/%s/%s", g_Config.m_TcNameplatePingCircle ? Localize("Ping") : "-",
					g_Config.m_TcNameplateCountry ? Localize("Country") : "-",
					g_Config.m_TcWhiteFeet ? Localize("White feet") : "-");
				DrawCompactDeferredSection(BoxRect, Localize("Visual: Nameplates"), aBuf);
				s_VisualNameplateSectionCachedHeight = BoxRect.h;
				Column = MeasuredColumn;
			}
			else
			{
				BoxRect = LayoutVisualNameplateSection(Column, true);
				s_VisualNameplateSectionCachedHeight = BoxRect.h;
			}
			LogSettingsStage("tclient_settings_left_visual_nameplates_section", VisualSectionTimer);
		}
		else
		{
			s_VisualNameplateSectionCachedHeight = BoxRect.h;
			Column = MeasuredColumn;
		}
	}

	// ***** Visual: Effects ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutVisualEffectsSection(MeasuredColumn, false);
		if(IsSectionVisible(BoxRect, CullContext))
		{
			CPerfTimer VisualSectionTimer;
			DrawSectionBox(BoxRect);
			if(CompactVisualEffectsSection)
			{
				char aBuf[160];
				const char *pTinyTeeMode = !g_Config.m_TcTinyTees ? Localize("None") : (g_Config.m_TcTinyTeesOthers ? Localize("All") : Localize("Self"));
				str_format(aBuf, sizeof(aBuf), "%s | Jelly %s", pTinyTeeMode, g_Config.m_QmJellyTee ? Localize("On") : Localize("Off"));
				DrawCompactDeferredSection(BoxRect, Localize("Visual: Effects"), aBuf);
				s_VisualEffectsSectionCachedHeight = BoxRect.h;
				Column = MeasuredColumn;
			}
			else
			{
				BoxRect = LayoutVisualEffectsSection(Column, true);
				s_VisualEffectsSectionCachedHeight = BoxRect.h;
			}
			LogSettingsStage("tclient_settings_left_visual_effects_section", VisualSectionTimer);
		}
		else
		{
			s_VisualEffectsSectionCachedHeight = BoxRect.h;
			Column = MeasuredColumn;
		}
	}

		LogSettingsStage("tclient_settings_left_visual_total", VisualSectionsTotalTimer);

	// ***** Input ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutInputSection(MeasuredColumn, false);
		s_InputSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			DrawSectionBox(BoxRect);
			BoxRect = LayoutInputSection(Column, true);
			s_InputSectionCachedHeight = BoxRect.h;
		}
		else
			Column = MeasuredColumn;
	}

	// ***** Anti Latency Tools ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutAntiLatencyToolsSection(MeasuredColumn, false);
		s_AntiLatencyToolsSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			DrawSectionBox(BoxRect);
			BoxRect = LayoutAntiLatencyToolsSection(Column, true);
			s_AntiLatencyToolsSectionCachedHeight = BoxRect.h;
		}
		else
			Column = MeasuredColumn;
	}

	// ***** Improved Anti Ping ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutAntiPingSmoothingSection(MeasuredColumn, false);
		s_AntiPingSmoothingSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			DrawSectionBox(BoxRect);
			BoxRect = LayoutAntiPingSmoothingSection(Column, true);
			s_AntiPingSmoothingSectionCachedHeight = BoxRect.h;
		}
		else
			Column = MeasuredColumn;
	}

	// ***** Execute on join ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutAutoExecuteSection(MeasuredColumn, false);
		s_AutoExecuteSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			CPerfTimer AutoExecuteTimer;
			DrawSectionBox(BoxRect);
			BoxRect = LayoutAutoExecuteSection(Column, true);
			s_AutoExecuteSectionCachedHeight = BoxRect.h;
			LogSettingsStage("tclient_settings_left_auto_execute", AutoExecuteTimer);
		}
		else
			Column = MeasuredColumn;
	}

	// ***** Voting ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutVotingSection(MeasuredColumn, false);
		s_VotingSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			CPerfTimer VotingTimer;
			DrawSectionBox(BoxRect);
			BoxRect = LayoutVotingSection(Column, true);
			s_VotingSectionCachedHeight = BoxRect.h;
			LogSettingsStage("tclient_settings_left_voting", VotingTimer);
		}
		else
			Column = MeasuredColumn;
	}

	// ***** 自动回复 ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutAutoReplySection(MeasuredColumn, false);
		s_AutoReplySectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			CPerfTimer AutoReplyTimer;
			DrawSectionBox(BoxRect);
			BoxRect = LayoutAutoReplySection(Column, true);
			s_AutoReplySectionCachedHeight = BoxRect.h;
			LogSettingsStage("tclient_settings_left_auto_reply", AutoReplyTimer);
		}
		else
			Column = MeasuredColumn;
	}

	// ***** Player Indicator ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutPlayerIndicatorSection(MeasuredColumn, false);
		s_PlayerIndicatorSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			CPerfTimer PlayerIndicatorTimer;
			DrawSectionBox(BoxRect);
			BoxRect = LayoutPlayerIndicatorSection(Column, true);
			s_PlayerIndicatorSectionCachedHeight = BoxRect.h;
			LogSettingsStage("tclient_settings_left_player_indicator_total", PlayerIndicatorTimer);
		}
		else
			Column = MeasuredColumn;
	}

	// ***** 宠物 ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutPetSection(MeasuredColumn, false);
		s_PetSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			DrawSectionBox(BoxRect);
			BoxRect = LayoutPetSection(Column, true);
			s_PetSectionCachedHeight = BoxRect.h;
		}
		else
			Column = MeasuredColumn;
	}

		LeftView = Column;
		LogSettingsStage("tclient_settings_left_column", LeftColumnTimer);
	}

	// ***** RightView ***** //
	{
		CPerfTimer RightColumnTimer;
		Column = RightView;
		auto LayoutHudSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CurrentColumn.HSplitTop(Margin, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("HUD"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			if(Render)
			{
				if(DeferRightHeavySections)
				{
					char aBuf[128];
					auto DrawSummaryToggle = [&](const char *pTitle, bool Enabled) {
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %s", pTitle, Enabled ? Localize("On") : Localize("Off"));
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
					};
					DrawSummaryToggle(Localize("Show compact vote HUD"), g_Config.m_TcMiniVoteHud != 0);
					DrawSummaryToggle(Localize("Show position and angle (mini debug)"), g_Config.m_TcMiniDebug != 0);
					DrawSummaryToggle(Localize("Show the cursor while free spectating"), g_Config.m_TcRenderCursorSpec != 0);
				}
				else
				{
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniVoteHud, Localize("Show compact vote HUD"), &g_Config.m_TcMiniVoteHud, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniDebug, Localize("Show position and angle (mini debug)"), &g_Config.m_TcMiniDebug, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderCursorSpec, Localize("Show the cursor while free spectating"), &g_Config.m_TcRenderCursorSpec, &CurrentColumn, LineSize);
				}
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize * 3.0f, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render && g_Config.m_TcRenderCursorSpec)
			{
				if(DeferRightHeavySections)
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "%s: %d%%", Localize("Free spectate cursor opacity"), g_Config.m_TcRenderCursorSpecAlpha);
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
					Ui()->DoScrollbarOption(&g_Config.m_TcRenderCursorSpecAlpha, &g_Config.m_TcRenderCursorSpecAlpha, &Button, Localize("Free spectate cursor opacity"), 0, 100);
			}

			if(Render)
			{
				if(DeferRightHeavySections)
				{
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Notify when only one tee is still alive:"), g_Config.m_TcNotifyWhenLast ? Localize("On") : Localize("Off"));
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNotifyWhenLast, Localize("Notify when only one tee is still alive:"), &g_Config.m_TcNotifyWhenLast, &CurrentColumn, LineSize);
			}
			else
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			CUIRect NotificationConfig;
			CurrentColumn.HSplitTop(LineSize + MarginSmall, Render ? &NotificationConfig : &TmpRect, &CurrentColumn);
			if(Render)
			{
				CPerfTimer NotifyWhenLastTimer;
				if(g_Config.m_TcNotifyWhenLast)
				{
					if(DeferRightHeavySections)
					{
						char aBuf[160];
						NotificationConfig.HSplitTop(MarginSmall, nullptr, &NotificationConfig);
						str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Message"), g_Config.m_TcNotifyWhenLastText[0] != '\0' ? g_Config.m_TcNotifyWhenLastText : Localize("You're the last one!"));
						Ui()->DoLabel(&NotificationConfig, aBuf, FontSize, TEXTALIGN_ML);
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %d%%", Localize("Horizontal position"), g_Config.m_TcNotifyWhenLastX);
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %d%%", Localize("Vertical position"), g_Config.m_TcNotifyWhenLastY);
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Font size"), g_Config.m_TcNotifyWhenLastSize);
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
					}
					else
					{
						NotificationConfig.VSplitMid(&Button, &NotificationConfig);
						static CLineInput s_LastInput(g_Config.m_TcNotifyWhenLastText, sizeof(g_Config.m_TcNotifyWhenLastText));
						s_LastInput.SetEmptyText(Localize("You're the last one!"));
						Button.HSplitTop(MarginSmall, nullptr, &Button);
						Ui()->DoEditBox(&s_LastInput, &Button, EditBoxFontSize);
						static CButtonContainer s_ClientNotifyWhenLastColor;
						DoLine_ColorPicker(&s_ClientNotifyWhenLastColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &NotificationConfig, "", &g_Config.m_TcNotifyWhenLastColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastX, &g_Config.m_TcNotifyWhenLastX, &Button, Localize("Horizontal position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastY, &g_Config.m_TcNotifyWhenLastY, &Button, Localize("Vertical position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastSize, &g_Config.m_TcNotifyWhenLastSize, &Button, Localize("Font size"), 1, 50);
					}
				}
				else
				{
					CurrentColumn.HSplitTop(LineSize * 3.0f, nullptr, &CurrentColumn);
				}
				LogSettingsStage("tclient_settings_right_hud_notify_when_last", NotifyWhenLastTimer);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize * 3.0f, nullptr, &CurrentColumn);
			}

			if(Render)
			{
				if(DeferRightHeavySections)
				{
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Show the screen center line"), g_Config.m_TcShowCenter ? Localize("On") : Localize("Off"));
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowCenter, Localize("Show the screen center line"), &g_Config.m_TcShowCenter, &CurrentColumn, LineSize);
			}
			else
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(LineSize + MarginSmall, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
			{
				CPerfTimer ShowCenterTimer;
				if(g_Config.m_TcShowCenter)
				{
					if(DeferRightHeavySections)
					{
						char aBuf[128];
						str_format(aBuf, sizeof(aBuf), "%s: 0x%08x", Localize("Screen center line color"), g_Config.m_TcShowCenterColor);
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Screen center line width"), g_Config.m_TcShowCenterWidth);
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
					}
					else
					{
						static CButtonContainer s_ShowCenterLineColor;
						DoLine_ColorPicker(&s_ShowCenterLineColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Button, Localize("Screen center line color"), &g_Config.m_TcShowCenterColor, CConfig::ms_TcShowCenterColor, false, nullptr, true);
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						Ui()->DoScrollbarOption(&g_Config.m_TcShowCenterWidth, &g_Config.m_TcShowCenterWidth, &Button, Localize("Screen center line width"), 0, 20);
					}
				}
				else
				{
					CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
				}
				LogSettingsStage("tclient_settings_right_hud_show_center", ShowCenterTimer);
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutTeeStatusBarSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Tee status bar"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			if(Render)
			{
				if(DeferRightHeavySections)
				{
					char aBuf[128];
					auto DrawSummaryToggle = [&](const char *pTitle, bool Enabled) {
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %s", pTitle, Enabled ? Localize("On") : Localize("Off"));
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
					};
					DrawSummaryToggle(Localize("Show tee status bar"), g_Config.m_TcShowFrozenHud != 0);
					DrawSummaryToggle(Localize("Use custom skins instead of the ninja tee"), g_Config.m_TcShowFrozenHudSkins != 0);
					DrawSummaryToggle(Localize("Only show after joining a team"), g_Config.m_TcFrozenHudTeamOnly != 0);
				}
				else
				{
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHud, Localize("Show tee status bar"), &g_Config.m_TcShowFrozenHud, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHudSkins, Localize("Use custom skins instead of the ninja tee"), &g_Config.m_TcShowFrozenHudSkins, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFrozenHudTeamOnly, Localize("Only show after joining a team"), &g_Config.m_TcFrozenHudTeamOnly, &CurrentColumn, LineSize);
				}
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize * 3.0f, nullptr, &CurrentColumn);
			}
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
			{
				if(DeferRightHeavySections)
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Maximum rows"), g_Config.m_TcFrozenMaxRows);
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
					Ui()->DoScrollbarOption(&g_Config.m_TcFrozenMaxRows, &g_Config.m_TcFrozenMaxRows, &Button, Localize("Maximum rows"), 1, 6);
			}
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
			{
				if(DeferRightHeavySections)
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Tee size"), g_Config.m_TcFrozenHudTeeSize);
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
					Ui()->DoScrollbarOption(&g_Config.m_TcFrozenHudTeeSize, &g_Config.m_TcFrozenHudTeeSize, &Button, Localize("Tee size"), 8, 27);
			}
			if(Render)
			{
				if(DeferRightHeavySections)
				{
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Show the number of tees still alive"), g_Config.m_TcShowFrozenText >= 1 ? Localize("On") : Localize("Off"));
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Show the number of frozen tees"), g_Config.m_TcShowFrozenText == 2 ? Localize("On") : Localize("Off"));
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
				{
					CUIRect CheckBoxRect, CheckBoxRect2;
					CurrentColumn.HSplitTop(LineSize, &CheckBoxRect, &CurrentColumn);
					CurrentColumn.HSplitTop(LineSize, &CheckBoxRect2, &CurrentColumn);
					if(DoButton_CheckBox(&g_Config.m_TcShowFrozenText, Localize("Show the number of tees still alive"), g_Config.m_TcShowFrozenText >= 1, &CheckBoxRect))
						g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText >= 1 ? 0 : 1;
					if(g_Config.m_TcShowFrozenText)
					{
						static int s_CountFrozenText = 0;
						if(DoButton_CheckBox(&s_CountFrozenText, Localize("Show the number of frozen tees"), g_Config.m_TcShowFrozenText == 2, &CheckBoxRect2))
							g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText != 2 ? 2 : 1;
					}
				}
			}
			else
			{
				CurrentColumn.HSplitTop(LineSize * 2.0f, nullptr, &CurrentColumn);
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutTileOutlinesSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			auto ShouldRenderTileOutlineBlock = [&](float Height) {
				return Render && ShouldRenderSection(CurrentColumn, 0.0f, Height);
			};
			auto SkipTileOutlineBlock = [&](float Height) {
				SkipSection(CurrentColumn, 0.0f, Height);
			};
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Tile outlines"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(ShouldRenderTileOutlineBlock(LineSize * 4.0f))
			{
				CPerfTimer TileOutlinesBaseTimer;
				if(DeferRightHeavySections)
				{
					char aBuf[128];
					auto DrawSummaryToggle = [&](const char *pTitle, bool Enabled) {
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %s", pTitle, Enabled ? Localize("On") : Localize("Off"));
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
					};
					DrawSummaryToggle(Localize("Show all enabled outlines"), g_Config.m_TcOutline != 0);
					DrawSummaryToggle(Localize("Only show outlines in the entities layer"), g_Config.m_TcOutlineEntities != 0);
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %d%%", Localize("Outline opacity"), g_Config.m_TcOutlineAlpha);
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %d%%", Localize("Solid tile outline opacity"), g_Config.m_TcOutlineSolidAlpha);
					Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
				{
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutline, Localize("Show all enabled outlines"), &g_Config.m_TcOutline, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineEntities, Localize("Only show outlines in the entities layer"), &g_Config.m_TcOutlineEntities, &CurrentColumn, LineSize);
					CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
					Ui()->DoScrollbarOption(&g_Config.m_TcOutlineAlpha, &g_Config.m_TcOutlineAlpha, &Button, Localize("Outline opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
					Ui()->DoScrollbarOption(&g_Config.m_TcOutlineSolidAlpha, &g_Config.m_TcOutlineSolidAlpha, &Button, Localize("Solid tile outline opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				}
				LogSettingsStage("tclient_settings_right_tile_outlines_base", TileOutlinesBaseTimer);
			}
			else
			{
				SkipTileOutlineBlock(LineSize * 4.0f);
			}

			auto DoOutlineType = [&](const char *pStage, CButtonContainer &ButtonContainer, const char *pName, int &Enable, int &Width, unsigned int &Color, const unsigned int &ColorDefault) {
				CPerfTimer OutlineTypeTimer;
				if(Render)
					DoLine_ColorPicker(&ButtonContainer, ColorPickerLineSize, ColorPickerLabelSize, 0, &CurrentColumn, pName, &Color, ColorDefault, true, &Enable, true);
				else
					CurrentColumn.HSplitTop(ColorPickerLineSize, nullptr, &CurrentColumn);
				CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
				if(Render)
					Ui()->DoScrollbarOption(&Width, &Width, &Button, Localize("Width", "Outlines"), 1, 16);
				CurrentColumn.HSplitTop(ColorPickerLineSpacing, nullptr, &CurrentColumn);
				if(Render)
					LogSettingsStage(pStage, OutlineTypeTimer);
			};

			CurrentColumn.HSplitTop(ColorPickerLineSpacing, nullptr, &CurrentColumn);
			static CButtonContainer s_aOutlineButtonContainers[5];
			static CButtonContainer s_OutlineDeepFreezeColorId;
			static CButtonContainer s_OutlineDeepUnfreezeColorId;
			const float OutlineEntryHeight = ColorPickerLineSize + LineSize + ColorPickerLineSpacing;
			const float OutlineColorOnlyHeight = ColorPickerLineSize + ColorPickerLineSpacing;
			if(DeferRightHeavySections)
			{
				auto DrawDeferredOutlineLabel = [&](const char *pName, int Enabled, int Width) {
					CUIRect SummaryLine;
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &SummaryLine, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %s, %s %d", pName, Enabled ? Localize("On") : Localize("Off"), Localize("Width"), Width);
					Ui()->DoLabel(&SummaryLine, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(ColorPickerLineSpacing, nullptr, &CurrentColumn);
				};
				if(ShouldRenderTileOutlineBlock(OutlineEntryHeight))
					DrawDeferredOutlineLabel(Localize("Solid"), g_Config.m_TcOutlineSolid, g_Config.m_TcOutlineWidthSolid);
				else
					SkipTileOutlineBlock(OutlineEntryHeight);
				if(ShouldRenderTileOutlineBlock(OutlineEntryHeight))
					DrawDeferredOutlineLabel(Localize("Freeze"), g_Config.m_TcOutlineFreeze, g_Config.m_TcOutlineWidthFreeze);
				else
					SkipTileOutlineBlock(OutlineEntryHeight);
				if(ShouldRenderTileOutlineBlock(OutlineColorOnlyHeight))
				{
					CUIRect SummaryLine;
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &SummaryLine, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: 0x%08x", Localize("Deep freeze color"), g_Config.m_TcOutlineColorDeepFreeze);
					Ui()->DoLabel(&SummaryLine, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(ColorPickerLineSpacing, nullptr, &CurrentColumn);
				}
				else
					SkipTileOutlineBlock(OutlineColorOnlyHeight);
				if(ShouldRenderTileOutlineBlock(OutlineEntryHeight))
					DrawDeferredOutlineLabel(Localize("Unfreeze"), g_Config.m_TcOutlineUnfreeze, g_Config.m_TcOutlineWidthUnfreeze);
				else
					SkipTileOutlineBlock(OutlineEntryHeight);
				if(ShouldRenderTileOutlineBlock(OutlineColorOnlyHeight))
				{
					CUIRect SummaryLine;
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &SummaryLine, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: 0x%08x", Localize("Deep unfreeze color"), g_Config.m_TcOutlineColorDeepUnfreeze);
					Ui()->DoLabel(&SummaryLine, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(ColorPickerLineSpacing, nullptr, &CurrentColumn);
				}
				else
					SkipTileOutlineBlock(OutlineColorOnlyHeight);
				if(ShouldRenderTileOutlineBlock(OutlineEntryHeight))
					DrawDeferredOutlineLabel(Localize("Kill"), g_Config.m_TcOutlineKill, g_Config.m_TcOutlineWidthKill);
				else
					SkipTileOutlineBlock(OutlineEntryHeight);
				if(ShouldRenderTileOutlineBlock(OutlineEntryHeight))
					DrawDeferredOutlineLabel(Localize("Tele"), g_Config.m_TcOutlineTele, g_Config.m_TcOutlineWidthTele);
				else
					SkipTileOutlineBlock(OutlineEntryHeight);
			}
			else
			{
				if(ShouldRenderTileOutlineBlock(OutlineEntryHeight))
					DoOutlineType("tclient_settings_right_tile_outlines_solid", s_aOutlineButtonContainers[0], Localize("Solid"), g_Config.m_TcOutlineSolid, g_Config.m_TcOutlineWidthSolid, g_Config.m_TcOutlineColorSolid, CConfig::ms_TcOutlineColorSolid);
				else
					SkipTileOutlineBlock(OutlineEntryHeight);
				if(ShouldRenderTileOutlineBlock(OutlineEntryHeight))
					DoOutlineType("tclient_settings_right_tile_outlines_freeze", s_aOutlineButtonContainers[1], Localize("Freeze"), g_Config.m_TcOutlineFreeze, g_Config.m_TcOutlineWidthFreeze, g_Config.m_TcOutlineColorFreeze, CConfig::ms_TcOutlineColorFreeze);
				else
					SkipTileOutlineBlock(OutlineEntryHeight);
				{
					if(ShouldRenderTileOutlineBlock(OutlineColorOnlyHeight))
					{
						CPerfTimer DeepFreezeTimer;
						DoLine_ColorPicker(&s_OutlineDeepFreezeColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &CurrentColumn, Localize("Deep freeze color"), &g_Config.m_TcOutlineColorDeepFreeze, CConfig::ms_TcOutlineColorDeepFreeze, false, nullptr, true);
						LogSettingsStage("tclient_settings_right_tile_outlines_deepfreeze_color", DeepFreezeTimer);
					}
					else
					{
						SkipTileOutlineBlock(OutlineColorOnlyHeight);
					}
				}
				if(ShouldRenderTileOutlineBlock(OutlineEntryHeight))
					DoOutlineType("tclient_settings_right_tile_outlines_unfreeze", s_aOutlineButtonContainers[2], Localize("Unfreeze"), g_Config.m_TcOutlineUnfreeze, g_Config.m_TcOutlineWidthUnfreeze, g_Config.m_TcOutlineColorUnfreeze, CConfig::ms_TcOutlineColorUnfreeze);
				else
					SkipTileOutlineBlock(OutlineEntryHeight);
				{
					if(ShouldRenderTileOutlineBlock(OutlineColorOnlyHeight))
					{
						CPerfTimer DeepUnfreezeTimer;
						DoLine_ColorPicker(&s_OutlineDeepUnfreezeColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &CurrentColumn, Localize("Deep unfreeze color"), &g_Config.m_TcOutlineColorDeepUnfreeze, CConfig::ms_TcOutlineColorDeepUnfreeze, false, nullptr, true);
						LogSettingsStage("tclient_settings_right_tile_outlines_deepunfreeze_color", DeepUnfreezeTimer);
					}
					else
					{
						SkipTileOutlineBlock(OutlineColorOnlyHeight);
					}
				}
				if(ShouldRenderTileOutlineBlock(OutlineEntryHeight))
					DoOutlineType("tclient_settings_right_tile_outlines_kill", s_aOutlineButtonContainers[3], Localize("Kill"), g_Config.m_TcOutlineKill, g_Config.m_TcOutlineWidthKill, g_Config.m_TcOutlineColorKill, CConfig::ms_TcOutlineColorKill);
				else
					SkipTileOutlineBlock(OutlineEntryHeight);
				if(ShouldRenderTileOutlineBlock(OutlineEntryHeight))
					DoOutlineType("tclient_settings_right_tile_outlines_tele", s_aOutlineButtonContainers[4], Localize("Tele"), g_Config.m_TcOutlineTele, g_Config.m_TcOutlineWidthTele, g_Config.m_TcOutlineColorTele, CConfig::ms_TcOutlineColorTele);
				else
					SkipTileOutlineBlock(OutlineEntryHeight);
			}
			CurrentColumn.h -= ColorPickerLineSpacing;
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutGhostToolsSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Ghost tools"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(Render)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowOthersGhosts, Localize("Show unpredicted ghosts for other players"), &g_Config.m_TcShowOthersGhosts, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcSwapGhosts, Localize("Swap ghosts with regular players"), &g_Config.m_TcSwapGhosts, &CurrentColumn, LineSize);
			}
			else
				CurrentColumn.HSplitTop(LineSize * 2.0f, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoScrollbarOption(&g_Config.m_TcPredGhostsAlpha, &g_Config.m_TcPredGhostsAlpha, &Button, Localize("Predicted ghost opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoScrollbarOption(&g_Config.m_TcUnpredGhostsAlpha, &g_Config.m_TcUnpredGhostsAlpha, &Button, Localize("Unpredicted ghost opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
			if(Render)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcHideFrozenGhosts, Localize("Hide ghosts of frozen players"), &g_Config.m_TcHideFrozenGhosts, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderGhostAsCircle, Localize("Render ghosts as circles"), &g_Config.m_TcRenderGhostAsCircle, &CurrentColumn, LineSize);
				static CButtonContainer s_ReaderButtonGhost, s_ClearButtonGhost;
				DoLine_KeyReader(CurrentColumn, s_ReaderButtonGhost, s_ClearButtonGhost, Localize("Toggle ghost key"), "toggle tc_show_others_ghosts 0 1");
			}
			else
				CurrentColumn.HSplitTop(LineSize * 3.0f, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutRainbowSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect RainbowDropDownRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Rainbow"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(Render)
			{
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowTees, Localize("Rainbow Tees"), &g_Config.m_TcRainbowTees, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowWeapon, Localize("Rainbow weapons"), &g_Config.m_TcRainbowWeapon, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowHook, Localize("Rainbow hook"), &g_Config.m_TcRainbowHook, &CurrentColumn, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowOthers, Localize("Rainbow others"), &g_Config.m_TcRainbowOthers, &CurrentColumn, LineSize);
			}
			else
				CurrentColumn.HSplitTop(LineSize * 4.0f, nullptr, &CurrentColumn);

			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			static std::vector<const char *> s_RainbowDropDownNames;
			s_RainbowDropDownNames = {Localize("Rainbow"), Localize("Pulse"), Localize("Black"), Localize("Random")};
			static CUi::SDropDownState s_RainbowDropDownState;
			static CScrollRegion s_RainbowDropDownScrollRegion;
			s_RainbowDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_RainbowDropDownScrollRegion;
			int RainbowSelectedOld = g_Config.m_TcRainbowMode - 1;
			CurrentColumn.HSplitTop(LineSize, Render ? &RainbowDropDownRect : &TmpRect, &CurrentColumn);
			if(Render)
			{
				const int RainbowSelectedNew = Ui()->DoDropDown(&RainbowDropDownRect, RainbowSelectedOld, s_RainbowDropDownNames.data(), s_RainbowDropDownNames.size(), s_RainbowDropDownState);
				if(RainbowSelectedOld != RainbowSelectedNew)
					g_Config.m_TcRainbowMode = RainbowSelectedNew + 1;
			}
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(LineSize, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoScrollbarOption(&g_Config.m_TcRainbowSpeed, &g_Config.m_TcRainbowSpeed, &Button, Localize("Rainbow speed"), 0, 5000, &CUi::ms_LogarithmicScrollbarScale, 0, "%");
			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutTeeTrailsSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect TrailDropDownRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Tee Trails"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(Render)
			{
				CPerfTimer BaseTimer;
				if(DeferRightHeavySections)
				{
					char aBuf[128];
					auto DrawSummaryToggle = [&](const char *pTitle, bool Enabled) {
						CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
						str_format(aBuf, sizeof(aBuf), "%s: %s", pTitle, Enabled ? Localize("On") : Localize("Off"));
						Ui()->DoLabel(&Button, aBuf, FontSize, TEXTALIGN_ML);
					};
					DrawSummaryToggle(Localize("Enable tee trails"), g_Config.m_TcTeeTrail != 0);
					DrawSummaryToggle(Localize("Show other tees' trails"), g_Config.m_TcTeeTrailOthers != 0);
					DrawSummaryToggle(Localize("Fade trail alpha"), g_Config.m_TcTeeTrailFade != 0);
					DrawSummaryToggle(Localize("Taper trail width"), g_Config.m_TcTeeTrailTaper != 0);
				}
				else
				{
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrail, Localize("Enable tee trails"), &g_Config.m_TcTeeTrail, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailOthers, Localize("Show other tees' trails"), &g_Config.m_TcTeeTrailOthers, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailFade, Localize("Fade trail alpha"), &g_Config.m_TcTeeTrailFade, &CurrentColumn, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailTaper, Localize("Taper trail width"), &g_Config.m_TcTeeTrailTaper, &CurrentColumn, LineSize);
				}
				LogSettingsStage("tclient_settings_right_tee_trails_base", BaseTimer);
			}
			else
				CurrentColumn.HSplitTop(LineSize * 4.0f, nullptr, &CurrentColumn);

			CurrentColumn.HSplitTop(MarginExtraSmall, nullptr, &CurrentColumn);
			static std::vector<const char *> s_TrailDropDownNames;
			s_TrailDropDownNames = {Localize("Solid"), Localize("Tee"), Localize("Rainbow"), Localize("Speed")};
			static CUi::SDropDownState s_TrailDropDownState;
			static CScrollRegion s_TrailDropDownScrollRegion;
			s_TrailDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TrailDropDownScrollRegion;
			int TrailSelectedOld = g_Config.m_TcTeeTrailColorMode - 1;
			if(Render)
			{
				CurrentColumn.HSplitTop(LineSize, &TrailDropDownRect, &CurrentColumn);
				if(DeferRightHeavySections)
				{
					const char *pMode = s_TrailDropDownNames[std::clamp(TrailSelectedOld, 0, (int)s_TrailDropDownNames.size() - 1)];
					Ui()->DoLabel(&TrailDropDownRect, pMode, FontSize, TEXTALIGN_ML);
				}
				else
				{
					CPerfTimer DropDownTimer;
					const int TrailSelectedNew = Ui()->DoDropDown(&TrailDropDownRect, TrailSelectedOld, s_TrailDropDownNames.data(), s_TrailDropDownNames.size(), s_TrailDropDownState);
					if(TrailSelectedOld != TrailSelectedNew)
						g_Config.m_TcTeeTrailColorMode = TrailSelectedNew + 1;
					LogSettingsStage("tclient_settings_right_tee_trails_dropdown", DropDownTimer);
				}
			}
			else
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(Render && g_Config.m_TcTeeTrailColorMode == CTrails::COLORMODE_SOLID)
			{
				if(DeferRightHeavySections)
				{
					CUIRect SummaryLine;
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &SummaryLine, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: 0x%08x", Localize("Tee trail color"), g_Config.m_TcTeeTrailColor);
					Ui()->DoLabel(&SummaryLine, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(ColorPickerLineSpacing, nullptr, &CurrentColumn);
				}
				else
				{
					CPerfTimer ColorTimer;
					static CButtonContainer s_TeeTrailColor;
					DoLine_ColorPicker(&s_TeeTrailColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &CurrentColumn, Localize("Tee trail color"), &g_Config.m_TcTeeTrailColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
					LogSettingsStage("tclient_settings_right_tee_trails_color", ColorTimer);
				}
			}
			else
				CurrentColumn.HSplitTop(ColorPickerLineSize + ColorPickerLineSpacing, nullptr, &CurrentColumn);

			if(Render)
			{
				if(DeferRightHeavySections)
				{
					CUIRect SummaryLine;
					char aBuf[128];
					CurrentColumn.HSplitTop(LineSize, &SummaryLine, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Trail width"), g_Config.m_TcTeeTrailWidth);
					Ui()->DoLabel(&SummaryLine, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(LineSize, &SummaryLine, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Trail length"), g_Config.m_TcTeeTrailLength);
					Ui()->DoLabel(&SummaryLine, aBuf, FontSize, TEXTALIGN_ML);
					CurrentColumn.HSplitTop(LineSize, &SummaryLine, &CurrentColumn);
					str_format(aBuf, sizeof(aBuf), "%s: %d%%", Localize("Trail alpha"), g_Config.m_TcTeeTrailAlpha);
					Ui()->DoLabel(&SummaryLine, aBuf, FontSize, TEXTALIGN_ML);
				}
				else
				{
					CPerfTimer SlidersTimer;
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailWidth, &g_Config.m_TcTeeTrailWidth, &Button, Localize("Trail width"), 0, 20);
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailLength, &g_Config.m_TcTeeTrailLength, &Button, Localize("Trail length"), 0, 200);
					CurrentColumn.HSplitTop(LineSize, &Button, &CurrentColumn);
					Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailAlpha, &g_Config.m_TcTeeTrailAlpha, &Button, Localize("Trail alpha"), 0, 100);
					LogSettingsStage("tclient_settings_right_tee_trails_sliders", SlidersTimer);
				}
			}
			else
				CurrentColumn.HSplitTop(LineSize * 3.0f, nullptr, &CurrentColumn);

			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutBackgroundDrawSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Background Draw"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			static CButtonContainer s_BgDrawColor;
			if(Render)
				DoLine_ColorPicker(&s_BgDrawColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &CurrentColumn, Localize("Color"), &g_Config.m_TcBgDrawColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
			else
				CurrentColumn.HSplitTop(ColorPickerLineSize + ColorPickerLineSpacing, nullptr, &CurrentColumn);

			CurrentColumn.HSplitTop(LineSize * 2.0f, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
			{
				if(g_Config.m_TcBgDrawFadeTime == 0)
					Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, Localize("Stroke fade time"), 0, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, Localize(" seconds (never)"));
				else
					Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, Localize("Stroke fade time"), 0, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, Localize(" seconds"));
			}
			CurrentColumn.HSplitTop(LineSize * 2.0f, Render ? &Button : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawWidth, &g_Config.m_TcBgDrawWidth, &Button, Localize("Width"), 1, 50, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);
			if(Render)
			{
				static CButtonContainer s_ReaderButtonDraw, s_ClearButtonDraw;
				DoLine_KeyReader(CurrentColumn, s_ReaderButtonDraw, s_ClearButtonDraw, Localize("Draw where mouse is"), "+bg_draw");
			}
			else
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};
		auto LayoutFinishNameSection = [&](CUIRect &CurrentColumn, bool Render) {
			CUIRect BoxRect;
			CUIRect TmpRect;
			CUIRect FinishNameBox;
			CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);
			BoxRect = CurrentColumn;
			CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpRect, &CurrentColumn);
			if(Render)
				Ui()->DoLabel(&Label, Localize("Finish Name"), HeadlineFontSize, TEXTALIGN_ML);
			CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);

			if(Render)
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcChangeNameNearFinish, Localize("Attempt to change your name when near finish"), &g_Config.m_TcChangeNameNearFinish, &CurrentColumn, LineSize);
			else
				CurrentColumn.HSplitTop(LineSize, nullptr, &CurrentColumn);
			CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, Render ? &FinishNameBox : &TmpRect, &CurrentColumn);
			if(Render)
			{
				FinishNameBox.VSplitMid(&Label, &Button);
				Ui()->DoLabel(&Label, Localize("Finish Name:"), FontSize, TEXTALIGN_ML);
				static CLineInput s_FinishName(g_Config.m_TcFinishName, sizeof(g_Config.m_TcFinishName));
				Ui()->DoEditBox(&s_FinishName, &Button, EditBoxFontSize);
			}
			BoxRect.h = CurrentColumn.y - BoxRect.y;
			return BoxRect;
		};

	// ***** HUD ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutHudSection(MeasuredColumn, false);
		s_HudSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			CPerfTimer HudSectionTimer;
			DrawSectionBox(BoxRect);
			if(CompactHudSection)
			{
				char aBuf[160];
				str_format(aBuf, sizeof(aBuf), "Vote HUD %s | Mini debug %s", g_Config.m_TcMiniVoteHud ? Localize("On") : Localize("Off"),
					g_Config.m_TcMiniDebug ? Localize("On") : Localize("Off"));
				DrawCompactDeferredSection(BoxRect, Localize("HUD"), aBuf);
				s_HudSectionCachedHeight = BoxRect.h;
				Column = MeasuredColumn;
			}
			else
			{
				BoxRect = LayoutHudSection(Column, true);
				s_HudSectionCachedHeight = BoxRect.h;
			}
			LogSettingsStage("tclient_settings_right_hud_total", HudSectionTimer);
		}
		else
			Column = MeasuredColumn;
	}

	// ***** Frozen Tee Display ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutTeeStatusBarSection(MeasuredColumn, false);
		s_TeeStatusBarSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			DrawSectionBox(BoxRect);
			if(CompactTeeStatusBarSection)
			{
				char aBuf[160];
				str_format(aBuf, sizeof(aBuf), "%s | %s %d", g_Config.m_TcShowFrozenHud ? Localize("Enabled") : Localize("Disabled"),
					Localize("Rows"), g_Config.m_TcFrozenMaxRows);
				DrawCompactDeferredSection(BoxRect, Localize("Tee status bar"), aBuf);
				s_TeeStatusBarSectionCachedHeight = BoxRect.h;
				Column = MeasuredColumn;
			}
			else
			{
				BoxRect = LayoutTeeStatusBarSection(Column, true);
				s_TeeStatusBarSectionCachedHeight = BoxRect.h;
			}
		}
		else
			Column = MeasuredColumn;
	}

	// ***** Tile Outlines ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutTileOutlinesSection(MeasuredColumn, false);
		if(IsSectionVisible(BoxRect, CullContext))
		{
			CPerfTimer TileOutlinesTimer;
			DrawSectionBox(BoxRect);
			if(CompactTileOutlinesSection)
			{
				char aBuf[160];
				str_format(aBuf, sizeof(aBuf), "%s | %s %d%%", g_Config.m_TcOutline ? Localize("Enabled") : Localize("Disabled"),
					Localize("Opacity"), g_Config.m_TcOutlineAlpha);
				DrawCompactDeferredSection(BoxRect, Localize("Tile outlines"), aBuf);
				Column = MeasuredColumn;
			}
			else
				BoxRect = LayoutTileOutlinesSection(Column, true);
			LogSettingsStage("tclient_settings_tile_outlines_section", TileOutlinesTimer);
		}
		else
			Column = MeasuredColumn;
	}

	// ***** Ghost Tools ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutGhostToolsSection(MeasuredColumn, false);
		s_GhostToolsSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			DrawSectionBox(BoxRect);
			BoxRect = LayoutGhostToolsSection(Column, true);
			s_GhostToolsSectionCachedHeight = BoxRect.h;
		}
		else
			Column = MeasuredColumn;
	}

	// ***** 彩虹! ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutRainbowSection(MeasuredColumn, false);
		s_RainbowSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			DrawSectionBox(BoxRect);
			BoxRect = LayoutRainbowSection(Column, true);
			s_RainbowSectionCachedHeight = BoxRect.h;
		}
		else
			Column = MeasuredColumn;
	}

	// ***** Tee Trails ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutTeeTrailsSection(MeasuredColumn, false);
		s_TeeTrailsSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			CPerfTimer TeeTrailsTimer;
			DrawSectionBox(BoxRect);
			BoxRect = LayoutTeeTrailsSection(Column, true);
			s_TeeTrailsSectionCachedHeight = BoxRect.h;
			LogSettingsStage("tclient_settings_right_tee_trails_total", TeeTrailsTimer);
		}
		else
			Column = MeasuredColumn;
	}

	// ***** 背景绘画 ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutBackgroundDrawSection(MeasuredColumn, false);
		s_BackgroundDrawSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			DrawSectionBox(BoxRect);
			BoxRect = LayoutBackgroundDrawSection(Column, true);
			s_BackgroundDrawSectionCachedHeight = BoxRect.h;
		}
		else
			Column = MeasuredColumn;
	}



	// ***** 终点恰分改名 ***** //
	{
		CUIRect MeasuredColumn = Column;
		CUIRect BoxRect = LayoutFinishNameSection(MeasuredColumn, false);
		s_FinishNameSectionCachedHeight = BoxRect.h;
		if(IsSectionVisible(BoxRect, CullContext))
		{
			DrawSectionBox(BoxRect);
			BoxRect = LayoutFinishNameSection(Column, true);
			s_FinishNameSectionCachedHeight = BoxRect.h;
		}
		else
			Column = MeasuredColumn;
	}



	// ***** END OF PAGE 1 SETTINGS ***** //
		RightView = Column;
		LogSettingsStage("tclient_settings_right_column", RightColumnTimer);
	}

	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = maximum(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsTClientBindWheel(CUIRect MainView)
{
	CUIRect LeftView, RightView, Label, Button;
	MainView.VSplitLeft(MainView.w / 2.1f, &LeftView, &RightView);
	const int DeferredFrames = GetDeferredTClientTabFrames(TCLIENT_TAB_BINDWHEEL);

	const float Radius = minimum(RightView.w, RightView.h) / 2.0f;
	vec2 Center = RightView.Center();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f);
	Graphics()->DrawCircle(Center.x, Center.y, Radius, 64);
	Graphics()->QuadsEnd();

	static char s_aBindName[BINDWHEEL_MAX_NAME];
	static char s_aBindCommand[BINDWHEEL_MAX_CMD];

	static int s_SelectedBindIndex = -1;
	int HoveringIndex = -1;

	float MouseDist = distance(Center, Ui()->MousePos());
	const int SegmentCount = GameClient()->m_BindWheel.m_vBinds.size();
	if(DeferredFrames >= 4)
	{
		LeftView.HSplitTop(LineSize, &Label, &LeftView);
		Ui()->DoLabel(&Label, Localize("Bind Wheel"), HeadlineFontSize, TEXTALIGN_ML);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		const int PreviewCount = minimum(SegmentCount, 6);
		for(int i = 0; i < PreviewCount; ++i)
		{
			LeftView.HSplitTop(LineSize, &Label, &LeftView);
			char aBuf[128];
			const auto &Bind = GameClient()->m_BindWheel.m_vBinds[i];
			str_format(aBuf, sizeof(aBuf), "%d. %s", i + 1, Bind.m_aName[0] != '\0' ? Bind.m_aName : "*");
			Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		}
		if(SegmentCount > PreviewCount)
		{
			LeftView.HSplitTop(LineSize, &Label, &LeftView);
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), Localize("+%d more binds"), SegmentCount - PreviewCount);
			Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		}
	}
	if(MouseDist < Radius && MouseDist > Radius * 0.25f && SegmentCount > 0)
	{
		float SegmentAngle = 2.0f * pi / SegmentCount;
		float HoveringAngle = angle(Ui()->MousePos() - Center) + SegmentAngle / 2.0f;
		if(HoveringAngle < 0.0f)
			HoveringAngle += 2.0f * pi;

		HoveringIndex = (int)(HoveringAngle / (2.0f * pi) * SegmentCount);
		HoveringIndex = std::clamp(HoveringIndex, 0, SegmentCount - 1);
		if(Ui()->MouseButtonClicked(0))
		{
			s_SelectedBindIndex = HoveringIndex;
			str_copy(s_aBindName, GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aName);
			str_copy(s_aBindCommand, GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aCommand);
		}
		else if(Ui()->MouseButtonClicked(1) && s_SelectedBindIndex >= 0 && HoveringIndex >= 0 && HoveringIndex != s_SelectedBindIndex)
		{
			CBindWheel::CBind BindA = GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex];
			CBindWheel::CBind BindB = GameClient()->m_BindWheel.m_vBinds[HoveringIndex];
			str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aName, BindB.m_aName);
			str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aCommand, BindB.m_aCommand);
			str_copy(GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aName, BindA.m_aName);
			str_copy(GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aCommand, BindA.m_aCommand);
		}
		else if(Ui()->MouseButtonClicked(2))
		{
			s_SelectedBindIndex = HoveringIndex;
		}
	}
	else if(MouseDist < Radius && Ui()->MouseButtonClicked(0))
	{
		s_SelectedBindIndex = -1;
		str_copy(s_aBindName, "");
		str_copy(s_aBindCommand, "");
	}

	if(DeferredFrames <= 3)
	{
		CPerfTimer WheelTimer;
		const float Theta = pi * 2.0f / std::max<float>(1.0f, GameClient()->m_BindWheel.m_vBinds.size());
		for(int i = 0; i < static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()); i++)
		{
			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

			float SegmentFontSize = FontSize * 1.1f;
			if(i == s_SelectedBindIndex)
			{
				SegmentFontSize = FontSize * 1.7f;
				TextRender()->TextColor(ColorRGBA(0.5f, 1.0f, 0.75f, 1.0f));
			}
			else if(i == HoveringIndex)
			{
				SegmentFontSize = FontSize * 1.35f;
			}

			const CBindWheel::CBind &Bind = GameClient()->m_BindWheel.m_vBinds[i];
			const float Angle = Theta * i;
			const vec2 Pos = direction(Angle) * (Radius * 0.75f) + Center;
			const CUIRect Rect = CUIRect{Pos.x - 50.0f, Pos.y - 50.0f, 100.0f, 100.0f};
			Ui()->DoLabel(&Rect, Bind.m_aName, SegmentFontSize, TEXTALIGN_MC);
		}
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "count=%d", SegmentCount);
		LogTClientPerfStage("tclient_bindwheel_wheel", WheelTimer.ElapsedMs(), false, aExtra);
	}
	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

	if(DeferredFrames <= 2)
	{
		CPerfTimer EditorTimer;
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Button.VSplitLeft(100.0f, &Label, &Button);
		Ui()->DoLabel(&Label, Localize("Name:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_NameInput;
		s_NameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
		s_NameInput.SetEmptyText(Localize("Name"));
		Ui()->DoEditBox(&s_NameInput, &Button, EditBoxFontSize);

		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Button.VSplitLeft(100.0f, &Label, &Button);
		Ui()->DoLabel(&Label, Localize("Command:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_BindInput;
		s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
		s_BindInput.SetEmptyText(Localize("Command"));
		Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize);

		static CButtonContainer s_AddButton, s_RemoveButton, s_OverrideButton;
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		if(DoButton_Menu(&s_OverrideButton, Localize("Override Selected"), 0, &Button) && s_SelectedBindIndex >= 0 && s_SelectedBindIndex < static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()))
		{
			CBindWheel::CBind TempBind;
			if(str_length(s_aBindName) == 0)
				str_copy(TempBind.m_aName, "*");
			else
				str_copy(TempBind.m_aName, s_aBindName);

			str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aName, TempBind.m_aName);
			str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
		}
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		CUIRect ButtonAdd, ButtonRemove;
		Button.VSplitMid(&ButtonRemove, &ButtonAdd, MarginSmall);
		if(DoButton_Menu(&s_AddButton, Localize("Add Bind"), 0, &ButtonAdd))
		{
			CBindWheel::CBind TempBind;
			if(str_length(s_aBindName) == 0)
				str_copy(TempBind.m_aName, "*");
			else
				str_copy(TempBind.m_aName, s_aBindName);

			GameClient()->m_BindWheel.AddBind(TempBind.m_aName, s_aBindCommand);
			s_SelectedBindIndex = static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()) - 1;
		}
		if(DoButton_Menu(&s_RemoveButton, Localize("Remove Bind"), 0, &ButtonRemove) && s_SelectedBindIndex >= 0)
		{
			GameClient()->m_BindWheel.RemoveBind(s_SelectedBindIndex);
			s_SelectedBindIndex = -1;
		}
		LogTClientPerfStage("tclient_bindwheel_editor", EditorTimer.ElapsedMs(), false);
	}
	else
	{
		LeftView.HSplitTop(LineSize, &Label, &LeftView);
		if(s_SelectedBindIndex >= 0 && s_SelectedBindIndex < SegmentCount)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Selected"), GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aName);
			Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		}
		else
			Ui()->DoLabel(&Label, Localize("Select a bind to edit"), FontSize, TEXTALIGN_ML);
	}

	CPerfTimer FooterTextTimer;
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, Localize("Commands run in the console"), FontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, DeferredFrames <= 1 ? Localize("L select  R swap  M select only") : Localize("L select  R swap"), FontSize * 0.8f, TEXTALIGN_ML);
	LogTClientPerfStage("tclient_bindwheel_footer_text", FooterTextTimer.ElapsedMs(), false);

	{
		CPerfTimer FooterControlsTimer;
		LeftView.HSplitBottom(LineSize, &LeftView, &Label);
		static CButtonContainer s_ReaderButtonWheel, s_ClearButtonWheel;
		DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, Localize("Bind Wheel Key"), "+bindwheel");

		LeftView.HSplitBottom(LineSize, &LeftView, &Label);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcResetBindWheelMouse, Localize("Reset position of mouse when opening bindwheel"), &g_Config.m_TcResetBindWheelMouse, &Label, LineSize);
		LogTClientPerfStage("tclient_bindwheel_footer_controls", FooterControlsTimer.ElapsedMs(), false);
	}
}

void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label;
	const int DeferredFrames = GetDeferredTClientTabFrames(TCLIENT_TAB_BINDCHAT);

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxes;
	static vec2 s_PrevScrollOffset(0.0f, 0.0f);

	MainView.y += ScrollOffset.y;

	MainView.HSplitTop(Margin, nullptr, &MainView);
	MainView.VSplitRight(5.0f, &MainView, nullptr); // Padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // Padding for scrollbar

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset.y - ScrollOffset.y;
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** All the stuff ***** //

	auto DoBindchatDefault = [&](CUIRect &Column, CBindChat::CBindDefault &BindDefault) {
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		CBindChat::CBind *pOldBind = GameClient()->m_BindChat.GetBind(BindDefault.m_Bind.m_aCommand);
		static char s_aTempName[BINDCHAT_MAX_NAME] = "";
		char *pName;
		if(pOldBind == nullptr)
			pName = s_aTempName;
		else
			pName = pOldBind->m_aName;
		if(DoEditBoxWithLabel(&BindDefault.m_LineInput, &Button, Localize(BindDefault.m_pTitle), BindDefault.m_Bind.m_aName, pName, BINDCHAT_MAX_NAME) && BindDefault.m_LineInput.IsActive())
		{
			if(!pOldBind && pName[0] != '\0')
			{
				auto BindNew = BindDefault.m_Bind;
				str_copy(BindNew.m_aName, pName);
				GameClient()->m_BindChat.RemoveBind(pName); // Prevent duplicates
				GameClient()->m_BindChat.AddBind(BindNew);
				s_aTempName[0] = '\0';
			}
			if(pOldBind && pName[0] == '\0')
			{
				GameClient()->m_BindChat.RemoveBind(pName);
			}
		}
	};

	auto DoBindchatDefaults = [&](CUIRect &Column, const char *pTitle, std::vector<CBindChat::CBindDefault> &vBindchatDefaults) {
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		Ui()->DoLabel(&Label, pTitle, HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		for(CBindChat::CBindDefault &BindchatDefault : vBindchatDefaults)
			DoBindchatDefault(Column, BindchatDefault);
		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	};

	float SizeL = 0.0f, SizeR = 0.0f;
	int GroupIndex = 0;
	const int AllowedGroups = DeferredFrames >= 2 ? 2 : (int)CBindChat::BIND_DEFAULTS.size();
	for(auto &[pTitle, vBindDefaults] : CBindChat::BIND_DEFAULTS)
	{
		if(GroupIndex >= AllowedGroups)
			break;
		float &Size = SizeL > SizeR ? SizeR : SizeL;
		CUIRect &Column = SizeL > SizeR ? RightView : LeftView;
		DoBindchatDefaults(Column, Localize(pTitle), vBindDefaults);
		Size += vBindDefaults.size() * (MarginSmall + LineSize) + HeadlineHeight + HeadlineFontSize + MarginSmall * 2.0f;
		++GroupIndex;
	}

	if(DeferredFrames >= 2)
	{
		CUIRect Notice;
		Notice.x = MainView.x + MarginSmall;
		Notice.y = maximum(LeftView.y, RightView.y) + MarginSmall;
		Notice.w = MainView.w - MarginSmall * 2.0f;
		Notice.h = LineSize;
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), Localize("Showing %d of %d chat bind groups"), AllowedGroups, (int)CBindChat::BIND_DEFAULTS.size());
		Ui()->DoLabel(&Notice, aBuf, FontSize, TEXTALIGN_ML);
	}

	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = maximum(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsTClientWarList(CUIRect MainView)
{
	CUIRect RightView, LeftView, Column1, Column2, Column3, Column4, Button, ButtonL, ButtonR, Label;
	const int DeferredFrames = GetDeferredTClientTabFrames(TCLIENT_TAB_WARLIST);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.VSplitMid(&LeftView, &RightView, Margin);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	// WAR LIST will have 4 columns
	//  [War entries] - [Entry Editing] - [Group Types] - [Recent Players]
	//									 [Group Editing]

	// putting this here so it can be updated by the entry list
	static char s_aEntryName[MAX_NAME_LENGTH];
	static char s_aEntryClan[MAX_CLAN_LENGTH];
	static char s_aEntryReason[MAX_WARLIST_REASON_LENGTH];
	static bool s_IsClan = false;
	static bool s_IsName = true;

	LeftView.VSplitMid(&Column1, &Column2, Margin);
	RightView.VSplitMid(&Column3, &Column4, Margin);

	// ======WAR ENTRIES======
	static CWarEntry *s_pSelectedEntry = nullptr;
	static CWarType *s_pSelectedType = GameClient()->m_WarList.m_WarTypes[0];
	auto RenderWarTypeSummary = [&](CUIRect &Column, int MaxRows) {
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		Ui()->DoLabel(&Label, Localize("War Groups"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		int Rendered = 0;
		for(const CWarType *pType : GameClient()->m_WarList.m_WarTypes)
		{
			if(pType == nullptr || Rendered >= MaxRows)
				continue;
			Column.HSplitTop(LineSize, &Label, &Column);
			TextRender()->TextColor(pType->m_Color);
			Ui()->DoLabel(&Label, pType->m_aWarName, FontSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			++Rendered;
		}
		if(Rendered == 0)
		{
			Column.HSplitTop(LineSize, &Label, &Column);
			Ui()->DoLabel(&Label, Localize("No war groups"), FontSize, TEXTALIGN_ML);
		}
		else if((int)GameClient()->m_WarList.m_WarTypes.size() > Rendered)
		{
			Column.HSplitTop(LineSize, &Label, &Column);
			char aBuf[96];
			str_format(aBuf, sizeof(aBuf), Localize("+%d more groups"), (int)GameClient()->m_WarList.m_WarTypes.size() - Rendered);
			Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		}
	};
	auto RenderOnlinePlayerSummary = [&](CUIRect &Column, int MaxRows) {
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		Ui()->DoLabel(&Label, Localize("Online Players"), HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		int Rendered = 0;
		int Total = 0;
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[i])
				continue;
			++Total;
			if(Rendered >= MaxRows)
				continue;
			const auto &Client = GameClient()->m_aClients[i];
			Column.HSplitTop(LineSize, &Label, &Column);
			char aBuf[128];
			if(Client.m_aClan[0] != '\0')
				str_format(aBuf, sizeof(aBuf), "%s (%s)", Client.m_aName, Client.m_aClan);
			else
				str_copy(aBuf, Client.m_aName, sizeof(aBuf));
			Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
			++Rendered;
		}
		if(Rendered == 0)
		{
			Column.HSplitTop(LineSize, &Label, &Column);
			Ui()->DoLabel(&Label, Localize("No online players"), FontSize, TEXTALIGN_ML);
		}
		else if(Total > Rendered)
		{
			Column.HSplitTop(LineSize, &Label, &Column);
			char aBuf[96];
			str_format(aBuf, sizeof(aBuf), Localize("+%d more players"), Total - Rendered);
			Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		}
	};
	{
		Column1.HSplitTop(HeadlineHeight, &Label, &Column1);
		Label.VSplitRight(25.0f, &Label, &Button);
		Ui()->DoLabel(&Label, Localize("War Entries"), HeadlineFontSize, TEXTALIGN_ML);
		Column1.HSplitTop(MarginSmall, nullptr, &Column1);

		static CButtonContainer s_ReverseEntries;
		static bool s_Reversed = true;
		if(Ui()->DoButton_FontIcon(&s_ReverseEntries, s_Reversed ? FONT_ICON_CHEVRON_UP : FONT_ICON_CHEVRON_DOWN, 0, &Button, IGraphics::CORNER_ALL))
		{
			s_Reversed = !s_Reversed;
		}

		CUIRect EntriesSearch;
		Column1.HSplitBottom(25.0f, &Column1, &EntriesSearch);
		EntriesSearch.HSplitTop(MarginSmall, nullptr, &EntriesSearch);

		// Filter the list
		static CLineInputBuffered<128> s_EntriesFilterInput;
		std::vector<CWarEntry *> vpFilteredEntries;
		for(CWarEntry &Entry : GameClient()->m_WarList.m_vWarEntries)
		{
			if(str_find_nocase(Entry.m_aName, s_EntriesFilterInput.GetString()))
				vpFilteredEntries.push_back(&Entry);
			else if(str_find_nocase(Entry.m_aClan, s_EntriesFilterInput.GetString()))
				vpFilteredEntries.push_back(&Entry);
			else if(str_find_nocase(Entry.m_pWarType->m_aWarName, s_EntriesFilterInput.GetString()))
				vpFilteredEntries.push_back(&Entry);
		}
		if(s_Reversed)
			std::reverse(vpFilteredEntries.begin(), vpFilteredEntries.end());

		int SelectedOldEntry = -1;
		static CListBox s_EntriesListBox;
		s_EntriesListBox.DoStart(35.0f, vpFilteredEntries.size(), 1, 2, SelectedOldEntry, &Column1);

		static std::vector<unsigned char> s_vItemIds;
		static std::vector<CButtonContainer> s_vDeleteButtons;

		const int MaxEntries = GameClient()->m_WarList.m_vWarEntries.size();
		s_vItemIds.resize(MaxEntries);
		s_vDeleteButtons.resize(MaxEntries);

		for(size_t i = 0; i < vpFilteredEntries.size(); i++)
		{
			CWarEntry *pEntry = vpFilteredEntries[i];

			if(s_pSelectedEntry && pEntry == s_pSelectedEntry)
				SelectedOldEntry = i;

			const CListboxItem Item = s_EntriesListBox.DoNextItem(&s_vItemIds[i], SelectedOldEntry >= 0 && (size_t)SelectedOldEntry == i);
			if(!Item.m_Visible)
				continue;

			CUIRect EntryRect, DeleteButton, EntryTypeRect, WarType, ToolTip;
			Item.m_Rect.Margin(0.0f, &EntryRect);
			EntryRect.VSplitLeft(26.0f, &DeleteButton, &EntryRect);
			DeleteButton.HMargin(7.5f, &DeleteButton);
			DeleteButton.VSplitLeft(MarginSmall, nullptr, &DeleteButton);
			DeleteButton.VSplitRight(MarginExtraSmall, &DeleteButton, nullptr);

			if(Ui()->DoButton_FontIcon(&s_vDeleteButtons[i], FONT_ICON_TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
				GameClient()->m_WarList.RemoveWarEntry(pEntry);

			bool IsClan = false;
			char aBuf[32];
			if(str_comp(pEntry->m_aClan, "") != 0)
			{
				str_copy(aBuf, pEntry->m_aClan);
				IsClan = true;
			}
			else
			{
				str_copy(aBuf, pEntry->m_aName);
			}
			EntryRect.VSplitLeft(35.0f, &EntryTypeRect, &EntryRect);

			if(IsClan)
			{
				RenderFontIcon(EntryTypeRect, FONT_ICON_USERS, 18.0f, TEXTALIGN_MC);
			}
			else
			{
				RenderDevSkin(EntryTypeRect.Center(), 35.0f, "default", "default", false, 0, 0, 0, false, false);
			}

			if(str_comp(pEntry->m_aReason, "") != 0)
			{
				EntryRect.VSplitRight(20.0f, &EntryRect, &ToolTip);
				RenderFontIcon(ToolTip, FONT_ICON_COMMENT, 18.0f, TEXTALIGN_MC);
				GameClient()->m_Tooltips.DoToolTip(&s_vItemIds[i], &ToolTip, pEntry->m_aReason);
				GameClient()->m_Tooltips.SetFadeTime(&s_vItemIds[i], 0.0f);
			}

			EntryRect.HMargin(MarginExtraSmall, &EntryRect);
			EntryRect.HSplitMid(&EntryRect, &WarType, MarginSmall);

			Ui()->DoLabel(&EntryRect, aBuf, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(pEntry->m_pWarType->m_Color);
			Ui()->DoLabel(&WarType, pEntry->m_pWarType->m_aWarName, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		const int NewSelectedEntry = s_EntriesListBox.DoEnd();
		if(SelectedOldEntry != NewSelectedEntry || (SelectedOldEntry >= 0 && Ui()->HotItem() == &s_vItemIds[NewSelectedEntry] && Ui()->MouseButtonClicked(0)))
		{
			s_pSelectedEntry = vpFilteredEntries[NewSelectedEntry];
			if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
			{
				str_copy(s_aEntryName, s_pSelectedEntry->m_aName);
				str_copy(s_aEntryClan, s_pSelectedEntry->m_aClan);
				str_copy(s_aEntryReason, s_pSelectedEntry->m_aReason);
				if(str_comp(s_pSelectedEntry->m_aClan, "") != 0)
				{
					s_IsName = false;
					s_IsClan = true;
				}
				else
				{
					s_IsName = true;
					s_IsClan = false;
				}
				s_pSelectedType = s_pSelectedEntry->m_pWarType;
			}
		}

		Ui()->DoEditBox_Search(&s_EntriesFilterInput, &EntriesSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
	}

	if(DeferredFrames >= 4)
	{
		Column2.HSplitTop(HeadlineHeight, &Label, &Column2);
		Ui()->DoLabel(&Label, Localize("Edit Entry"), HeadlineFontSize, TEXTALIGN_ML);
		Column2.HSplitTop(MarginSmall, nullptr, &Column2);
		Column2.HSplitTop(LineSize, &Label, &Column2);
		if(s_pSelectedEntry != nullptr)
		{
			char aBuf[128];
			const char *pSelectedName = s_pSelectedEntry->m_aClan[0] != '\0' ? s_pSelectedEntry->m_aClan : s_pSelectedEntry->m_aName;
			str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Selected"), pSelectedName);
			Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		}
		else
			Ui()->DoLabel(&Label, Localize("No entry selected"), FontSize, TEXTALIGN_ML);
		Column2.HSplitTop(MarginSmall, nullptr, &Column2);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListAllowDuplicates, Localize("Allow Duplicate Entries"), &g_Config.m_TcWarListAllowDuplicates, &Column2, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarList, Localize("Enable warlist"), &g_Config.m_TcWarList, &Column2, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListChat, Localize("Colors in chat"), &g_Config.m_TcWarListChat, &Column2, LineSize);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListScoreboard, Localize("Colors in scoreboard"), &g_Config.m_TcWarListScoreboard, &Column2, LineSize);
		RenderWarTypeSummary(Column3, 6);
		RenderOnlinePlayerSummary(Column4, 6);
		return;
	}

	// ======WAR ENTRY EDITING======
	Column2.HSplitTop(HeadlineHeight, &Label, &Column2);
	Label.VSplitRight(25.0f, &Label, &Button);
	Ui()->DoLabel(&Label, Localize("Edit Entry"), HeadlineFontSize, TEXTALIGN_ML);
	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(HeadlineFontSize, &Button, &Column2);

	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	static CLineInput s_NameInput;
	s_NameInput.SetBuffer(s_aEntryName, sizeof(s_aEntryName));
	s_NameInput.SetEmptyText(Localize("Name"));
	if(s_IsName)
	{
		Ui()->DoEditBox(&s_NameInput, &ButtonL, 12.0f);
	}
	else
	{
		ButtonL.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), 15, 3.0f);
		Ui()->ClipEnable(&ButtonL);
		ButtonL.VMargin(2.0f, &ButtonL);
		s_NameInput.Render(&ButtonL, 12.0f, TEXTALIGN_ML, false, -1.0f, 0.0f);
		Ui()->ClipDisable();
	}

	static CLineInput s_ClanInput;
	s_ClanInput.SetBuffer(s_aEntryClan, sizeof(s_aEntryClan));
	s_ClanInput.SetEmptyText(Localize("Clan"));
	if(s_IsClan)
		Ui()->DoEditBox(&s_ClanInput, &ButtonR, 12.0f);
	else
	{
		ButtonR.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), 15, 3.0f);
		Ui()->ClipEnable(&ButtonR);
		ButtonR.VMargin(2.0f, &ButtonR);
		s_ClanInput.Render(&ButtonR, 12.0f, TEXTALIGN_ML, false, -1.0f, 0.0f);
		Ui()->ClipDisable();
	}

	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(LineSize, &Button, &Column2);
	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	static unsigned char s_NameRadio, s_ClanRadio;
	if(DoButton_CheckBox_Common(&s_NameRadio, Localize("Name"), s_IsName ? "X" : "", &ButtonL, BUTTONFLAG_LEFT))
	{
		s_IsName = true;
		s_IsClan = false;
	}
	if(DoButton_CheckBox_Common(&s_ClanRadio, Localize("Clan"), s_IsClan ? "X" : "", &ButtonR, BUTTONFLAG_LEFT))
	{
		s_IsName = false;
		s_IsClan = true;
	}
	if(!s_IsName)
		str_copy(s_aEntryName, "");
	if(!s_IsClan)
		str_copy(s_aEntryClan, "");

	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(HeadlineFontSize, &Button, &Column2);
	static CLineInput s_ReasonInput;
	s_ReasonInput.SetBuffer(s_aEntryReason, sizeof(s_aEntryReason));
	s_ReasonInput.SetEmptyText(Localize("Reason"));
	Ui()->DoEditBox(&s_ReasonInput, &Button, 12.0f);

	static CButtonContainer s_AddButton, s_OverrideButton;

	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(LineSize * 2.0f, &Button, &Column2);
	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);

	if(DoButtonLineSize_Menu(&s_OverrideButton, Localize("Override Entry"), 0, &ButtonL, LineSize) && s_pSelectedEntry)
	{
		if(s_pSelectedEntry && s_pSelectedType && (str_comp(s_aEntryName, "") != 0 || str_comp(s_aEntryClan, "") != 0))
		{
			str_copy(s_pSelectedEntry->m_aName, s_aEntryName);
			str_copy(s_pSelectedEntry->m_aClan, s_aEntryClan);
			str_copy(s_pSelectedEntry->m_aReason, s_aEntryReason);
			s_pSelectedEntry->m_pWarType = s_pSelectedType;
		}
	}
	if(DoButtonLineSize_Menu(&s_AddButton, Localize("Add Entry"), 0, &ButtonR, LineSize))
	{
		if(s_pSelectedType)
			GameClient()->m_WarList.AddWarEntry(s_aEntryName, s_aEntryClan, s_aEntryReason, s_pSelectedType->m_aWarName);
	}
	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(HeadlineFontSize + MarginSmall, &Button, &Column2);
	if(s_pSelectedType)
	{
		float Shade = 0.0f;
		Button.Draw(ColorRGBA(Shade, Shade, Shade, 0.25f), 15, 3.0f);
		TextRender()->TextColor(s_pSelectedType->m_Color);
		Ui()->DoLabel(&Button, s_pSelectedType->m_aWarName, HeadlineFontSize, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	Column2.HSplitBottom(150.0f, nullptr, &Column2);

	Column2.HSplitTop(HeadlineHeight, &Label, &Column2);
	Ui()->DoLabel(&Label, Localize("Settings"), HeadlineFontSize, TEXTALIGN_ML);
	Column2.HSplitTop(MarginSmall, nullptr, &Column2);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListAllowDuplicates, Localize("Allow Duplicate Entries"), &g_Config.m_TcWarListAllowDuplicates, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarList, Localize("Enable warlist"), &g_Config.m_TcWarList, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListChat, Localize("Colors in chat"), &g_Config.m_TcWarListChat, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListScoreboard, Localize("Colors in scoreboard"), &g_Config.m_TcWarListScoreboard, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListSpectate, Localize("Colors in spectate select"), &g_Config.m_TcWarListSpectate, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListShowClan, Localize("Show clan if war"), &g_Config.m_TcWarListShowClan, &Column2, LineSize);

	if(DeferredFrames >= 2)
	{
		RenderWarTypeSummary(Column3, 8);
		RenderOnlinePlayerSummary(Column4, 8);
		return;
	}

	// ======WAR TYPE EDITING======

	Column3.HSplitTop(HeadlineHeight, &Label, &Column3);
	Ui()->DoLabel(&Label, Localize("War Groups"), HeadlineFontSize, TEXTALIGN_ML);
	Column3.HSplitTop(MarginSmall, nullptr, &Column3);

	static char s_aTypeName[MAX_WARLIST_TYPE_LENGTH];
	static ColorRGBA s_GroupColor = ColorRGBA(1, 1, 1, 1);

	CUIRect WarTypeList;
	Column3.HSplitBottom(180.0f, &WarTypeList, &Column3);
	m_pRemoveWarType = nullptr;
	int SelectedOldType = -1;
	static CListBox s_WarTypeListBox;
	s_WarTypeListBox.DoStart(25.0f, GameClient()->m_WarList.m_WarTypes.size(), 1, 2, SelectedOldType, &WarTypeList, true, IGraphics::CORNER_ALL, true);

	static std::vector<unsigned char> s_vTypeItemIds;
	static std::vector<CButtonContainer> s_vTypeDeleteButtons;

	const int MaxTypes = GameClient()->m_WarList.m_WarTypes.size();
	s_vTypeItemIds.resize(MaxTypes);
	s_vTypeDeleteButtons.resize(MaxTypes);

	for(int i = 0; i < (int)GameClient()->m_WarList.m_WarTypes.size(); i++)
	{
		CWarType *pType = GameClient()->m_WarList.m_WarTypes[i];

		if(!pType)
			continue;

		if(s_pSelectedType && pType == s_pSelectedType)
			SelectedOldType = i;

		const CListboxItem Item = s_WarTypeListBox.DoNextItem(&s_vTypeItemIds[i], SelectedOldType >= 0 && SelectedOldType == i);
		if(!Item.m_Visible)
			continue;

		CUIRect TypeRect, DeleteButton;
		Item.m_Rect.Margin(0.0f, &TypeRect);

		if(pType->m_Removable)
		{
			TypeRect.VSplitRight(20.0f, &TypeRect, &DeleteButton);
			DeleteButton.HSplitTop(20.0f, &DeleteButton, nullptr);
			DeleteButton.Margin(2.0f, &DeleteButton);
			if(DoButtonNoRect_FontIcon(&s_vTypeDeleteButtons[i], FONT_ICON_TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
				m_pRemoveWarType = pType;
		}
		TextRender()->TextColor(pType->m_Color);
		Ui()->DoLabel(&TypeRect, pType->m_aWarName, StandardFontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	const int NewSelectedType = s_WarTypeListBox.DoEnd();
	if((SelectedOldType != NewSelectedType && NewSelectedType >= 0) || (NewSelectedType >= 0 && Ui()->HotItem() == &s_vTypeItemIds[NewSelectedType] && Ui()->MouseButtonClicked(0)))
	{
		s_pSelectedType = GameClient()->m_WarList.m_WarTypes[NewSelectedType];
		if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
		{
			str_copy(s_aTypeName, s_pSelectedType->m_aWarName);
			s_GroupColor = s_pSelectedType->m_Color;
		}
	}
	if(m_pRemoveWarType != nullptr)
	{
		char aMessage[256];
		str_format(aMessage, sizeof(aMessage),
			Localize("Are you sure that you want to remove '%s' from your war groups?"),
			m_pRemoveWarType->m_aWarName);
		PopupConfirm(Localize("Remove War Group"), aMessage, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmRemoveWarType);
	}

	static CLineInput s_TypeNameInput;
	Column3.HSplitTop(MarginSmall, nullptr, &Column3);
	Column3.HSplitTop(HeadlineFontSize + MarginSmall, &Button, &Column3);
	s_TypeNameInput.SetBuffer(s_aTypeName, sizeof(s_aTypeName));
	s_TypeNameInput.SetEmptyText(Localize("Group name"));
	Ui()->DoEditBox(&s_TypeNameInput, &Button, 12.0f);
	static CButtonContainer s_AddGroupButton, s_OverrideGroupButton, s_GroupColorPicker;

	Column3.HSplitTop(MarginSmall, nullptr, &Column3);
	static unsigned int s_ColorValue = 0;
	s_ColorValue = color_cast<ColorHSLA>(s_GroupColor).Pack(false);
	ColorHSLA PickedColor = DoLine_ColorPicker(&s_GroupColorPicker, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column3, Localize("Color"), &s_ColorValue, ColorRGBA(1.0f, 1.0f, 1.0f), true);
	s_GroupColor = color_cast<ColorRGBA>(PickedColor);

	Column3.HSplitTop(LineSize * 2.0f, &Button, &Column3);
	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	bool OverrideDisabled = NewSelectedType == 0;
	if(DoButtonLineSize_Menu(&s_OverrideGroupButton, Localize("Override Group"), 0, &ButtonL, LineSize, OverrideDisabled) && s_pSelectedType)
	{
		if(s_pSelectedType && str_comp(s_aTypeName, "") != 0)
		{
			str_copy(s_pSelectedType->m_aWarName, s_aTypeName);
			s_pSelectedType->m_Color = s_GroupColor;
		}
	}
	bool AddDisabled = str_comp(GameClient()->m_WarList.FindWarType(s_aTypeName)->m_aWarName, "none") != 0 || str_comp(s_aTypeName, "none") == 0;
	if(DoButtonLineSize_Menu(&s_AddGroupButton, Localize("Add Group"), 0, &ButtonR, LineSize, AddDisabled))
	{
		GameClient()->m_WarList.AddWarType(s_aTypeName, s_GroupColor);
	}

	// ======ONLINE PLAYER LIST======

	Column4.HSplitTop(HeadlineHeight, &Label, &Column4);
	Ui()->DoLabel(&Label, Localize("Online Players"), HeadlineFontSize, TEXTALIGN_ML);
	Column4.HSplitTop(MarginSmall, nullptr, &Column4);

	CUIRect PlayerSearch;
	Column4.HSplitBottom(25.0f, &Column4, &PlayerSearch);
	PlayerSearch.HSplitTop(MarginSmall, nullptr, &PlayerSearch);
	static CLineInputBuffered<128> s_PlayerSearchInput;
	Ui()->DoEditBox_Search(&s_PlayerSearchInput, &PlayerSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());

	CUIRect PlayerList;
	Column4.HSplitBottom(0.0f, &PlayerList, &Column4);
	static CListBox s_PlayerListBox;
	s_PlayerListBox.DoStart(30.0f, MAX_CLIENTS, 1, 2, -1, &PlayerList, true, IGraphics::CORNER_ALL, true);

	static std::vector<unsigned char> s_vPlayerItemIds;
	static std::vector<CButtonContainer> s_vNameButtons;
	static std::vector<CButtonContainer> s_vClanButtons;

	s_vPlayerItemIds.resize(MAX_CLIENTS);
	s_vNameButtons.resize(MAX_CLIENTS);
	s_vClanButtons.resize(MAX_CLIENTS);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;

		const auto &Client = GameClient()->m_aClients[i];

		if(!str_find_nocase(Client.m_aName, s_PlayerSearchInput.GetString()) &&
			!str_find_nocase(Client.m_aClan, s_PlayerSearchInput.GetString()))
			continue;

		const CListboxItem Item = s_PlayerListBox.DoNextItem(&s_vPlayerItemIds[i], false);
		if(!Item.m_Visible)
			continue;

		CUIRect PlayerRect, TeeRect, NameRect, ClanRect;
		Item.m_Rect.Margin(0.0f, &PlayerRect);
		PlayerRect.VSplitLeft(25.0f, &TeeRect, &PlayerRect);

		PlayerRect.VSplitMid(&NameRect, &ClanRect);
		PlayerRect = NameRect;
		PlayerRect.x = TeeRect.x;
		PlayerRect.w += TeeRect.w;
		TextRender()->TextColor(GameClient()->m_WarList.GetWarData(i).m_NameColor);
		ColorRGBA NameButtonColor = Ui()->CheckActiveItem(&s_vNameButtons[i]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
											(Ui()->HotItem() == &s_vNameButtons[i] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
		PlayerRect.Draw(NameButtonColor, IGraphics::CORNER_L, 5.0f);
		Ui()->DoLabel(&NameRect, Client.m_aName, StandardFontSize, TEXTALIGN_ML);
		if(Ui()->DoButtonLogic(&s_vNameButtons[i], false, &PlayerRect, BUTTONFLAG_LEFT))
		{
			s_IsName = true;
			s_IsClan = false;
			str_copy(s_aEntryName, Client.m_aName);
		}

		TextRender()->TextColor(GameClient()->m_WarList.GetWarData(i).m_ClanColor);
		ColorRGBA ClanButtonColor = Ui()->CheckActiveItem(&s_vClanButtons[i]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
											(Ui()->HotItem() == &s_vClanButtons[i] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
		ClanRect.Draw(ClanButtonColor, IGraphics::CORNER_R, 5.0f);
		Ui()->DoLabel(&ClanRect, Client.m_aClan, StandardFontSize, TEXTALIGN_ML);
		if(Ui()->DoButtonLogic(&s_vClanButtons[i], false, &ClanRect, BUTTONFLAG_LEFT))
		{
			s_IsName = false;
			s_IsClan = true;
			str_copy(s_aEntryClan, Client.m_aClan);
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		CTeeRenderInfo TeeInfo = Client.m_RenderInfo;
		TeeInfo.m_Size = 25.0f;
		RenderTeeCute(CAnimState::GetIdle(), &TeeInfo, 0, vec2(1.0f, 0.0f), TeeRect.Center() + vec2(-1.0f, 2.5f), true);
	}
	s_PlayerListBox.DoEnd();
}

void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label, StatusBar;
	const int DeferredFrames = GetDeferredTClientTabFrames(TCLIENT_TAB_STATUSBAR);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitBottom(100.0f, &MainView, &StatusBar);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	auto GetStatusBarEditorLabel = [](const CStatusItem *pItem) {
		return str_comp(pItem->m_aName, "Space") == 0 ? pItem->m_aName : pItem->m_aDisplayName;
	};

	auto RenderStatusBarPreview = [&](CUIRect PreviewRect, int MaxItems = -1) {
		PreviewRect.Draw(ColorRGBA(0, 0, 0, 0.5f), IGraphics::CORNER_ALL, 5.0f);
		PreviewRect.VSplitLeft(MarginExtraSmall, nullptr, &PreviewRect);
		const int TotalCount = (int)GameClient()->m_StatusBar.m_StatusBarItems.size();
		const int PreviewCount = MaxItems > 0 ? minimum(TotalCount, MaxItems) : TotalCount;
		if(TotalCount <= 0 || PreviewCount <= 0)
		{
			PreviewRect.Margin(10.0f, &PreviewRect);
			Ui()->DoLabel(&PreviewRect, Localize("No status bar items"), FontSize, TEXTALIGN_ML);
			return;
		}

		float AvailableWidth = PreviewRect.w - MarginSmall;
		float ItemWidth = AvailableWidth / (float)PreviewCount;
		CUIRect PreviewItem;
		for(int i = 0; i < PreviewCount; ++i)
		{
			PreviewRect.VSplitLeft(ItemWidth, &PreviewItem, &PreviewRect);
			PreviewItem.HMargin(MarginSmall, &PreviewItem);
			PreviewItem.VMargin(MarginExtraSmall, &PreviewItem);
			PreviewItem.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f), IGraphics::CORNER_ALL, 5.0f);
			Ui()->DoLabel(&PreviewItem, Localize(GetStatusBarEditorLabel(GameClient()->m_StatusBar.m_StatusBarItems[i])), FontSize, TEXTALIGN_MC);
		}
		if(PreviewCount < TotalCount)
		{
			PreviewRect.VSplitLeft(ItemWidth, &PreviewItem, &PreviewRect);
			PreviewItem.HMargin(MarginSmall, &PreviewItem);
			PreviewItem.VMargin(MarginExtraSmall, &PreviewItem);
			PreviewItem.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f), IGraphics::CORNER_ALL, 5.0f);
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "+%d", TotalCount - PreviewCount);
			Ui()->DoLabel(&PreviewItem, aBuf, FontSize, TEXTALIGN_MC);
		}
	};
	auto RenderStatusBarCodes = [&](CUIRect View, int Limit) {
		const char *apCodes[] = {
			Localize("a = Angle"),
			Localize("p = Ping"),
			Localize("d = Prediction"),
			Localize("c = Position"),
			Localize("l = Local Time"),
			Localize("r = Race Time"),
			Localize("f = FPS"),
			Localize("v = Velocity"),
			Localize("z = Zoom"),
			Localize("_ or ' ' = Space"),
		};
		View.HSplitTop(HeadlineHeight, &Label, &View);
		Ui()->DoLabel(&Label, Localize("Status Bar Codes:"), HeadlineFontSize, TEXTALIGN_ML);
		View.HSplitTop(MarginSmall, nullptr, &View);
		const int RenderCount = Limit > 0 ? minimum(Limit, (int)std::size(apCodes)) : (int)std::size(apCodes);
		for(int i = 0; i < RenderCount; ++i)
		{
			View.HSplitTop(LineSize, &Label, &View);
			Ui()->DoLabel(&Label, apCodes[i], FontSize, TEXTALIGN_ML);
		}
		if(RenderCount < (int)std::size(apCodes))
		{
			char aBuf[64];
			View.HSplitTop(LineSize, &Label, &View);
			str_format(aBuf, sizeof(aBuf), Localize("+%d more codes"), (int)std::size(apCodes) - RenderCount);
			Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		}
	};
	auto RenderStatusBarSummaryControls = [&](CUIRect View) {
		char aBuf[128];
		View.HSplitTop(HeadlineHeight, &Label, &View);
		Ui()->DoLabel(&Label, Localize("Status Bar"), HeadlineFontSize, TEXTALIGN_ML);
		View.HSplitTop(MarginSmall, nullptr, &View);
		View.HSplitTop(LineSize, &Label, &View);
		str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Show status bar"), g_Config.m_TcStatusBar ? Localize("On") : Localize("Off"));
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		View.HSplitTop(LineSize, &Label, &View);
		str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Show labels on status bar items"), g_Config.m_TcStatusBarLabels ? Localize("On") : Localize("Off"));
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		View.HSplitTop(LineSize, &Label, &View);
		str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Status bar height"), g_Config.m_TcStatusBarHeight);
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);

		View.HSplitTop(HeadlineHeight, &Label, &View);
		Ui()->DoLabel(&Label, Localize("Local Time"), HeadlineFontSize, TEXTALIGN_ML);
		View.HSplitTop(MarginSmall, nullptr, &View);
		View.HSplitTop(LineSize, &Label, &View);
		str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Use 12 hour clock"), g_Config.m_TcStatusBar12HourClock ? Localize("On") : Localize("Off"));
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		View.HSplitTop(LineSize, &Label, &View);
		str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Show seconds on clock"), g_Config.m_TcStatusBarLocalTimeSeconds ? Localize("On") : Localize("Off"));
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);

		View.HSplitTop(HeadlineHeight, &Label, &View);
		Ui()->DoLabel(&Label, Localize("Colors"), HeadlineFontSize, TEXTALIGN_ML);
		View.HSplitTop(MarginSmall, nullptr, &View);
		View.HSplitTop(LineSize, &Label, &View);
		str_format(aBuf, sizeof(aBuf), "%s: 0x%08x", Localize("Status bar color"), g_Config.m_TcStatusBarColor);
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		View.HSplitTop(LineSize, &Label, &View);
		str_format(aBuf, sizeof(aBuf), "%s: 0x%08x", Localize("Text color"), g_Config.m_TcStatusBarTextColor);
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		View.HSplitTop(LineSize, &Label, &View);
		str_format(aBuf, sizeof(aBuf), "%s: %d%%", Localize("Status bar alpha"), g_Config.m_TcStatusBarAlpha);
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		View.HSplitTop(LineSize, &Label, &View);
		str_format(aBuf, sizeof(aBuf), "%s: %d%%", Localize("Text alpha"), g_Config.m_TcStatusBarTextAlpha);
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
	};
	auto RenderStatusBarCompactSummary = [&](CUIRect LeftSummary, CUIRect RightSummary, CUIRect PreviewSummary) {
		char aBuf[128];
		LeftSummary.HSplitTop(HeadlineHeight, &Label, &LeftSummary);
		Ui()->DoLabel(&Label, Localize("Status Bar"), HeadlineFontSize, TEXTALIGN_ML);
		LeftSummary.HSplitTop(MarginSmall, nullptr, &LeftSummary);
		LeftSummary.HSplitTop(LineSize, &Label, &LeftSummary);
		str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Show status bar"), g_Config.m_TcStatusBar ? Localize("On") : Localize("Off"));
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		LeftSummary.HSplitTop(LineSize, &Label, &LeftSummary);
		str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Items"), (int)GameClient()->m_StatusBar.m_StatusBarItems.size());
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		LeftSummary.HSplitTop(LineSize, &Label, &LeftSummary);
		str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Scheme"), g_Config.m_TcStatusBarScheme[0] != '\0' ? g_Config.m_TcStatusBarScheme : Localize("default"));
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);

		RenderStatusBarCodes(RightSummary, 2);

		PreviewSummary.Margin(10.0f, &PreviewSummary);
		PreviewSummary.HSplitTop(LineSize, &Label, &PreviewSummary);
		str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Preview items"), minimum((int)GameClient()->m_StatusBar.m_StatusBarItems.size(), 3));
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		PreviewSummary.HSplitTop(MarginSmall, nullptr, &PreviewSummary);
		RenderStatusBarPreview(PreviewSummary, 3);
	};

	if(DeferredFrames >= 5)
	{
		RenderStatusBarCompactSummary(LeftView, RightView, StatusBar);
		return;
	}

	if(DeferredFrames >= 4)
	{
		RenderStatusBarSummaryControls(LeftView);
		RenderStatusBarCodes(RightView, 4);
		StatusBar.Margin(10.0f, &StatusBar);
		StatusBar.HSplitTop(LineSize, &Label, &StatusBar);
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s: %s (%d)", Localize("Scheme"), g_Config.m_TcStatusBarScheme[0] != '\0' ? g_Config.m_TcStatusBarScheme : Localize("default"), (int)GameClient()->m_StatusBar.m_StatusBarItems.size());
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		StatusBar.HSplitTop(MarginSmall, nullptr, &StatusBar);
		RenderStatusBarPreview(StatusBar, 4);
		return;
	}

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, Localize("Status Bar"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBar, Localize("Show status bar"), &g_Config.m_TcStatusBar, &LeftView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBarLabels, Localize("Show labels on status bar items"), &g_Config.m_TcStatusBarLabels, &LeftView, LineSize);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarHeight, &g_Config.m_TcStatusBarHeight, &Button, Localize("Status bar height"), 1, 16);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, Localize("Local Time"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBar12HourClock, Localize("Use 12 hour clock"), &g_Config.m_TcStatusBar12HourClock, &LeftView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBarLocalTimeSeconds, Localize("Show seconds on clock"), &g_Config.m_TcStatusBarLocalTimeSeconds, &LeftView, LineSize);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	{
		LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
		Ui()->DoLabel(&Label, Localize("Colors"), HeadlineFontSize, TEXTALIGN_ML);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		static CButtonContainer s_StatusbarColor, s_StatusbarTextColor;

		DoLine_ColorPicker(&s_StatusbarColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Status bar color"), &g_Config.m_TcStatusBarColor, ColorRGBA(0.0f, 0.0f, 0.0f), false);
		DoLine_ColorPicker(&s_StatusbarTextColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, Localize("Text color"), &g_Config.m_TcStatusBarTextColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarAlpha, &g_Config.m_TcStatusBarAlpha, &Button, Localize("Status bar alpha"), 0, 100);
		LeftView.HSplitTop(LineSize, &Button, &LeftView);
		Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarTextAlpha, &g_Config.m_TcStatusBarTextAlpha, &Button, Localize("Text alpha"), 0, 100);
	}

	RenderStatusBarCodes(RightView, 0);

	if(DeferredFrames >= 3)
	{
		StatusBar.Margin(10.0f, &StatusBar);
		StatusBar.HSplitTop(LineSize, &Label, &StatusBar);
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s: %s (%d)", Localize("Scheme"), g_Config.m_TcStatusBarScheme[0] != '\0' ? g_Config.m_TcStatusBarScheme : Localize("default"), (int)GameClient()->m_StatusBar.m_StatusBarItems.size());
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		StatusBar.HSplitTop(MarginSmall, nullptr, &StatusBar);
		RenderStatusBarPreview(StatusBar, 6);
		return;
	}

	static int s_SelectedItem = -1;
	static int s_TypeSelectedOld = -1;

	CUIRect StatusScheme, StatusButtons, ItemLabel;
	static CButtonContainer s_ApplyButton, s_AddButton, s_RemoveButton;
	StatusBar.HSplitBottom(LineSize + MarginSmall, &StatusBar, &StatusScheme);
	StatusBar.HSplitTop(LineSize + MarginSmall, &ItemLabel, &StatusBar);
	StatusScheme.HSplitTop(MarginSmall, nullptr, &StatusScheme);

	if(s_TypeSelectedOld >= 0)
		Ui()->DoLabel(&ItemLabel, Localize(GameClient()->m_StatusBar.m_StatusItemTypes[s_TypeSelectedOld].m_aDesc), FontSize, TEXTALIGN_ML);

	StatusScheme.VSplitMid(&StatusButtons, &StatusScheme, MarginSmall);
	StatusScheme.VSplitMid(&Label, &StatusScheme, MarginSmall);
	StatusScheme.VSplitMid(&StatusScheme, &Button, MarginSmall);
	if(DoButton_Menu(&s_ApplyButton, Localize("Apply"), 0, &Button))
	{
		GameClient()->m_StatusBar.ApplyStatusBarScheme(g_Config.m_TcStatusBarScheme);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = -1;
	}
	Ui()->DoLabel(&Label, Localize("Status Scheme:"), FontSize, TEXTALIGN_MR);
	static CLineInput s_StatusScheme(g_Config.m_TcStatusBarScheme, sizeof(g_Config.m_TcStatusBarScheme));
	s_StatusScheme.SetEmptyText("");
	Ui()->DoEditBox(&s_StatusScheme, &StatusScheme, EditBoxFontSize);

	static std::vector<std::string> s_DropDownNameStorage;
	static std::vector<const char *> s_DropDownNames;
	if(s_DropDownNameStorage.size() != GameClient()->m_StatusBar.m_StatusItemTypes.size())
	{
		s_DropDownNameStorage.clear();
		s_DropDownNames.clear();
		s_DropDownNameStorage.reserve(GameClient()->m_StatusBar.m_StatusItemTypes.size());
		s_DropDownNames.reserve(GameClient()->m_StatusBar.m_StatusItemTypes.size());
		for(const CStatusItem &StatusItemType : GameClient()->m_StatusBar.m_StatusItemTypes)
		{
			s_DropDownNameStorage.emplace_back(Localize(StatusItemType.m_aName));
			s_DropDownNames.push_back(s_DropDownNameStorage.back().c_str());
		}
	}

	static CUi::SDropDownState s_DropDownState;
	static CScrollRegion s_DropDownScrollRegion;
	s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
	CUIRect DropDownRect;

	StatusButtons.VSplitMid(&DropDownRect, &StatusButtons, MarginSmall);
	const int TypeSelectedNew = Ui()->DoDropDown(&DropDownRect, s_TypeSelectedOld, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState);
	if(s_TypeSelectedOld != TypeSelectedNew)
	{
		s_TypeSelectedOld = TypeSelectedNew;
		if(s_SelectedItem >= 0)
		{
			GameClient()->m_StatusBar.m_StatusBarItems[s_SelectedItem] = &GameClient()->m_StatusBar.m_StatusItemTypes[s_TypeSelectedOld];
			GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		}
	}
	CUIRect ButtonL, ButtonR;
	StatusButtons.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	size_t NumItems = GameClient()->m_StatusBar.m_StatusBarItems.size();
	if(DoButton_Menu(&s_AddButton, Localize("Add Item"), 0, &ButtonL) && s_TypeSelectedOld >= 0 && NumItems < 128)
	{
		GameClient()->m_StatusBar.m_StatusBarItems.push_back(&GameClient()->m_StatusBar.m_StatusItemTypes[s_TypeSelectedOld]);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = (int)GameClient()->m_StatusBar.m_StatusBarItems.size() - 1;
	}
	if(DoButton_Menu(&s_RemoveButton, Localize("Remove Item"), 0, &ButtonR) && s_SelectedItem >= 0)
	{
		GameClient()->m_StatusBar.m_StatusBarItems.erase(GameClient()->m_StatusBar.m_StatusBarItems.begin() + s_SelectedItem);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = -1;
	}

	if(DeferredFrames >= 2)
	{
		RenderStatusBarPreview(StatusBar, 8);
		return;
	}

	// color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcStatusBarColor)).WithAlpha(0.5f)
	StatusBar.Draw(ColorRGBA(0, 0, 0, 0.5f), IGraphics::CORNER_ALL, 5.0f);
	int ItemCount = GameClient()->m_StatusBar.m_StatusBarItems.size();
	if(ItemCount <= 0)
	{
		RenderStatusBarPreview(StatusBar);
		return;
	}
	float AvailableWidth = StatusBar.w;
	// AvailableWidth -= (ItemCount - 1) * MarginSmall;
	AvailableWidth -= MarginSmall;
	StatusBar.VSplitLeft(MarginExtraSmall, nullptr, &StatusBar);
	float ItemWidth = AvailableWidth / (float)ItemCount;
	CUIRect StatusItemButton;
	static std::vector<CButtonContainer *> s_pItemButtons;
	static std::vector<CButtonContainer> s_ItemButtons;
	static vec2 s_ActivePos = vec2(0.0f, 0.0f);
	class CSwapItem
	{
	public:
		vec2 m_InitialPosition = vec2(0.0f, 0.0f);
		float m_Duration = 0.0f;
	};

	static std::vector<CSwapItem> s_ItemSwaps;

	if((int)s_ItemButtons.size() != ItemCount)
	{
		s_ItemSwaps.resize(ItemCount);
		s_pItemButtons.resize(ItemCount);
		s_ItemButtons.resize(ItemCount);
		for(int i = 0; i < ItemCount; ++i)
		{
			s_pItemButtons[i] = &s_ItemButtons[i];
		}
	}
	bool StatusItemActive = false;
	int HotStatusIndex = 0;
	for(int i = 0; i < ItemCount; ++i)
	{
		if(Ui()->ActiveItem() == s_pItemButtons[i])
		{
			StatusItemActive = true;
			HotStatusIndex = i;
		}
	}

	for(int i = 0; i < ItemCount; ++i)
	{
		// if(i > 0)
		//	StatusBar.VSplitLeft(MarginSmall, nullptr, &StatusBar);
		StatusBar.VSplitLeft(ItemWidth, &StatusItemButton, &StatusBar);
		StatusItemButton.HMargin(MarginSmall, &StatusItemButton);
		StatusItemButton.VMargin(MarginExtraSmall, &StatusItemButton);
		CStatusItem *StatusItem = GameClient()->m_StatusBar.m_StatusBarItems[i];
		ColorRGBA Col = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);
		if(s_SelectedItem == i)
			Col = ColorRGBA(1.0f, 0.35f, 0.35f, 0.75f);
		CUIRect TempItemButton = StatusItemButton;
		TempItemButton.y = 0, TempItemButton.h = 10000.0f;
		if(StatusItemActive && Ui()->ActiveItem() != s_pItemButtons[i] && Ui()->MouseInside(&TempItemButton))
		{
			std::swap(s_pItemButtons[i], s_pItemButtons[HotStatusIndex]);
			std::swap(GameClient()->m_StatusBar.m_StatusBarItems[i], GameClient()->m_StatusBar.m_StatusBarItems[HotStatusIndex]);
			s_SelectedItem = -2;
			s_ItemSwaps[HotStatusIndex].m_InitialPosition = vec2(StatusItemButton.x, StatusItemButton.y);
			s_ItemSwaps[HotStatusIndex].m_Duration = 0.15f;
			s_ItemSwaps[i].m_InitialPosition = vec2(s_ActivePos.x, s_ActivePos.y);
			s_ItemSwaps[i].m_Duration = 0.15f;
			GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		}
		TempItemButton = StatusItemButton;
		s_ItemSwaps[i].m_Duration = std::max(0.0f, s_ItemSwaps[i].m_Duration - Client()->RenderFrameTime());
		if(s_ItemSwaps[i].m_Duration > 0.0f)
		{
			float Progress = std::pow(2.0, -5.0 * (1.0 - s_ItemSwaps[i].m_Duration / 0.15f));
			TempItemButton.x = mix(TempItemButton.x, s_ItemSwaps[i].m_InitialPosition.x, Progress);
		}
		if(DoButtonLineSize_Menu(s_pItemButtons[i], Localize(GetStatusBarEditorLabel(StatusItem)), 0, &TempItemButton, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, Col))
		{
			if(s_SelectedItem == -2)
				s_SelectedItem++;
			else if(s_SelectedItem != i)
			{
				s_SelectedItem = i;
				for(int t = 0; t < (int)GameClient()->m_StatusBar.m_StatusItemTypes.size(); ++t)
					if(str_comp(GameClient()->m_StatusBar.m_StatusItemTypes[t].m_aName, StatusItem->m_aName) == 0)
						s_TypeSelectedOld = t;
			}
			else
			{
				s_SelectedItem = -1;
				s_TypeSelectedOld = -1;
			}
		}
		if(Ui()->ActiveItem() == s_pItemButtons[i])
			s_ActivePos = vec2(StatusItemButton.x, StatusItemButton.y);
	}
	if(!StatusItemActive)
		s_SelectedItem = std::max(-1, s_SelectedItem);
}

void CMenus::RenderSettingsTClientInfo(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label, LowerLeftView;
	const int DeferredFrames = GetDeferredTClientTabFrames(TCLIENT_TAB_INFO);

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, Localize("TClient Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_GithubButton, s_SupportButton;
	CUIRect ButtonLeft, ButtonRight;

	if(DeferredFrames >= 4)
	{
		LeftView.HSplitTop(LineSize, &Label, &LeftView);
		Ui()->DoLabel(&Label, Localize("Discord / Website / Github / Support"), FontSize, TEXTALIGN_ML);
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	}
	else
	{
		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
		if(DoButtonLineSize_Menu(&s_DiscordButton, Localize("Discord"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			Client()->ViewLink("https://discord.gg/fBvhH93Bt6");
		if(DoButtonLineSize_Menu(&s_WebsiteButton, Localize("Website"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			Client()->ViewLink("https://tclient.app/");

		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

		if(DoButtonLineSize_Menu(&s_GithubButton, Localize("Github"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			Client()->ViewLink("https://github.com/sjrc6/TaterClient-ddnet");
		if(DoButtonLineSize_Menu(&s_SupportButton, Localize("Support ♥"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
			Client()->ViewLink("https://ko-fi.com/Totar");
	}

	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 4.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, Localize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect TClientConfig, ProfilesFile, WarlistFile, ChatbindsFile;

	static CButtonContainer s_Config, s_Profiles, s_Warlist, s_Chatbinds;
	if(DeferredFrames >= 4)
	{
		LeftView.HSplitTop(LineSize, &Label, &LeftView);
		Ui()->DoLabel(&Label, Localize("QmClient Settings / Profiles / War List / Chat Binds"), FontSize, TEXTALIGN_ML);
	}
	else
	{
		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Button.VSplitMid(&TClientConfig, &ProfilesFile, MarginSmall);

		if(DoButtonLineSize_Menu(&s_Config, Localize("QmClient Settings"), 0, &TClientConfig, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::QMCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		if(DoButtonLineSize_Menu(&s_Profiles, Localize("Profiles"), 0, &ProfilesFile, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTPROFILES].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

		LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
		Button.VSplitMid(&WarlistFile, &ChatbindsFile, MarginSmall);

		if(DoButtonLineSize_Menu(&s_Warlist, Localize("War List"), 0, &WarlistFile, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTWARLIST].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		if(DoButtonLineSize_Menu(&s_Chatbinds, Localize("Chat Binds"), 0, &ChatbindsFile, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTCHATBINDS].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
	}

	if(DeferredFrames >= 3)
	{
		RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
		Ui()->DoLabel(&Label, Localize("TClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		char aBufDevelopers[128];
		RightView.HSplitTop(LineSize, &Label, &RightView);
		str_format(aBufDevelopers, sizeof(aBufDevelopers), Localize("Developers: %d"), 5);
		Ui()->DoLabel(&Label, aBufDevelopers, FontSize, TEXTALIGN_ML);
		RightView.HSplitTop(LineSize, &Label, &RightView);
		Ui()->DoLabel(&Label, "Tater / SollyBunny / PeBox", FontSize, TEXTALIGN_ML);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
		Ui()->DoLabel(&Label, Localize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		int HiddenTabs = 0;
		for(int i = 0; i < NUMBER_OF_TCLIENT_TABS; ++i)
			HiddenTabs += IsFlagSet(g_Config.m_TcTClientSettingsTabs, i) ? 1 : 0;
		str_format(aBuf, sizeof(aBuf), Localize("Hidden tabs: %d"), HiddenTabs);
		RightView.HSplitTop(LineSize, &Label, &RightView);
		Ui()->DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);
		return;
	}

	if(DeferredFrames >= 1)
	{
		RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
		Ui()->DoLabel(&Label, Localize("TClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		const char *apDeveloperNames[] = {"Tater", "SollyBunny / bun bun", "PeBox", "Teero", "ChillerDragon"};
		for(const char *pDeveloperName : apDeveloperNames)
		{
			RightView.HSplitTop(LineSize, &Label, &RightView);
			Ui()->DoLabel(&Label, pDeveloperName, FontSize, TEXTALIGN_ML);
		}
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
		Ui()->DoLabel(&Label, Localize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		int HiddenTabs = 0;
		for(int i = 0; i < NUMBER_OF_TCLIENT_TABS; ++i)
			HiddenTabs += IsFlagSet(g_Config.m_TcTClientSettingsTabs, i) ? 1 : 0;
		char aBufHiddenTabs[96];
		str_format(aBufHiddenTabs, sizeof(aBufHiddenTabs), Localize("Hidden tabs: %d"), HiddenTabs);
		RightView.HSplitTop(LineSize, &Label, &RightView);
		Ui()->DoLabel(&Label, aBufHiddenTabs, FontSize, TEXTALIGN_ML);
		return;
	}

	// =======RIGHT VIEW========
	{
		RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
		Ui()->DoLabel(&Label, Localize("TClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);

		const float TeeSize = 50.0f;
		const float CardSize = TeeSize + MarginSmall;
		CUIRect TeeRect, DevCardRect;
		static CButtonContainer s_LinkButton1, s_LinkButton2, s_LinkButton3, s_LinkButton4, s_LinkButton5;
		{
			RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
			DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
			Label.VSplitLeft(TextRender()->TextWidth(LineSize, "Tater"), &Label, &Button);
			Button.VSplitLeft(MarginSmall, nullptr, &Button);
			Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
			Ui()->DoLabel(&Label, "Tater", LineSize, TEXTALIGN_ML);
			if(Ui()->DoButton_FontIcon(&s_LinkButton1, FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
				Client()->ViewLink("https://github.com/sjrc6");
			RenderDevSkin(TeeRect.Center(), 50.0f, "glow_mermyfox", "mermyfox", true, 0, 0, 0, false, true, ColorRGBA(0.92f, 0.29f, 0.48f, 1.0f), ColorRGBA(0.55f, 0.64f, 0.76f, 1.0f));
		}
		{
			RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
			DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
			Label.VSplitLeft(TextRender()->TextWidth(LineSize, "SollyBunny / bun bun"), &Label, &Button);
			Button.VSplitLeft(MarginSmall, nullptr, &Button);
			Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
			Ui()->DoLabel(&Label, "SollyBunny / bun bun", LineSize, TEXTALIGN_ML);
			if(Ui()->DoButton_FontIcon(&s_LinkButton3, FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
				Client()->ViewLink("https://github.com/SollyBunny");
			RenderDevSkin(TeeRect.Center(), 50.0f, "tuzi", "tuzi", false, 0, 0, 2, true, true, true);
		}
		{
			RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
			DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
			Label.VSplitLeft(TextRender()->TextWidth(LineSize, "PeBox"), &Label, &Button);
			Button.VSplitLeft(MarginSmall, nullptr, &Button);
			Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
			Ui()->DoLabel(&Label, "PeBox", LineSize, TEXTALIGN_ML);
			if(Ui()->DoButton_FontIcon(&s_LinkButton2, FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
				Client()->ViewLink("https://github.com/danielkempf");
			RenderDevSkin(TeeRect.Center(), 50.0f, "greyfox", "greyfox", true, 0, 0, 2, false, true, ColorRGBA(0.00f, 0.09f, 1.00f, 1.00f), ColorRGBA(1.00f, 0.92f, 0.00f, 1.00f));
		}
		{
			RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
			DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
			Label.VSplitLeft(TextRender()->TextWidth(LineSize, "Teero"), &Label, &Button);
			Button.VSplitLeft(MarginSmall, nullptr, &Button);
			Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
			Ui()->DoLabel(&Label, "Teero", LineSize, TEXTALIGN_ML);
			if(Ui()->DoButton_FontIcon(&s_LinkButton4, FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
				Client()->ViewLink("https://github.com/Teero888");
			RenderDevSkin(TeeRect.Center(), 50.0f, "glow_mermyfox", "mermyfox", true, 0, 0, 0, false, true, ColorRGBA(1.00f, 1.00f, 1.00f, 1.00f), ColorRGBA(1.00f, 0.02f, 0.13f, 1.00f));
		}
		{
			RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
			DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
			Label.VSplitLeft(TextRender()->TextWidth(LineSize, "ChillerDragon"), &Label, &Button);
			Button.VSplitLeft(MarginSmall, nullptr, &Button);
			Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
			Ui()->DoLabel(&Label, "ChillerDragon", LineSize, TEXTALIGN_ML);
			if(Ui()->DoButton_FontIcon(&s_LinkButton5, FONT_ICON_ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
				Client()->ViewLink("https://github.com/ChillerDragon");
			RenderDevSkin(TeeRect.Center(), 50.0f, "glow_greensward", "greensward", false, 0, 0, 0, false, true, ColorRGBA(1.00f, 1.00f, 1.00f, 1.00f), ColorRGBA(1.00f, 0.02f, 0.13f, 1.00f));
		}
	}

	{
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
		Ui()->DoLabel(&Label, Localize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
		RightView.HSplitTop(MarginSmall, nullptr, &RightView);
		CUIRect LeftSettings, RightSettings;

		RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);
		RightView.HSplitTop(LineSize * 3.5f, nullptr, &RightView);

		const char *apTabNames[] = {
			Localize("Settings"),
			Localize("Bind Wheel"),
			Localize("War List"),
			Localize("Chat Binds"),
			Localize("Status Bar"),
			Localize("Info")};
		static int s_aShowTabs[NUMBER_OF_TCLIENT_TABS] = {};
		for(int i = 0; i < NUMBER_OF_TCLIENT_TABS - 1; ++i)
		{
			DoButton_CheckBoxAutoVMarginAndSet(&s_aShowTabs[i], apTabNames[i], &s_aShowTabs[i], i % 2 == 0 ? &LeftSettings : &RightSettings, LineSize);
			SetFlag(g_Config.m_TcTClientSettingsTabs, i, s_aShowTabs[i]);
		}
	}

	// RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	// Ui()->DoLabel(&Label, Localize("Integration"), HeadlineFontSize, TEXTALIGN_ML);
	// RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcDiscordRPC, Localize("Enable Discord Integration"), &g_Config.m_TcDiscordRPC, &RightView, LineSize);
}

void CMenus::RenderSettingsTClientProfiles(CUIRect MainView)
{
	int *pCurrentUseCustomColor = m_Dummy ? &g_Config.m_ClDummyUseCustomColor : &g_Config.m_ClPlayerUseCustomColor;

	const char *pCurrentSkinName = m_Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const int CurrentColorBody = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody) : -1;
	const int CurrentColorFeet = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet) : -1;
	const int CurrentFlag = m_Dummy ? g_Config.m_ClDummyCountry : g_Config.m_PlayerCountry;
	const int Emote = m_Dummy ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes;
	const char *pCurrentName = m_Dummy ? g_Config.m_ClDummyName : g_Config.m_PlayerName;
	const char *pCurrentClan = m_Dummy ? g_Config.m_ClDummyClan : g_Config.m_PlayerClan;

	const CProfile CurrentProfile(
		CurrentColorBody,
		CurrentColorFeet,
		CurrentFlag,
		Emote,
		pCurrentSkinName,
		pCurrentName,
		pCurrentClan);

	static int s_SelectedProfile = -1;
	auto &vProfiles = GameClient()->m_SkinProfiles.m_Profiles;
	if(s_SelectedProfile >= (int)vProfiles.size())
		s_SelectedProfile = vProfiles.empty() ? -1 : (int)vProfiles.size() - 1;

	CUIRect Label, Button;

	auto RenderProfile = [&](CUIRect Rect, const CProfile &Profile, bool Main) {
		auto RenderCross = [&](CUIRect Cross, float MaxSize = 0.0f) {
			float MaxExtent = std::max(Cross.w, Cross.h);
			if(MaxSize > 0.0f && MaxExtent > MaxSize)
				MaxExtent = MaxSize;
			TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f));
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			const auto TextBoundingBox = TextRender()->TextBoundingBox(MaxExtent * 0.8f, FONT_ICON_XMARK);
			TextRender()->Text(Cross.x + (Cross.w - TextBoundingBox.m_W) / 2.0f, Cross.y + (Cross.h - TextBoundingBox.m_H) / 2.0f, MaxExtent * 0.8f, FONT_ICON_XMARK);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		};
		{
			CUIRect Skin;
			Rect.VSplitLeft(50.0f, &Skin, &Rect);
			if(!Main && Profile.m_SkinName[0] == '\0')
			{
				RenderCross(Skin, 20.0f);
			}
			else
			{
				CTeeRenderInfo TeeRenderInfo;
				TeeRenderInfo.Apply(GameClient()->m_Skins.Find(Profile.m_SkinName));
				TeeRenderInfo.ApplyColors(Profile.m_BodyColor >= 0 && Profile.m_FeetColor > 0, Profile.m_BodyColor, Profile.m_FeetColor);
				TeeRenderInfo.m_Size = 50.0f;
				const vec2 Pos = Skin.Center() + vec2(0.0f, TeeRenderInfo.m_Size / 10.0f); // Prevent overflow from hats
				vec2 Dir = vec2(1.0f, 0.0f);
				if(Main)
					RenderTeeCute(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos, false);
				else
					RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos);
			}
		}
		Rect.VSplitLeft(5.0f, nullptr, &Rect);
		{
			CUIRect Colors;
			Rect.VSplitLeft(10.0f, &Colors, &Rect);
			CUIRect BodyColor{Colors.Center().x - 5.0f, Colors.Center().y - 11.0f, 10.0f, 10.0f};
			CUIRect FeetColor{Colors.Center().x - 5.0f, Colors.Center().y + 1.0f, 10.0f, 10.0f};
			if(Profile.m_BodyColor >= 0 && Profile.m_FeetColor > 0)
			{
				// Body Color
				Graphics()->DrawRect(BodyColor.x, BodyColor.y, BodyColor.w, BodyColor.h,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f),
					IGraphics::CORNER_ALL, 2.0f);
				// Feet Color;
				Graphics()->DrawRect(FeetColor.x, FeetColor.y, FeetColor.w, FeetColor.h,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f),
					IGraphics::CORNER_ALL, 2.0f);
			}
			else
			{
				RenderCross(BodyColor);
				RenderCross(FeetColor);
			}
		}
		Rect.VSplitLeft(5.0f, nullptr, &Rect);
		{
			CUIRect Flag;
			Rect.VSplitRight(50.0f, &Rect, &Flag);
			Flag = {Flag.x, Flag.y + (Flag.h - 25.0f) / 2.0f, Flag.w, 25.0f};
			if(Profile.m_CountryFlag == -2)
				RenderCross(Flag, 20.0f);
			else
				GameClient()->m_CountryFlags.Render(Profile.m_CountryFlag, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), Flag.x, Flag.y, Flag.w, Flag.h);
		}
		Rect.VSplitRight(5.0f, &Rect, nullptr);
		{
			const float Height = Rect.h / 3.0f;
			if(Main)
			{
				char aBuf[256];
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), Localize("Name: %s"), Profile.m_Name);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), Localize("Clan: %s"), Profile.m_Clan);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), Localize("Skin: %s"), Profile.m_SkinName);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
			}
			else
			{
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Name, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Clan, Height / LineSize * FontSize, TEXTALIGN_ML);
			}
		}
	};

	auto IsSelectedProfileValid = [&]() {
		return s_SelectedProfile >= 0 && s_SelectedProfile < (int)vProfiles.size();
	};

	auto pSelectedProfile = [&]() -> CProfile * {
		if(!IsSelectedProfileValid())
			return nullptr;
		return &vProfiles[s_SelectedProfile];
	};

	auto pConstSelectedProfile = [&]() -> const CProfile * {
		if(!IsSelectedProfileValid())
			return nullptr;
		return &vProfiles[s_SelectedProfile];
	};

	auto BuildProfileFromCurrentSettings = [&]() {
		return CProfile(
			g_Config.m_TcProfileColors ? CurrentColorBody : -1,
			g_Config.m_TcProfileColors ? CurrentColorFeet : -1,
			g_Config.m_TcProfileFlag ? CurrentFlag : -2,
			g_Config.m_TcProfileEmote ? Emote : -1,
			g_Config.m_TcProfileSkin ? pCurrentSkinName : "",
			g_Config.m_TcProfileName ? pCurrentName : "",
			g_Config.m_TcProfileClan ? pCurrentClan : "");
	};

	auto BuildPreviewProfile = [&]() {
		CProfile PreviewProfile = CurrentProfile;
		const CProfile *pProfile = pConstSelectedProfile();
		if(!pProfile)
			return PreviewProfile;

		if(g_Config.m_TcProfileSkin && pProfile->m_SkinName[0] != '\0')
			str_copy(PreviewProfile.m_SkinName, pProfile->m_SkinName);
		if(g_Config.m_TcProfileColors && pProfile->m_BodyColor != -1 && pProfile->m_FeetColor != -1)
		{
			PreviewProfile.m_BodyColor = pProfile->m_BodyColor;
			PreviewProfile.m_FeetColor = pProfile->m_FeetColor;
		}
		if(g_Config.m_TcProfileEmote && pProfile->m_Emote != -1)
			PreviewProfile.m_Emote = pProfile->m_Emote;
		if(g_Config.m_TcProfileName && pProfile->m_Name[0] != '\0')
			str_copy(PreviewProfile.m_Name, pProfile->m_Name);
		if(g_Config.m_TcProfileClan && (pProfile->m_Clan[0] != '\0' || g_Config.m_TcProfileOverwriteClanWithEmpty))
			str_copy(PreviewProfile.m_Clan, pProfile->m_Clan);
		if(g_Config.m_TcProfileFlag && pProfile->m_CountryFlag != -2)
			PreviewProfile.m_CountryFlag = pProfile->m_CountryFlag;

		return PreviewProfile;
	};

	auto ApplySelectedProfile = [&]() {
		const CProfile *pProfile = pConstSelectedProfile();
		if(!pProfile)
			return;
		GameClient()->m_SkinProfiles.ApplyProfile(m_Dummy, *pProfile);
	};

	auto DeleteSelectedProfile = [&]() {
		if(!IsSelectedProfileValid())
			return;
		vProfiles.erase(vProfiles.begin() + s_SelectedProfile);
		if(vProfiles.empty())
			s_SelectedProfile = -1;
		else if(s_SelectedProfile >= (int)vProfiles.size())
			s_SelectedProfile = (int)vProfiles.size() - 1;
	};

	{
		CUIRect Top;
		MainView.HSplitTop(160.0f, &Top, &MainView);
		CUIRect Profiles, Settings, Actions;
		Top.VSplitLeft(300.0f, &Profiles, &Top);
		{
			CUIRect Skin;
			Profiles.HSplitTop(LineSize, &Label, &Profiles);
			Ui()->DoLabel(&Label, Localize("Your profile"), FontSize, TEXTALIGN_ML);
			Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
			Profiles.HSplitTop(50.0f, &Skin, &Profiles);
			RenderProfile(Skin, CurrentProfile, true);

			// After load
			if(pConstSelectedProfile())
			{
				Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
				Profiles.HSplitTop(LineSize, &Label, &Profiles);
				Ui()->DoLabel(&Label, Localize("After Load"), FontSize, TEXTALIGN_ML);
				Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
				Profiles.HSplitTop(50.0f, &Skin, &Profiles);
				RenderProfile(Skin, BuildPreviewProfile(), true);
			}
		}
		Top.VSplitLeft(20.0f, nullptr, &Top);
		Top.VSplitMid(&Settings, &Actions, 20.0f);
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileSkin, Localize("Save/Load Skin"), &g_Config.m_TcProfileSkin, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileColors, Localize("Save/Load Colors"), &g_Config.m_TcProfileColors, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileEmote, Localize("Save/Load Emote"), &g_Config.m_TcProfileEmote, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileName, Localize("Save/Load Name"), &g_Config.m_TcProfileName, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileClan, Localize("Save/Load Clan"), &g_Config.m_TcProfileClan, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileFlag, Localize("Save/Load Flag"), &g_Config.m_TcProfileFlag, &Settings, LineSize);
		}
		{
			Actions.HSplitTop(30.0f, &Button, &Actions);
			static CButtonContainer s_LoadButton;
			if(DoButton_Menu(&s_LoadButton, Localize("Load"), 0, &Button))
				ApplySelectedProfile();
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			Actions.HSplitTop(30.0f, &Button, &Actions);
			static CButtonContainer s_SaveButton;
			if(DoButton_Menu(&s_SaveButton, Localize("Save"), 0, &Button))
			{
				const CProfile ProfileToSave = BuildProfileFromCurrentSettings();
				GameClient()->m_SkinProfiles.AddProfile(
					ProfileToSave.m_BodyColor,
					ProfileToSave.m_FeetColor,
					ProfileToSave.m_CountryFlag,
					ProfileToSave.m_Emote,
					ProfileToSave.m_SkinName,
					ProfileToSave.m_Name,
					ProfileToSave.m_Clan);
			}
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			static int s_AllowDelete;
			DoButton_CheckBoxAutoVMarginAndSet(&s_AllowDelete, Localizable("Enable Deleting"), &s_AllowDelete, &Actions, LineSize);
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			if(s_AllowDelete)
			{
				Actions.HSplitTop(30.0f, &Button, &Actions);
				static CButtonContainer s_DeleteButton;
				if(DoButton_Menu(&s_DeleteButton, Localize("Delete"), 0, &Button))
					DeleteSelectedProfile();
				Actions.HSplitTop(5.0f, nullptr, &Actions);

				Actions.HSplitTop(30.0f, &Button, &Actions);
				static CButtonContainer s_OverrideButton;
				if(DoButton_Menu(&s_OverrideButton, Localize("Override"), 0, &Button))
				{
					if(CProfile *pProfile = pSelectedProfile())
						*pProfile = BuildProfileFromCurrentSettings();
				}
			}
		}
	}
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	{
		CUIRect Options;
		MainView.HSplitTop(LineSize, &Options, &MainView);

		Options.VSplitLeft(150.0f, &Button, &Options);
		if(DoButton_CheckBox(&m_Dummy, Localize("Dummy"), m_Dummy, &Button))
			m_Dummy = 1 - m_Dummy;

		Options.VSplitLeft(150.0f, &Button, &Options);
		static int s_CustomColorId = 0;
		if(DoButton_CheckBox(&s_CustomColorId, Localize("Custom colors"), *pCurrentUseCustomColor, &Button))
		{
			*pCurrentUseCustomColor = *pCurrentUseCustomColor ? 0 : 1;
			SetNeedSendInfo();
		}

		Button = Options;
		if(DoButton_CheckBox(&g_Config.m_TcProfileOverwriteClanWithEmpty, Localize("Overwrite clan even if empty"), g_Config.m_TcProfileOverwriteClanWithEmpty, &Button))
			g_Config.m_TcProfileOverwriteClanWithEmpty = 1 - g_Config.m_TcProfileOverwriteClanWithEmpty;
	}
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	{
		CUIRect SelectorRect;
		MainView.HSplitBottom(LineSize + MarginSmall, &MainView, &SelectorRect);
		SelectorRect.HSplitTop(MarginSmall, nullptr, &SelectorRect);

		static CButtonContainer s_ProfilesFile;
		SelectorRect.VSplitLeft(130.0f, &Button, &SelectorRect);
		if(DoButton_Menu(&s_ProfilesFile, Localize("Profiles file"), 0, &Button))
		{
			char aBuf[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTPROFILES].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
	}

	static CListBox s_ListBox;
	s_ListBox.DoStart(50.0f, vProfiles.size(), MainView.w / 200.0f, 3, s_SelectedProfile, &MainView, true, IGraphics::CORNER_ALL, true);

	static std::vector<int> s_vProfileItemIds;
	if(s_vProfileItemIds.size() != vProfiles.size())
	{
		s_vProfileItemIds.resize(vProfiles.size());
		for(size_t i = 0; i < s_vProfileItemIds.size(); ++i)
			s_vProfileItemIds[i] = (int)i;
	}

	for(size_t i = 0; i < vProfiles.size(); ++i)
	{
		CListboxItem Item = s_ListBox.DoNextItem(&s_vProfileItemIds[i], s_SelectedProfile >= 0 && (size_t)s_SelectedProfile == i);
		if(!Item.m_Visible)
			continue;

		RenderProfile(Item.m_Rect, vProfiles[i], false);
	}

	s_SelectedProfile = s_ListBox.DoEnd();
	if(s_ListBox.WasItemActivated())
		ApplySelectedProfile();
}

void CMenus::RenderSettingsTClientConfigs(CUIRect MainView)
{
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

	auto ClearStagedAndCaches = [&]() {
		s_StagedInts.clear();
		s_StagedStrs.clear();
		s_StagedCols.clear();
		s_IntInputs.clear();
		s_StrInputs.clear();
		s_ColInputs.clear();
	};

	size_t ChangesCount = 0;

	CUIRect ApplyBar, FilterBar, TagsBar, ListArea;
	MainView.VSplitRight(5.0f, &MainView, nullptr); // padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	MainView.HSplitTop(LineSize + MarginSmall, &ApplyBar, &MainView);
	MainView.HSplitTop(LineSize + MarginSmall, &FilterBar, &MainView);
	MainView.HSplitTop((LineSize + MarginSmall) * 2, &TagsBar, &ListArea); // Two rows for tags
	ListArea.HSplitTop(MarginSmall, nullptr, &ListArea);

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
	// Apply Bar - 应用/清除按钮 + 更改计数
	{
		CUIRect Row = ApplyBar;
		Row.HMargin(MarginSmall, &Row);
		Row.h = LineSize;
		Row.y = ApplyBar.y + (ApplyBar.h - LineSize) / 2.0f;

		const float BtnWidth = 120.0f;
		CUIRect ApplyBtn, ClearBtn, Counter;
		Row.VSplitLeft(BtnWidth, &ApplyBtn, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Row.VSplitLeft(BtnWidth, &ClearBtn, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Counter);

		static CButtonContainer s_ApplyBtn, s_ClearBtn;
		int DisabledStyle = ChangesCount > 0 ? 0 : -1;
		const bool ApplyClicked = DoButton_Menu(&s_ApplyBtn, Localize("Apply Changes"), DisabledStyle, &ApplyBtn);
		if(ChangesCount > 0 && ApplyClicked)
		{
			for(const auto &it : s_StagedInts)
			{
				const SConfigVariable *pVar = it.first;
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %d", pVar->m_pScriptName, it.second.m_Value);
				Console()->ExecuteLine(aCmd);
			}
			for(const auto &it : s_StagedStrs)
			{
				const SConfigVariable *pVar = it.first;
				char aEsc[1024];
				aEsc[0] = '\0';
				char *pDst = aEsc;
				str_escape(&pDst, it.second.m_Value.c_str(), aEsc + sizeof(aEsc));
				char aCmd[1200];
				str_format(aCmd, sizeof(aCmd), "%s \"%s\"", pVar->m_pScriptName, aEsc);
				Console()->ExecuteLine(aCmd);
			}
			for(const auto &it : s_StagedCols)
			{
				const SConfigVariable *pVar = it.first;
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %u", pVar->m_pScriptName, it.second.m_Value);
				Console()->ExecuteLine(aCmd);
			}
			ClearStagedAndCaches();
		}
		const bool ClearClicked = DoButton_Menu(&s_ClearBtn, Localize("Clear Changes"), DisabledStyle, &ClearBtn);
		if(ChangesCount > 0 && ClearClicked)
		{
			ClearStagedAndCaches();
		}

		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("Changes: %d"), (int)ChangesCount);
		Ui()->DoLabel(&Counter, aBuf, FontSize, TEXTALIGN_ML);
	}

	// Filter Bar - 搜索 + 所有筛选条件整合在一行
	const float SearchLabelW = 50.0f;
	{
		CUIRect Row = FilterBar;
		Row.HMargin(MarginSmall, &Row);
		Row.h = LineSize;
		Row.y = FilterBar.y + (FilterBar.h - LineSize) / 2.0f;

		// 搜索框
		CUIRect SearchLabel, SearchEdit;
		Row.VSplitLeft(SearchLabelW, &SearchLabel, &Row);
		Row.VSplitLeft(250.0f, &SearchEdit, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Ui()->DoLabel(&SearchLabel, Localize("Search"), FontSize, TEXTALIGN_ML);
		Ui()->DoClearableEditBox(&s_SearchInput, &SearchEdit, EditBoxFontSize);

		// 分隔
		Row.VSplitLeft(MarginSmall, nullptr, &Row);

		// Domain 筛选 - DDNet / TClient / 栖梦
		const float DomainWidth = 85.0f;
		CUIRect DomainDDNet, DomainTClient, DomainQm;
		Row.VSplitLeft(DomainWidth, &DomainDDNet, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Row.VSplitLeft(DomainWidth, &DomainTClient, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Row.VSplitLeft(DomainWidth, &DomainQm, &Row);
		Row.VSplitLeft(Margin, nullptr, &Row);

		if(DoButton_CheckBox(&g_Config.m_TcUiShowDDNet, Localize("DDNet"), g_Config.m_TcUiShowDDNet, &DomainDDNet))
			g_Config.m_TcUiShowDDNet ^= 1;
		if(DoButton_CheckBox(&g_Config.m_TcUiShowTClient, Localize("TClient"), g_Config.m_TcUiShowTClient, &DomainTClient))
			g_Config.m_TcUiShowTClient ^= 1;
		if(DoButton_CheckBox(&g_Config.m_TcUiShowQm, Localize("栖梦"), g_Config.m_TcUiShowQm, &DomainQm))
			g_Config.m_TcUiShowQm ^= 1;

		// 其他筛选 - 紧凑列表 / 仅显示已修改
		const float FilterWidth = 90.0f;
		CUIRect FilterCompact, FilterModified;
		Row.VSplitLeft(FilterWidth, &FilterCompact, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Row.VSplitLeft(FilterWidth, &FilterModified, &Row);

		if(DoButton_CheckBox(&g_Config.m_TcUiCompactList, Localize("Compact"), g_Config.m_TcUiCompactList, &FilterCompact))
			g_Config.m_TcUiCompactList ^= 1;
		if(DoButton_CheckBox(&g_Config.m_TcUiOnlyModified, Localize("Modified"), g_Config.m_TcUiOnlyModified, &FilterModified))
			g_Config.m_TcUiOnlyModified ^= 1;
	}

	// Tags Filter Bar - Row 1
	{
		CUIRect TagsRow = TagsBar;
		TagsRow.h = LineSize;
		TagsRow.y = TagsBar.y;

		const float TagLabelWidth = 40.0f;
		CUIRect TagsLabel, TagsArea;
		TagsRow.VSplitLeft(TagLabelWidth, &TagsLabel, &TagsArea);
		Ui()->DoLabel(&TagsLabel, Localize("Tags"), FontSize, TEXTALIGN_ML);

		// Calculate tag button width - fit 5 tags per row
		const float TagMargin = 5.0f;
		const int TagsPerRow = 5;
		float TagBtnWidth = (TagsArea.w - TagMargin * (TagsPerRow - 1)) / TagsPerRow;

		CUIRect TagBtn;
		TagsArea.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea);
		if(DoButton_CheckBox(&s_TcUiTagVisual, Localize("Visual"), s_TcUiTagVisual, &TagBtn))
			s_TcUiTagVisual ^= 1;

		TagsArea.VSplitLeft(TagMargin, nullptr, &TagsArea);
		TagsArea.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea);
		if(DoButton_CheckBox(&s_TcUiTagHud, Localize("HUD"), s_TcUiTagHud, &TagBtn))
			s_TcUiTagHud ^= 1;

		TagsArea.VSplitLeft(TagMargin, nullptr, &TagsArea);
		TagsArea.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea);
		if(DoButton_CheckBox(&s_TcUiTagInput, Localize("Input"), s_TcUiTagInput, &TagBtn))
			s_TcUiTagInput ^= 1;

		TagsArea.VSplitLeft(TagMargin, nullptr, &TagsArea);
		TagsArea.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea);
		if(DoButton_CheckBox(&s_TcUiTagChat, Localize("Chat"), s_TcUiTagChat, &TagBtn))
			s_TcUiTagChat ^= 1;

		TagsArea.VSplitLeft(TagMargin, nullptr, &TagsArea);
		TagsArea.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea);
		if(DoButton_CheckBox(&s_TcUiTagAudio, Localize("Audio"), s_TcUiTagAudio, &TagBtn))
			s_TcUiTagAudio ^= 1;
	}

	// Tags Filter Bar - Row 2 (Automation, Social, Camera, Gameplay, Misc)
	{
		CUIRect TagsRow2 = TagsBar;
		TagsRow2.h = LineSize;
		TagsRow2.y = TagsBar.y + LineSize + 2.0f;

		const float TagLabelWidth = 40.0f;
		CUIRect TagsLabel2, TagsArea2;
		TagsRow2.VSplitLeft(TagLabelWidth, &TagsLabel2, &TagsArea2);
		// Leave label empty for second row alignment

		const float TagMargin = 5.0f;
		const int TagsPerRow = 5;
		float TagBtnWidth = (TagsArea2.w - TagMargin * (TagsPerRow - 1)) / TagsPerRow;

		CUIRect TagBtn;
		TagsArea2.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea2);
		if(DoButton_CheckBox(&s_TcUiTagAutomation, Localize("Auto"), s_TcUiTagAutomation, &TagBtn))
			s_TcUiTagAutomation ^= 1;

		TagsArea2.VSplitLeft(TagMargin, nullptr, &TagsArea2);
		TagsArea2.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea2);
		if(DoButton_CheckBox(&s_TcUiTagSocial, Localize("Social"), s_TcUiTagSocial, &TagBtn))
			s_TcUiTagSocial ^= 1;

		TagsArea2.VSplitLeft(TagMargin, nullptr, &TagsArea2);
		TagsArea2.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea2);
		if(DoButton_CheckBox(&s_TcUiTagCamera, Localize("Camera"), s_TcUiTagCamera, &TagBtn))
			s_TcUiTagCamera ^= 1;

		TagsArea2.VSplitLeft(TagMargin, nullptr, &TagsArea2);
		TagsArea2.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea2);
		if(DoButton_CheckBox(&s_TcUiTagGameplay, Localize("Gameplay"), s_TcUiTagGameplay, &TagBtn))
			s_TcUiTagGameplay ^= 1;

		TagsArea2.VSplitLeft(TagMargin, nullptr, &TagsArea2);
		TagsArea2.VSplitLeft(TagBtnWidth, &TagBtn, &TagsArea2);
		if(DoButton_CheckBox(&s_TcUiTagMisc, Localize("Misc"), s_TcUiTagMisc, &TagBtn))
			s_TcUiTagMisc ^= 1;
	}

	const int FlagMask = CFGFLAG_CLIENT;
	static std::vector<const SConfigVariable *> s_vAllClientVars;
	if(s_vAllClientVars.empty())
	{
		auto IsLegacyMigratedQmConfig = [](const char *pScriptName) {
			return str_comp(pScriptName, "tc_hide_chat_bubbles") == 0 ||
				str_comp(pScriptName, "tc_chat_bubble") == 0 ||
				str_comp(pScriptName, "tc_chat_bubble_duration") == 0 ||
				str_comp(pScriptName, "tc_chat_bubble_alpha") == 0 ||
				str_comp(pScriptName, "tc_chat_bubble_font_size") == 0 ||
				str_comp(pScriptName, "tc_chat_bubble_bg_color") == 0 ||
				str_comp(pScriptName, "tc_chat_bubble_text_color") == 0 ||
				str_comp(pScriptName, "tc_chat_bubble_animation") == 0 ||
				str_comp(pScriptName, "cl_scoreboard_points") == 0 ||
				str_comp(pScriptName, "cl_scoreboard_sort_mode") == 0 ||
				str_comp(pScriptName, "cl_dummy_miniview") == 0 ||
				str_comp(pScriptName, "cl_dummy_miniview_auto") == 0 ||
				str_comp(pScriptName, "cl_dummy_miniview_size") == 0 ||
				str_comp(pScriptName, "cl_dummy_miniview_zoom") == 0 ||
				str_comp(pScriptName, "cl_smtc_enable") == 0 ||
				str_comp(pScriptName, "cl_smtc_show_hud") == 0;
		};
		auto Collector = [](const SConfigVariable *pVar, void *pUserData) {
			auto *pVec = static_cast<std::vector<const SConfigVariable *> *>(pUserData);
			pVec->push_back(pVar);
		};
		std::vector<const SConfigVariable *> vCollectedVars;
		ConfigManager()->PossibleConfigVariables("", FlagMask, Collector, &vCollectedVars);
		for(const SConfigVariable *pVar : vCollectedVars)
		{
			const char *pScriptName = pVar->m_pScriptName ? pVar->m_pScriptName : "";
			if(IsLegacyMigratedQmConfig(pScriptName))
				continue;
			s_vAllClientVars.push_back(pVar);
		}
		std::sort(s_vAllClientVars.begin(), s_vAllClientVars.end(), [](const SConfigVariable *a, const SConfigVariable *b) {
			if(a->m_ConfigDomain != b->m_ConfigDomain)
				return a->m_ConfigDomain < b->m_ConfigDomain;
			return str_comp(a->m_pScriptName, b->m_pScriptName) < 0;
		});
	}

	auto GetConfigSource = [&](const SConfigVariable *pVar) {
		if(pVar->m_ConfigDomain == ConfigDomain::DDNET)
			return EConfigSource::DDNET;
		const char *pName = pVar->m_pScriptName ? pVar->m_pScriptName : "";
		if(str_startswith(pName, "qm_"))
			return EConfigSource::QM;
		return EConfigSource::TCLIENT;
	};

	auto SourceEnabled = [&](EConfigSource Source) {
		switch(Source)
		{
		case EConfigSource::DDNET: return g_Config.m_TcUiShowDDNet != 0;
		case EConfigSource::TCLIENT: return g_Config.m_TcUiShowTClient != 0;
		case EConfigSource::QM: return g_Config.m_TcUiShowQm != 0;
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
			const SIntConfigVariable *pint = static_cast<const SIntConfigVariable *>(p);
			auto it = s_StagedInts.find(p);
			int v = it != s_StagedInts.end() ? it->second.m_Value : *pint->m_pVariable;
			return v == pint->m_Default;
		}
		if(p->m_Type == SConfigVariable::VAR_STRING)
		{
			const SStringConfigVariable *ps = static_cast<const SStringConfigVariable *>(p);
			auto it = s_StagedStrs.find(p);
			const char *v = it != s_StagedStrs.end() ? it->second.m_Value.c_str() : ps->m_pStr;
			return str_comp(v, ps->m_pDefault) == 0;
		}
		if(p->m_Type == SConfigVariable::VAR_COLOR)
		{
			const SColorConfigVariable *pc = static_cast<const SColorConfigVariable *>(p);
			auto it = s_StagedCols.find(p);
			unsigned v = it != s_StagedCols.end() ? it->second.m_Value : *pc->m_pVariable;
			return v == pc->m_Default;
		}
		return true;
	};

	std::vector<const SConfigVariable *> vpFiltered;
	vpFiltered.reserve(s_vAllClientVars.size());
	for(const SConfigVariable *pVar : s_vAllClientVars)
	{
		if(!SourceEnabled(GetConfigSource(pVar)))
			continue;
		if(g_Config.m_TcUiOnlyModified && IsEffectiveDefaultVar(pVar))
			continue;
		if(pSearch && pSearch[0])
		{
			const char *pName = pVar->m_pScriptName ? pVar->m_pScriptName : "";
			const char *pHelp = pVar->m_pHelp ? pVar->m_pHelp : "";
			if(!str_find_nocase(pName, pSearch) && !str_find_nocase(pHelp, pSearch))
				continue;
		}
		// Tags filter
		if(AnyTagEnabled)
		{
			std::vector<EConfigTag> vTags = ConfigTagsManager()->GetTagsForVariable(pVar->m_pScriptName);
			bool HasEnabledTag = false;
			for(EConfigTag Tag : vTags)
			{
				if(TagEnabled(Tag))
				{
					HasEnabledTag = true;
					break;
				}
			}
			// If variable has no tags but Misc is enabled, show it
			if(vTags.empty() && s_TcUiTagMisc)
				HasEnabledTag = true;
			if(!HasEnabledTag)
				continue;
		}
		vpFiltered.push_back(pVar);
	}

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	s_ScrollRegion.Begin(&ListArea, &ScrollOffset, &ScrollParams);

	ListArea.y += ScrollOffset.y;
	ListArea.VSplitRight(5.0f, &ListArea, nullptr);
	CUIRect Content = ListArea;

	auto SourceName = [](EConfigSource Source) {
		switch(Source)
		{
		case EConfigSource::DDNET: return "DDNet";
		case EConfigSource::TCLIENT: return "TClient";
		case EConfigSource::QM: return "栖梦";
		default: return "Other";
		}
	};

	EConfigSource CurrentSource = static_cast<EConfigSource>(-1);
	for(const SConfigVariable *pVar : vpFiltered)
	{
		const EConfigSource Source = GetConfigSource(pVar);
		if(Source != CurrentSource)
		{
			CurrentSource = Source;
			CUIRect Header;
			Content.HSplitTop(HeadlineHeight, &Header, &Content);
			if(s_ScrollRegion.AddRect(Header))
				Ui()->DoLabel(&Header, SourceName(CurrentSource), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
		}

		CUIRect RowItem;
		const float RowHeight = g_Config.m_TcUiCompactList ? (std::max(LineSize, ColorPickerLineSize) + 5.0f) : 55.0f;
		Content.HSplitTop(RowHeight, &RowItem, &Content);
		Content.HSplitTop(MarginExtraSmall, nullptr, &Content);
		const bool Visible = s_ScrollRegion.AddRect(RowItem);
		if(!Visible)
			continue;

		const bool Modified = !IsEffectiveDefaultVar(pVar);
		const ColorRGBA BgModified = ColorRGBA(1.0f, 0.8f, 0.0f, 0.15f);
		const ColorRGBA BgNormal = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
		RowItem.Draw(Modified ? BgModified : BgNormal, IGraphics::CORNER_ALL, 6.0f);

		CUIRect RowContent;
		RowItem.Margin(5.0f, &RowContent);

		CUIRect TopLine, Below;
		if(g_Config.m_TcUiCompactList)
		{
			const float UsedHeight = (pVar->m_Type == SConfigVariable::VAR_COLOR) ? ColorPickerLineSize : LineSize;
			TopLine = RowContent;
			TopLine.h = UsedHeight;
			TopLine.y = round_to_int(RowContent.y + (RowContent.h - UsedHeight) / 2.0f);
			Below = RowContent;
		}
		else
		{
			RowContent.HSplitTop(LineSize, &TopLine, &Below);
		}
		CUIRect NameLine, Right;
		TopLine.VSplitRight(320.0f, &NameLine, &Right);
		NameLine.VSplitLeft(10.0f, nullptr, &NameLine);

		Ui()->DoLabel(&NameLine, pVar->m_pScriptName, FontSize, TEXTALIGN_ML);

		CUIRect Controls, ResetRect;
		Right.VSplitRight(120.0f, &Controls, &ResetRect);
		Controls.h = LineSize;
		Controls.y = TopLine.y + (TopLine.h - LineSize) / 2.0f;
		ResetRect.h = LineSize;
		ResetRect.y = Controls.y;
		Controls.VSplitRight(MarginSmall, &Controls, nullptr);

		if(!g_Config.m_TcUiCompactList)
		{
			CUIRect Help;
			Below.HSplitTop(2.0f, nullptr, &Below);
			Help = Below;
			Help.VSplitLeft(10.0f, nullptr, &Help);
			Ui()->DoLabel(&Help, pVar->m_pHelp ? pVar->m_pHelp : "", 11.0f, TEXTALIGN_ML);
		}

		static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ResetBtns;
		if(Modified && pVar->m_Type != SConfigVariable::VAR_COLOR)
		{
			CButtonContainer &ResetBtn = s_ResetBtns[pVar];
			if(DoButton_Menu(&ResetBtn, Localize("Reset"), 0, &ResetRect))
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
			if(pInt->m_Min == 0 && pInt->m_Max == 1)
			{
				const int Effective = s_StagedInts.count(pVar) ? s_StagedInts[pVar].m_Value : *pInt->m_pVariable;
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
				SIntState &State = s_IntInputs[pVar];
				const int Effective = s_StagedInts.count(pVar) ? s_StagedInts[pVar].m_Value : *pInt->m_pVariable;
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

				if(Ui()->DoEditBox(&State.m_Input, &InputBox, EditBoxFontSize))
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
			SStrState &State = s_StrInputs[pVar];
			const char *Effective = s_StagedStrs.count(pVar) ? s_StagedStrs[pVar].m_Value.c_str() : pStr->m_pStr;
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

			if(Ui()->DoEditBox(&State.m_Input, &Controls, EditBoxFontSize))
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
			ColorRect.x = Controls.x;
			ColorRect.h = ColorPickerLineSize;
			ColorRect.y = TopLine.y + (TopLine.h - ColorPickerLineSize) / 2.0f;
			ColorRect.w = ColorPickerLineSize + 8.0f + 60.0f;
			const ColorRGBA DefaultColor = color_cast<ColorRGBA>(ColorHSLA(pCol->m_Default, true).UnclampLighting(pCol->m_DarkestLighting));
			static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ColorResetIds;
			CButtonContainer &ResetId = s_ColorResetIds[pVar];

			SColState &ColState = s_ColInputs[pVar];
			unsigned Effective = s_StagedCols.count(pVar) ? s_StagedCols[pVar].m_Value : *pCol->m_pVariable;
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

			DoLine_ColorPicker(&ResetId, ColorPickerLineSize, ColorPickerLabelSize, 0.0f, &ColorRect, "", &ColState.m_Working, DefaultColor, false, nullptr, pCol->m_Alpha);
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

	CUIRect EndPad{Content.x, Content.y, Content.w, 5.0f};
	s_ScrollRegion.AddRect(EndPad);
	s_ScrollRegion.End();
}
