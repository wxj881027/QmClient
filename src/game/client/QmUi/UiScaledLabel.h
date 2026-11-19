#ifndef GAME_CLIENT_QMUI_UISCALEDLABEL_H
#define GAME_CLIENT_QMUI_UISCALEDLABEL_H

#include "UiTextRenderState.h"

#include <game/client/ui.h>

#include <new>
#include <stdexcept>

// 固定字号生成字形，仅在绘制时缩放，避免动画跨越整数像素字号时跳变。
inline void DrawUiScaledLabel(CUi &Ui, CUIElement::SUIElementRect &Label, const CUIRect &BaseRect, const CUIRect &DrawRect, const char *pText, float FontSize, float Scale)
{
	if(Ui.PreLayoutInput() || Scale <= 0.0f)
		return;

	try
	{
		auto *pTextRender = Ui.TextRender();
		CUiScopedTextRenderState TextState(*pTextRender);
		pTextRender->SetRenderFlags(pTextRender->GetRenderFlags() | TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		Ui.DoLabelStreamed(Label, &BaseRect, pText, FontSize, TEXTALIGN_MC, {}, -1, nullptr, false);
		TextState.Restore();
		if(Ui.DrawingSuppressed())
			return;

		Ui.FlushQuadBatch();
		auto *pGraphics = Ui.Graphics();
		float X0, Y0, X1, Y1;
		pGraphics->GetScreen(&X0, &Y0, &X1, &Y1);
		struct SRestoreScreen
		{
			IGraphics &m_Graphics;
			CScreenRect m_Screen;
			// 只恢复坐标状态，不分配资源或提交绘制命令。
			~SRestoreScreen() { m_Graphics.MapScreen(m_Screen); }
		};
		const SRestoreScreen RestoreScreen{*pGraphics, CScreenRect(vec2(X0, Y0), vec2(X1, Y1))};
		const vec2 BaseCenter(BaseRect.x + BaseRect.w * 0.5f, BaseRect.y + BaseRect.h * 0.5f);
		const vec2 DrawCenter(DrawRect.x + DrawRect.w * 0.5f, DrawRect.y + DrawRect.h * 0.5f);
		pGraphics->MapScreen(BaseCenter.x + (X0 - DrawCenter.x) / Scale, BaseCenter.y + (Y0 - DrawCenter.y) / Scale,
			BaseCenter.x + (X1 - DrawCenter.x) / Scale, BaseCenter.y + (Y1 - DrawCenter.y) / Scale);
		Ui.RenderLabelTextContainerAligned(Label, &BaseRect, TEXTALIGN_MC);
	}
	catch(const std::bad_alloc &)
	{
		// 缓存仍由标签持有，下帧沿原重建或绘制入口重试。
		return;
	}
	catch(const std::length_error &)
	{
		return;
	}
}

#endif
