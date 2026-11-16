#include "checkpoint_guide.h"

#include "modes.h"

#include <base/color.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/client/enums.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiTokens.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
	bool IsGuideSwitchActive(unsigned char Number, void *pUser)
	{
		auto *pCharacter = static_cast<CCharacter *>(pUser);
		const auto &vSwitchers = pCharacter->Switchers();
		const int Team = pCharacter->Team();
		return Number < vSwitchers.size() && Team >= 0 && Team < NUM_DDRACE_TEAMS && Team != pCharacter->TeamsCore()->TeamSuper() && vSwitchers[Number].m_aStatus[Team];
	}
}

void CQmCheckpointGuide::OnReset()
{
	for(auto &Player : m_aPlayers)
	{
		Player.m_Planner.Reset();
		Player.m_ClientId = -1;
		Player.m_NextSearch = 0;
	}
	m_WasEnabled = false;
}

void CQmCheckpointGuide::OnMapLoad()
{
	OnReset();
}

void CQmCheckpointGuide::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE)
		OnReset();
}

void CQmCheckpointGuide::ObservePlayers()
{
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		auto &Player = m_aPlayers[Dummy];
		const int ClientId = GameClient()->m_aLocalIds[Dummy];
		if((Dummy != 0 && !Client()->DummyConnected()) || ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		{
			Player.m_Planner.Reset();
			Player.m_ClientId = -1;
			Player.m_NextSearch = 0;
			continue;
		}
		if(Player.m_ClientId != ClientId)
		{
			Player.m_Planner.Reset();
			Player.m_ClientId = ClientId;
			Player.m_NextSearch = 0;
		}
		const auto &Character = GameClient()->m_Snap.m_aCharacters[ClientId];
		int Checkpoint = -1;
		if(Character.m_HasExtendedData)
			Checkpoint = Character.m_ExtendedData.m_TeleCheckpoint;
		else if(const CCharacter *pCharacter = GameClient()->m_PredictedWorld.GetCharacterById(ClientId))
			Checkpoint = pCharacter->m_TeleCheckpoint;
		if(Player.m_Planner.Checkpoint() != Checkpoint)
			Player.m_NextSearch = 0;
		Player.m_Planner.ObserveCheckpoint(Checkpoint);
	}
}

void CQmCheckpointGuide::OnNewSnapshot()
{
	ObservePlayers();
}

void CQmCheckpointGuide::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType != NETMSGTYPE_SV_KILLMSG && MsgType != NETMSGTYPE_SV_KILLMSGTEAM)
		return;
	for(auto &Player : m_aPlayers)
	{
		if(Player.m_ClientId < 0)
			continue;
		const bool Killed = MsgType == NETMSGTYPE_SV_KILLMSG ?
					    Player.m_ClientId == static_cast<const CNetMsg_Sv_KillMsg *>(pRawMsg)->m_Victim :
					    GameClient()->m_Teams.Team(Player.m_ClientId) == static_cast<const CNetMsg_Sv_KillMsgTeam *>(pRawMsg)->m_Team;
		if(Killed)
		{
			Player.m_Planner.Reset();
			Player.m_NextSearch = 0;
		}
	}
}

void CQmCheckpointGuide::OnRender()
{
	if(!g_Config.m_QmCheckpointGuide)
	{
		m_WasEnabled = false;
		return;
	}
	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_GameInfo.m_Race || GameClient()->m_Snap.m_SpecInfo.m_Active ||
		GameClient()->m_RankGhost.IsViewModeActive() || GetQmFocusModeDecisions().m_HideGuideLines)
		return;
	if(!m_WasEnabled)
	{
		for(auto &Player : m_aPlayers)
			Player.m_NextSearch = 0;
		m_WasEnabled = true;
	}
	ObservePlayers();
	const int Dummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	auto &Player = m_aPlayers[Dummy];
	if(Player.m_ClientId < 0)
		return;
	CCollision *pCollision = Collision();
	if(!pCollision || !pCollision->GameLayer() || !pCollision->TeleLayer())
		return;
	const vec2 Position = GameClient()->m_aClients[Player.m_ClientId].m_RenderPos;
	const int StartIndex = pCollision->GetPureMapIndex(Position);
	const int64_t Now = time_get();
	if(Now >= Player.m_NextSearch)
	{
		CCharacter *pCharacter = GameClient()->m_GameWorld.GetCharacterById(Player.m_ClientId);
		if(!pCharacter)
			pCharacter = GameClient()->m_PredictedWorld.GetCharacterById(Player.m_ClientId);
		if(!pCharacter)
			return;
		Player.m_Planner.Update(pCollision->GetWidth(), pCollision->GetHeight(), StartIndex, [&](int Index) {
			const CTeleTile &Tele = pCollision->TeleLayer()[Index];
			const int Restrictions = pCollision->GetMoveRestrictions(IsGuideSwitchActive, pCharacter, pCollision->GetPos(Index), 18.0f, Index);
			return QmMapProgress::MakeTile(pCollision->GameLayer()[Index].m_Index,
				pCollision->FrontLayer() ? pCollision->FrontLayer()[Index].m_Index : TILE_AIR,
				Tele.m_Type, Tele.m_Number, Restrictions);
		});
		Player.m_NextSearch = Now + time_freq() / 10;
	}
	const auto &Result = Player.m_Planner.Result();
	if(Result.m_TargetIndex < 0)
		return;
	const auto Begin = std::find(Result.m_vRoute.begin(), Result.m_vRoute.end(), StartIndex);
	// 移出已计算的路线后等待下一轮局部搜索，不把旧路线直接接过墙角。
	if(Begin == Result.m_vRoute.end())
		return;
	const ColorRGBA Color = Result.m_Warning ? ui_token::color::WARNING : ui_token::color::ACCENT_PRIMARY;
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Color.WithAlpha(0.85f));
	vec2 From = Position;
	for(auto It = Begin; It != Result.m_vRoute.end(); ++It)
	{
		const vec2 To = pCollision->GetPos(*It);
		const vec2 Delta = To - From;
		if(length_squared(Delta) > 0.01f)
		{
			const vec2 Normal = vec2(-Delta.y, Delta.x) / length(Delta) * (Result.m_Warning ? 2.5f : 1.5f);
			IGraphics::CFreeformItem Segment(From + Normal, To + Normal, From - Normal, To - Normal);
			Graphics()->QuadsDrawFreeform(&Segment, 1);
		}
		From = To;
	}
	const vec2 Target = pCollision->GetPos(Result.m_TargetIndex);
	const float Pulse = Result.m_Warning ? 2.0f * std::sin((float)(Now % time_freq()) / time_freq() * 2.0f * pi) : 0.0f;
	const float Half = 18.0f + Pulse;
	IGraphics::CQuadItem aBorder[] = {
		{Target.x - Half, Target.y - Half, Half * 2.0f, 3.0f},
		{Target.x - Half, Target.y + Half - 3.0f, Half * 2.0f, 3.0f},
		{Target.x - Half, Target.y - Half, 3.0f, Half * 2.0f},
		{Target.x + Half - 3.0f, Target.y - Half, 3.0f, Half * 2.0f},
	};
	Graphics()->QuadsDrawTL(aBorder, (int)std::size(aBorder));
	Graphics()->QuadsEnd();
	char aText[256];
	str_format(aText, sizeof(aText), Localize("CP %d"), Result.m_Checkpoint);
	const ColorRGBA OldText = TextRender()->GetTextColor();
	const ColorRGBA OldOutline = TextRender()->GetTextOutlineColor();
	TextRender()->TextColor(Color);
	TextRender()->TextOutlineColor(ui_token::color::NEUTRAL_BLACK.WithAlpha(0.8f));
	TextRender()->Text(Target.x - TextRender()->TextWidth(14.0f, aText) / 2.0f, Target.y - 38.0f, 14.0f, aText);
	if(Result.m_Warning)
	{
		str_format(aText, sizeof(aText), Localize("Collect CP %d before teleporting"), Result.m_Checkpoint);
		TextRender()->Text(Position.x - TextRender()->TextWidth(16.0f, aText) / 2.0f, Position.y - 66.0f, 16.0f, aText);
	}
	TextRender()->TextColor(OldText);
	TextRender()->TextOutlineColor(OldOutline);
}
