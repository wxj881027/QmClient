#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>

#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/components/qmclient/bindchat.h>
#include <game/client/components/qmclient/bindwheel.h>
#include <game/client/components/qmclient/trails.h>
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

static float s_Time = 0.0f;
static bool s_StartedTime = false;
static constexpr const char *QMCLIENT_LOCALIZATION_CONTEXT = "QmClient";

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

static float CalcQiaFenInputHeight(ITextRender *pTextRender, const char *pText, float Width, float FontSize, float LineSpacing, float MinHeight)
{
	const float VPadding = 2.0f;
	const float LineWidth = maximum(1.0f, Width - VPadding * 2.0f);
	const char *pMeasureText = (pText && pText[0] != '\0') ? pText : " ";
	const STextBoundingBox Box = pTextRender->TextBoundingBox(FontSize, pMeasureText, -1, LineWidth, LineSpacing);
	return maximum(MinHeight, Box.m_H + VPadding * 2.0f);
}

static bool DoEditBoxMultiLine(CUi *pUi, CLineInput *pLineInput, const CUIRect *pRect, float FontSize, float LineSpacing)
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
	pLineInput->Render(&Textbox, FontSize, TEXTALIGN_TL, Changed || CursorChanged, LineWidth, LineSpacing);
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
	const char *apTabNames[] = {
		TCLocalize("Settings"),
		TCLocalize("Bind Wheel"),
		TCLocalize("War List"),
		TCLocalize("Chat Binds"),
		TCLocalize("Status Bar"),
		TCLocalize("Info")};

	for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_TcTClientSettingsTabs, Tab))
			continue;

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : Tab == NUMBER_OF_TCLIENT_TABS - 1 ? IGraphics::CORNER_R :
													 IGraphics::CORNER_NONE;
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurCustomTab = Tab;
	}

	MainView.HSplitTop(Margin, nullptr, &MainView);

	if(!s_CustomTabTransitionInitialized)
	{
		s_PrevCustomTab = s_CurCustomTab;
		s_CustomTabTransitionInitialized = true;
	}
	else if(s_CurCustomTab != s_PrevCustomTab)
	{
		s_CustomTabTransitionDirection = s_CurCustomTab > s_PrevCustomTab ? 1.0f : -1.0f;
		TriggerUiSwitchAnimation(TClientTabSwitchNode, 0.18f);
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

	if(s_CurCustomTab == TCLIENT_TAB_SETTINGS)
		RenderSettingsTClientSettings(ContentView);
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

	if(TransitionActive && TransitionAlpha > 0.0f)
	{
		ContentClip.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha), IGraphics::CORNER_NONE, 0.0f);
	}
	if(TransitionActive)
	{
		Ui()->ClipDisable();
	}
}

void CMenus::RenderSettingsTClientSettings(CUIRect MainView)
{
	CUIRect Column, LeftView, RightView, Button, Label;

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

	// ***** LeftView ***** //
	Column = LeftView;

	// ***** 视觉效果 ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Visual"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static std::vector<std::string> s_FontDropDownNamesOwned;
	static std::vector<const char *> s_FontDropDownNames;
	static CUi::SDropDownState s_FontDropDownState;
	static CScrollRegion s_FontDropDownScrollRegion;
	s_FontDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_FontDropDownScrollRegion;
	s_FontDropDownState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
	const auto &CustomFaces = *TextRender()->GetCustomFaces();
	if(s_FontDropDownNamesOwned != CustomFaces)
	{
		s_FontDropDownNamesOwned = CustomFaces;
		s_FontDropDownNames.clear();
		s_FontDropDownNames.reserve(s_FontDropDownNamesOwned.size());
		for(const auto &FaceName : s_FontDropDownNamesOwned)
			s_FontDropDownNames.push_back(FaceName.c_str());
	}
	int FontSelectedOld = -1;
	for(size_t i = 0; i < CustomFaces.size(); ++i)
	{
		if(str_find_nocase(g_Config.m_TcCustomFont, CustomFaces[i].c_str()))
			FontSelectedOld = i;
	}
	CUIRect FontDropDownRect, FontDirectory;
	Column.HSplitTop(LineSize, &FontDropDownRect, &Column);
	FontDropDownRect.VSplitLeft(100.0f, &Label, &FontDropDownRect);
	FontDropDownRect.VSplitRight(20.0f, &FontDropDownRect, &FontDirectory);
	FontDropDownRect.VSplitRight(MarginSmall, &FontDropDownRect, nullptr);

	Ui()->DoLabel(&Label, TCLocalize("Custom Font: "), FontSize, TEXTALIGN_ML);
	const int FontSelectedNew = Ui()->DoDropDown(&FontDropDownRect, FontSelectedOld, s_FontDropDownNames.data(), s_FontDropDownNames.size(), s_FontDropDownState);
	if(FontSelectedOld != FontSelectedNew && FontSelectedNew >= 0 && (size_t)FontSelectedNew < s_FontDropDownNames.size())
	{
		str_copy(g_Config.m_TcCustomFont, s_FontDropDownNames[FontSelectedNew]);
		TextRender()->SetCustomFace(g_Config.m_TcCustomFont);

		// Attempt to reset all the containers
		TextRender()->OnPreWindowResize();
		GameClient()->OnWindowResize();
		GameClient()->Editor()->OnWindowResize();
		TextRender()->OnWindowResize();
		GameClient()->m_MapImages.SetTextureScale(101);
		GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
	}

	//打开字体文件夹按钮,没有就创建
	static CButtonContainer s_FontDirectoryId;
	if(Ui()->DoButton_FontIcon(&s_FontDirectoryId, FONT_ICON_FOLDER, 0, &FontDirectory, IGraphics::CORNER_ALL))
	{
		Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("qmclient/fonts", IStorage::TYPE_SAVE);
		char aBuf[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "qmclient/fonts", aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	CUIRect TinyTeeConfig;
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

	{
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		static std::vector<const char *> s_DropDownNames;
		s_DropDownNames = {TCLocalize("Normal", "Hammer Mode"), TCLocalize("Rotate with cursor", "Hammer Mode"), TCLocalize("Rotate with cursor like gun", "Hammer Mode")};
		static CUi::SDropDownState s_DropDownState;
		static CScrollRegion s_DropDownScrollRegion;
		s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
		CUIRect DropDownRect;
		Column.HSplitTop(LineSize, &DropDownRect, &Column);
		DropDownRect.VSplitLeft(120.0f, &Label, &DropDownRect);
		Ui()->DoLabel(&Label, TCLocalize("Hammer Mode: "), FontSize, TEXTALIGN_ML);
		g_Config.m_TcHammerRotatesWithCursor = Ui()->DoDropDown(&DropDownRect, g_Config.m_TcHammerRotatesWithCursor, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	}

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcCursorScale, &g_Config.m_TcCursorScale, &Button, TCLocalize("Ingame cursor scale"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(LineSize, &Button, &Column);
	if(g_Config.m_TcAnimateWheelTime > 0)
		Ui()->DoScrollbarOption(&g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, TCLocalize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms");
	else
		Ui()->DoScrollbarOption(&g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, TCLocalize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms (off)");

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplatePingCircle, TCLocalize("Show ping colored circle in nameplates"), &g_Config.m_TcNameplatePingCircle, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplateCountry, TCLocalize("Show country flags in nameplates"), &g_Config.m_TcNameplateCountry, &Column, LineSize);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderNameplateSpec, TCLocalize("Hide nameplates in spec"), &g_Config.m_TcRenderNameplateSpec, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplateSkins, TCLocalize("Show skin names in nameplate"), &g_Config.m_TcNameplateSkins, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFreezeStars, TCLocalize("Freeze stars"), &g_Config.m_ClFreezeStars, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcColorFreeze, TCLocalize("Use colored skins for frozen tees", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcColorFreeze, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFreezeKatana, TCLocalize("Show katan on frozen players"), &g_Config.m_TcFreezeKatana, &Column, LineSize);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWhiteFeet, TCLocalize("Render all custom colored feet as white feet skin"), &g_Config.m_TcWhiteFeet, &Column, LineSize);
	CUIRect FeetBox;
	Column.HSplitTop(LineSize + MarginExtraSmall, &FeetBox, &Column);
	if(g_Config.m_TcWhiteFeet)
	{
		FeetBox.HSplitTop(MarginExtraSmall, nullptr, &FeetBox);
		FeetBox.VSplitMid(&FeetBox, nullptr);
		static CLineInput s_WhiteFeet(g_Config.m_TcWhiteFeetSkin, sizeof(g_Config.m_TcWhiteFeetSkin));
		s_WhiteFeet.SetEmptyText("x_ninja");
		Ui()->DoEditBox(&s_WhiteFeet, &FeetBox, EditBoxFontSize);
	}

	{
		static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
		int Value = g_Config.m_TcTinyTees ? (g_Config.m_TcTinyTeesOthers ? 2 : 1) : 0;
		if(DoLine_RadioMenu(Column, TCLocalize("Smaller tees", QMCLIENT_LOCALIZATION_CONTEXT),
			   s_vButtonContainers,
			   {TCLocalize("None", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Self", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("All", QMCLIENT_LOCALIZATION_CONTEXT)},
			   {0, 1, 2},
			   Value))
		{
			g_Config.m_TcTinyTees = Value > 0 ? 1 : 0;
			g_Config.m_TcTinyTeesOthers = Value > 1 ? 1 : 0;
		}
		Column.HSplitTop(LineSize, &TinyTeeConfig, &Column);
		if(g_Config.m_TcTinyTees > 0)
			Ui()->DoScrollbarOption(&g_Config.m_TcTinyTeeSize, &g_Config.m_TcTinyTeeSize, &TinyTeeConfig, TCLocalize("Tiny Tee Size"), 85, 115);
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTee, TCLocalize("Enable Jelly Tee", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmJellyTee, &Column, LineSize);
	if(g_Config.m_QmJellyTee)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTeeOthers, TCLocalize("Jelly others", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmJellyTeeOthers, &Column, LineSize);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_QmJellyTeeStrength, &g_Config.m_QmJellyTeeStrength, &Button, TCLocalize("Jelly strength", QMCLIENT_LOCALIZATION_CONTEXT), 0, 1000);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_QmJellyTeeDuration, &g_Config.m_QmJellyTeeDuration, &Button, TCLocalize("Jelly duration", QMCLIENT_LOCALIZATION_CONTEXT), 1, 500);
	}

	{
		static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
		int Value = g_Config.m_TcFakeCtfFlags;
		if(DoLine_RadioMenu(Column, TCLocalize("Fake CTF flags"),
			   s_vButtonContainers,
			   {Localize("None"), Localize("Red"), Localize("Blue")},
			   {0, 1, 2},
			   Value))
		{
			g_Config.m_TcFakeCtfFlags = Value;
		}
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMovingTilesEntities, TCLocalize("Show moving tiles in entities"), &g_Config.m_TcMovingTilesEntities, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Input ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Input"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInput, TCLocalize("Fast input (reduce visual latency)", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcFastInput, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	CUIRect ModeLabel, ModeButtons, ModeButton;
	Button.VSplitLeft(150.0f, &ModeLabel, &ModeButtons);
	Ui()->DoLabel(&ModeLabel, TCLocalize("Mode", QMCLIENT_LOCALIZATION_CONTEXT), FontSize, TEXTALIGN_ML);
	static CButtonContainer s_FastInputModeFast, s_FastInputModeDeltaInput, s_FastInputModeGammaInput;
	const int OldMode = g_Config.m_BcFastInputMode;
	CUIRect Left, RightRest, Middle, Right;
	ModeButtons.VSplitLeft((ModeButtons.w - 4.0f) / 3.0f, &Left, &RightRest);
	RightRest.VSplitLeft(2.0f, nullptr, &RightRest);
	RightRest.VSplitLeft((RightRest.w - 2.0f) / 2.0f, &Middle, &Right);
	Right.VSplitLeft(2.0f, nullptr, &Right);
	Left.HMargin(2.0f, &Left);
	Middle.HMargin(2.0f, &Middle);
	Right.HMargin(2.0f, &Right);

	if(DoButtonLineSize_Menu(&s_FastInputModeFast, Localize("Fast input"), g_Config.m_BcFastInputMode == 0, &Left, LineSize, false, 0, IGraphics::CORNER_L, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		g_Config.m_BcFastInputMode = 0;
	if(DoButtonLineSize_Menu(&s_FastInputModeDeltaInput, Localize("Delta input"), g_Config.m_BcFastInputMode == 1, &Middle, LineSize, false, 0, IGraphics::CORNER_NONE, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		g_Config.m_BcFastInputMode = 1;
	if(DoButtonLineSize_Menu(&s_FastInputModeGammaInput, Localize("Gamma input"), g_Config.m_BcFastInputMode == 2, &Right, LineSize, false, 0, IGraphics::CORNER_R, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		g_Config.m_BcFastInputMode = 2;

	if(g_Config.m_BcFastInputMode != OldMode)
	{
		if(g_Config.m_BcFastInputMode == 1 && g_Config.m_BcFastInputDeltaInput <= 0)
		{
			if(OldMode == 2 && g_Config.m_BcFastInputGammaInput > 0)
				g_Config.m_BcFastInputDeltaInput = BcFastInputGammaUiToEffectiveAmount(g_Config.m_BcFastInputGammaInput);
			else if(g_Config.m_TcFastInputAmount > 0)
				g_Config.m_BcFastInputDeltaInput = std::clamp(g_Config.m_TcFastInputAmount * 5, 0, 500);
		}
		else if(g_Config.m_BcFastInputMode == 2 && g_Config.m_BcFastInputGammaInput <= 0)
		{
			if(OldMode == 1 && g_Config.m_BcFastInputDeltaInput > 0)
				g_Config.m_BcFastInputGammaInput = BcFastInputGammaEffectiveToUiAmount(g_Config.m_BcFastInputDeltaInput);
			else if(g_Config.m_TcFastInputAmount > 0)
				g_Config.m_BcFastInputGammaInput = BcFastInputGammaEffectiveToUiAmount(g_Config.m_TcFastInputAmount * 5);
		}
		else if(g_Config.m_BcFastInputMode == 0 && g_Config.m_TcFastInputAmount <= 0)
		{
			const int SourceAmount = OldMode == 2 ? BcFastInputGammaUiToEffectiveAmount(g_Config.m_BcFastInputGammaInput) : g_Config.m_BcFastInputDeltaInput;
			if(SourceAmount > 0)
				g_Config.m_TcFastInputAmount = std::clamp((SourceAmount + 2) / 5, 0, 40);
		}
	}

	Column.HSplitTop(LineSize, &Button, &Column);
	if(g_Config.m_BcFastInputMode == 0)
	{
		DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, TCLocalize("Amount"), 1, 100, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	}
	else
	{
		const bool GammaMode = g_Config.m_BcFastInputMode == 2;
		const int Min = 0;
		const int Max = GammaMode ? BC_FAST_INPUT_GAMMA_UI_MAX : 500;
		int *pAmountValue = GammaMode ? &g_Config.m_BcFastInputGammaInput : &g_Config.m_BcFastInputDeltaInput;
		int Value = std::clamp(*pAmountValue, Min, Max);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s: %.2f%s", GammaMode ? Localize("Gamma amount") : TCLocalize("Amount"), Value / 100.0f, GammaMode ? "M" : "A");

		CUIRect AmountLabel, ScrollBar;
		Button.VSplitMid(&AmountLabel, &ScrollBar, minimum(10.0f, Button.w * 0.05f));
		Ui()->DoLabel(&AmountLabel, aBuf, FontSize, TEXTALIGN_ML);

		const float Rel = (Value - Min) / (float)(Max - Min);
		const float NewRel = Ui()->DoScrollbarH(pAmountValue, &ScrollBar, Rel);
		Value = (int)(Min + NewRel * (Max - Min) + 0.5f);
		*pAmountValue = std::clamp(Value, Min, Max);
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	if(g_Config.m_TcFastInput)
	{
		if(g_Config.m_BcFastInputMode == 0)
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, Localize("Fast input others"), &g_Config.m_TcFastInputOthers, &Column, LineSize);
		else if(g_Config.m_BcFastInputMode == 1)
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcDeltaInputOthers, Localize("Delta input others"), &g_Config.m_BcDeltaInputOthers, &Column, LineSize);
		else
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcGammaInputOthers, Localize("Gamma input others"), &g_Config.m_BcGammaInputOthers, &Column, LineSize);
	}
	else
		Column.HSplitTop(LineSize, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_BcFastInputAutoMargin, Localize("Auto margin"), &g_Config.m_BcFastInputAutoMargin, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, TCLocalize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Column, LineSize);
	// A little extra spacing because these are multi line
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Anti Latency Tools ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Anti Latency Tools"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, TCLocalize("Prediction Margin"), 10, 75, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRemoveAnti, TCLocalize("Remove prediction & antiping in freeze"), &g_Config.m_TcRemoveAnti, &Column, LineSize);
	if(g_Config.m_TcRemoveAnti)
	{
		if(g_Config.m_TcUnfreezeLagDelayTicks < g_Config.m_TcUnfreezeLagTicks)
			g_Config.m_TcUnfreezeLagDelayTicks = g_Config.m_TcUnfreezeLagTicks;
		Column.HSplitTop(LineSize, &Button, &Column);
		DoSliderWithScaledValue(&g_Config.m_TcUnfreezeLagTicks, &g_Config.m_TcUnfreezeLagTicks, &Button, TCLocalize("Amount"), 100, 300, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
		Column.HSplitTop(LineSize, &Button, &Column);
		DoSliderWithScaledValue(&g_Config.m_TcUnfreezeLagDelayTicks, &g_Config.m_TcUnfreezeLagDelayTicks, &Button, TCLocalize("Delay"), 100, 3000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	}
	else
		Column.HSplitTop(LineSize * 2, nullptr, &Column);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcUnpredOthersInFreeze, TCLocalize("Dont predict other players if you are frozen"), &g_Config.m_TcUnpredOthersInFreeze, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPredMarginInFreeze, TCLocalize("Adjust your prediction margin while frozen"), &g_Config.m_TcPredMarginInFreeze, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	if(g_Config.m_TcPredMarginInFreeze)
		Ui()->DoScrollbarOption(&g_Config.m_TcPredMarginInFreezeAmount, &g_Config.m_TcPredMarginInFreezeAmount, &Button, TCLocalize("Frozen Margin"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "ms");
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Improved Anti Ping ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Anti Ping Smoothing"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingImproved, TCLocalize("Use new smoothing algorithm"), &g_Config.m_TcAntiPingImproved, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingStableDirection, TCLocalize("Optimistic prediction along stable direction"), &g_Config.m_TcAntiPingStableDirection, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingNegativeBuffer, TCLocalize("Negative stability buffer (for Gores)"), &g_Config.m_TcAntiPingNegativeBuffer, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcAntiPingUncertaintyScale, &g_Config.m_TcAntiPingUncertaintyScale, &Button, TCLocalize("Uncertainty duration"), 50, 400, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Execute on join ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);

	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Auto execute", QMCLIENT_LOCALIZATION_CONTEXT), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, TCLocalize("Execute before connecting", QMCLIENT_LOCALIZATION_CONTEXT), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_TcExecuteOnConnect, sizeof(g_Config.m_TcExecuteOnConnect));
		Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, TCLocalize("Execute on connect", QMCLIENT_LOCALIZATION_CONTEXT), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_TcExecuteOnJoin, sizeof(g_Config.m_TcExecuteOnJoin));
		Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
	}

	Column.HSplitTop(LineSize, &Button, &Column);
	DoSliderWithScaledValue(&g_Config.m_TcExecuteOnJoinDelay, &g_Config.m_TcExecuteOnJoinDelay, &Button, TCLocalize("Delay"), 140, 2000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Voting ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Voting", QMCLIENT_LOCALIZATION_CONTEXT), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoVoteWhenFar, TCLocalize("Automatically vote no on map changes", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcAutoVoteWhenFar, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcAutoVoteWhenFarTime, &g_Config.m_TcAutoVoteWhenFarTime, &Button, TCLocalize("Minimum time", QMCLIENT_LOCALIZATION_CONTEXT), 1, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, TCLocalize(" min", QMCLIENT_LOCALIZATION_CONTEXT));

	CUIRect VoteMessage;
	Column.HSplitTop(LineSize + MarginExtraSmall, &VoteMessage, &Column);
	VoteMessage.HSplitTop(MarginExtraSmall, nullptr, &VoteMessage);
	VoteMessage.VSplitMid(&Label, &VoteMessage);
	Ui()->DoLabel(&Label, TCLocalize("Message to send in chat:", QMCLIENT_LOCALIZATION_CONTEXT), FontSize, TEXTALIGN_ML);
	static CLineInput s_VoteMessage(g_Config.m_TcAutoVoteWhenFarMessage, sizeof(g_Config.m_TcAutoVoteWhenFarMessage));
	s_VoteMessage.SetEmptyText(TCLocalize("Leave empty to disable", QMCLIENT_LOCALIZATION_CONTEXT));
	Ui()->DoEditBox(&s_VoteMessage, &VoteMessage, EditBoxFontSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** 自动回复 ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Auto reply", QMCLIENT_LOCALIZATION_CONTEXT), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMuted, TCLocalize("Automatically reply to muted players", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcAutoReplyMuted, &Column, LineSize);
	CUIRect MutedReply;
	Column.HSplitTop(LineSize + MarginExtraSmall, &MutedReply, &Column);
	if(g_Config.m_TcAutoReplyMuted)
	{
		MutedReply.HSplitTop(MarginExtraSmall, nullptr, &MutedReply);
		static CLineInput s_MutedReply(g_Config.m_TcAutoReplyMutedMessage, sizeof(g_Config.m_TcAutoReplyMutedMessage));
		s_MutedReply.SetEmptyText(TCLocalize("I muted you", QMCLIENT_LOCALIZATION_CONTEXT));
		Ui()->DoEditBox(&s_MutedReply, &MutedReply, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMinimized, TCLocalize("Automatically reply while the window is unfocused", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcAutoReplyMinimized, &Column, LineSize);
	CUIRect MinimizedReply;
	Column.HSplitTop(LineSize + MarginExtraSmall, &MinimizedReply, &Column);
	if(g_Config.m_TcAutoReplyMinimized)
	{
		MinimizedReply.HSplitTop(MarginExtraSmall, nullptr, &MinimizedReply);
		static CLineInput s_MinimizedReply(g_Config.m_TcAutoReplyMinimizedMessage, sizeof(g_Config.m_TcAutoReplyMinimizedMessage));
		s_MinimizedReply.SetEmptyText(TCLocalize("I am away from the game window", QMCLIENT_LOCALIZATION_CONTEXT));
		Ui()->DoEditBox(&s_MinimizedReply, &MinimizedReply, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Player Indicator ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Player indicator", QMCLIENT_LOCALIZATION_CONTEXT), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPlayerIndicator, TCLocalize("Show any enabled Indicators"), &g_Config.m_TcPlayerIndicator, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorHideVisible, TCLocalize("Hide indicator for tees on your screen"), &g_Config.m_TcIndicatorHideVisible, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPlayerIndicatorFreeze, TCLocalize("Show only freeze Players"), &g_Config.m_TcPlayerIndicatorFreeze, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorTeamOnly, TCLocalize("Only show after joining a team"), &g_Config.m_TcIndicatorTeamOnly, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorTees, TCLocalize("Render tiny tees instead of circles"), &g_Config.m_TcIndicatorTees, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicator, TCLocalize("Use warlist groups for indicator"), &g_Config.m_TcWarListIndicator, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorRadius, &g_Config.m_TcIndicatorRadius, &Button, TCLocalize("Indicator size"), 1, 16);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOpacity, &g_Config.m_TcIndicatorOpacity, &Button, TCLocalize("Indicator opacity"), 0, 100);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorVariableDistance, TCLocalize("Change indicator offset based on distance to other tees"), &g_Config.m_TcIndicatorVariableDistance, &Column, LineSize);
	if(g_Config.m_TcIndicatorVariableDistance)
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffset, &g_Config.m_TcIndicatorOffset, &Button, TCLocalize("Indicator min offset"), 16, 200);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffsetMax, &g_Config.m_TcIndicatorOffsetMax, &Button, TCLocalize("Indicator max offset"), 16, 200);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorMaxDistance, &g_Config.m_TcIndicatorMaxDistance, &Button, TCLocalize("Indicator max distance"), 500, 7000);
	}
	else
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffset, &g_Config.m_TcIndicatorOffset, &Button, TCLocalize("Indicator offset"), 16, 200);
		Column.HSplitTop(LineSize * 2, nullptr, &Column);
	}
	if(g_Config.m_TcWarListIndicator)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorColors, TCLocalize("Use warlist colors instead of regular colors"), &g_Config.m_TcWarListIndicatorColors, &Column, LineSize);
		char aBuf[128];
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorAll, TCLocalize("Show all warlist groups"), &g_Config.m_TcWarListIndicatorAll, &Column, LineSize);
		str_format(aBuf, sizeof(aBuf), TCLocalize("Show %s group", QMCLIENT_LOCALIZATION_CONTEXT), GameClient()->m_WarList.m_WarTypes.at(1)->m_aWarName);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorEnemy, aBuf, &g_Config.m_TcWarListIndicatorEnemy, &Column, LineSize);
		str_format(aBuf, sizeof(aBuf), TCLocalize("Show %s group", QMCLIENT_LOCALIZATION_CONTEXT), GameClient()->m_WarList.m_WarTypes.at(2)->m_aWarName);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorTeam, aBuf, &g_Config.m_TcWarListIndicatorTeam, &Column, LineSize);
	}
	if(!g_Config.m_TcWarListIndicatorColors || !g_Config.m_TcWarListIndicator)
	{
		static CButtonContainer s_IndicatorAliveColorId, s_IndicatorDeadColorId, s_IndicatorSavedColorId;
		DoLine_ColorPicker(&s_IndicatorAliveColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Indicator alive color"), &g_Config.m_TcIndicatorAlive, ColorRGBA(0.0f, 0.0f, 0.0f), false);
		DoLine_ColorPicker(&s_IndicatorDeadColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Indicator in freeze color"), &g_Config.m_TcIndicatorFreeze, ColorRGBA(0.0f, 0.0f, 0.0f), false);
		DoLine_ColorPicker(&s_IndicatorSavedColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Indicator safe color"), &g_Config.m_TcIndicatorSaved, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	}
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** 宠物 ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Pet"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPetShow, TCLocalize("Show the pet"), &g_Config.m_TcPetShow, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcPetSize, &g_Config.m_TcPetSize, &Button, TCLocalize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcPetAlpha, &g_Config.m_TcPetAlpha, &Button, TCLocalize("Pet alpha"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize + MarginExtraSmall, &Button, &Column);
	Button.VSplitMid(&Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Pet Skin:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_PetSkin(g_Config.m_TcPetSkin, sizeof(g_Config.m_TcPetSkin));
	Ui()->DoEditBox(&s_PetSkin, &Button, EditBoxFontSize);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** RightView ***** //
	LeftView = Column;
	Column = RightView;

	// ***** HUD ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("HUD"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniVoteHud, TCLocalize("Show compact vote HUD", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcMiniVoteHud, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniDebug, TCLocalize("Show position and angle (mini debug)", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcMiniDebug, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderCursorSpec, TCLocalize("Show the cursor while free spectating", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcRenderCursorSpec, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	if(g_Config.m_TcRenderCursorSpec)
	{
		Ui()->DoScrollbarOption(&g_Config.m_TcRenderCursorSpecAlpha, &g_Config.m_TcRenderCursorSpecAlpha, &Button, TCLocalize("Free spectate cursor opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100);
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNotifyWhenLast, TCLocalize("Notify when only one tee is still alive:", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcNotifyWhenLast, &Column, LineSize);
	CUIRect NotificationConfig;
	Column.HSplitTop(LineSize + MarginSmall, &NotificationConfig, &Column);
	if(g_Config.m_TcNotifyWhenLast)
	{
		NotificationConfig.VSplitMid(&Button, &NotificationConfig);
		static CLineInput s_LastInput(g_Config.m_TcNotifyWhenLastText, sizeof(g_Config.m_TcNotifyWhenLastText));
		s_LastInput.SetEmptyText(TCLocalize("You're the last one!", QMCLIENT_LOCALIZATION_CONTEXT));
		Button.HSplitTop(MarginSmall, nullptr, &Button);
		Ui()->DoEditBox(&s_LastInput, &Button, EditBoxFontSize);
		static CButtonContainer s_ClientNotifyWhenLastColor;
		DoLine_ColorPicker(&s_ClientNotifyWhenLastColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &NotificationConfig, "", &g_Config.m_TcNotifyWhenLastColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastX, &g_Config.m_TcNotifyWhenLastX, &Button, TCLocalize("Horizontal position", QMCLIENT_LOCALIZATION_CONTEXT), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastY, &g_Config.m_TcNotifyWhenLastY, &Button, TCLocalize("Vertical position", QMCLIENT_LOCALIZATION_CONTEXT), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastSize, &g_Config.m_TcNotifyWhenLastSize, &Button, TCLocalize("Font size", QMCLIENT_LOCALIZATION_CONTEXT), 1, 50);
	}
	else
	{
		Column.HSplitTop(LineSize * 3.0f, nullptr, &Column);
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowCenter, TCLocalize("Show the screen center line", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcShowCenter, &Column, LineSize);
	Column.HSplitTop(LineSize + MarginSmall, &Button, &Column);
	if(g_Config.m_TcShowCenter)
	{
		static CButtonContainer s_ShowCenterLineColor;
		DoLine_ColorPicker(&s_ShowCenterLineColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Button, TCLocalize("Screen center line color", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcShowCenterColor, CConfig::ms_TcShowCenterColor, false, nullptr, true);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcShowCenterWidth, &g_Config.m_TcShowCenterWidth, &Button, TCLocalize("Screen center line width", QMCLIENT_LOCALIZATION_CONTEXT), 0, 20);
	}
	else
	{
		Column.HSplitTop(LineSize, nullptr, &Column);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Frozen Tee Display ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Tee status bar", QMCLIENT_LOCALIZATION_CONTEXT), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHud, TCLocalize("Show tee status bar", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcShowFrozenHud, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHudSkins, TCLocalize("Use custom skins instead of the ninja tee", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcShowFrozenHudSkins, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFrozenHudTeamOnly, TCLocalize("Only show after joining a team", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcFrozenHudTeamOnly, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcFrozenMaxRows, &g_Config.m_TcFrozenMaxRows, &Button, TCLocalize("Maximum rows", QMCLIENT_LOCALIZATION_CONTEXT), 1, 6);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcFrozenHudTeeSize, &g_Config.m_TcFrozenHudTeeSize, &Button, TCLocalize("Tee size", QMCLIENT_LOCALIZATION_CONTEXT), 8, 27);

	{
		CUIRect CheckBoxRect, CheckBoxRect2;
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect2, &Column);
		if(DoButton_CheckBox(&g_Config.m_TcShowFrozenText, TCLocalize("Show the number of tees still alive", QMCLIENT_LOCALIZATION_CONTEXT), g_Config.m_TcShowFrozenText >= 1, &CheckBoxRect))
			g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText >= 1 ? 0 : 1;

		if(g_Config.m_TcShowFrozenText)
		{
			static int s_CountFrozenText = 0;
			if(DoButton_CheckBox(&s_CountFrozenText, TCLocalize("Show the number of frozen tees", QMCLIENT_LOCALIZATION_CONTEXT), g_Config.m_TcShowFrozenText == 2, &CheckBoxRect2))
				g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText != 2 ? 2 : 1;
		}
	}
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Tile Outlines ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Tile outlines", QMCLIENT_LOCALIZATION_CONTEXT), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutline, TCLocalize("Show all enabled outlines", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcOutline, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineEntities, TCLocalize("Only show outlines in the entities layer", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcOutlineEntities, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcOutlineAlpha, &g_Config.m_TcOutlineAlpha, &Button, TCLocalize("Outline opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcOutlineSolidAlpha, &g_Config.m_TcOutlineSolidAlpha, &Button, TCLocalize("Solid tile outline opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	auto DoOutlineType = [&](CButtonContainer &ButtonContainer, const char *pName, int &Enable, int &Width, unsigned int &Color, const unsigned int &ColorDefault) {
		// Checkbox & Color
		DoLine_ColorPicker(&ButtonContainer, ColorPickerLineSize, ColorPickerLabelSize, 0, &Column, pName, &Color, ColorDefault, true, &Enable, true);
		// Width
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&Width, &Width, &Button, TCLocalize("Width", "Outlines"), 1, 16);
		//
		Column.HSplitTop(ColorPickerLineSpacing, nullptr, &Column);
	};
	Column.HSplitTop(ColorPickerLineSpacing, nullptr, &Column);
	static CButtonContainer s_aOutlineButtonContainers[5];
	static CButtonContainer s_OutlineDeepFreezeColorId;
	static CButtonContainer s_OutlineDeepUnfreezeColorId;
	DoOutlineType(s_aOutlineButtonContainers[0], TCLocalize("Solid", QMCLIENT_LOCALIZATION_CONTEXT), g_Config.m_TcOutlineSolid, g_Config.m_TcOutlineWidthSolid, g_Config.m_TcOutlineColorSolid, CConfig::ms_TcOutlineColorSolid);
	DoOutlineType(s_aOutlineButtonContainers[1], TCLocalize("Freeze", QMCLIENT_LOCALIZATION_CONTEXT), g_Config.m_TcOutlineFreeze, g_Config.m_TcOutlineWidthFreeze, g_Config.m_TcOutlineColorFreeze, CConfig::ms_TcOutlineColorFreeze);
	DoLine_ColorPicker(&s_OutlineDeepFreezeColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Deep freeze color", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcOutlineColorDeepFreeze, CConfig::ms_TcOutlineColorDeepFreeze, false, nullptr, true);
	DoOutlineType(s_aOutlineButtonContainers[2], TCLocalize("Unfreeze", QMCLIENT_LOCALIZATION_CONTEXT), g_Config.m_TcOutlineUnfreeze, g_Config.m_TcOutlineWidthUnfreeze, g_Config.m_TcOutlineColorUnfreeze, CConfig::ms_TcOutlineColorUnfreeze);
	DoLine_ColorPicker(&s_OutlineDeepUnfreezeColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Deep unfreeze color", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcOutlineColorDeepUnfreeze, CConfig::ms_TcOutlineColorDeepUnfreeze, false, nullptr, true);
	DoOutlineType(s_aOutlineButtonContainers[3], TCLocalize("Kill", QMCLIENT_LOCALIZATION_CONTEXT), g_Config.m_TcOutlineKill, g_Config.m_TcOutlineWidthKill, g_Config.m_TcOutlineColorKill, CConfig::ms_TcOutlineColorKill);
	DoOutlineType(s_aOutlineButtonContainers[4], TCLocalize("Tele", QMCLIENT_LOCALIZATION_CONTEXT), g_Config.m_TcOutlineTele, g_Config.m_TcOutlineWidthTele, g_Config.m_TcOutlineColorTele, CConfig::ms_TcOutlineColorTele);
	Column.h -= ColorPickerLineSpacing;

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Ghost Tools ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Ghost tools", QMCLIENT_LOCALIZATION_CONTEXT), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowOthersGhosts, TCLocalize("Show unpredicted ghosts for other players", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcShowOthersGhosts, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcSwapGhosts, TCLocalize("Swap ghosts with regular players", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcSwapGhosts, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcPredGhostsAlpha, &g_Config.m_TcPredGhostsAlpha, &Button, TCLocalize("Predicted ghost opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcUnpredGhostsAlpha, &g_Config.m_TcUnpredGhostsAlpha, &Button, TCLocalize("Unpredicted ghost opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcHideFrozenGhosts, TCLocalize("Hide ghosts of frozen players", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcHideFrozenGhosts, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderGhostAsCircle, TCLocalize("Render ghosts as circles", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcRenderGhostAsCircle, &Column, LineSize);

	static CButtonContainer s_ReaderButtonGhost, s_ClearButtonGhost;
	DoLine_KeyReader(Column, s_ReaderButtonGhost, s_ClearButtonGhost, TCLocalize("Toggle ghost key", QMCLIENT_LOCALIZATION_CONTEXT), "toggle tc_show_others_ghosts 0 1");

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** 彩虹! ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Rainbow"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowTees, TCLocalize("Rainbow Tees"), &g_Config.m_TcRainbowTees, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowWeapon, TCLocalize("Rainbow weapons"), &g_Config.m_TcRainbowWeapon, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowHook, TCLocalize("Rainbow hook"), &g_Config.m_TcRainbowHook, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowOthers, TCLocalize("Rainbow others"), &g_Config.m_TcRainbowOthers, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	static std::vector<const char *> s_RainbowDropDownNames;
	s_RainbowDropDownNames = {TCLocalize("Rainbow"), TCLocalize("Pulse"), TCLocalize("Black"), TCLocalize("Random")};
	static CUi::SDropDownState s_RainbowDropDownState;
	static CScrollRegion s_RainbowDropDownScrollRegion;
	s_RainbowDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_RainbowDropDownScrollRegion;
	int RainbowSelectedOld = g_Config.m_TcRainbowMode - 1;
	CUIRect RainbowDropDownRect;
	Column.HSplitTop(LineSize, &RainbowDropDownRect, &Column);
	const int RainbowSelectedNew = Ui()->DoDropDown(&RainbowDropDownRect, RainbowSelectedOld, s_RainbowDropDownNames.data(), s_RainbowDropDownNames.size(), s_RainbowDropDownState);
	if(RainbowSelectedOld != RainbowSelectedNew)
	{
		g_Config.m_TcRainbowMode = RainbowSelectedNew + 1;
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcRainbowSpeed, &g_Config.m_TcRainbowSpeed, &Button, TCLocalize("Rainbow speed"), 0, 5000, &CUi::ms_LogarithmicScrollbarScale, 0, "%");
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	// ***** Tee Trails ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Tee Trails"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrail, TCLocalize("Enable tee trails"), &g_Config.m_TcTeeTrail, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailOthers, TCLocalize("Show other tees' trails"), &g_Config.m_TcTeeTrailOthers, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailFade, TCLocalize("Fade trail alpha"), &g_Config.m_TcTeeTrailFade, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailTaper, TCLocalize("Taper trail width"), &g_Config.m_TcTeeTrailTaper, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	std::vector<const char *> vTrailDropDownNames;
	vTrailDropDownNames = {TCLocalize("Solid"), TCLocalize("Tee"), TCLocalize("Rainbow"), TCLocalize("Speed")};
	static CUi::SDropDownState s_TrailDropDownState;
	static CScrollRegion s_TrailDropDownScrollRegion;
	s_TrailDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TrailDropDownScrollRegion;
	int TrailSelectedOld = g_Config.m_TcTeeTrailColorMode - 1;
	CUIRect TrailDropDownRect;
	Column.HSplitTop(LineSize, &TrailDropDownRect, &Column);
	const int TrailSelectedNew = Ui()->DoDropDown(&TrailDropDownRect, TrailSelectedOld, vTrailDropDownNames.data(), vTrailDropDownNames.size(), s_TrailDropDownState);
	if(TrailSelectedOld != TrailSelectedNew)
	{
		g_Config.m_TcTeeTrailColorMode = TrailSelectedNew + 1;
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_TeeTrailColor;
	if(g_Config.m_TcTeeTrailColorMode == CTrails::COLORMODE_SOLID)
		DoLine_ColorPicker(&s_TeeTrailColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Tee trail color"), &g_Config.m_TcTeeTrailColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	else
		Column.HSplitTop(ColorPickerLineSize + ColorPickerLineSpacing, &Button, &Column);

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailWidth, &g_Config.m_TcTeeTrailWidth, &Button, TCLocalize("Trail width"), 0, 20);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailLength, &g_Config.m_TcTeeTrailLength, &Button, TCLocalize("Trail length"), 0, 200);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailAlpha, &g_Config.m_TcTeeTrailAlpha, &Button, TCLocalize("Trail alpha"), 0, 100);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	// ***** 背景绘画 ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Background Draw"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_BgDrawColor;
	DoLine_ColorPicker(&s_BgDrawColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Color"), &g_Config.m_TcBgDrawColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);

	Column.HSplitTop(LineSize * 2.0f, &Button, &Column);
	if(g_Config.m_TcBgDrawFadeTime == 0)
		Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, TCLocalize("Stroke fade time", QMCLIENT_LOCALIZATION_CONTEXT), 0, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, TCLocalize(" seconds (never)"));
	else
		Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, TCLocalize("Stroke fade time", QMCLIENT_LOCALIZATION_CONTEXT), 0, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, TCLocalize(" seconds"));

	Column.HSplitTop(LineSize * 2.0f, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawWidth, &g_Config.m_TcBgDrawWidth, &Button, TCLocalize("Width"), 1, 50, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);

	static CButtonContainer s_ReaderButtonDraw, s_ClearButtonDraw;
	DoLine_KeyReader(Column, s_ReaderButtonDraw, s_ClearButtonDraw, TCLocalize("Draw where mouse is"), "+bg_draw");

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	Column.HSplitTop(MarginSmall, nullptr, &Column);



	// ***** 终点恰分改名 ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Finish Name"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcChangeNameNearFinish, TCLocalize("Attempt to change your name when near finish"), &g_Config.m_TcChangeNameNearFinish, &Column, LineSize);
	Column.HSplitTop(LineSize + MarginExtraSmall, &Button, &Column);
	Button.VSplitMid(&Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Finish Name:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_FinishName(g_Config.m_TcFinishName, sizeof(g_Config.m_TcFinishName));
	Ui()->DoEditBox(&s_FinishName, &Button, EditBoxFontSize);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;



	// ***** END OF PAGE 1 SETTINGS ***** //
	RightView = Column;

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

	const float Radius = minimum(RightView.w, RightView.h) / 2.0f;
	vec2 Center = RightView.Center();
	// Draw Circle
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

		const CBindWheel::CBind Bind = GameClient()->m_BindWheel.m_vBinds[i];
		const float Angle = Theta * i;

		const vec2 Pos = direction(Angle) * (Radius * 0.75f) + Center;
		const CUIRect Rect = CUIRect{Pos.x - 50.0f, Pos.y - 50.0f, 100.0f, 100.0f};
		Ui()->DoLabel(&Rect, Bind.m_aName, SegmentFontSize, TEXTALIGN_MC);
	}

	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Button.VSplitLeft(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Name:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_NameInput;
	s_NameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
	s_NameInput.SetEmptyText(TCLocalize("Name"));
	Ui()->DoEditBox(&s_NameInput, &Button, EditBoxFontSize);

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Button.VSplitLeft(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Command:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_BindInput;
	s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
	s_BindInput.SetEmptyText(TCLocalize("Command"));
	Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize);

	static CButtonContainer s_AddButton, s_RemoveButton, s_OverrideButton;

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	if(DoButton_Menu(&s_OverrideButton, TCLocalize("Override Selected"), 0, &Button) && s_SelectedBindIndex >= 0 && s_SelectedBindIndex < static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()))
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
	if(DoButton_Menu(&s_AddButton, TCLocalize("Add Bind"), 0, &ButtonAdd))
	{
		CBindWheel::CBind TempBind;
		if(str_length(s_aBindName) == 0)
			str_copy(TempBind.m_aName, "*");
		else
			str_copy(TempBind.m_aName, s_aBindName);

		GameClient()->m_BindWheel.AddBind(TempBind.m_aName, s_aBindCommand);
		s_SelectedBindIndex = static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()) - 1;
	}
	if(DoButton_Menu(&s_RemoveButton, TCLocalize("Remove Bind"), 0, &ButtonRemove) && s_SelectedBindIndex >= 0)
	{
		GameClient()->m_BindWheel.RemoveBind(s_SelectedBindIndex);
		s_SelectedBindIndex = -1;
	}

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("The command is ran in console not chat"), FontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use left mouse to select"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use right mouse to swap with selected"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use middle mouse select without copy"), FontSize * 0.8f, TEXTALIGN_ML);

	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	static CButtonContainer s_ReaderButtonWheel, s_ClearButtonWheel;
	DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, TCLocalize("Bind Wheel Key"), "+bindwheel");

	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcResetBindWheelMouse, TCLocalize("Reset position of mouse when opening bindwheel"), &g_Config.m_TcResetBindWheelMouse, &Label, LineSize);
}

void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label;

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
		if(DoEditBoxWithLabel(&BindDefault.m_LineInput, &Button, TCLocalize(BindDefault.m_pTitle), BindDefault.m_Bind.m_aName, pName, BINDCHAT_MAX_NAME) && BindDefault.m_LineInput.IsActive())
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
	for(auto &[pTitle, vBindDefaults] : CBindChat::BIND_DEFAULTS)
	{
		float &Size = SizeL > SizeR ? SizeR : SizeL;
		CUIRect &Column = SizeL > SizeR ? RightView : LeftView;
		DoBindchatDefaults(Column, TCLocalize(pTitle), vBindDefaults);
		Size += vBindDefaults.size() * (MarginSmall + LineSize) + HeadlineHeight + HeadlineFontSize + MarginSmall * 2.0f;
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
	{
		Column1.HSplitTop(HeadlineHeight, &Label, &Column1);
		Label.VSplitRight(25.0f, &Label, &Button);
		Ui()->DoLabel(&Label, TCLocalize("War Entries"), HeadlineFontSize, TEXTALIGN_ML);
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

	// ======WAR ENTRY EDITING======
	Column2.HSplitTop(HeadlineHeight, &Label, &Column2);
	Label.VSplitRight(25.0f, &Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Edit Entry"), HeadlineFontSize, TEXTALIGN_ML);
	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(HeadlineFontSize, &Button, &Column2);

	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	static CLineInput s_NameInput;
	s_NameInput.SetBuffer(s_aEntryName, sizeof(s_aEntryName));
	s_NameInput.SetEmptyText(TCLocalize("Name"));
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
	s_ClanInput.SetEmptyText(TCLocalize("Clan"));
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
	if(DoButton_CheckBox_Common(&s_NameRadio, TCLocalize("Name"), s_IsName ? "X" : "", &ButtonL, BUTTONFLAG_LEFT))
	{
		s_IsName = true;
		s_IsClan = false;
	}
	if(DoButton_CheckBox_Common(&s_ClanRadio, TCLocalize("Clan"), s_IsClan ? "X" : "", &ButtonR, BUTTONFLAG_LEFT))
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
	s_ReasonInput.SetEmptyText(TCLocalize("Reason"));
	Ui()->DoEditBox(&s_ReasonInput, &Button, 12.0f);

	static CButtonContainer s_AddButton, s_OverrideButton;

	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(LineSize * 2.0f, &Button, &Column2);
	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);

	if(DoButtonLineSize_Menu(&s_OverrideButton, TCLocalize("Override Entry"), 0, &ButtonL, LineSize) && s_pSelectedEntry)
	{
		if(s_pSelectedEntry && s_pSelectedType && (str_comp(s_aEntryName, "") != 0 || str_comp(s_aEntryClan, "") != 0))
		{
			str_copy(s_pSelectedEntry->m_aName, s_aEntryName);
			str_copy(s_pSelectedEntry->m_aClan, s_aEntryClan);
			str_copy(s_pSelectedEntry->m_aReason, s_aEntryReason);
			s_pSelectedEntry->m_pWarType = s_pSelectedType;
		}
	}
	if(DoButtonLineSize_Menu(&s_AddButton, TCLocalize("Add Entry"), 0, &ButtonR, LineSize))
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
	Ui()->DoLabel(&Label, TCLocalize("Settings"), HeadlineFontSize, TEXTALIGN_ML);
	Column2.HSplitTop(MarginSmall, nullptr, &Column2);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListAllowDuplicates, TCLocalize("Allow Duplicate Entries"), &g_Config.m_TcWarListAllowDuplicates, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarList, TCLocalize("Enable warlist"), &g_Config.m_TcWarList, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListChat, TCLocalize("Colors in chat"), &g_Config.m_TcWarListChat, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListScoreboard, TCLocalize("Colors in scoreboard"), &g_Config.m_TcWarListScoreboard, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListSpectate, TCLocalize("Colors in spectate select"), &g_Config.m_TcWarListSpectate, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListShowClan, TCLocalize("Show clan if war"), &g_Config.m_TcWarListShowClan, &Column2, LineSize);

	// ======WAR TYPE EDITING======

	Column3.HSplitTop(HeadlineHeight, &Label, &Column3);
	Ui()->DoLabel(&Label, TCLocalize("War Groups"), HeadlineFontSize, TEXTALIGN_ML);
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
			TCLocalize("Are you sure that you want to remove '%s' from your war groups?"),
			m_pRemoveWarType->m_aWarName);
		PopupConfirm(TCLocalize("Remove War Group"), aMessage, TCLocalize("Yes"), TCLocalize("No"), &CMenus::PopupConfirmRemoveWarType);
	}

	static CLineInput s_TypeNameInput;
	Column3.HSplitTop(MarginSmall, nullptr, &Column3);
	Column3.HSplitTop(HeadlineFontSize + MarginSmall, &Button, &Column3);
	s_TypeNameInput.SetBuffer(s_aTypeName, sizeof(s_aTypeName));
	s_TypeNameInput.SetEmptyText(TCLocalize("Group name", QMCLIENT_LOCALIZATION_CONTEXT));
	Ui()->DoEditBox(&s_TypeNameInput, &Button, 12.0f);
	static CButtonContainer s_AddGroupButton, s_OverrideGroupButton, s_GroupColorPicker;

	Column3.HSplitTop(MarginSmall, nullptr, &Column3);
	static unsigned int s_ColorValue = 0;
	s_ColorValue = color_cast<ColorHSLA>(s_GroupColor).Pack(false);
	ColorHSLA PickedColor = DoLine_ColorPicker(&s_GroupColorPicker, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column3, TCLocalize("Color"), &s_ColorValue, ColorRGBA(1.0f, 1.0f, 1.0f), true);
	s_GroupColor = color_cast<ColorRGBA>(PickedColor);

	Column3.HSplitTop(LineSize * 2.0f, &Button, &Column3);
	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	bool OverrideDisabled = NewSelectedType == 0;
	if(DoButtonLineSize_Menu(&s_OverrideGroupButton, TCLocalize("Override Group"), 0, &ButtonL, LineSize, OverrideDisabled) && s_pSelectedType)
	{
		if(s_pSelectedType && str_comp(s_aTypeName, "") != 0)
		{
			str_copy(s_pSelectedType->m_aWarName, s_aTypeName);
			s_pSelectedType->m_Color = s_GroupColor;
		}
	}
	bool AddDisabled = str_comp(GameClient()->m_WarList.FindWarType(s_aTypeName)->m_aWarName, "none") != 0 || str_comp(s_aTypeName, "none") == 0;
	if(DoButtonLineSize_Menu(&s_AddGroupButton, TCLocalize("Add Group"), 0, &ButtonR, LineSize, AddDisabled))
	{
		GameClient()->m_WarList.AddWarType(s_aTypeName, s_GroupColor);
	}

	// ======ONLINE PLAYER LIST======

	Column4.HSplitTop(HeadlineHeight, &Label, &Column4);
	Ui()->DoLabel(&Label, TCLocalize("Online Players"), HeadlineFontSize, TEXTALIGN_ML);
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
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitBottom(100.0f, &MainView, &StatusBar);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Status Bar"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBar, TCLocalize("Show status bar"), &g_Config.m_TcStatusBar, &LeftView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBarLabels, TCLocalize("Show labels on status bar items"), &g_Config.m_TcStatusBarLabels, &LeftView, LineSize);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarHeight, &g_Config.m_TcStatusBarHeight, &Button, TCLocalize("Status bar height"), 1, 16);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Local Time"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBar12HourClock, TCLocalize("Use 12 hour clock"), &g_Config.m_TcStatusBar12HourClock, &LeftView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBarLocalTimeSeconds, TCLocalize("Show seconds on clock"), &g_Config.m_TcStatusBarLocalTimeSeconds, &LeftView, LineSize);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Colors"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	static CButtonContainer s_StatusbarColor, s_StatusbarTextColor;

	DoLine_ColorPicker(&s_StatusbarColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, TCLocalize("Status bar color"), &g_Config.m_TcStatusBarColor, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	DoLine_ColorPicker(&s_StatusbarTextColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, TCLocalize("Text color"), &g_Config.m_TcStatusBarTextColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarAlpha, &g_Config.m_TcStatusBarAlpha, &Button, TCLocalize("Status bar alpha"), 0, 100);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarTextAlpha, &g_Config.m_TcStatusBarTextAlpha, &Button, TCLocalize("Text alpha"), 0, 100);

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("Status Bar Codes:"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("a = Angle"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("p = Ping"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("d = Prediction"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("c = Position"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("l = Local Time"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("r = Race Time"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("f = FPS"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("v = Velocity"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("z = Zoom"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("_ or ' ' = Space"), FontSize, TEXTALIGN_ML);
	static int s_SelectedItem = -1;
	static int s_TypeSelectedOld = -1;

	CUIRect StatusScheme, StatusButtons, ItemLabel;
	static CButtonContainer s_ApplyButton, s_AddButton, s_RemoveButton;
	StatusBar.HSplitBottom(LineSize + MarginSmall, &StatusBar, &StatusScheme);
	StatusBar.HSplitTop(LineSize + MarginSmall, &ItemLabel, &StatusBar);
	StatusScheme.HSplitTop(MarginSmall, nullptr, &StatusScheme);

	if(s_TypeSelectedOld >= 0)
		Ui()->DoLabel(&ItemLabel, GameClient()->m_StatusBar.m_StatusItemTypes[s_TypeSelectedOld].m_aDesc, FontSize, TEXTALIGN_ML);

	StatusScheme.VSplitMid(&StatusButtons, &StatusScheme, MarginSmall);
	StatusScheme.VSplitMid(&Label, &StatusScheme, MarginSmall);
	StatusScheme.VSplitMid(&StatusScheme, &Button, MarginSmall);
	if(DoButton_Menu(&s_ApplyButton, TCLocalize("Apply"), 0, &Button))
	{
		GameClient()->m_StatusBar.ApplyStatusBarScheme(g_Config.m_TcStatusBarScheme);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = -1;
	}
	Ui()->DoLabel(&Label, TCLocalize("Status Scheme:"), FontSize, TEXTALIGN_MR);
	static CLineInput s_StatusScheme(g_Config.m_TcStatusBarScheme, sizeof(g_Config.m_TcStatusBarScheme));
	s_StatusScheme.SetEmptyText("");
	Ui()->DoEditBox(&s_StatusScheme, &StatusScheme, EditBoxFontSize);

	static std::vector<const char *> s_DropDownNames = {};
	for(const CStatusItem &StatusItemType : GameClient()->m_StatusBar.m_StatusItemTypes)
		if(s_DropDownNames.size() != GameClient()->m_StatusBar.m_StatusItemTypes.size())
			s_DropDownNames.push_back(StatusItemType.m_aName);

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
	if(DoButton_Menu(&s_AddButton, TCLocalize("Add Item"), 0, &ButtonL) && s_TypeSelectedOld >= 0 && NumItems < 128)
	{
		GameClient()->m_StatusBar.m_StatusBarItems.push_back(&GameClient()->m_StatusBar.m_StatusItemTypes[s_TypeSelectedOld]);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = (int)GameClient()->m_StatusBar.m_StatusBarItems.size() - 1;
	}
	if(DoButton_Menu(&s_RemoveButton, TCLocalize("Remove Item"), 0, &ButtonR) && s_SelectedItem >= 0)
	{
		GameClient()->m_StatusBar.m_StatusBarItems.erase(GameClient()->m_StatusBar.m_StatusBarItems.begin() + s_SelectedItem);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = -1;
	}

	// color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcStatusBarColor)).WithAlpha(0.5f)
	StatusBar.Draw(ColorRGBA(0, 0, 0, 0.5f), IGraphics::CORNER_ALL, 5.0f);
	int ItemCount = GameClient()->m_StatusBar.m_StatusBarItems.size();
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
		if(DoButtonLineSize_Menu(s_pItemButtons[i], StatusItem->m_aDisplayName, 0, &TempItemButton, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, Col))
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
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("TClient Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_GithubButton, s_SupportButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, TCLocalize("Discord"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/fBvhH93Bt6");
	if(DoButtonLineSize_Menu(&s_WebsiteButton, TCLocalize("Website"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://tclient.app/");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

	if(DoButtonLineSize_Menu(&s_GithubButton, TCLocalize("Github"), 0, &ButtonLeft, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://github.com/sjrc6/TaterClient-ddnet");
	if(DoButtonLineSize_Menu(&s_SupportButton, TCLocalize("Support ♥"), 0, &ButtonRight, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://ko-fi.com/Totar");

	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 4.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect TClientConfig, ProfilesFile, WarlistFile, ChatbindsFile;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&TClientConfig, &ProfilesFile, MarginSmall);

	static CButtonContainer s_Config, s_Profiles, s_Warlist, s_Chatbinds;
	if(DoButtonLineSize_Menu(&s_Config, TCLocalize("TClient Settings"), 0, &TClientConfig, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	if(DoButtonLineSize_Menu(&s_Profiles, TCLocalize("Profiles"), 0, &ProfilesFile, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTPROFILES].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&WarlistFile, &ChatbindsFile, MarginSmall);

	if(DoButtonLineSize_Menu(&s_Warlist, TCLocalize("War List"), 0, &WarlistFile, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTWARLIST].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	if(DoButtonLineSize_Menu(&s_Chatbinds, TCLocalize("Chat Binds"), 0, &ChatbindsFile, LineSize, false, 0, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTCHATBINDS].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	// =======RIGHT VIEW========

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("TClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
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

	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	CUIRect LeftSettings, RightSettings;

	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);
	RightView.HSplitTop(LineSize * 3.5f, nullptr, &RightView);

	const char *apTabNames[] = {
		TCLocalize("Settings"),
		TCLocalize("Bind Wheel"),
		TCLocalize("War List"),
		TCLocalize("Chat Binds"),
		TCLocalize("Status Bar"),
		TCLocalize("Info")};
	static int s_aShowTabs[NUMBER_OF_TCLIENT_TABS] = {};
	for(int i = 0; i < NUMBER_OF_TCLIENT_TABS - 1; ++i)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&s_aShowTabs[i], apTabNames[i], &s_aShowTabs[i], i % 2 == 0 ? &LeftSettings : &RightSettings, LineSize);
		SetFlag(g_Config.m_TcTClientSettingsTabs, i, s_aShowTabs[i]);
	}

	// RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	// Ui()->DoLabel(&Label, TCLocalize("Integration"), HeadlineFontSize, TEXTALIGN_ML);
	// RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcDiscordRPC, TCLocalize("Enable Discord Integration"), &g_Config.m_TcDiscordRPC, &RightView, LineSize);
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
				str_format(aBuf, sizeof(aBuf), TCLocalize("Name: %s"), Profile.m_Name);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), TCLocalize("Clan: %s"), Profile.m_Clan);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), TCLocalize("Skin: %s"), Profile.m_SkinName);
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
			Ui()->DoLabel(&Label, TCLocalize("Your profile"), FontSize, TEXTALIGN_ML);
			Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
			Profiles.HSplitTop(50.0f, &Skin, &Profiles);
			RenderProfile(Skin, CurrentProfile, true);

			// After load
			if(pConstSelectedProfile())
			{
				Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
				Profiles.HSplitTop(LineSize, &Label, &Profiles);
				Ui()->DoLabel(&Label, TCLocalize("After Load"), FontSize, TEXTALIGN_ML);
				Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
				Profiles.HSplitTop(50.0f, &Skin, &Profiles);
				RenderProfile(Skin, BuildPreviewProfile(), true);
			}
		}
		Top.VSplitLeft(20.0f, nullptr, &Top);
		Top.VSplitMid(&Settings, &Actions, 20.0f);
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileSkin, TCLocalize("Save/Load Skin"), &g_Config.m_TcProfileSkin, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileColors, TCLocalize("Save/Load Colors"), &g_Config.m_TcProfileColors, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileEmote, TCLocalize("Save/Load Emote"), &g_Config.m_TcProfileEmote, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileName, TCLocalize("Save/Load Name"), &g_Config.m_TcProfileName, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileClan, TCLocalize("Save/Load Clan"), &g_Config.m_TcProfileClan, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileFlag, TCLocalize("Save/Load Flag"), &g_Config.m_TcProfileFlag, &Settings, LineSize);
		}
		{
			Actions.HSplitTop(30.0f, &Button, &Actions);
			static CButtonContainer s_LoadButton;
			if(DoButton_Menu(&s_LoadButton, TCLocalize("Load"), 0, &Button))
				ApplySelectedProfile();
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			Actions.HSplitTop(30.0f, &Button, &Actions);
			static CButtonContainer s_SaveButton;
			if(DoButton_Menu(&s_SaveButton, TCLocalize("Save"), 0, &Button))
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
				if(DoButton_Menu(&s_DeleteButton, TCLocalize("Delete"), 0, &Button))
					DeleteSelectedProfile();
				Actions.HSplitTop(5.0f, nullptr, &Actions);

				Actions.HSplitTop(30.0f, &Button, &Actions);
				static CButtonContainer s_OverrideButton;
				if(DoButton_Menu(&s_OverrideButton, TCLocalize("Override"), 0, &Button))
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
		if(DoButton_CheckBox(&m_Dummy, TCLocalize("Dummy"), m_Dummy, &Button))
			m_Dummy = 1 - m_Dummy;

		Options.VSplitLeft(150.0f, &Button, &Options);
		static int s_CustomColorId = 0;
		if(DoButton_CheckBox(&s_CustomColorId, TCLocalize("Custom colors"), *pCurrentUseCustomColor, &Button))
		{
			*pCurrentUseCustomColor = *pCurrentUseCustomColor ? 0 : 1;
			SetNeedSendInfo();
		}

		Button = Options;
		if(DoButton_CheckBox(&g_Config.m_TcProfileOverwriteClanWithEmpty, TCLocalize("Overwrite clan even if empty"), g_Config.m_TcProfileOverwriteClanWithEmpty, &Button))
			g_Config.m_TcProfileOverwriteClanWithEmpty = 1 - g_Config.m_TcProfileOverwriteClanWithEmpty;
	}
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	{
		CUIRect SelectorRect;
		MainView.HSplitBottom(LineSize + MarginSmall, &MainView, &SelectorRect);
		SelectorRect.HSplitTop(MarginSmall, nullptr, &SelectorRect);

		static CButtonContainer s_ProfilesFile;
		SelectorRect.VSplitLeft(130.0f, &Button, &SelectorRect);
		if(DoButton_Menu(&s_ProfilesFile, TCLocalize("Profiles file"), 0, &Button))
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

	CUIRect ApplyBar, TopBar, ListArea;
	MainView.VSplitRight(5.0f, &MainView, nullptr); // padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	MainView.HSplitTop(LineSize + MarginSmall, &ApplyBar, &MainView);
	MainView.HSplitTop(LineSize + MarginSmall, &TopBar, &ListArea);
	ListArea.HSplitTop(MarginSmall, nullptr, &ListArea);

	static CLineInputBuffered<128> s_SearchInput;

	ChangesCount = s_StagedInts.size() + s_StagedStrs.size() + s_StagedCols.size();
	{
		CUIRect LeftHalf, RightHalf;
		ApplyBar.VSplitMid(&LeftHalf, &RightHalf, 0.0f);
		CUIRect Row = LeftHalf;
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

		CUIRect RightRow = RightHalf;
		RightRow.h = LineSize;
		RightRow.y = ApplyBar.y + (ApplyBar.h - LineSize) / 2.0f;
		const float RightInset = 24.0f;
		RightRow.VSplitLeft(RightInset, nullptr, &RightRow);
		CUIRect TopCol1, TopCol2;
		RightRow.VSplitMid(&TopCol1, &TopCol2, 0.0f);
		if(DoButton_CheckBox(&g_Config.m_TcUiShowTClient, Localize("QmClient"), g_Config.m_TcUiShowTClient, &TopCol1))
			g_Config.m_TcUiShowTClient ^= 1;
		if(DoButton_CheckBox(&g_Config.m_TcUiCompactList, Localize("Compact List"), g_Config.m_TcUiCompactList, &TopCol2))
			g_Config.m_TcUiCompactList ^= 1;
	}

	const float SearchLabelW = 60.0f;
	{
		CUIRect SearchRow = TopBar;
		SearchRow.h = LineSize;
		SearchRow.y = TopBar.y + (TopBar.h - LineSize) / 2.0f;

		CUIRect LeftHalf, RightHalf;
		SearchRow.VSplitMid(&LeftHalf, &RightHalf, 0.0f);

		CUIRect SearchLabel, SearchEdit;
		LeftHalf.VSplitLeft(SearchLabelW, &SearchLabel, &SearchEdit);
		Ui()->DoLabel(&SearchLabel, Localize("Search"), FontSize, TEXTALIGN_ML);
		Ui()->DoClearableEditBox(&s_SearchInput, &SearchEdit, EditBoxFontSize);

		CUIRect RightCol1, RightCol2;
		const float RightInset2 = 24.0f;
		RightHalf.VSplitLeft(RightInset2, nullptr, &RightHalf);
		RightHalf.VSplitMid(&RightCol1, &RightCol2, 0.0f);
		if(DoButton_CheckBox(&g_Config.m_TcUiShowDDNet, Localize("DDNet"), g_Config.m_TcUiShowDDNet, &RightCol1))
			g_Config.m_TcUiShowDDNet ^= 1;
		if(DoButton_CheckBox(&g_Config.m_TcUiOnlyModified, Localize("Only modified"), g_Config.m_TcUiOnlyModified, &RightCol2))
			g_Config.m_TcUiOnlyModified ^= 1;
	}

	const int FlagMask = CFGFLAG_CLIENT;
	static std::vector<const SConfigVariable *> s_vAllClientVars;
	if(s_vAllClientVars.empty())
	{
		auto Collector = [](const SConfigVariable *pVar, void *pUserData) {
			auto *pVec = static_cast<std::vector<const SConfigVariable *> *>(pUserData);
			pVec->push_back(pVar);
		};
		ConfigManager()->PossibleConfigVariables("", FlagMask, Collector, &s_vAllClientVars);
		std::sort(s_vAllClientVars.begin(), s_vAllClientVars.end(), [](const SConfigVariable *a, const SConfigVariable *b) {
			if(a->m_ConfigDomain != b->m_ConfigDomain)
				return a->m_ConfigDomain < b->m_ConfigDomain;
			return str_comp(a->m_pScriptName, b->m_pScriptName) < 0;
		});
	}

	auto DomainEnabled = [&](ConfigDomain Domain) {
		if(Domain == ConfigDomain::DDNET)
			return g_Config.m_TcUiShowDDNet != 0;
		if(Domain == ConfigDomain::TCLIENT)
			return g_Config.m_TcUiShowTClient != 0;
		// only show DDNet and TClient domains
		return false;
	};

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
		if(!DomainEnabled(pVar->m_ConfigDomain))
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

	auto DomainName = [](ConfigDomain D) {
		switch(D)
		{
		case ConfigDomain::DDNET: return "DDNet";
		case ConfigDomain::TCLIENT: return "TClient";
		default: return "Other";
		}
	};

	ConfigDomain CurrentDomain = ConfigDomain::NUM;
	for(const SConfigVariable *pVar : vpFiltered)
	{
		if(pVar->m_ConfigDomain != CurrentDomain)
		{
			CurrentDomain = pVar->m_ConfigDomain;
			CUIRect Header;
			Content.HSplitTop(HeadlineHeight, &Header, &Content);
			if(s_ScrollRegion.AddRect(Header))
				Ui()->DoLabel(&Header, DomainName(CurrentDomain), HeadlineFontSize, TEXTALIGN_ML);
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

void CMenus::RenderSettingsQiMeng(CUIRect MainView)
{
	// ============================================
	// Liquid Glass UI - 稳定布局系统
	// ============================================

	// === 布局常量（统一定义，禁止魔数） ===
	const float ViewWidth = MainView.w;
	// 5:4 等窄屏在设置页实际可用宽度会明显变小，提前切紧凑布局并适当缩放。
	const bool CompactLayout = ViewWidth < 680.0f;
	const float UiScale = std::clamp(ViewWidth / 1000.0f, CompactLayout ? 0.78f : 0.85f, 1.0f);

	const float LG_CardPadding = std::clamp(14.0f * UiScale, 10.0f, 14.0f);          // 卡片内边距
	const float LG_CardSpacing = std::clamp(16.0f * UiScale, 10.0f, 16.0f);          // 卡片间距
	const float LG_CornerRadius = std::clamp(12.0f * UiScale, 8.0f, 12.0f);          // 圆角
	const float LG_CardAlpha = 0.70f;                                                // 卡片透明度
	const float LG_HeadlineSize = std::clamp(14.0f * UiScale, 12.0f, 14.0f);         // 标题字号
	const float LG_BodySize = std::clamp(12.0f * UiScale, 10.0f, 12.0f);             // 正文字号
	const float LG_LineHeight = std::clamp(20.0f * UiScale, 16.0f, 20.0f);           // 统一行高
	const float LG_LineSpacing = std::clamp(5.0f * UiScale, 3.0f, 5.0f);             // 统一行间距
	const float LG_HeadlineMargin = std::clamp(10.0f * UiScale, 7.0f, 10.0f);        // 标题下方间距
	const float LG_TipSize = std::clamp(LG_BodySize * 0.7f, 8.0f, LG_BodySize);
	const float LG_TipHeight = maximum(LG_HeadlineMargin, LG_TipSize + 2.0f);
	const float LG_LabelMaxWidth = maximum(CompactLayout ? 96.0f : 120.0f, ViewWidth * (CompactLayout ? 0.38f : 0.45f));
	const float LG_LabelBaseWidth = CompactLayout ? 148.0f : 170.0f;
	const float LG_LabelMinWidth = CompactLayout ? 96.0f : 120.0f;
	const float LG_LabelWidth = std::clamp(LG_LabelBaseWidth * UiScale, LG_LabelMinWidth, LG_LabelMaxWidth); // 左侧标签列宽度（固定）

	// === 颜色定义 ===
	const ColorRGBA LG_GlassColor(0.08f, 0.09f, 0.12f, LG_CardAlpha);
	const ColorRGBA LG_HighlightColor(1.0f, 1.0f, 1.0f, 0.05f);
	const ColorRGBA LG_ShadowColor(0.0f, 0.0f, 0.0f, 0.12f);
	const ColorRGBA LG_TipTextColor(1.0f, 1.0f, 1.0f, 0.65f);

	CUIRect LeftView, RightView;

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f * UiScale;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = std::clamp(8.0f * UiScale, 6.0f, 8.0f);
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);

	static std::vector<CUIRect> s_GlassCards;
	static vec2 s_PrevScrollOffset(0.0f, 0.0f);

	MainView.y += ScrollOffset.y;

	// 外边距
	const float OuterMargin = CompactLayout ? std::clamp(7.0f * UiScale, 4.0f, 7.0f) : std::clamp(10.0f * UiScale, 6.0f, 10.0f);
	MainView.VSplitRight(OuterMargin, &MainView, nullptr);
	MainView.VSplitLeft(OuterMargin, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, LG_CardSpacing);

	// === 绘制 Liquid Glass 卡片背景 ===
	for(CUIRect &Card : s_GlassCards)
	{
		Card.y -= s_PrevScrollOffset.y - ScrollOffset.y;

		// 阴影层
		CUIRect Shadow = Card;
		Shadow.x += 1.5f;
		Shadow.y += 2.0f;
		Shadow.Draw(LG_ShadowColor, IGraphics::CORNER_ALL, LG_CornerRadius);

		// 主玻璃层
		Card.Draw(LG_GlassColor, IGraphics::CORNER_ALL, LG_CornerRadius);

		// 顶部高光
		CUIRect TopHighlight = Card;
		TopHighlight.h = 1.0f;
		TopHighlight.x += LG_CornerRadius;
		TopHighlight.w -= LG_CornerRadius * 2.0f;
		TopHighlight.Draw(LG_HighlightColor, IGraphics::CORNER_NONE, 0.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_GlassCards.clear();

	// === 动态彩色标题 ===
	const float Time = Client()->GlobalTime();
	bool ShowSearchModuleControls = true;
	auto GetRainbowColor = [Time](int ModuleIndex) -> ColorRGBA {
		// 每个模块有不同的相位偏移，形成彩虹波浪效果
		const float Hue = std::fmod(Time * 0.15f + ModuleIndex * 0.12f, 1.0f);
		// HSL 转 RGB（S=0.7, L=0.65 柔和饱和度）
		ColorHSLA Hsla(Hue, 0.7f, 0.65f, 1.0f);
		return color_cast<ColorRGBA>(Hsla);
	};

	auto DoModuleHeadline = [&](CUIRect &Content, int RainbowIndex, const char *pTitle, const char *pTip) {
		CUIRect TitleRect, TipRect;
		Content.HSplitTop(LG_HeadlineSize, &TitleRect, &Content);
		TextRender()->TextColor(GetRainbowColor(RainbowIndex));
		Ui()->DoLabel(&TitleRect, pTitle, LG_HeadlineSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Content.HSplitTop(LG_TipHeight, &TipRect, &Content);
		if(pTip && pTip[0] != '\0' && Ui()->MouseHovered(&TitleRect))
		{
			TextRender()->TextColor(LG_TipTextColor);
			Ui()->DoLabel(&TipRect, pTip, LG_TipSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	};
	auto RenderMenuImage = [&](const CMenuImage *pImage, const CUIRect &Rect, float Alpha = 1.0f) {
		if(!pImage)
			return;
		Graphics()->TextureSet(pImage->m_OrgTexture);
		Graphics()->WrapClamp();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		IGraphics::CQuadItem QuadItem(Rect.x, Rect.y, Rect.w, Rect.h);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
	};
	auto RenderTexture = [&](IGraphics::CTextureHandle Texture, const CUIRect &Rect, float Alpha = 1.0f) {
		if(!Texture.IsValid())
			return;
		Graphics()->TextureSet(Texture);
		Graphics()->WrapClamp();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		IGraphics::CQuadItem QuadItem(Rect.x, Rect.y, Rect.w, Rect.h);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
	};

	// Avoid repeatedly scanning every key/modifier combination for each bind row.
	std::unordered_map<std::string, CBindSlot> CommandBindCache;
	CommandBindCache.reserve(64);
	for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT; ++Mod)
	{
		for(int KeyId = 0; KeyId < KEY_LAST; ++KeyId)
		{
			const char *pBind = GameClient()->m_Binds.Get(KeyId, Mod);
			if(!pBind[0])
				continue;
			CommandBindCache.try_emplace(pBind, KeyId, Mod);
		}
	}

	CUIRect Row, LabelCol, ControlCol, CardContent;
	static bool s_ShowSponsorQrCode = false;
	static bool s_SponsorQrTextureTried = false;
	static bool s_SponsorQrTextureReady = false;
	static bool s_SponsorQrDecodeFailed = false;
	static IGraphics::CTextureHandle s_SponsorQrTexture;
	static const char *s_pSponsorQrPngBase64 =
		"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XHKJTjrppHoaJRpCOBzWvffeqyVLliTc36lTJ02fPj3Fo4IkHXnkkeUW4quOir5HpQLXhygr0czXwYMHl1vg67bbbtN//vMfvfLKK1We7/XXX9fRRx+t1q1bN/jP1vrkuq62bt0a11/TrzkcDmvdunVavHhxpaWKfv3rX8fKa02ePFkvvPCCFi9erJ07dyY8fuXKlVq5cqX69eunq6++WgMHDqzTwPTll19OuOBxZTPhG7tkJnZVF9fPaIwIZhGnOis2+v3+WADbpUsXdezYUR06dNBxxx3X5KfYFxUVxd2KXJZt29q7d68+/PBD/epXv0p4TKIFpq677rq4C++jjjpKN910U7Vrnt5666167bXX4j7hW79+vfLy8nTffffFAvPCwkLNnj07Ljzq3bu3jj/++KTfMxm7du3Sli1btHHjRq1cuTJusaWqTJgwQddcc02txxUIBDRq1ChdcMEF+v3vf6/58+cnvKAptXbtWq1du1YjR47UtGnTGt3CPU3JunXr4oLQtm3bVrpSal1IdGvTpk2bqnxdbm6uJk6cqEGDBknyfkk41IYNGzR+/HhlZ2crFArFzX6q7srKf/zjH+P6Lr744oSznb788kvdddddcf3p6em66aabEp7ftm1t3rxZf/jDHxJekB6qa9eu+vnPf66BAwdK8uobrlixQk8++aTmz59f4QyB/fv3a9GiRVq0aJF69OihSy65ROeee26d1yytSCQS0SeffKK///3v2rhxY4UX7FXp06ePLrvsMg0aNKjGq8Oj8dq/f7/uvPPOhDMNJe8D"
		"6UWLFtXrwoSo2ObNm2tcE37btm06//zz62wsXB+iptauXRvXV/oztVRaWpoefvhh3XnnnVq2bFml55s7d67mzp2r9u3b69RTT9Uxxxyjo48+Wi1btlQwGJTP51MgEGjwOzpatWpV4/rhu3bt0nPPPZdwYeDqhp5ffPGFVq9erd/97neVThRp2bKlHnnkkXJ3Rxx11FG6/vrrNW7cOP35z3/WI488UuHvNxs2bNCGDRvUq1cvXX/99Ro0aFDcLPnqKp2Ze6g+ffqkpIRCfUl0Z2NVC/JWhetnNEYEs4hz7LHHJuzv0aOHevTooS5duqhTp07q0KGD2rVr12yLUK9atUrTpk2r1TkSBXtdu3ZVXl6eli5dqqysLF1//fWaNGlSjW5V6Nixo37+858nnMGzadMmDRo0SAMHDlT79u21efPmhMXiR44cWe33PdTevXv1j3/8Q++//77Wr1+f8MKzKsFgUOPHj9f48ePrPBDNysrS1VdfrREjRujxxx/XY489Vmndn+XLl2v58uW6+uqrNWXKlGp9mIGqFRUV6b777ovrnzp1ar0txlZUVKQFCxZUOBOuIt27d9fkyZM1ZMgQ5ebmxvoTXTS++eab+sEPfqB27dpp586dcZ+cV2e21z//+c+Ei+GNHj06ri8UCumWW25JOIPjrrvuirvldc+ePXrllVf01FNP6cMPP6xyLLm5ubr55ps1bty4uHA5KytLP/3pTzVy5EgtWrSo0sX3JG92+tatW3X33XerT58+Gjx4sPr166du3brV2aySUCikjz76SNu2bdOWLVv09ttvJ1zAKRnHHnusxowZoyFDhhx2s2OnTJmiMWPGlOtr06ZNA42mcduxY4emTp1aaZmnefPmqWfPnikcFRorrg9RE47jaPny5XH9iWYlZ2RkaN68eerQoUPCxdwO9fnnn+vzzz+vk3HWh5/97GdJBbPTpk0r9304Pz+/0g9SD515nsiOHTu0YcMGrVq1SmvWrKny+AEDBmj27Nnq3Llzwv0tWrTQ5MmTdemll+rFF1/UI488Um7BtrK2bNmiKVOm6JRTTtHUqVM1ePDgGoeOa9asSTjD8vLLL2/0MymLiorK3UVm27b27dunDz74IOHCdrW5fZ/rZzRW/C0hTqdOnTR27Fh16dJFxx9/fCyAre5srqautuHgKaecUmGtzKuuukrBYFBTpkyp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6acndeGdm5urXr166eyzz9YFF1ygjh071v/gDtGyZUs99dRTuv/++/XEE09Uemy3bt20ePHiWoWNF110UZXBbE5Oji666CKdc845Ouussxrlgjmmaer888/XgAEDtHz5cs2bN69cMf/BgwdXuIonqsc0Tc2dO1e33XZbuf9Xd999d0pmKHbt2lVTpkyJ/f8YNWqUxo4dq759+8o0zWqf70c/+pGef/55Pf3003rrrbfibm869thj1b9/f40ZM6ZaK1v36NFDl156abnbmUaMGBFX66pXr1666667NHv27HL9w4cPL1db9NRTT9Wtt95a4Z0Sbdu21ZVXXqlRo0bV+afu7dq10w033KArr7xSb7zxhp555pm4MLtLly760Y9+VK3zTpgwQYsXL660VvSh+vbtq3POOUenn366evTokfQvb2ia2rZtq9/+9re69NJL436Ry8nJ0eLFizVgwIAGGl3zlZ6errFjx8b112bxouOPPz7hLMPa1DTn+hDV8dZbb2nXrl3l+oYPH16jut/HHnusRo8eHauZ+d133+nzzz/X999/r++//147d+5UYWGhiouLVVJSUq2fk/WhVatWSa9X0r59e+Xk5Gj//v0VHjNs2DBNnz497ppI8oLBvLy8CkM3v9+vn/zkJxo5cqTOPvvsern2TE9P1+WXX66hQ4fqueee04IFC+L+7qdOnar09PRqnfeII47Qww8/rJNPPlm/+MUvlJeXV+G6MXVh0aJF1X7NJZdcUuGHmb169aoymJ06dap++tOf1rhmKtfPaMwMN9HS0fXs"
		"+117yrVbtWz8U+xR9xKtgjt06FAtXry4Tt/n1Vdf1RVXXFGub+bMmbr22mtrfe69e/fqwIEDFe73+/0KBoPKzs5u8BVPy3rvvff0zDPPaMWKFeUWIGrZsqXy8vJ0xRVX1PrWF8dxNGTIkLhZpr1799aPf/xj9enTRz179qz2hUdD27dvn55++mktXLhQxcXFev3115OujYXkhEIhPfTQQ3r44Yc1ZcoUzZo1q8bnGjp0aLm6a2PHjq20VMuXX36pF154QRdffLE6dOhQ4/c9lOu6KiwsjP0ClJaWpmAwWKtZmJ9++qmef/55Pfnkk3rsscfUv3//hMetW7dON998s3bs2KEuXbpo+fLlcTPSCwsLdfHFF2vr1q2xvh/96EcaN26cBg0alNIa4x9//LFWr16t5557Tp988onmzJmjyZMnV/s899xzT8Lb0Er169dPZ511lnr16qXu3bs3yg+GmrNLL7007m6XV199tcYrd9fUhg0bNGHChNjPyhNPPFELFy5s0FWuX3755YQzFj/44INGdXv5/v37YzOLytq8eXPC0KQp4foQ1REKhfTmm2/q2WefjdX2XbJkiQYPHtzAI2t81qxZU+6DbtM0FQwGdeSRR6pTp05Vztz8/vvv1adPn3K3/g8cOFDDhw/XwIEDU/49dNeuXfrtb3+rRYsWKRwOq23btnr77bdr9eHwm2++qQ4dOlS4QFlZ77//vi666KJyfbNmzdKUKVNq/P41sW3btnL1rk3TVCAQ0JFHHqn27dvrlFNOqXVd7VJcP+NQjSGfJJhFgwmFQnGrHAYCgTpfjGTPnj3lZjhK3qezrD7t/TD57LPPVFhYqOzsbHXo0KFOPx1+8skn9cwzz2jgwIGxWWh19UO1oe3YsUN///vfU3rLYXPz5ptv6rTTTqvVL4HhcFhlf8yZptnkVifdvXu3cnJyKv26vvrqK91+++26/fbbdeqppyY85m9/+5vy8vJ0+eWXa9iwYerevXt9DTkppQsqdOnSpUalTT799FP169dPkhcqnH322erdu7dOOeUUdevWjRlfjVxjCWYl7/bHq6++WiNHjtTs2bMbPJjavHmznn322bj+WbNmNapFKAsKChIGyA8++CDXYI1cfV8fomL//Oc/tWLFCt144421mrWNit177736/vvvdd5556lPnz6N4vvRf/7zH82dO1c//OEP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YBYAAAANql+/fnr66aerPG7Lli0aPXp0ub677rpLV1xxRZWvHT58uD788MMaj7E6mDErvfjii1qwYEGsPXHixLhjQqGQfvazn2n79u2SpJNPPlkDBgxI2RgBAAAaGsEsAAAAknLllVeqQ4cOCfetWrVK7777bo3OaxiGgsFglcelp6cn7Evmtam8Pb45B7OhUEi/+c1v9MADD8T68vLydPrpp5c77r///a9mzJiht956K9Y3c+bMZleDFwAANG8EswAAANU0ZMgQ9e7du1xfdnZ2rc973nnnxWpylsrMzKz1eevK0KFDdcYZZyTc95///KfGwWxT01xLGezatUuXXnppbAasJPXv31933nlnueNWr16t6667TkVFRbG+n//85zr77LNTNlYAAIDGgGAWAACgmtq0aaM2bdrU+Xlbt26t1q1b1/l5kVrNdcZsy5YtddJJJ8WC2REjRugXv/iFcnJyyh136qmnKi0tTUVFRfL7/Zo9e7YmTJjQEEMGAABoUASzAACgWfvLX/6i+fPnN/Qw6lTHjh310EMPNfQwmq3KZsU25WBWkqZNm6ZNmzZp+vTpGjVqlCzLijumdevWuummm/TWW29pxowZ6t69ewOMFAAAoOERzAIAgGYtPz9fmzdvbuhhoAnx+Sq+xG7qwewJJ5ygtWvXJqwHXNakSZM0ZcoUmaaZopEBAAA0PgSzAAAAQB1qrqUMSlUVykpikS8AAABJfEQNAAAA1KHKwtfKZtMCAACgeeHKEAAA4BB9+/bVtddem/TxeXl5cX1Lly5N6rVPPPGE"
		"1q9fX65vxIgRGjFiRFKvf//997VgwYKkjm2s1q9fr9WrV1d53MaNG+P6nnzySR177LFVvnbr1q01GltNNOdSBgAAAEgewSwAAMAh2rRpo/PPPz/p49u3b6/PP/881h49enTSry8oKIgLZkePHq1zzz03qdc7jpP0OBuzROF2Mj755JMav7a+NOfFvwAAAJA8ShkAAAAAdYgZswAAAEgGwSwAAABQh5r74l8AAABIDsEsAAAAUIdY/AsAAADJ4MoQAAAADSo9PV0bNmyo8rgXX3xRs2bNKtc3Z84cDRkypMrX9urVq8bjq67KgtnK6s8CAACgeSGYBQAAQIPq3bu32rRpU+Vx2dnZcX0nn3xyVUDC5QAAIABJREFUUq/t0aOHtm7dWqPxVVdls2KZMQsAAIBSlDIAAADAYWHPnj1xfbm5uQ0wkspRYxYAAADJ4CN7AACAQ3z00Ud68MEHkz7+888/L9detmyZOnbsmNRr586dG9d31VVXaerUqUm9/pNPPknquKbgs88+i+s74ogjGmAklWPGLAAAAJLBlSEAAMAhtm/fru3bt9fqHIkC12QVFRXV6vVN1ccff1yufdRRR6lVq1YNNJqKsfgXAAAAkkEpAwAAADR6BQUFev/998v19ezZU4ZhNNCIKmYYhnJycuL6c3NzG+V4AQAA0DAIZgEAANDo/etf/1I4HC7Xd9JJJzXQaKqWmZkZ15eRkdEAIwEAAEBjxb1UAACgWTv++OM1c+bMWp1j1qxZFe678847FQgEanX+6mqMdVdra/PmzXF9p512WgOMJDmJQliCWQAAAJRFMAsAAJq1E088USeeeGKNX7969eqE/Z07d9af//xnHXXUUTU+d3Oxd+9e/eUvf6lwv23bmj17dlz/zp07K31dWQcOHKjx+GoiPT09qT4AAAA0XwSzAAAANbRp0ybdcMMNcf3BYFAPP/wwoWyStm3bprFjx1b7ddOnT6+H0dSNYDAY10cwCwAAgLKoMQsAAFAD7733nq644grl5+fH7XvooYfUs2fPBhgVGotEwWxaWloDjAQAAACNFcEsAABANb3//vvKy8vT3r174/bdfffdGj58eAOMCo1JohA2UVgLAACA5otgFgAAoBo2bNigsWPHateuXXH7rr76al177bUNMCo0NomCWWbMAgAAoCxqzAIAACTptdde0+TJkyvc/8EHH2jcuHEpHFF5U6dOVb9+/Rrs/ZN14403Ki8vr9Jjdu7cWWEN2fvvv1+tW7eu1Rhyc3Nr9fqqJAphA4FAvb4nAAAADi8EswAAAFVwXVdLlizR3XffXelxmzdvTtGIEps4cWKDvn+y+vbtW+n+b7/9VlOnTk2479FHHz0sSkVQYxYAAABVIZgFAACoRCgU0uzZs7VkyZKGHkqz8NFHH+n666/X9u3b4/aNHDlSw4YNkyR99913ikQi5fa3bNmy0YSfiWbHNpaxAQAAoHEgmAUAAKjAF198odtuu03r1q1r6KE0ea7ratmyZZoxY4by8/Pj9p9xxhn65S9/KcMwJEk33HCD1q9fX+6YV199VT169EjJeKuSKJillAEAAADKIpgFAABI4O2339bNN9+sb7/9tsJjpk6dKsuyUjam4uJiPfXUUwqFQgn3l4aWh5vPP/9cv/zlL7VixYqE+08++WQ98sgjysnJSfHIao5gFgAAAFUhmAUAACgjPz9fDz/8sB5++OEKj7n99tt10003pXBUUkFBge64444KQ9kRI0ZowIABKR1TbRUUFOiPf/yj7r///oSzZCVvpuxvfvMbtWnTJsWjqx2/359UHwAAAJovglkAAICo9957T//v//0/bdu2rdLjfvOb30iSxo0bp6OOOqrex/XZZ59p2rRpevfddxPuHzFihB588MGEC041Ro7jaMWKFZo7d64+/fTTCo8bNWqU7rvvPuXm5qZwdHXjBz/4gcaOHVuur2vXrg00GgAAADRGBLMAAKDZ27Nnjx555BEtWrQoqeP3"
		"79+vBx54QI899piuv/56jRs3TkcccUS9jK2qkgqXXHKJ7r///sMmlJUk0zS1YcOGCkNZv9+ve+65R5MmTZJpmikeXd0YPny4hg8f3tDDAAAAQCN2eF7pAgAA1AHbtrVixQqdf/75SYeyZe3du1e/+MUvdM455+jJJ59UQUFBnY2toKBAc+bM0eWXX15hKHvDDTdo3rx5h1UoW+rWW2/VkUceGdc/YMAAvfbaa5o8efJhG8oCAAAAyWDGLAAAaJY2btyoBx98UBs2bKjwmI4dO+r+++/X6aefrpdeekm//vWv9X//939xx+3cuVMzZszQ448/rptvvlnDhg1TWlpajcf23nvv6a677tIHH3xQ4TFz5sxRXl5eShf82rJlizIyMhLu2717d7XOdcwxx+jOO+/U7bffLklq27atbrvtNo0ePVo+H5eoAAAAaPq46gUAAM1KQUGBbrnlFq1cubLS40aPHq2ZM2eqVatWkqTLLrtMw4YN05///GfNnz9fX331Vdxr/vvf/2ratGmxgHbQoEHVChn37NmjX//613r00UcrPKZ169aaP39+gyz0NWvWrBq97ptvvtHnn38e19+hQwdJUo8ePXTLLbcoNzdXW7ZsSeqc69evT9hX0eJoVfnhD3+Y0pAbAAAAIJgFAADNSmZmZqVhabt27XTvvffqggsuiAvq0tPTNW7cOA0bNkzPP/+8HnrooYQzRbdt26Yrr7xSffr00S233KKzzz670jEVFxdrxYoV+p//+Z+EgW+ps846Sw8++KDat29fxVfZuGzevFnXXXddhfu3bt2qvLy8Wr/P7Nmza/za//73vwoEArUeAwAAAJAsCncBAIBm54YbbkjYf8UVV+iVV17R4MGDK509mZ2drSuvvFJvv/22pk2bJr/fn/C4zZs3a+3atRWex3EcvfnmmxoxYoRuuummSkPZadOm6emnnz7sQlkAAAAAiRHMAgCAZufEE0/UpEmTYu2+fftqxYoVmj17dsIFqSrSqlUr3XHHHXr77bd1+eWXx+3Pzc3VlClT4vojkYhWr16tUaNGafz48ZXWku3WrZtefPFF3XHHHUpPT096bAAAAAAaN4JZAADQLF1zzTVq166dFixYoOeff14//OEPa3yuTp06ae7cuVq5cmW5sgXTpk1T69atY+39+/frhRde0LBhw5SXl6fNmzdXeE6/369p06bpxRdf1BlnnFHjsQEAAABonKgxCwAAmqWOHTvqL3/5i4LBYJ2ds3fv3vr973+vV199VU888URsFu3HH3+sFStW6KmnntKuXbuqPM+QIUN0xx13qGvXrnU2trqwYMEC9evXL+njjzrqKEne4l6VLWhWE4lq1ubl5alPnz41Ol91Fmmryssvv6wvvvgi4b527drpoosuqrP3AgAAwOGLYBYAADRbdRnKlrIsSxdddJH69u2rV199VcuWLdO6deuSeu0ZZ5yhG2+8UQMHDqy0xm1D6dChg9q2bVvt13Xq1EmdOnWq07E8++yzWr9+fbm+MWPGqEePHnX6PjXx2muvadmyZQn3jR49mmAWAAAAkghmAQAA6szevXu1ceNGvfbaa3rppZcUCoWSet0ZZ5yhqVOn6txzz5VlWfU8SgAAAACNAcEsAABALXz99df661//qtdff12rVq1KOoyVpAsuuEATJkzQgAEDCGQBAACAZoZgFgAAoBpKSkq0fft2bdq0SWvWrNE777xTrdenp6dr4sSJuuSSS3TSSSfV0ygBAAAANHYEswAAAJWIRCL65JNP9Pe//10bN27UG2+8od27d1f7PH369NFll12mQYMGqWXLlvUwUgAAAACHE4JZAACAMkKhkD766CNt27ZNW7Zs0dtvv62vv/66Ruc69thjNWbMGA0ZMuSwmx07ZcoUjRkzplxfmzZtGmg0AAAAQNNDMAsAAJot13X11Vdf6d///re2b9+uv/3tb1q7dq2KiopqfM5jjjlGI0aM0HnnnadevXopLS2tDkec"
		"Oq1bt1br1q0behgAAABAk0UwCwAAmhXXdbV06VK9//77euedd7Rjx45an7Nbt2668MIL1b9/f5166qmHbRiLurFw4UItXLiwoYcBAACARo5gFgAANCuGYejrr7/WsmXLanyOYDCo8847T/3799cZZ5yhLl26yDCMOhwlAAAAgKaOYBYAADQ7EyZM0OLFixUOh5N+Td++fXXOOefo9NNPV48ePZSVlVWPIwQAAADQ1BHMAgCAZqddu3aaPHmyHnvssQqP6devn8466yz16tVL3bt3V8uWLVM4QgAAAABNHcEsAABoliZNmhQLZlu2bKmzzz5bvXv31imnnKJu3bopJyengUcIAAAAoCkzXNd1U/2m3+/aU67dquURqR4CAACA3nnnHbVu3VodOnSQz8fn1YeT4uJiHXoZGwgEZJpmA40IAAAAh5PGkE/yGwgAAGi2zjrrrIYeAmooLS2toYcAAAAA1ApTCgAAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMUIZgEAAAAAAAAgxQhmAQAAAAAAACDFCGYBAAAAAAAAIMV8X7w5Qr7gUbKCbeRLbyNf8Gj5M9rIDB4lf0YbGVamDNPf0OMEAAAAAAAAgMODG5EdPiA79K0ihd/JDn2rcNE3skPfKVz4jezi7+VzIoUKF+xQJPS9ivd/JNMMyIg+ZPpl+jNl+VvISjtCZiBXvsCRMtNyZaUdIStwhEx/tgwzraG/VAAAAAAAAABICdcplhPeL7t4r+ySPbKL98gu2SuneLfs8F7ZJfvkRgrk2CWSE5bjlEhOidd2w3LtsHyuE5GriGQXSZLsQ9/F8Mn0BWVY6bJ8GZIVlOXLlGEFZfoyvH5/lgx/tix/tgxflqxAtkxftqxAtgwrS6YvQzKomgAAAAAAAACgcXNdW64dkhPeLzeSr0jJAbmRA7JLDsiN5MsO75cTKZQTKYgeVyjXLpITKZTrhOSEC+Q6xXKdSKXv46t6JBE54XwpnB8f2kqSYcq00mT6c2T6s2T6D4aypq+0nRUNcYMyfOmy8yMyrHTJTJNhpcu102VYaZKMGv1hAQAAAAAAAEDVXLl2idzIPskulusUSXZIRa7PC1ftIrmRIjnhfDmRfDnhA3LCB2SHS0NZr+3YIcl1ajWSqoPZKr8WR06kSE6kSCr6NuEhpun3ZtKm5coM5MpWpmTlyAzkSr5shYxjZfiyZFhpMgy/DCvgzdS1ApLhj20Ns/bDBQAAAAAAAND0uK4rQ3a0XECkTNkAb+u1S2SXHJC952s54X1SZL+c8F7tN4sUKd4jt2SvNzPWrXy2a11ISdLpOGGpZI/skj0J93/7X0OmFfBq2AZbykpr5T2CLeVLayUrraWsYCuZvqxoOGt6pREMQ4ZhSjKjW4OSCQAAAAAAAECT5Mh1XW+mqutIcuW6jiSv7ToROZEDskO7FCn+Xnbxbtmh72UXe49IaJfskr2S"
		"U+Kdp4z6j2HjNZIpqK4cu1hu6HtFQt/LMD6Jhq6WXEmGYUkyZPrSZfpbyBf0wlpfekuZ/pbRMLelrGBLmb5sGaa/ob8gAAAAAAAAAHXFjcgu2S+7ZLciRd/LKdklO7RLdskuRYq+l12yS3bx3mhtV1uloa0hV657aNut6t1SopEEsx7vD6nMtrQ/unUi+TKK9yhS+JUMK+CVPTD9kumXaQa8rS8os7Rsgr+FrECuzEALWYEWMv25XttPeAsAAAAAAAA0PNeb6RreL7tkn5ySvbLD3tYp2Se7ZK/skn1ejVe7WK5TIjlh7w59JyzXCct1SuQ6YcmNRGfQlj37oe/WeDSqYLZKriPXLfH+AiIFiY+JLkZmmEGZvnQZlreVGYwtQGb50mVYGbL8WTJ8mbL82TJ8GdGFyjK94NaXIdNKT+3XBwAAAAAAADQZrly72FtEK1Igp+SAHLtATjhfbqQguqBWgexwgVw7JCdSJNcpuy2U7GLZkcJo+YHaLbbV2BxewWwyoouRSUUV1rT1yiIEoyFslkxf6SNTZiAazvqyYuGsYQVl+IIyraAMKz22Naw0mVZQMqxUfoUAAAAAAABAg3LdiDeD1Q55oaodkmsXyYmE5DohuZEiOXZITqRAbsQLY51IfnRbph3xQtnGUl4glZpeMJsUV06kyAtwQ99XcIwh0/LL8GXLSmvh1bYN5Mrw50TLIrSQL62FDF+WTF+GZPhlmn7J8EXLLHhbGb5YvwwjpV8lAAAAAAAAUF2uUyK5ETl2tDyAXeIFsbH+EskOyQ7v90oQFO+TE9kX3e6XXRwtQxDeJ7l2swxdk9FMg9lkuNF/ZLtkF++q8CjDsGT4MuRLO1JW4AhZaUfGPwJHyAwc4S1iZpiSYcowTEmG15ZxSBsAAAAAAACoa260VKgjuY5KF8Qq7ffqvYa9Oq/Fu2WX7PG2xbsVKd4tJ/rcLtkjO1IQfQ1qimC2llzXliIFKokUyCjcodKQ1S0TthqmJcnyFiQL5MoXbOlt01rKDBwhX9qRsa3hy5Jh8tcCAAAAAACAOuTa3gzXkj2KhHZ72+JDtqHdcsP75DgRGXLluLYMRfMvuXIdR0YszEVtkQDWgdJ/jLFtaX+5oww5kQMyir5RyX6fDNNf5uG1Zfi9hcv8WbL8LWT6s6NlE0rLJ2TLDLSQ5c+Jlk/grw8AAAAAAKA5c52IXLswGrpGH5F9skv2eWUGSqLlBsL5Xk1YJ1qewAnHHnJLn0fkOhGVTbUOLUJAUYK6Q7KXMu7Bf+yVMmT60mSYQZm+oGSmxRYgO7Rt+TNkWBky/VkyrAxZsW2mDF+mTH+mDCvDK6EAAAAAAACAw4PrSnLkRAq9Rzhfrl0gO1wQDWHzpejWW3CrSK5T7K2nFN167VCZ/hJmujYyBLONjisnEpIUkl1S8VGGaUXD2wwZ/kxZvqxoSJsp01fm4c+QYQZlWN7DtIIyrLQy7bTo/jRv1i4AAAAAAADqjTdxr1iuHZJrF8uxQ3LtkBw7JJVpe32FcsIFciLew40Uyo4UyI3kR/sLvWNdu6G/LNQAwexhynVsuY73n1KhnapsHq43gzZbViBHpi9HViBHhj/a9kfbVpYsf5ZkWDLMgGRYMq1AmbZPpuWXDF+0Bq6Rqi8VAAAAAACgUXNdR4ZryylTJkBuRI4d3TphGdF2adkBN3xAkZL9ciMHZBfvkxM5ILtkv9yIV37AtYvkuhQOaMoIZpsBO1wghQsUKfqm4oMMQ4YRkC+6QJkVOEJWIFdW2hEyA0d47TSvbfiyZFpBSYZkGJJMGdFt4jYAAAAAAMDhqHShK1dyHcl15cqREd3Kje53imWHD8gp2Su7eK/skj2yS/ZEF9ba4/VHH65dIiq1QiKYRSnXlVSicNFOGcW7JBkyDFNudFu2bVp+r5Zt2hEyAy3kCxzpLUqWdoRMvxfeWv4W8qUdIVkZ0Rm2AAAAAAAAhxE3IidcKCe81wtXw3tlF++RE94XC1sjJXvk"
		"luz1JsW5kYMLwzu2jFioK7nuwbZhGGIiLCSCWZThTY935TrRbxql/Ycc54QNGeY+RUI7ZZh+GYbPq09remUODMPvhbGmX6aVJtOXIdOfI9OfLcufI8OfJcufI9Of5fX7vLZhBSXDTOWXDAAAAAAAmhPX8Wq4RvZ7M1xjj3zZ4f3R56XbArlOSbQm7MHyBK4TlltarsCJePtcW4cmKIfmKbGchVQWUQSzqAFXrhORFJFrhyo/1DC9WrVmmrfwmJkm0xeUzECZdppkpMn0pcuw0mX5M7yFzHwZsQXNvH5va/q8Bc0okwAAAAAAALyF1Iu8xbIiBXLtIi9UtYtkhwvkOoVywoVy7cLoYlnRBbacYm8BdqfEW3gr2nad4mg7wqJaqFcEs6hfruN9k1NIjvZVeqhhWDJ86TJLQ1mftzWtDJlxYW26DDNNhpXmhbvRrWEd7DOs0n5vETMAAAAAAHCYcG1vtqpdLMculusUy7W9h1PmeWm/YxfJjXjBqxe+lnkeKZRTGspGighb0WgQzKLRcF1bbjhfTji/iiO9Rce8GbRZsgLZMn3ZMv3ZMv2ZXlkEX5asQI4MX6YsX5Zkpck0/ZJheWUXDJ9Myy9XlkyrTL8sauICAAAAAFDnvMWzXCfi1W51wtHw1ds6dlhS5GA74gWtdviA3IhXZsAN53vlByL5sksOyI0ckB3OlxspJGzFYYkECochV3Jd2SUHJB1QpOjrKo43ZPkzZPpbyAzkygq0kBXIlRloIdPfItpuEd2fI9OXKcOwVBoAS6YMw4i2Dy6EdnA/JRUAAAAAAM2R9/u5K8dbVDy6do3keHVUXSfaduW6tpxwgVe/tWSv7PB+2SX75IT3ee2SfQfbxXtl24VihSw0dQSzaBbscKFXJ6bo22joKhmGKbdMyGoYplzDkGkGZPqzZAVayPC3kC/QQqY/xwtvS0NcX46stFwZvmyZvoyG/eIAAAAAAGgArh2Khqn7D26L98qN7FekbDu8X3bJfm/BrNJFstzowuOuI0Ou3GiIW9omlEVzQDCLZsD7Zl56W0NsW27vQY4MGSV7FS78Robpk2H4vPIG0W35536vxq0/U6YvW5Y/S4YvS6bfe1i+LBmlW1+mt5CZmZairxsAAAAAgOR5tVoLvIWzIl6pQTu6dSL5csIHvOfhfG+RLadYrhOOlSdwnYhcNxJdNCsS319B2HpoL5EsmguCWSCO69W0UVjJlKgxTEsyAjJ9QRmGX4YvKMMMyLSCkumXaXltw0o7eJwVlOVLl8x0Wf4MGWZQpj9DMoOyfBkyrKA3E9cKyrTSRLkEAAAAAED1uHLtEm/BKyckJxzdRgolOyQ7UijXLvJqucb2ly6oVSLHLpbKbUOxxbjcaFvMbAVqhWAWqCXXsSUVybaLkjjakGFYMnzpMq10mb50GVa6zFgYe0jbSpfhC8o00yQzTYYViIa+Xtu0AlK0bZjePpn+WLkGAAAAAEAT4TpeMOqUyLW9reMcDE9d2wtNFe13nRIvdLW9MPbgtigWyDplnrt2UfT3W4JWIFUIZoGUcr3bOcIH5IQPJPUKw/R5IW60TILlz5JhZcgKZEfLI2TJtDKj+zNk+TK9cNb0S4Yl0/RLMmNtrxSDVWa/T5IVXcgMAAAAAJAKrhOWXNu73V/RrWvLKdvv2rHjHKdYbqRIdjhfrl0oJ5wv1y6QXXJAbqTA6y+zdSIFii3CBaBRIpgFGjnXich2DshOMsiVDJm+oEx/tix/jszYI1tWwNvG2v7SdrZXNze6EJoX0hoyolvFFkgr2/beCwAAAACavdjCVa68GafeLf6unOit/qV9Xtt1I3IiB+SU7JcdnbjjhKPPS7znXnu/t69kvxwnRNkAoIkhmAWaHFdOJCTXLlEktCsatkqGYcqVUUHbiC1cVhreWv4W3sJlAS/Ytfw5MnyZ3tbvLXQmwxLhLAAAAIDmzHXDciOFskv2yY3kyy45ICdyQHbJfrnhA164Gm2XhqyuXSjH"
		"sWWUhrmGK9dxZEhyXedg2/DaBrVcgSaJYBZokly50ZXLYtvYnsSc8AHJ9Hk1cE1fdAat5W2j/Sq3jS5s5kuX6css88jwSiz4MmT6Mr3yCv6saO3cTK8ObmzGLQAAAAA0Fo5XpzVSKDtSIDdSKCeSLydSKCfWLoi1nUih3EiBXKdYjh2W3Ei0RqtXhsB1I5ITkRsrSxA5WJ6ggpWmD/19rTSLJZIFmiaCWQCSogGubSf9A98ra2DJiC1AFpBh+GVYaZLp9xYmM0oXKvMf3O8LyjDTvIXNrDSZvnTJTJPly5DMoCxfumSleaGv5R0nM+DVxAUAAACAZLgROXZIrl0sxy6S7GLZkSLJKfYWunJC0QWviqMLYBVHF7/yFtKSE/G2bjjaDst1iuU6Ye9YNxxdgCvs1YAFgBogmAVQI17tpIjcSERSoZxkX2hYMq00GVYwGr4GoyFtUKbP2xqxUDZ4sG0GDj4sfzQE9gJbw0zzZvWaadF2wAuDTb9k+kS5BQAAAOBw5HozTp2wHKckGo6WhqGHbO2SMmFpiRei2sVy7ZCc2Dbkha/2wVDWtYui/aHo6yIN/UUDaEYIZgGklmvLiRRKkUIlvnknMdPyS2a6LH+mVxbBnynDjG6tDFn+jDLtg8cZVlAyfDJNn1zDkmn6JJneDFzD9Mo2qLR8gxkt3xBtE+gCAAAAtebVSHXkOBEZbnQrW44TkVwnGobaB2/zdyLyygqEJSckO1LoBajhArl2kVdmwC6SHS6Q7CKvrEBsf6H3+4ZrRyeTAEDjRTAL4LDg2GHJDssJ76/W6wzDJ9OfIdOXJdOf7S1w5s/y2oHsaF3cLJmlfb4Mmf4sGb4MmWZauQXSZBiSjGgZB6NcnwxDhoxyC6wBAAAATYe3+JQr9+CCVbEFqbz+0ufl+l1Hckq88DScH63NWvo8/2Bf+ID3PHzgYNsuZAYrgCaNYBZAk+bK9lZFDRfILfo2FpoahhkNUQ25riHDNCW3tF8yTNNb4Myf5YW3/iwZvszoQmbRrS9TViBbhpUh058ts3Try/Bq7TLjFgAAAE2A6zqSG5Zdki/ZBbLLhKlutO1GCmSX5Mu1C2WHD3h3yIUPeDNY7RLFQlqVzqD1wl3j0H6jdL9i/QDQVBHMAmjaorcvla56GtuW7i7dJqirYBim7JK9ZcobRLelbcNXYb9h+aO1c9O9oNYX3cZq6qZHa+qme21feplau0HJYLEzAAAA1B2vHmtpbdWQ3EhRdNGrIjmx56X9hXIjhdH+Qq/fLokGtBG5ri3XsaPlAqLlB9xoO9ZBOfmiAAAgAElEQVRfdhsfsB56PR7rdxP3A0BTRDALABXwLjwduar+KquGaUmyZFoBqXShMsMnwwp4NW+jW8M8tN8vGX5vvxmQaQVjW8P0RxdDi/YbAZm+NMkMyIoed3ChNIJdAACAJsN1JdlyItEFquyQXLdETqRYcorl2MXRcgFev2sXyy1tR4+XG/bKg8W2Ebl2iaSIN6PVPbh1Y+1ib+uEE4arAIDaIZgFgHrgOrYkW7ZTUv0XG16dWsOMhqy+NBlmmkwzzQt4S8NXK9pvRfvNNK+vNMQ1/TIMbyvTL8P0eSFwRe3oc29hNF80XKYcAwAAQK1EQ1VvxmrE27oRqUxbbiTxfjcs1y7dlhwMZZ0SOXZxNICNPuxi75hon2OX6XeK5TqOmIcKAI0LwSwANDauG709rEBSgVSTbNf0eaGtP12GGZTpy5DMNFm+dK/fly5ZQa+cgpkm00qXLG+/ovsNIyDD8ksyoyGtt3VlyjR90a3XNkzfIcf5JBne1lV0kTQAAIDDjOtIcqIfupfdRpJvu3Y0NA1FywUUe1unWE6kSCpt29EyA6X9ZcoO2JGixLW3AACHNYJZAGiCvNkWETmRghqfwzAsGVaaTF9G9JFZpmZutM/KKNc2Yu10Wb4MGVa6DNPvhbeGIW8GrhGdiFu6ENuh/d5zwzC8BdpKZ+1GF24DAACoXOkaA25s"
		"gSmvr7R4qfc8tv+Qfsn1PiiXt+CVE/bqrzqR0rqrhd6CVuFCuY7Xdsr2lznO6wuxiBUAICGCWQBAQq5rRxeDCMkw98h1vQXRJG/rtaPhqWFEJ8Yeut+bRWtaQcmKhrVmUKY/42DIW7o1g94M37JtX4YMX1CmlRGtr5smyisAAIAKudEwNRqI2pFCyS7yZpzaRbIjhd6M1Uih5IRkhwui1zuF0RmrhbHFsLxyAGEZhrf2gCHFAlZXbqwd6zck13ESHg8AQCIEswCAipXOKHFKm94tdIeuolvRqrqSF97aMiXDUtgwZRiWlMRWxsHyCZLXb5i+aJ3dNJlm8GBNXSvo1dqN1dk9+NzrL63BW3aBNK+fMgsAADQg1/XqppapnVquhqpzsGaqG62p6i10VSzHDkXLA5TuD0XrsIbllRFw5NV2jZYXcB2vrEB0gVfvuVdqQG7Z/WXaldRkrej6x3UT9wP4/+3dd5xcd33v//c503Z2dnd2tkqrLtuSLatY7jaYZgIEMIRmTMgPSBxwkgsYAgEMCT3AvQTCj5BygfxC4BdCSbh0CGBwAFe5yZZlS7J6216n7NRz/5iyc6bszszOzrbX8/Ewq/M93/M93yPJa/Y93/P5AihEMAsAWFBWZsMLFYS6tTAMMx3eZjYoM02XZDjyNixz5h07ZJY4tpR/nBnHkd0QzZ05dkumS6aZ99VwpgNfIxMOZ0JiGdl+TpmmW5bhSIfLAACsKNkPa+NSKqFUKpb3NbMxVWajKllxpZIxyZo5n+uXismyErIS0cz5eDoMzX1NSFYy3a6krGRC2Y2z0rVa0+2pZPZ8vGCchIhEAQDLBcEsAGDZyK5wSf/QJc3/5UAjs1GaKx3WFoaxmXYjE9oaBcf28zP/yHDIMLKhsCMd6hqOzHH215mvZjrInemf91XZ6wvGo5wDAKCUzIeh6Q2nEumyRFZSSmV+nUrknS88TmRWls6Eo1bmuvQq0kSmPSErFc/Us8+sTs18nfln5jg/lM2GuZatP0EqAGD1IpgFAKxi1swPjhn12O84Ha66ZlbeOjyS4ZbpSIe7psMjy3DLdLoz7Z50qOtwy8gcy3RlVuhm+7tkOtPHhunM1PTNK/+QObYMw3ac3kjNke5vZo/NTG3gzLFZcJw7z4ZrAFAfVqbmqKVUKpnZkCrv2LAytUnt59P/nSo4tpKZ9lSmPfOafva1/WQ29IymvyajmRWsM8fpMgExycqWCUhk+sWUTEYzK2Cjsqy4Uon09ekVsfX4ryQAAMgimAUAoM7Sq38SSiYjkuoT9malyzk402Futsaumaml60jXzpXpzoTCnpnQ1/TIcGRCYtMj09Yv/6tnpv6u6ZQhM3vj7AzS/xh5v878b3YjuLzZFtTwzRxbyl0/c3X2fP4xANRbemWmZVmZ7zR5x0a2/M5MuwqPC6638vsZM/3t41kzr/Bn66RmglErFZOSMaVS+bVUs1+zIWo0V4M1XVt15lorV2N1embVqsXqUwAAlguCWQAAlpH0iqm4kvG4jEQos3LWzIQAZuY4HX4ahpHOCgwj154+NpXuYGbChHT4autvSYZp5lbsyuGW6WjKrQA2jPQmbDPHLhkOV2alsGemVm+uZq/bXsM3f0VxtoavM+86UacXQJ1ZCVlWIr1SNFMTVdbM6/Wp7HEyJik+UwPVVjs1NlM7NZl9PT+9wnQmNI3mQtN0e0y5FbOGkVkpmwlts2GukRf25kLdlAwZtuske+hb2B8AACwvBLMAACw3eSuy0l+zP6zbN1ib62slDMNUUoZkOCQjW97AzKyENTPHmfIHhpFeYZs5N9PfyIS/pY5n+sow02tojcy4pitdW9dMb9iWLeOQ/Zre8M2VXtmb3XQt+9XM+7XhyDvvzGwgN3NsZMtBGE7JNEu0OzL3MPPanZnfE8o9AOnX6GfqkKZrgc/UOE2fz9Ysze52nzm2Co5z45T4mrIfz9ROjc/UPk3GM7VQ47laqOk+iVydcuWVBbAd"
		"W5ak9Fcr87V8/3TfbDmC7PXpfpn75I9T4XfeSr9vZxfFWgXHAABgeSGYBQAAZeVWYBWEvgtpZsVutgauQ5ZMmXk1dS2ZmfNmUe1cM6+mbvrYXkNXpilZ6dq6thq8+TV3LaNETV4zNx9be+7YORMqZ+ZlyZSZmUf6fKa/ZZ//zDjZmsHlz6efz8iE08U1gQ3DkVvxLMvIrJjOPz/TP7/8RHF7fn/lnp9SE3PJhneZFY+GlMrUApWVSq+MTM2Ee9naojIsKTVzfmYFZLYWqXIhYKrgOL1hUyqv9mj6WJmv6ZqlM+dTqUTedVam/Er6fK7dyh8vMVPL1EopZSVlWIX308z1+bVRDc3cJxtu5h2na6rm10qVvaaqUaLGaiqZ+X3Kzjc7/1TevFOZ8/nPT3oJAACWFoJZAACwpORWAqcSma/pzdmW7JYzhiOzmja7wjYvnM2uqs3rk/t1fvia3bitVDibG8+RCZMLxsutUi5ckZwXpmbP536dWdGs7EpnY6Zftj07npENY7Mhbl44a8y0p0PGTNmM/LrB2S95tYVzv3XpuHLm2Cg4LjVO9hdW9v75NT6NgpqgxkwNUGVf+9bMca573l1zr5dn523ljZauKWp/fTy/FunMP7naonkrLPPDxZnwdmaVZrbdyo5jzXy1lMprS9nHt8qHkla2LZVSLrzN9s+Et5aSmbA4lf4QxiroZ6U3lZo5zq5cnQlTl6pa3hQAAABoFIJZAACA+ciGVFpN4U925W96ZW42jJ2peSyZ2bDWNPNqF2drIae/zoSr6TGVq308055fGzl9XUHtZBkqrslZWIMzJXsqm41ZsyvCMzU+U3nXFdTwtNUA1UztTylv5So1PgEAAFAFglkAAABUyZpZ0ZzMtti/5tZRLu0FlVUrW/Oz0RMBAADAsseOFQAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAAAAAAAA0GAEswAAAAAAAADQYASzAAAAAAAAANBgBLMAAABAAzR1XCYZjsWeBgAAAJYI52JPAACA5ap148uVjI4oERlUPHxWViK02FOqu9XwjPVgOlvUuuGliow+otjEocWezpJhOH1yuPx5LSklIv2LNp9qdF76TrlbNiseOqN46JSik09revRRyUrWNJ7Hf4nWXP1ZpRIhxaeOKjpxSMGz"
		"P1Ns6midZ169po498vgvyR0nIv0K9d9Vl7HdrRfI024fOzL8YF3Gnou362oloyNV/R67Wjarpe9GW9vU6R8qERmoeAzDdMnbfY3CA7+t+BoAALA6EcwCAFADZ3OfOnfcntdiaeTg5zV16nuLNqd6Ww3PWC/enusU2P4WBSSl4hOKTR3TwMMfLBlkm+6AfGuerebuqzV2+J+XRDC3UFrXvVAdl7w1ryWlEz99/qLNpxpNHZfJ5dsgT2CXJMlKRXXqFy+TVWMw6+2+WpJkOn3yBHbLE9it8NB9dZvvXLp2vU8Od6tiU8c0PfaEInn39nZfI/+WW3LHkZEHqwpmm3ufKf+WmxWbOq7Y5BGF+n+jVHwiPXbXVQpsf8vM2EP3NiSY9a19rrp2vlupREgDD/9VxR+YuFs2y7/19ba28NC+ioNZ09minr0fUVPHHo0e+t+aPPHtqucOAABWD4JZAABq4PL2FbQYSsbGF2UuC2U1PGO9NHXszv3adPnl9PaWDGU7LnmbWje8VIbpkiTFQ2c0+tQ/LNi8Wje8VPHQaU2P7l+we6xEpqtNrmb73/946LSsVLzmMZsyAW9WMjqs6ZFHah6vGobDK9+aG2Q4vPJ2XyfP6H5bMDtfzT3PlKd9pzztOyUrpcjww7lgdjG0rn+JOna8XYbpksPhVe8Vn9DgIx9SdOzAgt7X4elS7+Ufk9u/XZLUsf02SSKcBQAAZVFjFgCAGjiauovakpHBRZjJwlkNz1gvTXmvgUtSdOxgyX6JyPlcKCtJzT3XLdicOi55qzovfae6dr1HDk/Hgt1nJfJ2XVlUCzY2+XTN4xkOr+11fkmaXuCQMF82lM2KTjxZ1/GbOmZC51jwmBKRc3Udv1r5H35IksMdUO/lH1NTx94Fu6fLt0lrrv5MLpSVJBmmAttulatly4LdFwAALG+smAUAoAaOpsKgy1I8dLqqMdytF8jVsrlucypnevQRJaOjVV+3Gp6xHly+DXK1bra1RUZKv6odPPNTBS56kwxHsyTJ2bxO3s4ry/avhenyq3vP++Xtuip9D+9ade1+nwb2vadu91jpPO2XFrXNJ0j1rXmWLRiV1LA6q5LU3PusvCNLofN31W1sb9eVcnrX5o6nRx6t29i1Gnj4A+q94hNyt23LtZkuv3r2flhD+z+uyPC+ut7PE9itnsv+Ug5Pl63dSkU18sTnFQ8er+v95qvz0neouef6BRn7zG/+kFrkAABUgWAWAIAaONwB23EqHlQqEaxqDN/a5xTVMlwIAw++V5FagtlV8Iz10Lzm2cp/CSmVCCl0/r9L9k0lgoqMPGoLRXx9z6trMGs4PHK3bLK1eTuvVGDbmzV2+Et1u89K5vFvszdYyXm9+t/c+0z7cKmowv2/rnm8ajg8XfJ2XZE7jodOKTZ5uG7jt6x7Yd6RpdD5X9Vt7Folo6Pqf/AOrbniE7YVrKarVT2XfVCD+/+6bqUcfGueo86d75Lp9NnaU4mghh/7nwoP3l3z2J2X/nl69XadDD32KUXHHpPpaisKkevBSkYIZQEAqBLBLAAANXC4223HydjYIs1k4aykZwxsv02etm1zdyzDUrLsitPmguBkenS/rFS07Fjhgd/Ygllv9zXp1+Zr3FSqUHJ6UMMHPqOeyz9ue53bv+VmRScOKTzQmEBwuTJMj9ytF9jaYsETNa/INhxeeTsus7VFxw9W/SFHrVo3vESG6ckdu3ybtOaqz9j6NHXaX/H3dl5Z1CcrGRvV0P6/lpSpXbv2xryzhtov/IOCsa+wHXu7r1PvFX8957zHj31T0bHH5uxXTio2pv4H36c1V35Sbv/FMzN0+tSz5wMa3P9xRYbur3l8SfJv/X0FLnqTZNh/pEpGhzX46MfnNX9JcngCcnrXzGuMfKbDM3eneUjFJxd0fAAAViKCWQAAalAUWi7Sas2FtJKe0dO2rSh8qk6qZKvT2ydP+w5b21wr5ELnf6mOi/9MpqtVUnplckvfjQqe/dk8"
		"5mcXGd6niWP/pvYL3zTTaDjUdentOh88XnVJitXE232NDEeTrS06/kTN47Wse4GMgtWUoQatlpWklr7fKWqr5N+Fcn0Skf7cr9s2vaLovLd77rrJlfQJnvulyn+8UZlUfCK9crYgnLVScaUSkXmOLiWjYyrcsiMePKHBRz6seOjUvMdfbpIxglkAAKpFMAsAQA1Mt992nIyOLNJMFs5qeMb5aln/QttquVR8QqFzv5j1GisV1/Too2ruvSHX5lvz3LoGs5I0/vRX5Wm/NFdrVpJMd0Ddu+/Q+ftvl5WK1/V+K0WpV8fnUw/Wt/Y5tuNUIlj3P+tyWje8TM7mvgUb317GYGkqDGeT0RENPvzBumyAFjz7E7l8fblyLdNj+zX4yEeVWuJvF4TO3anY1NF5j9PS9wK5fBtyx8n4xLzHBABgtSGYBQCgBo6C0DIRGah6jND5uxSbOlGX+ZiuFgUu+qPcKsyZeZ2reeXWanjG+fIV1A4ND95TUeAZHrjbFsw2deyR6Wqr+6vAw49/Wn3X/b0cTd25Nrf/YnVc8jaNPPHZut5ruWndcJNkmCXaX1rU5vB0qXXjy8sPZlmaOv39omaXb5Oa2nfa2uJTx+Vu3VL9hDOS0XElIucq6tu68SbbcSo2pnj4fFG/wlXfUrrcQun7pz+gKQzlyl1Tzdj2uc4d8nXuuF3Na541Zz9JubIeDndAPVd8vGy/wtrakrT2ms9VVMrF7dukdc/8ckXzCZ75icYOz943dP4uxYMnyp4vrN+dio1p6syPy/aPh89Kyqzqn0ft2yzfmucU3J8VswAAVItgFgCAKhmmp+iH90SJsGMusamjdVm15GrZrN7LP14cWIbPauCh99cUqK60Z5we269UYqri+zk8HfLkB2pWcSkDb/c1crXYA7aWdb9bUHOzNMMw7MeOJm288buyUrGy16QSIZ3+5avmHDtfMjqs4Sc+p97LP2Jb2du64SWKTRzS1Jkf5dqc3j45muq7IVDxak1TnsDuWa+JTRyatUZvvXTueFtRbdDyfd8+R49UyWC2deNN6frBeTyBXVp77RcqnWaR4NmfaPjxT8/Zr3XDS221cqMTT+r8vf+jZN/A9rfIv+WW3HFk5MGyNZWz/Ftuzjuy1P/An2t6dH+JfrcosP0tM2MP3auBhz4w5/wrYbpaSgapszLM6q9R6cC2aD4F5V9m7etsnrNP6PydCpX5ttvUeUVRMDvy5Beq33xtHvWtCz+8S8bGaxoHAIDVjGAWAIAyPP5Lil5DliRnU09R2OLtulKulo1zjhkZfkiR4QfqNUW5/dvVu/cjcjT12NrjodMaeOgDSoTPzHr9anhGKf1afzXaL3yjLZhN15K0a91wU1GbJBmmu6p7VXqtadZWeiAydK8mTvyHLXiTDAUu/hNFp55WbOKQJMm/5dVq3fh7Nd2jGmuv+dys58/d/ea6hPmLzTBd8q19bt3HrWRFtmF65N/6+7a2yZPfrdscWjfcJFfr1tzx9MjDJUNZLJzm7qttx1YqpsjQvqrGcDT1aMOzv65UIqRUYkqpeFAjBz9f0YpmSUUflC3nDSIBAFgsBLMAAJThbrtAbZtfU1Hf5jXPrqiflYrVLbT0BHarZ++HilZyxUOnNfDgHRW97rwanrEWTu9a23FietB27PJtUHPP9Qty74UwduiLamq/VJ7Arlyb6fSpe/f7dO7et8pKhBZxditT64abalqZORcrlZizT1PnXpl5G47FJg8rdO7ndZtDZHifgmd+Il/f82QYTo1V+cHHQrFSsSW7SaGzqVMyXHUbrynv32UpvdI8lQhWNYaruU8yTJmu1nTI6q3s75eULu9hmB5b21L9vQcAYCkjmAUAYBnydl2l7j1/WbRiKR48kXm1v7/MlcvHYj6j02tfnVtYKsG/5bULdu+FMvT4p9V37efzXre2ND12QFZyelHntVK1rn/xgow7W7mLrMjQfTp91y1q3fBStfTdqIlj36jrHBKRfg0f"
		"+LQmjn9L3u6rFR17vK7j1yoePKFz9/zJYk+jpPXP+v/rthGb6Q7IlVemQlJNK5aLS5dYiodOV3Sty7euqC05PVz1HAAAWO0IZgEAWGaae29Q9+73ynDYaxTGg8fV/+AdShas7lyOFvIZXb6Nc24W5szbLEuSEuGZlbkOT1fJ8g+TJ76tiePfnvP+7Re9Ua3rX5I7Dp77mcYOVbZh0Hwkwmc08uTfq3vPHbJSSY0d+WdNHv/Wgt93uRg79MWKSyi4Wy+w1U0t1NL3Atur/lJ6Y6axI/9S9bwC22+zrX6dK5htWf9iuXzrc8eRkYfl9m+T27+t/DVrn2c79nZeWf75UgmNHfn/5Gnfqebe6zNzLP970dx9jX3s7utm7Z8vOv6UwgO/rqjvatLS9/zcZmZZoYHfVj2O09NpO04lwrKSkYqudRR8j5S0Ij4QBACg0QhmAQAow7JSklXitc5SGwaV6idl6rTmbfRkzW9Ovr7fUdfOPy96hTQ2dVQDD96hZLS6FUur4Rmzmnuul3/LzXL7t+vcPX+mePB42b6FK8lieX3bL3xDUWAsSalkpKK5Wclo0XGtz1St0Pk75W7dquj4wfTO7HkmTnxHoYHf1PV+vt4biurW9u9716zXxENz1wxeCPHwGUWGK6vRaTg8s55v2/zKghZLg/s/rumRR6qeV2D7bfaR5qgx61v7HHk7r6z6PoXsNYnz7x9LB7P+bWX71Dp2oakzPyKYLaEw7E6Ezyg2ebjqcQpLbaTiExVf6/QUrLa1koqHF6a0DAAAKxnBLAAAZQTP/FjBMz8uat/w3G/JkfdD6fjTXym7udTa6/5eHv8lueP5bI7SuuFl6rjkfxStlIpNHtHAQx+oKdxbDc8oSZ0732VbpRq46I0afOTDJft6ArsKQmFL02NPSEqvtm1Z9zs1zWGpGDv8pZLtifCZijZSq4a7ZUtBS6qmcHI5aVn/Yrnb7KtTo2MHan5uo2ATvko2/0Kab81zZLpaFuXeyfiUwv3/XfdxTZdfnsCltrZwlZt+5cbKlTVJS8UmK77W4SkMdackK1nTPAAAWM0IZgEAqILh8Mrh7rC1xaZOlO1f+KpoPHy2pvu2bblZHdvenFmdmnfvycPqf/AOpeq4G/ZKfMbp4YdswWxzzzPkad+p6PiBor5N7Ttsx8noWK50QmDbHxWt5AWyDNOl9q2vK2qfOPmdeY1pQzBbsfaL3iiXb9Oi3DsePL4gwWzrhhcXfXAUOveLmsZyuP2242Ss8hWzjoJQNxmfqmkOAACsdgSzAABUwdO+QzLMvBZL0fGDpTsbjqJVRbHJympY5nO3bVPH9ttkKxcgKTbxVDqwrOL100qsxGcM9d+ltonXzKzsNRxqv+D1GnjojuK5FNQGTWSCZm/3dWrufea85rGcuNu2KbD9jzVy4LPUjqxQ+4VvlLPZvilSdPxg7QGd4Sj6oMJKzl5jdmDfe8qea9v8GnVc/Ke549D5X2po/8dtffqe8SW58zaWOn//OxQde6xorMmT39FkQeC87hlfztXWjQdP6Oxv/8h2fs3Vf6umjj2SpGR0WKd/dfOsz4Jivt5n2Y7jwZOKTjxZ01hFwWwV32eLVtvW+b9DAACsFgSzAABUwdN2ke04GRsv+3q9p22bZMysdkvFp2p6Fd9wNKkwsIxOPKmBB+9QKl75q6eVWqnPOHHs39Wz96O5Y2/3VWrq3Fv0irmrxb7CLjZ1TIbpUsf2N0sytRq0rH+xOi7+U5lOn7p2v1f9979zsae05LlbL1DbplcUtFqaOPb1msc0XW1FbankdM3jOb1rbMeJ6aGaxyrF4e3J/ToZrW2Fe/tFb1JTYFc60B66X9Gx4lXttbKS0wtWCsIwXZnvYwvH7d9etInbfOpCF/79SkXHK762eLVt/f9bBADAakAwCwBAFVwtm23HiVk2O3G12vsm6xiChM7ftSChrLRynzE88FtFx5+Qpz1bn9FU+wV/oP78YNZwyOXbYLsuOvGU"
		"AttvK/p9WUocng7bKse5xKaOlw3QWze+XJ073q5sUN4U2KOOi/9Uo0/9Yz2mumJZqbiiE4dzK0IlaXrkEYUH76l5TNPhLXGf2VfMzsbdstF2PNsGeNXy+C+R6Zyp5xoPn65pHIe7XU0de9XUsVfezit07t4/K9t39Kl/1PjTX8sdW6lo2b6SNP70VzVx/Bs1zWsu/gter8BFt5Y937/vPTLMmR+9ann1v23jy5X/AZaViowY8MEAACAASURBVGrq1A+qHifLdNuD2WSs8mDWdNmD2VQV1wIAgBkEswAAVMHlW287jodOzdLXHvAlMnVKl7qV/IwTx76hnss/ljtu6rhM3u5rFBm6X5Lk7bpKhi0Ms2Qlo2rb+DLbOInwWcVDZ+Qt2B19sTR17FX3ng9U3H/4sU8peO5nJc+Fzv9KbZteafuzbdv0Sk2PPaHwwK/nPdeVKh46pf4H3in/ltfKf8EfyDDdGjv8z/Ma03SWCGaTs4ePZcdytcntvzh/IEWGH65qDE9gl/xbblZyekgjBz9vO1e4Kd50jStdDXNm1amVSszaNxkdVTI6WtN9Gi0RKf8BVyVMV5uae2+wtSWjY7YPAsqJTR1VPHjC1maYHplOn32OVbzt4JhHqAsAAGYQzAIAUAWXz14/MjZ5rHzfglqT8VlWni4lK/kZw4N3Kzp+QJ72nZkWQ+1bf38mmO283NY/ETkvh6dDMuz/l2nsyFfU0vc8W1v7BW9Q+wVvqHpOrRtuUuuGm+bsd+Knz5uzTz2k4pMaPvBprbnyf828mm041Hnp7YpNHqbe7Bwmjn9T4cH75O2+Klf70zA9sqxE1bvWG47ijebmWhVaTtum37MFcdHJwxWXHWnuuV5tm1+VCQHNooDedAfkWzvz9zMZG6ugrm7psiCmszn361Sq9rINK03hn5+ULk1RyQcy40e+ovGCYDb9AZy9fExyurK/D6arreADLC2bgBwAgKVmdRRKAwCgDlwtW4pe3yy7KZZKrTw9syDzqqfV8IwTx79lO/YEdqm55/rMry+1nYtOHNbU6R8oGR3JtUVGHlTo/J0LP9FFFB07oLGnv2Jrc7gD6tr13sWZ0DITD53U5In/kCQ5m/u09prPqXPH7VWPY5jFwWwtNWZdvo2Z1+BnBE//uPQ9CzYb69r5LvVc/nE1dexV9kcHIy88laTuPXfY6pWGzt81Zy1X09Va9L1Gstd4rnV18IpjONSy7kV1HdLp7S1qq/SDtcLv+xLBLAAAtWLFLAAAFWrq2G07thKh8rthGw45vWttTbGppxdqanWzGp4xXWv2oDztO3Jt/q23KDK6X+7MjvJZ0bEDslJxhc7dqbYtN8tKhjX65OqotTp5/FtqCuzOhdaS1NSxR4Htb9HYoS8u4syWj6bOK9S9+71yeLrk9m9TbOqopk59r+LrS66YTVQXzJrOFnXv+YBMdyDXlgif09SZHxX19XZfJ5fPXoe2qFxJ5LwiQw9kJ6juXe+Rt/PK3PlkdFgTx/695FwsayasNUyX1lz5SUVGH5WViskw3XK3Xmi7X/4HIvXQtunl8vXdWNcxsxyu1gUZV0qXEincuG2+HJ4u27GViitZYSkaZ1NPUdtSL2MDAMBSRTALAECFCjdXigVPlu3bFNhl26HbSsUUHU8HnL1XfFJmFT/EO5u6ito6Lv5T+dY8u+IxYpOHi2pClrIanlGSJk58Wz2XfSh37GnfqcBFbypYoZhSeOi+TP//UOvGl2ni2Ddn3TCpktf8SwUs8y0PEBnep/P3vbXkOYenUz17P1LTuCNPfFaetgvlyAti/Jtfo+j4U9SbnYPD06WevR/Oe/3cUMf22xQPntD06P6KxigVzKaSkYrn4PT2qeeyv5K77aKZRiuukSe/UHwv06Xu3e+RjFIv1KUUHTugqTM/VvBsujaxq2Wzuna+2/YBh6ykRg7+XdkSCYlwv9Q5c+z2X2yve1sgNnl01uerlqOpV46m4pWiS5rhKFrtXA8O"
		"T4ft2EqE7H+WsyjVz3B4itqTsUklwkv/LQoAABYTwSwAABUqXE0Zmypfe7XwB9RE+FyuvqSn/eKSr/BWq9IfoiXN+Vpx1mp4RkkK9/+3YhNP2UKhtk2vsvWJB0+mn0npVYBjh7+kyZP/p+yY40e/qvEjX5nz3h2XvFVtm16ZO546/QONPPG3Fc+9lFR8smzJicKwvRrJ6KhGDn4+HexmX3E3HOq69HadmzpG6DKLZHRYwdM/UtuWm3NthqNJXbveq/P3vb2i+q6G6SpqS8WnKrp/c++z1LnjrUUrIyeOf1uRzAcO+axUXNGJQ/J2XT3TlgwrPHC3Jk9+N69erkv+C14v/6ZXybDVPE1p7PCXFR74Tdk5Bc/8RC19z7d9oFNOInxGwXO/mLPfSuffeouczX1F7VZyWid//uKS13TufJda179k1nEdnnbbselu19priwP7Sq256jNFbaHzv9LQ/o+V6A0AALIIZgEAqIThkLt1i62p7Cv+kjz+bbbj5VB7dVU8Y56JE/+h7j1/WfZ84arG2ULZlSw8eI8mT31fbZtekWsz3QF1736fzt9/e9UbWi1V/i23yNt9TUV93S1b5u4kafTQP8nTfok8gV25Nqd3jbp3v0/9+9495/WG6S5oScmaY8Ws6Q6oY/ttaul7ftHq16nTP9TY4S+XvXbq1Pfl7bpaifA5Bc/9QpOnvqdUbMzWx+2/WG0bbrKFslYqqtEn/1FTp78/69yiE09q4KH3q23T78nZvK74+VJxpRJBRSef1sSxb8z5rHOZOvV9me75f0BUi2R0bO5OczDdAfk3v7oOsynmcLfP3WmeLDZvAwBgTgSzAABUwNuxt2AX6pQiww+V7e9uvdB2HJuq7yu5C2E1PGO+0Plfyr/ltfbXvPOEB+9p8IyWrrFD/6Smjl22P3NP+w51bL9No0/9wyLOrH487TuqWqFdqaHH/6fWXvt3cuTVeG3qvFyB7bdp7ND/nvVas6CUQSWrwr2dl8u39rn2UNZKavzY1zV+5F9mvTY8eI8GH/7LWf/uR8ceV/++96j7sg/K5dugRPichg/8jaZHH51zbpI0PfpoxX3na7l/mNKx/Y/r8uZBKQs1br5UlfWQAQBYjQhmAQCogCew03YcD50uu1GKy7dRzmb7pljZ2qtS+hX5auqvOjxdJfvPVuu0UCIy927bq+EZC02e/E917XpfUXtyelCR4QerHm+lslJxjRz4W625+m9s4X3bpldoevRRQuxZJMLnNHrw79Krs/PCUv/mVys6fnDWV//tNY8lK5WY836h83fKdDSp89LbJcOZK0dRaU3gSv4sY1NH1f/AO9W28RWaOP5NpRLBisZG5bzd16ml7wULNr7D3bZgY2dZSYJZAADmQjALAEAFPO2X2I6jE4fK9m3ufYYkI3dsJac1PfpI7vj8/e+o7t6B3Vp7zedsbaNP/aMmT3y7qnHmvM8qeMZCwbM/k3/LLXK1bLa1h4f3Leh9l6PoxJMaP/p1BbbdmmtLJYIztWdRVqj/LnkCu2zlIGQ41LnjdsUmj5b9UKGoxmyFdZSnzvxIMkx5u660bcbl8V+its2vnOPq6nReevu8ro8MP6zg2Z/UaTYrR8e2W23/blnJsCwrJdPZUpfxz/721rk7VWndDf8il29T7jiVDNf9HgAArDQEswAAVMDTZn9tPxkdLdu3qXOv7TgWPF7VxlSLZTU8Yynx0OmiYDYR6V+cySxxE8f+Td6uK9XUsUexqac1tP8TigdPLPa06seae0WqJMmo/v9Cjz71D2oKXCp320xtZoenQ127/0L997+z9G0KarBalc5P6U3lpk7/wNbm9PbKt/bGKma98FKJcF2D2Z7LPiRX6+a6jVcPqfiUzt/39qquSUwPyZW3GePkie/I1/f8ugWzC8G0lcKRrGR0kWYCAMDyQTALAMAcPIGdMvPqQ0qSf8vNMh1ejTz5BdvmRw5Ph5ryNvqR7K/4L1Wr4RnLcTR1FbU1d1+j"
		"iaP/tgizWfpGnvhbtW15jUYPfkFWauUEL4OPfFDhgd9W1Le595nq2fvR6m5gJTX8+Ke15prPyczbOKspsEftF/1hyfqvhqMgmE3FqrvnKuRsXmtbtbkUpOITVV8THrw7txldfOqYxp7+V/n6nl/vqdWVURTMUsoAAIC5EMwCADAHh8svKxUveK3YVOvGl8vdukWD+z+Rq8Xasu5FRXUhI4P3NXC2tVkNz1iKp32HPP6LS7TvVHPP9VXVTvVvepWc3jVz9mspCFdaN9wko2CTp1KGH/tUxXNZSPHQKY0c+MxiT2NZik0d1fiRr6jjkv9ha/dvuUXTI4/ayoFIxaUMKqkxi5UheO5OBbbfJsN0avjg52wfji1VprPJdpxKRBZpJgAALB8EswAAzCE8eLcGH/mguna+Sw6PfXWlJ7Bbfdd+XkOPfUrTo4/K13uD7XxyekiRkaW/idRqeMZSWje8TPm1cvP5t9xSVTBrOH01b9ZTyXVLJZitRlGNVGtx5rGUTJ78T3m7r5K36+pcm2G61NL3/KJgVkXBbP3LhYwf+Yri4bN1H7ec7t3va2hdYisVlxq90th0Fn14VS0rGdH0yMOKB08qOnagThNbOKazRTIK/r6yYhYAgDkRzAIAUIHI0P06f9/b1L37A/IEdtrOOZp61HvFJxQ8+1O5/dvt1xUGLUvYanjGfE5vr3xrbih73hPYKW/3dYoM3dvAWa0spsteD9NaBqv+GmHkic+p7/p/lOnyy0pOa/zYv5UsnWEW1JitdPOvakQnjzT073j37vc27F6SFB74tYb2/3VD7+nfcosC298y73HGn/6aYlNP12FGC69USRg2/wIAYG4EswAAVCgRGdD5B96pzh23q3XDS5S/0tJwNKl14+8VXRM8+18NnOH8rYZnzPJvfV1RTcTiPjcTzM5DUa1Pa3luEFdviUi/xo58Ra0bXqqRA59VdKJMjeYGrJithrfranXteretbXp0f8ODz4VkutqKSpIkwueUSgQbPpflEspKkqOgRrmU3tgNAADMjmAWAIBqWEmNPPFZxYMnFNj+5llfV41NHtH0yDJcTboKntHh6SranT468aQig/er/aI35dqaArvl7bpKkeF9c44ZmzqqUP9dc/ZrXfe7cjb35Y6T04OaPP2Diue+XDSvebaae66xtaXijQ+3lqqpU9/T1Okfzlo71DCX1uZfrpZNRaVODEdTmd7LU9umV6r9wjfY2s7f91ZFxw8u0ozqp/PSd8jTfmldxxx69OOKh07K4fYXnUvFQ3W9FwAAKxHBLAAANZg8+R3FQ6fUteu9cng6S/aJjDzU4FnV10p+Rv/WW2Q6fba2qVM/VKj/LrVuenne6i9D/q23VBTMhgfvLvk6eiGHO6C2Ta+cuW7o/oquWwoC226V6Wore94wHDJdLXJ618rddqEK6/cmMhvIIWOO0g6Gaf+/6ou9YtZZ4vtAKj65CDNZOOm/tzOsZHhFhLKS5Gzuk7v1grqOmd24sLBsibTy/m4AALAQCGYBAKhRZPhB9T/w51p3w7+WPN+6/iWKTRyuaBXlUrUSn9HpXaOWdS+0tcWDJxQ8+xNJUvDMT+Xf+rrcuaaOy9TUuXdZrgyut6bAbnkCu2q+PjpxuI6zWfmW2orZUnVEk9OjizCThWGYLjUF7CtKY5NHF2k2y4vpLAxmUwSzAABUwFzsCQAAsLyZspKR0mdcrere8375L3h9g+dUbyvrGQPb/rhotezkif/M/Xri+LcKAgVD/i2v03Ll9PYWtZmu1prGik0dq3keVjKiqVPfr/n61chYYjVm3a1bi9piwRONn8gCaVn3Ipku+yv5keEHF2k2y0vh91QrubgfIgAAsFywYhYAgBoZpktdu/5i9g2kDKcCF90qt2+Thg98etGDlWqttGds6tgj35pn29riwROaOvOj3HEqPqHguV/Yyg14uy6XJ7BL0bHHGzbXemjq3KuuXe8pavdvfa1C/f+tZHS4qvGiE4dV"
		"S6Sbik9q5ODnFQ+dquHq1atoxewihl3e7mvkatlsb7Timh7dvyjzWQhtm15hO7ZScQXP3blIs6m/0Pm7FJus74ZiyWh6xbThtP83wkpF63ofAABWKoJZAABq1LXrPfK076ior6/v+XI2r9Xgox9TchnV2VxpzxjYdqtkOGxt4yXqu04c+4Za1/9uXiBtqn3rLRp4aPkEs96uK9W9569Kro51eLrUvecO9T/wrqrGnB59WMnY2OydrKSsVFyp+JQSkUFFJ57U1JmfKjXXdShSvGJ2ccIu09mijov/pKg9MT1Sdbi/VHVs/5Oi4NkwXWpZ/wKFzv1yRXyoEDzz4wUb2ywMZpMEswAAVIJgFgCAGgS23Srf2huL2ieOfV3R8SfVtesvijZJ8rRfqrXXfE5D+z+h6PiBRk21ZivtGVs3vlye9p22tujY4wqdL14Rl4wOK3T+V2pZ/+Jcm7frarn92xWbOLTgc50vb9dV6r7sg0WvF+dr6tirrt3v0/Bjn6p43ERkQKd/+ap6TBEVMBxNtuPFWDFrugPq3fshuXybis45vWu0/tn/rumRhxQa+K0iQ/c1fH714N/yWrVteXXJc+0XvEHtF/yBYpNHFRl+UKHzv1RsirqzhQzT/nc1lZxepJkAALC8EMwCAFCl9gv+H/m3/n5Re2TkQY0d/rIkqX/fkHou+6CczX22Pk7vGvVe+UmNPPG5koFgKYZhzN2pzlbaMzq9fQpc9If2RiulsSNfKXvN+LFvyNd3owwzveu4DIfat75Og498eMHmWQ/e7mvVvecDJWo+TktWUkZee0vfC2Q6mjT0+KdlJUKNnirm4HDb653Od/OvRHRYkaF7bW3JWPnNu5p7n6WOi98ip7evbB+nt1ct61+slvUvVjI6ounRRxUevEeh/l9LVrKof3jwPhnmzKr1+dQtLiUVDyoVn5g5ToTL9jUcXnVc/Kdq3fASSbN9DzLlbrtI7raL5N96i2JTxzIh7a8Umyze0C4Zn1A8dDJvTlO1PMqsDEeTNr/wZ2VONn4bkcKV+dYsv+8AAGAGwSwAAFXwX/B6tV/0JhX+EJ8In9PQ/k/mjmOTh3X+/neo5/KPyOO/xNbXdPrUuePtiow8XNHr3cW7XS+slfiMnZe+vWh1b6j/Lk2PPlL2mkT4jMIDd8u39nm5tuae6+Rq2aJ48HhRf5dvo7xdV805F1fzuoLj9RVdJ0mpREjR8YNlzzf3XK/uPe+X4Wi2tVupqIYf/18yHB517foL5e//2tz7LK1r266xI/+s0LlfVDSPlahn70fnLtOQYRSUw1gITZ17i2o7p8pswlep6NgBDTz0gTn7Nfdcr7ZNr1RT517NHljaOTyd8q29Ub61N6pzx6SmR/enQ9rzd+XKMAw+8sFap1+R/n3vrqhf68aXy7/lNWVD52R0RA5PZ4kzhtytF8jdeoH8W25WPHhSkeF9Cp67MxfSBs/8RMEzP6n1ESpnLJ0f5Qo3GUwuQBgNAMBKtHT+aw4AwBIX2P4W+be8VoVBRTI2psFHP1oUQCajw+p/4N3queyv5O2+Nu9MSmOHv1RxzU1320XFjSVWotXDSnzG1o0vl7fraltbKj6p0UNfnPPaiWPfSG8WlgviTHk7LysZzPrWPEe+Nc+pen5NnXszAdjcYpOHde6e4lqfktTc84xMKFu4CU9Mw4//jUL9d0lKr2huv/CNtj5Ob6+6d79f7Vt/X+Gh+xTu/42iE09W/SzLncMdWOwp5LRvfX1RWzI6vmD3S698/V35em+Qq2VL2X7R8YMafvzTau59hpp7rpfHf3HJFZqmq03NvTeoufcGdV7yVk2PPa7w0P0KnbtTqURwwZ5jNs7mPrWuf7F8a58np3dNyT7x0EkNPvxBxUOn5e26Sr61N8rbdaUcno4SvQ25WjbL1bJZbZtfnV5JO/SAgud+rnjwxII+y1LiatlS9IFTdlMwAAAwO4JZAAAq0LXz3bZ6o1mp+JQGH/lIyddZJclK"
		"RjTw0PvVeemfq3XDSyVJkye/q6nTP6zovs29z1TbxpuK2hdiNdJKfcbmnmcUtU0c/2ZFG5TFpp5WeOh+Nfdcr9jkEY0d+pIiIw/WZV711Nz7LHXvfm+JUDaukSc+ayspMf70v8owXfJvfZ0KA3hXy2b5WzbLv+UWpRIhJaPDSsbGZSXCspIxWVZSkrWgzxIPntL40a8t6D2WJMMhV/N6eQI71Lr+JSU33YtNHqnrLT3tO9Xcc628nZenPxyZbSWwldDU6R9p9Kl/kpWKauLYSU0c+7pcvk1qWf/C9GryEnVoJclw+uTtvlbe7mvVsf02TY8fUGRon0Ln71zwAM/VskW+NTfI23lF+vd0lmcMD96j4QOfyX2gFBnep8jwPknZD16eraauK8qs8M9bSbv1tYpNHFF46H4Fz/1MifC5hXi0JcF0B9S1811F4Xw8dHqRZgQAwPJCMAsAwCxMZ4u693xA3u5ris6lEiENPfZJRccem3OckSc+q+T0sDyBHRp98gu59p69Hy65Os0w3XK4W4teSc+KTxWv2KzVSn/G8aNfk7dzby6QiU0c0sSxf6/4+onj31R07IAmjn+jLvNZCM091xaFsrLiGjn4/yp4trgO5djhLysZHVNg261FG0xlmU6fTKevbNi2UErNdyVo3XCTOne8rXwHw6HZygYkpwcVm3q69gkYDnk79soTuFQe/8Vy+7dVvEJ4emy/xo98tWTpj3jopMYOfVFjh74oT2C3Wvqer+aea+XwdJWZRpO8nVfK23mlAtv+WNHxJzKrTH+hZHS49ufL8LTvUFNglzztl8jddqGc3rWaqxxDInxO40f/TcGz5csPhPrvUqj/LhkOr1r6fkfNa56ppsAeGaarRG9Tbv92uf3b1X7B6xWdOKTw0L0Knv15RR8IVer8/e+oqF8i0l/VuE0dl8nlW1/2vGG6Zbra5PKtk7frqqIyMZIUGXmoqnsCALBaEcwCAFCGq2Wzei77oFwtm4vOpeKTGnz0Y5qu4ofP8aNfLR4nMS2Xb0NV80pE+ucX0ORZDc8YHXtcofO/lK/vd2SlYhp58u+qvj469nhFfUuVOKineOhsyfbg2Z+rZd2LZhqspEYOfkHBMz8uO9bkyf/U9Oh+BbbdKm/31aqmluhCWqwd7+Oh04qHTtV2sZWas0t04tC8aoIGq6j/a5guedp3yuPfLlfrZrl9m+Rq2VQ2hC/JSigy+qimTn5X4cF7KrokOvaYomOPaeSJzArTtc9RU+cVRRvR5c+zqeMyNXVcpvaL/lDR8YMKD96j4LlfzFkGxXT51dSxS+6WrXK1bJCreZ1cvg22ze3mEg+d0tSZH2vq5P+RlYpXdI2VjGjq9Pc1dfr7cni61Lr+d9Xc+4xMOZYS/w4ZDnnad8jTvkOBC9+g6fGDCg+mQ9pKS72Unsd0RR+Y1cLbdVVmRX1t4sHjik0cquOMAABYuQhmAQAoI7Dt1pKBZTI2psFHPqTo2IF53yM2eVha94IqrrA0cezr875v1mp4RkkaPfzP8nZfq6kzP5p186z5GD/6VY0f+cqCjD2X6dFHFA+dksu3MR3KPvn3mjr9gzmvi009rYGH7pC3+xq19L1A3s7LZC5yndXo+OLUth07/CWFB367YOPHJg8rlQjWtNHd9OijGjv85Yr7B7a9WW2bX131faR0QB0Zul9Tp39Ye1CtghWm614gX++z5AnsLLPCNBvS7lFTxx4Ftv2RBh76y1k/FHK3blXP3o9WPS8rGVZk+CEFz/183n/eyeiwxo9+TeNHvyZ364VqWf8iNfdcl1mlW4LhUlNgj5oCexS46A818sTfLskV4vMqmWGlNH60vt+/AQBYyQhmAQAoY/jxT6v3yg55/Jfk2uKh0xp69KN1W9UXGXm44r6pREgTx79Rce3WSqyGZ5TSr4GPHPx8bgOslSg8eI/8m9dr9Kl/0tSp71Z1bWTofkWG7pckebuvkadtu5zNfXJ4AjKdPhkOjwzTKal4"
		"k6d6slIxRcfn/2HAUpUInZHbf3HF/VOJoKZO/1DjR/61qvtMnvyOWjfeJMP0zN3ZSig2dUzTo48p1P/ruv/+W8mIpk59T1OnvidHU49a179IzT3PkLvtQpVbpZ2KT2l69NFZx50efUTx4ImSHywVSsbGFB07kN587PyvZCUjNTzJ7GJTT2v0yS9o9MkvyNt9nVr6bsy85t9a+gLLUmS48u+NjRQZ3S8ppWr/fbeSEY0d+RdbTWsAADA7glkAAMpIxSc18OD7tebKT8nt367o2OMafPQjdd2sJh48ofDgPWVe9U3JSkaVjI4pFjyu0Pm76lKD0XaHVfCMWSs9LAie+amS0TFNnvzPeY2TH9KivoLnfiFPuHQ5CkmyUklZyUjm34djigztk5WKVn2fRKRf4YG75Vv7vFI3UTx8VrHJw5oeO6DwwN0L9u9coeT0oMaf/qrGn/6q3P7tal2XXmHqaOqx9QsP/FayknOOFx64W/5SZVgSIcWnjml6/AlFhvdpeqS4Nu5Cigzdq8jQvTJMj1rWvyi9aVhgp62URWT4gap/3wcefK8M0y1JmY34FkYqNqbY1HE5C/5cCnrJSsaUjE8qOT2o6PghBc/9lxKRgQWbFwAAK5Fx/CfPXditdQEAWOZMd0D+La/R+JF/qbgO4XKzGp6x3joueZu8nZfljidPfV9Tp763iDNCNQLbbs1tCCdJwTP/pXjo5CLOqL48/ku09tq/UyoRVDx4SrHgMUUnDikytK9hQWylmntvSK8w7bxChsOrc/fcVtGKfad3jdbd8K9KRkcVD55QbPKIpsceU2TkkYqC3UZyNvepdf1L1Nz7TLl8GzTw4HsVGd632NMCAACLjGAWAAAAWIFcvo3zqhPbaKazRd7uqxQ6/6vKr3G1KRWfXMBZ1Z8nsLMu9bsBAMDyRzALAAAAAAAAAA22sDs4AAAAAAAAAACKEMwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECD39nGUQAAArtJREFUEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMR"
		"zAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECDEcwCAAAAAAAAQIMRzAIAAAAAAABAgxHMAgAAAAAAAECD/V/9MPXG3HkXoQAAAABJRU5ErkJggg==";
	auto EnsureSponsorQrTexture = [&]() -> bool {
		if(s_SponsorQrTextureReady && s_SponsorQrTexture.IsValid())
			return true;
		if(s_SponsorQrTextureTried)
			return false;
		s_SponsorQrTextureTried = true;

		if(s_pSponsorQrPngBase64[0] == '\0')
			return false;

		const char *pPayload = s_pSponsorQrPngBase64;
		if(const char *pComma = str_find(pPayload, ","))
			pPayload = pComma + 1;

		std::string CleanBase64;
		CleanBase64.reserve(str_length(pPayload));
		for(const char *pChar = pPayload; *pChar != '\0'; ++pChar)
		{
			if(*pChar == ' ' || *pChar == '\t' || *pChar == '\r' || *pChar == '\n')
				continue;
			CleanBase64.push_back(*pChar);
		}
		if(CleanBase64.empty())
			return false;

		const int MaxDecodedSize = static_cast<int>((CleanBase64.size() * 3) / 4 + 4);
		std::vector<uint8_t> vDecoded(MaxDecodedSize);
		const int DecodedSize = str_base64_decode(vDecoded.data(), MaxDecodedSize, CleanBase64.c_str());
		if(DecodedSize <= 0)
		{
			s_SponsorQrDecodeFailed = true;
			return false;
		}
		vDecoded.resize(DecodedSize);

		CImageInfo QrImage;
		if(!Graphics()->LoadPng(QrImage, vDecoded.data(), vDecoded.size(), "qmclient_sponsor_qr_base64"))
		{
			s_SponsorQrDecodeFailed = true;
			return false;
		}

		s_SponsorQrTexture = Graphics()->LoadTextureRawMove(QrImage, 0, "qmclient_sponsor_qr");
		s_SponsorQrTextureReady = s_SponsorQrTexture.IsValid();
		s_SponsorQrDecodeFailed = !s_SponsorQrTextureReady;
		return s_SponsorQrTextureReady;
	};
	auto DoKeyBindRow = [&](CUIRect &Content, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pLabel, const char *pCommand) {
		CBindSlot Bind(KEY_UNKNOWN, KeyModifier::NONE);
		const auto CurrentBindIt = CommandBindCache.find(pCommand);
		if(CurrentBindIt != CommandBindCache.end())
			Bind = CurrentBindIt->second;

		CUIRect BindRow, BindLabel, BindKey;
		Content.HSplitTop(LG_LineHeight, &BindRow, &Content);
		BindRow.VSplitLeft(LG_LabelWidth, &BindLabel, &BindKey);
		Ui()->DoLabel(&BindLabel, pLabel, LG_BodySize, TEXTALIGN_ML);

		const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&ReaderButton, &ClearButton, &BindKey, Bind, false);
		if(Result.m_Bind != Bind)
		{
			if(Bind.m_Key != KEY_UNKNOWN)
				GameClient()->m_Binds.Bind(Bind.m_Key, "", false, Bind.m_ModifierMask);
			if(Result.m_Bind.m_Key != KEY_UNKNOWN)
			{
				GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
				CommandBindCache.insert_or_assign(std::string(pCommand), Result.m_Bind);
			}
			else
			{
				CommandBindCache.erase(pCommand);
			}
		}

		Content.HSplitTop(LG_LineSpacing, nullptr, &Content);
	};

	enum class EQmModuleId
	{
		Info,
		ChatBubble,
		GoresActor,
		Gores,
		KeyBinds,
		MiniFeatures,
		CameraView,
		DummyMiniView,
		Coords,
		Streamer,
		FriendNotify,
		BlockWords,
		Translate,
		QiaFen,
		PieMenu,
		EntityOverlay,
		Laser,
		PlayerStats,
		CollisionHitbox,
		FavoriteMaps,
		HJAssist,
		InputOverlay,
		Voice,
		DynamicIsland,
		SystemMediaControls,
	};

	enum class EQmModuleColumn
	{
		Full,
		Left,
		Right,
	};

	struct SQmModuleEntry
	{
		EQmModuleId m_Id;
		EQmModuleColumn m_Column;
		int m_OrderInColumn;
		const char *m_pKey;
	};

	constexpr size_t kQmModuleCount = 25;

	// Layout string format: key:column:order; entries separated by ';'.
	static const std::array<SQmModuleEntry, kQmModuleCount> s_aQmModuleDefaults = {{
		{EQmModuleId::Info, EQmModuleColumn::Full, 0, "info"},
		{EQmModuleId::ChatBubble, EQmModuleColumn::Left, 0, "chat_bubble"},
		{EQmModuleId::GoresActor, EQmModuleColumn::Left, 1, "gores_actor"},
		{EQmModuleId::Gores, EQmModuleColumn::Left, 2, "gores"},
		{EQmModuleId::KeyBinds, EQmModuleColumn::Left, 3, "key_binds"},
		{EQmModuleId::MiniFeatures, EQmModuleColumn::Left, 4, "mini_features"},
		{EQmModuleId::Coords, EQmModuleColumn::Left, 5, "coords"},
		{EQmModuleId::Streamer, EQmModuleColumn::Left, 6, "streamer"},
		{EQmModuleId::FriendNotify, EQmModuleColumn::Left, 7, "friend_notify"},
		{EQmModuleId::BlockWords, EQmModuleColumn::Left, 8, "block_words"},
		{EQmModuleId::Translate, EQmModuleColumn::Left, 11, "translate"},
		{EQmModuleId::QiaFen, EQmModuleColumn::Left, 9, "qiafen"},
		{EQmModuleId::PieMenu, EQmModuleColumn::Left, 10, "pie_menu"},
		{EQmModuleId::CameraView, EQmModuleColumn::Right, 0, "camera_view"},
		{EQmModuleId::EntityOverlay, EQmModuleColumn::Right, 1, "entity_overlay"},
		{EQmModuleId::Laser, EQmModuleColumn::Right, 2, "laser"},
		{EQmModuleId::PlayerStats, EQmModuleColumn::Right, 3, "player_stats"},
		{EQmModuleId::CollisionHitbox, EQmModuleColumn::Right, 4, "collision_hitbox"},
		{EQmModuleId::FavoriteMaps, EQmModuleColumn::Right, 5, "favorite_maps"},
		{EQmModuleId::HJAssist, EQmModuleColumn::Right, 6, "hj_assist"},
		{EQmModuleId::InputOverlay, EQmModuleColumn::Right, 7, "input_overlay"},
		{EQmModuleId::Voice, EQmModuleColumn::Right, 8, "voice"},
		{EQmModuleId::DummyMiniView, EQmModuleColumn::Right, 9, "dummy_miniview"},
		{EQmModuleId::DynamicIsland, EQmModuleColumn::Right, 10, "dynamic_island"},
		{EQmModuleId::SystemMediaControls, EQmModuleColumn::Right, 11, "system_media_controls"}
	}};

	static std::array<SQmModuleEntry, kQmModuleCount> s_aQmModuleLayout = s_aQmModuleDefaults;
	static char s_aQmModuleLayoutConfigCache[sizeof(g_Config.m_QmSidebarCardOrder)] = {};
	static bool s_QmModuleLayoutInitialized = false;
	static bool s_QmModuleColumnCacheDirty = true;
	static std::vector<const SQmModuleEntry *> s_vCachedFullModules;
	static std::vector<const SQmModuleEntry *> s_vCachedLeftModules;
	static std::vector<const SQmModuleEntry *> s_vCachedRightModules;
	static std::array<bool, kQmModuleCount> s_aQmModuleCollapsed = {};
	static std::array<int, kQmModuleCount> s_aQmModuleUsage = {};
	static std::array<float, kQmModuleCount> s_aQmModuleLastHeights = {};
	static char s_aQmModuleCollapsedConfigCache[sizeof(g_Config.m_QmSidebarCardCollapsed)] = {};
	static char s_aQmModuleUsageConfigCache[sizeof(g_Config.m_QmSidebarCardUsage)] = {};
	static bool s_QmModuleCollapsedInitialized = false;
	static bool s_QmModuleUsageInitialized = false;

	auto QmModuleColumnToString = [](EQmModuleColumn Column) -> const char * {
		switch(Column)
		{
		case EQmModuleColumn::Full:
			return "full";
		case EQmModuleColumn::Left:
			return "left";
		case EQmModuleColumn::Right:
			return "right";
		}
		return "left";
	};

	auto ParseQmModuleColumn = [](const char *pValue, EQmModuleColumn *pOut) -> bool {
		if(str_comp_nocase(pValue, "full") == 0)
		{
			*pOut = EQmModuleColumn::Full;
			return true;
		}
		if(str_comp_nocase(pValue, "left") == 0)
		{
			*pOut = EQmModuleColumn::Left;
			return true;
		}
		if(str_comp_nocase(pValue, "right") == 0)
		{
			*pOut = EQmModuleColumn::Right;
			return true;
		}
		int ColumnValue = 0;
		if(str_toint(pValue, &ColumnValue))
		{
			if(ColumnValue == 0)
			{
				*pOut = EQmModuleColumn::Full;
				return true;
			}
			if(ColumnValue == 1)
			{
				*pOut = EQmModuleColumn::Left;
				return true;
			}
			if(ColumnValue == 2)
			{
				*pOut = EQmModuleColumn::Right;
				return true;
			}
		}
		return false;
	};

	auto FindQmModuleIndex = [&](const char *pKey) -> int {
		for(size_t i = 0; i < s_aQmModuleDefaults.size(); ++i)
		{
			if(str_comp(s_aQmModuleDefaults[i].m_pKey, pKey) == 0)
				return static_cast<int>(i);
		}
		return -1;
	};

	auto GetQmModuleIndexById = [](EQmModuleId Id) -> int {
		return std::clamp(static_cast<int>(Id), 0, static_cast<int>(kQmModuleCount) - 1);
	};

	auto ParseQmModuleCollapsed = [&](const char *pConfig) -> bool {
		if(!pConfig || pConfig[0] == '\0')
			return false;

		bool AnyParsed = false;
		char aEntry[128];
		const char *pEntry = pConfig;
		while((pEntry = str_next_token(pEntry, ";", aEntry, sizeof(aEntry))))
		{
			if(aEntry[0] == '\0')
				continue;

			char aKey[64];
			str_next_token(aEntry, ":", aKey, sizeof(aKey));
			if(aKey[0] == '\0')
				continue;

			const int Index = FindQmModuleIndex(aKey);
			if(Index < 0)
				continue;

			s_aQmModuleCollapsed[Index] = true;
			AnyParsed = true;
		}

		return AnyParsed;
	};

	auto SerializeQmModuleCollapsed = [&](char *pOut, int OutSize) {
		pOut[0] = '\0';
		bool First = true;
		for(const auto &Entry : s_aQmModuleDefaults)
		{
			const int Index = GetQmModuleIndexById(Entry.m_Id);
			if(!s_aQmModuleCollapsed[Index])
				continue;

			if(!First)
				str_append(pOut, ";", OutSize);
			str_append(pOut, Entry.m_pKey, OutSize);
			First = false;
		}
	};

	auto PersistQmModuleCollapsed = [&]() {
		char aSerialized[sizeof(g_Config.m_QmSidebarCardCollapsed)];
		SerializeQmModuleCollapsed(aSerialized, sizeof(aSerialized));
		if(str_comp(aSerialized, g_Config.m_QmSidebarCardCollapsed) != 0)
			str_copy(g_Config.m_QmSidebarCardCollapsed, aSerialized, sizeof(g_Config.m_QmSidebarCardCollapsed));
		str_copy(s_aQmModuleCollapsedConfigCache, g_Config.m_QmSidebarCardCollapsed, sizeof(s_aQmModuleCollapsedConfigCache));
	};

	auto SyncQmModuleCollapsed = [&]() {
		const bool ConfigChanged = !s_QmModuleCollapsedInitialized || str_comp(s_aQmModuleCollapsedConfigCache, g_Config.m_QmSidebarCardCollapsed) != 0;
		if(ConfigChanged)
		{
			s_aQmModuleCollapsed.fill(false);
			ParseQmModuleCollapsed(g_Config.m_QmSidebarCardCollapsed);
			s_QmModuleCollapsedInitialized = true;
		}

		PersistQmModuleCollapsed();
	};

	auto ParseQmModuleUsage = [&](const char *pConfig) -> bool {
		if(!pConfig || pConfig[0] == '\0')
			return false;

		bool AnyParsed = false;
		char aEntry[128];
		const char *pEntry = pConfig;
		while((pEntry = str_next_token(pEntry, ";", aEntry, sizeof(aEntry))))
		{
			if(aEntry[0] == '\0')
				continue;

			char aKey[64];
			char aCount[32] = "";
			const char *pField = str_next_token(aEntry, ":", aKey, sizeof(aKey));
			if(aKey[0] == '\0' || pField == nullptr)
				continue;

			str_next_token(pField, ":", aCount, sizeof(aCount));
			if(aCount[0] == '\0')
				continue;

			const int Index = FindQmModuleIndex(aKey);
			if(Index < 0)
				continue;

			int ParsedCount = 0;
			if(!str_toint(aCount, &ParsedCount) || ParsedCount < 0)
				continue;

			s_aQmModuleUsage[Index] = ParsedCount;
			AnyParsed = true;
		}

		return AnyParsed;
	};

	auto SerializeQmModuleUsage = [&](char *pOut, int OutSize) {
		pOut[0] = '\0';
		bool First = true;
		for(const auto &Entry : s_aQmModuleDefaults)
		{
			const int Index = GetQmModuleIndexById(Entry.m_Id);
			if(s_aQmModuleUsage[Index] <= 0)
				continue;

			char aEntry[96];
			str_format(aEntry, sizeof(aEntry), "%s:%d", Entry.m_pKey, s_aQmModuleUsage[Index]);
			if(!First)
				str_append(pOut, ";", OutSize);
			str_append(pOut, aEntry, OutSize);
			First = false;
		}
	};

	auto PersistQmModuleUsage = [&]() {
		char aSerialized[sizeof(g_Config.m_QmSidebarCardUsage)];
		SerializeQmModuleUsage(aSerialized, sizeof(aSerialized));
		if(str_comp(aSerialized, g_Config.m_QmSidebarCardUsage) != 0)
			str_copy(g_Config.m_QmSidebarCardUsage, aSerialized, sizeof(g_Config.m_QmSidebarCardUsage));
		str_copy(s_aQmModuleUsageConfigCache, g_Config.m_QmSidebarCardUsage, sizeof(s_aQmModuleUsageConfigCache));
	};

	auto SyncQmModuleUsage = [&]() {
		const bool ConfigChanged = !s_QmModuleUsageInitialized || str_comp(s_aQmModuleUsageConfigCache, g_Config.m_QmSidebarCardUsage) != 0;
		if(ConfigChanged)
		{
			s_aQmModuleUsage.fill(0);
			ParseQmModuleUsage(g_Config.m_QmSidebarCardUsage);
			s_QmModuleUsageInitialized = true;
		}

		PersistQmModuleUsage();
	};

	auto IsQmModuleCollapsed = [&](EQmModuleId Id) -> bool {
		return s_aQmModuleCollapsed[GetQmModuleIndexById(Id)];
	};

	auto ToggleQmModuleCollapsed = [&](EQmModuleId Id) {
		const int Index = GetQmModuleIndexById(Id);
		s_aQmModuleCollapsed[Index] = !s_aQmModuleCollapsed[Index];
		PersistQmModuleCollapsed();
	};

	auto GetQmModuleUsage = [&](EQmModuleId Id) -> int {
		return s_aQmModuleUsage[GetQmModuleIndexById(Id)];
	};

	auto RecordQmModuleUsage = [&](EQmModuleId Id) {
		const int Index = GetQmModuleIndexById(Id);
		if(s_aQmModuleUsage[Index] < std::numeric_limits<int>::max())
			++s_aQmModuleUsage[Index];
		PersistQmModuleUsage();
	};

	auto ParseQmModuleLayout = [&](const char *pConfig) -> bool {
		if(!pConfig || pConfig[0] == '\0')
			return false;

		bool AnyParsed = false;
		bool aSeen[kQmModuleCount] = {};
		char aEntry[128];
		const char *pEntry = pConfig;
		while((pEntry = str_next_token(pEntry, ";", aEntry, sizeof(aEntry))))
		{
			if(aEntry[0] == '\0')
				continue;

			char aKey[64];
			char aColumn[16] = "";
			char aOrder[16] = "";
			const char *pField = str_next_token(aEntry, ":", aKey, sizeof(aKey));
			if(aKey[0] == '\0')
				continue;

			const int Index = FindQmModuleIndex(aKey);
			if(Index < 0)
				continue;
			if(aSeen[Index])
				continue;

			const SQmModuleEntry &DefaultEntry = s_aQmModuleDefaults[Index];
			EQmModuleColumn Column = DefaultEntry.m_Column;
			int Order = DefaultEntry.m_OrderInColumn;
			bool InvalidField = false;

			if(pField)
			{
				pField = str_next_token(pField, ":", aColumn, sizeof(aColumn));
				if(aColumn[0] != '\0')
				{
					EQmModuleColumn ParsedColumn;
					if(ParseQmModuleColumn(aColumn, &ParsedColumn))
						Column = ParsedColumn;
					else
						InvalidField = true;
				}
			}

			if(pField)
			{
				str_next_token(pField, ":", aOrder, sizeof(aOrder));
				if(aOrder[0] != '\0')
				{
					int ParsedOrder = 0;
					if(str_toint(aOrder, &ParsedOrder) && ParsedOrder >= 0)
						Order = ParsedOrder;
					else
						InvalidField = true;
				}
			}

			if(InvalidField)
				continue;

			if(DefaultEntry.m_Column == EQmModuleColumn::Full)
			{
				Column = EQmModuleColumn::Full;
			}
			else if(Column == EQmModuleColumn::Full)
			{
				Column = DefaultEntry.m_Column;
			}

			s_aQmModuleLayout[Index].m_Column = Column;
			s_aQmModuleLayout[Index].m_OrderInColumn = Order;
			aSeen[Index] = true;
			AnyParsed = true;
		}

		return AnyParsed;
	};

	auto SerializeQmModuleLayout = [&](char *pOut, int OutSize) {
		pOut[0] = '\0';
		bool First = true;
		for(const auto &Entry : s_aQmModuleLayout)
		{
			char aEntry[128];
			str_format(aEntry, sizeof(aEntry), "%s:%s:%d", Entry.m_pKey, QmModuleColumnToString(Entry.m_Column), Entry.m_OrderInColumn);
			if(!First)
				str_append(pOut, ";", OutSize);
			str_append(pOut, aEntry, OutSize);
			First = false;
		}
	};

	auto SyncQmModuleLayout = [&]() {
		const bool ConfigChanged = !s_QmModuleLayoutInitialized || str_comp(s_aQmModuleLayoutConfigCache, g_Config.m_QmSidebarCardOrder) != 0;
		if(ConfigChanged)
		{
			s_aQmModuleLayout = s_aQmModuleDefaults;
			ParseQmModuleLayout(g_Config.m_QmSidebarCardOrder);
			s_QmModuleLayoutInitialized = true;
			s_QmModuleColumnCacheDirty = true;
		}

		char aSerialized[sizeof(g_Config.m_QmSidebarCardOrder)];
		SerializeQmModuleLayout(aSerialized, sizeof(aSerialized));
		if(str_comp(aSerialized, g_Config.m_QmSidebarCardOrder) != 0)
			str_copy(g_Config.m_QmSidebarCardOrder, aSerialized, sizeof(g_Config.m_QmSidebarCardOrder));
		str_copy(s_aQmModuleLayoutConfigCache, g_Config.m_QmSidebarCardOrder, sizeof(s_aQmModuleLayoutConfigCache));
	};

	SyncQmModuleLayout();
	SyncQmModuleCollapsed();
	SyncQmModuleUsage();

	struct SQmModuleDragState
	{
		const SQmModuleEntry *m_pPressed;
		const SQmModuleEntry *m_pDragging;
		float m_PressStartTime;
		vec2 m_GrabOffset;
		float m_DraggedWidth;
		float m_DraggedHeight;
		bool m_HasDragRect;
	};

	static SQmModuleDragState s_DragState = {nullptr, nullptr, 0.0f, vec2(0.0f, 0.0f), 0.0f, 0.0f, false};
	const float DragHoldSeconds = 0.5f;
	const float DragOutlineThickness = std::clamp(2.0f * UiScale, 1.0f, 2.0f);
	const ColorRGBA DragOutlineColor(1.0f, 0.85f, 0.2f, 0.9f);
	const ColorRGBA DragGhostColor(0.08f, 0.09f, 0.12f, 0.55f);

	auto DrawDragOutline = [&](const CUIRect &Rect) {
		CUIRect Line = Rect;
		Line.HSplitTop(DragOutlineThickness, &Line, nullptr);
		Line.Draw(DragOutlineColor, IGraphics::CORNER_NONE, 0.0f);

		Line = Rect;
		Line.HSplitBottom(DragOutlineThickness, nullptr, &Line);
		Line.Draw(DragOutlineColor, IGraphics::CORNER_NONE, 0.0f);

		Line = Rect;
		Line.VSplitLeft(DragOutlineThickness, &Line, nullptr);
		Line.Draw(DragOutlineColor, IGraphics::CORNER_NONE, 0.0f);

		Line = Rect;
		Line.VSplitRight(DragOutlineThickness, nullptr, &Line);
		Line.Draw(DragOutlineColor, IGraphics::CORNER_NONE, 0.0f);
	};

	struct SQmModuleCardInfo
	{
		const SQmModuleEntry *m_pModule;
		EQmModuleColumn m_Column;
		CUIRect m_Rect;
	};

	struct SQmModuleDropPreview
	{
		const SQmModuleEntry *m_pDragged;
		EQmModuleColumn m_TargetColumn;
		int m_InsertIndex;
		bool m_Active;
		bool m_Valid;
		CUIRect m_LineRect;
	};

	static SQmModuleDropPreview s_DropPreview = {nullptr, EQmModuleColumn::Left, 0, false, false, CUIRect()};
	const float DropPreviewThickness = std::clamp(3.0f * UiScale, 2.0f, 4.0f);
	const ColorRGBA DropPreviewColor(0.2f, 0.9f, 0.4f, 0.9f);
	bool SearchSingleColumnMode = false;
	static std::array<CButtonContainer, kQmModuleCount> s_aModuleCollapseButtons;
	auto GetModuleCollapseButtonRect = [&](const SQmModuleEntry *pModule, const CUIRect &CardRect, CUIRect *pOutRect) -> bool {
		if(!ShowSearchModuleControls || pModule == nullptr || pModule->m_Column == EQmModuleColumn::Full)
			return false;

		CUIRect Inner = CardRect;
		Inner.Margin(LG_CardPadding, &Inner);
		CUIRect TitleRect = Inner;
		TitleRect.HSplitTop(LG_HeadlineSize, &TitleRect, nullptr);
		const float ButtonWidth = std::clamp(LG_HeadlineSize * 1.05f, 18.0f, 26.0f);
		CUIRect ButtonRect;
		TitleRect.VSplitRight(ButtonWidth + LG_LineSpacing * 0.25f, nullptr, &ButtonRect);
		ButtonRect.Margin(std::clamp(1.0f * UiScale, 0.5f, 1.5f), &ButtonRect);
		if(pOutRect != nullptr)
			*pOutRect = ButtonRect;
		return true;
	};

	auto HandleModuleDragState = [&](const SQmModuleEntry *pModule, const CUIRect &CardRect, bool BlockDrag = false) {
		CUIRect CollapseButtonRect;
		const bool HasCollapseButton = GetModuleCollapseButtonRect(pModule, CardRect, &CollapseButtonRect);
		const bool OverCollapseButton = HasCollapseButton && Ui()->MouseHovered(&CollapseButtonRect);
		const bool Inside = Ui()->MouseHovered(&CardRect) && !OverCollapseButton;
		if(Inside && Ui()->MouseButtonClicked(0))
			RecordQmModuleUsage(pModule->m_Id);
		const bool InteractionBlocked = BlockDrag || Ui()->ActiveItem() != nullptr || Ui()->IsPopupOpen() || Ui()->IsPopupHovered();
		if(InteractionBlocked && s_DragState.m_pPressed == pModule && s_DragState.m_pDragging == nullptr)
			s_DragState.m_pPressed = nullptr;

		if(!InteractionBlocked && Ui()->MouseButtonClicked(0) && Inside)
		{
			s_DragState.m_pPressed = pModule;
			s_DragState.m_pDragging = nullptr;
			s_DragState.m_PressStartTime = Client()->GlobalTime();
		}

		if(!InteractionBlocked && s_DragState.m_pPressed == pModule && Ui()->MouseButton(0) && s_DragState.m_pDragging == nullptr)
		{
			if(Inside && Client()->GlobalTime() - s_DragState.m_PressStartTime >= DragHoldSeconds)
			{
				s_DragState.m_pDragging = pModule;
				s_DragState.m_GrabOffset = vec2(Ui()->MouseX() - CardRect.x, Ui()->MouseY() - CardRect.y);
				s_DragState.m_DraggedWidth = CardRect.w;
				s_DragState.m_DraggedHeight = CardRect.h;
				s_DragState.m_HasDragRect = true;
			}
		}

		if(s_DragState.m_pDragging != pModule)
			return;
		DrawDragOutline(CardRect);
	};

	std::vector<SQmModuleCardInfo> ModuleCards;
	std::vector<const SQmModuleCardInfo *> LeftCards;
	std::vector<const SQmModuleCardInfo *> RightCards;
	ModuleCards.reserve(s_aQmModuleLayout.size());
	LeftCards.reserve(s_aQmModuleLayout.size());
	RightCards.reserve(s_aQmModuleLayout.size());

	auto RegisterModuleCard = [&](const SQmModuleEntry *pModule, EQmModuleColumn Column, const CUIRect &Rect) {
		ModuleCards.push_back({pModule, Column, Rect});
		s_aQmModuleLastHeights[GetQmModuleIndexById(pModule->m_Id)] = Rect.h;
		const SQmModuleCardInfo *pInfo = &ModuleCards.back();
		if(Column == EQmModuleColumn::Left)
			LeftCards.push_back(pInfo);
		else if(Column == EQmModuleColumn::Right)
			RightCards.push_back(pInfo);
	};
	auto HandleSearchCollapseButton = [&](const SQmModuleEntry *pModule, const CUIRect &CardRect) {
		CUIRect ButtonRect;
		if(!GetModuleCollapseButtonRect(pModule, CardRect, &ButtonRect))
			return;

		const int ModuleIndex = GetQmModuleIndexById(pModule->m_Id);
		const bool Collapsed = IsQmModuleCollapsed(pModule->m_Id);
		const char *pIcon = Collapsed ? FONT_ICON_PLUS : FONT_ICON_MINUS;
		if(Ui()->DoButton_FontIcon(&s_aModuleCollapseButtons[ModuleIndex], pIcon, 0, &ButtonRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
			ToggleQmModuleCollapsed(pModule->m_Id);
		if(Ui()->MouseHovered(&ButtonRect))
		{
			GameClient()->m_Tooltips.DoToolTip(
				&s_aModuleCollapseButtons[ModuleIndex],
				&ButtonRect,
				Collapsed ?
					TCLocalize("Expand module", QMCLIENT_LOCALIZATION_CONTEXT) :
					TCLocalize("Minimize module", QMCLIENT_LOCALIZATION_CONTEXT));
		}
	};

	bool ColumnsReady = false;
	auto EnsureColumns = [&]() {
		if(ColumnsReady)
			return;
		if(CompactLayout || SearchSingleColumnMode)
		{
			LeftView = MainView;
			RightView = MainView;
		}
		else
		{
			MainView.VSplitMid(&LeftView, &RightView, LG_CardSpacing);
		}
		ColumnsReady = true;
	};

	auto ResolveColumn = [&](EQmModuleColumn Column) -> CUIRect & {
		EnsureColumns();
		if(CompactLayout || SearchSingleColumnMode || Column == EQmModuleColumn::Left)
			return LeftView;
		return RightView;
	};

	float LeftColumnTop = 0.0f;
	float RightColumnTop = 0.0f;
	CUIRect LeftColumnFrame;
	CUIRect RightColumnFrame;
	bool ColumnTopsReady = false;
	auto EnsureColumnTops = [&]() {
		if(ColumnTopsReady)
			return;
		EnsureColumns();
		LeftColumnTop = LeftView.y;
		RightColumnTop = RightView.y;
		LeftColumnFrame = LeftView;
		RightColumnFrame = RightView;
		ColumnTopsReady = true;
	};

	if(s_QmModuleColumnCacheDirty)
	{
		auto RebuildColumnCache = [&](EQmModuleColumn Column, std::vector<const SQmModuleEntry *> &Out) {
			Out.clear();
			Out.reserve(s_aQmModuleLayout.size());
			for(const auto &Entry : s_aQmModuleLayout)
			{
				if(Entry.m_Column == Column)
					Out.push_back(&Entry);
			}
			std::stable_sort(Out.begin(), Out.end(), [](const SQmModuleEntry *a, const SQmModuleEntry *b) {
				return a->m_OrderInColumn < b->m_OrderInColumn;
			});
		};

		RebuildColumnCache(EQmModuleColumn::Full, s_vCachedFullModules);
		RebuildColumnCache(EQmModuleColumn::Left, s_vCachedLeftModules);
		RebuildColumnCache(EQmModuleColumn::Right, s_vCachedRightModules);
		s_QmModuleColumnCacheDirty = false;
	}

	const std::vector<const SQmModuleEntry *> &FullModules = s_vCachedFullModules;
	const std::vector<const SQmModuleEntry *> &LeftModules = s_vCachedLeftModules;
	const std::vector<const SQmModuleEntry *> &RightModules = s_vCachedRightModules;

	static CLineInputBuffered<128> s_ModuleSearchInput;
	const char *pModuleSearch = s_ModuleSearchInput.GetString();
	const bool HasModuleSearch = pModuleSearch[0] != '\0';
	ShowSearchModuleControls = true;

	auto ModuleSearchKeywords = [](EQmModuleId Id) -> const char * {
		switch(Id)
		{
		case EQmModuleId::ChatBubble: return "消息气泡 liaotian qipao chat bubble typing 预览 yulan 镜头缩放 suofang 持续时间 chixu 透明度 touming 字体大小 ziti 最大宽度 kuandu 垂直偏移 pianyi 圆角 yuanjiao";
		case EQmModuleId::GoresActor: return "gores 演员 actor 掉水 diaoshui 自动发言 zidong fayan 表情 biaoqing 表情id emoticon 发送概率 gaolv";
		case EQmModuleId::Gores: return "gores kog king of gores 锤枪切换 chuichang qiehuan 自动切枪 zidong qieqiang gun hammer prevweapon fire 开火后切锤 kaihuo qiechui 拿到其他武器停用";
		case EQmModuleId::KeyBinds: return "按键绑定 anjian bangding bind 快捷键 kuaijiejian 常用绑定 changyong bangding";
		case EQmModuleId::MiniFeatures: return "梦的小功能 meng xiaogongneng 粒子拖尾 lizi tuowei 远程粒子 yuancheng lizi 计分板查分 chafen 聊天框淡出 liaotian danchu 表情选择 biaoqing xuanze 动画优化 donghua youhua 复读 fudu 锤人换皮 chuiren huanpi 随机表情 suiji biaoqing 说话不弹表情 shuo hua biaoqing 本地彩虹名字 caihong mingzi 武器弹道辅助线 dan dao fuzhuxian 位置跳跃提示 tiaoyue tishi jelly q弹 tee 果冻 gudong 拉伸 yashen 压扁 yabian 回弹 huidan";
		case EQmModuleId::CameraView: return "镜头 jingtou camera drift 漂移 piaoyi dynamic fov 动态视野 dongtai shiye 纵横比 zonghengbi aspect ratio preset 预设 yushe 自定义 zidinyi 视野视角 shijiao";
		case EQmModuleId::DummyMiniView: return "分身小窗 fenshen xiaochuang dummy mini view 预览 yulan 缩放 suofang 小窗大小 daxiao";
		case EQmModuleId::Coords: return "显示坐标 xianshi zuobiao coords position 自己坐标 ziji 他人坐标 taren 显示x xianshi x 显示y xianshi y 对齐提示 duiqi tishi 严格对齐 yange duiqi";
		case EQmModuleId::Streamer: return "主播模式 zhubo moshi 直播 zhibo 隐私 yinsi 非好友昵称改id feihaoyou nicheng id 非好友皮肤默认 pifu moren 计分板默认国旗 guoqi";
		case EQmModuleId::FriendNotify: return "好友提醒 haoyou tixing 好友上线 shangxian 自动刷新 zidong shuaxin 服务器列表 fuwuqi liebiao 刷新间隔 jiange 进图打招呼 jintu dazhaohu 大字显示 dazi xianshi";
		case EQmModuleId::BlockWords: return "屏蔽词 pingbici block words 控制台显示 kongzhitai 启用列表 qiyong liebiao 按词长替换 cichang tihuan 多字符替换 duozifu tihuan";
		case EQmModuleId::Translate: return "翻译 fanyi translate 腾讯云 tengxunyun 自动翻译 zidong fanyi 主动翻译 zhudong fanyi [ru] 目标语言 mubiao yuyan 端点 duandian endpoint 地域 diyu region 中文跳过 zhongwen tiaoguo 服务器消息跳过";
		case EQmModuleId::QiaFen: return "关键词回复 guanjianci huifu 自动回复 zidong huifu 冷却 lengque dummy 发言 fayan 规则 guize 改名 gaiming 自动改名 zidong gaiming";
		case EQmModuleId::PieMenu: return "饼菜单 bingcaidan pie menu 启用 qiyong ui大小 daxiao 不透明度 butouming 检测距离 jiance juli 改名名单 gaiming mingdan";
		case EQmModuleId::EntityOverlay: return "实体层颜色 shiti ceng yanse 实体层 shiti entity overlay 死亡透明度 siwang 冻结透明度 dongjie 解冻透明度 jiedong 深度冻结 shendu dongjie 深度解冻 shendu jiedong 传送透明度 chuansong cp点透明度 cp checkpoint 开关透明度 kaiguan 叠层透明度 dieceng";
		case EQmModuleId::Laser: return "激光设置 jiguang laser 增强特效 zengqiang texiao 辉光强度 huiguang qiangdu 激光大小 daxiao 半透明 bantouming 圆角端点 yuanjiao duandian 脉冲速度 maichong sudu 脉冲幅度 maichong fudu";
		case EQmModuleId::PlayerStats: return "玩家统计 wanjia tongji player stats gores hud 显示统计 xianshi tongji 进服重置 jinfu chongzhi";
		case EQmModuleId::CollisionHitbox: return "碰撞体积可视化 pengzhuang tiji keshihua 碰撞箱 pengzhuangxiang collision hitbox 显示碰撞 xianshi pengzhuang 透明度 touming";
		case EQmModuleId::FavoriteMaps: return "收藏地图 shoucang ditu favorite maps 地图管理 ditu guanli 收藏 shoucang 取消收藏 quxiao shoucang";
		case EQmModuleId::HJAssist: return "hj辅助 hj fuzhu 解冻辅助 jiedong fuzhu 自动取消旁观 quxiao pangguan 自动切换 qiehuan tee 自动关闭聊天 guanbi liaotian";
		case EQmModuleId::InputOverlay: return "按键显示 anjian xianshi input overlay 按键叠加 anjian diejia 大小 daxiao 不透明度 butouming 水平位置 shuiping weizhi 垂直位置 chuizhi weizhi";
		case EQmModuleId::Voice: return "语音 yuyin voice chat 麦克风 maikefeng mic 静音 jingyin 音量 yinliang 语音激活 vad 阈值 yuzhi 释放延迟 shifang yanchi 服务器 fuwuqi token 叠加层 diejiaceng 按住说话 ptt push to talk 全图收听 quantu 衰减 shuijian 距离 juli 半径 banjing 测试 ceshi 本地 bendi 回环 huihuan 设备 shebei 输入 shuru 左右声道定位 左右 zuoyou 声道 shengdao 立体声 stereo";
		case EQmModuleId::DynamicIsland: return "灵动岛 lld lingdongdao dynamic island hud 顶部 dingbu 背景 beijing 颜色 yanse 透明度 touming 黑底 heidi 原版 yuanban 默认 moren classic old style";
		case EQmModuleId::SystemMediaControls: return "系统媒体控制 xitong meiti kongzhi smtc media controls 启用系统媒体 qiyong 显示歌曲信息 gequ xinxi 上一个 shangyige 播放暂停 bofang zanting 下一个 xiayige";
		case EQmModuleId::Info: return "";
		}
		return "";
	};

	auto ModuleMatchesSearch = [&](const SQmModuleEntry *pModule) -> bool {
		if(!HasModuleSearch)
			return true;
		return str_utf8_find_nocase(pModule->m_pKey, pModuleSearch) != nullptr ||
			str_utf8_find_nocase(ModuleSearchKeywords(pModule->m_Id), pModuleSearch) != nullptr;
	};

	struct SQmModuleHeadlineInfo
	{
		int m_RainbowIndex;
		const char *m_pTitle;
		const char *m_pTip;
	};

	auto GetQmModuleHeadlineInfo = [&](EQmModuleId Id) -> SQmModuleHeadlineInfo {
		switch(Id)
		{
		case EQmModuleId::ChatBubble:
			return {0, TCLocalize("Chat bubble", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Show chat bubbles above tees", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::GoresActor:
			return {1, TCLocalize("Gores actor tools", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Auto speak on water death", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::Gores:
			return {2, TCLocalize("Gores", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("King of Gores auto weapon swap", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::KeyBinds:
			return {3, TCLocalize("Key binds", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Common bind collection", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::MiniFeatures:
			return {2, TCLocalize("Qm mini features", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Qimeng's assorted daily-use tools", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::CameraView:
			return {10, TCLocalize("镜头与视野", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("镜头漂移、动态视野与纵横比预设", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::DummyMiniView:
			return {12, TCLocalize("Dummy mini view", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Dummy mini view preview and scaling", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::Coords:
			return {4, TCLocalize("Coordinates", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Show player coordinate info", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::Streamer:
			return {5, TCLocalize("Streamer mode", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Stream and privacy protection toggles", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::FriendNotify:
			return {6, TCLocalize("Friend notifications", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Friend online and join alerts", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::BlockWords:
			return {7, TCLocalize("Block words", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Chat block word filter", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::Translate:
			return {8, TCLocalize("Translate", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Chat translation settings", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::QiaFen:
			return {8, TCLocalize("Keyword reply", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Automatic replies for chat keywords", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::PieMenu:
			return {9, TCLocalize("Pie menu", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Open a prototype menu for quick access to common actions", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::EntityOverlay:
			return {6, TCLocalize("Entity overlay colors", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Entity layer tile colors", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::Laser:
			return {5, TCLocalize("Laser settings", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Laser style", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::PlayerStats:
			return {6, TCLocalize("Player stats", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Player stats and info display", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::CollisionHitbox:
			return {7, TCLocalize("Collision hitbox", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Show the base player collision box", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::FavoriteMaps:
			return {7, TCLocalize("Favorite maps", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Favorite map manager", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::HJAssist:
			return {11, TCLocalize("HJ assist", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Unfreeze automation helpers", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::InputOverlay:
			return {11, TCLocalize("Input overlay", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Input overlay display", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::Voice:
			return {12, TCLocalize("Voice", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Voice connection, input, and display", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::DynamicIsland:
			return {14, TCLocalize("Dynamic island", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Dynamic island", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::SystemMediaControls:
			return {13, TCLocalize("System media controls", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Media control toggles and buttons", QMCLIENT_LOCALIZATION_CONTEXT)};
		case EQmModuleId::Info:
			return {-1, "", ""};
		}
		return {-1, "", ""};
	};

	auto GetQmModuleSearchOrder = [&](const SQmModuleEntry *pModule) -> int {
		return FindQmModuleIndex(pModule->m_pKey);
	};

	auto GetQmModuleEstimatedHeight = [&](const SQmModuleEntry *pModule) -> float {
		const int Index = GetQmModuleIndexById(pModule->m_Id);
		if(IsQmModuleCollapsed(pModule->m_Id))
			return LG_CardPadding * 2.0f + LG_HeadlineSize + LG_TipHeight + LG_CardSpacing;
		if(s_aQmModuleLastHeights[Index] > 0.0f)
			return s_aQmModuleLastHeights[Index] + LG_CardSpacing;
		return LG_CardPadding * 2.0f + LG_HeadlineSize + LG_TipHeight + LG_LineHeight * 6.0f + LG_CardSpacing;
	};

	std::vector<const SQmModuleEntry *> VisibleLeftModules;
	std::vector<const SQmModuleEntry *> VisibleRightModules;
	VisibleLeftModules.reserve(LeftModules.size());
	VisibleRightModules.reserve(RightModules.size());
	for(const SQmModuleEntry *pModule : LeftModules)
	{
		if(ModuleMatchesSearch(pModule))
			VisibleLeftModules.push_back(pModule);
	}
	for(const SQmModuleEntry *pModule : RightModules)
	{
		if(ModuleMatchesSearch(pModule))
			VisibleRightModules.push_back(pModule);
	}
	std::vector<const SQmModuleEntry *> SearchVisibleModules;
	std::vector<const SQmModuleEntry *> SearchLeftModules;
	std::vector<const SQmModuleEntry *> SearchRightModules;
	SearchVisibleModules.reserve(VisibleLeftModules.size() + VisibleRightModules.size());
	if(HasModuleSearch)
	{
		SearchVisibleModules.insert(SearchVisibleModules.end(), VisibleLeftModules.begin(), VisibleLeftModules.end());
		SearchVisibleModules.insert(SearchVisibleModules.end(), VisibleRightModules.begin(), VisibleRightModules.end());
		std::stable_sort(SearchVisibleModules.begin(), SearchVisibleModules.end(), [&](const SQmModuleEntry *a, const SQmModuleEntry *b) {
			const int UsageA = GetQmModuleUsage(a->m_Id);
			const int UsageB = GetQmModuleUsage(b->m_Id);
			if(UsageA != UsageB)
				return UsageA > UsageB;
			return GetQmModuleSearchOrder(a) < GetQmModuleSearchOrder(b);
		});

		SearchSingleColumnMode = SearchVisibleModules.size() == 1;
		if(CompactLayout || SearchSingleColumnMode)
		{
			SearchLeftModules = SearchVisibleModules;
		}
		else
		{
			float LeftEstimatedHeight = 0.0f;
			float RightEstimatedHeight = 0.0f;
			for(const SQmModuleEntry *pModule : SearchVisibleModules)
			{
				const float EstimatedHeight = GetQmModuleEstimatedHeight(pModule);
				if(LeftEstimatedHeight <= RightEstimatedHeight)
				{
					SearchLeftModules.push_back(pModule);
					LeftEstimatedHeight += EstimatedHeight;
				}
				else
				{
					SearchRightModules.push_back(pModule);
					RightEstimatedHeight += EstimatedHeight;
				}
			}
		}
	}
	const int VisibleModuleCount = HasModuleSearch ?
		static_cast<int>(SearchVisibleModules.size()) :
		static_cast<int>(VisibleLeftModules.size() + VisibleRightModules.size());

	for(const SQmModuleEntry *pModule : FullModules)
	{
		switch(pModule->m_Id)
		{
		case EQmModuleId::Info:
		{
			// ========== 模块 0: QmClient 信息 (横跨整个宽度) ==========
			static float s_QQCopiedTime = 0.0f;  
			static bool s_QQCopied = false;       
			MainView.HSplitTop(LG_CardSpacing, nullptr, &MainView);
			const float CardStartY = MainView.y;
			CUIRect FullWidthCard = MainView;
			CUIRect CardInner = MainView;
			CardInner.VSplitLeft(LG_CardPadding, nullptr, &CardInner);
			CardInner.VSplitRight(LG_CardPadding, &CardInner, nullptr);
			CardInner.HSplitTop(LG_CardPadding, nullptr, &CardInner);
			CUIRect LeftPart = CardInner;
			CUIRect RightPart = CardInner;
			if(!CompactLayout)
			{
				const float ColumnSpacing = LG_CardSpacing * 2.0f;
				const float MinRightWidth = 360.0f * UiScale;
				const float PreferredLeftWidth = CardInner.w * 0.30f;
				const float MaxLeftWidth = maximum(220.0f * UiScale, CardInner.w - ColumnSpacing - MinRightWidth);
				const float LeftWidth = std::clamp(PreferredLeftWidth, 220.0f * UiScale, MaxLeftWidth);
				CardInner.VSplitLeft(LeftWidth, &LeftPart, &RightPart);
				RightPart.VSplitLeft(ColumnSpacing, nullptr, &RightPart);
			}
			const float LeftStartY = LeftPart.y;
			CUIRect LeftContent = LeftPart;
			DoModuleHeadline(LeftContent, -1, TCLocalize("QmClient community", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Official community links", QMCLIENT_LOCALIZATION_CONTEXT));
			// QQ群复制
			LeftContent.HSplitTop(LG_LineHeight, &Row, &LeftContent);
			{
				static int s_QQGroupButtonId;
				// 检测点击
				if(Ui()->MouseInside(&Row))
				{
					Ui()->SetHotItem(&s_QQGroupButtonId);
					if(Ui()->MouseButtonClicked(0))
					{
						Input()->SetClipboardText("1076765929");
						s_QQCopied = true;
						s_QQCopiedTime = Client()->LocalTime();
					}
				}
				if(s_QQCopied && Client()->LocalTime() - s_QQCopiedTime > 1.5f)
					s_QQCopied = false;
				if(s_QQCopied)
				{
					TextRender()->TextColor(0.0f, 1.0f, 0.0f, 1.0f);
					Ui()->DoLabel(&Row, TCLocalize("Copied", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				}
				else
				{
					TextRender()->TextColor(1.0f, 0.85f, 0.0f, 1.0f); 
					Ui()->DoLabel(&Row, TCLocalize("QQ Group: 1076765929 (click to copy)", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				}
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				if(Ui()->HotItem() == &s_QQGroupButtonId)
					GameClient()->m_Tooltips.DoToolTip(&s_QQGroupButtonId, &Row, TCLocalize("Click to copy the QQ group number", QMCLIENT_LOCALIZATION_CONTEXT));
			}
			LeftContent.HSplitTop(LG_LineSpacing * 2, nullptr, &LeftContent);
			DoModuleHeadline(LeftContent, -3, TCLocalize("Support", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Thanks for supporting QmClient", QMCLIENT_LOCALIZATION_CONTEXT));
			if(const CMenuImage *pSponsorImage = FindMenuImage("sponsor"))
			{
				const float SponsorImageHeight = std::clamp(LeftContent.w * 0.30f, LG_LineHeight * 2.2f, LG_LineHeight * 4.2f);
				LeftContent.HSplitTop(SponsorImageHeight, &Row, &LeftContent);
				CUIRect SponsorImageRect = Row;
				SponsorImageRect.Margin(LG_LineSpacing * 0.4f, &SponsorImageRect);
				RenderMenuImage(pSponsorImage, SponsorImageRect, 1.0f);
				LeftContent.HSplitTop(LG_LineSpacing, nullptr, &LeftContent);
			}
			LeftContent.HSplitTop(LG_LineHeight, &Row, &LeftContent);
			const float SponsorButtonWidth = LeftContent.w;
			{
				CUIRect SponsorButton;
				static CButtonContainer s_SponsorButton;
				Row.VSplitLeft(SponsorButtonWidth, &SponsorButton, nullptr);
				const char *pSponsorButtonText = s_ShowSponsorQrCode ? TCLocalize("Hide support QR code", QMCLIENT_LOCALIZATION_CONTEXT) : TCLocalize("View support QR code", QMCLIENT_LOCALIZATION_CONTEXT);
				if(DoButton_Menu(&s_SponsorButton, pSponsorButtonText, 0, &SponsorButton))
					s_ShowSponsorQrCode = !s_ShowSponsorQrCode;
			}
			if(s_ShowSponsorQrCode)
			{
				LeftContent.HSplitTop(LG_LineSpacing * 0.8f, nullptr, &LeftContent);
				if(EnsureSponsorQrTexture())
				{
					const float QrSide = std::clamp(LeftContent.w, LG_LineHeight * 8.0f, LG_LineHeight * 12.0f);
					LeftContent.HSplitTop(QrSide, &Row, &LeftContent);

					CUIRect QrRect = Row;
					QrRect.Margin(LG_LineSpacing * 0.35f, &QrRect);
					QrRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);
					QrRect.Margin(LG_LineSpacing * 0.5f, &QrRect);

					// 强制按正方形区域显示二维码，避免被拉伸。
					if(QrRect.w > QrRect.h)
					{
						const float Pad = (QrRect.w - QrRect.h) * 0.5f;
						QrRect.VSplitLeft(Pad, nullptr, &QrRect);
						QrRect.VSplitRight(Pad, &QrRect, nullptr);
					}
					else if(QrRect.h > QrRect.w)
					{
						const float Pad = (QrRect.h - QrRect.w) * 0.5f;
						QrRect.HSplitTop(Pad, nullptr, &QrRect);
						QrRect.HSplitBottom(Pad, &QrRect, nullptr);
					}
					RenderTexture(s_SponsorQrTexture, QrRect, 1.0f);
				}
				else
				{
					LeftContent.HSplitTop(LG_LineHeight * 1.4f, &Row, &LeftContent);
					TextRender()->TextColor(ColorRGBA(1.0f, 0.75f, 0.3f, 1.0f));
					if(s_SponsorQrDecodeFailed)
						Ui()->DoLabel(&Row, TCLocalize("Failed to load the support QR code. Check the Base64 content", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize * 0.92f, TEXTALIGN_ML);
					else
						Ui()->DoLabel(&Row, TCLocalize("Support QR code Base64 is not configured", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize * 0.92f, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}
			LeftContent.HSplitTop(LG_LineSpacing * 0.5f, nullptr, &LeftContent);
			LeftContent.HSplitTop(LG_LineHeight, &Row, &LeftContent);
			{
				CUIRect RecentUpdateButton;
				static CButtonContainer s_RecentUpdateButton;
				Row.VSplitLeft(SponsorButtonWidth, &RecentUpdateButton, nullptr);
				if(DoButton_Menu(&s_RecentUpdateButton, TCLocalize("View recent updates", QMCLIENT_LOCALIZATION_CONTEXT), 0, &RecentUpdateButton))
				{
					Client()->ViewLink("https://publish.obsidian.md/qmclient");
				}
			}
			LeftContent.HSplitTop(LG_LineSpacing * 0.5f, nullptr, &LeftContent);
			LeftContent.HSplitTop(LG_LineHeight, &Row, &LeftContent);
			{
				CUIRect FeedbackButton;
				static CButtonContainer s_FeedbackButton;
				Row.VSplitLeft(SponsorButtonWidth, &FeedbackButton, nullptr);
				if(DoButton_Menu(&s_FeedbackButton, TCLocalize("Send feedback", QMCLIENT_LOCALIZATION_CONTEXT), 0, &FeedbackButton))
				{
					Client()->ViewLink("https://qmclient.icu/feedback.html");
				}
			}

			const float LeftUsedHeight = LeftContent.y - LeftStartY;
			if(CompactLayout)
			{
				RightPart.y = LeftStartY + LeftUsedHeight + LG_CardSpacing;
				RightPart.h = (CardInner.y + CardInner.h) - RightPart.y;
			}
			//右侧
			const float RightStartY = RightPart.y;
			const float TeeSize = std::clamp(60.0f * UiScale, 36.0f, 60.0f);
			const float RightContentShift = LG_CardPadding * 0.40f;
			CUIRect TeeRect, TextRect;
				const float TeeTextOffset = TeeSize + LG_CardPadding * 0.65f;
				RightPart.VSplitLeft(TeeTextOffset, &TeeRect, &TextRect);
			vec2 TeePos = vec2(TeeRect.x + TeeSize * 0.5f - RightContentShift, RightStartY + TeeSize * 0.5f + LG_CardPadding * 0.5f);
			RenderDevSkin(
				TeePos,               // 位置
				TeeSize,              // 大小
				"rabbit_new2",         // 皮肤名
				"santa_tuzi",           // 备用皮肤
				false,                 // 自定义颜色
				0, 0, 0,              // 脚部颜色，身体颜色，表情
				false,                 // 彩虹效果
				true                  // Cute
			);
			CUIRect RightContent = TextRect;
			RightContent.x -= RightContentShift;
			RightContent.w += RightContentShift;
			DoModuleHeadline(RightContent, -2, TCLocalize("QmClient team", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Developers and supporters", QMCLIENT_LOCALIZATION_CONTEXT));
			//我名字
			RightContent.HSplitTop(LG_LineHeight, &Row, &RightContent);
			TextRender()->TextColor(GetRainbowColor(-6));
			Ui()->DoLabel(&Row, "栖梦(璇梦),夏日", LG_BodySize + 2.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			// 感谢名单
			constexpr float SponsorFontShrink = 4.0f;
			constexpr float MinSponsorFontSize = 8.0f;
			RightContent.HSplitTop(LG_LineSpacing * 1.75f, nullptr, &RightContent);
			RightContent.HSplitTop(LG_LineHeight * 0.92f, &Row, &RightContent);
			TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.82f));
			Ui()->DoLabel(&Row, TCLocalize("Supporters:", QMCLIENT_LOCALIZATION_CONTEXT), maximum(LG_BodySize * 0.95f - SponsorFontShrink, MinSponsorFontSize), TEXTALIGN_ML);
			RightContent.HSplitTop(LG_LineSpacing * 0.35f, nullptr, &RightContent);
			CUIRect Divider;
			RightContent.HSplitTop(1.0f, &Divider, &RightContent);
			Divider.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.16f), IGraphics::CORNER_NONE, 0.0f);
			RightContent.HSplitTop(LG_LineSpacing * 0.55f, nullptr, &RightContent);
			//TextRender()->TextColor(GetRainbowColor(-8));//彩虹循环效果
			TextRender()->TextColor(ColorRGBA(0.95f, 0.8f, 0.2f, 1.0f));
			{
				static const char *const s_apSponsors[] = {
					"喵不一",
					"久桃",
					"芽芽",
					"碳烤綿芽",
					"骨头",
					"陌浅羽",
					"树羽小朋友",
					"望舒",
					"松子",
					"平凡..",
					"cixin",
					"洗点",
					"秀色",
					"朱朱",
					"Twen",
					"大恐龙",
					":luv:",
					"小左",
					"Blue°F",
					"怯修",
					"yezeen",
					"鹑",
					"枫香°",
					"没问题啊",
					"·蓝蓝蓝蓝",
					"临渊捕鱼",
					"?hook?",
					"放肆zero",
					"Q币",
					"洛天依",
					"spider",
					"贝塔塔塔",
					"见月",
					"咩子的银耳",
					"Cancer",
					"少女`",
					"长亭寂寞独自愁",
					"fantuan",
					"无言鱼",
					"胖人老许",
					"夏日",
					"张宁我儿",
					"拌饭",
					"shengyan",
					"修勾在修沟",
					"taffy",
					"杀意没爱意",
					"DYL",
					"小信",
					"哆啦梦",
					"菜菜羊",
					"吃了吗chilem",
					"你就是我的",
					"xiaopang",
					"星星🌙",
					"軽い猫"
					};
				const float SponsorFontSize = maximum(LG_BodySize * 1.1f - SponsorFontShrink, MinSponsorFontSize);
				const float MaxLineWidth = RightContent.w;
				static std::vector<std::string> s_SponsorLines;
				static float s_LastSponsorFontSize = -1.0f;
				static float s_LastSponsorMaxLineWidth = -1.0f;
				if(s_SponsorLines.empty() ||
					std::abs(s_LastSponsorFontSize - SponsorFontSize) > 0.01f ||
					std::abs(s_LastSponsorMaxLineWidth - MaxLineWidth) > 0.5f)
				{
					s_LastSponsorFontSize = SponsorFontSize;
					s_LastSponsorMaxLineWidth = MaxLineWidth;
					const char *pSeparator = ",";
					const float SeparatorWidth = TextRender()->TextWidth(SponsorFontSize, pSeparator);

					s_SponsorLines.clear();
					s_SponsorLines.emplace_back();
					float LineWidth = 0.0f;
					for(const char *pName : s_apSponsors)
					{
						const float NameWidth = TextRender()->TextWidth(SponsorFontSize, pName);
						if(s_SponsorLines.back().empty())
						{
							s_SponsorLines.back() = pName;
							LineWidth = NameWidth;
							continue;
						}

						const float NextWidth = LineWidth + SeparatorWidth + NameWidth;
						if(NextWidth > MaxLineWidth)
						{
							s_SponsorLines.emplace_back(pName);
							LineWidth = NameWidth;
						}
						else
						{
							s_SponsorLines.back().append(pSeparator);
							s_SponsorLines.back().append(pName);
							LineWidth = NextWidth;
						}
					}
				}

				for(const auto &Line : s_SponsorLines)
				{
					RightContent.HSplitTop(LG_LineHeight * 0.96f, &Row, &RightContent);
					Ui()->DoLabel(&Row, Line.c_str(), SponsorFontSize, TEXTALIGN_ML);
				}
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			RightContent.HSplitTop(LG_LineSpacing * 0.55f, nullptr, &RightContent);
			RightContent.HSplitTop(LG_LineHeight * 0.9f, &Row, &RightContent);
			Ui()->DoLabel(&Row, TCLocalize("I could not have come this far without your support, big or small. Thank you.", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize * 0.93f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			const float RightUsedHeight = RightContent.y - RightStartY;
			const float ContentHeight = CompactLayout ? (LeftUsedHeight + LG_CardSpacing + RightUsedHeight) : maximum(LeftUsedHeight, RightUsedHeight);
			const float InfoCardHeight = ContentHeight + LG_CardPadding * 2;

			FullWidthCard.y = CardStartY;
			FullWidthCard.h = InfoCardHeight;
			s_GlassCards.push_back(FullWidthCard);
			RegisterModuleCard(pModule, EQmModuleColumn::Full, FullWidthCard);
			HandleModuleDragState(pModule, FullWidthCard);
			MainView.HSplitTop(InfoCardHeight, nullptr, &MainView);
			MainView.HSplitTop(LG_CardSpacing, nullptr, &MainView);

		}
		break;
		default:
			break;
		}
	}

	{
		const float SearchCardStartY = MainView.y;
		CUIRect SearchCard = MainView;
		CUIRect SearchContent = MainView;
		SearchContent.VSplitLeft(LG_CardPadding, nullptr, &SearchContent);
		SearchContent.VSplitRight(LG_CardPadding, &SearchContent, nullptr);
		SearchContent.HSplitTop(LG_CardPadding, nullptr, &SearchContent);
		DoModuleHeadline(SearchContent, -4, TCLocalize("Feature search", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Quickly locate feature modules", QMCLIENT_LOCALIZATION_CONTEXT));
		SearchContent.HSplitTop(LG_LineHeight, &Row, &SearchContent);
		Ui()->DoEditBox_Search(&s_ModuleSearchInput, &Row, LG_BodySize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
		SearchContent.HSplitTop(LG_LineSpacing * 0.65f, nullptr, &SearchContent);

		char aSearchHint[64];
		str_format(aSearchHint, sizeof(aSearchHint), TCLocalize("Matched %d feature modules", QMCLIENT_LOCALIZATION_CONTEXT), VisibleModuleCount);
		SearchContent.HSplitTop(LG_LineHeight * 0.85f, &Row, &SearchContent);
		TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.82f));
		if(HasModuleSearch && VisibleModuleCount == 0)
			Ui()->DoLabel(&Row, TCLocalize("No matching feature found. Try another keyword", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize * 0.92f, TEXTALIGN_ML);
		else
			Ui()->DoLabel(&Row, aSearchHint, LG_BodySize * 0.92f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		SearchContent.HSplitTop(LG_CardPadding, nullptr, &SearchContent);
		const float SearchCardHeight = SearchContent.y - SearchCardStartY;
		SearchCard.y = SearchCardStartY;
		SearchCard.h = SearchCardHeight;
		s_GlassCards.push_back(SearchCard);
		MainView.HSplitTop(SearchCardHeight, nullptr, &MainView);
		MainView.HSplitTop(LG_CardSpacing, nullptr, &MainView);
	}

	auto RenderColumnModules = [&](const std::vector<const SQmModuleEntry *> &Modules, EQmModuleColumn ColumnId) {
		if(Modules.empty())
			return;
		CUIRect &Column = ResolveColumn(ColumnId);
		for(const SQmModuleEntry *pModule : Modules)
		{
			const SQmModuleHeadlineInfo HeadlineInfo = GetQmModuleHeadlineInfo(pModule->m_Id);
			if(IsQmModuleCollapsed(pModule->m_Id))
			{
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CollapsedCard = Column;
				s_GlassCards.push_back(CollapsedCard);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, HeadlineInfo.m_RainbowIndex, HeadlineInfo.m_pTitle, HeadlineInfo.m_pTip);

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
				HandleSearchCollapseButton(pModule, s_GlassCards.back());
				continue;
			}

			switch(pModule->m_Id)
			{
			case EQmModuleId::ChatBubble:
			{
				// ========== 模块 1: 消息气泡 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect Card1Start = Column;
				s_GlassCards.push_back(Card1Start);
				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 0, TCLocalize("Chat bubble", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Show chat bubbles above tees", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmChatBubble, TCLocalize("Show chat bubbles above players", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmChatBubble, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmChatBubble)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmChatBubbleDuration, &g_Config.m_QmChatBubbleDuration, &Row, TCLocalize("Duration", QMCLIENT_LOCALIZATION_CONTEXT), 1, 30, &CUi::ms_LinearScrollbarScale, 0, "s");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmChatBubbleAlpha, &g_Config.m_QmChatBubbleAlpha, &Row, TCLocalize("Bubble opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmChatBubbleFontSize, &g_Config.m_QmChatBubbleFontSize, &Row, TCLocalize("Font size", QMCLIENT_LOCALIZATION_CONTEXT), 8, 32);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					static std::vector<const char *> s_ChatBubbleAnimDropDownNames;
					s_ChatBubbleAnimDropDownNames = {TCLocalize("Fade out", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Shrink", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Float up", QMCLIENT_LOCALIZATION_CONTEXT)};
					static CUi::SDropDownState s_ChatBubbleAnimDropDownState;
					static CScrollRegion s_ChatBubbleAnimDropDownScrollRegion;
					s_ChatBubbleAnimDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ChatBubbleAnimDropDownScrollRegion;
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("Animation", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					const int AnimSelectedNew = Ui()->DoDropDown(&ControlCol, g_Config.m_QmChatBubbleAnimation, s_ChatBubbleAnimDropDownNames.data(), s_ChatBubbleAnimDropDownNames.size(), s_ChatBubbleAnimDropDownState);
					if(g_Config.m_QmChatBubbleAnimation != AnimSelectedNew)
						g_Config.m_QmChatBubbleAnimation = AnimSelectedNew;
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					static CButtonContainer s_ChatBubbleBgColorId, s_ChatBubbleTextColorId;
					DoLine_ColorPicker(&s_ChatBubbleBgColorId, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("Background color", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmChatBubbleBgColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.8f), false, nullptr, true);

					DoLine_ColorPicker(&s_ChatBubbleTextColorId, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("Text color", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmChatBubbleTextColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::GoresActor:
			{
				// ========== 模块 2: Gores演员专用 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect Card2Start = Column;
				s_GlassCards.push_back(Card2Start);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 1, TCLocalize("Gores actor tools", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Auto speak on water death", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFreezeChatEnabled, TCLocalize("Auto chat and emote on water death", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcFreezeChatEnabled, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_TcFreezeChatEnabled)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFreezeChatEmoticon, TCLocalize("Send an emote on water death", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcFreezeChatEmoticon, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					if(g_Config.m_TcFreezeChatEmoticon)
					{
						CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
						Ui()->DoScrollbarOption(&g_Config.m_TcFreezeChatEmoticonId, &g_Config.m_TcFreezeChatEmoticonId, &Row, TCLocalize("Emote ID", QMCLIENT_LOCALIZATION_CONTEXT), 0, 15);
						CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					}

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("Chat message", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					static CLineInput s_FreezeChatMessageQiMeng(g_Config.m_TcFreezeChatMessage, sizeof(g_Config.m_TcFreezeChatMessage));
					s_FreezeChatMessageQiMeng.SetEmptyText(TCLocalize("Leave empty to disable", QMCLIENT_LOCALIZATION_CONTEXT));
					Ui()->DoEditBox(&s_FreezeChatMessageQiMeng, &ControlCol, LG_BodySize);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_TcFreezeChatChance, &g_Config.m_TcFreezeChatChance, &Row, TCLocalize("Send chance", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::Gores:
			{
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardGoresStart = Column;
				s_GlassCards.push_back(CardGoresStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 2, TCLocalize("Gores", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("King of Gores auto weapon swap", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmGores, TCLocalize("Enable Gores auto weapon swap", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmGores, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmGoresDisableIfWeapons, TCLocalize("Disable after picking up other weapons", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmGoresDisableIfWeapons, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmGoresAutoEnable, TCLocalize("Auto enable in Gores mode", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmGoresAutoEnable, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing * 0.7f, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight * 0.85f, &Row, &CardContent);

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::KeyBinds:
			{
				// ========== 模块 2.5: 按键绑定 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardControlsStart = Column;
				s_GlassCards.push_back(CardControlsStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 3, TCLocalize("Key binds", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Common bind collection", QMCLIENT_LOCALIZATION_CONTEXT));

				static CButtonContainer s_ReaderButtonDummyPseudo, s_ClearButtonDummyPseudo,
					s_ReaderButtonDeepfly, s_ClearButtonDeepfly,
					s_ReaderButtonDeepflyToggle, s_ClearButtonDeepflyToggle,
					s_ReaderButton45Degrees, s_ClearButton45Degrees,
					s_ReaderButtonSmallSens, s_ClearButtonSmallSens,
					s_ReaderButtonLeftJump, s_ClearButtonLeftJump,
					s_ReaderButtonRightJump, s_ClearButtonRightJump;

				DoKeyBindRow(CardContent, s_ReaderButtonDummyPseudo, s_ClearButtonDummyPseudo,
					TCLocalize("HDF"), "+toggle cl_dummy_hammer 1 0");
				DoKeyBindRow(CardContent, s_ReaderButtonDeepfly, s_ClearButtonDeepfly,
					TCLocalize("DF"), "+fire; +toggle cl_dummy_hammer 1 0");
				DoKeyBindRow(CardContent, s_ReaderButton45Degrees, s_ClearButton45Degrees,
					TCLocalize("45-degree aim", QMCLIENT_LOCALIZATION_CONTEXT), "echo You are using 45-degree aim;+toggle cl_mouse_max_distance 2 400; +toggle_restore inp_mousesens 1");
				DoKeyBindRow(CardContent, s_ReaderButtonSmallSens, s_ClearButtonSmallSens,
					TCLocalize("Gap rescue aim", QMCLIENT_LOCALIZATION_CONTEXT), "+toggle_restore inp_mousesens 1");
				DoKeyBindRow(CardContent, s_ReaderButtonLeftJump, s_ClearButtonLeftJump,
					TCLocalize("Three-tile jump left", QMCLIENT_LOCALIZATION_CONTEXT), "+jump; +left");
				DoKeyBindRow(CardContent, s_ReaderButtonRightJump, s_ClearButtonRightJump,
					TCLocalize("Three-tile jump right", QMCLIENT_LOCALIZATION_CONTEXT), "+jump; +right");

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::MiniFeatures:
			{
				// ========== 模块 3: 梦的小功能 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardMiniFeaturesStart = Column;
				s_GlassCards.push_back(CardMiniFeaturesStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 2, TCLocalize("Qm mini features", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Qimeng's assorted daily-use tools", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmcFootParticles, TCLocalize("Local particles", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmcFootParticles, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmClientMarkTrail, TCLocalize("Remote particles", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmClientMarkTrail, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmClientShowBadge, TCLocalize("Show Qm badge", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmClientShowBadge, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClScoreboardPoints, TCLocalize("Show scoreboard points", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_ClScoreboardPoints, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmChatFadeOutAnim, TCLocalize("Chat box animation", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmChatFadeOutAnim, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmEmoticonSelectAnim, TCLocalize("Emote animation", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmEmoticonSelectAnim, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmScoreboardAnimOptim, TCLocalize("Scoreboard animation", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmScoreboardAnimOptim, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRepeatEnabled, TCLocalize("Enable repeat feature", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmRepeatEnabled, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmHammerSwapSkin, TCLocalize("Hammer hit copies skin", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmHammerSwapSkin, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTee, TCLocalize("Enable Jelly Tee", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmJellyTee, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmJellyTee)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmJellyTeeOthers, TCLocalize("Jelly others", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmJellyTeeOthers, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmJellyTeeStrength, &g_Config.m_QmJellyTeeStrength, &Row, TCLocalize("Jelly strength", QMCLIENT_LOCALIZATION_CONTEXT), 0, 1000);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmJellyTeeDuration, &g_Config.m_QmJellyTeeDuration, &Row, TCLocalize("Jelly duration", QMCLIENT_LOCALIZATION_CONTEXT), 1, 500);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRandomEmoteOnHit, TCLocalize("Random emote on hit", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmRandomEmoteOnHit, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmSayNoPop, TCLocalize("Hide typing emote while chatting", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmSayNoPop, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowName, TCLocalize("Rainbow name", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmRainbowName, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWeaponTrajectory, TCLocalize("Weapon trajectory guide", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmWeaponTrajectory, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcJumpHint, TCLocalize("Position jump hint", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcJumpHint, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::Coords:
			{
				// ========== 模块 3.5: 显示坐标 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardCoordsStart = Column;
				s_GlassCards.push_back(CardCoordsStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 4, TCLocalize("Coordinates", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Show player coordinate info", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoordsOwn, TCLocalize("Show own coordinates", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmNameplateCoordsOwn, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoords, TCLocalize("Show other players' coordinates", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmNameplateCoords, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoordX, TCLocalize("Show X", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmNameplateCoordX, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoordY, TCLocalize("Show Y", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmNameplateCoordY, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoordXAlignHint, TCLocalize("Alignment hint", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmNameplateCoordXAlignHint, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmNameplateCoordXAlignHint)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoordXAlignHintStrict, TCLocalize("Strict alignment", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmNameplateCoordXAlignHintStrict, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::Streamer:
			{
				// ========== 模块 3.8: 主播模式 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardStreamerStart = Column;
				s_GlassCards.push_back(CardStreamerStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 5, TCLocalize("Streamer mode", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Stream and privacy protection toggles", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmStreamerHideNames, TCLocalize("Replace non-friend names with IDs", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmStreamerHideNames, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmStreamerHideSkins, TCLocalize("Replace non-friend skins with default skins", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmStreamerHideSkins, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmStreamerScoreboardDefaultFlags, TCLocalize("Use default flags on the scoreboard", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmStreamerScoreboardDefaultFlags, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::FriendNotify:
			{
				// ========== 模块 3.9: 好友提醒 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardFriendNotifyStart = Column;
				s_GlassCards.push_back(CardFriendNotifyStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 6, TCLocalize("Friend notifications", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Friend online and join alerts", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFriendOnlineNotify, TCLocalize("Notify when friends come online", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmFriendOnlineNotify, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFriendOnlineAutoRefresh, TCLocalize("Auto-refresh the server list", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmFriendOnlineAutoRefresh, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmFriendOnlineAutoRefresh)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmFriendOnlineRefreshSeconds, &g_Config.m_QmFriendOnlineRefreshSeconds, &Row, TCLocalize("Refresh interval", QMCLIENT_LOCALIZATION_CONTEXT), 5, 300, &CUi::ms_LinearScrollbarScale, 0, "s");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFriendEnterAutoGreet, TCLocalize("Auto greet when a friend joins the map", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmFriendEnterAutoGreet, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFriendEnterBroadcast, TCLocalize("Big-screen friend join alert", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmFriendEnterBroadcast, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmFriendEnterBroadcast)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("Big-screen message", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					static CLineInput s_FriendEnterBroadcastText(g_Config.m_QmFriendEnterBroadcastText, sizeof(g_Config.m_QmFriendEnterBroadcastText));
					s_FriendEnterBroadcastText.SetEmptyText(TCLocalize("Use %s for the friend name", QMCLIENT_LOCALIZATION_CONTEXT));
					Ui()->DoEditBox(&s_FriendEnterBroadcastText, &ControlCol, LG_BodySize);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				if(g_Config.m_QmFriendEnterAutoGreet)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("Greeting text", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					static CLineInput s_FriendEnterGreetText(g_Config.m_QmFriendEnterGreetText, sizeof(g_Config.m_QmFriendEnterGreetText));
					s_FriendEnterGreetText.SetEmptyText(TCLocalize("Leave empty to disable", QMCLIENT_LOCALIZATION_CONTEXT));
					Ui()->DoEditBox(&s_FriendEnterGreetText, &ControlCol, LG_BodySize);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::BlockWords:
			{
				// ========== 模块 3.95: 屏蔽词 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardBlockWordsStart = Column;
				s_GlassCards.push_back(CardBlockWordsStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 7, TCLocalize("Block words", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Chat block word filter", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmBlockWordsShowConsole, TCLocalize("Show blocked words in the console", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmBlockWordsShowConsole, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				static CButtonContainer s_BlockWordsConsoleColorId;
				DoLine_ColorPicker(&s_BlockWordsConsoleColorId, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("Console color", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmBlockWordsConsoleColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmBlockWordsEnabled, TCLocalize("Enable block word list", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmBlockWordsEnabled, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmBlockWordsMultiReplace, TCLocalize("Use multi-character replacement based on word length", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmBlockWordsMultiReplace, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				static CLineInputBuffered<8> s_BlockWordsReplaceInput;
				static bool s_BlockWordsReplaceInited = false;
				if(!s_BlockWordsReplaceInited)
				{
					s_BlockWordsReplaceInput.Set(g_Config.m_QmBlockWordsReplacementChar);
					s_BlockWordsReplaceInited = true;
				}
				else if(!s_BlockWordsReplaceInput.IsActive() && str_comp(s_BlockWordsReplaceInput.GetString(), g_Config.m_QmBlockWordsReplacementChar) != 0)
				{
					s_BlockWordsReplaceInput.Set(g_Config.m_QmBlockWordsReplacementChar);
				}
				s_BlockWordsReplaceInput.SetEmptyText("*");

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("Replacement char", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				if(Ui()->DoEditBox(&s_BlockWordsReplaceInput, &ControlCol, LG_BodySize))
				{
					char aReplacement[8];
					str_utf8_truncate(aReplacement, sizeof(aReplacement), s_BlockWordsReplaceInput.GetString(), 1);
					if(aReplacement[0] == '\0')
						str_copy(aReplacement, "*", sizeof(aReplacement));
					str_copy(g_Config.m_QmBlockWordsReplacementChar, aReplacement, sizeof(g_Config.m_QmBlockWordsReplacementChar));
				}
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("Replacement mode", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				CUIRect ModeRow = ControlCol;
				CUIRect ModeButton;
				static CButtonContainer s_BlockWordsModeRegex, s_BlockWordsModeFull, s_BlockWordsModeBoth;
				const float ModeWidth = ModeRow.w / 3.0f;
				ModeRow.VSplitLeft(ModeWidth, &ModeButton, &ModeRow);
				if(DoButtonLineSize_Menu(&s_BlockWordsModeRegex, TCLocalize("Regex", QMCLIENT_LOCALIZATION_CONTEXT), g_Config.m_QmBlockWordsMode == 0, &ModeButton, LG_LineHeight, false, 0, IGraphics::CORNER_L, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
					g_Config.m_QmBlockWordsMode = 0;
				ModeRow.VSplitLeft(ModeWidth, &ModeButton, &ModeRow);
				if(DoButtonLineSize_Menu(&s_BlockWordsModeFull, TCLocalize("Literal", QMCLIENT_LOCALIZATION_CONTEXT), g_Config.m_QmBlockWordsMode == 1, &ModeButton, LG_LineHeight, false, 0, IGraphics::CORNER_NONE, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
					g_Config.m_QmBlockWordsMode = 1;
				if(DoButtonLineSize_Menu(&s_BlockWordsModeBoth, TCLocalize("Both", QMCLIENT_LOCALIZATION_CONTEXT), g_Config.m_QmBlockWordsMode == 2, &ModeRow, LG_LineHeight, false, 0, IGraphics::CORNER_R, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
					g_Config.m_QmBlockWordsMode = 2;
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				static CLineInputBuffered<1024> s_BlockWordsInput;
				static bool s_BlockWordsInited = false;
				if(!s_BlockWordsInited)
				{
					s_BlockWordsInput.Set(g_Config.m_QmBlockWordsList);
					s_BlockWordsInited = true;
				}
				else if(!s_BlockWordsInput.IsActive() && str_comp(s_BlockWordsInput.GetString(), g_Config.m_QmBlockWordsList) != 0)
				{
					s_BlockWordsInput.Set(g_Config.m_QmBlockWordsList);
				}
				s_BlockWordsInput.SetEmptyText(TCLocalize("Separate with commas", QMCLIENT_LOCALIZATION_CONTEXT));

				const float BlockInputWidth = CardContent.w - LG_LabelWidth;
				const float BlockInputLineSpacing = std::clamp(2.0f * UiScale, 1.0f, 2.0f);
				const float BlockInputHeight = CalcQiaFenInputHeight(TextRender(), s_BlockWordsInput.GetString(), BlockInputWidth, LG_BodySize, BlockInputLineSpacing, LG_LineHeight);
				CardContent.HSplitTop(BlockInputHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("Blocked words", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				if(DoEditBoxMultiLine(Ui(), &s_BlockWordsInput, &ControlCol, LG_BodySize, BlockInputLineSpacing))
					str_copy(g_Config.m_QmBlockWordsList, s_BlockWordsInput.GetString(), sizeof(g_Config.m_QmBlockWordsList));

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::Translate:
			{
				// ========== 模块 3.96: 翻译 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardTranslateStart = Column;
				s_GlassCards.push_back(CardTranslateStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 8, TCLocalize("Translate", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Chat translation settings", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTranslateAuto, TCLocalize("Automatically translate chat messages", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_TcTranslateAuto, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				static std::vector<const char *> s_TranslateBackendDropDownNames;
				s_TranslateBackendDropDownNames = {TCLocalize("Tencent Cloud", QMCLIENT_LOCALIZATION_CONTEXT), "LibreTranslate", "FTAPI"};
				static CUi::SDropDownState s_TranslateBackendDropDownState;
				static CScrollRegion s_TranslateBackendDropDownScrollRegion;
				s_TranslateBackendDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TranslateBackendDropDownScrollRegion;

				int BackendSelectedOld = 0;
				if(str_comp_nocase(g_Config.m_TcTranslateBackend, "libretranslate") == 0)
					BackendSelectedOld = 1;
				else if(str_comp_nocase(g_Config.m_TcTranslateBackend, "ftapi") == 0)
					BackendSelectedOld = 2;

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("Translation backend", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				const int BackendSelectedNew = Ui()->DoDropDown(&ControlCol, BackendSelectedOld, s_TranslateBackendDropDownNames.data(), s_TranslateBackendDropDownNames.size(), s_TranslateBackendDropDownState);
				if(BackendSelectedNew != BackendSelectedOld)
				{
					if(BackendSelectedNew == 1)
						str_copy(g_Config.m_TcTranslateBackend, "libretranslate", sizeof(g_Config.m_TcTranslateBackend));
					else if(BackendSelectedNew == 2)
						str_copy(g_Config.m_TcTranslateBackend, "ftapi", sizeof(g_Config.m_TcTranslateBackend));
					else
						str_copy(g_Config.m_TcTranslateBackend, "tencentcloud", sizeof(g_Config.m_TcTranslateBackend));
				}
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("Target language", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				static CLineInput s_TranslateTarget(g_Config.m_TcTranslateTarget, sizeof(g_Config.m_TcTranslateTarget));
				s_TranslateTarget.SetEmptyText("zh");
				Ui()->DoEditBox(&s_TranslateTarget, &ControlCol, LG_BodySize);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("Endpoint", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				static CLineInput s_TranslateEndpoint(g_Config.m_TcTranslateEndpoint, sizeof(g_Config.m_TcTranslateEndpoint));
				s_TranslateEndpoint.SetEmptyText("https://tmt.tencentcloudapi.com/");
				Ui()->DoEditBox(&s_TranslateEndpoint, &ControlCol, LG_BodySize);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("Region", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				static CLineInput s_TranslateRegion(g_Config.m_TcTranslateRegion, sizeof(g_Config.m_TcTranslateRegion));
				s_TranslateRegion.SetEmptyText("ap-guangzhou");
				Ui()->DoEditBox(&s_TranslateRegion, &ControlCol, LG_BodySize);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				// CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				// Ui()->DoLabel(&Row, TCLocalize("自动翻译会跳过简体、繁体和服务器消息"), LG_BodySize * 0.8f, TEXTALIGN_ML);
				// CardContent.HSplitTop(LG_LineSpacing / 2.0f, nullptr, &CardContent);
				//
				// CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				// Ui()->DoLabel(&Row, TCLocalize("发送时可在末尾加 [ru]、[en]、[ja] 等目标语言代码"), LG_BodySize * 0.8f, TEXTALIGN_ML);
				// CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::QiaFen:
			{
				// ========== 模块 4: 关键词回复 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect Card3_5Start = Column;
				s_GlassCards.push_back(Card3_5Start);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 8, TCLocalize("Keyword reply", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Automatic replies for chat keywords", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmAutoReplyCooldown, &g_Config.m_QmAutoReplyCooldown, &Row, TCLocalize("Auto reply cooldown", QMCLIENT_LOCALIZATION_CONTEXT), 0, 30, &CUi::ms_LinearScrollbarScale, 0, TCLocalize(" sec", QMCLIENT_LOCALIZATION_CONTEXT));
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				auto SyncRuleRowsFromConfig = [](std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, bool &Inited, const char *pConfigRules) {
					std::vector<SAutoReplyRulePlain> vParsedRules;
					ParseAutoReplyRules(pConfigRules, vParsedRules);

					const auto RebuildRows = [&]() {
						vRows.clear();
						for(const auto &Rule : vParsedRules)
							vRows.push_back(CreateAutoReplyRuleInputRow(Rule.m_Keywords.c_str(), Rule.m_Reply.c_str(), Rule.m_AutoRename, Rule.m_Regex));
					};

					bool HasActiveInput = false;
					for(const auto &pRow : vRows)
					{
						if(pRow->m_TriggerInput.IsActive() || pRow->m_ReplyInput.IsActive())
						{
							HasActiveInput = true;
							break;
						}
					}

					if(!Inited)
					{
						RebuildRows();
						Inited = true;
					}
					else if(!HasActiveInput && !AutoReplyRowsMatchRules(vRows, vParsedRules))
					{
						RebuildRows();
					}
				};

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmKeywordReplyEnabled, TCLocalize("Enable keyword reply", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmKeywordReplyEnabled, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmKeywordReplyUseDummy, TCLocalize("Reply with dummy", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmKeywordReplyUseDummy, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				static std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> s_vKeywordRuleRows;
				static bool s_KeywordRuleRowsInited = false;
				static CButtonContainer s_KeywordAddRuleButton;
				static std::vector<CButtonContainer> s_vKeywordRemoveRuleButtons;
				SyncRuleRowsFromConfig(s_vKeywordRuleRows, s_KeywordRuleRowsInited, g_Config.m_QmKeywordReplyRules);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("Keyword rules", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				CUIRect AddRuleButtonRect;
				ControlCol.VSplitRight(maximum(LG_LineHeight, 24.0f * UiScale), &ControlCol, &AddRuleButtonRect);
				if(DoButton_Menu(&s_KeywordAddRuleButton, "+", 0, &AddRuleButtonRect))
				{
					auto pNewRule = CreateAutoReplyRuleInputRow();
					pNewRule->m_TriggerInput.Activate(EInputPriority::UI);
					s_vKeywordRuleRows.push_back(std::move(pNewRule));
				}
				s_vKeywordRemoveRuleButtons.resize(s_vKeywordRuleRows.size());
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				for(size_t i = 0; i < s_vKeywordRuleRows.size();)
				{
					auto &pRuleRow = s_vKeywordRuleRows[i];
					pRuleRow->m_TriggerInput.SetEmptyText("");
					pRuleRow->m_ReplyInput.SetEmptyText("");
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					CUIRect OptionsCol;
					Row.VSplitLeft(LG_LabelWidth, &OptionsCol, &ControlCol);
					CUIRect RenameCol, RegexCol, TriggerCol, SendCol, ReplyCol, RemoveButtonRect;
					ControlCol.VSplitRight(maximum(LG_LineHeight, 24.0f * UiScale), &ControlCol, &RemoveButtonRect);
					ControlCol.VSplitLeft(ControlCol.w * 0.45f, &TriggerCol, &ControlCol);
					ControlCol.VSplitLeft(maximum(40.0f, 40.0f * UiScale), &SendCol, &ReplyCol);
					OptionsCol.VSplitLeft(maximum(54.0f, 54.0f * UiScale), &RenameCol, &OptionsCol);
					OptionsCol.VSplitLeft(maximum(54.0f, 54.0f * UiScale), &RegexCol, &OptionsCol);
					DoButton_CheckBoxAutoVMarginAndSet(&pRuleRow->m_AutoRename, TCLocalize("Rename", QMCLIENT_LOCALIZATION_CONTEXT), &pRuleRow->m_AutoRename, &RenameCol, LG_LineHeight);
					DoButton_CheckBoxAutoVMarginAndSet(&pRuleRow->m_Regex, TCLocalize("Regex", QMCLIENT_LOCALIZATION_CONTEXT), &pRuleRow->m_Regex, &RegexCol, LG_LineHeight);
					Ui()->DoEditBox(&pRuleRow->m_TriggerInput, &TriggerCol, LG_BodySize);
					Ui()->DoLabel(&SendCol, TCLocalize("Send", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_MC);
					Ui()->DoEditBox(&pRuleRow->m_ReplyInput, &ReplyCol, LG_BodySize);
					const bool RemoveClicked = DoButton_Menu(&s_vKeywordRemoveRuleButtons[i], "-", 0, &RemoveButtonRect);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					if(RemoveClicked)
					{
						s_vKeywordRuleRows.erase(s_vKeywordRuleRows.begin() + i);
						s_vKeywordRemoveRuleButtons.erase(s_vKeywordRemoveRuleButtons.begin() + i);
						continue;
					}
					++i;
				}

				char aKeywordRules[sizeof(g_Config.m_QmKeywordReplyRules)];
				BuildAutoReplyRulesFromRows(s_vKeywordRuleRows, aKeywordRules, sizeof(aKeywordRules));
				str_copy(g_Config.m_QmKeywordReplyRules, aKeywordRules, sizeof(g_Config.m_QmKeywordReplyRules));

				bool KeywordHalfFilled = false;
				for(const auto &pRuleRow : s_vKeywordRuleRows)
				{
					if(IsAutoReplyRuleRowHalfFilled(*pRuleRow))
					{
						KeywordHalfFilled = true;
						break;
					}
				}
				if(KeywordHalfFilled)
				{
					CardContent.HSplitTop(LG_LineHeight * 0.8f, &Row, &CardContent);
					TextRender()->TextColor(1.0f, 0.2f, 0.2f, 1.0f);
					Ui()->DoLabel(&Row, TCLocalize("Both sides of a keyword rule must be filled in", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize * 0.7f, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::PieMenu:
			{
				// ========== 模块 5: 饼菜单 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect Card3_6Start = Column;
				s_GlassCards.push_back(Card3_6Start);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 9, TCLocalize("Pie menu", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Open a prototype menu for quick access to common actions", QMCLIENT_LOCALIZATION_CONTEXT));
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPieMenuEnabled, TCLocalize("Enable pie menu", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmPieMenuEnabled, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				bool BlockPieMenuCardDrag = Ui()->IsPopupOpen(&m_ColorPickerPopupContext) || Ui()->IsPopupHovered();
				if(g_Config.m_QmPieMenuEnabled)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmPieMenuScale, &g_Config.m_QmPieMenuScale, &Row, TCLocalize("UI scale", QMCLIENT_LOCALIZATION_CONTEXT), 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmPieMenuOpacity, &g_Config.m_QmPieMenuOpacity, &Row, TCLocalize("Pie menu opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmPieMenuMaxDistance, &g_Config.m_QmPieMenuMaxDistance, &Row, TCLocalize("Detection distance", QMCLIENT_LOCALIZATION_CONTEXT), 100, 2000);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("Rename queue", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					static CLineInput s_PieMenuRenameQueue(g_Config.m_QmPieMenuRenameQueue, sizeof(g_Config.m_QmPieMenuRenameQueue));
					s_PieMenuRenameQueue.SetEmptyText(TCLocalize("Example: Name1|Name2|Name3", QMCLIENT_LOCALIZATION_CONTEXT));
					Ui()->DoEditBox(&s_PieMenuRenameQueue, &ControlCol, LG_BodySize);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_BodySize, &Row, &CardContent);
					TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.8f));
					Ui()->DoLabel(&Row, TCLocalize("Option colors", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					struct SPieMenuColorEntry
					{
						const char *m_pName;
						const char *m_pIcon;
						unsigned int *m_pColorValue;
						ColorRGBA m_DefaultColor;
					};

					const std::array<SPieMenuColorEntry, 6> aPieMenuColorEntries = {{
						{TCLocalize("Friend", QMCLIENT_LOCALIZATION_CONTEXT), "♥", (unsigned int *)&g_Config.m_QmPieMenuColorFriend, ColorRGBA(0.9f, 0.3f, 0.4f)},
						{TCLocalize("Whisper", QMCLIENT_LOCALIZATION_CONTEXT), "✉", (unsigned int *)&g_Config.m_QmPieMenuColorWhisper, ColorRGBA(0.5f, 0.35f, 0.7f)},
						{TCLocalize("Mention", QMCLIENT_LOCALIZATION_CONTEXT), "➤", (unsigned int *)&g_Config.m_QmPieMenuColorMention, ColorRGBA(0.85f, 0.5f, 0.2f)},
						{TCLocalize("Copy skin", QMCLIENT_LOCALIZATION_CONTEXT), "⚡", (unsigned int *)&g_Config.m_QmPieMenuColorCopySkin, ColorRGBA(0.25f, 0.55f, 0.8f)},
						{TCLocalize("Swap", QMCLIENT_LOCALIZATION_CONTEXT), "⇄", (unsigned int *)&g_Config.m_QmPieMenuColorSwap, ColorRGBA(0.8f, 0.3f, 0.3f)},
						{TCLocalize("Spectate", QMCLIENT_LOCALIZATION_CONTEXT), "👁", (unsigned int *)&g_Config.m_QmPieMenuColorSpectate, ColorRGBA(0.45f, 0.55f, 0.6f)},
					}};

					auto OpenPieMenuColorPopup = [&](unsigned int *pColorValue) {
						ColorHSLA HslaColor = ColorHSLA(*pColorValue, false);
						m_ColorPickerPopupContext.m_pHslaColor = pColorValue;
						m_ColorPickerPopupContext.m_HslaColor = HslaColor;
						m_ColorPickerPopupContext.m_HsvaColor = color_cast<ColorHSVA>(HslaColor);
						m_ColorPickerPopupContext.m_RgbaColor = color_cast<ColorRGBA>(m_ColorPickerPopupContext.m_HsvaColor);
						m_ColorPickerPopupContext.m_Alpha = false;
						Ui()->ShowPopupColorPicker(Ui()->MouseX(), Ui()->MouseY(), &m_ColorPickerPopupContext);
					};

					constexpr float PieMenuPreviewStartAngle = -90.0f;
					constexpr float PieMenuPreviewSectorGap = 3.6f;
					constexpr float PieMenuPreviewInnerRatio = 108.0f / 288.0f;
					constexpr float PieMenuPreviewHighlightScale = 1.12f;

					const float PreviewBaseSide = minimum(CardContent.w, std::clamp(CardContent.w * 0.88f, LG_LineHeight * 10.0f, LG_LineHeight * 13.5f));
					const float PreviewSide = PreviewBaseSide * 0.8f;
					CUIRect PreviewRow;
					CardContent.HSplitTop(PreviewSide, &PreviewRow, &CardContent);

					CUIRect PreviewRect, PreviewInfoRect;
					PreviewRow.VSplitLeft(PreviewSide, &PreviewRect, &PreviewInfoRect);
					PreviewInfoRect.VSplitLeft(maximum(LG_CardPadding * 0.8f, LG_LineSpacing * 2.0f), nullptr, &PreviewInfoRect);
					PreviewRect.Margin(LG_LineSpacing * 0.5f, &PreviewRect);

					CUIRect PreviewFrame = PreviewRect;
					PreviewFrame.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f), IGraphics::CORNER_ALL, LG_CornerRadius * 0.8f);
					PreviewRect.Margin(maximum(4.0f, LG_LineSpacing * 0.6f), &PreviewRect);

					const vec2 PreviewCenter = PreviewRect.Center();
					const float BaseOuterRadius = maximum(1.0f, minimum(PreviewRect.w, PreviewRect.h) * 0.5f - LG_LineSpacing * 0.8f);
					const float InnerRadius = BaseOuterRadius * PieMenuPreviewInnerRatio;
					const float CenterRadius = maximum(1.0f, InnerRadius - maximum(4.0f, BaseOuterRadius * 0.03f));
					const float AnglePerSector = 360.0f / (float)std::size(aPieMenuColorEntries);
					const float PreviewAlpha = std::clamp(g_Config.m_QmPieMenuOpacity / 100.0f, 0.2f, 1.0f);

					int PopupSectorIndex = -1;
					if(Ui()->IsPopupOpen(&m_ColorPickerPopupContext))
					{
						for(size_t i = 0; i < aPieMenuColorEntries.size(); ++i)
						{
							if(m_ColorPickerPopupContext.m_pHslaColor == aPieMenuColorEntries[i].m_pColorValue)
							{
								PopupSectorIndex = (int)i;
								break;
							}
						}
					}

					static bool s_PieMenuColorPreviewPressed = false;
					if(Ui()->MouseButtonClicked(0) && Ui()->MouseHovered(&PreviewFrame))
						s_PieMenuColorPreviewPressed = true;
					if(!Ui()->MouseButton(0))
						s_PieMenuColorPreviewPressed = false;
					BlockPieMenuCardDrag = BlockPieMenuCardDrag || s_PieMenuColorPreviewPressed || Ui()->MouseHovered(&PreviewFrame);

					int HoveredSector = -1;
					if(Ui()->MouseInside(&PreviewFrame))
					{
						const vec2 MouseDir = Ui()->MousePos() - PreviewCenter;
						const float MouseDist = length(MouseDir);
						if(MouseDist >= InnerRadius && MouseDist <= BaseOuterRadius * PieMenuPreviewHighlightScale)
						{
							float MouseAngle = atan2(MouseDir.y, MouseDir.x) * 180.0f / pi;
							while(MouseAngle < 0.0f)
								MouseAngle += 360.0f;
							while(MouseAngle >= 360.0f)
								MouseAngle -= 360.0f;

							float AdjustedAngle = MouseAngle - PieMenuPreviewStartAngle;
							while(AdjustedAngle < 0.0f)
								AdjustedAngle += 360.0f;
							while(AdjustedAngle >= 360.0f)
								AdjustedAngle -= 360.0f;

							const int SectorIndex = (int)(AdjustedAngle / AnglePerSector);
							const float AngleInSector = AdjustedAngle - SectorIndex * AnglePerSector;
							if(SectorIndex >= 0 && SectorIndex < (int)aPieMenuColorEntries.size() && AngleInSector >= PieMenuPreviewSectorGap * 0.5f && AngleInSector <= AnglePerSector - PieMenuPreviewSectorGap * 0.5f)
								HoveredSector = SectorIndex;
						}
					}

					static CButtonContainer s_PieMenuColorPreviewButton;
					if(Ui()->DoButtonLogic(&s_PieMenuColorPreviewButton, 0, &PreviewFrame, BUTTONFLAG_LEFT) && HoveredSector >= 0)
						OpenPieMenuColorPopup(aPieMenuColorEntries[HoveredSector].m_pColorValue);

					for(size_t i = 0; i < aPieMenuColorEntries.size(); ++i)
					{
						const bool Highlighted = (int)i == HoveredSector || (int)i == PopupSectorIndex;
						const float HighlightScale = Highlighted ? PieMenuPreviewHighlightScale : 1.0f;
						const float OuterRadius = BaseOuterRadius * HighlightScale;
						const float StartAngle = PieMenuPreviewStartAngle + AnglePerSector * i + PieMenuPreviewSectorGap * 0.5f;
						const float EndAngle = StartAngle + AnglePerSector - PieMenuPreviewSectorGap;

						ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(*aPieMenuColorEntries[i].m_pColorValue));
						if(Highlighted)
						{
							Color.r = minimum(Color.r * 1.3f, 1.0f);
							Color.g = minimum(Color.g * 1.3f, 1.0f);
							Color.b = minimum(Color.b * 1.3f, 1.0f);
							Color.a = minimum(Color.a * 1.2f, 1.0f);
						}

						Graphics()->TextureClear();
						Graphics()->QuadsBegin();
						Graphics()->SetColor(Color.r, Color.g, Color.b, Color.a * PreviewAlpha);
						for(int Segment = 0; Segment < 24; ++Segment)
						{
							const float Angle1 = StartAngle + (EndAngle - StartAngle) * (Segment / 24.0f);
							const float Angle2 = StartAngle + (EndAngle - StartAngle) * ((Segment + 1) / 24.0f);
							const float Rad1 = Angle1 * pi / 180.0f;
							const float Rad2 = Angle2 * pi / 180.0f;

							const vec2 Inner1 = PreviewCenter + vec2(cos(Rad1), sin(Rad1)) * InnerRadius;
							const vec2 Outer1 = PreviewCenter + vec2(cos(Rad1), sin(Rad1)) * OuterRadius;
							const vec2 Inner2 = PreviewCenter + vec2(cos(Rad2), sin(Rad2)) * InnerRadius;
							const vec2 Outer2 = PreviewCenter + vec2(cos(Rad2), sin(Rad2)) * OuterRadius;

							const IGraphics::CFreeformItem Freeform(
								Inner1.x, Inner1.y,
								Outer1.x, Outer1.y,
								Inner2.x, Inner2.y,
								Outer2.x, Outer2.y);
							Graphics()->QuadsDrawFreeform(&Freeform, 1);
						}
						Graphics()->QuadsEnd();

						const float MidRadius = (InnerRadius + OuterRadius) * 0.5f;
						const float MidAngle = (StartAngle + EndAngle) * 0.5f * pi / 180.0f;
						const vec2 ItemPos = PreviewCenter + vec2(cos(MidAngle), sin(MidAngle)) * MidRadius;
						const float IconSize = maximum(LG_BodySize * 1.45f, BaseOuterRadius * (Highlighted ? 0.20f : 0.163f));
						const float IconYOffset = BaseOuterRadius * 0.0625f;
						const float TextSize = maximum(LG_BodySize * 0.95f, BaseOuterRadius * (Highlighted ? 0.10f : 0.08f));
						const float TextYOffset = BaseOuterRadius * 0.0486f;

						TextRender()->TextColor(1.0f, 1.0f, 1.0f, PreviewAlpha);
						const float IconWidth = TextRender()->TextWidth(IconSize, aPieMenuColorEntries[i].m_pIcon);
						TextRender()->Text(ItemPos.x - IconWidth * 0.5f, ItemPos.y - IconSize * 0.5f - IconYOffset, IconSize, aPieMenuColorEntries[i].m_pIcon);

						const float NameWidth = TextRender()->TextWidth(TextSize, aPieMenuColorEntries[i].m_pName);
						TextRender()->Text(ItemPos.x - NameWidth * 0.5f, ItemPos.y + TextYOffset, TextSize, aPieMenuColorEntries[i].m_pName);
					}

					Graphics()->TextureClear();
					Graphics()->QuadsBegin();
					Graphics()->SetColor(0.15f, 0.15f, 0.2f, 0.9f * PreviewAlpha);
					Graphics()->DrawCircle(PreviewCenter.x, PreviewCenter.y, CenterRadius, 48);
					Graphics()->QuadsEnd();

					const int FocusedSector = HoveredSector >= 0 ? HoveredSector : PopupSectorIndex;
					const char *pCenterTitle = FocusedSector >= 0 ? aPieMenuColorEntries[FocusedSector].m_pName : TCLocalize("Click a sector", QMCLIENT_LOCALIZATION_CONTEXT);
					const char *pCenterSubtitle = FocusedSector >= 0 ? TCLocalize("Open color picker", QMCLIENT_LOCALIZATION_CONTEXT) : TCLocalize("Set colors", QMCLIENT_LOCALIZATION_CONTEXT);
					const char *pHintText = FocusedSector >= 0 ? aPieMenuColorEntries[FocusedSector].m_pName : TCLocalize("Click any sector to set its color", QMCLIENT_LOCALIZATION_CONTEXT);
					const float CenterTitleSize = maximum(LG_BodySize * 1.05f, BaseOuterRadius * 0.095f);
					const float CenterSubtitleSize = maximum(LG_TipSize, BaseOuterRadius * 0.055f);

					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.98f);
					const float CenterTitleWidth = TextRender()->TextWidth(CenterTitleSize, pCenterTitle);
					TextRender()->Text(PreviewCenter.x - CenterTitleWidth * 0.5f, PreviewCenter.y - CenterTitleSize * 0.9f, CenterTitleSize, pCenterTitle);
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.68f);
					const float CenterSubtitleWidth = TextRender()->TextWidth(CenterSubtitleSize, pCenterSubtitle);
					TextRender()->Text(PreviewCenter.x - CenterSubtitleWidth * 0.5f, PreviewCenter.y + CenterSubtitleSize * 0.1f, CenterSubtitleSize, pCenterSubtitle);
					TextRender()->TextColor(TextRender()->DefaultTextColor());

					CUIRect PreviewInfoContent = PreviewInfoRect;
					const float InfoSpacing = LG_LineSpacing * 0.75f;
					const float InfoHeight = LG_LineHeight * 2.0f + InfoSpacing;
					if(PreviewInfoContent.h > InfoHeight)
						PreviewInfoContent.HSplitTop((PreviewInfoContent.h - InfoHeight) * 0.5f, nullptr, &PreviewInfoContent);

					CUIRect PieMenuColorHintRow, PieMenuColorResetRow;
					PreviewInfoContent.HSplitTop(LG_LineHeight, &PieMenuColorHintRow, &PreviewInfoContent);
					PreviewInfoContent.HSplitTop(InfoSpacing, nullptr, &PreviewInfoContent);
					PreviewInfoContent.HSplitTop(LG_LineHeight, &PieMenuColorResetRow, &PreviewInfoContent);

					Ui()->DoLabel(&PieMenuColorHintRow, pHintText, LG_BodySize * 0.9f, TEXTALIGN_MR);

					static CButtonContainer s_PieMenuColorResetAllButton;
					CUIRect PieMenuColorResetButton;
					PieMenuColorResetRow.VSplitRight(maximum(88.0f, 88.0f * UiScale), nullptr, &PieMenuColorResetButton);
					if(DoButton_Menu(&s_PieMenuColorResetAllButton, TCLocalize("Reset all", QMCLIENT_LOCALIZATION_CONTEXT), 0, &PieMenuColorResetButton))
					{
						for(const auto &Entry : aPieMenuColorEntries)
							*Entry.m_pColorValue = color_cast<ColorHSLA>(Entry.m_DefaultColor).Pack(false);
					}
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back(), BlockPieMenuCardDrag);

			}
			break;
			case EQmModuleId::CameraView:
			{
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardCameraViewStart = Column;
				s_GlassCards.push_back(CardCameraViewStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 10, TCLocalize("镜头与视野", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("镜头漂移、动态视野与纵横比预设", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmCameraDrift, TCLocalize("启用镜头漂移", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmCameraDrift, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				if(g_Config.m_QmCameraDrift)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmCameraDriftAmount, &g_Config.m_QmCameraDriftAmount, &Row, TCLocalize("漂移强度", QMCLIENT_LOCALIZATION_CONTEXT), 0, 200);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmCameraDriftSmoothness, &g_Config.m_QmCameraDriftSmoothness, &Row, TCLocalize("漂移平滑度", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmCameraDriftReverse, TCLocalize("反向漂移方向", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmCameraDriftReverse, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmDynamicFov, TCLocalize("启用动态视野", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmDynamicFov, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				if(g_Config.m_QmDynamicFov)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmDynamicFovAmount, &g_Config.m_QmDynamicFovAmount, &Row, TCLocalize("动态视野强度", QMCLIENT_LOCALIZATION_CONTEXT), 0, 200);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmDynamicFovSmoothness, &g_Config.m_QmDynamicFovSmoothness, &Row, TCLocalize("动态视野平滑度", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				bool AspectChanged = false;
				const char *apAspectPresetNames[] = {
					TCLocalize("关闭", QMCLIENT_LOCALIZATION_CONTEXT),
					"5:4",
					"4:3",
					"3:2",
					"16:9",
					"21:9",
					TCLocalize("自定义", QMCLIENT_LOCALIZATION_CONTEXT),
				};
				static CUi::SDropDownState s_AspectPresetDropDownState;
				static CScrollRegion s_AspectPresetDropDownScrollRegion;
				s_AspectPresetDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_AspectPresetDropDownScrollRegion;
				const int CurrentPreset = std::clamp(g_Config.m_QmAspectPreset, 0, 6);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("纵横比预设", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				const int NewPreset = Ui()->DoDropDown(&ControlCol, CurrentPreset, apAspectPresetNames, static_cast<int>(std::size(apAspectPresetNames)), s_AspectPresetDropDownState);
				if(NewPreset != CurrentPreset)
				{
					g_Config.m_QmAspectPreset = NewPreset;
					switch(NewPreset)
					{
					case 1: g_Config.m_QmAspectRatio = 125; break;
					case 2: g_Config.m_QmAspectRatio = 133; break;
					case 3: g_Config.m_QmAspectRatio = 150; break;
					case 4: g_Config.m_QmAspectRatio = 178; break;
					case 5: g_Config.m_QmAspectRatio = 233; break;
					case 6:
						if(g_Config.m_QmAspectRatio < 100)
							g_Config.m_QmAspectRatio = 178;
						break;
					default: break;
					}
					AspectChanged = true;
				}
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmAspectPreset == 6)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					if(Ui()->DoScrollbarOption(&g_Config.m_QmAspectRatio, &g_Config.m_QmAspectRatio, &Row, TCLocalize("自定义比例", QMCLIENT_LOCALIZATION_CONTEXT), 100, 300, &CUi::ms_LinearScrollbarScale, 0, " x100"))
						AspectChanged = true;
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				int EffectiveAspectValue = 0;
				switch(g_Config.m_QmAspectPreset)
				{
				case 1: EffectiveAspectValue = 125; break;
				case 2: EffectiveAspectValue = 133; break;
				case 3: EffectiveAspectValue = 150; break;
				case 4: EffectiveAspectValue = 178; break;
				case 5: EffectiveAspectValue = 233; break;
				case 6: EffectiveAspectValue = std::clamp(g_Config.m_QmAspectRatio, 100, 300); break;
				default: break;
				}

				CardContent.HSplitTop(LG_BodySize, &Row, &CardContent);
				char aAspectInfo[128];
				if(EffectiveAspectValue > 0)
					str_format(aAspectInfo, sizeof(aAspectInfo), "%s %.2f:1", TCLocalize("当前比例：", QMCLIENT_LOCALIZATION_CONTEXT), EffectiveAspectValue / 100.0f);
				else
					str_copy(aAspectInfo, TCLocalize("当前比例：显示器默认（全局生效）", QMCLIENT_LOCALIZATION_CONTEXT), sizeof(aAspectInfo));
				Ui()->DoLabel(&Row, aAspectInfo, LG_BodySize, TEXTALIGN_ML);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_BodySize, &Row, &CardContent);
				Ui()->DoLabel(&Row, TCLocalize("当前纵横比预设会作用于整个客户端画面", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize * 0.9f, TEXTALIGN_ML);

				if(AspectChanged)
					GameClient()->m_TClient.QueueAspectApply();

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::EntityOverlay:
			{
				// ========== 模块 8.5: 实体层颜色 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardEntityOverlayStart = Column;
				s_GlassCards.push_back(CardEntityOverlayStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 6, TCLocalize("Entity overlay colors", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Entity layer tile colors", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoLabel(&Row, TCLocalize("The entities layer must be enabled", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayDeathAlpha, &g_Config.m_QmEntityOverlayDeathAlpha, &Row, TCLocalize("Death opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayFreezeAlpha, &g_Config.m_QmEntityOverlayFreezeAlpha, &Row, TCLocalize("Freeze opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayUnfreezeAlpha, &g_Config.m_QmEntityOverlayUnfreezeAlpha, &Row, TCLocalize("Unfreeze opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayDeepFreezeAlpha, &g_Config.m_QmEntityOverlayDeepFreezeAlpha, &Row, TCLocalize("Deep freeze opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayDeepUnfreezeAlpha, &g_Config.m_QmEntityOverlayDeepUnfreezeAlpha, &Row, TCLocalize("Deep unfreeze opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayTeleAlpha, &g_Config.m_QmEntityOverlayTeleAlpha, &Row, TCLocalize("Tele opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayTeleCheckpointAlpha, &g_Config.m_QmEntityOverlayTeleCheckpointAlpha, &Row, TCLocalize("Checkpoint tele opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlaySwitchAlpha, &g_Config.m_QmEntityOverlaySwitchAlpha, &Row, TCLocalize("Switch opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_ClOverlayEntities, &g_Config.m_ClOverlayEntities, &Row, TCLocalize("Overlay opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::Laser:
			{
				// ========== 模块 9: 激光设置 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect Card6Start = Column;
				s_GlassCards.push_back(Card6Start);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 5, TCLocalize("Laser settings", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Laser style", QMCLIENT_LOCALIZATION_CONTEXT));
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmLaserEnhanced, TCLocalize("Enhanced laser effects", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmLaserEnhanced, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmLaserGlowIntensity, &g_Config.m_QmLaserGlowIntensity, &Row, TCLocalize("Glow intensity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmLaserSize, &g_Config.m_QmLaserSize, &Row, TCLocalize("Laser size", QMCLIENT_LOCALIZATION_CONTEXT), 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmLaserAlpha, &g_Config.m_QmLaserAlpha, &Row, TCLocalize("Transparency", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmLaserRoundCaps, TCLocalize("Rounded caps", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmLaserRoundCaps, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmLaserEnhanced)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmLaserPulseSpeed, &g_Config.m_QmLaserPulseSpeed, &Row, TCLocalize("Pulse speed", QMCLIENT_LOCALIZATION_CONTEXT), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmLaserPulseAmplitude, &g_Config.m_QmLaserPulseAmplitude, &Row, TCLocalize("Pulse amplitude", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				// 激光预览区域
				CardContent.HSplitTop(LG_LineSpacing * 2, nullptr, &CardContent);
				CardContent.HSplitTop(LG_BodySize, &Row, &CardContent);
				Ui()->DoLabel(&Row, TCLocalize("Laser preview", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				const float LaserPreviewHeightQM = std::clamp(56.0f * UiScale, 40.0f, 56.0f);
				CUIRect LaserPreviewRectQM;
				CardContent.HSplitTop(LaserPreviewHeightQM, &LaserPreviewRectQM, &CardContent);
				LaserPreviewRectQM.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, 8.0f);
				{
					ColorHSLA OutlineColor = ColorHSLA(g_Config.m_ClLaserRifleOutlineColor);
					ColorHSLA InnerColor = ColorHSLA(g_Config.m_ClLaserRifleInnerColor);
					DoLaserPreview(&LaserPreviewRectQM, OutlineColor, InnerColor, LASERTYPE_RIFLE);
				}

				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LaserPreviewHeightQM, &LaserPreviewRectQM, &CardContent);
				LaserPreviewRectQM.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), IGraphics::CORNER_ALL, 8.0f);
				{
					ColorHSLA OutlineColor = ColorHSLA(g_Config.m_ClLaserShotgunOutlineColor);
					ColorHSLA InnerColor = ColorHSLA(g_Config.m_ClLaserShotgunInnerColor);
					DoLaserPreview(&LaserPreviewRectQM, OutlineColor, InnerColor, LASERTYPE_SHOTGUN);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::PlayerStats:
			{
				// ========== 模块10: 玩家统计 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect Card7Start = Column;
				s_GlassCards.push_back(Card7Start);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 6, TCLocalize("Player stats", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Player stats and info display", QMCLIENT_LOCALIZATION_CONTEXT));

				// 显示统计HUD
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerStatsHud, TCLocalize("Show player stats HUD", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmPlayerStatsHud, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmPlayerStatsHud)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerStatsMapProgress, TCLocalize("Map progress bar (experimental)", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmPlayerStatsMapProgress, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					if(g_Config.m_QmPlayerStatsMapProgress)
					{
						CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
						DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerStatsMapProgressStyle, TCLocalize("Use embedded HUD progress bar", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmPlayerStatsMapProgressStyle, &Row, LG_LineHeight);
						CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

						if(g_Config.m_QmPlayerStatsMapProgressStyle == 0)
						{
							static CButtonContainer s_MapProgressColorId;

							DoLine_ColorPicker(&s_MapProgressColorId, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("Progress bar color", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmPlayerStatsMapProgressColor, ColorRGBA(36.0f / 255.0f, 199.0f / 255.0f, 100.0f / 255.0f, 1.0f), false, nullptr, true);

							CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
							Ui()->DoScrollbarOption(&g_Config.m_QmPlayerStatsMapProgressWidth, &g_Config.m_QmPlayerStatsMapProgressWidth, &Row, TCLocalize("Progress bar width", QMCLIENT_LOCALIZATION_CONTEXT), 10, 80, &CUi::ms_LinearScrollbarScale, 0, "%");
							CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

							CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
							Ui()->DoScrollbarOption(&g_Config.m_QmPlayerStatsMapProgressHeight, &g_Config.m_QmPlayerStatsMapProgressHeight, &Row, TCLocalize("Progress bar height", QMCLIENT_LOCALIZATION_CONTEXT), 6, 30, &CUi::ms_LinearScrollbarScale, 0, "px");
							CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

							CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
							Ui()->DoScrollbarOption(&g_Config.m_QmPlayerStatsMapProgressPosX, &g_Config.m_QmPlayerStatsMapProgressPosX, &Row, TCLocalize("Horizontal position", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
							CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

							CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
							Ui()->DoScrollbarOption(&g_Config.m_QmPlayerStatsMapProgressPosY, &g_Config.m_QmPlayerStatsMapProgressPosY, &Row, TCLocalize("Vertical position", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
							CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
						}

						CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
						DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerStatsMapProgressDbgRoute, TCLocalize("Show dotted map route debug", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmPlayerStatsMapProgressDbgRoute, &Row, LG_LineHeight);
						CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					}
				}

				// 进入服务器时重置统计
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerStatsResetOnJoin, TCLocalize("Reset stats when joining a server", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmPlayerStatsResetOnJoin, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::CollisionHitbox:
			{
				// ========== 模块11: 碰撞体积可视化 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect Card8Start_CollisionHitbox = Column;
				s_GlassCards.push_back(Card8Start_CollisionHitbox);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 7, TCLocalize("Collision hitbox", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Show the base player collision box", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowCollisionHitbox, TCLocalize("Show collision hitbox", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmShowCollisionHitbox, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmShowCollisionHitbox)
				{
					static CButtonContainer s_FreezeColorId;
					DoLine_ColorPicker(&s_FreezeColorId, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("Freeze border color", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmCollisionHitboxColorFreeze, ColorRGBA(1.0f, 0.0f, 1.0f), false);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmCollisionHitboxAlpha, &g_Config.m_QmCollisionHitboxAlpha, &Row, TCLocalize("Hitbox opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::FavoriteMaps:
			{
				// ========== 模块12: 收藏地图 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect Card8Start = Column;
				s_GlassCards.push_back(Card8Start);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 7, TCLocalize("Favorite maps", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Favorite map manager", QMCLIENT_LOCALIZATION_CONTEXT));

				// 收藏地图列表
				const auto &FavMaps = GameClient()->m_TClient.GetFavoriteMaps();

				auto MapCategoryKeyFromText = [&](const char *pText) -> const char * {
					if(!pText || pText[0] == '\0')
						return nullptr;
					if(str_find_nocase(pText, "DDmaX"))
					{
						if(str_find_nocase(pText, "Easy"))
							return "DDmaX Easy";
						if(str_find_nocase(pText, "Next"))
							return "DDmaX Next";
						if(str_find_nocase(pText, "Pro"))
							return "DDmaX Pro";
						if(str_find_nocase(pText, "Nut"))
							return "DDmaX Nut";
						return "DDmaX";
					}
					if(str_find_nocase(pText, "Oldschool"))
						return "Oldschool";
					if(str_find_nocase(pText, "Novice"))
						return "Novice";
					if(str_find_nocase(pText, "Moderate"))
						return "Moderate";
					if(str_find_nocase(pText, "Brutal"))
						return "Brutal";
					if(str_find_nocase(pText, "Insane"))
						return "Insane";
					if(str_find_nocase(pText, "Dummy"))
						return "Dummy";
					if(str_find_nocase(pText, "Solo"))
						return "Solo";
					if(str_find_nocase(pText, "Race"))
						return "Race";
					if(str_find_nocase(pText, "Fun"))
						return "Fun";
					if(str_find_nocase(pText, "Event"))
						return "Event";
					return nullptr;
				};

				auto MapTypeDisplayName = [&](const char *pType) -> const char * {
					if(!pType || pType[0] == '\0')
						return TCLocalize("Unknown", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "DDmaX Easy") == 0)
						return TCLocalize("Classic easy", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "DDmaX Next") == 0)
						return TCLocalize("Classic next", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "DDmaX Pro") == 0)
						return TCLocalize("Classic pro", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "DDmaX Nut") == 0)
						return TCLocalize("Classic nut", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "DDmaX") == 0)
						return TCLocalize("Classic", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "Novice") == 0)
						return TCLocalize("Novice", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "Moderate") == 0)
						return TCLocalize("Moderate", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "Brutal") == 0)
						return TCLocalize("Brutal", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "Insane") == 0)
						return TCLocalize("Insane", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "Dummy") == 0)
						return TCLocalize("Dummy", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "Solo") == 0)
						return TCLocalize("Solo", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "Oldschool") == 0)
						return TCLocalize("Oldschool", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "Race") == 0)
						return TCLocalize("Race", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "Fun") == 0)
						return TCLocalize("Fun", QMCLIENT_LOCALIZATION_CONTEXT);
					if(str_comp_nocase(pType, "Event") == 0)
						return TCLocalize("Event", QMCLIENT_LOCALIZATION_CONTEXT);
					return TCLocalize("Unknown", QMCLIENT_LOCALIZATION_CONTEXT);
				};

				static std::unordered_map<std::string, std::string> s_MapCategories;
				static int s_MapCategoryScanIndex = 0;
				static int s_LastNumServers = -1;
				static float s_NextFullScan = 0.0f;
				IServerBrowser *pServerBrowser = ServerBrowser();
				const float Now = Client()->LocalTime();
				if(!pServerBrowser || FavMaps.empty())
				{
					s_MapCategories.clear();
					s_MapCategoryScanIndex = 0;
					s_LastNumServers = -1;
					s_NextFullScan = 0.0f;
				}
				else
				{
					const int NumServers = pServerBrowser->NumSortedServers();
					if(NumServers != s_LastNumServers)
					{
						s_LastNumServers = NumServers;
						s_MapCategoryScanIndex = 0;
						s_NextFullScan = 0.0f;
					}

					if(NumServers > 0 && (Now >= s_NextFullScan || s_MapCategoryScanIndex > 0))
					{
						if(s_MapCategoryScanIndex == 0)
						{
							s_MapCategories.clear();
							s_MapCategories.reserve((size_t)NumServers);
						}

						constexpr int ServersPerFrame = 64;
						int ProcessedServers = 0;
						while(s_MapCategoryScanIndex < NumServers && ProcessedServers < ServersPerFrame)
						{
							const CServerInfo *pInfo = pServerBrowser->SortedGet(s_MapCategoryScanIndex);
							++s_MapCategoryScanIndex;
							++ProcessedServers;
							if(!pInfo || pInfo->m_aMap[0] == '\0')
								continue;
							const char *pCategoryKey = MapCategoryKeyFromText(pInfo->m_aCommunityType);
							if(!pCategoryKey)
								pCategoryKey = MapCategoryKeyFromText(pInfo->m_aName);
							if(!pCategoryKey)
								continue;
							auto It = s_MapCategories.find(pInfo->m_aMap);
							if(It == s_MapCategories.end() || It->second != pCategoryKey)
							{
								s_MapCategories[pInfo->m_aMap] = pCategoryKey;
								GameClient()->m_TClient.UpdateMapCategoryCache(pInfo->m_aMap, pCategoryKey);
							}
						}

						if(s_MapCategoryScanIndex >= NumServers)
						{
							s_MapCategoryScanIndex = 0;
							s_NextFullScan = Now + 2.0f;
						}
					}

					const IServerBrowser::CServerEntry *pEntry = pServerBrowser->Find(Client()->ServerAddress());
					if(pEntry && pEntry->m_Info.m_aMap[0] != '\0')
					{
						const char *pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aCommunityType);
						if(!pCategoryKey)
							pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aName);
						if(pCategoryKey)
						{
							s_MapCategories[pEntry->m_Info.m_aMap] = pCategoryKey;
							GameClient()->m_TClient.UpdateMapCategoryCache(pEntry->m_Info.m_aMap, pCategoryKey);
						}
					}
				}

				auto GetMapCategory = [&](const char *pMapName) -> const char * {
					if(!pMapName || pMapName[0] == '\0')
						return TCLocalize("Unknown", QMCLIENT_LOCALIZATION_CONTEXT);
					const auto It = s_MapCategories.find(pMapName);
					if(It != s_MapCategories.end() && !It->second.empty())
						return MapTypeDisplayName(It->second.c_str());
					if(pServerBrowser)
					{
						const char *pCurrentMap = Client()->GetCurrentMap();
						if(pCurrentMap && str_comp(pCurrentMap, pMapName) == 0)
						{
							const IServerBrowser::CServerEntry *pEntry = pServerBrowser->Find(Client()->ServerAddress());
							if(pEntry)
							{
								const char *pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aCommunityType);
								if(!pCategoryKey)
									pCategoryKey = MapCategoryKeyFromText(pEntry->m_Info.m_aName);
								if(pCategoryKey)
									return MapTypeDisplayName(pCategoryKey);
							}
						}
					}
					const char *pCachedCategory = GameClient()->m_TClient.GetCachedMapCategoryKey(pMapName);
					if(pCachedCategory)
						return MapTypeDisplayName(pCachedCategory);
					return TCLocalize("Unknown", QMCLIENT_LOCALIZATION_CONTEXT);
				};

				// 记录复制状态和时间
				static int s_CopiedMapIndex = -1;
				static float s_CopiedTime = 0.0f;
				if(s_CopiedMapIndex >= 0 && Client()->LocalTime() - s_CopiedTime > 1.5f)
					s_CopiedMapIndex = -1;

				if(FavMaps.empty())
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoLabel(&Row, TCLocalize("No favorite maps yet", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}
				else
				{
					static int s_aMapButtonIds[64];
					static CButtonContainer s_aMapRemoveButtons[64];
					std::string RemoveMapName;
					size_t MapIndex = 0;
					for(const std::string &MapName : FavMaps)
					{
						if(MapIndex >= sizeof(s_aMapButtonIds) / sizeof(s_aMapButtonIds[0]))
							break;

						CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
						CUIRect RowLabel, RowRemove;
						Row.VSplitRight(LG_LineHeight, &RowLabel, &RowRemove);
						RowRemove.HMargin(std::clamp(2.0f * UiScale, 1.0f, 2.0f), &RowRemove);

						if(Ui()->DoButton_FontIcon(&s_aMapRemoveButtons[MapIndex], FONT_ICON_XMARK, 0, &RowRemove, IGraphics::CORNER_ALL))
						{
							if(RemoveMapName.empty())
								RemoveMapName = MapName;
							s_CopiedMapIndex = -1;
						}

						// 检测点击复制（排除删除按钮）
						if(Ui()->MouseInside(&RowLabel))
						{
							Ui()->SetHotItem(&s_aMapButtonIds[MapIndex]);
							if(Ui()->MouseButtonClicked(0))
							{
								Input()->SetClipboardText(MapName.c_str());
								s_CopiedMapIndex = (int)MapIndex;
								s_CopiedTime = Client()->LocalTime();
							}
						}

						// 显示地图名或"已复制"
						if(s_CopiedMapIndex == (int)MapIndex)
						{
							TextRender()->TextColor(0.0f, 1.0f, 0.0f, 1.0f); // 绿色
							Ui()->DoLabel(&RowLabel, TCLocalize("Copied", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
						}
						else
						{
							char aLabel[256];
							const char *pCategory = GetMapCategory(MapName.c_str());
							str_format(aLabel, sizeof(aLabel), "%s (%s)", MapName.c_str(), pCategory);
							TextRender()->TextColor(1.0f, 0.85f, 0.0f, 1.0f); // 金色
							Ui()->DoLabel(&RowLabel, aLabel, LG_BodySize, TEXTALIGN_ML);
						}
						TextRender()->TextColor(TextRender()->DefaultTextColor());

						if(Ui()->HotItem() == &s_aMapButtonIds[MapIndex])
							GameClient()->m_Tooltips.DoToolTip(&s_aMapButtonIds[MapIndex], &RowLabel, TCLocalize("Click to copy the map name", QMCLIENT_LOCALIZATION_CONTEXT));
						if(Ui()->HotItem() == &s_aMapRemoveButtons[MapIndex])
							GameClient()->m_Tooltips.DoToolTip(&s_aMapRemoveButtons[MapIndex], &RowRemove, TCLocalize("Remove from favorites", QMCLIENT_LOCALIZATION_CONTEXT));

						CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
						++MapIndex;
					}
					if(!RemoveMapName.empty())
						GameClient()->m_TClient.RemoveFavoriteMap(RemoveMapName.c_str());
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::HJAssist:
			{
					// ========== 模块13: HJ大佬辅助 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardHJStart = Column;
				s_GlassCards.push_back(CardHJStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 11, TCLocalize("HJ assist", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Unfreeze automation helpers", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoUnspecOnUnfreeze, TCLocalize("Auto unspec on unfreeze", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmAutoUnspecOnUnfreeze, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoSwitchOnUnfreeze, TCLocalize("Auto switch to the tee that got unfrozen", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmAutoSwitchOnUnfreeze, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoCloseChatOnUnfreeze, TCLocalize("Automatically close the current chat after waking from freeze", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmAutoCloseChatOnUnfreeze, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());

			}
			break;
			case EQmModuleId::InputOverlay:
			{
				// ========== 模块14: 按键显示 ==========

				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardInputOverlayStart = Column;
				s_GlassCards.push_back(CardInputOverlayStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 11, TCLocalize("Input overlay", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Input overlay display", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmInputOverlay, TCLocalize("Show inputs", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmInputOverlay, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmInputOverlay)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmInputOverlayScale, &g_Config.m_QmInputOverlayScale, &Row, TCLocalize("Size", QMCLIENT_LOCALIZATION_CONTEXT), 1, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmInputOverlayOpacity, &g_Config.m_QmInputOverlayOpacity, &Row, TCLocalize("Input overlay opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmInputOverlayPosX, &g_Config.m_QmInputOverlayPosX, &Row, TCLocalize("Horizontal position", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmInputOverlayPosY, &g_Config.m_QmInputOverlayPosY, &Row, TCLocalize("Vertical position", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_BodySize, &Row, &CardContent);
					TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.8f));
					Ui()->DoLabel(&Row, TCLocalize("Config file: data/input_overlay.json", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_BodySize, &Row, &CardContent);
					TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.8f));
					Ui()->DoLabel(&Row, TCLocalize("Auto hot-reload after external saves", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());

			}
			break;
			case EQmModuleId::Voice:
			{
				// ========== 模块15: 语音 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardVoiceStart = Column;
				s_GlassCards.push_back(CardVoiceStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 12, TCLocalize("Voice", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Voice connection, input, and display", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceEnable, TCLocalize("Enable voice", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_RiVoiceEnable, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_RiVoiceEnable)
				{
					auto AddVoiceSectionLabel = [&](const char *pTitle, const char *pHint) {
						CardContent.HSplitTop(LG_LineHeight * 0.78f, &Row, &CardContent);
						Ui()->DoLabel(&Row, pTitle, LG_BodySize * 0.96f, TEXTALIGN_ML);
						if(pHint != nullptr && pHint[0] != '\0')
						{
							CardContent.HSplitTop(LG_LineHeight * 0.68f, &Row, &CardContent);
							Ui()->DoLabel(&Row, pHint, LG_BodySize * 0.72f, TEXTALIGN_ML);
						}
						CardContent.HSplitTop(LG_LineSpacing * 0.75f, nullptr, &CardContent);
					};

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("Server", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					static CLineInput s_VoiceServer(g_Config.m_RiVoiceServer, sizeof(g_Config.m_RiVoiceServer));
					s_VoiceServer.SetEmptyText("42.194.185.210:9987");
					Ui()->DoEditBox(&s_VoiceServer, &ControlCol, LG_BodySize);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("Room password", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					static CLineInput s_VoiceToken(g_Config.m_RiVoiceToken, sizeof(g_Config.m_RiVoiceToken));
					s_VoiceToken.SetEmptyText(TCLocalize("Leave empty for the public room", QMCLIENT_LOCALIZATION_CONTEXT));
					Ui()->DoEditBox(&s_VoiceToken, &ControlCol, LG_BodySize);
					CardContent.HSplitTop(LG_LineSpacing * 1.15f, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("Input device", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize, TEXTALIGN_ML);
					static std::vector<std::string> s_VoiceInputDeviceDisplayNames;
					static std::vector<std::string> s_VoiceInputDeviceConfigValues;
					static std::vector<const char *> s_VoiceInputDeviceDropDownNames;
					static CUi::SDropDownState s_VoiceInputDeviceDropDownState;
					static CScrollRegion s_VoiceInputDeviceDropDownScrollRegion;
					static bool s_VoiceInputDevicesInitialized = false;
					s_VoiceInputDeviceDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_VoiceInputDeviceDropDownScrollRegion;
					auto RefreshVoiceInputDeviceList = [&]() {
						s_VoiceInputDeviceDisplayNames.clear();
						s_VoiceInputDeviceConfigValues.clear();
						s_VoiceInputDeviceDropDownNames.clear();

						s_VoiceInputDeviceDisplayNames.emplace_back(TCLocalize("Default microphone", QMCLIENT_LOCALIZATION_CONTEXT));
						s_VoiceInputDeviceConfigValues.emplace_back("");

						const int NumInputs = SDL_GetNumAudioDevices(1);
						for(int i = 0; i < NumInputs; i++)
						{
							const char *pName = SDL_GetAudioDeviceName(i, 1);
							if(!pName || pName[0] == '\0')
								continue;

							bool Exists = false;
							for(const auto &DeviceName : s_VoiceInputDeviceConfigValues)
							{
								if(str_comp_nocase(DeviceName.c_str(), pName) == 0)
								{
									Exists = true;
									break;
								}
							}
							if(Exists)
								continue;
							s_VoiceInputDeviceDisplayNames.emplace_back(pName);
							s_VoiceInputDeviceConfigValues.emplace_back(pName);
						}

						if(g_Config.m_RiVoiceInputDevice[0] != '\0')
						{
							bool FoundCurrent = false;
							for(const auto &DeviceName : s_VoiceInputDeviceConfigValues)
							{
								if(str_comp_nocase(DeviceName.c_str(), g_Config.m_RiVoiceInputDevice) == 0)
								{
									FoundCurrent = true;
									break;
								}
							}
							if(!FoundCurrent)
							{
								char aDisplay[160];
								str_format(aDisplay, sizeof(aDisplay), "%s (%s)", g_Config.m_RiVoiceInputDevice, TCLocalize("Current config", QMCLIENT_LOCALIZATION_CONTEXT));
								s_VoiceInputDeviceDisplayNames.emplace_back(aDisplay);
								s_VoiceInputDeviceConfigValues.emplace_back(g_Config.m_RiVoiceInputDevice);
							}
						}

						s_VoiceInputDeviceDropDownNames.reserve(s_VoiceInputDeviceDisplayNames.size());
						for(const auto &DisplayName : s_VoiceInputDeviceDisplayNames)
							s_VoiceInputDeviceDropDownNames.push_back(DisplayName.c_str());
					};
					if(!s_VoiceInputDevicesInitialized)
					{
						RefreshVoiceInputDeviceList();
						s_VoiceInputDevicesInitialized = true;
					}

					CUIRect VoiceInputDropDownRect;
					CUIRect VoiceInputRefreshButton;
					ControlCol.VSplitRight(maximum(68.0f, 68.0f * UiScale), &VoiceInputDropDownRect, &VoiceInputRefreshButton);

					int VoiceInputSelectedOld = 0;
					if(g_Config.m_RiVoiceInputDevice[0] != '\0')
					{
						for(size_t i = 1; i < s_VoiceInputDeviceConfigValues.size(); ++i)
						{
							if(str_comp_nocase(s_VoiceInputDeviceConfigValues[i].c_str(), g_Config.m_RiVoiceInputDevice) == 0)
							{
								VoiceInputSelectedOld = (int)i;
								break;
							}
						}
					}

					const int VoiceInputSelectedNew = Ui()->DoDropDown(&VoiceInputDropDownRect, VoiceInputSelectedOld, s_VoiceInputDeviceDropDownNames.data(), s_VoiceInputDeviceDropDownNames.size(), s_VoiceInputDeviceDropDownState);
					if(VoiceInputSelectedNew >= 0 && VoiceInputSelectedNew != VoiceInputSelectedOld && (size_t)VoiceInputSelectedNew < s_VoiceInputDeviceConfigValues.size())
						str_copy(g_Config.m_RiVoiceInputDevice, s_VoiceInputDeviceConfigValues[VoiceInputSelectedNew].c_str(), sizeof(g_Config.m_RiVoiceInputDevice));

					static CButtonContainer s_VoiceInputRefreshButton;
					if(DoButton_Menu(&s_VoiceInputRefreshButton, TCLocalize("Refresh", QMCLIENT_LOCALIZATION_CONTEXT), 0, &VoiceInputRefreshButton))
						RefreshVoiceInputDeviceList();
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceMicMute, TCLocalize("Mute microphone", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_RiVoiceMicMute, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_RiVoiceMicVolume, &g_Config.m_RiVoiceMicVolume, &Row, TCLocalize("Microphone volume", QMCLIENT_LOCALIZATION_CONTEXT), 0, 300, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceVadEnable, TCLocalize("Voice activation", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_RiVoiceVadEnable, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					if(g_Config.m_RiVoiceVadEnable)
					{
						CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
						Ui()->DoScrollbarOption(&g_Config.m_RiVoiceVadThreshold, &g_Config.m_RiVoiceVadThreshold, &Row, TCLocalize("Voice activation threshold", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
						CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

						CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
						Ui()->DoScrollbarOption(&g_Config.m_RiVoiceVadReleaseDelayMs, &g_Config.m_RiVoiceVadReleaseDelayMs, &Row, TCLocalize("Voice activation release delay", QMCLIENT_LOCALIZATION_CONTEXT), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms");
						CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					}

					CardContent.HSplitTop(LG_LineSpacing * 1.15f, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_RiVoiceVolume, &g_Config.m_RiVoiceVolume, &Row, TCLocalize("Playback volume", QMCLIENT_LOCALIZATION_CONTEXT), 0, 400, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceStereo, TCLocalize("Enable stereo positioning", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_RiVoiceStereo, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					if(g_Config.m_RiVoiceStereo)
					{
						CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
						Ui()->DoScrollbarOption(&g_Config.m_RiVoiceStereoWidth, &g_Config.m_RiVoiceStereoWidth, &Row, TCLocalize("Stereo width", QMCLIENT_LOCALIZATION_CONTEXT), 0, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
						CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					}

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_RiVoiceRadius, &g_Config.m_RiVoiceRadius, &Row, TCLocalize("Voice distance radius", QMCLIENT_LOCALIZATION_CONTEXT), 1, 400, &CUi::ms_LinearScrollbarScale, 0, "tile");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceGroupGlobal, TCLocalize("Hear teammates globally", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_RiVoiceGroupGlobal, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceShowOverlay, TCLocalize("Show the speaker list on the left", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_RiVoiceShowOverlay, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					if(g_Config.m_RiVoiceShowOverlay)
					{
						CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
						DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_RiVoiceShowWhenActive, TCLocalize("Also show while you are speaking", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_RiVoiceShowWhenActive, &Row, LG_LineHeight);
						CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					}
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::DummyMiniView:
			{
				// ========== 模块16: 分身小窗 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardDummyMiniViewStart = Column;
				s_GlassCards.push_back(CardDummyMiniViewStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 12, TCLocalize("Dummy mini view", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Dummy mini view preview and scaling", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClDummyMiniView, TCLocalize("Enable dummy mini view", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_ClDummyMiniView, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight * 0.8f, &Row, &CardContent);
				Ui()->DoLabel(&Row, TCLocalize("This is very expensive. AMD + Vulkan has a known unrecoverable bug", QMCLIENT_LOCALIZATION_CONTEXT), LG_BodySize * 0.7f, TEXTALIGN_ML);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_ClDummyMiniView)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_ClDummyMiniViewSize, &g_Config.m_ClDummyMiniViewSize, &Row, TCLocalize("Mini view size", QMCLIENT_LOCALIZATION_CONTEXT), 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_ClDummyMiniViewZoom, &g_Config.m_ClDummyMiniViewZoom, &Row, TCLocalize("Mini view zoom", QMCLIENT_LOCALIZATION_CONTEXT), 10, 300, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::DynamicIsland:
			{
				// ========== 模块17: 灵动岛 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardDynamicIslandStart = Column;
				s_GlassCards.push_back(CardDynamicIslandStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 14, TCLocalize("Dynamic island", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Dynamic island", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmHudIslandUseOriginalStyle, TCLocalize("Disable dynamic island", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmHudIslandUseOriginalStyle, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmHudIslandUseOriginalStyle)
				{
				}
				else
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmHudIslandBgOpacity, &g_Config.m_QmHudIslandBgOpacity, &Row, TCLocalize("Background opacity", QMCLIENT_LOCALIZATION_CONTEXT), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					static CButtonContainer s_DynamicIslandBgColorId;
					DoLine_ColorPicker(&s_DynamicIslandBgColorId, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("Background color", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_QmHudIslandBgColor, ColorRGBA(0.04f, 0.05f, 0.07f, 1.0f), false);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			case EQmModuleId::SystemMediaControls:
			{
				// ========== 模块18: 系统媒体控制 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardSystemMediaControlsStart = Column;
				s_GlassCards.push_back(CardSystemMediaControlsStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 13, TCLocalize("System media controls", QMCLIENT_LOCALIZATION_CONTEXT), TCLocalize("Media control toggles and buttons", QMCLIENT_LOCALIZATION_CONTEXT));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSmtcEnable, TCLocalize("Enable system media controls", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_ClSmtcEnable, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_ClSmtcEnable)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSmtcShowHud, TCLocalize("Show song info in the top-left corner", QMCLIENT_LOCALIZATION_CONTEXT), &g_Config.m_ClSmtcShowHud, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CUIRect MediaButtons, PrevButton, PlayButton, NextButton;
					CardContent.HSplitTop(LG_LineHeight, &MediaButtons, &CardContent);
					MediaButtons.VSplitLeft((MediaButtons.w - LG_LineSpacing * 2.0f) / 3.0f, &PrevButton, &MediaButtons);
					MediaButtons.VSplitLeft(LG_LineSpacing, nullptr, &MediaButtons);
					MediaButtons.VSplitLeft((MediaButtons.w - LG_LineSpacing) / 2.0f, &PlayButton, &MediaButtons);
					MediaButtons.VSplitLeft(LG_LineSpacing, nullptr, &MediaButtons);
					NextButton = MediaButtons;

					static CButtonContainer s_SmtcPrev;
					if(DoButton_Menu(&s_SmtcPrev, TCLocalize("Previous", QMCLIENT_LOCALIZATION_CONTEXT), 0, &PrevButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f))
						GameClient()->m_SystemMediaControls.Previous();

					static CButtonContainer s_SmtcPlayPause;
					if(DoButton_Menu(&s_SmtcPlayPause, TCLocalize("Play/Pause", QMCLIENT_LOCALIZATION_CONTEXT), 0, &PlayButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f))
						GameClient()->m_SystemMediaControls.PlayPause();

					static CButtonContainer s_SmtcNext;
					if(DoButton_Menu(&s_SmtcNext, TCLocalize("Next", QMCLIENT_LOCALIZATION_CONTEXT), 0, &NextButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f))
						GameClient()->m_SystemMediaControls.Next();

					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_CardPadding, nullptr, &CardContent);
				Column.y = CardContent.y;
				s_GlassCards.back().h = Column.y - s_GlassCards.back().y;
				RegisterModuleCard(pModule, ColumnId, s_GlassCards.back());
				HandleModuleDragState(pModule, s_GlassCards.back());
			}
			break;
			default:
				break;
			}
			HandleSearchCollapseButton(pModule, s_GlassCards.back());
		}
	};

	auto UpdateDropPreview = [&]() {
		s_DropPreview.m_Active = false;
		s_DropPreview.m_Valid = false;
		s_DropPreview.m_pDragged = nullptr;

		if(s_DragState.m_pDragging == nullptr)
			return;
		if(s_DragState.m_pDragging->m_Column == EQmModuleColumn::Full)
			return;

		EnsureColumnTops();

		EQmModuleColumn TargetColumn = EQmModuleColumn::Left;
		if(CompactLayout)
		{
			if(!RightCards.empty())
			{
				const float RightStartY = RightCards.front()->m_Rect.y;
				if(Ui()->MouseY() >= RightStartY)
					TargetColumn = EQmModuleColumn::Right;
			}
		}
		else
		{
			const float RightColumnX = RightView.x;
			if(Ui()->MouseX() >= RightColumnX)
				TargetColumn = EQmModuleColumn::Right;
		}

		const auto &Cards = (TargetColumn == EQmModuleColumn::Left) ? LeftCards : RightCards;
		std::vector<const SQmModuleCardInfo *> FilteredCards;
		FilteredCards.reserve(Cards.size());
		for(const auto *pCard : Cards)
		{
			if(pCard->m_pModule != s_DragState.m_pDragging)
				FilteredCards.push_back(pCard);
		}

		int InsertIndex = 0;
		const float MouseY = Ui()->MouseY();
		for(const auto *pCard : FilteredCards)
		{
			const float MidY = pCard->m_Rect.y + pCard->m_Rect.h * 0.5f;
			if(MouseY > MidY)
				++InsertIndex;
		}

		const float ColumnTop = (TargetColumn == EQmModuleColumn::Left) ? LeftColumnTop : RightColumnTop;
		const CUIRect ColumnFrame = (TargetColumn == EQmModuleColumn::Left) ? LeftColumnFrame : RightColumnFrame;
		CUIRect DropFrame = ColumnFrame;
		if(Ui()->IsClipped())
		{
			const CUIRect *pClip = Ui()->ClipArea();
			DropFrame.y = pClip->y;
			DropFrame.h = pClip->h;
		}
		if(!Ui()->MouseHovered(&DropFrame))
			return;
		float LineY = ColumnTop;
		if(FilteredCards.empty())
		{
			LineY = ColumnTop + LG_CardSpacing * 0.5f;
		}
		else if(InsertIndex <= 0)
		{
			LineY = FilteredCards.front()->m_Rect.y - LG_CardSpacing * 0.5f;
			LineY = maximum(LineY, ColumnTop);
		}
		else if(InsertIndex >= (int)FilteredCards.size())
		{
			const SQmModuleCardInfo *pLast = FilteredCards.back();
			LineY = pLast->m_Rect.y + pLast->m_Rect.h + LG_CardSpacing * 0.5f;
		}
		else
		{
			const SQmModuleCardInfo *pPrev = FilteredCards[InsertIndex - 1];
			const SQmModuleCardInfo *pNext = FilteredCards[InsertIndex];
			LineY = (pPrev->m_Rect.y + pPrev->m_Rect.h + pNext->m_Rect.y) * 0.5f;
		}

		const float LinePadding = std::clamp(6.0f * UiScale, 3.0f, 8.0f);
		CUIRect LineRect;
		LineRect.x = ColumnFrame.x + LinePadding;
		LineRect.w = maximum(0.0f, ColumnFrame.w - LinePadding * 2.0f);
		LineRect.y = LineY - DropPreviewThickness * 0.5f;
		LineRect.h = DropPreviewThickness;

		s_DropPreview.m_pDragged = s_DragState.m_pDragging;
		s_DropPreview.m_TargetColumn = TargetColumn;
		s_DropPreview.m_InsertIndex = InsertIndex;
		s_DropPreview.m_Active = true;
		s_DropPreview.m_Valid = true;
		s_DropPreview.m_LineRect = LineRect;
	};

	auto RenderDragGhost = [&]() {
		if(s_DragState.m_pDragging == nullptr || !s_DragState.m_HasDragRect)
			return;
		CUIRect Ghost;
		Ghost.x = Ui()->MouseX() - s_DragState.m_GrabOffset.x;
		Ghost.y = Ui()->MouseY() - s_DragState.m_GrabOffset.y;
		Ghost.w = s_DragState.m_DraggedWidth;
		Ghost.h = s_DragState.m_DraggedHeight;
		CUIRect Shadow = Ghost;
		Shadow.x += 1.5f;
		Shadow.y += 2.0f;
		Shadow.Draw(LG_ShadowColor, IGraphics::CORNER_ALL, LG_CornerRadius);
		Ghost.Draw(DragGhostColor, IGraphics::CORNER_ALL, LG_CornerRadius);
		CUIRect TopHighlight = Ghost;
		TopHighlight.h = 1.0f;
		TopHighlight.x += LG_CornerRadius;
		TopHighlight.w = maximum(0.0f, TopHighlight.w - LG_CornerRadius * 2.0f);
		TopHighlight.Draw(LG_HighlightColor, IGraphics::CORNER_NONE, 0.0f);
		DrawDragOutline(Ghost);
	};

	auto CommitDropPreview = [&]() -> bool {
		if(!s_DropPreview.m_Active || !s_DropPreview.m_Valid || s_DropPreview.m_pDragged == nullptr)
			return false;
		if(s_DropPreview.m_pDragged->m_Column == EQmModuleColumn::Full)
			return false;

		auto FindModuleIndexById = [&](EQmModuleId Id) -> int {
			for(size_t i = 0; i < s_aQmModuleLayout.size(); ++i)
			{
				if(s_aQmModuleLayout[i].m_Id == Id)
					return static_cast<int>(i);
			}
			return -1;
		};

		const int DraggedIndex = FindModuleIndexById(s_DropPreview.m_pDragged->m_Id);
		if(DraggedIndex < 0)
			return false;

		std::vector<int> LeftIndices;
		std::vector<int> RightIndices;
		LeftIndices.reserve(s_aQmModuleLayout.size());
		RightIndices.reserve(s_aQmModuleLayout.size());

		auto BuildColumnIndices = [&](EQmModuleColumn Column, std::vector<int> &Out) {
			Out.clear();
			for(size_t i = 0; i < s_aQmModuleLayout.size(); ++i)
			{
				if(s_aQmModuleLayout[i].m_Column == Column)
					Out.push_back(static_cast<int>(i));
			}
			std::stable_sort(Out.begin(), Out.end(), [&](int a, int b) {
				return s_aQmModuleLayout[a].m_OrderInColumn < s_aQmModuleLayout[b].m_OrderInColumn;
			});
		};

		BuildColumnIndices(EQmModuleColumn::Left, LeftIndices);
		BuildColumnIndices(EQmModuleColumn::Right, RightIndices);

		const EQmModuleColumn SourceColumn = s_aQmModuleLayout[DraggedIndex].m_Column;
		std::vector<int> *pSourceList = (SourceColumn == EQmModuleColumn::Left) ? &LeftIndices : &RightIndices;
		std::vector<int> *pTargetList = (s_DropPreview.m_TargetColumn == EQmModuleColumn::Left) ? &LeftIndices : &RightIndices;

		auto It = std::find(pSourceList->begin(), pSourceList->end(), DraggedIndex);
		if(It != pSourceList->end())
			pSourceList->erase(It);

		int InsertIndex = std::clamp(s_DropPreview.m_InsertIndex, 0, (int)pTargetList->size());
		pTargetList->insert(pTargetList->begin() + InsertIndex, DraggedIndex);

		s_aQmModuleLayout[DraggedIndex].m_Column = s_DropPreview.m_TargetColumn;
		for(size_t i = 0; i < LeftIndices.size(); ++i)
			s_aQmModuleLayout[LeftIndices[i]].m_OrderInColumn = static_cast<int>(i);
		for(size_t i = 0; i < RightIndices.size(); ++i)
			s_aQmModuleLayout[RightIndices[i]].m_OrderInColumn = static_cast<int>(i);

		char aSerialized[sizeof(g_Config.m_QmSidebarCardOrder)];
		SerializeQmModuleLayout(aSerialized, sizeof(aSerialized));
		if(str_comp(aSerialized, g_Config.m_QmSidebarCardOrder) != 0)
			str_copy(g_Config.m_QmSidebarCardOrder, aSerialized, sizeof(g_Config.m_QmSidebarCardOrder));
		str_copy(s_aQmModuleLayoutConfigCache, g_Config.m_QmSidebarCardOrder, sizeof(s_aQmModuleLayoutConfigCache));
		s_QmModuleColumnCacheDirty = true;

		return true;
	};

	const std::vector<const SQmModuleEntry *> &RenderedLeftModules = HasModuleSearch ? SearchLeftModules : VisibleLeftModules;
	const std::vector<const SQmModuleEntry *> &RenderedRightModules = HasModuleSearch ? SearchRightModules : VisibleRightModules;
	EnsureColumnTops();
	RenderColumnModules(RenderedLeftModules, EQmModuleColumn::Left);
	if(!SearchSingleColumnMode)
		RenderColumnModules(RenderedRightModules, EQmModuleColumn::Right);
	if(HasModuleSearch)
	{
		s_DragState.m_pPressed = nullptr;
		s_DragState.m_pDragging = nullptr;
		s_DragState.m_GrabOffset = vec2(0.0f, 0.0f);
		s_DragState.m_DraggedWidth = 0.0f;
		s_DragState.m_DraggedHeight = 0.0f;
		s_DragState.m_HasDragRect = false;
		s_DropPreview.m_Active = false;
		s_DropPreview.m_Valid = false;
		s_DropPreview.m_pDragged = nullptr;
	}
	else
	{
		UpdateDropPreview();
		RenderDragGhost();
		const bool MouseReleased = !Ui()->MouseButton(0) && Ui()->LastMouseButton(0);
		if(MouseReleased)
		{
			if(s_DragState.m_pDragging != nullptr)
				CommitDropPreview();
			s_DragState.m_pPressed = nullptr;
			s_DragState.m_pDragging = nullptr;
			s_DragState.m_GrabOffset = vec2(0.0f, 0.0f);
			s_DragState.m_DraggedWidth = 0.0f;
			s_DragState.m_DraggedHeight = 0.0f;
			s_DragState.m_HasDragRect = false;
			s_DropPreview.m_Active = false;
			s_DropPreview.m_Valid = false;
			s_DropPreview.m_pDragged = nullptr;
		}
		if(s_DropPreview.m_Active && s_DropPreview.m_Valid)
			s_DropPreview.m_LineRect.Draw(DropPreviewColor, IGraphics::CORNER_ALL, DropPreviewThickness);
	}
	if(!ColumnsReady)
		EnsureColumns();

	// === 滚动区域 Manba OUT! ===
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = maximum(LeftView.y, RightView.y) + LG_CardSpacing;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

