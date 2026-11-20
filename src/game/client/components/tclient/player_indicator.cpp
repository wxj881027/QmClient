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
	if(GameClient()->ReplaySessionStatus().ViewMode())
		return;
	if(g_Config.m_QmPlayerIndicator != 1)
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
	if(!(GameClient()->m_Teams.Team(LocalClientId) == 0 && g_Config.m_QmIndicatorTeamOnly))
	{
		const bool HideVisible = g_Config.m_QmIndicatorHideVisible != 0;
		float ScreenX0 = 0.0f;
		float ScreenY0 = 0.0f;
		float ScreenX1 = 0.0f;
		float ScreenY1 = 0.0f;
		if(HideVisible)
			Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

		auto &WarList = GameClient()->m_WarList;
		const bool UseWarList = g_Config.m_QmWarListIndicator != 0;
		const CWarType *pTeamType = UseWarList && g_Config.m_QmWarListIndicatorTeam ?
						    WarList.FindActiveBuiltinWarType(WARLIST_BUILTIN_GROUP_TEAM) :
						    nullptr;
		const CWarType *pEnemyType = UseWarList && g_Config.m_QmWarListIndicatorEnemy ?
						     WarList.FindActiveBuiltinWarType(WARLIST_BUILTIN_GROUP_ENEMY) :
						     nullptr;
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
				if(g_Config.m_QmPlayerIndicatorFreeze && !(OtherTee.m_FreezeEnd > 0 || OtherTee.m_DeepFrozen))
					continue;

				// Hide tees on our screen if the config is set to do so
				if(HideVisible && in_range(OtherTee.m_RenderPos.x, ScreenX0, ScreenX1) && in_range(OtherTee.m_RenderPos.y, ScreenY0, ScreenY1))
					continue;

				const vec2 Norm = DirectionTo(OtherTee.m_RenderPos, Position) * (-1.0f);

				float Offset = g_Config.m_QmIndicatorOffset;
				if(g_Config.m_QmIndicatorVariableDistance && g_Config.m_QmIndicatorMaxDistance > 0)
				{
					Offset = mix((float)g_Config.m_QmIndicatorOffset, (float)g_Config.m_QmIndicatorOffsetMax,
						std::min(distance(Position, OtherTee.m_RenderPos) / (float)g_Config.m_QmIndicatorMaxDistance, 1.0f));
				}

				vec2 IndicatorPos(Norm.x * Offset + Position.x, Norm.y * Offset + Position.y);
				CTeeRenderInfo TeeInfo = OtherTee.m_RenderInfo;
				float Alpha = g_Config.m_QmIndicatorOpacity / 100.0f;
				if(OtherTee.m_FreezeEnd > 0 || OtherTee.m_DeepFrozen)
				{
					// check if player is frozen or is getting saved
					if(OtherCharacter.m_IsInFreeze == 0)
					{
						// player is on the way to get free again
						Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmIndicatorSaved));
					}
					else
					{
						// player is frozen
						Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmIndicatorFreeze));
					}
					if(g_Config.m_QmIndicatorTees)
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
					Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmIndicatorAlive));
				}
				bool HideIfNotWar = false;
				ColorRGBA PrevCol = Col;
				if(UseWarList)
				{
					HideIfNotWar = true;
					if(g_Config.m_QmWarListIndicatorAll && WarList.GetAnyWar(i))
					{
						Col = WarList.GetPriorityColor(i);
						HideIfNotWar = false;
					}

					const auto &vMatches = WarList.GetWarData(i).m_WarGroupMatches;
					const auto ApplyGroup = [&](const CWarType *pType) {
						if(pType == nullptr)
							return;
						// 槽位与匹配缓存由战争名单在同一次快照更新中刷新。
						const size_t Index = static_cast<size_t>(pType->m_Index);
						if(Index < vMatches.size() && vMatches[Index])
						{
							Col = pType->m_Color;
							HideIfNotWar = false;
						}
					};
					ApplyGroup(pTeamType);
					ApplyGroup(pEnemyType);
				}

				if(HideIfNotWar)
					continue;
				if(!g_Config.m_QmWarListIndicatorColors)
					Col = PrevCol;

				Col.a = Alpha;

				TeeInfo.m_Size = g_Config.m_QmIndicatorRadius * 4.0f;

				if(g_Config.m_QmIndicatorTees)
				{
					RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, OtherTee.m_RenderCur.m_Emote, vec2(1.0f, 0.0f), IndicatorPos, Col.a);
				}
				else
				{
					Graphics()->QuadsBegin();
					Graphics()->SetColor(Col);
					Graphics()->DrawCircle(IndicatorPos.x, IndicatorPos.y, g_Config.m_QmIndicatorRadius, 16);
					Graphics()->QuadsEnd();
				}
			}
		}
	}

	// reset texture color
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
