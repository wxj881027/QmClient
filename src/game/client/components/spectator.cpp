/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "spectator.h"

#include "camera.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	uint64_t SpectatorPresentationNodeKey(const char *pScope)
	{
		static const uint64_t s_BaseKey = static_cast<uint64_t>(str_quickhash("qm_extra_spectator_presentation"));
		return BuildUiAnimNodeKey(s_BaseKey, static_cast<uint64_t>(str_quickhash(pScope)));
	}

	SUiSpringConfig SpectatorPresentationSpring()
	{
		SUiSpringConfig Spring;
		Spring.m_Stiffness = 480.0f;
		Spring.m_Damping = 42.0f;
		Spring.m_RestEpsilon = 0.006f;
		Spring.m_RestVelocity = 0.08f;
		return Spring;
	}

	SUiSpringConfig SpectatorContentSpring(bool Opening)
	{
		SUiSpringConfig Spring = SpectatorPresentationSpring();
		if(!Opening)
		{
			constexpr float CloseTimeScale = 0.30f;
			Spring.m_Stiffness /= CloseTimeScale * CloseTimeScale;
			Spring.m_Damping /= CloseTimeScale;
			Spring.m_RestVelocity /= CloseTimeScale;
		}
		return Spring;
	}
} // namespace

bool CSpectator::CanChangeSpectatorId()
{
	// don't change SpectatorId when not spectating
	if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;

	// stop follow mode from changing SpectatorId
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_DemoSpecId == SPEC_FOLLOW)
		return false;

	return true;
}

void CSpectator::SpectateNext(bool Reverse)
{
	int CurIndex = -1;
	const CNetObj_PlayerInfo **paPlayerInfos = GameClient()->m_Snap.m_apInfoByDDTeamName;

	// m_SpectatorId may be uninitialized if m_Active is false
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(paPlayerInfos[i] && paPlayerInfos[i]->m_ClientId == GameClient()->m_Snap.m_SpecInfo.m_SpectatorId)
			{
				CurIndex = i;
				break;
			}
		}
	}

	int Start;
	if(CurIndex != -1)
	{
		if(Reverse)
			Start = CurIndex - 1;
		else
			Start = CurIndex + 1;
	}
	else
	{
		if(Reverse)
			Start = -1;
		else
			Start = 0;
	}

	int Increment = Reverse ? -1 : 1;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		int PlayerIndex = (Start + i * Increment) % MAX_CLIENTS;
		// % in C++ takes the sign of the dividend, not divisor
		if(PlayerIndex < 0)
			PlayerIndex += MAX_CLIENTS;

		const CNetObj_PlayerInfo *pPlayerInfo = paPlayerInfos[PlayerIndex];
		if(pPlayerInfo && pPlayerInfo->m_Team != TEAM_SPECTATORS)
		{
			Spectate(pPlayerInfo->m_ClientId);
			break;
		}
	}
}

void CSpectator::ConKeySpectator(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;

	if(pSelf->GameClient()->m_Scoreboard.IsActive())
		return;

	if(pSelf->GameClient()->m_Snap.m_SpecInfo.m_Active || pSelf->Client()->State() == IClient::STATE_DEMOPLAYBACK)
		pSelf->m_Active = pResult->GetInteger(0) != 0;
	else
		pSelf->m_Active = false;
}

void CSpectator::ConSpectate(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;
	if(!pSelf->CanChangeSpectatorId())
		return;

	pSelf->Spectate(pResult->GetInteger(0));
}

void CSpectator::ConSpectateNext(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;
	if(!pSelf->CanChangeSpectatorId())
		return;

	pSelf->SpectateNext(false);
}

void CSpectator::ConSpectatePrevious(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;
	if(!pSelf->CanChangeSpectatorId())
		return;

	pSelf->SpectateNext(true);
}

void CSpectator::ConSpectateClosest(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;
	pSelf->SpectateClosest();
}

void CSpectator::ConMultiView(IConsole::IResult *pResult, void *pUserData)
{
	CSpectator *pSelf = (CSpectator *)pUserData;
	int Input = pResult->GetInteger(0);

	if(Input == -1)
		std::fill(std::begin(pSelf->GameClient()->m_aMultiViewId), std::end(pSelf->GameClient()->m_aMultiViewId), false); // remove everyone from multiview
	else if(Input < MAX_CLIENTS && Input >= 0)
		pSelf->GameClient()->m_aMultiViewId[Input] = !pSelf->GameClient()->m_aMultiViewId[Input]; // activate or deactivate one player from multiview
}

CSpectator::CSpectator()
{
	m_SelectorMouse = vec2(0.0f, 0.0f);
	OnReset();
}

void CSpectator::OnConsoleInit()
{
	Console()->Register("+spectate", "", CFGFLAG_CLIENT, ConKeySpectator, this, "Open spectator mode selector");
	Console()->Register("spectate", "i[spectator-id]", CFGFLAG_CLIENT, ConSpectate, this, "Switch spectator mode");
	Console()->Register("spectate_next", "", CFGFLAG_CLIENT, ConSpectateNext, this, "Spectate the next player");
	Console()->Register("spectate_previous", "", CFGFLAG_CLIENT, ConSpectatePrevious, this, "Spectate the previous player");
	Console()->Register("spectate_closest", "", CFGFLAG_CLIENT, ConSpectateClosest, this, "Spectate the closest player");
	Console()->Register("spectate_multiview", "i[id]", CFGFLAG_CLIENT, ConMultiView, this, "Add/remove Client-IDs to spectate them exclusively (-1 to reset)");
}

bool CSpectator::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	m_SelectorMouse += vec2(x, y);
	return true;
}

bool CSpectator::OnInput(const IInput::CEvent &Event)
{
	if(IsActive() && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		m_SelectedSpectatorId = NO_SELECTION;
		m_Active = false;
		return true;
	}

	if(g_Config.m_ClSpectatorMouseclicks)
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_Active && !IsActive() && !GameClient()->m_MultiViewActivated &&
			!Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive() && !GameClient()->m_Menus.IsActive())
		{
			if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_MOUSE_1)
			{
				if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
					Spectate(SPEC_FREEVIEW);
				else
					SpectateClosest();
				return true;
			}
		}
	}

	if(GameClient()->m_Camera.SpectatingPlayer() && GameClient()->m_Camera.CanUseAutoSpecCamera())
	{
		if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_MOUSE_2)
		{
			GameClient()->m_Camera.ResetAutoSpecCamera();
			return true;
		}
	}

	return false;
}

void CSpectator::OnRelease()
{
	OnReset();
}

void CSpectator::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!GameClient()->m_MultiViewActivated && m_MultiViewActivateDelay != 0.0f)
	{
		if(m_MultiViewActivateDelay <= Client()->LocalTime())
		{
			m_MultiViewActivateDelay = 0.0f;
			GameClient()->m_MultiViewActivated = true;
		}
	}

	const bool ExtraAnimations = g_Config.m_QmExtraAnimations != 0 && GameClient()->UiRuntimeV2()->Enabled();

	if(!m_Active)
	{
		// closing the spectator menu
		if(m_WasActive)
		{
			if(m_SelectedSpectatorId != NO_SELECTION)
			{
				if(m_SelectedSpectatorId == MULTI_VIEW)
					GameClient()->m_MultiViewActivated = true;
				else if(m_SelectedSpectatorId == SPEC_FREEVIEW || m_SelectedSpectatorId == SPEC_FOLLOW)
					GameClient()->m_MultiViewActivated = false;

				if(!GameClient()->m_MultiViewActivated)
					Spectate(m_SelectedSpectatorId);

				if(GameClient()->m_MultiViewActivated && m_SelectedSpectatorId != MULTI_VIEW && GameClient()->m_Teams.Team(m_SelectedSpectatorId) != GameClient()->m_MultiViewTeam)
				{
					GameClient()->ResetMultiView();
					Spectate(m_SelectedSpectatorId);
					m_MultiViewActivateDelay = Client()->LocalTime() + 0.3f;
				}
			}
			m_WasActive = false;
		}
		if(!ExtraAnimations || !m_PresentationInitialized)
			return;
	}

	if(!GameClient()->m_Snap.m_SpecInfo.m_Active && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		m_Active = false;
		m_WasActive = false;
		if(!ExtraAnimations)
		{
			return;
		}
	}

	const bool WantActive = m_Active;
	float PanelAlpha = WantActive ? 1.0f : 0.0f;
	float ContentAlpha = PanelAlpha;
	float PanelOffsetY = WantActive ? 0.0f : -10.0f;
	CUiV2AnimationRuntime *pAnimRuntime = ExtraAnimations ? &GameClient()->UiRuntimeV2()->AnimRuntime() : nullptr;
	const uint64_t PanelNode = SpectatorPresentationNodeKey("panel");
	if(pAnimRuntime != nullptr)
	{
		if(!m_PresentationInitialized)
		{
			SetUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::ALPHA, 0.0f);
			SetUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::COLOR_A, 0.0f);
			SetUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::POS_Y, -10.0f);
			m_PresentationInitialized = true;
		}
		const SUiSpringConfig Spring = SpectatorPresentationSpring();
		PanelAlpha = std::clamp(ResolveUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::ALPHA, WantActive ? 1.0f : 0.0f, Spring, 3, 0.004f), 0.0f, 1.0f);
		ContentAlpha = std::clamp(ResolveUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::COLOR_A, WantActive ? 1.0f : 0.0f, SpectatorContentSpring(WantActive), 3, 0.004f), 0.0f, 1.0f);
		PanelOffsetY = ResolveUiPresentationStateValue(*pAnimRuntime, PanelNode, EUiAnimProperty::POS_Y, WantActive ? 0.0f : -10.0f, Spring, 3, 0.01f);
		if(!WantActive && PanelAlpha <= 0.01f && !pAnimRuntime->HasActiveAnimation(PanelNode, EUiAnimProperty::ALPHA))
			return;
	}

	if(WantActive)
	{
		m_WasActive = true;
		m_SelectedSpectatorId = NO_SELECTION;
	}

	// draw background
	float Width = 400 * 3.0f * Graphics()->ScreenAspect();
	float Height = 400 * 3.0f;
	float ObjWidth = 300.0f;
	float FontSize = 20.0f;
	float BigFontSize = 20.0f;
	float StartY = -190.0f;
	float LineHeight = 60.0f;
	float TeeSizeMod = 1.0f;
	float RoundRadius = 30.0f;
	bool MultiViewSelected = false;
	int TotalPlayers = 0;
	int PerLine = 8;
	float BoxMove = -10.0f;
	float BoxOffset = 0.0f;

	for(const auto &pInfo : GameClient()->m_Snap.m_apInfoByDDTeamName)
	{
		if(!pInfo || pInfo->m_Team == TEAM_SPECTATORS)
			continue;

		++TotalPlayers;
	}

	if(TotalPlayers > 64)
	{
		FontSize = 12.0f;
		LineHeight = 15.0f;
		TeeSizeMod = 0.3f;
		PerLine = 32;
		RoundRadius = 5.0f;
		BoxMove = 3.0f;
		BoxOffset = 6.0f;
	}
	else if(TotalPlayers > 32)
	{
		FontSize = 18.0f;
		LineHeight = 30.0f;
		TeeSizeMod = 0.7f;
		PerLine = 16;
		RoundRadius = 10.0f;
		BoxMove = 3.0f;
		BoxOffset = 6.0f;
	}
	if(TotalPlayers > 16)
	{
		ObjWidth = 600.0f;
	}

	const vec2 ScreenSize = vec2(Width, Height);
	const float CenterX = Width / 2.0f;
	const float CenterY = Height / 2.0f + PanelOffsetY;
	const vec2 ScreenCenter = vec2(CenterX, CenterY);
	CUIRect SpectatorRect = {CenterX - ObjWidth, CenterY - 300.0f, ObjWidth * 2.0f, 600.0f};
	CUIRect SpectatorMouseRect;
	SpectatorRect.Margin(20.0f, &SpectatorMouseRect);

	const bool WasTouchPressed = m_TouchState.m_AnyPressed;
	if(WantActive)
		Ui()->UpdateTouchState(m_TouchState);
	if(WantActive && m_TouchState.m_AnyPressed)
	{
		const vec2 TouchPos = (m_TouchState.m_PrimaryPosition - vec2(0.5f, 0.5f)) * ScreenSize;
		if(SpectatorMouseRect.Inside(ScreenCenter + TouchPos))
		{
			m_SelectorMouse = TouchPos;
		}
	}
	else if(WantActive && WasTouchPressed)
	{
		const vec2 TouchPos = (m_TouchState.m_PrimaryPosition - vec2(0.5f, 0.5f)) * ScreenSize;
		if(!SpectatorRect.Inside(ScreenCenter + TouchPos))
		{
			OnRelease();
			return;
		}
	}

	Graphics()->MapScreen(0, 0, Width, Height);

	SpectatorRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f * PanelAlpha), IGraphics::CORNER_ALL, 20.0f);

	// clamp mouse position to selector area
	m_SelectorMouse.x = std::clamp(m_SelectorMouse.x, -(ObjWidth - 20.0f), ObjWidth - 20.0f);
	m_SelectorMouse.y = std::clamp(m_SelectorMouse.y, -280.0f, 280.0f);

	const bool MousePressed = WantActive && (Input()->KeyPress(KEY_MOUSE_1) || m_TouchState.m_PrimaryPressed);

	// draw selections
	if((Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_DemoSpecId == SPEC_FREEVIEW) ||
		(Client()->State() != IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW))
	{
		Graphics()->DrawRect(CenterX - (ObjWidth - 20.0f), CenterY - 280.0f, ((ObjWidth * 2.0f) / 3.0f) - 40.0f, 60.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_ALL, 20.0f);
	}

	if(GameClient()->m_MultiViewActivated)
	{
		Graphics()->DrawRect(CenterX - (ObjWidth - 20.0f) + (ObjWidth * 2.0f / 3.0f), CenterY - 280.0f, ((ObjWidth * 2.0f) / 3.0f) - 40.0f, 60.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_ALL, 20.0f);
	}

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_LocalClientId >= 0 && GameClient()->m_DemoSpecId == SPEC_FOLLOW)
	{
		Graphics()->DrawRect(CenterX - (ObjWidth - 20.0f) + (ObjWidth * 2.0f * 2.0f / 3.0f), CenterY - 280.0f, ((ObjWidth * 2.0f) / 3.0f) - 40.0f, 60.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_ALL, 20.0f);
	}

	bool FreeViewSelected = false;
	if(WantActive && m_SelectorMouse.x >= -(ObjWidth - 20.0f) && m_SelectorMouse.x <= -(ObjWidth - 20.0f) + ((ObjWidth * 2.0f) / 3.0f) - 40.0f &&
		m_SelectorMouse.y >= -280.0f && m_SelectorMouse.y <= -220.0f)
	{
		m_SelectedSpectatorId = SPEC_FREEVIEW;
		FreeViewSelected = true;
		if(MousePressed)
		{
			GameClient()->m_MultiViewActivated = false;
			Spectate(m_SelectedSpectatorId);
		}
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, (FreeViewSelected ? 1.0f : 0.5f) * ContentAlpha);
	TextRender()->Text(CenterX - (ObjWidth - 40.0f), CenterY - 280.f + (60.f - BigFontSize) / 2.f, BigFontSize, Localize("Free-View"), -1.0f);

	if(WantActive && m_SelectorMouse.x >= -(ObjWidth - 20.0f) + (ObjWidth * 2.0f / 3.0f) && m_SelectorMouse.x <= -(ObjWidth - 20.0f) + (ObjWidth * 2.0f / 3.0f) + ((ObjWidth * 2.0f) / 3.0f) - 40.0f &&
		m_SelectorMouse.y >= -280.0f && m_SelectorMouse.y <= -220.0f)
	{
		m_SelectedSpectatorId = MULTI_VIEW;
		MultiViewSelected = true;
		if(MousePressed)
		{
			GameClient()->m_MultiViewActivated = true;
		}
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, (MultiViewSelected ? 1.0f : 0.5f) * ContentAlpha);
	TextRender()->Text(CenterX - (ObjWidth - 40.0f) + (ObjWidth * 2.0f / 3.0f), CenterY - 280.f + (60.f - BigFontSize) / 2.f, BigFontSize, Localize("Multi-View"), -1.0f);

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_LocalClientId >= 0)
	{
		bool FollowSelected = false;
		if(WantActive && m_SelectorMouse.x >= -(ObjWidth - 20.0f) + (ObjWidth * 2.0f * 2.0f / 3.0f) && m_SelectorMouse.x <= -(ObjWidth - 20.0f) + (ObjWidth * 2.0f * 2.0f / 3.0f) + ((ObjWidth * 2.0f) / 3.0f) - 40.0f &&
			m_SelectorMouse.y >= -280.0f && m_SelectorMouse.y <= -220.0f)
		{
			m_SelectedSpectatorId = SPEC_FOLLOW;
			FollowSelected = true;
			if(MousePressed)
			{
				GameClient()->m_MultiViewActivated = false;
				Spectate(m_SelectedSpectatorId);
			}
		}
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, (FollowSelected ? 1.0f : 0.5f) * ContentAlpha);
		TextRender()->Text(CenterX - (ObjWidth - 40.0f) + (ObjWidth * 2.0f * 2.0f / 3.0f), CenterY - 280.0f + (60.f - BigFontSize) / 2.f, BigFontSize, Localize("Follow"), -1.0f);
	}

	float x = -(ObjWidth - 35.0f), y = StartY;

	int OldDDTeam = -1;

	// 预先做一次逆序扫描，求出每个下标之后最近的「非观战者」队伍，
	// 取代原先在循环内为每个玩家各做一次前向线性扫描（满员时是 O(N²)）。
	// 语义与逐点前向扫描逐位一致：找不到后续非观战者时取 0（与原 NextDDTeam 初值相同）。
	int aNextDDTeam[MAX_CLIENTS];
	{
		int DDTeamAfter = 0;
		for(int j = MAX_CLIENTS - 1; j >= 0; --j)
		{
			aNextDDTeam[j] = DDTeamAfter;
			const CNetObj_PlayerInfo *pInfoNext = GameClient()->m_Snap.m_apInfoByDDTeamName[j];
			if(pInfoNext != nullptr && pInfoNext->m_Team != TEAM_SPECTATORS)
				DDTeamAfter = GameClient()->m_Teams.Team(pInfoNext->m_ClientId);
		}
	}

	for(int i = 0, Count = 0; i < MAX_CLIENTS; ++i)
	{
		if(!GameClient()->m_Snap.m_apInfoByDDTeamName[i] || GameClient()->m_Snap.m_apInfoByDDTeamName[i]->m_Team == TEAM_SPECTATORS)
			continue;

		++Count;

		if(Count == PerLine + 1 || (Count > PerLine + 1 && (Count - 1) % PerLine == 0))
		{
			x += 290.0f;
			y = StartY;
		}

		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apInfoByDDTeamName[i];
		int DDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
		const int NextDDTeam = aNextDDTeam[i];

		if(OldDDTeam == -1)
		{
			for(int j = i - 1; j >= 0; j--)
			{
				const CNetObj_PlayerInfo *pInfo2 = GameClient()->m_Snap.m_apInfoByDDTeamName[j];

				if(!pInfo2 || pInfo2->m_Team == TEAM_SPECTATORS)
					continue;

				OldDDTeam = GameClient()->m_Teams.Team(pInfo2->m_ClientId);
				break;
			}
		}

		if(DDTeam != TEAM_FLOCK)
		{
			const ColorRGBA Color = GameClient()->GetDDTeamColor(DDTeam).WithAlpha(0.5f * ContentAlpha);
			int Corners = 0;
			if(OldDDTeam != DDTeam)
				Corners |= IGraphics::CORNER_TL | IGraphics::CORNER_TR;
			if(NextDDTeam != DDTeam)
				Corners |= IGraphics::CORNER_BL | IGraphics::CORNER_BR;
			Graphics()->DrawRect(CenterX + x - 10.0f + BoxOffset, CenterY + y + BoxMove, 270.0f - BoxOffset, LineHeight, Color, Corners, RoundRadius);
		}
		OldDDTeam = DDTeam;

		if((Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_DemoSpecId == GameClient()->m_Snap.m_apInfoByDDTeamName[i]->m_ClientId) || (Client()->State() != IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == GameClient()->m_Snap.m_apInfoByDDTeamName[i]->m_ClientId))
		{
			Graphics()->DrawRect(CenterX + x - 10.0f + BoxOffset, CenterY + y + BoxMove, 270.0f - BoxOffset, LineHeight, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ContentAlpha), IGraphics::CORNER_ALL, RoundRadius);
		}

		bool PlayerSelected = false;
		if(WantActive && m_SelectorMouse.x >= x - 10.0f && m_SelectorMouse.x < x + 260.0f &&
			m_SelectorMouse.y >= y - (LineHeight / 6.0f) && m_SelectorMouse.y < y + (LineHeight * 5.0f / 6.0f))
		{
			m_SelectedSpectatorId = GameClient()->m_Snap.m_apInfoByDDTeamName[i]->m_ClientId;
			PlayerSelected = true;
			if(MousePressed)
			{
				if(GameClient()->m_MultiViewActivated)
				{
					if(GameClient()->m_MultiViewTeam == DDTeam)
					{
						GameClient()->m_aMultiViewId[m_SelectedSpectatorId] = !GameClient()->m_aMultiViewId[m_SelectedSpectatorId];
						if(!GameClient()->m_aMultiViewId[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId])
						{
							int NewClientId = GameClient()->FindFirstMultiViewId();
							if(NewClientId < MAX_CLIENTS && NewClientId >= 0)
							{
								GameClient()->CleanMultiViewId(NewClientId);
								GameClient()->m_aMultiViewId[NewClientId] = true;
								Spectate(NewClientId);
							}
						}
					}
					else
					{
						GameClient()->ResetMultiView();
						Spectate(m_SelectedSpectatorId);
						m_MultiViewActivateDelay = Client()->LocalTime() + 0.3f;
					}
				}
				else
				{
					Spectate(m_SelectedSpectatorId);
				}
			}
		}
		float TeeAlpha;
		float NameAlpha;
		if(Client()->State() == IClient::STATE_DEMOPLAYBACK &&
			!GameClient()->m_Snap.m_aCharacters[GameClient()->m_Snap.m_apInfoByDDTeamName[i]->m_ClientId].m_Active)
		{
			NameAlpha = 0.25f;
			TeeAlpha = 0.5f;
		}
		else
		{
			NameAlpha = PlayerSelected ? 1.0f : 0.5f;
			TeeAlpha = 1.0f;
		}
		NameAlpha *= ContentAlpha;
		TeeAlpha *= ContentAlpha;
		CTextCursor NameCursor;
		NameCursor.SetPosition(vec2(CenterX + x + 50.0f, CenterY + y + BoxMove + (LineHeight - FontSize) / 2.f));
		NameCursor.m_FontSize = FontSize;
		NameCursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
		NameCursor.m_LineWidth = 180.0f;
		const int ClientId = GameClient()->m_Snap.m_apInfoByDDTeamName[i]->m_ClientId;
		const bool HideIdentity = GameClient()->ShouldHideStreamerIdentity(ClientId);
		const bool IsFriend = GameClient()->m_aClients[ClientId].m_Friend;
		char aNameBuf[MAX_NAME_LENGTH];
		char aClanBuf[MAX_CLAN_LENGTH];
		GameClient()->FormatStreamerName(ClientId, aNameBuf, sizeof(aNameBuf));
		GameClient()->FormatStreamerClan(ClientId, aClanBuf, sizeof(aClanBuf));
		bool IsSameClan = false;
		if(aClanBuf[0] != '\0')
		{
			const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
			if(LocalClientId >= 0 && str_comp(aClanBuf, GameClient()->m_aClients[LocalClientId].m_aClan) == 0)
			{
				IsSameClan = true;
			}
		}

		ColorRGBA NameColor;
		if(GameClient()->IsLocalClientId(ClientId))
		{
			const float Time = Client()->GlobalTime();
			const float Hue = std::fmod(Time * 0.15f, 1.0f);
			NameColor = color_cast<ColorRGBA>(ColorHSLA(Hue, 0.7f, 0.65f, 1.0f));
		}
		else if(IsFriend)
		{
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
		}
		else if(IsSameClan)
		{
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor));
		}
		else
		{
			NameColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		}
		NameColor.a *= NameAlpha;
		TextRender()->TextColor(NameColor);

		if(g_Config.m_ClShowIds && !HideIdentity)
		{
			char aClientId[16];
			GameClient()->FormatClientId(ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
			TextRender()->TextEx(&NameCursor, aClientId);
		}

		TextRender()->TextEx(&NameCursor, aNameBuf);
		if(GameClient()->m_MultiViewActivated)
		{
			if(GameClient()->m_aMultiViewId[GameClient()->m_Snap.m_apInfoByDDTeamName[i]->m_ClientId])
			{
				TextRender()->TextColor(0.1f, 1.0f, 0.1f, (PlayerSelected ? 1.0f : 0.5f) * ContentAlpha);
				TextRender()->Text(CenterX + x + 50.0f + 180.0f, CenterY + y + BoxMove + (LineHeight - FontSize) / 2.f, FontSize - 3, "⬤", 220.0f);
			}
			else if(GameClient()->m_MultiViewTeam == DDTeam)
			{
				TextRender()->TextColor(1.0f, 0.1f, 0.1f, (PlayerSelected ? 1.0f : 0.5f) * ContentAlpha);
				TextRender()->Text(CenterX + x + 50.0f + 180.0f, CenterY + y + BoxMove + (LineHeight - FontSize) / 2.f, FontSize - 3, "◯", 220.0f);
			}
		}

		// flag
		if(GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) &&
			GameClient()->m_Snap.m_pGameDataObj && (GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierRed == GameClient()->m_Snap.m_apInfoByDDTeamName[i]->m_ClientId || GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierBlue == GameClient()->m_Snap.m_apInfoByDDTeamName[i]->m_ClientId))
		{
			Graphics()->BlendNormal();
			if(GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierBlue == GameClient()->m_Snap.m_apInfoByDDTeamName[i]->m_ClientId)
				Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagBlue);
			else
				Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagRed);

			Graphics()->QuadsBegin();
			Graphics()->QuadsSetSubset(1, 0, 0, 1);

			float Size = LineHeight;
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, ContentAlpha);
			IGraphics::CQuadItem QuadItem(CenterX + x - LineHeight / 5.0f, CenterY + y - LineHeight / 3.0f, Size / 2.0f, Size);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		}

		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[GameClient()->m_Snap.m_apInfoByDDTeamName[i]->m_ClientId].m_RenderInfo;
		TeeInfo.m_Size *= TeeSizeMod;

		const CAnimState *pIdleState = CAnimState::GetIdle();
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
		vec2 TeeRenderPos(CenterX + x + 20.0f, CenterY + y + BoxMove + LineHeight / 2.0f + OffsetToMid.y);

		RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos, TeeAlpha);

		float IconX = CenterX + x - TeeInfo.m_Size / 2.0f;
		const float IconY = CenterY + y + BoxMove + (LineHeight - FontSize) / 2.0f;
		const float IconSize = FontSize >= 10.0f ? FontSize - 2.0f : FontSize;
		if(IsFriend)
		{
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			ColorRGBA FriendIconColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendHeartColor));
			FriendIconColor.a *= NameAlpha;
			TextRender()->TextColor(FriendIconColor);
			TextRender()->Text(IconX, IconY, IconSize, FontIcons::FONT_ICON_HEART, 220.0f);
			IconX += IconSize - 2.0f;
		}

		if(IsSameClan)
		{
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			ColorRGBA TeamIconColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor));
			TeamIconColor.a *= NameAlpha;
			TextRender()->TextColor(TeamIconColor);
			TextRender()->Text(IconX, IconY, IconSize, FontIcons::FONT_ICON_USERS, 220.0f);
		}
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		y += LineHeight;
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	RenderTools()->RenderCursor(ScreenCenter + m_SelectorMouse, 48.0f, ContentAlpha);
}

void CSpectator::OnReset()
{
	m_WasActive = false;
	m_Active = false;
	m_PresentationInitialized = false;
	m_SelectedSpectatorId = NO_SELECTION;
	m_MultiViewActivateDelay = 0.0f;
	m_TouchState = {};
}

void CSpectator::Spectate(int SpectatorId)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		GameClient()->m_DemoSpecId = std::clamp(SpectatorId, (int)SPEC_FOLLOW, MAX_CLIENTS - 1);
		// The tick must be rendered for the spectator mode to be updated, so we do it manually when demo playback is paused
		// TODO: https://github.com/ddnet/ddnet/issues/11681
		if(DemoPlayer()->BaseInfo()->m_Paused)
			GameClient()->m_Menus.DemoSeekTick(IDemoPlayer::TICK_CURRENT);
		return;
	}

	if(GameClient()->m_FastPractice.ConsumeSpectatorCommand())
		return;

	if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SpectatorId)
		return;

	if(Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_SetSpectatorMode Msg;
		if(SpectatorId == SPEC_FREEVIEW)
		{
			Msg.m_SpecMode = protocol7::SPEC_FREEVIEW;
			Msg.m_SpectatorId = -1;
		}
		else
		{
			Msg.m_SpecMode = protocol7::SPEC_PLAYER;
			Msg.m_SpectatorId = SpectatorId;
		}
		Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL, true);
		return;
	}
	CNetMsg_Cl_SetSpectatorMode Msg;
	Msg.m_SpectatorId = SpectatorId;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
}

void CSpectator::SpectateClosest()
{
	if(!CanChangeSpectatorId())
		return;

	const CGameClient::CSnapState &Snap = GameClient()->m_Snap;
	int SpectatorId = Snap.m_SpecInfo.m_SpectatorId;

	int NewSpectatorId = -1;

	vec2 CurPosition = GameClient()->m_Camera.m_Center;
	if(SpectatorId != SPEC_FREEVIEW)
	{
		const CNetObj_Character &CurCharacter = Snap.m_aCharacters[SpectatorId].m_Cur;
		CurPosition = vec2(CurCharacter.m_X, CurCharacter.m_Y);
	}

	int ClosestDistance = std::numeric_limits<int>::max();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(ClientId == SpectatorId || !Snap.m_aCharacters[ClientId].m_Active || !Snap.m_apPlayerInfos[ClientId] || Snap.m_apPlayerInfos[ClientId]->m_Team == TEAM_SPECTATORS)
			continue;

		if(Client()->State() != IClient::STATE_DEMOPLAYBACK && ClientId == Snap.m_LocalClientId)
			continue;

		const CNetObj_Character &MaybeClosestCharacter = Snap.m_aCharacters[ClientId].m_Cur;
		int Distance = distance(CurPosition, vec2(MaybeClosestCharacter.m_X, MaybeClosestCharacter.m_Y));
		if(NewSpectatorId == -1 || Distance < ClosestDistance)
		{
			NewSpectatorId = ClientId;
			ClosestDistance = Distance;
		}
	}
	if(NewSpectatorId > -1)
		Spectate(NewSpectatorId);
}
