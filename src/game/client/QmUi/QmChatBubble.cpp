#include "QmChatBubble.h"

#include <engine/graphics.h>
#include <engine/textrender.h>

namespace qm_chat_bubble
{
	void Render(IGraphics *pGraphics, const SLayout &Layout, const SPalette &Colors)
	{
		if(Layout.m_Scale <= 0.0f || Colors.m_Outline.a <= 0.0f)
			return;
		const SContour Outer = Contour(Layout);
		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
		const float FeatherSize = AntialiasWidth(vec2(ScreenX1 - ScreenX0, ScreenY1 - ScreenY0), vec2(pGraphics->ScreenWidth(), pGraphics->ScreenHeight()));
		const float InnerFeather = std::min(FeatherSize * 0.5f, Layout.m_Stroke * 0.5f);
		const SContour Inner = Contour(Layout, Layout.m_Stroke + InnerFeather);
		const SContour BorderInner = Contour(Layout, Layout.m_Stroke - InnerFeather);
		const SContour Feather = Contour(Layout, -FeatherSize);
		std::array<IGraphics::CFreeformItem, MAX_CONTOUR_POINTS> aShadow;
		const vec2 Center = Layout.m_Position + Layout.m_Size * 0.5f;
		const vec2 ShadowOffset = vec2(2.5f, 3.0f) * Layout.m_Scale;
		int ShadowCount = 0;
		for(int i = 0; i < Outer.m_Count; ++i)
		{
			const int Next = (i + 1) % Outer.m_Count;
			const vec2 Edge = Outer.m_aPoints[Next] - Outer.m_aPoints[i];
			if(Edge.x * ShadowOffset.y - Edge.y * ShadowOffset.x < 0.0f)
				aShadow[ShadowCount++] = IGraphics::CFreeformItem(Outer.m_aPoints[i], Outer.m_aPoints[Next], Outer.m_aPoints[i] + ShadowOffset, Outer.m_aPoints[Next] + ShadowOffset);
		}
		auto FillColor = [&](vec2 Point) {
			const float T = std::clamp((Point.y - Layout.m_Position.y) / Layout.m_Size.y, 0.0f, 1.0f);
			return ColorRGBA(mix(Colors.m_Fill.r, Colors.m_FillBottom.r, T), mix(Colors.m_Fill.g, Colors.m_FillBottom.g, T),
				mix(Colors.m_Fill.b, Colors.m_FillBottom.b, T), mix(Colors.m_Fill.a, Colors.m_FillBottom.a, T));
		};
		auto DrawRing = [&](const SContour &Outside, const SContour &Inside, auto OutsideColor, auto InsideColor) {
			for(int i = 0; i < Outside.m_Count; ++i)
			{
				const int Next = (i + 1) % Outside.m_Count;
				const IGraphics::CFreeformItem Quad(Outside.m_aPoints[i], Outside.m_aPoints[Next], Inside.m_aPoints[i], Inside.m_aPoints[Next]);
				// 自由四边形的 2/3 号点与 SetColor4 的下方两个颜色顺序相反。
				pGraphics->SetColor4(OutsideColor(Outside.m_aPoints[i]), OutsideColor(Outside.m_aPoints[Next]), InsideColor(Inside.m_aPoints[Next]), InsideColor(Inside.m_aPoints[i]));
				pGraphics->QuadsDrawFreeform(&Quad, 1);
			}
		};
		const auto OutlineColor = [&](vec2) { return Colors.m_Outline; };
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		// 阴影逐渐淡出，避免尾尖和圆角外侧留下第二层锯齿实线。
		pGraphics->SetColor4(Colors.m_Shadow, Colors.m_Shadow, Colors.m_Shadow.WithAlpha(0.0f), Colors.m_Shadow.WithAlpha(0.0f));
		pGraphics->QuadsDrawFreeform(aShadow.data(), ShadowCount);
		for(int i = 0; i < Inner.m_Count; ++i)
		{
			const vec2 A = Inner.m_aPoints[i];
			const vec2 B = Inner.m_aPoints[(i + 1) % Inner.m_Count];
			const IGraphics::CFreeformItem Triangle(Center, A, B, B);
			pGraphics->SetColor4(FillColor(Center), FillColor(A), FillColor(B), FillColor(B));
			pGraphics->QuadsDrawFreeform(&Triangle, 1);
		}
		DrawRing(BorderInner, Inner, OutlineColor, FillColor);
		DrawRing(Outer, BorderInner, OutlineColor, OutlineColor);
		DrawRing(Outer, Feather, OutlineColor, [&](vec2) { return Colors.m_Outline.WithAlpha(0.0f); });
		pGraphics->QuadsEnd();
		if(Layout.m_Style == EStyle::CANDY)
		{
			// 高光落在顶部留白内，避免覆盖文字。
			pGraphics->DrawRect(Layout.m_Position.x + Layout.m_Radius, Layout.m_Position.y + 4.0f * Layout.m_Scale,
				std::min(Layout.m_Size.x * 0.22f, 32.0f * Layout.m_Scale), 1.5f * Layout.m_Scale,
				ColorRGBA(1.0f, 1.0f, 1.0f, Colors.m_Fill.a * 0.65f), IGraphics::CORNER_ALL, Layout.m_Scale);
		}
	}

	void RenderText(ITextRender *pTextRender, CTextCursor &Cursor, const char *pText, ColorRGBA Color, ColorRGBA Outline)
	{
		const unsigned PreviousFlags = pTextRender->GetRenderFlags();
		const ColorRGBA PreviousColor = pTextRender->GetTextColor();
		pTextRender->SetRenderFlags(PreviousFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
		pTextRender->TextColor(ColorRGBA(1, 1, 1, 1));
		STextContainerIndex Container;
		// TextEx 会在最终绘制时使用默认黑描边；气泡显式提交自己的文字与描边颜色。
		if(pTextRender->CreateTextContainer(Container, &Cursor, pText))
			pTextRender->RenderTextContainer(Container, Color, Outline);
		pTextRender->DeleteTextContainer(Container);
		pTextRender->TextColor(PreviousColor);
		pTextRender->SetRenderFlags(PreviousFlags);
	}
}
