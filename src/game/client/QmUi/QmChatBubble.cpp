#include "QmChatBubble.h"

#include <engine/graphics.h>

namespace qm_chat_bubble
{

	void Render(IGraphics *pGraphics, const SLayout &Layout, ColorRGBA Background, float Opacity)
	{
		const SPalette Colors = Palette(Background, Opacity);
		if(Layout.m_Scale <= 0.0f || Colors.m_Outline.a <= 0.0f)
			return;

		const TContour aOuter = Contour(Layout);
		const TContour aInner = Contour(Layout, Layout.m_Stroke);
		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
		const float FeatherSize = std::min(Layout.m_Scale, std::abs(ScreenX1 - ScreenX0) / std::max(1, pGraphics->ScreenWidth()));
		const TContour aFeather = Contour(Layout, -FeatherSize);
		std::array<IGraphics::CFreeformItem, CONTOUR_POINTS> aFill;
		std::array<IGraphics::CFreeformItem, CONTOUR_POINTS> aBorder;
		std::array<IGraphics::CFreeformItem, CONTOUR_POINTS> aAntialias;
		std::array<IGraphics::CFreeformItem, CONTOUR_POINTS> aShadow;
		const vec2 Center = Layout.m_Position + Layout.m_Size * 0.5f;
		const vec2 ShadowOffset = vec2(2.5f, 3.0f) * Layout.m_Scale;
		int ShadowCount = 0;
		for(int i = 0; i < CONTOUR_POINTS; ++i)
		{
			const int Next = (i + 1) % CONTOUR_POINTS;
			aFill[i] = IGraphics::CFreeformItem(Center, aInner[i], aInner[Next], aInner[Next]);
			// 自由四边形的顶点顺序为 0、1、3、2，第三个参数与第一个参数位于同侧。
			aBorder[i] = IGraphics::CFreeformItem(aOuter[i], aOuter[Next], aInner[i], aInner[Next]);
			aAntialias[i] = IGraphics::CFreeformItem(aOuter[i], aOuter[Next], aFeather[i], aFeather[Next]);
			const vec2 Edge = aOuter[Next] - aOuter[i];
			// 只画轮廓外侧的偏移阴影，避免整块阴影透过半透明底色。
			if(Edge.x * ShadowOffset.y - Edge.y * ShadowOffset.x < 0.0f)
				aShadow[ShadowCount++] = IGraphics::CFreeformItem(aOuter[i], aOuter[Next], aOuter[i] + ShadowOffset, aOuter[Next] + ShadowOffset);
		}

		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Colors.m_Shadow);
		pGraphics->QuadsDrawFreeform(aShadow.data(), ShadowCount);
		pGraphics->SetColor(Colors.m_Fill);
		pGraphics->QuadsDrawFreeform(aFill.data(), CONTOUR_POINTS);
		pGraphics->SetColor(Colors.m_Outline);
		pGraphics->QuadsDrawFreeform(aBorder.data(), CONTOUR_POINTS);
		// 轮廓外沿渐隐一个像素，尾巴和圆角在非整数缩放下也保持柔和。
		pGraphics->SetColor4(Colors.m_Outline, Colors.m_Outline, Colors.m_Outline.WithAlpha(0.0f), Colors.m_Outline.WithAlpha(0.0f));
		pGraphics->QuadsDrawFreeform(aAntialias.data(), CONTOUR_POINTS);
		pGraphics->QuadsEnd();
	}

}
