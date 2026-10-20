#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_BADGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_BADGE_H

#include "qm_title_appearance.h"

#include <engine/textrender.h>

#include <game/client/ui_rect.h>

// 聊天测量和绘制共用整块胶囊的推进，保持与普通行相同的高度。
inline CUIRect QmLayoutTitleBadge(CTextCursor &Cursor, float LabelWidth)
{
	const float LineFontSize = Cursor.m_AlignedFontSize > 0.0f ? Cursor.m_AlignedFontSize : Cursor.m_FontSize;
	vec2 Size = QmTitleBadgeSize(LabelWidth, Cursor.m_FontSize * 13.0f / 24.0f);
	float Advance = Size.x + Cursor.m_FontSize * 0.15f;
	if(Cursor.m_LineWidth > 0.0f)
	{
		if(Cursor.m_X > Cursor.m_StartX && Cursor.m_X - Cursor.m_StartX + Advance > Cursor.m_LineWidth)
		{
			Cursor.m_LongestLineWidth = std::max(Cursor.m_LongestLineWidth, Cursor.m_X - Cursor.m_StartX);
			Cursor.m_X = Cursor.m_StartX;
			Cursor.m_Y += LineFontSize + (Cursor.m_AlignedFontSize > 0.0f ? Cursor.m_AlignedLineSpacing : Cursor.m_LineSpacing);
			++Cursor.m_LineCount;
		}
		const float Scale = std::min(1.0f, Cursor.m_LineWidth / Advance);
		Size *= Scale;
		Advance *= Scale;
	}
	const CUIRect Rect{Cursor.m_X, Cursor.m_Y + (LineFontSize - Size.y) * 0.5f, Size.x, Size.y};
	Cursor.m_X += Advance;
	Cursor.m_LongestLineWidth = std::max(Cursor.m_LongestLineWidth, Cursor.m_X - Cursor.m_StartX);
	Cursor.m_MaxCharacterHeight = std::max(Cursor.m_MaxCharacterHeight, Size.y);
	return Rect;
}

struct SQmTitleBadgePlacement
{
	CUIRect m_Rect;
	SQmTitleAppearance m_Appearance;
};

class CQmTitleBadge
{
	IGraphics::CTextureHandle m_Logo;
	IGraphics::CTextureHandle m_aPatterns[2];

public:
	void Init(IGraphics *pGraphics);
	void Shutdown(IGraphics *pGraphics);
	void RenderSurface(IGraphics *pGraphics, const CUIRect &Rect, const SQmTitleAppearance &Appearance, float Alpha) const;
};

#endif
