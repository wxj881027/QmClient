#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_DEVELOPER_BADGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_DEVELOPER_BADGE_H

#include "qm_developer_badge_motion.h"

#include <engine/textrender.h>

#include <game/client/QmUi/QmBadgeGeometry.h>
#include <game/client/ui_rect.h>

#include <algorithm>

inline vec2 QmDeveloperBadgeSize(float FontSize)
{
	const float Height = std::max(FontSize, 0.0f);
	return vec2(Height * (QM_DEVELOPER_BADGE_WIDTH / QM_DEVELOPER_BADGE_HEIGHT), Height);
}

inline float QmDeveloperBadgeAdvance(float FontSize)
{
	return QmDeveloperBadgeSize(FontSize).x + std::max(FontSize, 0.0f) * 0.15f;
}

// 测量和绘制共用这次游标推进，徽章作为整体换行，不拆成文字占位。
inline CUIRect QmLayoutDeveloperBadge(CTextCursor &Cursor)
{
	const float FontSize = Cursor.m_FontSize;
	const float LineFontSize = Cursor.m_AlignedFontSize > 0.0f ? Cursor.m_AlignedFontSize : FontSize;
	vec2 Size = QmDeveloperBadgeSize(FontSize);
	float Advance = QmDeveloperBadgeAdvance(FontSize);
	if(Advance <= 0.0f)
		return {};
	if(Cursor.m_LineWidth > 0.0f)
	{
		if(Cursor.m_X > Cursor.m_StartX && Cursor.m_X - Cursor.m_StartX + Advance > Cursor.m_LineWidth)
		{
			Cursor.m_LongestLineWidth = std::max(Cursor.m_LongestLineWidth, Cursor.m_X - Cursor.m_StartX);
			Cursor.m_X = Cursor.m_StartX;
			Cursor.m_Y += LineFontSize + (Cursor.m_AlignedFontSize > 0.0f ? Cursor.m_AlignedLineSpacing : Cursor.m_LineSpacing);
			++Cursor.m_LineCount;
		}
		// 极窄聊天区仍保留完整胶囊比例。
		const float Scale = std::min(1.0f, Cursor.m_LineWidth / Advance);
		Size *= Scale;
		Advance *= Scale;
	}
	const CUIRect Rect = {Cursor.m_X, Cursor.m_Y + (LineFontSize - Size.y) * 0.5f, Size.x, Size.y};
	Cursor.m_X += Advance;
	Cursor.m_LongestLineWidth = std::max(Cursor.m_LongestLineWidth, Cursor.m_X - Cursor.m_StartX);
	Cursor.m_MaxCharacterHeight = std::max(Cursor.m_MaxCharacterHeight, Size.y);
	return Rect;
}

class CQmDeveloperBadge
{
	CQmBadgeGeometry m_Surface{EQmBadgeGeometry::DEVELOPER};
	mutable double m_MotionTime = -1.0;
	mutable SQmDeveloperBadgeMotion m_Motion{};

public:
	void Init(IGraphics *pGraphics);
	void Shutdown(IGraphics *pGraphics);
	bool Available() const { return m_Surface.Available(); }
	void Render(IGraphics *pGraphics, const CUIRect &Rect, float Alpha, double TimeSec) const;
};

#endif
