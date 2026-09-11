/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "scoreboard.h"

#include <engine/demo.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data7.h>
#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmLayout.h>
#include <game/client/QmUi/QmLegacy.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/animstate.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/motd.h>
#include <game/client/components/player_points.h>
#include <game/client/components/qmclient/axiom_scores.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/statboard.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
	// 记分板未打开时每帧预热的 Axiom 分数条数：够让打开时立刻有数据，又不会无谓刷接口。
	constexpr int AXIOM_SCOREBOARD_PREFETCH_BUDGET = 12;

	uint64_t ScoreboardPresentationNodeKey(const char *pScope)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("qm_extra_scoreboard_presentation"));
		return BuildUiAnimNodeKey(s_BaseKey, static_cast<uint64_t>(str_quickhash(pScope)));
	}

	SUiSpringConfig ScoreboardPresentationSpring()
	{
		SUiSpringConfig Spring;
		Spring.m_Stiffness = 460.0f;
		Spring.m_Damping = 42.0f;
		Spring.m_RestEpsilon = 0.008f;
		Spring.m_RestVelocity = 0.08f;
		return Spring;
	}

	SUiSpringConfig ScoreboardContentSpring(bool Opening)
	{
		SUiSpringConfig Spring = ScoreboardPresentationSpring();
		if(!Opening)
		{
			constexpr float CloseTimeScale = 0.30f;
			Spring.m_Stiffness /= CloseTimeScale * CloseTimeScale;
			Spring.m_Damping /= CloseTimeScale;
			Spring.m_RestVelocity /= CloseTimeScale;
		}
		return Spring;
	}

	SUiSpringConfig ScoreboardElementSpring(bool Visible)
	{
		SUiSpringConfig Spring;
		Spring.m_Stiffness = 520.0f;
		Spring.m_Damping = 44.0f;
		Spring.m_RestEpsilon = 0.005f;
		Spring.m_RestVelocity = 0.08f;
		if(!Visible)
		{
			constexpr float ExitTimeScale = 0.38f;
			Spring.m_Stiffness /= ExitTimeScale * ExitTimeScale;
			Spring.m_Damping /= ExitTimeScale;
			Spring.m_RestVelocity /= ExitTimeScale;
		}
		return Spring;
	}

	CUIRect ScaleRectAroundCenter(const CUIRect &Rect, float Scale)
	{
		Scale = std::max(0.01f, Scale);
		const float CenterX = Rect.x + Rect.w * 0.5f;
		const float CenterY = Rect.y + Rect.h * 0.5f;
		const float Width = Rect.w * Scale;
		const float Height = Rect.h * Scale;
		return {CenterX - Width * 0.5f, CenterY - Height * 0.5f, Width, Height};
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
		int CConfig::*m_pConfig;
		const char *m_pIcon;
		const char *m_pTitle;
		const char *m_pDescription;
	};

	static const SSoundMuteButtonDef gs_aSoundMuteButtonDefs[] = {
		{&CConfig::m_ClSndMuteWeapon, FontIcons::FONT_ICON_CIRCLE, "武器音效", "屏蔽主要武器发射与命中相关声音。"},
		{&CConfig::m_ClSndMuteWeaponSwitch, FontIcons::FONT_ICON_ARROWS_LEFT_RIGHT, "武器切换音效", "屏蔽武器切换及相关切换提示音。"},
		{&CConfig::m_ClSndMuteWeaponNoAmmo, FontIcons::FONT_ICON_TRIANGLE_EXCLAMATION, "无弹药提示音", "屏蔽武器无弹药时的提示音。"},
		{&CConfig::m_ClSndMuteHook, FontIcons::FONT_ICON_ARROWS_ROTATE, "钩子音效", "屏蔽钩子发射、收回等相关声音。"},
		{&CConfig::m_ClSndMuteMovement, FontIcons::FONT_ICON_ARROWS_UP_DOWN, "移动音效", "屏蔽行走与跳跃等移动相关声音。"},
		{&CConfig::m_ClSndMutePlayerState, FontIcons::FONT_ICON_HEART_CRACK, "玩家状态音效", "屏蔽玩家状态变化相关声音。"},
		{&CConfig::m_ClSndMutePickup, FontIcons::FONT_ICON_SQUARE_PLUS, "拾取音效", "屏蔽道具与武器拾取相关声音。"},
		{&CConfig::m_ClSndMuteFlag, FontIcons::FONT_ICON_FLAG_CHECKERED, "旗帜音效", "屏蔽 CTF 旗帜事件相关声音。"},
		{&CConfig::m_ClSndMuteMapSound, FontIcons::FONT_ICON_MAP, "地图音效", "屏蔽地图环境与脚本触发音效。"},
	};
	static_assert((sizeof(gs_aSoundMuteButtonDefs) / sizeof(gs_aSoundMuteButtonDefs[0])) == 9, "Sound mute button count mismatch");
	constexpr float CLIENT_BRAND_LABEL_GAP = 3.0f;

	ColorRGBA ClientBrandScoreboardColor(EClientBrand Brand, float Alpha)
	{
		switch(Brand)
		{
		case EClientBrand::QM:
			return ColorRGBA(0.38f, 0.89f, 1.0f, Alpha);
		case EClientBrand::ARG:
			return ColorRGBA(1.0f, 0.66f, 0.28f, Alpha);
		case EClientBrand::NONE:
			return ColorRGBA(1.0f, 1.0f, 1.0f, Alpha);
		}
		return ColorRGBA(1.0f, 1.0f, 1.0f, Alpha);
	}

	ColorRGBA ScoreboardUiColor()
	{
		return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmScoreboardColor)).WithAlpha(1.0f);
	}

	float ScoreboardUiAlpha(float AlphaScale)
	{
		return ui_token::color::UiColorAccent(ScoreboardUiColor(), AlphaScale * (g_Config.m_QmScoreboardOpacity / 100.0f)).a;
	}

	ColorRGBA ScoreboardUiColorSurface(float AlphaScale, float ColorScale = 0.16f)
	{
		ColorRGBA Color = ui_token::color::UiColorSurface(ScoreboardUiColor(), 1.0f, ColorScale);
		Color.a = ScoreboardUiAlpha(AlphaScale);
		return Color;
	}

	ColorRGBA ScoreboardWithUiAlpha(ColorRGBA Color, float AlphaScale)
	{
		Color.a = ScoreboardUiAlpha(AlphaScale);
		return Color;
	}

	ColorRGBA ScoreboardDecorationColor(ColorRGBA Color, float AlphaScale = 1.0f)
	{
		Color.a = std::clamp(Color.a * AlphaScale * (g_Config.m_QmScoreboardOpacity / 100.0f), 0.0f, 1.0f);
		return Color;
	}

	ColorRGBA ScoreboardGlassSurface(float AlphaScale)
	{
		return ScoreboardUiColorSurface(AlphaScale);
	}

	int DoScoreboardMediaIconButton(CUi *pUi, ITextRender *pTextRender, CButtonContainer *pButtonContainer, const char *pIcon, const CUIRect *pRect, bool Enabled, ColorRGBA ButtonColor, float ContentAlpha)
	{
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		const float IconAlpha = std::clamp(ContentAlpha, 0.0f, 1.0f);
		pRect->Draw(pUi->ScaleBackgroundAlpha(ButtonColor), IGraphics::CORNER_ALL, 5.0f);

		const ColorRGBA PreviousTextColor = pTextRender->GetTextColor();
		const ColorRGBA PreviousOutlineColor = pTextRender->GetTextOutlineColor();
		pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
		pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
		pTextRender->TextOutlineColor(pTextRender->DefaultTextOutlineColor().WithMultipliedAlpha(IconAlpha));
		pTextRender->TextColor(pTextRender->DefaultTextColor().WithMultipliedAlpha(IconAlpha));

		CUIRect Label;
		pRect->HMargin(2.0f, &Label);
		pUi->DoLabel(&Label, pIcon, Label.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);

		if(!Enabled)
		{
			pTextRender->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f, IconAlpha));
			pTextRender->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
			pUi->DoLabel(&Label, FontIcons::FONT_ICON_SLASH, Label.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
		}

		pTextRender->SetRenderFlags(0);
		pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
		pTextRender->TextOutlineColor(PreviousOutlineColor);
		pTextRender->TextColor(PreviousTextColor);

		return Enabled ? pUi->DoButtonLogic(pButtonContainer, 0, pRect, BUTTONFLAG_LEFT) : 0;
	}
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
		if(pSelf->m_LastMousePos.has_value())
			pSelf->SetUiMousePos(pSelf->m_LastMousePos.value());
		pSelf->m_LastMousePos = pSelf->Ui()->MousePos();
	}
}

void CScoreboard::UpdateQmAxiomScoreMode()
{
	m_QmAxiomScoreModeFrameValid = true;
	m_QmAxiomScoreModeFrame = EQmAxiomMode::NONE;

	if(!GameClient()->m_QmAxiomAutoLogin.IsAxiomCommunity())
		return;
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	CServerInfo ServerInfo;
	Client()->GetServerInfo(&ServerInfo);
	// 社区分类优先（Axiom 的 Gores / Other 两分类正好对应两个积分模式），
	// 分类拿不到时再看服务器名是否带 AXRace。
	m_QmAxiomScoreModeFrame = QmResolveAxiomModeFromServerContext({ServerInfo.m_aCommunityType, ServerInfo.m_aName});
}

bool CScoreboard::HasQmAxiomScoreMode()
{
	if(!m_QmAxiomScoreModeFrameValid)
		UpdateQmAxiomScoreMode();
	return m_QmAxiomScoreModeFrame != EQmAxiomMode::NONE;
}

void CScoreboard::QmAxiomScorePoints(const char *pPlayerName, bool Visible, char *pBuffer, int BufferSize) const
{
	pBuffer[0] = '\0';
	if(!Visible)
		return;

	const SQmAxiomLookupResult Lookup = GameClient()->m_QmAxiomScores.GetLookup(pPlayerName);
	switch(Lookup.m_Status)
	{
	case EQmAxiomScoreStatus::READY:
		str_format(pBuffer, BufferSize, "%lld", (long long)Lookup.m_Points);
		break;
	case EQmAxiomScoreStatus::FETCHING:
	case EQmAxiomScoreStatus::NOT_REQUESTED:
		str_copy(pBuffer, "...", BufferSize);
		break;
	case EQmAxiomScoreStatus::HTTP_ERROR:
	case EQmAxiomScoreStatus::API_ERROR:
	case EQmAxiomScoreStatus::INVALID_RESPONSE:
		str_copy(pBuffer, "?", BufferSize);
		break;
	case EQmAxiomScoreStatus::NOT_FOUND:
	case EQmAxiomScoreStatus::AMBIGUOUS:
		str_copy(pBuffer, "-", BufferSize);
		break;
	}
}

void CScoreboard::ConToggleScoreboardCursor(IConsole::IResult *pResult, void *pUserData)
{
	CScoreboard *pSelf = static_cast<CScoreboard *>(pUserData);

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
	m_PresentationInitialized = false;
	m_MouseUnlocked = false;
	m_RenderInteractions = false;
	m_aCachedTeamModes = {};
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
	m_PresentationInitialized = false;
	m_RenderInteractions = false;
	m_aCachedTeamModes = {};
	m_SoundMuteButtonAnimState.Reset();
	m_SoundMuteInfoAnimState.Reset();

	if(m_MouseUnlocked)
	{
		Ui()->ClosePopupMenus();
		m_MouseUnlocked = false;
		if(m_LastMousePos.has_value())
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

void CScoreboard::RenderTitle(CUIRect TitleLabel, int Team, const char *pTitle, float TitleFontSize)
{
	const bool IsMapTitle = Team == TEAM_GAME && !GameClient()->IsTeamPlay();
	if(IsMapTitle && m_MouseUnlocked && GameClient()->m_aMapDescription[0] != '\0')
	{
		const int ButtonResult = Ui()->DoButtonLogic(&m_MapTitleButtonId, 0, &TitleLabel, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
		if(ButtonResult != 0)
		{
			m_MapTitlePopupContext.m_pScoreboard = this;

			m_MapTitlePopupContext.m_FontSize = 12.0f;
			const float MaxWidth = 300.0f;
			const float Margin = 5.0f;
			const char *pDescription = GameClient()->m_aMapDescription;
			const float TextWidth = minimum(std::ceil(TextRender()->TextWidth(m_MapTitlePopupContext.m_FontSize, pDescription) + 0.5f), MaxWidth);
			float TextHeight = 0.0f;
			STextSizeProperties TextSizeProps{};
			TextSizeProps.m_pHeight = &TextHeight;
			TextRender()->TextWidth(m_MapTitlePopupContext.m_FontSize, pDescription, -1, TextWidth, 0, TextSizeProps);

			Ui()->DoPopupMenu(&m_MapTitlePopupContext, Ui()->MouseX(), Ui()->MouseY(), TextWidth + Margin * 2, TextHeight + Margin * 2, &m_MapTitlePopupContext, CMapTitlePopupContext::Render);
		}
		if(Ui()->HotItem() == &m_MapTitleButtonId)
		{
			TitleLabel.Draw(ColorRGBA(0.7f, 0.7f, 0.7f, 0.3f), IGraphics::CORNER_ALL, 5.0f);
		}
	}

	SLabelProperties Props;
	Props.m_MaxWidth = TitleLabel.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&TitleLabel, pTitle, TitleFontSize, Team == TEAM_RED ? TEXTALIGN_ML : TEXTALIGN_MR, Props);
}

void CScoreboard::RenderTitleScore(CUIRect ScoreLabel, int Team, float TitleFontSize)
{
	// map best
	char aScore[128] = "";
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;
	const bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;
	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes || TimeScore || Race7)
	{
		if(GameClient()->m_MapBestTimeSeconds != FinishTime::UNSET)
		{
			Ui()->RenderTime(ScoreLabel,
				TitleFontSize,
				GameClient()->m_MapBestTimeSeconds,
				GameClient()->m_MapBestTimeSeconds == FinishTime::NOT_FINISHED_MILLIS,
				GameClient()->m_MapBestTimeMillis,
				GameClient()->m_ReceivedDDNetPlayerFinishTimesMillis);
			return;
		}
	}
	else if(GameClient()->IsTeamPlay()) // normal score
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

	const float ScoreTextWidth = aScore[0] != '\0' ? TextRender()->TextWidth(TitleFontSize, aScore, -1, -1.0f, 0) : 0.0f;
	if(ScoreTextWidth != 0.0f)
	{
		Ui()->DoLabel(&ScoreLabel, aScore, TitleFontSize, Team == TEAM_RED ? TEXTALIGN_MR : TEXTALIGN_ML);
	}
}

void CScoreboard::RenderTitleBar(CUIRect TitleBar, int Team, const char *pTitle)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE || Team == TEAM_GAME, "Team invalid");

	const float TitleFontSize = 20.0f;
	const float ScoreTextWidth = TextRender()->TextWidth(TitleFontSize, "00:00:00");

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
		if(Team == TEAM_RED || Team == TEAM_GAME)
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

	RenderTitle(TitleLabel, Team, pTitle, TitleFontSize);
	RenderTitleScore(ScoreLabel, Team, TitleFontSize);
}

void CScoreboard::RenderGoals(CUIRect Goals)
{
	const float ContentAlpha = m_AnimContentAlpha;
	Goals.Draw(ScoreboardUiColorSurface(ContentAlpha), IGraphics::CORNER_ALL, 7.5f);
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

	const bool ShowMediaControls = g_Config.m_QmSmtcEnable != 0;
	const bool IsTeamPlay = GameClient()->IsTeamPlay();
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
	SpectatorPanel.Draw(ScoreboardUiColorSurface(ContentAlpha), IGraphics::CORNER_ALL, CornerRadius);
	CUIRect SpectatorList = SpectatorPanel;
	SpectatorList.Margin(5.0f, &SpectatorList);

	CUIRect MediaControls;
	if(ShowMediaControls)
	{
		MediaPanel.Draw(ScoreboardUiColorSurface(ContentAlpha), IGraphics::CORNER_ALL, CornerRadius);
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
		if(!pInfo || QmScoreboardEffectivePlayerTeam(pInfo->m_Team, GameClient()->m_aClients[pInfo->m_ClientId].m_Spec, IsTeamPlay) != TEAM_SPECTATORS)
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
		if(!pInfo || QmScoreboardEffectivePlayerTeam(pInfo->m_Team, GameClient()->m_aClients[pInfo->m_ClientId].m_Spec, IsTeamPlay) != TEAM_SPECTATORS)
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
	if(DoScoreboardMediaIconButton(Ui(), TextRender(), &s_SmtcPrevButton, FontIcons::FONT_ICON_BACKWARD_STEP, &PrevButton, CanPrev && m_RenderInteractions, ColorRGBA(1.0f, 1.0f, 1.0f, PrevButtonAlpha), ContentAlpha))
	{
		GameClient()->m_SystemMediaControls.Previous();
	}
	RestoreTextColors();

	static CButtonContainer s_SmtcPlayButton;
	const char *pPlayIcon = MediaState.m_Playing ? FontIcons::FONT_ICON_PAUSE : FontIcons::FONT_ICON_PLAY;
	const float PlayButtonAlpha = 0.5f * Ui()->ButtonColorMul(&s_SmtcPlayButton) * ContentAlpha;
	if(DoScoreboardMediaIconButton(Ui(), TextRender(), &s_SmtcPlayButton, pPlayIcon, &PlayButton, CanToggle && m_RenderInteractions, ColorRGBA(1.0f, 1.0f, 1.0f, PlayButtonAlpha), ContentAlpha))
	{
		GameClient()->m_SystemMediaControls.PlayPause();
	}
	RestoreTextColors();

	static CButtonContainer s_SmtcNextButton;
	const float NextButtonAlpha = 0.5f * Ui()->ButtonColorMul(&s_SmtcNextButton) * ContentAlpha;
	if(DoScoreboardMediaIconButton(Ui(), TextRender(), &s_SmtcNextButton, FontIcons::FONT_ICON_FORWARD_STEP, &NextButton, CanNext && m_RenderInteractions, ColorRGBA(1.0f, 1.0f, 1.0f, NextButtonAlpha), ContentAlpha))
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
	if(g_Config.m_QmExtraAnimations != 0 && GameClient()->UiRuntimeV2()->Enabled())
		pAnimRuntime = &GameClient()->UiRuntimeV2()->AnimRuntime();

	if(pAnimRuntime != nullptr && !m_SoundMuteButtonAnimState.m_Initialized)
	{
		for(int i = 0; i < NumButtons; ++i)
		{
			const uint64_t NodeKey = SoundMuteButtonNodeKey(i);
			SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::ALPHA, 0.0f);
			SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::SCALE, 1.0f);
			SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_X, 18.0f);
			SetUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_Y, 0.0f);
			m_SoundMuteButtonAnimState.m_aTargetAlpha[i] = 0.0f;
			m_SoundMuteButtonAnimState.m_aTargetScale[i] = 1.0f;
			m_SoundMuteButtonAnimState.m_aTargetOffsetX[i] = 18.0f;
			m_SoundMuteButtonAnimState.m_aTargetReveal[i] = 0.0f;
		}
		m_SoundMuteButtonAnimState.m_Initialized = true;
	}

	if(pAnimRuntime != nullptr && !m_SoundMuteInfoAnimState.m_Initialized)
	{
		SetUiPresentationStateValue(*pAnimRuntime, SoundMuteInfoNodeKey(), EUiAnimProperty::ALPHA, 0.0f);
		SetUiPresentationStateValue(*pAnimRuntime, SoundMuteInfoNodeKey(), EUiAnimProperty::POS_X, 14.0f);
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
		m_SoundMuteButtonAnimState.m_aTargetAlpha[i] = TargetAlpha;
		m_SoundMuteButtonAnimState.m_aTargetScale[i] = TargetScale;
		m_SoundMuteButtonAnimState.m_aTargetOffsetX[i] = TargetOffsetX;
		m_SoundMuteButtonAnimState.m_aTargetReveal[i] = TargetYOffset;
		if(pAnimRuntime != nullptr)
		{
			const uint64_t NodeKey = SoundMuteButtonNodeKey(i);
			const SUiSpringConfig ElementSpring = ScoreboardElementSpring(TargetAlpha > 0.01f);
			Alpha = ResolveUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::ALPHA, m_SoundMuteButtonAnimState.m_aTargetAlpha[i], ElementSpring, 2, 0.003f);
			Scale = ResolveUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::SCALE, m_SoundMuteButtonAnimState.m_aTargetScale[i], ElementSpring, 2, 0.003f);
			OffsetX = ResolveUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_X, m_SoundMuteButtonAnimState.m_aTargetOffsetX[i], ElementSpring, 2, 0.01f);
			YOffset = ResolveUiPresentationStateValue(*pAnimRuntime, NodeKey, EUiAnimProperty::POS_Y, m_SoundMuteButtonAnimState.m_aTargetReveal[i], ElementSpring, 2, 0.01f);
		}

		Alpha = std::clamp(Alpha, 0.0f, 1.0f);
		if(Alpha <= 0.01f)
			continue;

		CUIRect Button = {ColumnX, ColumnY + i * (ButtonSize + Gap), ButtonSize, ButtonSize};
		Button.x += OffsetX;
		Button.y += YOffset;
		const float ScaledSize = ButtonSize * std::max(0.01f, Scale);
		Button.x += (ButtonSize - ScaledSize) * 0.5f;
		Button.y += (ButtonSize - ScaledSize) * 0.5f;
		Button.w = ScaledSize;
		Button.h = ScaledSize;

		if(Button.w <= 1.0f || Button.h <= 1.0f)
			continue;

		aAnimatedRects[i] = Button;

		const bool Active = g_Config.*gs_aSoundMuteButtonDefs[i].m_pConfig != 0;
		const bool Clickable = IsNearest && m_RenderInteractions;
		const float RenderAlpha = Alpha * ContentAlpha;
		const ColorRGBA ButtonColor = Active ?
						      ColorRGBA(1.0f, 0.32f, 0.32f, 0.95f * RenderAlpha) :
						      ColorRGBA(0.82f, 0.88f, 0.96f, 0.45f * RenderAlpha);
		if(Ui()->DoButton_FontIcon(&s_aButtons[i], gs_aSoundMuteButtonDefs[i].m_pIcon, 0, &Button, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, Clickable, ButtonColor) && Clickable)
			g_Config.*gs_aSoundMuteButtonDefs[i].m_pConfig ^= 1;
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
	m_SoundMuteInfoAnimState.m_TargetAlpha = InfoTargetAlpha;
	m_SoundMuteInfoAnimState.m_TargetOffsetX = InfoTargetOffsetX;
	if(pAnimRuntime != nullptr)
	{
		const SUiSpringConfig InfoSpring = ScoreboardElementSpring(InfoTargetAlpha > 0.01f);
		InfoAlpha = ResolveUiPresentationStateValue(*pAnimRuntime, SoundMuteInfoNodeKey(), EUiAnimProperty::ALPHA, m_SoundMuteInfoAnimState.m_TargetAlpha, InfoSpring, 2, 0.003f);
		InfoOffsetX = ResolveUiPresentationStateValue(*pAnimRuntime, SoundMuteInfoNodeKey(), EUiAnimProperty::POS_X, m_SoundMuteInfoAnimState.m_TargetOffsetX, InfoSpring, 2, 0.01f);
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

	InfoRect.Draw(ScoreboardUiColorSurface(InfoAlpha * ContentAlpha, 0.18f), IGraphics::CORNER_ALL, 6.0f);

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

void CScoreboard::UpdateTeamModeCache()
{
	std::array<SQmScoreboardTeamModeState, NUM_DDRACE_TEAMS> aTeamModes{};
	std::array<bool, NUM_DDRACE_TEAMS> aTeamHasSpecPlayer{};
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(GameClient()->m_Snap.m_apPlayerInfos[ClientId] == nullptr)
			continue;

		const int DDTeam = GameClient()->m_Teams.Team(ClientId);
		if(DDTeam < TEAM_FLOCK || DDTeam >= NUM_DDRACE_TEAMS)
			continue;

		const auto &Character = GameClient()->m_Snap.m_aCharacters[ClientId];
		AccumulateQmScoreboardTeamModeState(aTeamModes[DDTeam], Character.m_HasExtendedDisplayInfo, Character.m_ExtendedData.m_Flags);
	}
	CacheAndRestoreQmScoreboardTeamModes(aTeamModes, aTeamHasSpecPlayer, m_aCachedTeamModes);
}

void CScoreboard::BuildPlayerRowPlan(int Team, CScoreboardPlayerRowPlan &Plan)
{
	Plan.m_Count = 0;
	Plan.m_aTeamModes = {};
	Plan.m_aTeamHasSpecPlayer = {};
	std::array<int, MAX_CLIENTS> aPreviousSourceDDTeam{};
	std::array<int, MAX_CLIENTS> aNextSourceDDTeam{};
	const bool IsTeamPlay = GameClient()->IsTeamPlay();
	auto &&IsInScoreboardTeam = [&](const CNetObj_PlayerInfo *pInfo) {
		return pInfo != nullptr && QmScoreboardEffectivePlayerTeam(pInfo->m_Team, GameClient()->m_aClients[pInfo->m_ClientId].m_Spec, IsTeamPlay) == Team;
	};

	int PreviousDDTeam = -1;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		aPreviousSourceDDTeam[i] = PreviousDDTeam;
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apInfoByDDTeamScore[i];
		if(IsInScoreboardTeam(pInfo))
			PreviousDDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
	}

	int NextDDTeam = 0;
	for(int i = MAX_CLIENTS - 1; i >= 0; --i)
	{
		aNextSourceDDTeam[i] = NextDDTeam;
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apInfoByDDTeamScore[i];
		if(IsInScoreboardTeam(pInfo))
			NextDDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
	}

	for(int RenderDead = 0; RenderDead < 2; ++RenderDead)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apInfoByDDTeamScore[i];
			if(!IsInScoreboardTeam(pInfo))
				continue;
			const bool IsDead = (Client()->m_TranslationContext.m_aClients[pInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_DEAD) != 0;
			if(IsDead != (RenderDead != 0))
				continue;

			CScoreboardPlayerRow &Row = Plan.m_aRows[Plan.m_Count++];
			Row.m_pInfo = pInfo;
			Row.m_DDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
			if(Row.m_DDTeam >= TEAM_FLOCK && Row.m_DDTeam < NUM_DDRACE_TEAMS)
			{
				Plan.m_aTeamHasSpecPlayer[Row.m_DDTeam] = Plan.m_aTeamHasSpecPlayer[Row.m_DDTeam] || GameClient()->m_aClients[pInfo->m_ClientId].m_Spec;
				const auto &Character = GameClient()->m_Snap.m_aCharacters[pInfo->m_ClientId];
				AccumulateQmScoreboardTeamModeState(Plan.m_aTeamModes[Row.m_DDTeam], Character.m_HasExtendedDisplayInfo, Character.m_ExtendedData.m_Flags);
			}
			Row.m_PreviousSourceDDTeam = aPreviousSourceDDTeam[i];
			Row.m_NextSourceDDTeam = aNextSourceDDTeam[i];
			Row.m_Dead = IsDead;
		}
	}
	CacheAndRestoreQmScoreboardTeamModes(Plan.m_aTeamModes, Plan.m_aTeamHasSpecPlayer, m_aCachedTeamModes);
}

void CScoreboard::RenderTeamModeIcons(float x, float y, float IconSize, const SQmScoreboardTeamModeState &State, float Alpha)
{
	if(!State.m_Known || State.m_Flags == 0)
		return;

	Graphics()->BlendNormal();
	auto &&RenderIcon = [&](bool Active, IGraphics::CTextureHandle Texture) {
		if(!Active)
			return;
		Graphics()->TextureSet(Texture);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
		IGraphics::CQuadItem QuadItem(x, y, IconSize, IconSize);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		x += IconSize + 1.5f;
	};

	RenderIcon(State.Practice(), GameClient()->m_HudSkin.m_SpriteHudPracticeMode);
	RenderIcon(State.Team0Mode(), GameClient()->m_HudSkin.m_SpriteHudTeam0Mode);
	RenderIcon(State.Locked(), GameClient()->m_HudSkin.m_SpriteHudLockMode);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CScoreboard::RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, const CScoreboardPlayerRowPlan &Plan, CScoreboardRenderState &State)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;
	const int NumPlayers = CountEnd - CountStart;
	const bool LowScoreboardWidth = Scoreboard.w < 350.0f;
	// Axiom 积分服上这一列显示当前模式的 Axiom 分数，取代 DDNet 在线点数。
	const bool AxiomScoreColumn = HasQmAxiomScoreMode();
	const bool ShowPoints = AxiomScoreColumn || g_Config.m_QmScoreboardPoints != 0;
	const float ContentAlpha = m_AnimContentAlpha;
	const ColorRGBA BaseTextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ContentAlpha);
	const ColorRGBA BaseOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ContentAlpha);
	TextRender()->TextColor(BaseTextColor);
	TextRender()->TextOutlineColor(BaseOutlineColor);

	bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;
	const int FirstRow = std::clamp(CountStart, 0, Plan.m_Count);
	const int EndRow = std::clamp(CountEnd, FirstRow, Plan.m_Count);
	auto &&TeamHasModeIcons = [&](int DDTeam) {
		return DDTeam >= TEAM_FLOCK && DDTeam < NUM_DDRACE_TEAMS && Plan.m_aTeamModes[DDTeam].m_Known && Plan.m_aTeamModes[DDTeam].m_Flags != 0;
	};
	int NumTeamLabels = 0;
	int NumTeamModeLabels = 0;
	for(int RowIndex = FirstRow; RowIndex < EndRow; ++RowIndex)
	{
		const CScoreboardPlayerRow &PlannedRow = Plan.m_aRows[RowIndex];
		if(PlannedRow.m_DDTeam != TEAM_FLOCK && PlannedRow.m_NextSourceDDTeam != PlannedRow.m_DDTeam)
		{
			++NumTeamLabels;
			if(TeamHasModeIcons(PlannedRow.m_DDTeam))
				++NumTeamModeLabels;
		}
	}

	// calculate measurements
	const float HeadlineFontsize = 11.0f;
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
	const float PreferredTeamFontSize = FontSize / 1.5f;
	const float RowsVerticalScale = ScoreboardRowsVerticalScale(
		maximum(0.0f, Scoreboard.h - HeadlineFontsize * 2.0f),
		EndRow - FirstRow,
		NumTeamLabels,
		NumTeamModeLabels,
		LineHeight,
		Spacing,
		PreferredTeamFontSize,
		SCOREBOARD_TEAM_MODE_ICON_SIZE);
	LineHeight *= RowsVerticalScale;
	TeeSizeMod *= RowsVerticalScale;
	Spacing *= RowsVerticalScale;
	RoundRadius *= RowsVerticalScale;
	FontSize *= RowsVerticalScale;

	const SScoreboardRowRenderDetail RowDetail = ResolveScoreboardRowRenderDetail();
	const bool ShowClientBrand = RowDetail.m_ShowClientBrand && g_Config.m_QmClientShowBadge;
	const float ClientBrandLength = ShowClientBrand ? maximum(TextRender()->TextWidth(FontSize, "Qm"), TextRender()->TextWidth(FontSize, "Arg")) + CLIENT_BRAND_LABEL_GAP : 0.0f;
	const float ClientBrandOffset = Scoreboard.x + 10.0f;
	const float ScoreOffset = Scoreboard.x + 20.0f + ClientBrandLength;
	const float ScoreLength = TextRender()->TextWidth(FontSize, TimeScore ? "00:00:00" : "99999");
	// Points column: placed between Score and Tee (only when enabled)
	const float PointsLength = ShowPoints ? (LowScoreboardWidth ? TextRender()->TextWidth(FontSize, "99999") : TextRender()->TextWidth(FontSize, "999999")) : 0.0f;
	const float PointsOffset = ScoreOffset + ScoreLength + 10.0f;
	const float TeeOffset = ShowPoints ? (PointsOffset + PointsLength + 10.0f) : (ScoreOffset + ScoreLength + 10.0f);
	const float TeeLength = 60.0f * TeeSizeMod;
	const float NameOffset = TeeOffset + TeeLength;
	const float CountryLength = (LineHeight - Spacing - TeeSizeMod * 5.0f) * 2.0f;
	const float PingLength = 27.5f;
	const float PingOffset = Scoreboard.x + Scoreboard.w - PingLength - 10.0f;
	const float CountryOffset = RowDetail.m_ShowCountry ? PingOffset - CountryLength : PingOffset;
	const float NameLength = RowDetail.m_ShowClan ? (LowScoreboardWidth ? 90.0f : 150.0f) - TeeLength : maximum(0.0f, PingOffset - NameOffset - 5.0f);
	const float ClanOffset = NameOffset + NameLength + 2.5f;
	const float ClanLength = CountryOffset - ClanOffset - 2.5f;

	// render headlines
	CUIRect Headline;
	Scoreboard.HSplitTop(HeadlineFontsize * 2.0f, &Headline, &Scoreboard);
	const float HeadlineY = Headline.y + Headline.h / 2.0f - HeadlineFontsize / 2.0f;
	const char *pScore = TimeScore ? Localize("Time") : Localize("Score");
	TextRender()->Text(ScoreOffset + ScoreLength - TextRender()->TextWidth(HeadlineFontsize, pScore), HeadlineY, HeadlineFontsize, pScore);
	// Points column header: only render when enabled
	if(ShowPoints)
	{
		// 表头直接写模式名，让玩家知道这一列是哪个模式的分数。
		const char *pPointsLabel = AxiomScoreColumn ? QmAxiomModeName(m_QmAxiomScoreModeFrame) : Localize("Points");
		TextRender()->Text(PointsOffset + PointsLength - TextRender()->TextWidth(HeadlineFontsize, pPointsLabel), HeadlineY, HeadlineFontsize, pPointsLabel);
	}
	TextRender()->Text(NameOffset, HeadlineY, HeadlineFontsize, Localize("Name"));
	if(RowDetail.m_ShowClan)
	{
		const char *pClanLabel = Localize("Clan");
		TextRender()->Text(ClanOffset + (ClanLength - TextRender()->TextWidth(HeadlineFontsize, pClanLabel)) / 2.0f, HeadlineY, HeadlineFontsize, pClanLabel);
	}
	const char *pPingLabel = Localize("Ping");
	TextRender()->Text(PingOffset + PingLength - TextRender()->TextWidth(HeadlineFontsize, pPingLabel), HeadlineY, HeadlineFontsize, pPingLabel);

	// render player entries
	int PrevDDTeam = -1;
	int &CurrentDDTeamSize = State.m_CurrentDDTeamSize;

	char aBuf[64];
	int MaxTeamSize = Config()->m_SvMaxTeamSize;
	const CAnimState *pIdleState = CAnimState::GetIdle();
	const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	const char *pLocalClan = LocalClientId >= 0 ? GameClient()->m_aClients[LocalClientId].m_aClan : "";

	for(int RowIndex = FirstRow; RowIndex < EndRow; ++RowIndex)
	{
		const CScoreboardPlayerRow &PlannedRow = Plan.m_aRows[RowIndex];
		const CNetObj_PlayerInfo *pInfo = PlannedRow.m_pInfo;
		const bool RenderDead = PlannedRow.m_Dead;
		const int DDTeam = PlannedRow.m_DDTeam;
		const int NextDDTeam = PlannedRow.m_NextSourceDDTeam;

		const float ItemAlpha = (RenderDead ? 0.5f : 1.0f) * ContentAlpha;
		ColorRGBA TextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(ItemAlpha);
		TextRender()->TextColor(TextColor);

		if(PrevDDTeam == -1)
			PrevDDTeam = PlannedRow.m_PreviousSourceDDTeam;

		const float TeamFontSize = FontSize / 1.5f;
		const bool EndsDDTeam = DDTeam != TEAM_FLOCK && NextDDTeam != DDTeam;
		const bool HasTeamModeIcons = EndsDDTeam && TeamHasModeIcons(DDTeam);
		const SScoreboardTeamLabelLayout TeamLabelLayout = ResolveScoreboardTeamLabelLayout(
			Scoreboard.x,
			Scoreboard.y,
			LineHeight,
			Spacing,
			TeamFontSize,
			HasTeamModeIcons ? SCOREBOARD_TEAM_MODE_ICON_SIZE : 0.0f,
			EndsDDTeam);
		CUIRect RowAndSpacing, Row;
		Scoreboard.HSplitTop(LineHeight + TeamLabelLayout.m_RowSpacing, &RowAndSpacing, &Scoreboard);
		RowAndSpacing.HSplitTop(LineHeight, &Row, nullptr);

		// team background
		if(DDTeam != TEAM_FLOCK)
		{
			const ColorRGBA Color = ScoreboardDecorationColor(GameClient()->GetDDTeamColor(DDTeam).WithAlpha(0.5f * ItemAlpha));
			int TeamRectCorners = 0;
			if(PrevDDTeam != DDTeam)
			{
				TeamRectCorners |= IGraphics::CORNER_T;
			}
			if(NextDDTeam != DDTeam)
				TeamRectCorners |= IGraphics::CORNER_B;
			RowAndSpacing.Draw(Color, TeamRectCorners, RoundRadius);

			CurrentDDTeamSize++;

			if(EndsDDTeam)
			{
				if(DDTeam == TEAM_SUPER)
					str_copy(aBuf, Localize("Super"));
				else if(CurrentDDTeamSize > 1)
					str_format(aBuf, sizeof(aBuf), Localize("Team %d (%d/%d)"), DDTeam, CurrentDDTeamSize, MaxTeamSize);
				else
					str_format(aBuf, sizeof(aBuf), Localize("Team %d"), DDTeam);
				TextRender()->Text(TeamLabelLayout.m_X, TeamLabelLayout.m_Y, TeamFontSize, aBuf);
				if(HasTeamModeIcons)
				{
					RenderTeamModeIcons(
						TeamLabelLayout.m_X + TextRender()->TextWidth(TeamFontSize, aBuf) + 1.5f,
						TeamLabelLayout.m_IconY,
						SCOREBOARD_TEAM_MODE_ICON_SIZE,
						Plan.m_aTeamModes[DDTeam],
						ItemAlpha);
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
			CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
			Row.Draw(ScoreboardDecorationColor(ui_token::color::ACCENT_PRIMARY_DIM.WithMultipliedAlpha(ItemAlpha * 1.45f)), IGraphics::CORNER_ALL, RoundRadius);
		}

		const int ClientId = pInfo->m_ClientId;
		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];

		if(m_MouseUnlocked && m_RenderInteractions)
		{
			CUiScopedGaussianBlurSuppression GaussianBlurSuppression(Ui());
			const int ButtonResult = Ui()->DoButtonLogic(&ClientData, 0, &Row, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
			if(ButtonResult != 0)
			{
				m_ScoreboardPopupContext.m_pScoreboard = this;
				m_ScoreboardPopupContext.m_ClientId = ClientId;
				m_ScoreboardPopupContext.m_IsLocal = GameClient()->m_aLocalIds[0] == ClientId ||
								     (Client()->DummyConnected() && GameClient()->m_aLocalIds[1] == ClientId);
				Ui()->DoPopupMenu(&m_ScoreboardPopupContext, Ui()->MouseX(), Ui()->MouseY(), 110.0f, m_ScoreboardPopupContext.m_IsLocal ? 58.5f : 87.5f, &m_ScoreboardPopupContext, PopupScoreboard);
			}

			if(Ui()->HotItem() == &ClientData ||
				(Ui()->IsPopupOpen(&m_ScoreboardPopupContext) && m_ScoreboardPopupContext.m_ClientId == ClientId))
			{
				Row.Draw(ScoreboardDecorationColor(ColorRGBA(0.7f, 0.7f, 0.7f, 0.7f * ItemAlpha)), IGraphics::CORNER_ALL, RoundRadius);
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
			if(LocalClientId >= 0 && str_comp(aClanBuf, pLocalClan) == 0)
			{
				IsSameClan = true;
			}
		}
		const bool HasWar = !HideIdentity && g_Config.m_TcWarList && g_Config.m_TcWarListScoreboard && GameClient()->m_WarList.GetAnyWar(ClientId);

		const EClientBrand ClientBrand = ShowClientBrand ? GameClient()->ClientBrand(ClientData.m_aName) : EClientBrand::NONE;
		if(!HideIdentity && ClientBrand != EClientBrand::NONE)
		{
			const char *pClientBrandLabel = ClientBrandPrefix(ClientBrand);
			const float ClientBrandWidth = TextRender()->TextWidth(FontSize, pClientBrandLabel);
			const float ClientBrandX = ClientBrandOffset + maximum(0.0f, ClientBrandLength - CLIENT_BRAND_LABEL_GAP - ClientBrandWidth) / 2.0f;
			TextRender()->TextColor(ClientBrandScoreboardColor(ClientBrand, ItemAlpha));
			TextRender()->Text(ClientBrandX, Row.y + (Row.h - FontSize) / 2.0f, FontSize, pClientBrandLabel);
			TextRender()->TextColor(TextColor);
		}
		// Points column: render actual points value, right-aligned (only when enabled)
		if(ShowPoints)
		{
			char aPointsValue[16];
			if(AxiomScoreColumn)
			{
				// 隐藏主播身份的玩家不暴露 Axiom 分数。
				const bool PointsVisible = !HideIdentity && !GameClient()->ShouldHideStreamerIdentity(ClientId);
				QmAxiomScorePoints(ClientData.m_aName, PointsVisible, aPointsValue, sizeof(aPointsValue));
			}
			else
			{
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
			}
			if(aPointsValue[0] != '\0')
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
			vec2 TeeRenderPos = vec2(TeeOffset + TeeLength / 2, Row.y + Row.h / 2.0f);
			if(RowDetail.m_FullTee)
			{
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
				TeeRenderPos.y += OffsetToMid.y;
				RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos, ItemAlpha);
			}
			else
				RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos, TEE_PREVIEW_LAYER_BODY, ItemAlpha);
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

			// TClient
			if(HasWar)
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
		if(RowDetail.m_ShowClan)
		{
			const char *pClanName = aClanBuf;
			if(IsSameClan)
			{
				TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor)).WithMultipliedAlpha(ItemAlpha));
			}
			else
			{
				TextRender()->TextColor(TextColor);
			}

			// TClient
			if(HasWar)
				TextRender()->TextColor(GameClient()->m_WarList.GetClanColor(ClientId).WithMultipliedAlpha(ItemAlpha));

			CTextCursor Cursor;
			Cursor.SetPosition(vec2(ClanOffset + (ClanLength - minimum(TextRender()->TextWidth(FontSize, pClanName), ClanLength)) / 2.0f, Row.y + (Row.h - FontSize) / 2.0f));
			Cursor.m_FontSize = FontSize;
			Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.m_LineWidth = ClanLength;
			TextRender()->TextEx(&Cursor, pClanName);
		}

		// country flag
		if(RowDetail.m_ShowCountry)
		{
			const int CountryCode = g_Config.m_QmStreamerScoreboardDefaultFlags ? -1 : ClientData.m_Country;
			GameClient()->m_CountryFlags.Render(CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * ItemAlpha),
				CountryOffset, Row.y + (Spacing + TeeSizeMod * 5.0f) / 2.0f, CountryLength, Row.h - Spacing - TeeSizeMod * 5.0f);
		}

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
	Rect.Draw(ScoreboardUiColorSurface(ContentAlpha), IGraphics::CORNER_B, 7.5f);
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
	Circle.Draw(ScoreboardDecorationColor(ColorRGBA(1.0f, 0.0f, 0.0f, ContentAlpha)), IGraphics::CORNER_ALL, Circle.h / 2.0f);

	Ui()->DoLabel(&TextRect, aBuf, FontSize, TEXTALIGN_ML);
}

void CScoreboard::OnRender()
{
	m_RenderInteractions = false;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	UpdateTeamModeCache();
	UpdateQmAxiomScoreMode();

	if(ShouldHideFocusScoreboard(g_Config.m_QmFocusMode != 0, g_Config.m_QmFocusModeHideScoreboard != 0))
		return;

	// 当记分板可见时（骗你的,不可见也查），为所有活跃玩家触发查询点
	if(HasQmAxiomScoreMode())
	{
		// Axiom 积分服：查当前 Axiom 模式的分数，不再请求 DDNet 在线点数。
		GameClient()->m_QmAxiomScores.SetMode(m_QmAxiomScoreModeFrame);
		// 记分板打开时给足预算铺满一屏，否则只做少量预热；
		// 真正发请求的速度由组件内每帧搜索上限控制。
		int Budget = IsActive() ? MAX_CLIENTS : AXIOM_SCOREBOARD_PREFETCH_BUDGET;
		for(int i = 0; i < MAX_CLIENTS && Budget > 0; i++)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[i] || !GameClient()->m_aClients[i].m_Active)
				continue;
			if(GameClient()->ShouldHideStreamerIdentity(i))
				continue;
			Budget--;
			GameClient()->m_QmAxiomScores.EnsureQueried(GameClient()->m_aClients[i].m_aName);
		}
	}
	else if(g_Config.m_QmScoreboardPoints || g_Config.m_QmScoreboardSortMode)
	{
		GameClient()->m_QmAxiomScores.SetMode(EQmAxiomMode::NONE);
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
	const bool ExtraAnimations = g_Config.m_QmExtraAnimations != 0 && GameClient()->UiRuntimeV2()->Enabled();
	float PanelOffsetY = 0.0f;
	float PanelScale = 1.0f;
	if(!ExtraAnimations)
	{
		if(!WantActive)
		{
			m_OpenTime = 0.0f;
			m_Visibility = 0.0f;
			m_AnimContentAlpha = 0.0f;
			m_RenderInteractions = false;
			return;
		}

		m_OpenTime = 1.0f;
		m_Visibility = 1.0f;
		m_AnimContentAlpha = 1.0f;
	}
	else
	{
		CUiV2AnimationRuntime &AnimRuntime = GameClient()->UiRuntimeV2()->AnimRuntime();
		const uint64_t PanelNode = ScoreboardPresentationNodeKey("panel");
		const SUiSpringConfig Spring = ScoreboardPresentationSpring();
		if(!m_PresentationInitialized)
		{
			SetUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::ALPHA, 0.0f);
			SetUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::COLOR_A, 0.0f);
			SetUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::POS_Y, -10.0f);
			SetUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::SCALE, 0.985f);
			m_PresentationInitialized = true;
		}

		const float TargetVisibility = WantActive ? 1.0f : 0.0f;
		const float TargetOffsetY = WantActive ? 0.0f : -10.0f;
		const float TargetScale = WantActive ? 1.0f : 0.985f;
		m_Visibility = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::ALPHA, TargetVisibility, Spring, 3, 0.004f), 0.0f, 1.0f);
		m_AnimContentAlpha = std::clamp(ResolveUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::COLOR_A, TargetVisibility, ScoreboardContentSpring(WantActive), 3, 0.004f), 0.0f, 1.0f);
		PanelOffsetY = ResolveUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::POS_Y, TargetOffsetY, Spring, 3, 0.01f);
		PanelScale = std::max(0.01f, ResolveUiPresentationStateValue(AnimRuntime, PanelNode, EUiAnimProperty::SCALE, TargetScale, Spring, 3, 0.002f));
		m_OpenTime = m_Visibility;

		if(!WantActive && m_Visibility <= 0.01f && !AnimRuntime.HasActiveAnimation(PanelNode, EUiAnimProperty::ALPHA))
		{
			m_OpenTime = 0.0f;
			m_Visibility = 0.0f;
			m_AnimContentAlpha = 0.0f;
			m_RenderInteractions = false;
			return;
		}
	}

	const bool ScoreboardUiInteractive = WantActive && !GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive();
	m_RenderInteractions = ScoreboardUiInteractive;
	if(ScoreboardUiInteractive)
	{
		Ui()->StartCheck();
		Ui()->Update();
	}

	// 如果记分板处于活动状态，则应同时清除每日公告消息。
	if(WantActive && GameClient()->m_Motd.IsActive())
		GameClient()->m_Motd.Clear();

	const CUIRect Screen = *Ui()->Screen();
	Ui()->MapScreen();

	const float BackgroundAlphaFinal = ExtraAnimations ? m_Visibility : m_AnimContentAlpha;
	const float GaussianBlurAlpha = WantActive ? 1.0f : m_AnimContentAlpha;
	CUiScopedGaussianBlur GaussianBlurScope(Ui(), GaussianBlurAlpha);
	const float ContentOffset = 0.0f;

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool Teams = GameClient()->IsTeamPlay();
	CScoreboardPlayerRowPlan RedPlayerRows;
	CScoreboardPlayerRowPlan BluePlayerRows;
	BuildPlayerRowPlan(TEAM_RED, RedPlayerRows);
	if(Teams)
		BuildPlayerRowPlan(TEAM_BLUE, BluePlayerRows);
	const int NumPlayers = Teams ? maximum(RedPlayerRows.m_Count, BluePlayerRows.m_Count) : RedPlayerRows.m_Count;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;

	// Scoreboard width: clamp to screen width for narrow aspect ratios
	const float ScreenMargin = 10.0f;
	const float MaxScoreboardWidth = maximum(200.0f, Screen.w - ScreenMargin);
	const int ScoreboardColumns = Teams ? 2 : (NumPlayers <= 16 ? 1 : (NumPlayers <= 64 ? 2 : 3));
	const float ClientBrandExtraWidth = g_Config.m_QmClientShowBadge ? maximum(TextRender()->TextWidth(12.0f, "Qm"), TextRender()->TextWidth(12.0f, "Arg")) + CLIENT_BRAND_LABEL_GAP : 0.0f;
	const float BaseScoreboardSmallWidth = (g_Config.m_QmScoreboardPoints ? (450.0f + 10.0f) : 450.0f) + ClientBrandExtraWidth;
	const float ScoreboardSmallWidth = minimum(BaseScoreboardSmallWidth, MaxScoreboardWidth);
	const float BaseScoreboardWidth = !Teams && NumPlayers <= 16 ? ScoreboardSmallWidth : 850.0f + ClientBrandExtraWidth * ScoreboardColumns;
	const float ScoreboardWidth = minimum(BaseScoreboardWidth, MaxScoreboardWidth);
	const float TitleHeight = 30.0f;

	CUIRect Scoreboard = {(Screen.w - ScoreboardWidth) / 2.0f, 75.0f, ScoreboardWidth, 355.0f + TitleHeight};
	if(ExtraAnimations)
	{
		Scoreboard = ScaleRectAroundCenter(Scoreboard, PanelScale);
		Scoreboard.y += PanelOffsetY;
	}
	CUIRect ScoreboardContent = Scoreboard;
	ScoreboardContent.y += ContentOffset;
	CScoreboardRenderState RenderState{};
	static CButtonContainer s_ScoreboardSortButton;
	const float SortButtonFontSize = 10.0f;
	const char *pSortLabel = nullptr;
	if(g_Config.m_QmScoreboardSortMode)
		pSortLabel = Localize("Current: Ranks");
	else
		pSortLabel = TimeScore ? Localize("Current: Time") : Localize("Current: Score");
	const float SortButtonWidth = TextRender()->TextWidth(SortButtonFontSize, pSortLabel) + 18.0f;
	const ColorRGBA SortButtonColor = g_Config.m_QmScoreboardSortMode ? ScoreboardWithUiAlpha(ColorRGBA(0.25f, 0.55f, 0.8f, 0.6f), m_AnimContentAlpha) : ScoreboardUiColorSurface(m_AnimContentAlpha, 0.18f);
	auto &&DoSortButton = [&](CUIRect Rect) {
		Rect.VMargin(4.0f, &Rect);
		Rect.HMargin(6.0f, &Rect);
		if(Ui()->DoButton_PopupMenu(&s_ScoreboardSortButton, pSortLabel, &Rect, SortButtonFontSize, TEXTALIGN_MC, 0.0f, false, m_RenderInteractions, SortButtonColor))
			g_Config.m_QmScoreboardSortMode ^= 1;
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

		RedTitle.Draw(ScoreboardWithUiAlpha(ui_token::color::DANGER, BackgroundAlphaFinal), IGraphics::CORNER_T, ui_token::radius::CARD);
		BlueTitleBackground.Draw(ScoreboardWithUiAlpha(ui_token::color::ACCENT_PRIMARY_DIM, BackgroundAlphaFinal), IGraphics::CORNER_T, ui_token::radius::CARD);
		RedScoreboard.Draw(ScoreboardGlassSurface(BackgroundAlphaFinal), IGraphics::CORNER_B, ui_token::radius::CARD);
		BlueScoreboard.Draw(ScoreboardGlassSurface(BackgroundAlphaFinal), IGraphics::CORNER_B, ui_token::radius::CARD);

		RenderTitleBar(RedTitleContent, TEAM_RED, pRedTeamName == nullptr ? Localize("Red team") : pRedTeamName);
		RenderTitleBar(BlueTitleContent, TEAM_BLUE, pBlueTeamName == nullptr ? Localize("Blue team") : pBlueTeamName);
		DoSortButton(SortButton);
		RenderScoreboard(RedScoreboardContent, TEAM_RED, 0, NumPlayers, RedPlayerRows, RenderState);
		RenderScoreboard(BlueScoreboardContent, TEAM_BLUE, 0, NumPlayers, BluePlayerRows, RenderState);
	}
	else
	{
		Scoreboard.Draw(ScoreboardGlassSurface(BackgroundAlphaFinal), IGraphics::CORNER_ALL, ui_token::radius::CARD);

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
		RenderTitleBar(Title, TEAM_GAME, pTitle);
		DoSortButton(SortButton);

		if(NumPlayers <= 16)
		{
			RenderScoreboard(ScoreboardContentBody, TEAM_GAME, 0, NumPlayers, RedPlayerRows, RenderState);
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
			RenderScoreboard(LeftScoreboard, TEAM_GAME, 0, PlayersPerSide, RedPlayerRows, RenderState);
			RenderScoreboard(RightScoreboard, TEAM_GAME, PlayersPerSide, 2 * PlayersPerSide, RedPlayerRows, RenderState);
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
					RenderScoreboard(Column, TEAM_GAME, i * PlayersPerColumn, (i + 1) * PlayersPerColumn, RedPlayerRows, RenderState);
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

	if(ScoreboardUiInteractive)
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
		if(!GameClient()->m_Snap.m_pLocalCharacter && g_Config.m_QmScoreboardOnDeath &&
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
		const float ActionSpacing = minimum(17.5f, (View.w - (ActionsNum * ActionSize)) / 2);
		const float ActionsWidth = ActionsNum * ActionSize + (ActionsNum - 1) * ActionSpacing;
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
		Container.VMargin(maximum(0.0f, (Container.w - ActionsWidth) * 0.5f), &Container);
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

CUi::EPopupMenuFunctionResult CScoreboard::CMapTitlePopupContext::Render(void *pContext, CUIRect View, bool Active)
{
	CMapTitlePopupContext *pPopupContext = static_cast<CMapTitlePopupContext *>(pContext);
	CScoreboard *pScoreboard = pPopupContext->m_pScoreboard;

	pScoreboard->TextRender()->Text(View.x, View.y, pPopupContext->m_FontSize, pScoreboard->GameClient()->m_aMapDescription, View.w);

	return CUi::POPUP_KEEP_OPEN;
}
