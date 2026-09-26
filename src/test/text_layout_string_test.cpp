#include <engine/client/text_layout_string.h>
#include <engine/client/text_word_cursor.h>

#include <gtest/gtest.h>

TEST(TextLayoutString, ByteLengthClampsToLimitAndTerminator)
{
	// 上限小于剩余长度：只扫描到上限，不遍历尾部。
	EXPECT_EQ(QmTextLayoutByteLength("abcdef", 3), 3);
	// 上限超过实际长度：在终止符处停止。
	EXPECT_EQ(QmTextLayoutByteLength("abc", 32), 3);
	// 负长度沿用整串长度语义。
	EXPECT_EQ(QmTextLayoutByteLength("hello", -1), 5);
	// 空串不得越界读取。
	EXPECT_EQ(QmTextLayoutByteLength("", 8), 0);
	EXPECT_EQ(QmTextLayoutByteLength("abc", 0), 0);
}

TEST(TextWordMeasureCursor, CopiesLayoutFieldsWithoutRenderState)
{
	CTextCursor Source;
	Source.m_Flags = TEXTFLAG_RENDER;
	Source.m_LineCount = 2;
	Source.m_GlyphCount = 7;
	Source.m_CharCount = 5;
	Source.m_MaxLines = 4;
	Source.m_StartX = 11.0f;
	Source.m_StartY = 12.0f;
	Source.m_LineWidth = 200.0f;
	Source.m_X = 1.0f;
	Source.m_Y = 2.0f;
	Source.m_FontSize = 14.0f;
	Source.m_LineSpacing = 16.0f;
	Source.m_vSelectionQuads.push_back(IGraphics::CQuadItem(0.0f, 0.0f, 1.0f, 1.0f));
	Source.m_vColorSplits.emplace_back(0, 2, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));

	const CTextCursor Measured = QmTextWordMeasureCursor(Source, 30.0f, 40.0f);

	// 词宽探测位置来自参数，渲染标志必须清除。
	EXPECT_FLOAT_EQ(Measured.m_X, 30.0f);
	EXPECT_FLOAT_EQ(Measured.m_Y, 40.0f);
	EXPECT_EQ(Measured.m_Flags & TEXTFLAG_RENDER, 0);
	EXPECT_NE(Measured.m_Flags & TEXTFLAG_DISALLOW_NEWLINE, 0);
	// 排版参数与字符计数保持一致。
	EXPECT_EQ(Measured.m_GlyphCount, Source.m_GlyphCount);
	EXPECT_EQ(Measured.m_CharCount, Source.m_CharCount);
	EXPECT_EQ(Measured.m_LineCount, Source.m_LineCount);
	EXPECT_EQ(Measured.m_MaxLines, Source.m_MaxLines);
	EXPECT_FLOAT_EQ(Measured.m_LineWidth, Source.m_LineWidth);
	EXPECT_FLOAT_EQ(Measured.m_FontSize, Source.m_FontSize);
	EXPECT_FLOAT_EQ(Measured.m_LineSpacing, Source.m_LineSpacing);
	// 选区四边形与颜色分段不参与词宽，必须留在源游标上。
	EXPECT_TRUE(Measured.m_vSelectionQuads.empty());
	EXPECT_TRUE(Measured.m_vColorSplits.empty());
	EXPECT_EQ(Source.m_vSelectionQuads.size(), 1U);
	EXPECT_EQ(Source.m_vColorSplits.size(), 1U);
}
