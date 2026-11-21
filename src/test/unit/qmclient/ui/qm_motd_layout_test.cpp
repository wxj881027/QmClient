#include <base/str.h>

#include <game/client/QmUi/QmMotdLayout.h>
#include <game/client/components/motd.h>

#include <gtest/gtest.h>

using namespace QmMotdLayout;

namespace
{
	constexpr float NATURAL_CELL = 8.0f;
	constexpr float LINE_HEIGHT = 16.0f;

	int LengthOf(const char *pText)
	{
		return (int)str_length(pText);
	}

	// 框线行的相邻横线要首尾相接：行宽必须正好是「字形数 × 推进」。
	float RowWidthOf(const SLine &Line)
	{
		return (float)Line.m_VisibleGlyphCount * Line.m_Advance;
	}
}

TEST(QmMotdLayout, BoxOnlyLineKeepsNaturalCellAndZeroLetterSpacing)
{
	const auto Layout = Build("┌───┐", 100.0f, NATURAL_CELL, LINE_HEIGHT);
	ASSERT_EQ(Layout.LineCount(), 1);
	const SLine &Line = Layout.m_vLines[0];
	EXPECT_EQ(Line.m_Mode, ELineMode::BOX);
	EXPECT_EQ(Line.m_GlyphCount, 5);
	EXPECT_EQ(Line.m_VisibleGlyphCount, 5);
	// 字间距为 0：一行只有一个推进量，且不被宽裕的可用宽度拉伸。
	EXPECT_FLOAT_EQ(Line.m_Advance, NATURAL_CELL);
	EXPECT_FLOAT_EQ(RowWidthOf(Line), 40.0f);
	EXPECT_FLOAT_EQ(Layout.MaxBoxLineWidth(), 40.0f);
	EXPECT_EQ(Layout.m_BoxLineCount, 1);
	EXPECT_TRUE(Layout.HasBoxLines());
}

TEST(QmMotdLayout, RowWithPlainTextStaysOrdinaryEvenIfFramed)
{
	const char *pText = "│ Hi │";
	const auto Layout = Build(pText, 100.0f, NATURAL_CELL, LINE_HEIGHT);
	ASSERT_EQ(Layout.LineCount(), 1);
	const SLine &Line = Layout.m_vLines[0];
	EXPECT_EQ(Line.m_Mode, ELineMode::TEXT);
	// 普通行不参与等宽推进，推进量为 0 表示交给文本排版。
	EXPECT_FLOAT_EQ(Line.m_Advance, 0.0f);
	EXPECT_EQ(Line.m_VisibleGlyphCount, Line.m_GlyphCount);
	EXPECT_EQ(Line.m_ByteLength, LengthOf(pText));
}

TEST(QmMotdLayout, BoxLineDetectionRequiresEveryVisibleGlyphToBeBoxDrawing)
{
	const char *pFrame = "├──┬──┤";
	const char *pLetters = "│ Hi │";
	const char *pIndentedFrame = "  ┌─┐";
	EXPECT_TRUE(IsBoxDrawingLine(pFrame, LengthOf(pFrame)));
	EXPECT_FALSE(IsBoxDrawingLine(pLetters, LengthOf(pLetters)));
	// 缩进的框线行仍然成框：空格只占格子，不打断等宽推进。
	EXPECT_TRUE(IsBoxDrawingLine(pIndentedFrame, LengthOf(pIndentedFrame)));
	EXPECT_TRUE(IsBoxDrawingLine("███", LengthOf("███")));
	// 空行与整行空格都不算框线行。
	EXPECT_FALSE(IsBoxDrawingLine("", 0));
	EXPECT_FALSE(IsBoxDrawingLine("   ", 3));
	EXPECT_TRUE(IsBoxDrawingCodepoint(0x2500));
	EXPECT_TRUE(IsBoxDrawingCodepoint(0x257F));
	EXPECT_FALSE(IsBoxDrawingCodepoint('A'));
}

TEST(QmMotdLayout, AdvanceFollowsAvailableWidthOnlyWhenTheRowIsTooWide)
{
	// 放得下：推进就是字形自然推进，可用宽度不参与拉伸。
	const auto Fits = Build("──────────", 100.0f, NATURAL_CELL, LINE_HEIGHT);
	ASSERT_EQ(Fits.LineCount(), 1);
	EXPECT_FLOAT_EQ(Fits.m_vLines[0].m_Advance, NATURAL_CELL);
	EXPECT_EQ(Fits.m_vLines[0].m_VisibleGlyphCount, 10);
	EXPECT_FLOAT_EQ(RowWidthOf(Fits.m_vLines[0]), 80.0f);

	// 刚好超宽：推进压到「可用宽度 / 字形数」，整行正好填满且没有字距。
	const auto Compressed = Build("──────────", 40.0f, NATURAL_CELL, LINE_HEIGHT);
	ASSERT_EQ(Compressed.LineCount(), 1);
	EXPECT_FLOAT_EQ(Compressed.m_vLines[0].m_Advance, 4.0f);
	EXPECT_EQ(Compressed.m_vLines[0].m_VisibleGlyphCount, 10);
	EXPECT_FLOAT_EQ(RowWidthOf(Compressed.m_vLines[0]), 40.0f);
}

TEST(QmMotdLayout, RowNarrowerThanTheCompressionFloorIsTruncatedInsteadOfOverlapping)
{
	// 20 宽要塞 10 格只剩下 2 的推进，低于自然推进的一半：压到下限，多数格子截断。
	const auto Layout = Build("──────────", 20.0f, NATURAL_CELL, LINE_HEIGHT);
	ASSERT_EQ(Layout.LineCount(), 1);
	const SLine &Line = Layout.m_vLines[0];
	EXPECT_EQ(Line.m_Mode, ELineMode::BOX);
	EXPECT_EQ(Line.m_GlyphCount, 10);
	EXPECT_FLOAT_EQ(Line.m_Advance, NATURAL_CELL * ui_token::motd::BOX_CELL_MIN_RATIO);
	EXPECT_EQ(Line.m_VisibleGlyphCount, 5);
	// 截断后行宽不超过可用宽度。
	EXPECT_LE(RowWidthOf(Line), 20.0f + 0.001f);
}

TEST(QmMotdLayout, LineCountAndHeightFollowUniformLineHeight)
{
	const char *pMotd = "┌─┐\nHi\n└─┘";
	const auto Layout = Build(pMotd, 100.0f, NATURAL_CELL, LINE_HEIGHT);
	ASSERT_EQ(Layout.LineCount(), 3);
	EXPECT_FLOAT_EQ(Layout.m_LineHeight, LINE_HEIGHT);
	EXPECT_FLOAT_EQ(Layout.Height(), 3.0f * LINE_HEIGHT);
	EXPECT_FLOAT_EQ(Layout.m_vLines[0].m_Y, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_vLines[1].m_Y, LINE_HEIGHT);
	EXPECT_FLOAT_EQ(Layout.m_vLines[2].m_Y, 2.0f * LINE_HEIGHT);

	EXPECT_EQ(Layout.m_vLines[0].m_Mode, ELineMode::BOX);
	EXPECT_EQ(Layout.m_vLines[1].m_Mode, ELineMode::TEXT);
	EXPECT_EQ(Layout.m_vLines[2].m_Mode, ELineMode::BOX);
	EXPECT_EQ(Layout.m_BoxLineCount, 2);

	// 字节偏移逐行累加，调用点据此切子串，不需要再扫一遍文本。
	EXPECT_EQ(Layout.m_vLines[0].m_ByteOffset, 0);
	EXPECT_EQ(Layout.m_vLines[1].m_ByteOffset, LengthOf("┌─┐\n"));
	EXPECT_EQ(Layout.m_vLines[2].m_ByteOffset, LengthOf("┌─┐\nHi\n"));
}

TEST(QmMotdLayout, EmptyRowsArePlainTextAndStillOccupyOneLine)
{
	const auto Layout = Build("\n\n", 100.0f, NATURAL_CELL, LINE_HEIGHT);
	ASSERT_EQ(Layout.LineCount(), 3);
	for(const SLine &Line : Layout.m_vLines)
	{
		EXPECT_EQ(Line.m_Mode, ELineMode::TEXT);
		EXPECT_EQ(Line.m_GlyphCount, 0);
		EXPECT_EQ(Line.m_VisibleGlyphCount, 0);
		EXPECT_FLOAT_EQ(Line.m_Advance, 0.0f);
	}
	EXPECT_FALSE(Layout.HasBoxLines());
	EXPECT_FLOAT_EQ(Layout.Height(), 3.0f * LINE_HEIGHT);
}

TEST(QmMotdLayout, SingleBoxGlyphIsALayoutableBoxRow)
{
	const auto Layout = Build("─", 100.0f, NATURAL_CELL, LINE_HEIGHT);
	ASSERT_EQ(Layout.LineCount(), 1);
	EXPECT_EQ(Layout.m_vLines[0].m_Mode, ELineMode::BOX);
	EXPECT_EQ(Layout.m_vLines[0].m_VisibleGlyphCount, 1);
	EXPECT_FLOAT_EQ(Layout.MaxBoxLineWidth(), NATURAL_CELL);
}

TEST(QmMotdLayout, CrlfTerminatorsAreNotCountedAsVisibleGlyphs)
{
	const auto Layout = Build("│\r\nHi\r\n", 100.0f, NATURAL_CELL, LINE_HEIGHT);
	ASSERT_EQ(Layout.LineCount(), 3);
	EXPECT_EQ(Layout.m_vLines[0].m_Mode, ELineMode::BOX);
	EXPECT_EQ(Layout.m_vLines[0].m_GlyphCount, 1);
	EXPECT_EQ(Layout.m_vLines[0].m_ByteLength, 3);
	EXPECT_EQ(Layout.m_vLines[1].m_ByteOffset, 5);
	EXPECT_EQ(Layout.m_vLines[1].m_ByteLength, 2);
	EXPECT_EQ(Layout.m_vLines[1].m_GlyphCount, 2);
	EXPECT_EQ(Layout.m_vLines[2].m_ByteOffset, 9);
	EXPECT_EQ(Layout.m_vLines[2].m_GlyphCount, 0);
}

TEST(QmMotdLayout, DegenerateInputsStayFinite)
{
	const auto Null = Build(nullptr, 100.0f, NATURAL_CELL, LINE_HEIGHT);
	EXPECT_EQ(Null.LineCount(), 0);
	EXPECT_FLOAT_EQ(Null.Height(), 0.0f);

	// 内容宽度为 0（面板还没布局）：一格都放不下，不允许出现负推进或除零。
	const auto NoWidth = Build("────", 0.0f, NATURAL_CELL, LINE_HEIGHT);
	ASSERT_EQ(NoWidth.LineCount(), 1);
	EXPECT_EQ(NoWidth.m_vLines[0].m_Mode, ELineMode::BOX);
	EXPECT_EQ(NoWidth.m_vLines[0].m_VisibleGlyphCount, 0);
	EXPECT_FLOAT_EQ(NoWidth.MaxBoxLineWidth(), 0.0f);

	// 字形宽度未知（0）：退回行高当格宽，至少还是一个不重叠的方形格子。
	const auto FallbackCell = Build("────", 100.0f, 0.0f, LINE_HEIGHT);
	ASSERT_EQ(FallbackCell.LineCount(), 1);
	EXPECT_FLOAT_EQ(FallbackCell.m_vLines[0].m_Advance, LINE_HEIGHT);
	EXPECT_EQ(FallbackCell.m_vLines[0].m_VisibleGlyphCount, 4);
}

TEST(QmMotdPopup, JoinHideSuppressesAutomaticPopup)
{
	// 开关打开时不再自动弹公告；公告正文仍保留给服务器信息面板。
	EXPECT_FALSE(CMotd::ShouldActivateMotdPopup("motd", 10, true));
	EXPECT_TRUE(CMotd::ShouldActivateMotdPopup("motd", 10, false));
}

TEST(QmMotdPopup, ActivationRequiresContentAndPositiveTime)
{
	EXPECT_FALSE(CMotd::ShouldActivateMotdPopup(nullptr, 10, false));
	EXPECT_FALSE(CMotd::ShouldActivateMotdPopup("", 10, false));
	EXPECT_FALSE(CMotd::ShouldActivateMotdPopup("motd", 0, false));
	EXPECT_FALSE(CMotd::ShouldActivateMotdPopup("motd", -1, false));
	EXPECT_TRUE(CMotd::ShouldActivateMotdPopup("motd", 10, false));
}
