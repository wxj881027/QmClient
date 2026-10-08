#include "pet.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>

void CPet::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		m_FollowedClientId = -1;
		return;
	}
	if(g_Config.m_QmPetShow <= 0)
	{
		m_FollowedClientId = -1;
		return;
	}

	const int PlayerId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(PlayerId < 0)
	{
		m_FollowedClientId = -1;
		return;
	}
	m_FollowedClientId = PlayerId;
	const auto &Player = GameClient()->m_aClients[PlayerId];
	const float Delta = Client()->RenderFrameTime();
	const float Now = (float)Client()->GameTick(g_Config.m_ClDummy) / (float)Client()->GameTickSpeed();

	const float Scale = (float)g_Config.m_QmPetSize / 100.0f;

	if(Player.m_Active && Player.m_Team != TEAM_SPECTATORS)
	{
		m_Target = Player.m_RenderPos;
		m_Target.x += (64.0f + Scale * 32.0f) * (m_Position.x > m_Target.x ? 1 : -1);
		m_Target.y -= 100.0f + Scale * 32.0f;
		m_Target.y += std::sin(Now / 2.0f) * 8.0f;
		if(m_Alpha == 0.0f)
		{
			m_Position = m_Target;
			m_Velocity = vec2(0.0f, 0.0f);
			m_Dir = random_direction();
		}
		if(m_Alpha < 1.0f)
		{
			m_Alpha += Delta;
			if(m_Alpha >= 1.0f)
				m_Alpha = 1.0f;
		}
		QmTClientPetAdvanceSpring(m_Position, m_Velocity, m_Target, Delta);
		if(distance(m_Target, m_Position) > 512.0f)
			m_Alpha = 0.01f;
	}
	else
	{
		if(m_Alpha > 0.0f)
		{
			m_Alpha -= Delta;
			if(m_Alpha <= 0.0f)
				m_Alpha = 0.0f;
		}
	}

	if(m_Alpha <= 0.0f)
		return;

	const auto &Character = Player.m_RenderCur;

	vec2 DirTarget;
	int Emote = 0;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		DirTarget = GameClient()->m_Camera.m_Center - m_Target;
		if(length(DirTarget) > 1.0f)
			DirTarget = normalize(DirTarget);
	}
	else
	{
		Emote = Character.m_Emote;
		vec2 DirMouse = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
		if(length(DirMouse) > 1.0f)
			DirMouse = normalize(DirMouse);
		vec2 DirVel = m_Velocity;
		if(length(DirVel) > 1.0f)
			DirVel = normalize(DirVel);
		DirTarget = mix(DirMouse, DirVel, std::clamp(length(m_Velocity) / 32.0f, 0.0f, 1.0f));
	}
	m_Dir = (DirTarget + m_Dir) / 2.0f; // TODO: stop being lazy

	CTeeRenderInfo TeeRenderInfo;
	TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_QmPetSkin));
	// TeeRenderInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
	TeeRenderInfo.m_Size = 64.0f * Scale;
	TeeRenderInfo.m_GotAirJump = m_Velocity.y > -10.0f;
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, Emote, m_Dir, m_Position, m_Alpha * (float)g_Config.m_QmPetAlpha / 100.0f);
}

void CPet::OnMapLoad()
{
	m_Alpha = 0.0f;
	m_FollowedClientId = -1;
}
