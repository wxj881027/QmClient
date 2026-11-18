// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "UiTextCaret.h"

#include "UiSurface.h"

#include <engine/graphics.h>

#include <algorithm>

void qm_text_caret::Render(IGraphics *pGraphics, const SCaretShape &Shape, const ColorRGBA &TextColor, const ColorRGBA &OutlineColor)
{
	if(pGraphics == nullptr || !Shape.m_Visible || Shape.m_Rect.w <= 0.0f || Shape.m_Rect.h <= 0.0f)
		return;

	SRoundedSurfaceParams Params;
	Params.m_PixelSize = std::max(Shape.m_PixelSize, 0.0001f);
	// 光标跟着文字亚像素移动：不做像素对齐，否则输入时光标会逐帧抖动。
	Params.m_AlignToPixel = false;
	// 描边层先画：整块形状沿同一条曲线向外扩一圈，不靠「矩形放大 + 半径加宽」手拼，
	// 方块样式在深色底上才有干净的边缘。外扩量为 0 时不画，避免叠两层同样的形状。
	Params.m_Radius = Shape.m_OutlineRadius;
	const bool HasOutline = OutlineColor.a > 0.0f && (Shape.m_OutlineRect.w > Shape.m_Rect.w || Shape.m_OutlineRect.h > Shape.m_Rect.h);
	if(HasOutline)
		DrawRoundedSurface(pGraphics, Shape.m_OutlineRect, OutlineColor, ui_token::color::COLOR_TRANSPARENT, Params);
	Params.m_Radius = Shape.m_Radius;
	DrawRoundedSurface(pGraphics, Shape.m_Rect, TextColor, ui_token::color::COLOR_TRANSPARENT, Params);
	// 立即模式颜色复位：后续调用点按既有约定直接继续绘制，不必再补一次复位。
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
