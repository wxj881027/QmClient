#ifndef GAME_CLIENT_QMUI_QMFINISHSUMMARYCOVER_H
#define GAME_CLIENT_QMUI_QMFINISHSUMMARYCOVER_H

#include <engine/graphics.h>

#include <game/client/ui_rect.h>

#include <array>

namespace qm_finish_summary
{
	inline constexpr int COVER_SEGMENTS = 24;
	struct SCoverRow
	{
		float m_Y = 0.0f;
		float m_Left = 0.0f;
		float m_Right = 0.0f;
	};

	// 三组各八段分别沿左上圆角、左侧直边和左下圆角取样。
	std::array<SCoverRow, COVER_SEGMENTS + 1> CoverRows(float Width, float Height, float Radius,
		int CornerCurve, float Smoothing, float CapsuleSeam);
	vec2 CoverUV(vec2 Point, float Width, float Height);
	void RenderCover(IGraphics &Graphics, const CUIRect &Rect, IGraphics::CTextureHandle Texture,
		float Radius, float Alpha);
}

#endif
