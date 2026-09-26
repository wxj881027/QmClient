#ifndef ENGINE_CLIENT_TEXT_WORD_CURSOR_H
#define ENGINE_CLIENT_TEXT_WORD_CURSOR_H

#include <engine/textrender.h>

// 换行探测只需要排版参数与字符计数，颜色、顶点偏移和选区不影响词宽。
// 不复制整段文字的三个数组，避免每个词都分配并复制与段落长度成正比的数据。
inline CTextCursor QmTextWordMeasureCursor(const CTextCursor &Source, float X, float Y)
{
	CTextCursor Result;
	Result.m_Flags = (Source.m_Flags & ~TEXTFLAG_RENDER) | TEXTFLAG_DISALLOW_NEWLINE;
	Result.m_LineCount = Source.m_LineCount;
	Result.m_GlyphCount = Source.m_GlyphCount;
	Result.m_CharCount = Source.m_CharCount;
	Result.m_MaxLines = Source.m_MaxLines;
	Result.m_StartX = Source.m_StartX;
	Result.m_StartY = Source.m_StartY;
	Result.m_LineWidth = Source.m_LineWidth;
	Result.m_X = X;
	Result.m_Y = Y;
	Result.m_FontSize = Source.m_FontSize;
	Result.m_LineSpacing = Source.m_LineSpacing;
	// 对齐字号在排版入口重新计算；边界框和交互结果不由词宽探测使用。
	return Result;
}

#endif // ENGINE_CLIENT_TEXT_WORD_CURSOR_H
