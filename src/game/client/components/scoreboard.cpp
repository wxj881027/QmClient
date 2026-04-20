/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "scoreboard.h"

#include <engine/demo.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data7.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/motd.h>
#include <game/client/components/player_points.h>
#include <game/client/components/statboard.h>
#include <game/client/gameclient.h>
#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmLayout.h>
#include <game/client/QmUi/QmLegacy.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
float ResolveAnimatedValue(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, EUiAnimProperty Property, float Target, float &LastTarget, float DurationSec)
{
	constexpr float Epsilon = 0.001f;
	const float Current = AnimRuntime.GetValue(NodeKey, Property, Target);
	const bool TargetChanged = std::abs(Target - LastTarget) > Epsilon;
	const bool NeedsSync = !AnimRuntime.HasActiveAnimation(NodeKey, Property) && std::abs(Target - Current) > Epsilon;

	if(TargetChanged || NeedsSync)
	{
		SUiAnimRequest Request;
		Request.m_NodeKey = NodeKey;
		Request.m_Property = Property;
		Request.m_Target = Target;
		Request.m_Transition.m_DurationSec = DurationSec;
		Request.m_Transition.m_DelaySec = 0.0f;
		Request.m_Transition.m_Priority = 1;
		Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
		Request.m_Transition.m_Easing = EEasing::EASE_OUT;
		AnimRuntime.RequestAnimation(Request);
		LastTarget = Target;
	}

	return AnimRuntime.GetValue(NodeKey, Property, Target);
}

uint64_t SoundMuteButtonNodeKey(int Index)
{
	static constexpr uint64_t NodeBase = 0x73636F72655F6D00ULL; // "score_m"
	return NodeBase + static_cast<uint64_t>(Index);
}

uint64_t SoundMuteInfoNodeKey()
{
	static constexpr uint64_t NodeKey = 0x73636F72655F694EULL; // "score_iN"
	return NodeKey;
}

struct SSoundMuteButtonDef
{
	int *m_pConfig;
	const char *m_pIcon;
	const char *m_pTitle;
	const char *m_pDescription;
};

static const SSoundMuteButtonDef gs_aSoundMuteButtonDefs[] = {
	{&g_Config.m_ClSndMuteWeapon, FontIcons::FONT_ICON_CIRCLE, "武器音效", "屏蔽主要武器发射与命中相关声音。"},
	{&g_Config.m_ClSndMuteWeaponSwitch, FontIcons::FONT_ICON_ARROWS_LEFT_RIGHT, "武器切换音效", "屏蔽武器切换及相关切换提示音。"},
	{&g_Config.m_ClSndMuteWeaponNoAmmo, FontIcons::FONT_ICON_TRIANGLE_EXCLAMATION, "无弹药提示音", "屏蔽武器无弹药时的提示音。"},
	{&g_Config.m_ClSndMuteHook, FontIcons::FONT_ICON_ARROWS_ROTATE, "钩子音效", "屏蔽钩子发射、收回等相关声音。"},
	{&g_Config.m_ClSndMuteMovement, FontIcons::FONT_ICON_ARROWS_UP_DOWN, "移动音效", "屏蔽行走与跳跃等移动相关声音。"},
	{&g_Config.m_ClSndMutePlayerState, FontIcons::FONT_ICON_HEART_CRACK, "玩家状态音效", "屏蔽玩家状态变化相关声音。"},
	{&g_Config.m_ClSndMutePickup, FontIcons::FONT_ICON_SQUARE_PLUS, "拾取音效", "屏蔽道具与武器拾取相关声音。"},
	{&g_Config.m_ClSndMuteFlag, FontIcons::FONT_ICON_FLAG_CHECKERED, "旗帜音效", "屏蔽 CTF 旗帜事件相关声音。"},
	{&g_Config.m_ClSndMuteMapSound, FontIcons::FONT_ICON_MAP, "地图音效", "屏蔽地图环境与脚本触发音效。"},
};
static_assert((sizeof(gs_aSoundMuteButtonDefs) / sizeof(gs_aSoundMuteButtonDefs[0])) == 9, "Sound mute button count mismatch");
}

CScoreboard::CScoreboard()
{
	OnReset();
}

void CScoreboard::SetUiMousePos(vec2 Pos)
{
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = Ui()->Screen();

	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	Pos = Pos / vec2(pScreen->w, pScreen->h) * WindowSize;
	Ui()->OnCursorMove(Pos.x - UpdatedMousePos.x, Pos.y - UpdatedMousePos.y);
}

void CScoreboard::ConKeyScoreboard(IConsole::IResult *pResult, void *pUserData)
{
	CScoreboard *pSelf = static_cast<CScoreboard *>(pUserData);

	pSelf->GameClient()->m_Spectator.OnRelease();
	pSelf->GameClient()->m_Emoticon.OnRelease();

	pSelf->m_Active = pResult->GetInteger(0) != 0;

	if(!pSelf->IsActive() && pSelf->m_MouseUnlocked)
	{
		pSelf->Ui()->ClosePopupMenus();
		pSelf->m_MouseUnlocked = false;
		pSelf->SetUiMousePos(pSelf->m_LastMousePos.value());
		pSelf->m_LastMousePos = pSelf->Ui()->MousePos();
	}
}

void CScoreboard::ConToggleScoreboardCursor(IConsole::IResult *pResult, void *pUserData)
{
	CScoreboard *pSelf = static_cast<CScoreboard *>(pUserData);

	if(pSelf->GameClient()->m_Chat.IsActive())
	{
		pSelf->GameClient()->m_Chat.ToggleMouseUnlocked();
		return;
	}

	if(!pSelf->IsActive() ||
		pSelf->GameClient()->m_Menus.IsActive() ||
		pSelf->Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		return;
	}

	pSelf->m_MouseUnlocked = !pSelf->m_MouseUnlocked;

	vec2 OldMousePos = pSelf->Ui()->MousePos();

	if(pSelf->m_LastMousePos == std::nullopt)
	{
		pSelf->SetUiMousePos(pSelf->Ui()->Screen()->Center());
	}
	else
	{
		pSelf->SetUiMousePos(pSelf->m_LastMousePos.value());
	}

	// save pos, so moving the mouse in esc menu doesn't change the position
	pSelf->m_LastMousePos = OldMousePos;
}

void CScoreboard::OnConsoleInit()
{
	Console()->Register("+scoreboard", "", CFGFLAG_CLIENT, ConKeyScoreboard, this, "Show scoreboard");
	Console()->Register("toggle_scoreboard_cursor", "", CFGFLAG_CLIENT, ConToggleScoreboardCursor, this, "Toggle scoreboard cursor");
}

void CScoreboard::OnInit()
{
	m_DeadTeeTexture = Graphics()->LoadTexture("deadtee.png", IStorage::TYPE_ALL);
}

void CScoreboard::OnReset()
{
	m_Active = false;
	m_ServerRecord = -1.0f;
	m_Visibility = 0.0f;
	m_OpenTime = 0.0f;
	m_AnimContentAlpha = 0.0f;
	m_MouseUnlocked = false;
	m_LastMousePos = std::nullopt;
	m_SoundMuteButtonAnimState.Reset();
	m_SoundMuteInfoAnimState.Reset();
}

void CScoreboard::OnRelease()
{
	m_Active = false;
	m_Visibility = 0.0f;
	m_OpenTime = 0.0f;
	m_AnimContentAlpha = 0.0f;
	m_SoundMuteButtonAnimState.Reset();
	m_SoundMuteInfoAnimState.Reset();

	if(m_MouseUnlocked)
	{
		Ui()->ClosePopupMenus();
		m_MouseUnlocked = false;
		SetUiMousePos(m_LastMousePos.value());
		m_LastMousePos = Ui()->MousePos();
	}
}

void CScoreboard::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType == NETMSGTYPE_SV_RECORD)
	{
		CNetMsg_Sv_Record *pMsg = static_cast<CNetMsg_Sv_Record *>(pRawMsg);
		m_ServerRecord = pMsg->m_ServerTimeBest / 100.0f;
	}
	else if(MsgType == NETMSGTYPE_SV_RECORDLEGACY)
	{
		CNetMsg_Sv_RecordLegacy *pMsg = static_cast<CNetMsg_Sv_RecordLegacy *>(pRawMsg);
		m_ServerRecord = pMsg->m_ServerTimeBest / 100.0f;
	}
}

bool CScoreboard::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive() || !m_MouseUnlocked)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);

	return true;
}

bool CScoreboard::OnInput(const IInput::CEvent &Event)
{
	if(m_MouseUnlocked && Event.m_Key == KEY_ESCAPE && (Event.m_Flags & IInput::FLAG_PRESS))
	{
		Ui()->ClosePopupMenus();
		m_MouseUnlocked = false;
		if(m_LastMousePos.has_value())
			SetUiMousePos(m_LastMousePos.value());
		m_LastMousePos = Ui()->MousePos();
		return true;
	}

	return IsActive() && m_MouseUnlocked;
}

void CScoreboard::RenderTitle(CUIRect TitleBar, int Team, const char *pTitle)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	char aScore[128] = "";
	if(GameClient()->m_GameInfo.m_TimeScore)
	{
		if(m_ServerRecord > 0)
		{
			str_time_float(m_ServerRecord, TIME_HOURS, aScore, sizeof(aScore));
		}
	}
	else if(GameClient()->IsTeamPlay())
	{
		const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
		if(pGameDataObj)
		{
			str_format(aScore, sizeof(aScore), "%d", Team == TEAM_RED ? pGameDataObj->m_TeamscoreRed : pGameDataObj->m_TeamscoreBlue);
		}
	}
	else
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_Active &&
			GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW &&
			GameClient()->m_Snap.m_apPlayerInfos[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId])
		{
			str_format(aScore, sizeof(aScore), "%d", GameClient()->m_Snap.m_apPlayerInfos[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId]->m_Score);
		}
		else if(GameClient()->m_Snap.m_pLocalInfo)
		{
			str_format(aScore, sizeof(aScore), "%d", GameClient()->m_Snap.m_pLocalInfo->m_Score);
		}
	}

	const float TitleFontSize = 20.0f;
	const float ScoreTextWidth = TextRender()->TextWidth(TitleFontSize, aScore);

	TitleBar.VMargin(10.0f, &TitleBar);
	CUIRect TitleLabel;
	CUIRect ScoreLabel;
	{
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle TitleRowStyle;
		TitleRowStyle.m_Axis = EUiAxis::ROW;
		TitleRowStyle.m_Gap = 5.0f;
		TitleRowStyle.m_AlignItems = EUiAlign::STRETCH;
		TitleRowStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vTitleChildren;
		std::vector<SUiLayoutChild> &vTitleChildren = s_vTitleChildren;
		vTitleChildren.assign(2, SUiLayoutChild{});
		if(Team == TEAM_RED)
		{
			vTitleChildren[0].m_Style.m_Width = SUiLength::Flex(1.0f);
			vTitleChildren[1].m_Style.m_Width = SUiLength::Px(ScoreTextWidth);
			LayoutEngine.ComputeChildren(TitleRowStyle, CUiV2LegacyAdapter::FromCUIRect(TitleBar), vTitleChildren);
			TitleLabel = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[0].m_Box);
			ScoreLabel = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[1].m_Box);
		}
		else
		{
			vTitleChildren[0].m_Style.m_Width = SUiLength::Px(ScoreTextWidth);
			vTitleChildren[1].m_Style.m_Width = SUiLength::Flex(1.0f);
			LayoutEngine.ComputeChildren(TitleRowStyle, CUiV2LegacyAdapter::FromCUIRect(TitleBar), vTitleChildren);
			ScoreLabel = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[0].m_Box);
			TitleLabel = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[1].m_Box);
		}
	}

	{
		SLabelProperties Props;
		Props.m_MaxWidth = TitleLabel.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&TitleLabel, pTitle, TitleFontSize, Team == TEAM_RED ? TEXTALIGN_ML : TEXTALIGN_MR, Props);
	}

	if(aScore[0] != '\0')
	{
		Ui()->DoLabel(&ScoreLabel, aScore, TitleFontSize, Team == TEAM_RED ? TEXTALIGN_MR : TEXTALIGN_ML);
	}
}

void CScoreboard::RenderGoals(CUIRect Goals)
{
	const float ContentAlpha = m_AnimContentAlpha;
	Goals.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * ContentAlpha), IGraphics::CORNER_ALL, 7.5f);
	Goals.VMargin(5.0f, &Goals);

	const float FontSize = 10.0f;
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	char aBuf[64];

	if(pGameInfoObj->m_ScoreLimit)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Score limit"), pGameInfoObj->m_ScoreLimit);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_ML);
	}

	if(pGameInfoObj->m_TimeLimit)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Time limit: %d min"), pGameInfoObj->m_TimeLimit);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_MC);
	}

	if(pGameInfoObj->m_RoundNum && pGameInfoObj->m_RoundCurrent)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Round %d/%d"), pGameInfoObj->m_RoundCurrent, pGameInfoObj->m_RoundNum);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_MR);
	}
}

void CScoreboard::RenderSpectators(CUIRect Spectators)
{
	const float ContentAlpha = m_AnimContentAlpha;
	const ColorRGBA BaseTextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha);
	const ColorRGBA BaseOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha);
	TextRender()->TextColor(BaseTextColor);
	TextRender()->TextOutlineColor(BaseOutlineColor);

	const bool ShowMediaControls = g_Config.m_ClSmtcEnable != 0;
	CUIRect SpectatorPanel = Spectators;
	CUIRect MediaPanel;
	if(ShowMediaControls)
	{
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle PanelStyle;
		PanelStyle.m_Axis = EUiAxis::ROW;
		PanelStyle.m_Gap = 5.0f;
		PanelStyle.m_AlignItems = EUiAlign::STRETCH;
		PanelStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vPanels;
		std::vector<SUiLayoutChild> &vPanels = s_vPanels;
		vPanels.assign(2, SUiLayoutChild{});
		vPanels[0].m_Style.m_Width = SUiLength::Flex(1.0f);
		vPanels[1].m_Style.m_Width = SUiLength::Flex(1.0f);
		LayoutEngine.ComputeChildren(PanelStyle, CUiV2LegacyAdapter::FromCUIRect(Spectators), vPanels);
		SpectatorPanel = CUiV2LegacyAdapter::ToCUIRect(vPanels[0].m_Box);
		MediaPanel = CUiV2LegacyAdapter::ToCUIRect(vPanels[1].m_Box);
	}

	const float CornerRadius = 7.5f;
	SpectatorPanel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * ContentAlpha), IGraphics::CORNER_ALL, CornerRadius);
	CUIRect SpectatorList = SpectatorPanel;
	SpectatorList.Margin(5.0f, &SpectatorList);

	CUIRect MediaControls;
	if(ShowMediaControls)
	{
		MediaPanel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * ContentAlpha), IGraphics::CORNER_ALL, CornerRadius);
		MediaControls = MediaPanel;
		MediaControls.Margin(5.0f, &MediaControls);
	}

	CTextCursor Cursor;
	Cursor.SetPosition(SpectatorList.TopLeft());
	Cursor.m_FontSize = 11.0f;
	Cursor.m_LineWidth = SpectatorList.w;
	Cursor.m_MaxLines = round_truncate(SpectatorList.h / Cursor.m_FontSize);

	int RemainingSpectators = 0;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfo || pInfo->m_Team != TEAM_SPECTATORS)
			continue;
		++RemainingSpectators;
	}

	TextRender()->TextEx(&Cursor, Localize("Spectators"));

	if(RemainingSpectators > 0)
	{
		TextRender()->TextEx(&Cursor, ": ");
	}

	bool CommaNeeded = false;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfo || pInfo->m_Team != TEAM_SPECTATORS)
			continue;

		if(CommaNeeded)
		{
			TextRender()->TextEx(&Cursor, ", ");
		}

		if(Cursor.m_LineCount == Cursor.m_MaxLines && RemainingSpectators >= 2)
		{
			// This is less expensive than checking with a separate invisible
			// text cursor though we waste some space at the end of the line.
			char aRemaining[64];
			str_format(aRemaining, sizeof(aRemaining), Localize("%d others…", "Spectators"), RemainingSpectators);
			TextRender()->TextEx(&Cursor, aRemaining);
			break;
		}

		const int ClientId = pInfo->m_ClientId;
		const bool HideIdentity = GameClient()->ShouldHideStreamerIdentity(ClientId);
		char aNameBuf[MAX_NAME_LENGTH];
		char aClanBuf[MAX_CLAN_LENGTH];
		GameClient()->FormatStreamerName(ClientId, aNameBuf, sizeof(aNameBuf));
		GameClient()->FormatStreamerClan(ClientId, aClanBuf, sizeof(aClanBuf));

		if(g_Config.m_ClShowIds && !HideIdentity)
		{
			char aClientId[16];
			GameClient()->FormatClientId(pInfo->m_ClientId, aClientId, EClientIdFormat::NO_INDENT);
			TextRender()->TextEx(&Cursor, aClientId);
		}

		{
			const char *pClanName = aClanBuf;
			if(pClanName[0] != '\0')
			{
				if(GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0 && str_comp(pClanName, GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_aClan) == 0)
				{
					TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor)).WithMultipliedAlpha(ContentAlpha));
				}
				else
				{
					TextRender()->TextColor(ColorRGBA(0.7f, 0.7f, 0.7f, ContentAlpha));
				}

				TextRender()->TextEx(&Cursor, pClanName);
				TextRender()->TextEx(&Cursor, " ");

				TextRender()->TextColor(BaseTextColor);
			}
		}

		if(GameClient()->m_aClients[ClientId].m_AuthLevel)
		{
			TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClAuthedPlayerColor)).WithMultipliedAlpha(ContentAlpha));
		}

		TextRender()->TextEx(&Cursor, aNameBuf);
		TextRender()->TextColor(BaseTextColor);

		CommaNeeded = true;
		--RemainingSpectators;
	}

	if(ShowMediaControls)
	{
		RenderMediaControls(MediaControls);
	}
}

void CScoreboard::RenderMediaControls(CUIRect Controls)
{
	const float ContentAlpha = m_AnimContentAlpha;
	const ColorRGBA BaseTextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha);
	const ColorRGBA BaseOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha);
	auto &&RestoreTextColors = [&]() {
		TextRender()->TextColor(BaseTextColor);
		TextRender()->TextOutlineColor(BaseOutlineColor);
	};
	RestoreTextColors();

	CSystemMediaControls::SState MediaState;
	const bool HasMedia = GameClient()->m_SystemMediaControls.GetStateSnapshot(MediaState);
	const bool CanToggle = HasMedia && (MediaState.m_CanPlay || MediaState.m_CanPause);
	const bool CanPrev = HasMedia && MediaState.m_CanPrev;
	const bool CanNext = HasMedia && MediaState.m_CanNext;

	char aMediaBuf[256];
	aMediaBuf[0] = '\0';
	if(HasMedia)
	{
		if(MediaState.m_aTitle[0] != '\0' && MediaState.m_aArtist[0] != '\0')
			str_format(aMediaBuf, sizeof(aMediaBuf), "%s - %s", MediaState.m_aTitle, MediaState.m_aArtist);
		else if(MediaState.m_aTitle[0] != '\0')
			str_copy(aMediaBuf, MediaState.m_aTitle, sizeof(aMediaBuf));
		else if(MediaState.m_aArtist[0] != '\0')
			str_copy(aMediaBuf, MediaState.m_aArtist, sizeof(aMediaBuf));
	}

	CUIRect ButtonArea = Controls;
	if(aMediaBuf[0] != '\0')
	{
		const float TitleFontSize = 10.0f;
		const float TitleHeight = TitleFontSize + 1.0f;
		CUIRect TitleRect;
		ButtonArea.HSplitTop(TitleHeight, &TitleRect, &ButtonArea);
		ButtonArea.HSplitTop(2.0f, nullptr, &ButtonArea);

		SLabelProperties Props;
		Props.m_MaxWidth = TitleRect.w;
		Props.m_EllipsisAtEnd = true;
		Props.m_MinimumFontSize = TitleFontSize;
		Ui()->DoLabel(&TitleRect, aMediaBuf, TitleFontSize, TEXTALIGN_MC, Props);
	}

	CUIRect Row = ButtonArea;
	const float LineSize = 20.0f;
	if(Row.h > LineSize)
	{
		Row.HSplitTop((Row.h - LineSize) * 0.5f, nullptr, &Row);
		Row.HSplitTop(LineSize, &Row, nullptr);
	}

	CUIRect PrevButton, PlayButton, NextButton;
	const float Spacing = 5.0f;
	{
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle ButtonRowStyle;
		ButtonRowStyle.m_Axis = EUiAxis::ROW;
		ButtonRowStyle.m_Gap = Spacing;
		ButtonRowStyle.m_AlignItems = EUiAlign::STRETCH;
		ButtonRowStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vButtons;
		std::vector<SUiLayoutChild> &vButtons = s_vButtons;
		vButtons.assign(3, SUiLayoutChild{});
		vButtons[0].m_Style.m_Width = SUiLength::Flex(1.0f);
		vButtons[1].m_Style.m_Width = SUiLength::Flex(1.0f);
		vButtons[2].m_Style.m_Width = SUiLength::Flex(1.0f);
		LayoutEngine.ComputeChildren(ButtonRowStyle, CUiV2LegacyAdapter::FromCUIRect(Row), vButtons);
		PrevButton = CUiV2LegacyAdapter::ToCUIRect(vButtons[0].m_Box);
		PlayButton = CUiV2LegacyAdapter::ToCUIRect(vButtons[1].m_Box);
		NextButton = CUiV2LegacyAdapter::ToCUIRect(vButtons[2].m_Box);
	}

	static CButtonContainer s_SmtcPrevButton;
	const float PrevButtonAlpha = 0.5f * Ui()->ButtonColorMul(&s_SmtcPrevButton) * ContentAlpha;
	if(Ui()->DoButton_FontIcon(&s_SmtcPrevButton, FontIcons::FONT_ICON_BACKWARD_STEP, 0, &PrevButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, CanPrev, ColorRGBA(1.0f, 1.0f, 1.0f, PrevButtonAlpha)))
	{
		GameClient()->m_SystemMediaControls.Previous();
	}
	RestoreTextColors();

	static CButtonContainer s_SmtcPlayButton;
	const char *pPlayIcon = MediaState.m_Playing ? FontIcons::FONT_ICON_PAUSE : FontIcons::FONT_ICON_PLAY;
	const float PlayButtonAlpha = 0.5f * Ui()->ButtonColorMul(&s_SmtcPlayButton) * ContentAlpha;
	if(Ui()->DoButton_FontIcon(&s_SmtcPlayButton, pPlayIcon, 0, &PlayButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, CanToggle, ColorRGBA(1.0f, 1.0f, 1.0f, PlayButtonAlpha)))
	{
		GameClient()->m_SystemMediaControls.PlayPause();
	}
	RestoreTextColors();

	static CButtonContainer s_SmtcNextButton;
	const float NextButtonAlpha = 0.5f * Ui()->ButtonColorMul(&s_SmtcNextButton) * ContentAlpha;
	if(Ui()->DoButton_FontIcon(&s_SmtcNextButton, FontIcons::FONT_ICON_FORWARD_STEP, 0, &NextButton, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, CanNext, ColorRGBA(1.0f, 1.0f, 1.0f, NextButtonAlpha)))
	{
		GameClient()->m_SystemMediaControls.Next();
	}
	RestoreTextColors();
}

void CScoreboard::RenderSoundMuteBar(CUIRect ScoreboardRect)
{
	const float ContentAlpha = m_AnimContentAlpha;
	const ColorRGBA BaseTextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha);
	const ColorRGBA BaseOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha);
	auto &&RestoreTextColors = [&]() {
		TextRender()->TextColor(BaseTextColor);
		TextRender()->TextOutlineColor(BaseOutlineColor);
	};
	RestoreTextColors();

	static CButtonContainer s_aButtons[SOUND_MUTE_BUTTON_COUNT];
	std::array<CUIRect, SOUND_MUTE_BUTTON_COUNT> aAnimatedRects{};

	const int NumButtons = static_cast<int>(sizeof(gs_aSoundMuteButtonDefs) / sizeof(gs_aSoundMuteButtonDefs[0]));
	dbg_assert(NumButtons == SOUND_MUTE_BUTTON_COUNT, "Sound mute button state mismatch");

	const float ButtonSize = 28.0f;
	const float Gap = 4.0f;
	const float ColumnHeight = NumButtons * ButtonSize + (NumButtons - 1) * Gap;
	const float ColumnX = maximum(0.0f, ScoreboardRect.x - ButtonSize - 6.0f);
	const float ColumnY = ScoreboardRect.y + (ScoreboardRect.h - ColumnHeight) * 0.5f;
	const CUIRect *pScreen = Ui()->Screen();

	const float MouseX = Ui()->MouseX();
	const float MouseY = Ui()->MouseY();
	const float ActivationLeft = maximum(0.0f, ScoreboardRect.x - 70.0f);
	const bool InActivationZone = m_MouseUnlocked &&
				      MouseX >= ActivationLeft && MouseX <= ScoreboardRect.x &&
				      MouseY >= ScoreboardRect.y && MouseY <= ScoreboardRect.y + ScoreboardRect.h;

	int NearestIndex = -1;
	if(InActivationZone)
	{
		float BestDistanceSq = std::numeric_limits<float>::max();
		for(int i = 0; i < NumButtons; ++i)
		{
			const float CenterX = ColumnX + ButtonSize * 0.5f;
			const float CenterY = ColumnY + i * (ButtonSize + Gap) + ButtonSize * 0.5f;
			const float Dx = MouseX - CenterX;
			const float Dy = MouseY - CenterY;
			const float DistanceSq = Dx * Dx + Dy * Dy;
			if(DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				NearestIndex = i;
			}
		}
	}

	CUiV2AnimationRuntime *pAnimRuntime = nullptr;
	if(GameClient()->UiRuntimeV2()->Enabled())
		pAnimRuntime = &GameClient()->UiRuntimeV2()->AnimRuntime();

	if(pAnimRuntime != nullptr && !m_SoundMuteButtonAnimState.m_Initialized)
	{
		for(int i = 0; i < NumButtons; ++i)
		{
			const uint64_t NodeKey = SoundMuteButtonNodeKey(i);
			pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::ALPHA, 0.0f);
			pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::SCALE, 1.0f);
			pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::POS_X, 18.0f);
			pAnimRuntime->SetValue(NodeKey, EUiAnimProperty::POS_Y, 0.0f);
			m_SoundMuteButtonAnimState.m_aTargetAlpha[i] = 0.0f;
			m_SoundMuteButtonAnimState.m_aTargetScale[i] = 1.0f;
			m_SoundMuteButtonAnimState.m_aTargetOffsetX[i] = 18.0f;
			m_SoundMuteButtonAnimState.m_aTargetReveal[i] = 0.0f;
		}
		m_SoundMuteButtonAnimState.m_Initialized = true;
	}

	if(pAnimRuntime != nullptr && !m_SoundMuteInfoAnimState.m_Initialized)
	{
		pAnimRuntime->SetValue(SoundMuteInfoNodeKey(), EUiAnimProperty::ALPHA, 0.0f);
		pAnimRuntime->SetValue(SoundMuteInfoNodeKey(), EUiAnimProperty::POS_X, 14.0f);
		m_SoundMuteInfoAnimState.m_TargetAlpha = 0.0f;
		m_SoundMuteInfoAnimState.m_TargetOffsetX = 14.0f;
		m_SoundMuteInfoAnimState.m_Initialized = true;
	}

	int HoveredNearestIndex = -1;
	const float ClipWidth = ScoreboardRect.x - pScreen->x;
	const bool ClipEnabled = ClipWidth > 0.0f;
	CUIRect ClipRect = *pScreen;
	if(ClipEnabled)
	{
		ClipRect.w = ClipWidth;
		Ui()->ClipEnable(&ClipRect);
	}
	for(int i = 0; i < NumButtons; ++i)
	{
		const bool IsNearest = InActivationZone && i == NearestIndex;
		const bool IsNeighbor = InActivationZone && (i == NearestIndex - 1 || i == NearestIndex + 1);
		const bool IsOuterNeighbor = InActivationZone && (i == NearestIndex - 2 || i == NearestIndex + 2);

		float TargetAlpha = 0.0f;
		float TargetScale = 1.0f;
		float TargetOffsetX = 18.0f;
		float TargetYOffset = 0.0f;

		if(IsNearest)
		{
			TargetAlpha = 1.0f;
			TargetScale = 1.0f;
			TargetOffsetX = 0.0f;
			TargetYOffset = -7.0f;
		}
		else if(IsNeighbor)
		{
			TargetAlpha = 0.65f;
			TargetScale = 1.0f;
			TargetOffsetX = 10.0f;
			TargetYOffset = -4.0f;
		}
		else if(IsOuterNeighbor)
		{
			TargetAlpha = 0.35f;
			TargetScale = 1.0f;
			TargetOffsetX = 16.0f;
			TargetYOffset = -1.0f;
		}

		float Alpha = TargetAlpha;
		float Scale = TargetScale;
		float OffsetX = TargetOffsetX;
		float YOffset = TargetYOffset;
		if(pAnimRuntime != nullptr)
		{
			const uint64_t NodeKey = SoundMuteButtonNodeKey(i);
			Alpha = ResolveAnimatedValue(*pAnimRuntime, NodeKey, EUiAnimProperty::ALPHA, TargetAlpha, m_SoundMuteButtonAnimState.m_aTargetAlpha[i], 0.12f);
			Scale = ResolveAnimatedValue(*pAnimRuntime, NodeKey, EUiAnimProperty::SCALE, TargetScale, m_SoundMuteButtonAnimState.m_aTargetScale[i], 0.12f);
			OffsetX = ResolveAnimatedValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_X, TargetOffsetX, m_SoundMuteButtonAnimState.m_aTargetOffsetX[i], 0.12f);
			YOffset = ResolveAnimatedValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_Y, TargetYOffset, m_SoundMuteButtonAnimState.m_aTargetReveal[i], 0.12f);
		}
		else
		{
			m_SoundMuteButtonAnimState.m_aTargetAlpha[i] = TargetAlpha;
			m_SoundMuteButtonAnimState.m_aTargetScale[i] = TargetScale;
			m_SoundMuteButtonAnimState.m_aTargetOffsetX[i] = TargetOffsetX;
			m_SoundMuteButtonAnimState.m_aTargetReveal[i] = TargetYOffset;
		}

		Alpha = std::clamp(Alpha, 0.0f, 1.0f);
		Scale = 1.0f;
		if(Alpha <= 0.01f || Scale <= 0.01f)
			continue;

		CUIRect Button = {ColumnX, ColumnY + i * (ButtonSize + Gap), ButtonSize, ButtonSize};
		Button.x += OffsetX;
		Button.y += YOffset;
		const float ScaledSize = ButtonSize;
		Button.w = ScaledSize;
		Button.h = ScaledSize;

		if(Button.w <= 1.0f || Button.h <= 1.0f)
			continue;

		aAnimatedRects[i] = Button;

		const bool Active = *gs_aSoundMuteButtonDefs[i].m_pConfig != 0;
		const bool Clickable = IsNearest;
		const float RenderAlpha = Alpha * ContentAlpha;
		const ColorRGBA ButtonColor = Active ?
						      ColorRGBA(1.0f, 0.32f, 0.32f, 0.95f * RenderAlpha) :
						      ColorRGBA(0.82f, 0.88f, 0.96f, 0.45f * RenderAlpha);
		if(Ui()->DoButton_FontIcon(&s_aButtons[i], gs_aSoundMuteButtonDefs[i].m_pIcon, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, true, ButtonColor) && Clickable)
			*gs_aSoundMuteButtonDefs[i].m_pConfig ^= 1;
		RestoreTextColors();

		if(Clickable && Ui()->HotItem() == &s_aButtons[i])
			HoveredNearestIndex = i;
	}
	if(ClipEnabled)
		Ui()->ClipDisable();

	const float InfoTargetAlpha = HoveredNearestIndex >= 0 ? 1.0f : 0.0f;
	const float InfoTargetOffsetX = HoveredNearestIndex >= 0 ? 0.0f : 14.0f;
	float InfoAlpha = InfoTargetAlpha;
	float InfoOffsetX = InfoTargetOffsetX;
	if(pAnimRuntime != nullptr)
	{
		InfoAlpha = ResolveAnimatedValue(*pAnimRuntime, SoundMuteInfoNodeKey(), EUiAnimProperty::ALPHA, InfoTargetAlpha, m_SoundMuteInfoAnimState.m_TargetAlpha, 0.10f);
		InfoOffsetX = ResolveAnimatedValue(*pAnimRuntime, SoundMuteInfoNodeKey(), EUiAnimProperty::POS_X, InfoTargetOffsetX, m_SoundMuteInfoAnimState.m_TargetOffsetX, 0.10f);
	}
	else
	{
		m_SoundMuteInfoAnimState.m_TargetAlpha = InfoTargetAlpha;
		m_SoundMuteInfoAnimState.m_TargetOffsetX = InfoTargetOffsetX;
	}
	InfoAlpha = std::clamp(InfoAlpha, 0.0f, 1.0f);

	if(HoveredNearestIndex >= 0)
		m_SoundMuteInfoAnimState.m_HoveredButton = HoveredNearestIndex;
	else if(InfoAlpha <= 0.01f)
		m_SoundMuteInfoAnimState.m_HoveredButton = -1;

	const int InfoIndex = HoveredNearestIndex >= 0 ? HoveredNearestIndex : m_SoundMuteInfoAnimState.m_HoveredButton;
	if(InfoIndex < 0 || InfoAlpha <= 0.01f)
		return;

	CUIRect Anchor = aAnimatedRects[InfoIndex];
	if(Anchor.w <= 0.0f || Anchor.h <= 0.0f)
		Anchor = {ColumnX, ColumnY + InfoIndex * (ButtonSize + Gap), ButtonSize, ButtonSize};

	const char *pTitle = gs_aSoundMuteButtonDefs[InfoIndex].m_pTitle;
	const char *pDescription = gs_aSoundMuteButtonDefs[InfoIndex].m_pDescription;
	const float TitleFontSize = 10.0f;
	const float BodyFontSize = 9.0f;
	const float Padding = 6.0f;
	const float InnerGap = 2.0f;
	const float InfoTextWidth = maximum(TextRender()->TextWidth(TitleFontSize, pTitle), TextRender()->TextWidth(BodyFontSize, pDescription));
	const float InfoWidth = std::clamp(InfoTextWidth + Padding * 2.0f, 170.0f, 360.0f);
	const float InfoHeight = Padding * 2.0f + TitleFontSize + InnerGap + BodyFontSize + 2.0f;

	CUIRect InfoRect = {Anchor.x + Anchor.w + 8.0f, Anchor.y + (Anchor.h - InfoHeight) * 0.5f, InfoWidth, InfoHeight};
	InfoRect.x += InfoOffsetX;
	const float ScreenMargin = 5.0f;
	if(InfoRect.x + InfoRect.w + ScreenMargin > pScreen->w)
		InfoRect.x = Anchor.x - InfoRect.w - 8.0f;
	InfoRect.x = std::clamp(InfoRect.x, ScreenMargin, pScreen->w - InfoRect.w - ScreenMargin);
	InfoRect.y = std::clamp(InfoRect.y, ScreenMargin, pScreen->h - InfoRect.h - ScreenMargin);

	InfoRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.75f * InfoAlpha * ContentAlpha), IGraphics::CORNER_ALL, 6.0f);

	CUIRect InfoContent = InfoRect;
	InfoContent.Margin(Padding, &InfoContent);
	CUIRect TitleRect, BodyRect;
	InfoContent.HSplitTop(TitleFontSize + 1.0f, &TitleRect, &BodyRect);
	BodyRect.HSplitTop(InnerGap, nullptr, &BodyRect);

	const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
	const ColorRGBA PrevOutlineColor = TextRender()->GetTextOutlineColor();
	TextRender()->TextColor(TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha * InfoAlpha));
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha * InfoAlpha));

	SLabelProperties TitleProps;
	TitleProps.m_MaxWidth = TitleRect.w;
	TitleProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&TitleRect, pTitle, TitleFontSize, TEXTALIGN_ML, TitleProps);

	SLabelProperties BodyProps;
	BodyProps.m_MaxWidth = BodyRect.w;
	BodyProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&BodyRect, pDescription, BodyFontSize, TEXTALIGN_ML, BodyProps);

	TextRender()->TextColor(PrevTextColor);
	TextRender()->TextOutlineColor(PrevOutlineColor);
}

void CScoreboard::RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, CScoreboardRenderState &State)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;
	const int NumPlayers = CountEnd - CountStart;
	const bool LowScoreboardWidth = Scoreboard.w < 350.0f;
	const bool ShowPoints = g_Config.m_ClScoreboardPoints != 0;
	const float ContentAlpha = m_AnimContentAlpha;
	const ColorRGBA BaseTextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha);
	const ColorRGBA BaseOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha);
	TextRender()->TextColor(BaseTextColor);
	TextRender()->TextOutlineColor(BaseOutlineColor);

	bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;

	// calculate measurements
	float LineHeight;
	float TeeSizeMod;
	float Spacing;
	float RoundRadius;
	float FontSize;
	if(NumPlayers <= 8)
	{
		LineHeight = 30.0f;
		TeeSizeMod = 0.5f;
		Spacing = 8.0f;
		RoundRadius = 5.0f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 12)
	{
		LineHeight = 25.0f;
		TeeSizeMod = 0.45f;
		Spacing = 2.5f;
		RoundRadius = 5.0f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 16)
	{
		LineHeight = 20.0f;
		TeeSizeMod = 0.4f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 24)
	{
		LineHeight = 13.5f;
		TeeSizeMod = 0.3f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 10.0f;
	}
	else if(NumPlayers <= 32)
	{
		LineHeight = 10.0f;
		TeeSizeMod = 0.2f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 8.0f;
	}
	else if(LowScoreboardWidth)
	{
		LineHeight = 7.5f;
		TeeSizeMod = 0.125f;
		Spacing = 0.0f;
		RoundRadius = 1.0f;
		FontSize = 7.0f;
	}
	else
	{
		LineHeight = 5.0f;
		TeeSizeMod = 0.1f;
		Spacing = 0.0f;
		RoundRadius = 1.0f;
		FontSize = 5.0f;
	}

	const float ScoreOffset = Scoreboard.x + 20.0f;
	const float ScoreLength = TextRender()->TextWidth(FontSize, TimeScore ? "00:00:00" : "99999");
	// Points column: placed between Score and Tee (only when enabled)
	const float PointsLength = ShowPoints ? (LowScoreboardWidth ? TextRender()->TextWidth(FontSize, "99999") : TextRender()->TextWidth(FontSize, "999999")) : 0.0f;
	const float PointsOffset = ScoreOffset + ScoreLength + 10.0f;
	const float TeeOffset = ShowPoints ? (PointsOffset + PointsLength + 10.0f) : (ScoreOffset + ScoreLength + 10.0f);
	const float TeeLength = 60.0f * TeeSizeMod;
	const float NameOffset = TeeOffset + TeeLength;
	const float NameLength = (LowScoreboardWidth ? 90.0f : 150.0f) - TeeLength;
	const float CountryLength = (LineHeight - Spacing - TeeSizeMod * 5.0f) * 2.0f;
	const float PingLength = 27.5f;
	const float PingOffset = Scoreboard.x + Scoreboard.w - PingLength - 10.0f;
	const float CountryOffset = PingOffset - CountryLength;
	const float ClanOffset = NameOffset + NameLength + 2.5f;
	const float ClanLength = CountryOffset - ClanOffset - 2.5f;

	// render headlines
	const float HeadlineFontsize = 11.0f;
	CUIRect Headline;
	Scoreboard.HSplitTop(HeadlineFontsize * 2.0f, &Headline, &Scoreboard);
	const float HeadlineY = Headline.y + Headline.h / 2.0f - HeadlineFontsize / 2.0f;
	const char *pScore = TimeScore ? Localize("Time") : Localize("Score");
	TextRender()->Text(ScoreOffset + ScoreLength - TextRender()->TextWidth(HeadlineFontsize, pScore), HeadlineY, HeadlineFontsize, pScore);
	// Points column header: only render when enabled
	if(ShowPoints)
	{
		const char *pPointsLabel = Localize("Points");
		TextRender()->Text(PointsOffset + PointsLength - TextRender()->TextWidth(HeadlineFontsize, pPointsLabel), HeadlineY, HeadlineFontsize, pPointsLabel);
	}
	TextRender()->Text(NameOffset, HeadlineY, HeadlineFontsize, Localize("Name"));
	const char *pClanLabel = Localize("Clan");
	TextRender()->Text(ClanOffset + (ClanLength - TextRender()->TextWidth(HeadlineFontsize, pClanLabel)) / 2.0f, HeadlineY, HeadlineFontsize, pClanLabel);
	const char *pPingLabel = Localize("Ping");
	TextRender()->Text(PingOffset + PingLength - TextRender()->TextWidth(HeadlineFontsize, pPingLabel), HeadlineY, HeadlineFontsize, pPingLabel);

	// render player entries
	int CountRendered = 0;
	int PrevDDTeam = -1;
	int &CurrentDDTeamSize = State.m_CurrentDDTeamSize;

	char aBuf[64];
	int MaxTeamSize = Config()->m_SvMaxTeamSize;

	for(int RenderDead = 0; RenderDead < 2; RenderDead++)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			// make sure that we render the correct team
			const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apInfoByDDTeamScore[i];
			if(!pInfo || pInfo->m_Team != Team)
				continue;

			if(CountRendered++ < CountStart)
				continue;

			int DDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
			int NextDDTeam = 0;
			bool IsDead = Client()->m_TranslationContext.m_aClients[pInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_DEAD;
			if(!RenderDead && IsDead)
				continue;
			if(RenderDead && !IsDead)
				continue;

			const float ItemAlpha = (RenderDead ? 0.5f : 1.0f) * ContentAlpha;
			ColorRGBA TextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ItemAlpha);
			TextRender()->TextColor(TextColor);

			for(int j = i + 1; j < MAX_CLIENTS; j++)
			{
				const CNetObj_PlayerInfo *pInfoNext = GameClient()->m_Snap.m_apInfoByDDTeamScore[j];
				if(!pInfoNext || pInfoNext->m_Team != Team)
					continue;

				NextDDTeam = GameClient()->m_Teams.Team(pInfoNext->m_ClientId);
				break;
			}

			if(PrevDDTeam == -1)
			{
				for(int j = i - 1; j >= 0; j--)
				{
					const CNetObj_PlayerInfo *pInfoPrev = GameClient()->m_Snap.m_apInfoByDDTeamScore[j];
					if(!pInfoPrev || pInfoPrev->m_Team != Team)
						continue;

					PrevDDTeam = GameClient()->m_Teams.Team(pInfoPrev->m_ClientId);
					break;
				}
			}

			CUIRect RowAndSpacing, Row;
			Scoreboard.HSplitTop(LineHeight + Spacing, &RowAndSpacing, &Scoreboard);
			RowAndSpacing.HSplitTop(LineHeight, &Row, nullptr);

			// team background
			if(DDTeam != TEAM_FLOCK)
			{
				const ColorRGBA Color = GameClient()->GetDDTeamColor(DDTeam).WithAlpha(0.5f * ItemAlpha);
				int TeamRectCorners = 0;
				if(PrevDDTeam != DDTeam)
				{
					TeamRectCorners |= IGraphics::CORNER_T;
					State.m_TeamStartX = Row.x;
					State.m_TeamStartY = Row.y;
				}
				if(NextDDTeam != DDTeam)
					TeamRectCorners |= IGraphics::CORNER_B;
				RowAndSpacing.Draw(Color, TeamRectCorners, RoundRadius);

				CurrentDDTeamSize++;

				if(NextDDTeam != DDTeam)
				{
					const float TeamFontSize = FontSize / 1.5f;

					if(NumPlayers > 8)
					{
						if(DDTeam == TEAM_SUPER)
							str_copy(aBuf, Localize("Super"));
						else if(CurrentDDTeamSize <= 1)
							str_format(aBuf, sizeof(aBuf), "%d", DDTeam);
						else
						 str_format(aBuf, sizeof(aBuf), Localize("%d\n(%d/%d)", "Team and size"), DDTeam, CurrentDDTeamSize, MaxTeamSize);
						TextRender()->Text(State.m_TeamStartX, maximum(State.m_TeamStartY + Row.h / 2.0f - TeamFontSize, State.m_TeamStartY + 1.5f /* padding top */), TeamFontSize, aBuf);
					}
					else
					{
						if(DDTeam == TEAM_SUPER)
							str_copy(aBuf, Localize("Super"));
						else if(CurrentDDTeamSize > 1)
							str_format(aBuf, sizeof(aBuf), Localize("Team %d (%d/%d)"), DDTeam, CurrentDDTeamSize, MaxTeamSize);
						else
							str_format(aBuf, sizeof(aBuf), Localize("Team %d"), DDTeam);
						TextRender()->Text(Row.x + Row.w / 2.0f - TextRender()->TextWidth(TeamFontSize, aBuf) / 2.0f + 5.0f, Row.y + Row.h, TeamFontSize, aBuf);
					}

					CurrentDDTeamSize = 0;
				}
			}
			PrevDDTeam = DDTeam;

			// background so it's easy to find the local player or the followed one in spectator mode
			if((!GameClient()->m_Snap.m_SpecInfo.m_Active && pInfo->m_Local) ||
				(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW && pInfo->m_Local) ||
				(GameClient()->m_Snap.m_SpecInfo.m_Active && pInfo->m_ClientId == GameClient()->m_Snap.m_SpecInfo.m_SpectatorId))
			{
				Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ItemAlpha), IGraphics::CORNER_ALL, RoundRadius);
			}

			const int ClientId = pInfo->m_ClientId;
			const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];

			if(m_MouseUnlocked)
			{
				const int ButtonResult = Ui()->DoButtonLogic(&ClientData, 0, &Row, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
				if(ButtonResult != 0)
				{
					m_ScoreboardPopupContext.m_pScoreboard = this;
					m_ScoreboardPopupContext.m_ClientId = ClientId;
					m_ScoreboardPopupContext.m_IsLocal = GameClient()->m_aLocalIds[0] == ClientId ||
									     (Client()->DummyConnected() && GameClient()->m_aLocalIds[1] == ClientId);

					Ui()->DoPopupMenu(&m_ScoreboardPopupContext, Ui()->MouseX(), Ui()->MouseY(), 110.0f,
						m_ScoreboardPopupContext.m_IsLocal ? 58.5f : 87.5f, &m_ScoreboardPopupContext, PopupScoreboard);
				}

				if(Ui()->HotItem() == &ClientData)
				{
					Row.Draw(ColorRGBA(0.7f, 0.7f, 0.7f, 0.7f * ItemAlpha), IGraphics::CORNER_ALL, RoundRadius);
				}
			}

			// score
			if(Race7)
			{
				if(pInfo->m_Score == -1)
				{
					aBuf[0] = '\0';
				}
				else
				{
					// 0.7 uses milliseconds and ddnets str_time wants centiseconds
					// 0.7 servers can also send the amount of precision the client should use
					// we ignore that and always show 3 digit precision
					str_time((int64_t)absolute(pInfo->m_Score / 10), TIME_MINS_CENTISECS, aBuf, sizeof(aBuf));
				}
			}
			else if(TimeScore)
			{
				if(pInfo->m_Score == -9999)
				{
					aBuf[0] = '\0';
				}
				else
				{
					str_time((int64_t)absolute(pInfo->m_Score) * 100, TIME_HOURS, aBuf, sizeof(aBuf));
				}
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "%d", std::clamp(pInfo->m_Score, -999, 99999));
			}
			TextRender()->Text(ScoreOffset + ScoreLength - TextRender()->TextWidth(FontSize, aBuf), Row.y + (Row.h - FontSize) / 2.0f, FontSize, aBuf);
			const bool HideIdentity = GameClient()->ShouldHideStreamerIdentity(ClientId);
			char aNameBuf[MAX_NAME_LENGTH];
			char aClanBuf[MAX_CLAN_LENGTH];
			GameClient()->FormatStreamerName(ClientId, aNameBuf, sizeof(aNameBuf));
			GameClient()->FormatStreamerClan(ClientId, aClanBuf, sizeof(aClanBuf));
			const bool IsFriend = ClientData.m_Friend;
			bool IsSameClan = false;
			if(aClanBuf[0] != '\0')
			{
				const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
				if(LocalClientId >= 0 && str_comp(aClanBuf, GameClient()->m_aClients[LocalClientId].m_aClan) == 0)
				{
					IsSameClan = true;
				}
			}
			// Points column: render actual points value, right-aligned (only when enabled)
			if(ShowPoints)
			{
				char aPointsValue[16];
				SPlayerPointsResult PointsResult = GameClient()->m_PlayerPoints.GetPoints(ClientData.m_aName);
				if(PointsResult.m_Status == EPointsStatus::READY)
				{
					str_format(aPointsValue, sizeof(aPointsValue), "%d", PointsResult.m_Points);
				}
				else if(PointsResult.m_Status == EPointsStatus::FETCHING || PointsResult.m_Status == EPointsStatus::NOT_REQUESTED)
				{
					str_copy(aPointsValue, "...");
				}
				else // FAILED
				{
					str_copy(aPointsValue, "?");
				}
				TextRender()->Text(PointsOffset + PointsLength - TextRender()->TextWidth(FontSize, aPointsValue), Row.y + (Row.h - FontSize) / 2.0f, FontSize, aPointsValue);
			}

			// CTF flag
			if(pGameInfoObj && (pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) &&
				pGameDataObj && (pGameDataObj->m_FlagCarrierRed == pInfo->m_ClientId || pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId))
			{
				Graphics()->BlendNormal();
				Graphics()->TextureSet(pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId ? GameClient()->m_GameSkin.m_SpriteFlagBlue : GameClient()->m_GameSkin.m_SpriteFlagRed);
				Graphics()->QuadsBegin();
				Graphics()->QuadsSetSubset(1.0f, 0.0f, 0.0f, 1.0f);
				IGraphics::CQuadItem QuadItem(TeeOffset, Row.y - 2.5f - Spacing / 2.0f, Row.h / 2.0f, Row.h);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
			}

			// skin
			if(RenderDead)
			{
				Graphics()->BlendNormal();
				Graphics()->TextureSet(m_DeadTeeTexture);
				Graphics()->QuadsBegin();
				if(GameClient()->IsTeamPlay())
				{
					const ColorRGBA TeamColor = GameClient()->m_Skins7.GetTeamColor(true, 0, GameClient()->m_aClients[pInfo->m_ClientId].m_Team, protocol7::SKINPART_BODY).WithMultipliedAlpha(ItemAlpha);
					Graphics()->SetColor(TeamColor);
				}
				else
				{
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, ItemAlpha);
				}
				CTeeRenderInfo TeeInfo = GameClient()->m_aClients[pInfo->m_ClientId].m_RenderInfo;
				TeeInfo.m_Size *= TeeSizeMod;
				IGraphics::CQuadItem QuadItem(TeeOffset, Row.y, TeeInfo.m_Size, TeeInfo.m_Size);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
			else
			{
				CTeeRenderInfo TeeInfo = ClientData.m_RenderInfo;
				TeeInfo.m_Size *= TeeSizeMod;
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
				const vec2 TeeRenderPos = vec2(TeeOffset + TeeLength / 2, Row.y + Row.h / 2.0f + OffsetToMid.y);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos, ItemAlpha);
			}

			// name
			{
				CTextCursor Cursor;
				Cursor.SetPosition(vec2(NameOffset, Row.y + (Row.h - FontSize) / 2.0f));
				Cursor.m_FontSize = FontSize;
				Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
				Cursor.m_LineWidth = NameLength;
				ColorRGBA NameColor = TextColor;
				if(ClientData.m_AuthLevel)
				{
					NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClAuthedPlayerColor)).WithMultipliedAlpha(ItemAlpha);
				}
				else if(IsFriend)
				{
					NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor)).WithMultipliedAlpha(ItemAlpha);
				}
				else if(IsSameClan)
				{
					NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor)).WithMultipliedAlpha(ItemAlpha);
				}
				TextRender()->TextColor(NameColor);
				if(g_Config.m_ClShowIds && !HideIdentity)
				{
					char aClientId[16];
					GameClient()->FormatClientId(ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
					TextRender()->TextEx(&Cursor, aClientId);
				}

				if(ClientId >= 0 && (GameClient()->m_aClients[ClientId].m_Foe || GameClient()->m_aClients[ClientId].m_ChatIgnore))
				{
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->TextEx(&Cursor, FontIcons::FONT_ICON_COMMENT_SLASH);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
				}

				if(!HideIdentity && g_Config.m_QmClientShowBadge && ClientId >= 0 && GameClient()->GetQ1menGClientQid(ClientId)[0] != '\0')
				{
					TextRender()->TextColor(ColorRGBA(0.38f, 0.89f, 1.0f, ItemAlpha));
					TextRender()->TextEx(&Cursor, "Qm ");
					TextRender()->TextColor(NameColor);
				}

				// TClient
				if(!HideIdentity && ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListScoreboard && GameClient()->m_WarList.GetAnyWar(ClientId))
					TextRender()->TextColor(GameClient()->m_WarList.GetNameplateColor(ClientId).WithMultipliedAlpha(ItemAlpha));

				TextRender()->TextEx(&Cursor, aNameBuf);

				// ready / watching
				if(Client()->IsSixup() && Client()->m_TranslationContext.m_aClients[pInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_READY)
				{
					TextRender()->TextColor(0.1f, 1.0f, 0.1f, TextColor.a);
					TextRender()->TextEx(&Cursor, "✓");
				}
			}

			// clan
			{
				const char *pClanName = aClanBuf;
				if(pClanName[0] != '\0' && GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0 && str_comp(pClanName, GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_aClan) == 0)
				{
					TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor)).WithMultipliedAlpha(ItemAlpha));
				}
				else
				{
					TextRender()->TextColor(TextColor);
				}

				// TClient
				if(!HideIdentity && ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListScoreboard && GameClient()->m_WarList.GetAnyWar(ClientId))
					TextRender()->TextColor(GameClient()->m_WarList.GetClanColor(ClientId).WithMultipliedAlpha(ItemAlpha));

				CTextCursor Cursor;
				Cursor.SetPosition(vec2(ClanOffset + (ClanLength - minimum(TextRender()->TextWidth(FontSize, pClanName), ClanLength)) / 2.0f, Row.y + (Row.h - FontSize) / 2.0f));
				Cursor.m_FontSize = FontSize;
				Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
				Cursor.m_LineWidth = ClanLength;
				TextRender()->TextEx(&Cursor, pClanName);
			}

			// country flag
			const int CountryCode = g_Config.m_QmStreamerScoreboardDefaultFlags ? -1 : ClientData.m_Country;
			GameClient()->m_CountryFlags.Render(CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * ItemAlpha),
				CountryOffset, Row.y + (Spacing + TeeSizeMod * 5.0f) / 2.0f, CountryLength, Row.h - Spacing - TeeSizeMod * 5.0f);

			// ping
			if(g_Config.m_ClEnablePingColor)
			{
				TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA((300.0f - std::clamp(pInfo->m_Latency, 0, 300)) / 1000.0f, 1.0f, 0.5f)).WithMultipliedAlpha(ItemAlpha));
			}
			else
			{
				TextRender()->TextColor(TextRender()->DefaultTextColor().WithMultipliedAlpha(ItemAlpha));
			}
			str_format(aBuf, sizeof(aBuf), "%d", std::clamp(pInfo->m_Latency, 0, 999));
			TextRender()->Text(PingOffset + PingLength - TextRender()->TextWidth(FontSize, aBuf), Row.y + (Row.h - FontSize) / 2.0f, FontSize, aBuf);
			TextRender()->TextColor(TextRender()->DefaultTextColor().WithMultipliedAlpha(ItemAlpha));

			if(CountRendered == CountEnd)
				break;
		}
	}

	TextRender()->TextColor(BaseTextColor);
	TextRender()->TextOutlineColor(BaseOutlineColor);
}

void CScoreboard::RenderRecordingNotification(float x)
{
	const float ContentAlpha = m_AnimContentAlpha;
	char aBuf[512] = "";

	const auto &&AppendRecorderInfo = [&](int Recorder, const char *pName) {
		if(GameClient()->DemoRecorder(Recorder)->IsRecording())
		{
			char aTime[32];
			str_time((int64_t)GameClient()->DemoRecorder(Recorder)->Length() * 100, TIME_HOURS, aTime, sizeof(aTime));
			str_append(aBuf, pName);
			str_append(aBuf, " ");
		 str_append(aBuf, aTime);
			str_append(aBuf, "  ");
		}
	};

	AppendRecorderInfo(RECORDER_MANUAL, Localize("Manual"));
	AppendRecorderInfo(RECORDER_RACE, Localize("Race"));
	AppendRecorderInfo(RECORDER_AUTO, Localize("Auto"));
	AppendRecorderInfo(RECORDER_REPLAYS, Localize("Replay"));

	if(aBuf[0] == '\0')
		return;

	const float FontSize = 10.0f;

	CUIRect Rect = {x, 0.0f, TextRender()->TextWidth(FontSize, aBuf) + 30.0f, 25.0f};
	Rect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f * ContentAlpha), IGraphics::CORNER_B, 7.5f);
	CUIRect Circle;
	CUIRect TextRect;
	{
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle RowStyle;
		RowStyle.m_Axis = EUiAxis::ROW;
		RowStyle.m_Gap = 5.0f;
		RowStyle.m_Padding.m_Left = 10.0f;
		RowStyle.m_Padding.m_Right = 5.0f;
		RowStyle.m_AlignItems = EUiAlign::STRETCH;
		RowStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vChildren;
		std::vector<SUiLayoutChild> &vChildren = s_vChildren;
		vChildren.assign(2, SUiLayoutChild{});
		vChildren[0].m_Style.m_Width = SUiLength::Px(10.0f);
		vChildren[1].m_Style.m_Width = SUiLength::Flex(1.0f);
		LayoutEngine.ComputeChildren(RowStyle, CUiV2LegacyAdapter::FromCUIRect(Rect), vChildren);
		Circle = CUiV2LegacyAdapter::ToCUIRect(vChildren[0].m_Box);
		TextRect = CUiV2LegacyAdapter::ToCUIRect(vChildren[1].m_Box);
	}
	Circle.HMargin((Circle.h - Circle.w) / 2.0f, &Circle);
	Circle.Draw(ColorRGBA(1.0f, 0.0f, 0.0f, ContentAlpha), IGraphics::CORNER_ALL, Circle.h / 2.0f);

	Ui()->DoLabel(&TextRect, aBuf, FontSize, TEXTALIGN_ML);
}

void CScoreboard::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;



	// 当记分板可见时（骗你的,不可见也查），为所有活跃玩家触发查询点
	if(g_Config.m_ClScoreboardPoints || g_Config.m_ClScoreboardSortMode)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameClient()->m_Snap.m_apPlayerInfos[i] && GameClient()->m_aClients[i].m_Active)
			{
				GameClient()->m_PlayerPoints.EnsureQueried(GameClient()->m_aClients[i].m_aName);
			}
		}
	}

	// If scoreboard was opened by death/pause auto activation, ensure cursor locks back when it closes.
	if(!IsActive() && m_MouseUnlocked)
	{
		Ui()->ClosePopupMenus();
		m_MouseUnlocked = false;
		if(m_LastMousePos.has_value())
			SetUiMousePos(m_LastMousePos.value());
		m_LastMousePos = Ui()->MousePos();
	}

	const bool WantActive = IsActive();
	if(!WantActive)
	{
		m_OpenTime = 0.0f;
		m_Visibility = 0.0f;
		m_AnimContentAlpha = 0.0f;
		return;
	}

	m_OpenTime = 1.0f;
	m_Visibility = 1.0f;
	m_AnimContentAlpha = 1.0f;

	if(!GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive())
	{
		Ui()->StartCheck();
		Ui()->Update();
	}

	// 如果记分板处于活动状态，则应同时清除每日公告消息。
	if(GameClient()->m_Motd.IsActive())
		GameClient()->m_Motd.Clear();

	const CUIRect Screen = *Ui()->Screen();
	Ui()->MapScreen();

	const float BackgroundAlphaFinal = 1.0f;
	const float ContentOffset = 0.0f;

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool Teams = GameClient()->IsTeamPlay();
	const auto &aTeamSize = GameClient()->m_Snap.m_aTeamSize;
	const int NumPlayers = Teams ? maximum(aTeamSize[TEAM_RED], aTeamSize[TEAM_BLUE]) : aTeamSize[TEAM_RED];
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;

	// Scoreboard width: clamp to screen width for narrow aspect ratios
	const float ScreenMargin = 10.0f;
	const float MaxScoreboardWidth = maximum(200.0f, Screen.w - ScreenMargin);
	const float BaseScoreboardSmallWidth = g_Config.m_ClScoreboardPoints ? (450.0f + 10.0f) : 450.0f;
	const float ScoreboardSmallWidth = minimum(BaseScoreboardSmallWidth, MaxScoreboardWidth);
	const float BaseScoreboardWidth = !Teams && NumPlayers <= 16 ? ScoreboardSmallWidth : 850.0f;
	const float ScoreboardWidth = minimum(BaseScoreboardWidth, MaxScoreboardWidth);
	const float TitleHeight = 30.0f;

	CUIRect Scoreboard = {(Screen.w - ScoreboardWidth) / 2.0f, 75.0f, ScoreboardWidth, 355.0f + TitleHeight};
	CUIRect ScoreboardContent = Scoreboard;
	ScoreboardContent.y += ContentOffset;
	CScoreboardRenderState RenderState{};
	static CButtonContainer s_ScoreboardSortButton;
	const float SortButtonFontSize = 10.0f;
	const char *pSortLabel = nullptr;
	if(g_Config.m_ClScoreboardSortMode)
		pSortLabel = Localize("Current: Ranks");
	else
		pSortLabel = TimeScore ? Localize("Current: Time") : Localize("Current: Score");
	const float SortButtonWidth = TextRender()->TextWidth(SortButtonFontSize, pSortLabel) + 18.0f;
	const ColorRGBA SortButtonBaseColor = g_Config.m_ClScoreboardSortMode ? ColorRGBA(0.25f, 0.55f, 0.8f, 0.6f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f);
	const ColorRGBA SortButtonColor = SortButtonBaseColor.WithMultipliedAlpha(m_AnimContentAlpha);
	auto &&DoSortButton = [&](CUIRect Rect) {
		Rect.VMargin(4.0f, &Rect);
		Rect.HMargin(6.0f, &Rect);
		if(Ui()->DoButton_PopupMenu(&s_ScoreboardSortButton, pSortLabel, &Rect, SortButtonFontSize, TEXTALIGN_MC, 0.0f, false, true, SortButtonColor))
			g_Config.m_ClScoreboardSortMode ^= 1;
	};

	const ColorRGBA PrevTextColor = TextRender()->GetTextColor();
	const ColorRGBA PrevTextOutlineColor = TextRender()->GetTextOutlineColor();
	const ColorRGBA BaseTextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(m_AnimContentAlpha);
	const ColorRGBA BaseTextOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(m_AnimContentAlpha);
	TextRender()->TextColor(BaseTextColor);
	TextRender()->TextOutlineColor(BaseTextOutlineColor);

	if(Teams)
	{
		const char *pRedTeamName = GetTeamName(TEAM_RED);
		const char *pBlueTeamName = GetTeamName(TEAM_BLUE);

		// Game over title
		const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
		if((pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && pGameDataObj)
		{
			char aTitle[256];
			if(pGameDataObj->m_TeamscoreRed > pGameDataObj->m_TeamscoreBlue)
			{
				TextRender()->TextColor(ColorRGBA(0.975f, 0.17f, 0.17f, 1.0f).WithMultipliedAlpha(m_AnimContentAlpha));
				if(pRedTeamName == nullptr)
				{
					str_copy(aTitle, Localize("Red team wins!"));
				}
				else
				{
					str_format(aTitle, sizeof(aTitle), Localize("%s wins!"), pRedTeamName);
				}
			}
			else if(pGameDataObj->m_TeamscoreBlue > pGameDataObj->m_TeamscoreRed)
			{
				TextRender()->TextColor(ColorRGBA(0.17f, 0.46f, 0.975f, 1.0f).WithMultipliedAlpha(m_AnimContentAlpha));
				if(pBlueTeamName == nullptr)
				{
					str_copy(aTitle, Localize("Blue team wins!"));
				}
				else
				{
					str_format(aTitle, sizeof(aTitle), Localize("%s wins!"), pBlueTeamName);
				}
			}
			else
			{
				TextRender()->TextColor(ColorRGBA(0.91f, 0.78f, 0.33f, 1.0f).WithMultipliedAlpha(m_AnimContentAlpha));
				str_copy(aTitle, Localize("Draw!"));
			}

			const float TitleFontSize = 36.0f;
			CUIRect GameOverTitle = {Scoreboard.x, Scoreboard.y - TitleFontSize - 6.0f + ContentOffset, Scoreboard.w, TitleFontSize};
			Ui()->DoLabel(&GameOverTitle, aTitle, TitleFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(BaseTextColor);
		}

		CUIRect RedScoreboard, BlueScoreboard, RedTitle, BlueTitle;
		CUIRect RedScoreboardContent, BlueScoreboardContent, RedTitleContent, BlueTitleContent;
		{
			CUiV2LayoutEngine LayoutEngine;
			SUiStyle TwoColumnStyle;
			TwoColumnStyle.m_Axis = EUiAxis::ROW;
			TwoColumnStyle.m_Gap = 7.5f;
			TwoColumnStyle.m_AlignItems = EUiAlign::STRETCH;
			TwoColumnStyle.m_JustifyContent = EUiAlign::START;
			static thread_local std::vector<SUiLayoutChild> s_vColumns;
			std::vector<SUiLayoutChild> &vColumns = s_vColumns;
			vColumns.assign(2, SUiLayoutChild{});
			vColumns[0].m_Style.m_Width = SUiLength::Flex(1.0f);
			vColumns[1].m_Style.m_Width = SUiLength::Flex(1.0f);
			LayoutEngine.ComputeChildren(TwoColumnStyle, CUiV2LegacyAdapter::FromCUIRect(Scoreboard), vColumns);
			RedScoreboard = CUiV2LegacyAdapter::ToCUIRect(vColumns[0].m_Box);
			BlueScoreboard = CUiV2LegacyAdapter::ToCUIRect(vColumns[1].m_Box);
		}
		RedScoreboard.HSplitTop(TitleHeight, &RedTitle, &RedScoreboard);
		BlueScoreboard.HSplitTop(TitleHeight, &BlueTitle, &BlueScoreboard);
		{
			CUiV2LayoutEngine LayoutEngine;
			SUiStyle TwoColumnStyle;
			TwoColumnStyle.m_Axis = EUiAxis::ROW;
			TwoColumnStyle.m_Gap = 7.5f;
			TwoColumnStyle.m_AlignItems = EUiAlign::STRETCH;
			TwoColumnStyle.m_JustifyContent = EUiAlign::START;
			static thread_local std::vector<SUiLayoutChild> s_vColumns;
			std::vector<SUiLayoutChild> &vColumns = s_vColumns;
			vColumns.assign(2, SUiLayoutChild{});
			vColumns[0].m_Style.m_Width = SUiLength::Flex(1.0f);
			vColumns[1].m_Style.m_Width = SUiLength::Flex(1.0f);
			LayoutEngine.ComputeChildren(TwoColumnStyle, CUiV2LegacyAdapter::FromCUIRect(ScoreboardContent), vColumns);
			RedScoreboardContent = CUiV2LegacyAdapter::ToCUIRect(vColumns[0].m_Box);
			BlueScoreboardContent = CUiV2LegacyAdapter::ToCUIRect(vColumns[1].m_Box);
		}
		RedScoreboardContent.HSplitTop(TitleHeight, &RedTitleContent, &RedScoreboardContent);
		BlueScoreboardContent.HSplitTop(TitleHeight, &BlueTitleContent, &BlueScoreboardContent);
		CUIRect SortButton;
		const CUIRect BlueTitleBackground = BlueTitle;
		{
			CUiV2LayoutEngine LayoutEngine;
			SUiStyle TitleSplitStyle;
			TitleSplitStyle.m_Axis = EUiAxis::ROW;
			TitleSplitStyle.m_AlignItems = EUiAlign::STRETCH;
			TitleSplitStyle.m_JustifyContent = EUiAlign::START;
			static thread_local std::vector<SUiLayoutChild> s_vTitleChildren;
			std::vector<SUiLayoutChild> &vTitleChildren = s_vTitleChildren;
			vTitleChildren.assign(2, SUiLayoutChild{});
			vTitleChildren[0].m_Style.m_Width = SUiLength::Flex(1.0f);
			vTitleChildren[1].m_Style.m_Width = SUiLength::Px(SortButtonWidth);
			LayoutEngine.ComputeChildren(TitleSplitStyle, CUiV2LegacyAdapter::FromCUIRect(BlueTitleContent), vTitleChildren);
			BlueTitleContent = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[0].m_Box);
			SortButton = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[1].m_Box);
		}

		RedTitle.Draw(ColorRGBA(0.975f, 0.17f, 0.17f, 0.5f * BackgroundAlphaFinal), IGraphics::CORNER_T, 7.5f);
		BlueTitleBackground.Draw(ColorRGBA(0.17f, 0.46f, 0.975f, 0.5f * BackgroundAlphaFinal), IGraphics::CORNER_T, 7.5f);
		RedScoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * BackgroundAlphaFinal), IGraphics::CORNER_B, 7.5f);
		BlueScoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * BackgroundAlphaFinal), IGraphics::CORNER_B, 7.5f);

		RenderTitle(RedTitleContent, TEAM_RED, pRedTeamName == nullptr ? Localize("Red team") : pRedTeamName);
		RenderTitle(BlueTitleContent, TEAM_BLUE, pBlueTeamName == nullptr ? Localize("Blue team") : pBlueTeamName);
		DoSortButton(SortButton);
		RenderScoreboard(RedScoreboardContent, TEAM_RED, 0, NumPlayers, RenderState);
		RenderScoreboard(BlueScoreboardContent, TEAM_BLUE, 0, NumPlayers, RenderState);
	}
	else
	{
		Scoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * BackgroundAlphaFinal), IGraphics::CORNER_ALL, 7.5f);

		const char *pTitle;
		if(pGameInfoObj && (pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		{
			pTitle = Localize("Game over");
		}
		else
		{
			pTitle = Client()->GetCurrentMap();
		}

		CUIRect Title;
		CUIRect ScoreboardContentBody = ScoreboardContent;
		ScoreboardContentBody.HSplitTop(TitleHeight, &Title, &ScoreboardContentBody);
		CUIRect SortButton;
		{
			CUiV2LayoutEngine LayoutEngine;
			SUiStyle TitleSplitStyle;
			TitleSplitStyle.m_Axis = EUiAxis::ROW;
			TitleSplitStyle.m_AlignItems = EUiAlign::STRETCH;
			TitleSplitStyle.m_JustifyContent = EUiAlign::START;
			static thread_local std::vector<SUiLayoutChild> s_vTitleChildren;
			std::vector<SUiLayoutChild> &vTitleChildren = s_vTitleChildren;
			vTitleChildren.assign(2, SUiLayoutChild{});
			vTitleChildren[0].m_Style.m_Width = SUiLength::Flex(1.0f);
			vTitleChildren[1].m_Style.m_Width = SUiLength::Px(SortButtonWidth);
			LayoutEngine.ComputeChildren(TitleSplitStyle, CUiV2LegacyAdapter::FromCUIRect(Title), vTitleChildren);
			Title = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[0].m_Box);
			SortButton = CUiV2LegacyAdapter::ToCUIRect(vTitleChildren[1].m_Box);
		}
		RenderTitle(Title, TEAM_GAME, pTitle);
		DoSortButton(SortButton);

		if(NumPlayers <= 16)
		{
			RenderScoreboard(ScoreboardContentBody, TEAM_GAME, 0, NumPlayers, RenderState);
		}
		else if(NumPlayers <= 64)
		{
			int PlayersPerSide;
			if(NumPlayers <= 24)
				PlayersPerSide = 12;
			else if(NumPlayers <= 32)
				PlayersPerSide = 16;
			else if(NumPlayers <= 48)
				PlayersPerSide = 24;
			else
				PlayersPerSide = 32;

			CUIRect LeftScoreboard, RightScoreboard;
			{
				CUiV2LayoutEngine LayoutEngine;
				SUiStyle TwoColumnStyle;
				TwoColumnStyle.m_Axis = EUiAxis::ROW;
				TwoColumnStyle.m_AlignItems = EUiAlign::STRETCH;
				TwoColumnStyle.m_JustifyContent = EUiAlign::START;
				static thread_local std::vector<SUiLayoutChild> s_vColumns;
				std::vector<SUiLayoutChild> &vColumns = s_vColumns;
				vColumns.assign(2, SUiLayoutChild{});
				vColumns[0].m_Style.m_Width = SUiLength::Flex(1.0f);
				vColumns[1].m_Style.m_Width = SUiLength::Flex(1.0f);
				LayoutEngine.ComputeChildren(TwoColumnStyle, CUiV2LegacyAdapter::FromCUIRect(ScoreboardContentBody), vColumns);
				LeftScoreboard = CUiV2LegacyAdapter::ToCUIRect(vColumns[0].m_Box);
				RightScoreboard = CUiV2LegacyAdapter::ToCUIRect(vColumns[1].m_Box);
			}
			RenderScoreboard(LeftScoreboard, TEAM_GAME, 0, PlayersPerSide, RenderState);
			RenderScoreboard(RightScoreboard, TEAM_GAME, PlayersPerSide, 2 * PlayersPerSide, RenderState);
		}
		else
		{
			const int NumColumns = 3;
			const int PlayersPerColumn = std::ceil(128.0f / NumColumns);
			{
				CUiV2LayoutEngine LayoutEngine;
				SUiStyle ColumnStyle;
				ColumnStyle.m_Axis = EUiAxis::ROW;
				ColumnStyle.m_AlignItems = EUiAlign::STRETCH;
				ColumnStyle.m_JustifyContent = EUiAlign::START;
				static thread_local std::vector<SUiLayoutChild> s_vColumns;
				std::vector<SUiLayoutChild> &vColumns = s_vColumns;
				vColumns.assign(NumColumns, SUiLayoutChild{});
				for(SUiLayoutChild &Child : vColumns)
				{
					Child.m_Style.m_Width = SUiLength::Flex(1.0f);
				}
				LayoutEngine.ComputeChildren(ColumnStyle, CUiV2LegacyAdapter::FromCUIRect(ScoreboardContentBody), vColumns);
				for(int i = 0; i < NumColumns; ++i)
				{
					CUIRect Column = CUiV2LegacyAdapter::ToCUIRect(vColumns[i].m_Box);
					RenderScoreboard(Column, TEAM_GAME, i * PlayersPerColumn, (i + 1) * PlayersPerColumn, RenderState);
				}
			}
		}
	}

	RenderSoundMuteBar(ScoreboardContent);

	CUIRect Spectators = {(Screen.w - ScoreboardSmallWidth) / 2.0f, ScoreboardContent.y + ScoreboardContent.h + 5.0f, ScoreboardSmallWidth, 100.0f};
	if(pGameInfoObj && (pGameInfoObj->m_ScoreLimit || pGameInfoObj->m_TimeLimit || (pGameInfoObj->m_RoundNum && pGameInfoObj->m_RoundCurrent)))
	{
		CUIRect Goals;
		CUIRect SpectatorRest;
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle ColumnStyle;
		ColumnStyle.m_Axis = EUiAxis::COLUMN;
		ColumnStyle.m_Gap = 5.0f;
		ColumnStyle.m_AlignItems = EUiAlign::STRETCH;
		ColumnStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vChildren;
		std::vector<SUiLayoutChild> &vChildren = s_vChildren;
		vChildren.assign(2, SUiLayoutChild{});
		vChildren[0].m_Style.m_Height = SUiLength::Px(25.0f);
		vChildren[1].m_Style.m_Height = SUiLength::Flex(1.0f);
		LayoutEngine.ComputeChildren(ColumnStyle, CUiV2LegacyAdapter::FromCUIRect(Spectators), vChildren);
		Goals = CUiV2LegacyAdapter::ToCUIRect(vChildren[0].m_Box);
		SpectatorRest = CUiV2LegacyAdapter::ToCUIRect(vChildren[1].m_Box);
		Spectators = SpectatorRest;
		RenderGoals(Goals);
	}
	RenderSpectators(Spectators);

	if(!g_Config.m_ClShowhudTimer)
		RenderRecordingNotification((Screen.w / 7) * 4 + 10);

	if(!GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive())
	{
		Ui()->RenderPopupMenus();

		if(m_MouseUnlocked)
			RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);

		Ui()->FinishCheck();
	}

	TextRender()->TextColor(PrevTextColor);
	TextRender()->TextOutlineColor(PrevTextOutlineColor);
}

bool CScoreboard::IsActive() const
{
	// if statboard is active don't show scoreboard
	if(GameClient()->m_Statboard.IsActive())
		return false;

	if(m_Active)
		return true;

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	if(GameClient()->m_Snap.m_pLocalInfo && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		// we are not a spectator, check if we are dead and the game isn't paused
		if(!GameClient()->m_Snap.m_pLocalCharacter && g_Config.m_ClScoreboardOnDeath &&
			!(pGameInfoObj && pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED))
			return true;
	}

	// if the game is over
	if(pGameInfoObj && pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
		return true;

	return false;
}

const char *CScoreboard::GetTeamName(int Team) const
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	int ClanPlayers = 0;
	const char *pClanName = nullptr;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByScore)
	{
		if(!pInfo || pInfo->m_Team != Team)
			continue;

		if(GameClient()->ShouldHideStreamerIdentity(pInfo->m_ClientId))
			return nullptr;

		if(!pClanName)
		{
			pClanName = GameClient()->m_aClients[pInfo->m_ClientId].m_aClan;
			ClanPlayers++;
		}
		else
		{
			if(str_comp(GameClient()->m_aClients[pInfo->m_ClientId].m_aClan, pClanName) == 0)
				ClanPlayers++;
			else
				return nullptr;
		}
	}

	if(ClanPlayers > 1 && pClanName[0] != '\0')
		return pClanName;
	else
		return nullptr;
}

CUi::EPopupMenuFunctionResult CScoreboard::PopupScoreboard(void *pContext, CUIRect View, bool Active)
{
	CScoreboardPopupContext *pPopupContext = static_cast<CScoreboardPopupContext *>(pContext);
	CScoreboard *pScoreboard = pPopupContext->m_pScoreboard;
	CUi *pUi = pPopupContext->m_pScoreboard->Ui();

	CGameClient::CClientData &Client = pScoreboard->GameClient()->m_aClients[pPopupContext->m_ClientId];

	if(!Client.m_Active)
		return CUi::POPUP_CLOSE_CURRENT;

	const float Margin = 5.0f;
	View.Margin(Margin, &View);

	CUIRect Label, Container, Action;
	const float ItemSpacing = 2.0f;
	const float FontSize = 12.0f;

	{
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle HeaderLayoutStyle;
		HeaderLayoutStyle.m_Axis = EUiAxis::COLUMN;
		HeaderLayoutStyle.m_AlignItems = EUiAlign::STRETCH;
		HeaderLayoutStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vHeaderChildren;
		std::vector<SUiLayoutChild> &vHeaderChildren = s_vHeaderChildren;
		vHeaderChildren.assign(2, SUiLayoutChild{});
		vHeaderChildren[0].m_Style.m_Height = SUiLength::Px(FontSize);
		vHeaderChildren[1].m_Style.m_Height = SUiLength::Flex(1.0f);
		LayoutEngine.ComputeChildren(HeaderLayoutStyle, CUiV2LegacyAdapter::FromCUIRect(View), vHeaderChildren);
		Label = CUiV2LegacyAdapter::ToCUIRect(vHeaderChildren[0].m_Box);
		View = CUiV2LegacyAdapter::ToCUIRect(vHeaderChildren[1].m_Box);
	}
	char aNameBuf[MAX_NAME_LENGTH];
	pScoreboard->GameClient()->FormatStreamerName(pPopupContext->m_ClientId, aNameBuf, sizeof(aNameBuf));
	pUi->DoLabel(&Label, aNameBuf, FontSize, TEXTALIGN_ML);

	if(!pPopupContext->m_IsLocal)
	{
		const int ActionsNum = 3;
		const float ActionSize = 25.0f;
		const float ActionSpacing = (View.w - (ActionsNum * ActionSize)) / 2;
		int ActionCorners = IGraphics::CORNER_ALL;

		{
			CUiV2LayoutEngine LayoutEngine;
			SUiStyle SectionStyle;
			SectionStyle.m_Axis = EUiAxis::COLUMN;
			SectionStyle.m_AlignItems = EUiAlign::STRETCH;
			SectionStyle.m_JustifyContent = EUiAlign::START;
			static thread_local std::vector<SUiLayoutChild> s_vSectionChildren;
			std::vector<SUiLayoutChild> &vSectionChildren = s_vSectionChildren;
			vSectionChildren.assign(3, SUiLayoutChild{});
			vSectionChildren[0].m_Style.m_Height = SUiLength::Px(ItemSpacing * 2.0f);
			vSectionChildren[1].m_Style.m_Height = SUiLength::Px(ActionSize);
			vSectionChildren[2].m_Style.m_Height = SUiLength::Flex(1.0f);
			LayoutEngine.ComputeChildren(SectionStyle, CUiV2LegacyAdapter::FromCUIRect(View), vSectionChildren);
			Container = CUiV2LegacyAdapter::ToCUIRect(vSectionChildren[1].m_Box);
			View = CUiV2LegacyAdapter::ToCUIRect(vSectionChildren[2].m_Box);
		}
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle ActionRowStyle;
		ActionRowStyle.m_Axis = EUiAxis::ROW;
		ActionRowStyle.m_Gap = ActionSpacing;
		ActionRowStyle.m_AlignItems = EUiAlign::STRETCH;
		ActionRowStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vActions;
		std::vector<SUiLayoutChild> &vActions = s_vActions;
		vActions.assign(ActionsNum, SUiLayoutChild{});
		for(SUiLayoutChild &Child : vActions)
		{
			Child.m_Style.m_Width = SUiLength::Px(ActionSize);
		}
		LayoutEngine.ComputeChildren(ActionRowStyle, CUiV2LegacyAdapter::FromCUIRect(Container), vActions);

		Action = CUiV2LegacyAdapter::ToCUIRect(vActions[0].m_Box);

		ColorRGBA FriendActionColor = Client.m_Friend ? ColorRGBA(0.95f, 0.3f, 0.3f, 0.85f * pUi->ButtonColorMul(&pPopupContext->m_FriendAction)) :
								ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * pUi->ButtonColorMul(&pPopupContext->m_FriendAction));
		const char *pFriendActionIcon = pUi->HotItem() == &pPopupContext->m_FriendAction && Client.m_Friend ? FontIcons::FONT_ICON_HEART_CRACK : FontIcons::FONT_ICON_HEART;
		if(pUi->DoButton_FontIcon(&pPopupContext->m_FriendAction, pFriendActionIcon, Client.m_Friend, &Action, BUTTONFLAG_LEFT, ActionCorners, true, FriendActionColor))
		{
			if(Client.m_Friend)
			{
				pScoreboard->GameClient()->Friends()->RemoveFriend(Client.m_aName, Client.m_aClan);
			}
			else
			{
				pScoreboard->GameClient()->Friends()->AddFriend(Client.m_aName, Client.m_aClan);
			}
		}

		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_FriendAction, &Action, Client.m_Friend ? Localize("Remove friend") : Localize("Add friend"));

		Action = CUiV2LegacyAdapter::ToCUIRect(vActions[1].m_Box);

		if(pUi->DoButton_FontIcon(&pPopupContext->m_MuteAction, FontIcons::FONT_ICON_BAN, Client.m_ChatIgnore, &Action, BUTTONFLAG_LEFT, ActionCorners))
		{
			Client.m_ChatIgnore ^= 1;
		}
		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_MuteAction, &Action, Client.m_ChatIgnore ? Localize("Unmute") : Localize("Mute"));

		Action = CUiV2LegacyAdapter::ToCUIRect(vActions[2].m_Box);

		const char *EmoticonActionIcon = Client.m_EmoticonIgnore ? FontIcons::FONT_ICON_COMMENT_SLASH : FontIcons::FONT_ICON_COMMENT;
		if(pUi->DoButton_FontIcon(&pPopupContext->m_EmoticonAction, EmoticonActionIcon, Client.m_EmoticonIgnore, &Action, BUTTONFLAG_LEFT, ActionCorners))
		{
			Client.m_EmoticonIgnore ^= 1;
		}
		pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_EmoticonAction, &Action, Client.m_EmoticonIgnore ? Localize("Unmute emoticons") : Localize("Mute emoticons"));
	}

	const float ButtonSize = 17.5f;
	{
		CUiV2LayoutEngine LayoutEngine;
		SUiStyle SectionStyle;
		SectionStyle.m_Axis = EUiAxis::COLUMN;
		SectionStyle.m_AlignItems = EUiAlign::STRETCH;
		SectionStyle.m_JustifyContent = EUiAlign::START;
		static thread_local std::vector<SUiLayoutChild> s_vSectionChildren;
		std::vector<SUiLayoutChild> &vSectionChildren = s_vSectionChildren;
		vSectionChildren.assign(3, SUiLayoutChild{});
		vSectionChildren[0].m_Style.m_Height = SUiLength::Px(ItemSpacing * 2.0f);
		vSectionChildren[1].m_Style.m_Height = SUiLength::Px(ButtonSize);
		vSectionChildren[2].m_Style.m_Height = SUiLength::Flex(1.0f);
		LayoutEngine.ComputeChildren(SectionStyle, CUiV2LegacyAdapter::FromCUIRect(View), vSectionChildren);
		Container = CUiV2LegacyAdapter::ToCUIRect(vSectionChildren[1].m_Box);
		View = CUiV2LegacyAdapter::ToCUIRect(vSectionChildren[2].m_Box);
	}

	bool IsSpectating = pScoreboard->GameClient()->m_Snap.m_SpecInfo.m_Active && pScoreboard->GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == pPopupContext->m_ClientId;
	ColorRGBA SpectateButtonColor = ColorRGBA(1.0f, 1.0f, 1.0f, (IsSpectating ? 0.25f : 0.5f) * pUi->ButtonColorMul(&pPopupContext->m_SpectateButton));
	if(pUi->DoButton_PopupMenu(&pPopupContext->m_SpectateButton, Localize("Spectate"), &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, SpectateButtonColor))
	{
		if(IsSpectating)
		{
			pScoreboard->GameClient()->m_Spectator.Spectate(SPEC_FREEVIEW);
			pScoreboard->Console()->ExecuteLine("say /spec");
		}
		else
		{
			if(pScoreboard->GameClient()->m_Snap.m_SpecInfo.m_Active)
			{
				pScoreboard->GameClient()->m_Spectator.Spectate(pPopupContext->m_ClientId);
			}
			else
			{
				// escape the name
				char aEscapedCommand[2 * MAX_NAME_LENGTH + 32];
				str_copy(aEscapedCommand, "say /spec \"");
				char *pDst = aEscapedCommand + str_length(aEscapedCommand);
				str_escape(&pDst, Client.m_aName, aEscapedCommand + sizeof(aEscapedCommand));
				str_append(aEscapedCommand, "\"");

				pScoreboard->Console()->ExecuteLine(aEscapedCommand);
			}
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}
