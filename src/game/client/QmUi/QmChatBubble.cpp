#include "QmChatBubble.h"

#include <engine/graphics.h>

namespace qm_chat_bubble
{
	void Render(IGraphics *pGraphics, const SLayout &Layout, const SPalette &Colors)
	{
		if(Layout.m_Scale <= 0.0f || Colors.m_Outline.a <= 0.0f)
			return;
		const SContour Outer = Contour(Layout);
		const SContour Inner = Contour(Layout, Layout.m_Stroke);
		const float RimWidth = Layout.m_Style == EStyle::STICKER ? 3.0f * Layout.m_Scale : 0.0f;
		const SContour Rim = Contour(Layout, -RimWidth);
		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
		const float FeatherSize = std::min(Layout.m_Scale, std::abs(ScreenX1 - ScreenX0) / std::max(1, pGraphics->ScreenWidth()));
		const SContour Feather = Contour(Layout, -RimWidth - FeatherSize);
		std::array<IGraphics::CFreeformItem, MAX_CONTOUR_POINTS> aBorder, aRim, aAntialias, aShadow;
		const vec2 Center = Layout.m_Position + Layout.m_Size * 0.5f;
		const vec2 ShadowOffset = vec2(2.5f, 3.0f) * Layout.m_Scale;
		int ShadowCount = 0;
		for(int i = 0; i < Outer.m_Count; ++i)
		{
			const int Next = (i + 1) % Outer.m_Count;
			// 自由四边形按 0、1、3、2 提交，内外对应点放在同侧。
			aBorder[i] = IGraphics::CFreeformItem(Outer.m_aPoints[i], Outer.m_aPoints[Next], Inner.m_aPoints[i], Inner.m_aPoints[Next]);
			aRim[i] = IGraphics::CFreeformItem(Outer.m_aPoints[i], Outer.m_aPoints[Next], Rim.m_aPoints[i], Rim.m_aPoints[Next]);
			aAntialias[i] = IGraphics::CFreeformItem(Rim.m_aPoints[i], Rim.m_aPoints[Next], Feather.m_aPoints[i], Feather.m_aPoints[Next]);
			const vec2 Edge = Rim.m_aPoints[Next] - Rim.m_aPoints[i];
			if(Edge.x * ShadowOffset.y - Edge.y * ShadowOffset.x < 0.0f)
				aShadow[ShadowCount++] = IGraphics::CFreeformItem(Rim.m_aPoints[i], Rim.m_aPoints[Next], Rim.m_aPoints[i] + ShadowOffset, Rim.m_aPoints[Next] + ShadowOffset);
		}
		auto FillColor = [&](vec2 Point) {
			const float T = std::clamp((Point.y - Layout.m_Position.y) / Layout.m_Size.y, 0.0f, 1.0f);
			return ColorRGBA(mix(Colors.m_Fill.r, Colors.m_FillBottom.r, T), mix(Colors.m_Fill.g, Colors.m_FillBottom.g, T),
				mix(Colors.m_Fill.b, Colors.m_FillBottom.b, T), mix(Colors.m_Fill.a, Colors.m_FillBottom.a, T));
		};
		const ColorRGBA RimColor = RimWidth > 0.0f ? ColorRGBA(1.0f, 0.98f, 0.99f, Colors.m_Outline.a) : Colors.m_Outline;
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Colors.m_Shadow);
		pGraphics->QuadsDrawFreeform(aShadow.data(), ShadowCount);
		for(int i = 0; i < Inner.m_Count; ++i)
		{
			const vec2 A = Inner.m_aPoints[i];
			const vec2 B = Inner.m_aPoints[(i + 1) % Inner.m_Count];
			const IGraphics::CFreeformItem Triangle(Center, A, B, B);
			pGraphics->SetColor4(FillColor(Center), FillColor(A), FillColor(B), FillColor(B));
			pGraphics->QuadsDrawFreeform(&Triangle, 1);
		}
		pGraphics->SetColor(Colors.m_Outline);
		pGraphics->QuadsDrawFreeform(aBorder.data(), Outer.m_Count);
		if(RimWidth > 0.0f)
		{
			pGraphics->SetColor(RimColor);
			pGraphics->QuadsDrawFreeform(aRim.data(), Outer.m_Count);
		}
		pGraphics->SetColor4(RimColor, RimColor, RimColor.WithAlpha(0.0f), RimColor.WithAlpha(0.0f));
		pGraphics->QuadsDrawFreeform(aAntialias.data(), Outer.m_Count);
		pGraphics->QuadsEnd();
		if(Layout.m_Style == EStyle::CANDY)
		{
			// 高光落在顶部留白内，避免覆盖文字。
			pGraphics->DrawRect(Layout.m_Position.x + Layout.m_Radius, Layout.m_Position.y + 4.0f * Layout.m_Scale,
				std::min(Layout.m_Size.x * 0.22f, 32.0f * Layout.m_Scale), 1.5f * Layout.m_Scale,
				ColorRGBA(1.0f, 1.0f, 1.0f, Colors.m_Fill.a * 0.65f), IGraphics::CORNER_ALL, Layout.m_Scale);
		}
	}
}
