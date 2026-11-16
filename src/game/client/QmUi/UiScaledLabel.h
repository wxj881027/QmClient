#ifndef GAME_CLIENT_QMUI_UISCALEDLABEL_H
#define GAME_CLIENT_QMUI_UISCALEDLABEL_H

#include <game/client/ui.h>

// 固定字号生成字形，仅在绘制时缩放，避免动画跨越整数像素字号时跳变。
inline void DrawUiScaledLabel(CUi &Ui, CUIElement::SUIElementRect &Label, const CUIRect &BaseRect, const CUIRect &DrawRect, const char *pText, float FontSize, float Scale)
{
	if(Ui.PreLayoutInput() || Scale <= 0.0f)
		return;
	auto *pTextRender = Ui.TextRender();
	const unsigned PreviousFlags = pTextRender->GetRenderFlags();
	pTextRender->SetRenderFlags(PreviousFlags | TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	Ui.DoLabelStreamed(Label, &BaseRect, pText, FontSize, TEXTALIGN_MC, {}, -1, nullptr, false);
	pTextRender->SetRenderFlags(PreviousFlags);
	if(Ui.DrawingSuppressed())
		return;

	Ui.FlushQuadBatch();
	auto *pGraphics = Ui.Graphics();
	float X0, Y0, X1, Y1;
	pGraphics->GetScreen(&X0, &Y0, &X1, &Y1);
	const vec2 BaseCenter(BaseRect.x + BaseRect.w * 0.5f, BaseRect.y + BaseRect.h * 0.5f);
	const vec2 DrawCenter(DrawRect.x + DrawRect.w * 0.5f, DrawRect.y + DrawRect.h * 0.5f);
	pGraphics->MapScreen(BaseCenter.x + (X0 - DrawCenter.x) / Scale, BaseCenter.y + (Y0 - DrawCenter.y) / Scale,
		BaseCenter.x + (X1 - DrawCenter.x) / Scale, BaseCenter.y + (Y1 - DrawCenter.y) / Scale);
	Ui.RenderLabelTextContainerAligned(Label, &BaseRect, TEXTALIGN_MC);
	pGraphics->MapScreen(X0, Y0, X1, Y1);
}

#endif
