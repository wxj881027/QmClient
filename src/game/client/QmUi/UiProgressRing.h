#ifndef GAME_CLIENT_QMUI_UIPROGRESSRING_H
#define GAME_CLIENT_QMUI_UIPROGRESSRING_H

#include "QmPieMenuRender.h"
#include "UiTokens.h"

#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

namespace ui_widget
{

	struct SProgressRingOptions
	{
		float m_Progress = -1.0f;
		ColorRGBA m_Color = ui_token::color::NEUTRAL_WHITE;
	};

	struct SProgressRingArc
	{
		float m_Start = -pi * 0.5f;
		float m_Sweep = 0.0f;
		bool m_DrawTrack = false;
	};

	inline SProgressRingArc ResolveProgressRingArc(float Progress, int64_t Now, bool Animate)
	{
		SProgressRingArc Arc;
		if(!std::isfinite(Progress))
			return Arc;
		if(Progress >= 0.0f)
		{
			Arc.m_Sweep = std::min(Progress, 1.0f) * 2.0f * pi;
			Arc.m_DrawTrack = Arc.m_Sweep < 2.0f * pi;
		}
		else
		{
			Arc.m_Sweep = ui_token::progress_ring::BUSY_FRACTION * 2.0f * pi;
			if(Animate)
			{
				const int64_t Period = ui_token::motion::PROGRESS_RING_PERIOD_NS;
				const int64_t Phase = (Now % Period + Period) % Period;
				Arc.m_Start += (float)Phase / (float)Period * 2.0f * pi;
			}
		}
		return Arc;
	}

	inline void DrawProgressRing(CUi *pUi, vec2 Center, float Radius, const SProgressRingOptions &Options = {})
	{
		const float Diameter = Radius * 2.0f;
		if(pUi->DrawingSuppressed() || !(Radius > 0.0f) || !std::isfinite(Diameter) || !std::isfinite(Center.x) || !std::isfinite(Center.y) || !(Options.m_Color.a > 0.0f))
			return;

		const auto Arc = ResolveProgressRingArc(Options.m_Progress, Options.m_Progress < 0.0f ? time_get() : 0, g_Config.m_QmUiMotionLevel != 0);
		constexpr float OuterRadius = ui_token::progress_ring::OUTER_RADIUS;
		constexpr float InnerRadius = OuterRadius * ui_token::progress_ring::INNER_RATIO;
		IGraphics *pGraphics = pUi->Graphics();
		const bool Sdf = pGraphics->HasProceduralRing();
		const auto Draw = [&](float Start, float Sweep, ColorRGBA Color) {
			if(Sweep <= 0.0f)
				return;
			if(Sdf)
			{
				IGraphics::SProceduralRingParams Params;
				Params.m_Rect = vec4(Center.x - Radius, Center.y - Radius, Diameter, Diameter);
				Params.m_Color = Color;
				Params.m_RingInnerRadius = InnerRadius;
				Params.m_RingOuterRadius = OuterRadius;
				Params.m_RingStartAngle = Start;
				Params.m_RingEndAngle = Start + Sweep;
				pGraphics->RenderProceduralRing(Params);
			}
			else
			{
				constexpr float DegreesPerRadian = 180.0f / pi;
				qm_pie_menu_ui::DrawSector(pGraphics, Center, Diameter * InnerRadius, Diameter * OuterRadius, Start * DegreesPerRadian, (Start + Sweep) * DegreesPerRadian, 360.0f, Color);
			}
		};
		if(Arc.m_DrawTrack)
			Draw(0.0f, 2.0f * pi, Options.m_Color.WithMultipliedAlpha(ui_token::progress_ring::TRACK_ALPHA));
		Draw(Arc.m_Start, Arc.m_Sweep, Options.m_Color);
	}

} // namespace ui_widget

#endif
