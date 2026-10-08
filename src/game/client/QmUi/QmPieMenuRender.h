#ifndef GAME_CLIENT_QMUI_QMPIEMENURENDER_H
#define GAME_CLIENT_QMUI_QMPIEMENURENDER_H

#include "QmPieMenuGeometry.h"

#include <engine/graphics.h>

namespace qm_pie_menu_ui
{
	inline float PixelSize(IGraphics *pGraphics)
	{
		const auto Screen = pGraphics->GetScreen();
		return UnitsPerPixel(Screen.m_BottomRight - Screen.m_TopLeft, pGraphics->ScreenSize());
	}

	inline ColorRGBA SelectionTint(ColorRGBA Color, ColorRGBA Tint)
	{
		const float Strength = std::clamp(Tint.a, 0.0f, 1.0f);
		Color.r += (Tint.r - Color.r) * Strength;
		Color.g += (Tint.g - Color.g) * Strength;
		Color.b += (Tint.b - Color.b) * Strength;
		return Color;
	}

	inline ColorRGBA OptionColor(ColorRGBA Color, bool Highlighted, ColorRGBA Tint = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f))
	{
		if(Highlighted)
		{
			Color.r = std::min(Color.r * 1.3f, 1.0f);
			Color.g = std::min(Color.g * 1.3f, 1.0f);
			Color.b = std::min(Color.b * 1.3f, 1.0f);
			Color.a = std::min(Color.a * 1.2f, 1.0f);
			Color = SelectionTint(Color, Tint);
		}
		return Color;
	}

	inline void DrawGeometry(IGraphics *pGraphics, vec2 Center, const SGeometry &Geometry, ColorRGBA Color)
	{
		if(Geometry.m_PointCount == 0 || Color.a <= 0.0f)
			return;
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Color);
		for(int Index = 0; Index < Geometry.m_ArcSegments; ++Index)
		{
			const auto aPoints = Geometry.FillQuad(Index);
			const IGraphics::CFreeformItem Quad(Center + aPoints[0], Center + aPoints[1], Center + aPoints[2], Center + aPoints[3]);
			pGraphics->QuadsDrawFreeform(&Quad, 1);
		}

		// Freeform 的 0/1 槽位沿实体边，2/3 沿透明外边；两个三角形共享颜色端点。
		const IGraphics::CColorVertex aColors[] = {
			{0, Color}, {1, Color}, {2, Color.WithAlpha(0.0f)}, {3, Color.WithAlpha(0.0f)}};
		pGraphics->SetColorVertex(aColors, std::size(aColors));
		for(int Index = 0; Index < Geometry.m_PointCount; ++Index)
		{
			if(!Geometry.HasEdge(Index))
				continue;
			const auto &Point = Geometry.m_aPoints[Index];
			const auto &Next = Geometry.m_aPoints[(Index + 1) % Geometry.m_PointCount];
			const IGraphics::CFreeformItem Quad(Center + Point.m_Position, Center + Next.m_Position, Center + Point.m_Fringe, Center + Next.m_Fringe);
			pGraphics->QuadsDrawFreeform(&Quad, 1);
		}
		pGraphics->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		pGraphics->QuadsEnd();
	}

	inline void DrawSector(IGraphics *pGraphics, vec2 Center, float InnerRadius, float OuterRadius, float StartAngle, float EndAngle, float GapDegrees, ColorRGBA Color)
	{
		if(Color.a <= 0.0f)
			return;
		const SGeometry Geometry = BuildSector(InnerRadius, OuterRadius, StartAngle, EndAngle, GapDegrees, PixelSize(pGraphics));
		DrawGeometry(pGraphics, Center, Geometry, Color);
	}

	inline void DrawDisc(IGraphics *pGraphics, vec2 Center, float Radius, ColorRGBA Color)
	{
		if(Color.a <= 0.0f)
			return;
		const SGeometry Geometry = BuildDisc(Radius, PixelSize(pGraphics));
		DrawGeometry(pGraphics, Center, Geometry, Color);
	}
}

#endif
