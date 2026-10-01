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

TEST(QmTextLayout, ExplicitByteLengthKeepsPrefixAndNullTerminationSemantics)
{
	for(const char *pText : {"", "hello world", "多行文字\n测试", "emoji: \xF0\x9F\x98\x80 suffix"})
	{
		const int FullLength = str_length(pText);
		for(int Limit = -2; Limit <= FullLength + 2; ++Limit)
			EXPECT_EQ(QmTextLayoutByteLength(pText, Limit), Limit < 0 ? FullLength : std::min(Limit, FullLength));
	}
	const char aEmbeddedNull[] = {'a', '\0', 'b', '\0'};
	EXPECT_EQ(QmTextLayoutByteLength(aEmbeddedNull, 3), 1);
	// 显式字节上限可以落在 UTF-8 编码中间，不能偷偷改成码点计数。
	EXPECT_EQ(QmTextLayoutByteLength("中文", 2), 2);
}

TEST(QmTextWordCursor, DecoratedParagraphDoesNotCopyPerCharacterStorage)
{
	CTextCursor Source;
	Source.m_Flags = TEXTFLAG_RENDER;
	Source.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_CALCULATE;
	Source.m_CursorMode = TEXT_CURSOR_CURSOR_MODE_CALCULATE;
	for(int Index = 0; Index < 4096; ++Index)
	{
		Source.m_vColorSplits.emplace_back(Index, 1, ColorRGBA(1.0f, 0.5f, 0.0f, 1.0f));
		Source.m_vCharOffsets.emplace_back(Index, 2.0f, -3.0f);
	}
	Source.m_vSelectionQuads.emplace_back(1.0f, 2.0f, 3.0f, 4.0f);

	const CTextCursor Measure = QmTextWordMeasureCursor(Source, 12.0f, 20.0f);
	EXPECT_EQ(Measure.m_vColorSplits.capacity(), 0u);
	EXPECT_EQ(Measure.m_vCharOffsets.capacity(), 0u);
	EXPECT_EQ(Measure.m_vSelectionQuads.capacity(), 0u);
	EXPECT_EQ(Measure.m_CalculateSelectionMode, TEXT_CURSOR_SELECTION_MODE_NONE);
	EXPECT_EQ(Measure.m_CursorMode, TEXT_CURSOR_CURSOR_MODE_NONE);
	EXPECT_EQ(Measure.m_Flags, TEXTFLAG_DISALLOW_NEWLINE);
	// 临时测量不转移或修改真正绘制所需的整段装饰数据。
	EXPECT_EQ(Source.m_vColorSplits.size(), 4096u);
	EXPECT_EQ(Source.m_vCharOffsets.size(), 4096u);
	EXPECT_EQ(Source.m_vSelectionQuads.size(), 1u);
	EXPECT_EQ(Source.m_Flags, TEXTFLAG_RENDER);
	EXPECT_EQ(Source.m_CalculateSelectionMode, TEXT_CURSOR_SELECTION_MODE_CALCULATE);
}

TEST(QmTextWordCursor, AppendedWordKeepsLineOriginAndCharacterProgress)
{
	CTextCursor Source;
	Source.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_STOP_AT_END;
	Source.m_StartX = 10.0f;
	Source.m_StartY = 15.0f;
	Source.m_X = 10.0f;
	Source.m_Y = 15.0f;
	Source.m_LineWidth = 90.0f;
	Source.m_FontSize = 13.5f;
	Source.m_LineSpacing = 2.5f;
	Source.m_LineCount = 3;
	Source.m_MaxLines = 4;
	Source.m_GlyphCount = 5;
	Source.m_CharCount = 9;

	const CTextCursor Measure = QmTextWordMeasureCursor(Source, 42.0f, 47.0f);
	// 测量从当前绘制位置开始，但截断仍相对于原行起点；保留首字形判定。
	EXPECT_FLOAT_EQ(Measure.m_X, 42.0f);
	EXPECT_FLOAT_EQ(Measure.m_Y, 47.0f);
	EXPECT_FLOAT_EQ(Measure.m_StartX, 10.0f);
	EXPECT_FLOAT_EQ(Measure.m_StartY, 15.0f);
	EXPECT_FLOAT_EQ(Measure.m_LineWidth, 90.0f);
	EXPECT_FLOAT_EQ(Measure.m_FontSize, 13.5f);
	EXPECT_FLOAT_EQ(Measure.m_LineSpacing, 2.5f);
	EXPECT_EQ(Measure.m_LineCount, 3);
	EXPECT_EQ(Measure.m_MaxLines, 4);
	EXPECT_EQ(Measure.m_GlyphCount, 5);
	EXPECT_EQ(Measure.m_CharCount, 9);
	EXPECT_EQ(Measure.m_Flags, TEXTFLAG_STOP_AT_END | TEXTFLAG_DISALLOW_NEWLINE);
	EXPECT_FLOAT_EQ(Source.m_X, 10.0f);
}
