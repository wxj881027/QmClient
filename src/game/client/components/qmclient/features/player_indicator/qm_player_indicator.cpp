/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "qm_player_indicator.h"

#include "qm_player_indicator_logic.h"

#include <base/color.h>

#include <engine/graphics.h>

#include <game/client/animstate.h>
#include <game/client/render.h>

void CQmPlayerIndicator::Render(const SQmPlayerIndicatorFrame &Frame, IGraphics *pGraphics, CRenderTools *pRenderTools) const
{
	if(!Frame.m_Settings.m_Enabled || !pGraphics || !pRenderTools)
		return;

	pGraphics->TextureClear();
	for(const SQmPlayerIndicatorPlayer &Player : Frame.m_aPlayers)
	{
		const SQmPlayerIndicatorCandidate Candidate{
			.m_OtherActive = Player.m_Active,
			.m_OtherIsLocal = Player.m_IsLocal,
			.m_OtherSpectator = Player.m_Spectator,
			.m_OtherTeam = Player.m_Team,
			.m_LocalTeam = Frame.m_LocalTeam,
			.m_LocalRaceTeam = Frame.m_LocalRaceTeam,
			.m_TeamOnly = Frame.m_Settings.m_TeamOnly,
			.m_FrozenOnly = Frame.m_Settings.m_FrozenOnly,
			.m_OtherFrozen = Player.m_Frozen,
			.m_HideVisible = Frame.m_Settings.m_HideVisible,
			.m_OtherVisible = Frame.m_Settings.m_HideVisible && Frame.m_Screen.Inside(Player.m_Position),
		};
		if(!QmPlayerIndicatorShouldRender(Candidate))
			continue;

		const float Distance = distance(Frame.m_LocalPosition, Player.m_Position);
		const float Offset = QmPlayerIndicatorOffset(
			Frame.m_Settings.m_Offset,
			Frame.m_Settings.m_OffsetMax,
			Frame.m_Settings.m_VariableDistance,
			Frame.m_Settings.m_MaxDistance,
			Distance);
		const vec2 IndicatorPosition = QmPlayerIndicatorPosition(Frame.m_LocalPosition, Player.m_Position, Offset);
		const int IndicatorColor = Player.m_Frozen ? Frame.m_Settings.m_FrozenColor : Frame.m_Settings.m_AliveColor;
		ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(IndicatorColor));
		Color.a = Frame.m_Settings.m_Opacity / 100.0f;

		if(Frame.m_Settings.m_UseTees && Player.m_pRenderInfo)
		{
			CTeeRenderInfo TeeInfo = *Player.m_pRenderInfo;
			TeeInfo.m_Size = Frame.m_Settings.m_Radius * 4.0f;
			pRenderTools->RenderTee(CAnimState::GetIdle(), &TeeInfo, Player.m_Emote, vec2(1.0f, 0.0f), IndicatorPosition, Color.a);
		}
		else
		{
			pGraphics->QuadsBegin();
			pGraphics->SetColor(Color);
			pGraphics->DrawCircle(IndicatorPosition.x, IndicatorPosition.y, Frame.m_Settings.m_Radius, 16);
			pGraphics->QuadsEnd();
		}
	}

	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
