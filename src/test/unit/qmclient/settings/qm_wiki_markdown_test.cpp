// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <game/client/QmUi/QmWikiMarkdown.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	std::string JoinSpans(const std::vector<SWikiSpan> &vSpans)
	{
		std::string Text;
		for(const SWikiSpan &Span : vSpans)
			Text += Span.m_Text;
		return Text;
	}

	bool HasStyle(const std::vector<SWikiSpan> &vSpans, unsigned Style, const std::string &Text)
	{
		for(const SWikiSpan &Span : vSpans)
		{
			if((Span.m_Style & Style) != 0 && Span.m_Text == Text)
				return true;
		}
		return false;
	}
} // namespace

TEST(WikiMarkdownTest, ParsesTitleHeadingAndParagraph)
{
	CWikiMarkdown Document;
	ASSERT_TRUE(Document.Parse("# Voice settings\n\nThe slider controls **your volume** and *others*.\n"));
	ASSERT_EQ(Document.Blocks().size(), 2u);
	EXPECT_EQ(Document.Title(), "Voice settings");

	const SWikiBlock &Heading = Document.Blocks()[0];
	EXPECT_EQ(Heading.m_Type, EWikiBlockType::HEADING);
	EXPECT_EQ(Heading.m_Level, 1);

	const SWikiBlock &Paragraph = Document.Blocks()[1];
	EXPECT_EQ(Paragraph.m_Type, EWikiBlockType::PARAGRAPH);
	EXPECT_EQ(JoinSpans(Paragraph.m_Spans), "The slider controls your volume and others.");
	EXPECT_TRUE(HasStyle(Paragraph.m_Spans, WIKI_INLINE_BOLD, "your volume"));
	EXPECT_TRUE(HasStyle(Paragraph.m_Spans, WIKI_INLINE_ITALIC, "others"));
}

TEST(WikiMarkdownTest, MergesContiguousLinesIntoOneParagraph)
{
	CWikiMarkdown Document;
	ASSERT_TRUE(Document.Parse("first line\nsecond line\n\nnext paragraph\n"));
	ASSERT_EQ(Document.Blocks().size(), 2u);
	EXPECT_EQ(JoinSpans(Document.Blocks()[0].m_Spans), "first line second line");
	EXPECT_EQ(JoinSpans(Document.Blocks()[1].m_Spans), "next paragraph");
}

TEST(WikiMarkdownTest, ParsesBulletAndOrderedListsWithIndent)
{
	CWikiMarkdown Document;
	ASSERT_TRUE(Document.Parse("- one\n- two\n  - nested\n\n1. first\n2. second\n"));
	ASSERT_EQ(Document.Blocks().size(), 5u);
	EXPECT_EQ(Document.Blocks()[0].m_Type, EWikiBlockType::BULLET);
	EXPECT_EQ(JoinSpans(Document.Blocks()[0].m_Spans), "one");
	EXPECT_EQ(Document.Blocks()[2].m_Level, 1);
	EXPECT_EQ(Document.Blocks()[3].m_Type, EWikiBlockType::ORDERED);
	EXPECT_EQ(Document.Blocks()[3].m_Order, 1);
	EXPECT_EQ(Document.Blocks()[4].m_Order, 2);
}

TEST(WikiMarkdownTest, ParsesTableRowsAndAlignment)
{
	CWikiMarkdown Document;
	ASSERT_TRUE(Document.Parse("| Setting | Default |\n|:--------|--------:|\n| Volume | 50 |\n| Duck | **yes** |\n"));
	ASSERT_EQ(Document.Blocks().size(), 1u);
	const SWikiBlock &Table = Document.Blocks()[0];
	EXPECT_EQ(Table.m_Type, EWikiBlockType::TABLE);
	ASSERT_EQ(Table.m_Rows.size(), 3u);
	ASSERT_EQ(Table.m_vAligns.size(), 2u);
	EXPECT_EQ(Table.m_vAligns[0], 'l');
	EXPECT_EQ(Table.m_vAligns[1], 'r');
	ASSERT_EQ(Table.m_Rows[0].m_Cells.size(), 2u);
	EXPECT_EQ(JoinSpans(Table.m_Rows[0].m_Cells[0]), "Setting");
	EXPECT_EQ(JoinSpans(Table.m_Rows[2].m_Cells[1]), "yes");
	EXPECT_TRUE(HasStyle(Table.m_Rows[2].m_Cells[1], WIKI_INLINE_BOLD, "yes"));
}

TEST(WikiMarkdownTest, ParsesMediaCodeMathQuoteAndDivider)
{
	CWikiMarkdown Document;
	ASSERT_TRUE(Document.Parse("![Voice panel](voice.mp4)\n\n"
				   "```c++\nint x = 1;\n```\n\n"
				   "$$\na = b + c\n$$\n\n> quoted text\n\n---\n"));
	const std::vector<SWikiBlock> &vBlocks = Document.Blocks();
	ASSERT_EQ(vBlocks.size(), 5u);
	EXPECT_EQ(vBlocks[0].m_Type, EWikiBlockType::IMAGE);
	EXPECT_EQ(vBlocks[0].m_MediaPath, "voice.mp4");
	EXPECT_EQ(vBlocks[0].m_AltText, "Voice panel");
	EXPECT_EQ(vBlocks[1].m_Type, EWikiBlockType::CODE);
	EXPECT_EQ(vBlocks[1].m_Text, "int x = 1;");
	EXPECT_EQ(vBlocks[2].m_Type, EWikiBlockType::MATH);
	EXPECT_EQ(vBlocks[2].m_Text, "a = b + c");
	EXPECT_EQ(vBlocks[3].m_Type, EWikiBlockType::QUOTE);
	EXPECT_EQ(JoinSpans(vBlocks[3].m_Spans), "quoted text");
	EXPECT_EQ(vBlocks[4].m_Type, EWikiBlockType::DIVIDER);
}

TEST(WikiMarkdownTest, KeepsUnclosedMarkersAndIdentifiersLiteral)
{
	std::vector<SWikiSpan> vSpans;
	CWikiMarkdown::ParseInline("use qm_voice and **unclosed", &vSpans);
	EXPECT_EQ(JoinSpans(vSpans), "use qm_voice and **unclosed");
	for(const SWikiSpan &Span : vSpans)
		EXPECT_EQ(Span.m_Style, 0u);
}

TEST(WikiMarkdownTest, ParsesInlineLinkAndMath)
{
	std::vector<SWikiSpan> vSpans;
	CWikiMarkdown::ParseInline("see [docs](https://example.com) and $a^2$", &vSpans);
	ASSERT_GE(vSpans.size(), 3u);
	EXPECT_TRUE(HasStyle(vSpans, WIKI_INLINE_LINK, "docs"));
	EXPECT_TRUE(HasStyle(vSpans, WIKI_INLINE_MATH, "a^2"));
	for(const SWikiSpan &Span : vSpans)
	{
		if((Span.m_Style & WIKI_INLINE_LINK) != 0)
			EXPECT_EQ(Span.m_Target, "https://example.com");
	}
}
