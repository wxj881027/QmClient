#ifndef GAME_CLIENT_QMUI_QMLYRICSHUD_H
#define GAME_CLIENT_QMUI_QMLYRICSHUD_H

#include <base/str.h>

#include <engine/graphics.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

class CHudEditor;
class ITextRender;

// 当前歌词的播放进度决定长句视口；短句保持居中，未知进度停在开头。
inline float QmLyricsScrollOffset(float TextWidth, float ViewWidth, float Progress)
{
	if(TextWidth <= ViewWidth || ViewWidth <= 0.0f || !std::isfinite(Progress) || Progress < 0.0f)
		return 0.0f;
	return std::clamp(TextWidth * std::clamp(Progress, 0.0f, 1.0f) - ViewWidth * 0.5f, 0.0f, TextWidth - ViewWidth);
}

inline CUIRect QmLyricsHudAnchor(const CUIRect &MainRect, const CUIRect &TransformRect, const CUIRect &TargetRect)
{
	const float Scale = TransformRect.w > 0.0f ? TargetRect.w / TransformRect.w : 1.0f;
	return {TargetRect.x + (MainRect.x - TransformRect.x) * Scale,
		TargetRect.y + (MainRect.y - TransformRect.y) * Scale, MainRect.w * Scale, MainRect.h * Scale};
}

inline CUIRect QmLyricsIslandLayout(const CUIRect &MainRect, float ScreenWidth, float ScreenHeight, int RowCount, bool FollowWidth = true)
{
	const float Width = FollowWidth ? std::max(0.0f, MainRect.w) : std::min(105.0f, std::max(0.0f, ScreenWidth - 7.0f));
	const float Height = std::min(3.5f + 4.9f * std::clamp(RowCount, 1, 2), std::max(0.0f, ScreenHeight));
	const float X = FollowWidth ? MainRect.x : std::clamp(MainRect.x + MainRect.w * 0.5f - Width * 0.5f, 0.0f, std::max(0.0f, ScreenWidth - Width));
	const float Y = std::clamp(MainRect.y + MainRect.h + 3.5f, 0.0f, std::max(0.0f, ScreenHeight - Height));
	return {X, Y, Width, Height};
}

void QmRenderLyricsRows(IGraphics &Graphics, ITextRender &TextRender, const CUIRect &Rect,
	const char *const *ppRows, int RowCount, float FontSize, ColorRGBA Color, float Progress);

void QmRenderLyricsIsland(IGraphics &Graphics, ITextRender &TextRender, CHudEditor &Editor,
	const CUIRect &Anchor, float ScreenWidth, float ScreenHeight, const char *const *ppRows, int RowCount,
	float Progress, IGraphics::CRenderTargetHandle Backdrop);

#endif
