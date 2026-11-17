#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_BADGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_BADGE_H

#include "qm_title_appearance.h"

#include <engine/textrender.h>

#include <game/client/QmUi/QmBadgeGeometry.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui_rect.h>

#include <cstdint>

inline constexpr float QM_TITLE_TEXT_WAVE_AMPLITUDE = 0.08f;
inline constexpr float QM_TITLE_TEXT_WAVE_FPS = 30.0f;

std::string QmTitleDisplayText(const char *pTitle, const SQmTitleAppearance &Appearance);
void QmPrepareTitleTextCursor(CTextCursor &Cursor, const char *pDisplayText, int TextStyle, bool Wave, float Time);

// 聊天前缀作为一个整体换行。文字款使用正文字号和字宽，胶囊还要给图标和内边距留位。
inline CUIRect QmLayoutTitleBadge(CTextCursor &Cursor, float LabelWidth, bool TextOnly = false)
{
	const float LineFontSize = Cursor.m_AlignedFontSize > 0.0f ? Cursor.m_AlignedFontSize : Cursor.m_FontSize;
	vec2 Size = TextOnly ? vec2(LabelWidth, Cursor.m_FontSize) : QmTitleBadgeSize(LabelWidth, QmTitleBadgeLabelFontSize(Cursor.m_FontSize));
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

inline CUIRect QmTitleBadgeVisualBounds(const CTextCursor &Cursor)
{
	if(!Cursor.m_HasVisualBoundingBox)
		return {0.0f, 0.0f, Cursor.m_LongestLineWidth, Cursor.Height()};
	return {Cursor.m_VisualLeft - Cursor.m_StartX, Cursor.m_VisualTop - Cursor.m_StartY,
		Cursor.m_VisualRight - Cursor.m_VisualLeft, Cursor.m_VisualBottom - Cursor.m_VisualTop};
}

// 文字区域位于图标右侧；按可见字形对齐，消除中文和拉丁字母的基线差异。
inline vec2 QmTitleBadgeTextPosition(const CUIRect &Rect, const CUIRect &VisualBounds)
{
	const float Scale = Rect.h / QM_TITLE_BADGE_HEIGHT;
	const float TextWidth = std::max(0.0f, Rect.w - 39.0f * Scale);
	return {Rect.x + 30.0f * Scale + (TextWidth - VisualBounds.w) * 0.5f - VisualBounds.x,
		Rect.y + (Rect.h - VisualBounds.h) * 0.5f - VisualBounds.y};
}

struct SQmTitleBadgePlacement
{
	CUIRect m_Rect;
	SQmTitleAppearance m_Appearance;
	STextContainerIndex m_TextContainerIndex;
	CUIRect m_TextVisualBounds{};
	std::string m_DisplayText;
	int m_LastWaveFrame = -1;

	// 用实际生成的独立容器取得边界，聊天正文的基线和像素取整不再改变胶囊内的对齐。
	void PrepareText(ITextRender *pTextRender, const char *pText, bool Wave = false, float Time = 0.0f)
	{
		CTextCursor Cursor;
		const bool TextOnly = m_Appearance.m_TextStyle != 0;
		Cursor.m_FontSize = TextOnly ? m_Rect.h : QmTitleBadgeLabelFontSize(m_Rect.h);
		Cursor.m_CalculateVisualBoundingBox = !TextOnly;
		if(TextOnly)
			QmPrepareTitleTextCursor(Cursor, pText, m_Appearance.m_TextStyle, Wave, Time);
		const unsigned PreviousFlags = pTextRender->GetRenderFlags();
		const ColorRGBA PreviousColor = pTextRender->GetTextColor();
		pTextRender->SetRenderFlags((PreviousFlags | TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT) & ~TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD);
		pTextRender->TextColor(ui_token::color::NEUTRAL_WHITE);
		pTextRender->RecreateTextContainerSoft(m_TextContainerIndex, &Cursor, pText);
		if(!TextOnly)
			m_TextVisualBounds = QmTitleBadgeVisualBounds(Cursor);
		pTextRender->TextColor(PreviousColor);
		pTextRender->SetRenderFlags(PreviousFlags);
	}

	void UpdateTextStyle(ITextRender *pTextRender, bool Wave, float Time)
	{
		const int Frame = Wave ? static_cast<int>(Time * QM_TITLE_TEXT_WAVE_FPS) : -1;
		if(m_TextContainerIndex.Valid() && Frame == m_LastWaveFrame)
			return;
		m_LastWaveFrame = Frame;
		PrepareText(pTextRender, m_DisplayText.c_str(), Wave, Frame / QM_TITLE_TEXT_WAVE_FPS);
	}

	void RenderText(ITextRender *pTextRender, float Alpha, vec2 Offset) const
	{
		if(m_Appearance.m_TextStyle != 0)
		{
			const float TextAlpha = Alpha * m_Appearance.m_Opacity / 100.0f;
			pTextRender->RenderTextContainer(m_TextContainerIndex, ui_token::color::NEUTRAL_WHITE.WithMultipliedAlpha(TextAlpha),
				pTextRender->DefaultTextOutlineColor().WithMultipliedAlpha(TextAlpha), m_Rect.x + Offset.x, m_Rect.y + Offset.y);
			return;
		}
		const vec2 Position = QmTitleBadgeTextPosition(m_Rect, m_TextVisualBounds) + Offset;
		pTextRender->RenderTextContainer(m_TextContainerIndex, QmTitleBadgePalette(m_Appearance, Alpha).m_Text,
			ui_token::color::COLOR_TRANSPARENT, Position.x, Position.y);
	}

	void DeleteText(ITextRender *pTextRender)
	{
		pTextRender->DeleteTextContainer(m_TextContainerIndex);
	}
};

class CQmTitleBadge
{
	CQmBadgeGeometry m_Logo{EQmBadgeGeometry::LOGO};
	IGraphics::CTextureHandle m_LogoDistance;
	uint32_t m_LogoGraphicsVersion = 0;
	CQmBadgeGeometry m_aPatterns[6] = {
		CQmBadgeGeometry{EQmBadgeGeometry::MOON_LEFT}, CQmBadgeGeometry{EQmBadgeGeometry::MOON_MIDDLE}, CQmBadgeGeometry{EQmBadgeGeometry::MOON_RIGHT},
		CQmBadgeGeometry{EQmBadgeGeometry::SUN_LEFT}, CQmBadgeGeometry{EQmBadgeGeometry::SUN_MIDDLE}, CQmBadgeGeometry{EQmBadgeGeometry::SUN_RIGHT}};

	bool RenderDistanceLogo(IGraphics *pGraphics, const CUIRect &Rect, ColorRGBA Color) const;

public:
	void Init(IGraphics *pGraphics);
	void Shutdown(IGraphics *pGraphics);
	void RenderLogo(IGraphics *pGraphics, const CUIRect &Rect, ColorRGBA Color) const;
	void RenderSurface(IGraphics *pGraphics, const CUIRect &Rect, const SQmTitleAppearance &Appearance, float Alpha) const;
};

#endif
