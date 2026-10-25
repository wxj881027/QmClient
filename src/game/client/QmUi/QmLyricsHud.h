#ifndef GAME_CLIENT_QMUI_QMLYRICSHUD_H
#define GAME_CLIENT_QMUI_QMLYRICSHUD_H

#include <base/str.h>

#include <engine/graphics.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cstdint>

class CHudEditor;
class ITextRender;

class CQmLyricsMarqueeState
{
	char m_aaRows[2][1024] = {};
	int m_RowCount = -1;
	int64_t m_StartTick = 0;

public:
	void Reset() { *this = {}; }
	float Update(const char *const *ppRows, int Count, int64_t Now, int64_t Frequency)
	{
		Count = std::clamp(Count, 0, 2);
		bool Changed = Count != m_RowCount;
		for(int Index = 0; Index < Count; ++Index)
			Changed |= str_comp(m_aaRows[Index], ppRows[Index]) != 0;
		if(Changed || Now < m_StartTick)
		{
			for(int Index = 0; Index < Count; ++Index)
				str_copy(m_aaRows[Index], ppRows[Index], sizeof(m_aaRows[Index]));
			m_RowCount = Count;
			m_StartTick = Now;
		}
		return Frequency > 0 ? (Now - m_StartTick) / static_cast<float>(Frequency) : 0.0f;
	}
};

inline CUIRect QmLyricsHudAnchor(const CUIRect &MainRect, const CUIRect &TransformRect, const CUIRect &TargetRect)
{
	const float Scale = TransformRect.w > 0.0f ? TargetRect.w / TransformRect.w : 1.0f;
	return {TargetRect.x + (MainRect.x - TransformRect.x) * Scale,
		TargetRect.y + (MainRect.y - TransformRect.y) * Scale, MainRect.w * Scale, MainRect.h * Scale};
}

inline CUIRect QmLyricsIslandLayout(const CUIRect &MainRect, float ScreenWidth, float ScreenHeight, int RowCount)
{
	const float Width = std::min(105.0f, std::max(0.0f, ScreenWidth - 7.0f));
	const float Height = std::min(3.5f + 4.9f * std::clamp(RowCount, 1, 2), std::max(0.0f, ScreenHeight));
	const float X = std::clamp(MainRect.x + MainRect.w * 0.5f - Width * 0.5f, 0.0f, std::max(0.0f, ScreenWidth - Width));
	const float Y = std::clamp(MainRect.y + MainRect.h + 3.5f, 0.0f, std::max(0.0f, ScreenHeight - Height));
	return {X, Y, Width, Height};
}

void QmRenderLyricsRows(IGraphics &Graphics, ITextRender &TextRender, const CUIRect &Rect,
	const char *const *ppRows, int RowCount, float FontSize, ColorRGBA Color, float ElapsedSeconds);

void QmRenderLyricsIsland(IGraphics &Graphics, ITextRender &TextRender, CHudEditor &Editor,
	const CUIRect &Anchor, float ScreenWidth, float ScreenHeight, const char *const *ppRows, int RowCount,
	float ElapsedSeconds, IGraphics::CRenderTargetHandle Backdrop);

#endif
