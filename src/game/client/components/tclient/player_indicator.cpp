#include "player_indicator.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>

static vec2 DirectionTo(vec2 Src, vec2 Dst)
{
	const vec2 Delta = vec2(Dst.x - Src.x, Dst.y - Src.y);
	const float DeltaLength = length(Delta);
	if(DeltaLength <= 0.0001f)
		return vec2(0.0f, 0.0f);
	return Delta / DeltaLength;
}

void CPlayerIndicator::OnRender()
{
	if(g_Config.m_TcPlayerIndicator != 1)
		return;

	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;

	// Don't render if we can't find our own tee
	if(LocalClientId == -1 || !GameClient()->m_Snap.m_aCharacters[LocalClientId].m_Active)
		return;

	// Don't render if not race gamemode or in demo
	if(!GameClient()->m_GameInfo.m_Race || Client()->State() == IClient::STATE_DEMOPLAYBACK || !GameClient()->m_Camera.ZoomAllowed())
		return;

	const CGameClient::CClientData &LocalClient = GameClient()->m_aClients[LocalClientId];
	const vec2 Position = LocalClient.m_RenderPos;

	Graphics()->TextureClear();
	ColorRGBA Col = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
	if(!(GameClient()->m_Teams.Team(LocalClientId) == 0 && g_Config.m_TcIndicatorTeamOnly))
	{
		const bool HideVisible = g_Config.m_TcIndicatorHideVisible != 0;
		float ScreenX0 = 0.0f;
		float ScreenY0 = 0.0f;
		float ScreenX1 = 0.0f;
		float ScreenY1 = 0.0f;
		if(HideVisible)
			Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[i] || i == LocalClientId)
				continue;

			const CGameClient::CClientData &OtherTee = GameClient()->m_aClients[i];
			const CCharacterCore &OtherCharacter = OtherTee.m_Predicted;
			if(
				OtherTee.m_Team == LocalClient.m_Team &&
				!OtherTee.m_Spec &&
				GameClient()->m_Snap.m_aCharacters[i].m_Active)
			{
				if(g_Config.m_TcPlayerIndicatorFreeze && !(OtherTee.m_FreezeEnd > 0 || OtherTee.m_DeepFrozen))
					continue;

				// Hide tees on our screen if the config is set to do so
				if(HideVisible && in_range(OtherTee.m_RenderPos.x, ScreenX0, ScreenX1) && in_range(OtherTee.m_RenderPos.y, ScreenY0, ScreenY1))
					continue;

				const vec2 Norm = DirectionTo(OtherTee.m_RenderPos, Position) * (-1.0f);

				float Offset = g_Config.m_TcIndicatorOffset;
				if(g_Config.m_TcIndicatorVariableDistance && g_Config.m_TcIndicatorMaxDistance > 0)
				{
					Offset = mix((float)g_Config.m_TcIndicatorOffset, (float)g_Config.m_TcIndicatorOffsetMax,
						std::min(distance(Position, OtherTee.m_RenderPos) / (float)g_Config.m_TcIndicatorMaxDistance, 1.0f));
				}

				vec2 IndicatorPos(Norm.x * Offset + Position.x, Norm.y * Offset + Position.y);
				CTeeRenderInfo TeeInfo = OtherTee.m_RenderInfo;
				float Alpha = g_Config.m_TcIndicatorOpacity / 100.0f;
				if(OtherTee.m_FreezeEnd > 0 || OtherTee.m_DeepFrozen)
				{
					// check if player is frozen or is getting saved
					if(OtherCharacter.m_IsInFreeze == 0)
					{
						// player is on the way to get free again
						Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcIndicatorSaved));
					}
					else
					{
						// player is frozen
						Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcIndicatorFreeze));
					}
					if(g_Config.m_TcIndicatorTees)
					{
						TeeInfo.m_ColorBody.r *= 0.4f;
						TeeInfo.m_ColorBody.g *= 0.4f;
						TeeInfo.m_ColorBody.b *= 0.4f;
						TeeInfo.m_ColorFeet.r *= 0.4f;
						TeeInfo.m_ColorFeet.g *= 0.4f;
						TeeInfo.m_ColorFeet.b *= 0.4f;
						Alpha *= 0.8f;
					}
				}
				else
				{
					Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcIndicatorAlive));
				}
				bool HideIfNotWar = false;
				ColorRGBA PrevCol = Col;
				if(g_Config.m_TcWarListIndicator)
				{
					HideIfNotWar = true;
					if(g_Config.m_TcWarListIndicatorAll)
					{
						if(GameClient()->m_WarList.GetAnyWar(i))
						{
							Col = GameClient()->m_WarList.GetPriorityColor(i);
							HideIfNotWar = false;
						}
					}
					if(g_Config.m_TcWarListIndicatorTeam)
					{
						if(GameClient()->m_WarList.GetWarData(i).m_WarGroupMatches[2])
						{
							Col = GameClient()->m_WarList.m_WarTypes[2]->m_Color;
							HideIfNotWar = false;
						}
					}
					if(g_Config.m_TcWarListIndicatorEnemy)
					{
						if(GameClient()->m_WarList.GetWarData(i).m_WarGroupMatches[1])
						{
							Col = GameClient()->m_WarList.m_WarTypes[1]->m_Color;
							HideIfNotWar = false;
						}
					}
				}

				if(HideIfNotWar)
					continue;
				if(!g_Config.m_TcWarListIndicatorColors)
					Col = PrevCol;

				Col.a = Alpha;

				TeeInfo.m_Size = g_Config.m_TcIndicatorRadius * 4.0f;

				if(g_Config.m_TcIndicatorTees)
				{
					RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, OtherTee.m_RenderCur.m_Emote, vec2(1.0f, 0.0f), IndicatorPos, Col.a);
				}
				else
				{
					Graphics()->QuadsBegin();
					Graphics()->SetColor(Col);
					Graphics()->DrawCircle(IndicatorPos.x, IndicatorPos.y, g_Config.m_TcIndicatorRadius, 16);
					Graphics()->QuadsEnd();
				}
			}
		}
	}

	// reset texture color
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
