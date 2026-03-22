#include <base/log.h>
#include <base/math.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/graphics.h>
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
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
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
};

struct SAutoReplyRuleInputRow
{
	char m_aTrigger[512] = "";
	char m_aReply[256] = "";
	CLineInput m_TriggerInput;
	CLineInput m_ReplyInput;

	SAutoReplyRuleInputRow()
	{
		m_TriggerInput.SetBuffer(m_aTrigger, sizeof(m_aTrigger));
		m_ReplyInput.SetBuffer(m_aReply, sizeof(m_aReply));
	}
};

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

static std::unique_ptr<SAutoReplyRuleInputRow> CreateAutoReplyRuleInputRow(const char *pTrigger = "", const char *pReply = "")
{
	auto pRow = std::make_unique<SAutoReplyRuleInputRow>();
	pRow->m_TriggerInput.Set(pTrigger);
	pRow->m_ReplyInput.Set(pReply);
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

		const char *pArrowConst = str_find(pLine, "=>");
		if(!pArrowConst)
			continue;

		char *pArrow = pLine + (pArrowConst - pLine);
		*pArrow = '\0';
		pArrow += 2;

		char *pKeywords = (char *)str_utf8_skip_whitespaces(pLine);
		str_utf8_trim_right(pKeywords);
		char *pReply = (char *)str_utf8_skip_whitespaces(pArrow);
		str_utf8_trim_right(pReply);
		if(pKeywords[0] == '\0' || pReply[0] == '\0')
			continue;

		vOutRules.push_back({pKeywords, pReply});
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
		vCompleteRows.push_back({aTrigger, aReply});
	}

	if(vCompleteRows.size() != vRules.size())
		return false;

	for(size_t i = 0; i < vCompleteRows.size(); ++i)
	{
		if(str_comp(vCompleteRows[i].m_Keywords.c_str(), vRules[i].m_Keywords.c_str()) != 0 ||
			str_comp(vCompleteRows[i].m_Reply.c_str(), vRules[i].m_Reply.c_str()) != 0)
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
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcColorFreeze, TCLocalize("冻结 Tee 使用彩色皮肤"), &g_Config.m_TcColorFreeze, &Column, LineSize);
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
		if(DoLine_RadioMenu(Column, TCLocalize("更小的Tee"),
			   s_vButtonContainers,
			   {Localize("无"), Localize("自身"), Localize("全部")},
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
	}//尚不明确作用

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Input ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Input"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInput, TCLocalize("快速输入(降低视觉延迟)"), &g_Config.m_TcFastInput, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, TCLocalize("Amount"), 1, 100, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	if(g_Config.m_TcFastInput)
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, TCLocalize("对其他玩家启用快速输入"), &g_Config.m_TcFastInputOthers, &Column, LineSize);
	else
		Column.HSplitTop(LineSize, nullptr, &Column);
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
	Ui()->DoLabel(&Label, TCLocalize("自动执行"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, Localize("连接前执行"), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_TcExecuteOnConnect, sizeof(g_Config.m_TcExecuteOnConnect));
		Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, Localize("在连接时执行"), FontSize, TEXTALIGN_ML);
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
	Ui()->DoLabel(&Label, TCLocalize("投票"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoVoteWhenFar, TCLocalize("自动投票反对地图变更"), &g_Config.m_TcAutoVoteWhenFar, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcAutoVoteWhenFarTime, &g_Config.m_TcAutoVoteWhenFarTime, &Button, TCLocalize("最短时间"), 1, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " 分钟");

	CUIRect VoteMessage;
	Column.HSplitTop(LineSize + MarginExtraSmall, &VoteMessage, &Column);
	VoteMessage.HSplitTop(MarginExtraSmall, nullptr, &VoteMessage);
	VoteMessage.VSplitMid(&Label, &VoteMessage);
	Ui()->DoLabel(&Label, TCLocalize("要在聊天中发送的消息："), FontSize, TEXTALIGN_ML);
	static CLineInput s_VoteMessage(g_Config.m_TcAutoVoteWhenFarMessage, sizeof(g_Config.m_TcAutoVoteWhenFarMessage));
	s_VoteMessage.SetEmptyText(TCLocalize("留空以禁用"));
	Ui()->DoEditBox(&s_VoteMessage, &VoteMessage, EditBoxFontSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** 自动回复 ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("自动回复"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMuted, TCLocalize("对已屏蔽发言的玩家自动回复"), &g_Config.m_TcAutoReplyMuted, &Column, LineSize);
	CUIRect MutedReply;
	Column.HSplitTop(LineSize + MarginExtraSmall, &MutedReply, &Column);
	if(g_Config.m_TcAutoReplyMuted)
	{
		MutedReply.HSplitTop(MarginExtraSmall, nullptr, &MutedReply);
		static CLineInput s_MutedReply(g_Config.m_TcAutoReplyMutedMessage, sizeof(g_Config.m_TcAutoReplyMutedMessage));
		s_MutedReply.SetEmptyText("我屏蔽你了");
		Ui()->DoEditBox(&s_MutedReply, &MutedReply, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMinimized, TCLocalize("切屏时自动回复"), &g_Config.m_TcAutoReplyMinimized, &Column, LineSize);
	CUIRect MinimizedReply;
	Column.HSplitTop(LineSize + MarginExtraSmall, &MinimizedReply, &Column);
	if(g_Config.m_TcAutoReplyMinimized)
	{
		MinimizedReply.HSplitTop(MarginExtraSmall, nullptr, &MinimizedReply);
		static CLineInput s_MinimizedReply(g_Config.m_TcAutoReplyMinimizedMessage, sizeof(g_Config.m_TcAutoReplyMinimizedMessage));
		s_MinimizedReply.SetEmptyText("我不在游戏窗口前");
		Ui()->DoEditBox(&s_MinimizedReply, &MinimizedReply, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Player Indicator ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("玩家指示器"), HeadlineFontSize, TEXTALIGN_ML);
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
		str_format(aBuf, sizeof(aBuf), "显示 %s 分组", GameClient()->m_WarList.m_WarTypes.at(1)->m_aWarName);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorEnemy, aBuf, &g_Config.m_TcWarListIndicatorEnemy, &Column, LineSize);
		str_format(aBuf, sizeof(aBuf), "显示 %s 分组", GameClient()->m_WarList.m_WarTypes.at(2)->m_aWarName);
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

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniVoteHud, TCLocalize("显示小型投票HUD"), &g_Config.m_TcMiniVoteHud, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniDebug, TCLocalize("显示位置和角度（小型调试）"), &g_Config.m_TcMiniDebug, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderCursorSpec, TCLocalize("自由观战时显示光标"), &g_Config.m_TcRenderCursorSpec, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	if(g_Config.m_TcRenderCursorSpec)
	{
		Ui()->DoScrollbarOption(&g_Config.m_TcRenderCursorSpecAlpha, &g_Config.m_TcRenderCursorSpecAlpha, &Button, TCLocalize("自由观战时光标不透明度"), 0, 100);
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNotifyWhenLast, TCLocalize("当只剩一名存活者时提示:"), &g_Config.m_TcNotifyWhenLast, &Column, LineSize);
	CUIRect NotificationConfig;
	Column.HSplitTop(LineSize + MarginSmall, &NotificationConfig, &Column);
	if(g_Config.m_TcNotifyWhenLast)
	{
		NotificationConfig.VSplitMid(&Button, &NotificationConfig);
		static CLineInput s_LastInput(g_Config.m_TcNotifyWhenLastText, sizeof(g_Config.m_TcNotifyWhenLastText));
		s_LastInput.SetEmptyText(TCLocalize("就你一个啦!"));
		Button.HSplitTop(MarginSmall, nullptr, &Button);
		Ui()->DoEditBox(&s_LastInput, &Button, EditBoxFontSize);
		static CButtonContainer s_ClientNotifyWhenLastColor;
		DoLine_ColorPicker(&s_ClientNotifyWhenLastColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &NotificationConfig, "", &g_Config.m_TcNotifyWhenLastColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastX, &g_Config.m_TcNotifyWhenLastX, &Button, TCLocalize("水平位置"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastY, &g_Config.m_TcNotifyWhenLastY, &Button, TCLocalize("垂直位置"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastSize, &g_Config.m_TcNotifyWhenLastSize, &Button, TCLocalize("字体大小"), 1, 50);
	}
	else
	{
		Column.HSplitTop(LineSize * 3.0f, nullptr, &Column);
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowCenter, TCLocalize("显示屏幕中心线"), &g_Config.m_TcShowCenter, &Column, LineSize);
	Column.HSplitTop(LineSize + MarginSmall, &Button, &Column);
	if(g_Config.m_TcShowCenter)
	{
		static CButtonContainer s_ShowCenterLineColor;
		DoLine_ColorPicker(&s_ShowCenterLineColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Button, TCLocalize("屏幕中心线颜色"), &g_Config.m_TcShowCenterColor, CConfig::ms_TcShowCenterColor, false, nullptr, true);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcShowCenterWidth, &g_Config.m_TcShowCenterWidth, &Button, TCLocalize("屏幕中心线宽度"), 0, 20);
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
	Ui()->DoLabel(&Label, TCLocalize("Tee状态栏"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHud, TCLocalize("显示Tee状态栏"), &g_Config.m_TcShowFrozenHud, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHudSkins, TCLocalize("使用自定义皮肤代替忍者Tee"), &g_Config.m_TcShowFrozenHudSkins, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFrozenHudTeamOnly, TCLocalize("仅在加入队伍后显示"), &g_Config.m_TcFrozenHudTeamOnly, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcFrozenMaxRows, &g_Config.m_TcFrozenMaxRows, &Button, TCLocalize("最大行数"), 1, 6);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcFrozenHudTeeSize, &g_Config.m_TcFrozenHudTeeSize, &Button, TCLocalize("Tee大小"), 8, 27);

	{
		CUIRect CheckBoxRect, CheckBoxRect2;
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect2, &Column);
		if(DoButton_CheckBox(&g_Config.m_TcShowFrozenText, TCLocalize("显示剩余存活Tee的数量"), g_Config.m_TcShowFrozenText >= 1, &CheckBoxRect))
			g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText >= 1 ? 0 : 1;

		if(g_Config.m_TcShowFrozenText)
		{
			static int s_CountFrozenText = 0;
			if(DoButton_CheckBox(&s_CountFrozenText, TCLocalize("显示冻结Tee的数量"), g_Config.m_TcShowFrozenText == 2, &CheckBoxRect2))
				g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText != 2 ? 2 : 1;
		}
	}
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Tile Outlines ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("墙体轮廓"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutline, TCLocalize("显示所有启用的轮廓"), &g_Config.m_TcOutline, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineEntities, TCLocalize("仅在实体层中显示轮廓"), &g_Config.m_TcOutlineEntities, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcOutlineAlpha, &g_Config.m_TcOutlineAlpha, &Button, TCLocalize("轮廓透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcOutlineSolidAlpha, &g_Config.m_TcOutlineSolidAlpha, &Button, TCLocalize("墙体轮廓透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	auto DoOutlineType = [&](CButtonContainer &ButtonContainer, const char *pName, int &Enable, int &Width, unsigned int &Color, const unsigned int &ColorDefault) {
		// Checkbox & Color
		DoLine_ColorPicker(&ButtonContainer, ColorPickerLineSize, ColorPickerLabelSize, 0, &Column, pName, &Color, ColorDefault, true, &Enable, true);
		// Width
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&Width, &Width, &Button, TCLocalize("宽度", "Outlines"), 1, 16);
		//
		Column.HSplitTop(ColorPickerLineSpacing, nullptr, &Column);
	};
	Column.HSplitTop(ColorPickerLineSpacing, nullptr, &Column);
	static CButtonContainer s_aOutlineButtonContainers[5];
	static CButtonContainer s_OutlineDeepFreezeColorId;
	static CButtonContainer s_OutlineDeepUnfreezeColorId;
	DoOutlineType(s_aOutlineButtonContainers[0], TCLocalize("墙体"), g_Config.m_TcOutlineSolid, g_Config.m_TcOutlineWidthSolid, g_Config.m_TcOutlineColorSolid, CConfig::ms_TcOutlineColorSolid);
	DoOutlineType(s_aOutlineButtonContainers[1], TCLocalize("冻结"), g_Config.m_TcOutlineFreeze, g_Config.m_TcOutlineWidthFreeze, g_Config.m_TcOutlineColorFreeze, CConfig::ms_TcOutlineColorFreeze);
	DoLine_ColorPicker(&s_OutlineDeepFreezeColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("深度冻结颜色"), &g_Config.m_TcOutlineColorDeepFreeze, CConfig::ms_TcOutlineColorDeepFreeze, false, nullptr, true);
	DoOutlineType(s_aOutlineButtonContainers[2], TCLocalize("解冻"), g_Config.m_TcOutlineUnfreeze, g_Config.m_TcOutlineWidthUnfreeze, g_Config.m_TcOutlineColorUnfreeze, CConfig::ms_TcOutlineColorUnfreeze);
	DoLine_ColorPicker(&s_OutlineDeepUnfreezeColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("深度解冻颜色"), &g_Config.m_TcOutlineColorDeepUnfreeze, CConfig::ms_TcOutlineColorDeepUnfreeze, false, nullptr, true);
	DoOutlineType(s_aOutlineButtonContainers[3], TCLocalize("刺"), g_Config.m_TcOutlineKill, g_Config.m_TcOutlineWidthKill, g_Config.m_TcOutlineColorKill, CConfig::ms_TcOutlineColorKill);
	DoOutlineType(s_aOutlineButtonContainers[4], TCLocalize("传送"), g_Config.m_TcOutlineTele, g_Config.m_TcOutlineWidthTele, g_Config.m_TcOutlineColorTele, CConfig::ms_TcOutlineColorTele);
	Column.h -= ColorPickerLineSpacing;

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Ghost Tools ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("影子工具"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowOthersGhosts, TCLocalize("向其他玩家显示未预见的影子"), &g_Config.m_TcShowOthersGhosts, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcSwapGhosts, TCLocalize("交换影子与普通玩家"), &g_Config.m_TcSwapGhosts, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcPredGhostsAlpha, &g_Config.m_TcPredGhostsAlpha, &Button, TCLocalize("预测透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcUnpredGhostsAlpha, &g_Config.m_TcUnpredGhostsAlpha, &Button, TCLocalize("未预测透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcHideFrozenGhosts, TCLocalize("隐藏冻结玩家的影子"), &g_Config.m_TcHideFrozenGhosts, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderGhostAsCircle, TCLocalize("将影子渲染为圆圈"), &g_Config.m_TcRenderGhostAsCircle, &Column, LineSize);

	static CButtonContainer s_ReaderButtonGhost, s_ClearButtonGhost;
	DoLine_KeyReader(Column, s_ReaderButtonGhost, s_ClearButtonGhost, TCLocalize("切换影子键"), "toggle tc_show_others_ghosts 0 1");

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
		Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, TCLocalize("笔画消失时间"), 0, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, TCLocalize(" seconds (never)"));
	else
		Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, TCLocalize("笔画消失时间"), 0, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, TCLocalize(" seconds"));

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
	s_TypeNameInput.SetEmptyText("分组名称");
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
		KeyBinds,
		MiniFeatures,
		DummyMiniView,
		Coords,
		Streamer,
		FriendNotify,
		BlockWords,
		QiaFen,
		PieMenu,
		EntityOverlay,
		Laser,
		PlayerStats,
		CollisionHitbox,
		FavoriteMaps,
		HJAssist,
		InputOverlay,
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

	constexpr size_t kQmModuleCount = 20;

	// Layout string format: key:column:order; entries separated by ';'.
	static const std::array<SQmModuleEntry, kQmModuleCount> s_aQmModuleDefaults = {{
		{EQmModuleId::Info, EQmModuleColumn::Full, 0, "info"},
		{EQmModuleId::ChatBubble, EQmModuleColumn::Left, 0, "chat_bubble"},
		{EQmModuleId::GoresActor, EQmModuleColumn::Left, 1, "gores_actor"},
		{EQmModuleId::KeyBinds, EQmModuleColumn::Left, 2, "key_binds"},
		{EQmModuleId::MiniFeatures, EQmModuleColumn::Left, 3, "mini_features"},
		{EQmModuleId::Coords, EQmModuleColumn::Left, 4, "coords"},
		{EQmModuleId::Streamer, EQmModuleColumn::Left, 5, "streamer"},
		{EQmModuleId::FriendNotify, EQmModuleColumn::Left, 6, "friend_notify"},
		{EQmModuleId::BlockWords, EQmModuleColumn::Left, 7, "block_words"},
		{EQmModuleId::QiaFen, EQmModuleColumn::Left, 8, "qiafen"},
		{EQmModuleId::PieMenu, EQmModuleColumn::Left, 9, "pie_menu"},
		{EQmModuleId::EntityOverlay, EQmModuleColumn::Right, 1, "entity_overlay"},
		{EQmModuleId::Laser, EQmModuleColumn::Right, 2, "laser"},
		{EQmModuleId::PlayerStats, EQmModuleColumn::Right, 3, "player_stats"},
		{EQmModuleId::CollisionHitbox, EQmModuleColumn::Right, 4, "collision_hitbox"},
		{EQmModuleId::FavoriteMaps, EQmModuleColumn::Right, 5, "favorite_maps"},
		{EQmModuleId::HJAssist, EQmModuleColumn::Right, 6, "hj_assist"},
		{EQmModuleId::InputOverlay, EQmModuleColumn::Right, 7, "input_overlay"},
		{EQmModuleId::DummyMiniView, EQmModuleColumn::Right, 8, "dummy_miniview"},
		{EQmModuleId::SystemMediaControls, EQmModuleColumn::Right, 9, "system_media_controls"}
	}};

	static std::array<SQmModuleEntry, kQmModuleCount> s_aQmModuleLayout = s_aQmModuleDefaults;
	static char s_aQmModuleLayoutConfigCache[sizeof(g_Config.m_QmSidebarCardOrder)] = {};
	static bool s_QmModuleLayoutInitialized = false;
	static bool s_QmModuleColumnCacheDirty = true;
	static std::vector<const SQmModuleEntry *> s_vCachedFullModules;
	static std::vector<const SQmModuleEntry *> s_vCachedLeftModules;
	static std::vector<const SQmModuleEntry *> s_vCachedRightModules;

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

	auto HandleModuleDragState = [&](const SQmModuleEntry *pModule, const CUIRect &CardRect) {
		const bool Inside = Ui()->MouseHovered(&CardRect);
		if(Ui()->MouseButtonClicked(0) && Inside && Ui()->ActiveItem() == nullptr)
		{
			s_DragState.m_pPressed = pModule;
			s_DragState.m_pDragging = nullptr;
			s_DragState.m_PressStartTime = Client()->GlobalTime();
		}

		if(s_DragState.m_pPressed == pModule && Ui()->MouseButton(0) && s_DragState.m_pDragging == nullptr)
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
		const SQmModuleCardInfo *pInfo = &ModuleCards.back();
		if(Column == EQmModuleColumn::Left)
			LeftCards.push_back(pInfo);
		else if(Column == EQmModuleColumn::Right)
			RightCards.push_back(pInfo);
	};

	bool ColumnsReady = false;
	auto EnsureColumns = [&]() {
		if(ColumnsReady)
			return;
		if(CompactLayout)
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
		if(CompactLayout || Column == EQmModuleColumn::Left)
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

	auto ModuleSearchKeywords = [](EQmModuleId Id) -> const char * {
		switch(Id)
		{
		case EQmModuleId::ChatBubble: return "消息气泡 liaotian qipao chat bubble typing 预览 yulan 镜头缩放 suofang 持续时间 chixu 透明度 touming 字体大小 ziti 最大宽度 kuandu 垂直偏移 pianyi 圆角 yuanjiao";
		case EQmModuleId::GoresActor: return "gores 演员 actor 掉水 diaoshui 自动发言 zidong fayan 表情 biaoqing 表情id emoticon 发送概率 gaolv";
		case EQmModuleId::KeyBinds: return "按键绑定 anjian bangding bind 快捷键 kuaijiejian 常用绑定 changyong bangding";
		case EQmModuleId::MiniFeatures: return "梦的小功能 meng xiaogongneng 粒子拖尾 lizi tuowei 远程粒子 yuancheng lizi 计分板查分 chafen 聊天框淡出 liaotian danchu 表情选择 biaoqing xuanze 动画优化 donghua youhua 复读 fudu 锤人换皮 chuiren huanpi 随机表情 suiji biaoqing 说话不弹表情 shuo hua biaoqing 本地彩虹名字 caihong mingzi 武器弹道辅助线 dan dao fuzhuxian 位置跳跃提示 tiaoyue tishi";
		case EQmModuleId::DummyMiniView: return "分身小窗 fenshen xiaochuang dummy mini view 预览 yulan 缩放 suofang 小窗大小 daxiao";
		case EQmModuleId::Coords: return "显示坐标 xianshi zuobiao coords position 自己坐标 ziji 他人坐标 taren 显示x xianshi x 显示y xianshi y 对齐提示 duiqi tishi 严格对齐 yange duiqi";
		case EQmModuleId::Streamer: return "主播模式 zhubo moshi 直播 zhibo 隐私 yinsi 非好友昵称改id feihaoyou nicheng id 非好友皮肤默认 pifu moren 计分板默认国旗 guoqi";
		case EQmModuleId::FriendNotify: return "好友提醒 haoyou tixing 好友上线 shangxian 自动刷新 zidong shuaxin 服务器列表 fuwuqi liebiao 刷新间隔 jiange 进图打招呼 jintu dazhaohu 大字显示 dazi xianshi";
		case EQmModuleId::BlockWords: return "屏蔽词 pingbici block words 控制台显示 kongzhitai 启用列表 qiyong liebiao 按词长替换 cichang tihuan 多字符替换 duozifu tihuan";
		case EQmModuleId::QiaFen: return "恰分 qiafen 自动回复 zidong huifu 冷却 lengque dummy 发言 fayan 关键词 guanjianci 回复 huifu 关键词回复 guanjianci huifu";
		case EQmModuleId::PieMenu: return "饼菜单 bingcaidan pie menu 启用 qiyong ui大小 daxiao 不透明度 butouming 检测距离 jiance juli";
		case EQmModuleId::EntityOverlay: return "实体层颜色 shiti ceng yanse 实体层 shiti entity overlay 死亡透明度 siwang 冻结透明度 dongjie 解冻透明度 jiedong 深度冻结 shendu dongjie 深度解冻 shendu jiedong 传送透明度 chuansong 开关透明度 kaiguan 叠层透明度 dieceng";
		case EQmModuleId::Laser: return "激光设置 jiguang laser 增强特效 zengqiang texiao 辉光强度 huiguang qiangdu 激光大小 daxiao 半透明 bantouming 圆角端点 yuanjiao duandian 脉冲速度 maichong sudu 脉冲幅度 maichong fudu";
		case EQmModuleId::PlayerStats: return "玩家统计 wanjia tongji player stats gores hud 显示统计 xianshi tongji 进服重置 jinfu chongzhi";
		case EQmModuleId::CollisionHitbox: return "碰撞体积可视化 pengzhuang tiji keshihua 碰撞箱 pengzhuangxiang collision hitbox 显示碰撞 xianshi pengzhuang 透明度 touming";
		case EQmModuleId::FavoriteMaps: return "收藏地图 shoucang ditu favorite maps 地图管理 ditu guanli 收藏 shoucang 取消收藏 quxiao shoucang";
		case EQmModuleId::HJAssist: return "hj辅助 hj fuzhu 解冻辅助 jiedong fuzhu 自动取消旁观 quxiao pangguan 自动切换 qiehuan tee 自动关闭聊天 guanbi liaotian";
		case EQmModuleId::InputOverlay: return "按键显示 anjian xianshi input overlay 按键叠加 anjian diejia 大小 daxiao 不透明度 butouming 水平位置 shuiping weizhi 垂直位置 chuizhi weizhi";
		case EQmModuleId::SystemMediaControls: return "系统媒体控制 xitong meiti kongzhi smtc media controls 启用系统媒体 qiyong 显示歌曲信息 gequ xinxi 上一个 shangyige 播放暂停 bofang zanting 下一个 xiayige";
		}
		return "";
	};

	auto ModuleMatchesSearch = [&](const SQmModuleEntry *pModule) -> bool {
		if(!HasModuleSearch)
			return true;
		return str_find_nocase(pModule->m_pKey, pModuleSearch) != nullptr ||
			str_find_nocase(ModuleSearchKeywords(pModule->m_Id), pModuleSearch) != nullptr;
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
	const int VisibleModuleCount = static_cast<int>(VisibleLeftModules.size() + VisibleRightModules.size());

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
				CardInner.VSplitMid(&LeftPart, &RightPart, LG_CardSpacing * 2);
			const float LeftStartY = LeftPart.y;
			CUIRect LeftContent = LeftPart;
			DoModuleHeadline(LeftContent, -1, TCLocalize("QmClient 社区"), TCLocalize("官方社区入口"));
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
					Ui()->DoLabel(&Row, TCLocalize("已复制"), LG_BodySize, TEXTALIGN_ML);
				}
				else
				{
					TextRender()->TextColor(1.0f, 0.85f, 0.0f, 1.0f); 
					Ui()->DoLabel(&Row, "QQ群: 1076765929(点击复制)", LG_BodySize, TEXTALIGN_ML);
				}
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				if(Ui()->HotItem() == &s_QQGroupButtonId)
					GameClient()->m_Tooltips.DoToolTip(&s_QQGroupButtonId, &Row, TCLocalize("点击复制QQ群号"));
			}
			LeftContent.HSplitTop(LG_LineSpacing * 2, nullptr, &LeftContent);
			DoModuleHeadline(LeftContent, -3, TCLocalize("赞助"), TCLocalize("感谢您为QmClient做出的贡献"));
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
			const float SponsorButtonWidth = LG_LineHeight * 9.0f;
			{
				CUIRect SponsorButton;
				static CButtonContainer s_SponsorButton;
				Row.VSplitLeft(SponsorButtonWidth, &SponsorButton, nullptr);
				if(DoButton_Menu(&s_SponsorButton, TCLocalize("前往赞助❤️"), 0, &SponsorButton))
				{
					Client()->ViewLink("https://afdian.com/a/Q1menGClient");
				}
			}
			LeftContent.HSplitTop(LG_LineSpacing * 0.5f, nullptr, &LeftContent);
			LeftContent.HSplitTop(LG_LineHeight, &Row, &LeftContent);
			{
				CUIRect RecentUpdateButton;
				static CButtonContainer s_RecentUpdateButton;
				Row.VSplitLeft(SponsorButtonWidth, &RecentUpdateButton, nullptr);
				if(DoButton_Menu(&s_RecentUpdateButton, TCLocalize("点击查看最近更新⭐"), 0, &RecentUpdateButton))
				{
					Client()->ViewLink("https://qmclient.icu/index.html");
				}
			}
			LeftContent.HSplitTop(LG_LineSpacing * 0.5f, nullptr, &LeftContent);
			LeftContent.HSplitTop(LG_LineHeight, &Row, &LeftContent);
			{
				CUIRect FeedbackButton;
				static CButtonContainer s_FeedbackButton;
				Row.VSplitLeft(SponsorButtonWidth, &FeedbackButton, nullptr);
				if(DoButton_Menu(&s_FeedbackButton, TCLocalize("点击反馈"), 0, &FeedbackButton))
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
			const float TeeSize = std::clamp(50.0f * UiScale, 36.0f, 50.0f);
			const float RightContentShift = LG_CardPadding * 0.40f;
			CUIRect TeeRect, TextRect;
				const float TeeTextOffset = TeeSize + LG_CardPadding * 0.65f;
				RightPart.VSplitLeft(TeeTextOffset, &TeeRect, &TextRect);
			vec2 TeePos = vec2(TeeRect.x + TeeSize * 0.5f - RightContentShift, RightStartY + TeeSize * 0.5f + LG_CardPadding * 0.5f);
			RenderDevSkin(
				TeePos,               // 位置
				TeeSize,              // 大小
				"santa_tuzi",         // 皮肤名
				"santa_tuzi",           // 备用皮肤
				false,                 // 自定义颜色
				0, 0, 0,              // 脚部颜色，身体颜色，表情
				false,                 // 彩虹效果
				true                  // Cute
			);
			CUIRect RightContent = TextRect;
			RightContent.x -= RightContentShift;
			RightContent.w += RightContentShift;
			DoModuleHeadline(RightContent, -2, TCLocalize("QmClient 开发人员"), TCLocalize("开发者与赞助名单"));
			//我名字
			RightContent.HSplitTop(LG_LineHeight, &Row, &RightContent);
			TextRender()->TextColor(GetRainbowColor(-6));
			Ui()->DoLabel(&Row, "栖梦(璇梦)", LG_BodySize + 2.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			// 感谢名单
			RightContent.HSplitTop(LG_LineSpacing * 1.75f, nullptr, &RightContent);
			RightContent.HSplitTop(LG_LineHeight * 0.92f, &Row, &RightContent);
			TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.82f));
			Ui()->DoLabel(&Row, "赞助名单:", LG_BodySize * 0.95f, TEXTALIGN_ML);
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
					"爱发电用户_07470",
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
					"无言鱼"
					};
				const float SponsorFontSize = LG_BodySize * 1.1f;
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
			Ui()->DoLabel(&Row, "没有你们或多或少的支持我无法走的这么远,谢谢", LG_BodySize * 0.93f, TEXTALIGN_ML);
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
		DoModuleHeadline(SearchContent, -4, TCLocalize("功能搜索"), TCLocalize("快速定位功能模块"));
		SearchContent.HSplitTop(LG_LineHeight, &Row, &SearchContent);
		Ui()->DoEditBox_Search(&s_ModuleSearchInput, &Row, LG_BodySize, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
		SearchContent.HSplitTop(LG_LineSpacing * 0.65f, nullptr, &SearchContent);

		char aSearchHint[64];
		str_format(aSearchHint, sizeof(aSearchHint), "匹配到 %d 个功能模块", VisibleModuleCount);
		SearchContent.HSplitTop(LG_LineHeight * 0.85f, &Row, &SearchContent);
		TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.82f));
		if(HasModuleSearch && VisibleModuleCount == 0)
			Ui()->DoLabel(&Row, TCLocalize("未找到匹配功能，请尝试其他关键词"), LG_BodySize * 0.92f, TEXTALIGN_ML);
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
				DoModuleHeadline(CardContent, 0, TCLocalize("消息气泡"), TCLocalize("在头顶显示聊天气泡"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmChatBubble, TCLocalize("在玩家头顶显示聊天气泡"), &g_Config.m_QmChatBubble, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmChatBubble)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmChatBubbleDuration, &g_Config.m_QmChatBubbleDuration, &Row, TCLocalize("持续时间"), 1, 30, &CUi::ms_LinearScrollbarScale, 0, "s");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmChatBubbleAlpha, &g_Config.m_QmChatBubbleAlpha, &Row, TCLocalize("透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmChatBubbleFontSize, &g_Config.m_QmChatBubbleFontSize, &Row, TCLocalize("字体大小"), 8, 32);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					static std::vector<const char *> s_ChatBubbleAnimDropDownNames;
					s_ChatBubbleAnimDropDownNames = {TCLocalize("淡出"), TCLocalize("收缩"), TCLocalize("上滑")};
					static CUi::SDropDownState s_ChatBubbleAnimDropDownState;
					static CScrollRegion s_ChatBubbleAnimDropDownScrollRegion;
					s_ChatBubbleAnimDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ChatBubbleAnimDropDownScrollRegion;
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("动画效果"), LG_BodySize, TEXTALIGN_ML);
					const int AnimSelectedNew = Ui()->DoDropDown(&ControlCol, g_Config.m_QmChatBubbleAnimation, s_ChatBubbleAnimDropDownNames.data(), s_ChatBubbleAnimDropDownNames.size(), s_ChatBubbleAnimDropDownState);
					if(g_Config.m_QmChatBubbleAnimation != AnimSelectedNew)
						g_Config.m_QmChatBubbleAnimation = AnimSelectedNew;
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					static CButtonContainer s_ChatBubbleBgColorId, s_ChatBubbleTextColorId;
					DoLine_ColorPicker(&s_ChatBubbleBgColorId, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("背景颜色"), &g_Config.m_QmChatBubbleBgColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.8f), false, nullptr, true);

					DoLine_ColorPicker(&s_ChatBubbleTextColorId, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("文本颜色"), &g_Config.m_QmChatBubbleTextColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);
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
				DoModuleHeadline(CardContent, 1, TCLocalize("Gores演员专用"), TCLocalize("落水自动发言"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFreezeChatEnabled, TCLocalize("掉水里自动发言和表情"), &g_Config.m_TcFreezeChatEnabled, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_TcFreezeChatEnabled)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFreezeChatEmoticon, TCLocalize("掉水里发表情"), &g_Config.m_TcFreezeChatEmoticon, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					if(g_Config.m_TcFreezeChatEmoticon)
					{
						CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
						Ui()->DoScrollbarOption(&g_Config.m_TcFreezeChatEmoticonId, &g_Config.m_TcFreezeChatEmoticonId, &Row, TCLocalize("表情ID"), 0, 15);
						CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					}

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("聊天消息"), LG_BodySize, TEXTALIGN_ML);
					static CLineInput s_FreezeChatMessageQiMeng(g_Config.m_TcFreezeChatMessage, sizeof(g_Config.m_TcFreezeChatMessage));
					s_FreezeChatMessageQiMeng.SetEmptyText(TCLocalize("留空禁用"));
					Ui()->DoEditBox(&s_FreezeChatMessageQiMeng, &ControlCol, LG_BodySize);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_TcFreezeChatChance, &g_Config.m_TcFreezeChatChance, &Row, TCLocalize("发送概率"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

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
				DoModuleHeadline(CardContent, 3, TCLocalize("按键绑定"), TCLocalize("常用bind合集"));

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
					TCLocalize("八角定位"), "echo 你正在使用45度瞄准;+toggle cl_mouse_max_distance 2 400; +toggle_restore inp_mousesens 1");
				DoKeyBindRow(CardContent, s_ReaderButtonSmallSens, s_ClearButtonSmallSens,
					TCLocalize("瞄缝救人"), "+toggle_restore inp_mousesens 1");
				DoKeyBindRow(CardContent, s_ReaderButtonLeftJump, s_ClearButtonLeftJump,
					TCLocalize("三格左跳"), "+jump; +left");
				DoKeyBindRow(CardContent, s_ReaderButtonRightJump, s_ClearButtonRightJump,
					TCLocalize("三格右跳"), "+jump; +right");

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
				DoModuleHeadline(CardContent, 2, TCLocalize("梦的小功能"), TCLocalize("栖梦出品,必属精品"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmcFootParticles, TCLocalize("本地粒子"), &g_Config.m_QmcFootParticles, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmClientMarkTrail, TCLocalize("远程粒子"), &g_Config.m_QmClientMarkTrail, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClScoreboardPoints, TCLocalize("显示计分板查分"), &g_Config.m_ClScoreboardPoints, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmChatFadeOutAnim, TCLocalize("聊天框动画"), &g_Config.m_QmChatFadeOutAnim, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmEmoticonSelectAnim, TCLocalize("表情动画"), &g_Config.m_QmEmoticonSelectAnim, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmScoreboardAnimOptim, TCLocalize("计分板动画"), &g_Config.m_QmScoreboardAnimOptim, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRepeatEnabled, TCLocalize("启用复读功能"), &g_Config.m_QmRepeatEnabled, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmHammerSwapSkin, TCLocalize("锤人换皮肤"), &g_Config.m_QmHammerSwapSkin, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRandomEmoteOnHit, TCLocalize("受击随机表情"), &g_Config.m_QmRandomEmoteOnHit, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmSayNoPop, TCLocalize("说话不弹表情"), &g_Config.m_QmSayNoPop, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmRainbowName, TCLocalize("彩虹名字"), &g_Config.m_QmRainbowName, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmWeaponTrajectory, TCLocalize("武器弹道辅助线"), &g_Config.m_QmWeaponTrajectory, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcJumpHint, TCLocalize("位置跳跃提示"), &g_Config.m_TcJumpHint, &Row, LG_LineHeight);
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
				DoModuleHeadline(CardContent, 4, TCLocalize("显示坐标"), TCLocalize("显示玩家坐标信息"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoordsOwn, TCLocalize("显示自己坐标"), &g_Config.m_QmNameplateCoordsOwn, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoords, TCLocalize("显示他人坐标"), &g_Config.m_QmNameplateCoords, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoordX, TCLocalize("显示X"), &g_Config.m_QmNameplateCoordX, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoordY, TCLocalize("显示Y"), &g_Config.m_QmNameplateCoordY, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoordXAlignHint, TCLocalize("对齐提示"), &g_Config.m_QmNameplateCoordXAlignHint, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmNameplateCoordXAlignHint)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmNameplateCoordXAlignHintStrict, TCLocalize("严格对齐"), &g_Config.m_QmNameplateCoordXAlignHintStrict, &Row, LG_LineHeight);
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
				DoModuleHeadline(CardContent, 5, TCLocalize("主播模式"), TCLocalize("直播/隐私保护开关"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmStreamerHideNames, TCLocalize("非好友昵称改为ID"), &g_Config.m_QmStreamerHideNames, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmStreamerHideSkins, TCLocalize("非好友皮肤改为默认"), &g_Config.m_QmStreamerHideSkins, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmStreamerScoreboardDefaultFlags, TCLocalize("计分板默认国旗"), &g_Config.m_QmStreamerScoreboardDefaultFlags, &Row, LG_LineHeight);
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
				DoModuleHeadline(CardContent, 6, TCLocalize("好友提醒"), TCLocalize("好友上线与进服提示"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFriendOnlineNotify, TCLocalize("好友上线提醒"), &g_Config.m_QmFriendOnlineNotify, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFriendOnlineAutoRefresh, TCLocalize("自动刷新服务器列表"), &g_Config.m_QmFriendOnlineAutoRefresh, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmFriendOnlineAutoRefresh)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmFriendOnlineRefreshSeconds, &g_Config.m_QmFriendOnlineRefreshSeconds, &Row, TCLocalize("刷新间隔"), 5, 300, &CUi::ms_LinearScrollbarScale, 0, "s");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFriendEnterAutoGreet, TCLocalize("好友进图自动打招呼"), &g_Config.m_QmFriendEnterAutoGreet, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmFriendEnterBroadcast, TCLocalize("大字显示好友进服"), &g_Config.m_QmFriendEnterBroadcast, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmFriendEnterBroadcast)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("大字提示文本"), LG_BodySize, TEXTALIGN_ML);
					static CLineInput s_FriendEnterBroadcastText(g_Config.m_QmFriendEnterBroadcastText, sizeof(g_Config.m_QmFriendEnterBroadcastText));
					s_FriendEnterBroadcastText.SetEmptyText(TCLocalize("使用%s代表好友名"));
					Ui()->DoEditBox(&s_FriendEnterBroadcastText, &ControlCol, LG_BodySize);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				if(g_Config.m_QmFriendEnterAutoGreet)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
					Ui()->DoLabel(&LabelCol, TCLocalize("打招呼文本"), LG_BodySize, TEXTALIGN_ML);
					static CLineInput s_FriendEnterGreetText(g_Config.m_QmFriendEnterGreetText, sizeof(g_Config.m_QmFriendEnterGreetText));
					s_FriendEnterGreetText.SetEmptyText(TCLocalize("留空禁用"));
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
				DoModuleHeadline(CardContent, 7, TCLocalize("屏蔽词"), TCLocalize("聊天屏蔽词过滤"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmBlockWordsShowConsole, TCLocalize("控制台显示屏蔽词"), &g_Config.m_QmBlockWordsShowConsole, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				static CButtonContainer s_BlockWordsConsoleColorId;
				DoLine_ColorPicker(&s_BlockWordsConsoleColorId, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("控制台颜色"), &g_Config.m_QmBlockWordsConsoleColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmBlockWordsEnabled, TCLocalize("启用屏蔽词列表"), &g_Config.m_QmBlockWordsEnabled, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmBlockWordsMultiReplace, TCLocalize("按词长多字符替换"), &g_Config.m_QmBlockWordsMultiReplace, &Row, LG_LineHeight);
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
				Ui()->DoLabel(&LabelCol, TCLocalize("替换字符"), LG_BodySize, TEXTALIGN_ML);
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
				Ui()->DoLabel(&LabelCol, TCLocalize("替换方式"), LG_BodySize, TEXTALIGN_ML);
				CUIRect ModeRow = ControlCol;
				CUIRect ModeButton;
				static CButtonContainer s_BlockWordsModeRegex, s_BlockWordsModeFull, s_BlockWordsModeBoth;
				const float ModeWidth = ModeRow.w / 3.0f;
				ModeRow.VSplitLeft(ModeWidth, &ModeButton, &ModeRow);
				if(DoButtonLineSize_Menu(&s_BlockWordsModeRegex, TCLocalize("正则"), g_Config.m_QmBlockWordsMode == 0, &ModeButton, LG_LineHeight, false, 0, IGraphics::CORNER_L, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
					g_Config.m_QmBlockWordsMode = 0;
				ModeRow.VSplitLeft(ModeWidth, &ModeButton, &ModeRow);
				if(DoButtonLineSize_Menu(&s_BlockWordsModeFull, TCLocalize("字面"), g_Config.m_QmBlockWordsMode == 1, &ModeButton, LG_LineHeight, false, 0, IGraphics::CORNER_NONE, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
					g_Config.m_QmBlockWordsMode = 1;
				if(DoButtonLineSize_Menu(&s_BlockWordsModeBoth, TCLocalize("两者"), g_Config.m_QmBlockWordsMode == 2, &ModeRow, LG_LineHeight, false, 0, IGraphics::CORNER_R, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
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
				s_BlockWordsInput.SetEmptyText(TCLocalize("用 , 分隔"));

				const float BlockInputWidth = CardContent.w - LG_LabelWidth;
				const float BlockInputLineSpacing = std::clamp(2.0f * UiScale, 1.0f, 2.0f);
				const float BlockInputHeight = CalcQiaFenInputHeight(TextRender(), s_BlockWordsInput.GetString(), BlockInputWidth, LG_BodySize, BlockInputLineSpacing, LG_LineHeight);
				CardContent.HSplitTop(BlockInputHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("屏蔽词"), LG_BodySize, TEXTALIGN_ML);
				if(DoEditBoxMultiLine(Ui(), &s_BlockWordsInput, &ControlCol, LG_BodySize, BlockInputLineSpacing))
					str_copy(g_Config.m_QmBlockWordsList, s_BlockWordsInput.GetString(), sizeof(g_Config.m_QmBlockWordsList));

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
				DoModuleHeadline(CardContent, 8, TCLocalize("关键词回复"), TCLocalize("恰分与关键词自动回复"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmAutoReplyCooldown, &g_Config.m_QmAutoReplyCooldown, &Row, TCLocalize("自动回复冷却"), 0, 30, &CUi::ms_LinearScrollbarScale, 0, TCLocalize(" 秒"));
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmQiaFenEnabled, TCLocalize("启用恰分功能"), &g_Config.m_QmQiaFenEnabled, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmQiaFenUseDummy, TCLocalize("使用分身回复"), &g_Config.m_QmQiaFenUseDummy, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				auto SyncRuleRowsFromConfig = [](std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> &vRows, bool &Inited, const char *pConfigRules) {
					std::vector<SAutoReplyRulePlain> vParsedRules;
					ParseAutoReplyRules(pConfigRules, vParsedRules);

					const auto RebuildRows = [&]() {
						vRows.clear();
						for(const auto &Rule : vParsedRules)
							vRows.push_back(CreateAutoReplyRuleInputRow(Rule.m_Keywords.c_str(), Rule.m_Reply.c_str()));
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

				static std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> s_vQiaFenRuleRows;
				static bool s_QiaFenRuleRowsInited = false;
				static CButtonContainer s_QiaFenAddRuleButton;
				static std::vector<CButtonContainer> s_vQiaFenRemoveRuleButtons;
				SyncRuleRowsFromConfig(s_vQiaFenRuleRows, s_QiaFenRuleRowsInited, g_Config.m_QmQiaFenRules);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("恰分规则"), LG_BodySize, TEXTALIGN_ML);
				CUIRect AddRuleButtonRect;
				ControlCol.VSplitRight(maximum(LG_LineHeight, 24.0f * UiScale), &ControlCol, &AddRuleButtonRect);
				if(DoButton_Menu(&s_QiaFenAddRuleButton, "+", 0, &AddRuleButtonRect))
				{
					auto pNewRule = CreateAutoReplyRuleInputRow();
					pNewRule->m_TriggerInput.Activate(EInputPriority::UI);
					s_vQiaFenRuleRows.push_back(std::move(pNewRule));
				}
				s_vQiaFenRemoveRuleButtons.resize(s_vQiaFenRuleRows.size());
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				for(size_t i = 0; i < s_vQiaFenRuleRows.size();)
				{
					auto &pRuleRow = s_vQiaFenRuleRows[i];
					pRuleRow->m_TriggerInput.SetEmptyText("");
					pRuleRow->m_ReplyInput.SetEmptyText("");
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Row.VSplitLeft(LG_LabelWidth, nullptr, &ControlCol);
					CUIRect TriggerCol, SendCol, ReplyCol, RemoveButtonRect;
					ControlCol.VSplitRight(maximum(LG_LineHeight, 24.0f * UiScale), &ControlCol, &RemoveButtonRect);
					ControlCol.VSplitLeft(ControlCol.w * 0.45f, &TriggerCol, &ControlCol);
					ControlCol.VSplitLeft(maximum(40.0f, 40.0f * UiScale), &SendCol, &ReplyCol);
					Ui()->DoEditBox(&pRuleRow->m_TriggerInput, &TriggerCol, LG_BodySize);
					Ui()->DoLabel(&SendCol, TCLocalize("发送"), LG_BodySize, TEXTALIGN_MC);
					Ui()->DoEditBox(&pRuleRow->m_ReplyInput, &ReplyCol, LG_BodySize);
					const bool RemoveClicked = DoButton_Menu(&s_vQiaFenRemoveRuleButtons[i], "-", 0, &RemoveButtonRect);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					if(RemoveClicked)
					{
						s_vQiaFenRuleRows.erase(s_vQiaFenRuleRows.begin() + i);
						s_vQiaFenRemoveRuleButtons.erase(s_vQiaFenRemoveRuleButtons.begin() + i);
						continue;
					}
					++i;
				}

				char aQiaFenRules[sizeof(g_Config.m_QmQiaFenRules)];
				BuildAutoReplyRulesFromRows(s_vQiaFenRuleRows, aQiaFenRules, sizeof(aQiaFenRules));
				str_copy(g_Config.m_QmQiaFenRules, aQiaFenRules, sizeof(g_Config.m_QmQiaFenRules));

				bool QiaFenHalfFilled = false;
				for(const auto &pRuleRow : s_vQiaFenRuleRows)
				{
					if(IsAutoReplyRuleRowHalfFilled(*pRuleRow))
					{
						QiaFenHalfFilled = true;
						break;
					}
				}
				if(QiaFenHalfFilled)
				{
					CardContent.HSplitTop(LG_LineHeight * 0.8f, &Row, &CardContent);
					TextRender()->TextColor(1.0f, 0.2f, 0.2f, 1.0f);
					Ui()->DoLabel(&Row, TCLocalize("恰分规则两侧都需要填写"), LG_BodySize * 0.7f, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				CardContent.HSplitTop(LG_CardPadding * 0.7f, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmKeywordReplyEnabled, TCLocalize("启用关键词回复"), &g_Config.m_QmKeywordReplyEnabled, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmKeywordReplyUseDummy, TCLocalize("使用分身回复"), &g_Config.m_QmKeywordReplyUseDummy, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				static std::vector<std::unique_ptr<SAutoReplyRuleInputRow>> s_vKeywordRuleRows;
				static bool s_KeywordRuleRowsInited = false;
				static CButtonContainer s_KeywordAddRuleButton;
				static std::vector<CButtonContainer> s_vKeywordRemoveRuleButtons;
				SyncRuleRowsFromConfig(s_vKeywordRuleRows, s_KeywordRuleRowsInited, g_Config.m_QmKeywordReplyRules);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Row.VSplitLeft(LG_LabelWidth, &LabelCol, &ControlCol);
				Ui()->DoLabel(&LabelCol, TCLocalize("关键词规则"), LG_BodySize, TEXTALIGN_ML);
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
					Row.VSplitLeft(LG_LabelWidth, nullptr, &ControlCol);
					CUIRect TriggerCol, SendCol, ReplyCol, RemoveButtonRect;
					ControlCol.VSplitRight(maximum(LG_LineHeight, 24.0f * UiScale), &ControlCol, &RemoveButtonRect);
					ControlCol.VSplitLeft(ControlCol.w * 0.45f, &TriggerCol, &ControlCol);
					ControlCol.VSplitLeft(maximum(40.0f, 40.0f * UiScale), &SendCol, &ReplyCol);
					Ui()->DoEditBox(&pRuleRow->m_TriggerInput, &TriggerCol, LG_BodySize);
					Ui()->DoLabel(&SendCol, TCLocalize("发送"), LG_BodySize, TEXTALIGN_MC);
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
					Ui()->DoLabel(&Row, TCLocalize("关键词规则两侧都需要填写"), LG_BodySize * 0.7f, TEXTALIGN_ML);
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
				DoModuleHeadline(CardContent, 9, TCLocalize("饼菜单"), TCLocalize("打开一个原型菜单快速使用常用功能"));
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPieMenuEnabled, TCLocalize("启用饼菜单"), &g_Config.m_QmPieMenuEnabled, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmPieMenuEnabled)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmPieMenuScale, &g_Config.m_QmPieMenuScale, &Row, TCLocalize("UI大小"), 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmPieMenuOpacity, &g_Config.m_QmPieMenuOpacity, &Row, TCLocalize("不透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmPieMenuMaxDistance, &g_Config.m_QmPieMenuMaxDistance, &Row, TCLocalize("检测距离"), 100, 2000);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_BodySize, &Row, &CardContent);
					TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.8f));
					Ui()->DoLabel(&Row, TCLocalize("选项颜色"), LG_BodySize, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					static CButtonContainer s_PieMenuColorFriend, s_PieMenuColorWhisper, s_PieMenuColorMention;
					static CButtonContainer s_PieMenuColorCopySkin, s_PieMenuColorSwap, s_PieMenuColorSpectate;
					DoLine_ColorPicker(&s_PieMenuColorFriend, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("好友"), (unsigned int *)&g_Config.m_QmPieMenuColorFriend, ColorRGBA(0.9f, 0.3f, 0.4f), true);
					DoLine_ColorPicker(&s_PieMenuColorWhisper, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("私聊"), (unsigned int *)&g_Config.m_QmPieMenuColorWhisper, ColorRGBA(0.5f, 0.35f, 0.7f), true);
					DoLine_ColorPicker(&s_PieMenuColorMention, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("提及"), (unsigned int *)&g_Config.m_QmPieMenuColorMention, ColorRGBA(0.85f, 0.5f, 0.2f), true);
					DoLine_ColorPicker(&s_PieMenuColorCopySkin, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("复制皮肤"), (unsigned int *)&g_Config.m_QmPieMenuColorCopySkin, ColorRGBA(0.25f, 0.55f, 0.8f), true);
					DoLine_ColorPicker(&s_PieMenuColorSwap, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("交换"), (unsigned int *)&g_Config.m_QmPieMenuColorSwap, ColorRGBA(0.8f, 0.3f, 0.3f), true);
					DoLine_ColorPicker(&s_PieMenuColorSpectate, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("观战"), (unsigned int *)&g_Config.m_QmPieMenuColorSpectate, ColorRGBA(0.45f, 0.55f, 0.6f), true);
				}

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
				DoModuleHeadline(CardContent, 6, TCLocalize("实体层颜色"), TCLocalize("实体层物块颜色"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoLabel(&Row, TCLocalize("需要开启实体层"), LG_BodySize, TEXTALIGN_ML);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayDeathAlpha, &g_Config.m_QmEntityOverlayDeathAlpha, &Row, TCLocalize("死亡透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayFreezeAlpha, &g_Config.m_QmEntityOverlayFreezeAlpha, &Row, TCLocalize("冻结透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayUnfreezeAlpha, &g_Config.m_QmEntityOverlayUnfreezeAlpha, &Row, TCLocalize("解冻透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayDeepFreezeAlpha, &g_Config.m_QmEntityOverlayDeepFreezeAlpha, &Row, TCLocalize("深度冻结透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayDeepUnfreezeAlpha, &g_Config.m_QmEntityOverlayDeepUnfreezeAlpha, &Row, TCLocalize("深度解冻透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlayTeleAlpha, &g_Config.m_QmEntityOverlayTeleAlpha, &Row, TCLocalize("传送透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmEntityOverlaySwitchAlpha, &g_Config.m_QmEntityOverlaySwitchAlpha, &Row, TCLocalize("开关透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_ClOverlayEntities, &g_Config.m_ClOverlayEntities, &Row, TCLocalize("叠层透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
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
				DoModuleHeadline(CardContent, 5, TCLocalize("激光设置"), TCLocalize("激光样式"));
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmLaserEnhanced, TCLocalize("增强激光特效"), &g_Config.m_QmLaserEnhanced, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmLaserGlowIntensity, &g_Config.m_QmLaserGlowIntensity, &Row, TCLocalize("辉光强度"), 0, 100);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmLaserSize, &g_Config.m_QmLaserSize, &Row, TCLocalize("激光大小"), 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				Ui()->DoScrollbarOption(&g_Config.m_QmLaserAlpha, &g_Config.m_QmLaserAlpha, &Row, TCLocalize("半透明"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmLaserRoundCaps, TCLocalize("圆角端点"), &g_Config.m_QmLaserRoundCaps, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmLaserEnhanced)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmLaserPulseSpeed, &g_Config.m_QmLaserPulseSpeed, &Row, TCLocalize("脉冲速度"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmLaserPulseAmplitude, &g_Config.m_QmLaserPulseAmplitude, &Row, TCLocalize("脉冲幅度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				// 激光预览区域
				CardContent.HSplitTop(LG_LineSpacing * 2, nullptr, &CardContent);
				CardContent.HSplitTop(LG_BodySize, &Row, &CardContent);
				Ui()->DoLabel(&Row, TCLocalize("激光预览"), LG_BodySize, TEXTALIGN_ML);
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
				// ========== 模块10: Gores玩家统计 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect Card7Start = Column;
				s_GlassCards.push_back(Card7Start);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 6, TCLocalize("Gores玩家统计"), TCLocalize("Gores玩家统计与信息显示"));

				// 显示统计HUD
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerStatsHud, TCLocalize("显示Gores玩家统计HUD"), &g_Config.m_QmPlayerStatsHud, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmPlayerStatsHud)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerStatsMapProgress, TCLocalize("地图进度条(内测中)"), &g_Config.m_QmPlayerStatsMapProgress, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				}

				// 进入服务器时重置统计
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmPlayerStatsResetOnJoin, TCLocalize("进入服务器时重置统计"), &g_Config.m_QmPlayerStatsResetOnJoin, &Row, LG_LineHeight);
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
				DoModuleHeadline(CardContent, 7, TCLocalize("碰撞体积可视化"), TCLocalize("显示玩家基础碰撞体积"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmShowCollisionHitbox, TCLocalize("显示碰撞体积"), &g_Config.m_QmShowCollisionHitbox, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmShowCollisionHitbox)
				{
					static CButtonContainer s_FreezeColorId;
					DoLine_ColorPicker(&s_FreezeColorId, LG_LineHeight, LG_BodySize, LG_LineSpacing, &CardContent, TCLocalize("Freeze边框颜色"), &g_Config.m_QmCollisionHitboxColorFreeze, ColorRGBA(1.0f, 0.0f, 1.0f), false);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmCollisionHitboxAlpha, &g_Config.m_QmCollisionHitboxAlpha, &Row, TCLocalize("透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
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
				DoModuleHeadline(CardContent, 7, TCLocalize("收藏地图"), TCLocalize("收藏地图管理"));

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
						return TCLocalize("未知");
					if(str_comp_nocase(pType, "DDmaX Easy") == 0)
						return TCLocalize("古典easy");
					if(str_comp_nocase(pType, "DDmaX Next") == 0)
						return TCLocalize("古典next");
					if(str_comp_nocase(pType, "DDmaX Pro") == 0)
						return TCLocalize("古典pro");
					if(str_comp_nocase(pType, "DDmaX Nut") == 0)
						return TCLocalize("古典nut");
					if(str_comp_nocase(pType, "DDmaX") == 0)
						return TCLocalize("古典");
					if(str_comp_nocase(pType, "Novice") == 0)
						return TCLocalize("简单");
					if(str_comp_nocase(pType, "Moderate") == 0)
						return TCLocalize("中阶");
					if(str_comp_nocase(pType, "Brutal") == 0)
						return TCLocalize("高阶");
					if(str_comp_nocase(pType, "Insane") == 0)
						return TCLocalize("疯狂");
					if(str_comp_nocase(pType, "Dummy") == 0)
						return TCLocalize("分身");
					if(str_comp_nocase(pType, "Solo") == 0)
						return TCLocalize("单人");
					if(str_comp_nocase(pType, "Oldschool") == 0)
						return TCLocalize("传统");
					if(str_comp_nocase(pType, "Race") == 0)
						return TCLocalize("竞速");
					if(str_comp_nocase(pType, "Fun") == 0)
						return TCLocalize("娱乐");
					if(str_comp_nocase(pType, "Event") == 0)
						return TCLocalize("活动");
					return TCLocalize("未知");
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
						return TCLocalize("未知");
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
					return TCLocalize("未知");
				};

				// 记录复制状态和时间
				static int s_CopiedMapIndex = -1;
				static float s_CopiedTime = 0.0f;
				if(s_CopiedMapIndex >= 0 && Client()->LocalTime() - s_CopiedTime > 1.5f)
					s_CopiedMapIndex = -1;

				if(FavMaps.empty())
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoLabel(&Row, TCLocalize("暂无收藏地图"), LG_BodySize, TEXTALIGN_ML);
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
							Ui()->DoLabel(&RowLabel, TCLocalize("已复制"), LG_BodySize, TEXTALIGN_ML);
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
							GameClient()->m_Tooltips.DoToolTip(&s_aMapButtonIds[MapIndex], &RowLabel, TCLocalize("点击复制地图名"));
						if(Ui()->HotItem() == &s_aMapRemoveButtons[MapIndex])
							GameClient()->m_Tooltips.DoToolTip(&s_aMapRemoveButtons[MapIndex], &RowRemove, TCLocalize("取消收藏"));

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
				DoModuleHeadline(CardContent, 11, TCLocalize("HJ大佬辅助"), TCLocalize("解冻相关自动化辅助"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoUnspecOnUnfreeze, TCLocalize("解冻自动取消旁观"), &g_Config.m_QmAutoUnspecOnUnfreeze, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoSwitchOnUnfreeze, TCLocalize("自动切换到解冻的tee"), &g_Config.m_QmAutoSwitchOnUnfreeze, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmAutoCloseChatOnUnfreeze, TCLocalize("从freeze醒来自动关闭当前聊天"), &g_Config.m_QmAutoCloseChatOnUnfreeze, &Row, LG_LineHeight);
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
				DoModuleHeadline(CardContent, 11, TCLocalize("按键显示"), TCLocalize("按键显示叠加"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_QmInputOverlay, TCLocalize("显示按键"), &g_Config.m_QmInputOverlay, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_QmInputOverlay)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmInputOverlayScale, &g_Config.m_QmInputOverlayScale, &Row, TCLocalize("大小"), 1, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmInputOverlayOpacity, &g_Config.m_QmInputOverlayOpacity, &Row, TCLocalize("不透明度"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmInputOverlayPosX, &g_Config.m_QmInputOverlayPosX, &Row, TCLocalize("水平位置"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_QmInputOverlayPosY, &g_Config.m_QmInputOverlayPosY, &Row, TCLocalize("垂直位置"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_BodySize, &Row, &CardContent);
					TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.8f));
					Ui()->DoLabel(&Row, TCLocalize("配置文件: data/input_overlay.json"), LG_BodySize, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_BodySize, &Row, &CardContent);
					TextRender()->TextColor(ColorRGBA(0.9f, 0.9f, 0.9f, 0.8f));
					Ui()->DoLabel(&Row, TCLocalize("外部保存后自动热重载"), LG_BodySize, TEXTALIGN_ML);
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
			case EQmModuleId::DummyMiniView:
			{
				// ========== 模块15: 分身小窗 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardDummyMiniViewStart = Column;
				s_GlassCards.push_back(CardDummyMiniViewStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 12, TCLocalize("分身小窗"), TCLocalize("分身小窗预览与缩放"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClDummyMiniView, TCLocalize("启用分身小窗"), &g_Config.m_ClDummyMiniView, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				CardContent.HSplitTop(LG_LineHeight * 0.8f, &Row, &CardContent);
				Ui()->DoLabel(&Row, TCLocalize("性能消耗极大,且使用AMD+Vulkan会造成已知且不能修复的BUG"), LG_BodySize * 0.7f, TEXTALIGN_ML);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_ClDummyMiniView)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_ClDummyMiniViewSize, &g_Config.m_ClDummyMiniViewSize, &Row, TCLocalize("小窗大小"), 50, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					Ui()->DoScrollbarOption(&g_Config.m_ClDummyMiniViewZoom, &g_Config.m_ClDummyMiniViewZoom, &Row, TCLocalize("小窗缩放"), 10, 300, &CUi::ms_LinearScrollbarScale, 0, "%");
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);
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
				// ========== 模块16: 系统媒体控制 ==========
				Column.HSplitTop(LG_CardSpacing, nullptr, &Column);
				CUIRect CardSystemMediaControlsStart = Column;
				s_GlassCards.push_back(CardSystemMediaControlsStart);

				Column.HSplitTop(LG_CardPadding, nullptr, &Column);
				Column.VSplitLeft(LG_CardPadding, nullptr, &CardContent);
				CardContent.VSplitRight(LG_CardPadding, &CardContent, nullptr);
				DoModuleHeadline(CardContent, 13, TCLocalize("系统媒体控制"), TCLocalize("系统媒体控制开关与按键"));

				CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSmtcEnable, Localize("启用系统媒体控制"), &g_Config.m_ClSmtcEnable, &Row, LG_LineHeight);
				CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

				if(g_Config.m_ClSmtcEnable)
				{
					CardContent.HSplitTop(LG_LineHeight, &Row, &CardContent);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSmtcShowHud, Localize("显示左上角歌曲信息"), &g_Config.m_ClSmtcShowHud, &Row, LG_LineHeight);
					CardContent.HSplitTop(LG_LineSpacing, nullptr, &CardContent);

					CUIRect MediaButtons, PrevButton, PlayButton, NextButton;
					CardContent.HSplitTop(LG_LineHeight, &MediaButtons, &CardContent);
					MediaButtons.VSplitLeft((MediaButtons.w - LG_LineSpacing * 2.0f) / 3.0f, &PrevButton, &MediaButtons);
					MediaButtons.VSplitLeft(LG_LineSpacing, nullptr, &MediaButtons);
					MediaButtons.VSplitLeft((MediaButtons.w - LG_LineSpacing) / 2.0f, &PlayButton, &MediaButtons);
					MediaButtons.VSplitLeft(LG_LineSpacing, nullptr, &MediaButtons);
					NextButton = MediaButtons;

					static CButtonContainer s_SmtcPrev;
					if(DoButton_Menu(&s_SmtcPrev, Localize("上一个"), 0, &PrevButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f))
						GameClient()->m_SystemMediaControls.Previous();

					static CButtonContainer s_SmtcPlayPause;
					if(DoButton_Menu(&s_SmtcPlayPause, Localize("播放/暂停"), 0, &PlayButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f))
						GameClient()->m_SystemMediaControls.PlayPause();

					static CButtonContainer s_SmtcNext;
					if(DoButton_Menu(&s_SmtcNext, Localize("下一个"), 0, &NextButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f))
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

	EnsureColumnTops();
	RenderColumnModules(VisibleLeftModules, EQmModuleColumn::Left);
	RenderColumnModules(VisibleRightModules, EQmModuleColumn::Right);
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
