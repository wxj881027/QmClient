#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_TEXT_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_TEXT_LAYOUT_H

#include <engine/textrender.h>

#include <algorithm>

struct SQmChatTextBlockLayout
{
	float m_Height = 0.0f;
	float m_SecondaryOffset = 0.0f;
	float m_Width = 0.0f;
	float m_PrimaryOffset = 0.0f;
};

inline float QmChatTranslationFontSize(float OriginalFontSize, int Percentage)
{
	return OriginalFontSize * std::clamp(Percentage, 50, 100) / 100.0f;
}

inline float QmChatTextBlockBottom(const CTextCursor &Cursor)
{
	const float LogicalHeight = Cursor.m_LineCount * (Cursor.m_AlignedFontSize + Cursor.m_AlignedLineSpacing);
	const float VisualHeight = Cursor.m_HasVisualBoundingBox ? Cursor.m_VisualBottom - Cursor.m_StartY : 0.0f;
	return std::max(LogicalHeight, VisualHeight);
}

// 前缀已经换行时只按当前行位置缩进；长前缀也必须给后续正文保留半行宽度。
// 测量和绘制共用此策略，不改动字符计数、行高和颜色分段。
inline void QmChatApplyMessageIndent(CTextCursor &Cursor)
{
	if(Cursor.m_LineWidth <= 0.0f)
		return;
	const float Indent = std::clamp(Cursor.m_X - Cursor.m_StartX, 0.0f, Cursor.m_LineWidth * 0.5f);
	Cursor.m_StartX += Indent;
	Cursor.m_LineWidth -= Indent;
}

// 前缀和正文可能位于不同行，背景宽度取各自的最右边缘，不能直接相加最长行。
inline float QmChatIndentedContentWidth(const CTextCursor &Prefix, const CTextCursor &Body)
{
	return std::max(Prefix.m_LongestLineWidth, Body.m_StartX - Prefix.m_StartX + Body.m_LongestLineWidth);
}

inline CTextCursor QmChatSecondaryCursor(const CTextCursor &Original, float FontSize, float Offset)
{
	CTextCursor Secondary;
	Secondary.SetPosition(vec2(Original.m_StartX, Original.m_StartY + Offset));
	Secondary.m_FontSize = FontSize;
	Secondary.m_LineWidth = Original.m_LineWidth;
	Secondary.m_LineSpacing = Original.m_LineSpacing;
	Secondary.m_Flags = Original.m_Flags;
	Secondary.m_TrackLineRanges = Original.m_TrackLineRanges;
	Secondary.m_CalculateVisualBoundingBox = true;
	// 颜色分段使用整段容器的字符序号，换字号只重置行布局，不重置字符序号。
	Secondary.m_CharCount = Original.m_CharCount;
	Secondary.m_GlyphCount = Original.m_GlyphCount;
	return Secondary;
}

// 两个字号分别布局；测量结果直接控制渲染起点，不能用最后字号乘总行数。
template<typename TOriginal, typename TSecondary>
SQmChatTextBlockLayout QmChatMeasureTextBlocks(CTextCursor &Original, float SecondaryFontSize, TOriginal EmitOriginal, TSecondary EmitSecondary)
{
	Original.m_CalculateVisualBoundingBox = true;
	EmitOriginal(Original);
	const float OriginalHeight = QmChatTextBlockBottom(Original);
	const float OriginalTop = Original.m_HasVisualBoundingBox ? Original.m_VisualTop - Original.m_StartY : 0.0f;
	const float PrimaryOffset = -std::min(0.0f, OriginalTop);
	CTextCursor Secondary = QmChatSecondaryCursor(Original, SecondaryFontSize, 0.0f);
	EmitSecondary(Secondary);
	const float SecondaryTop = Secondary.m_HasVisualBoundingBox ? Secondary.m_VisualTop - Secondary.m_StartY : 0.0f;
	const float SecondaryOffset = OriginalHeight - std::min(0.0f, SecondaryTop);
	return {PrimaryOffset + SecondaryOffset + QmChatTextBlockBottom(Secondary), SecondaryOffset, std::max(Original.m_LongestLineWidth, Secondary.m_LongestLineWidth), PrimaryOffset};
}

#endif
