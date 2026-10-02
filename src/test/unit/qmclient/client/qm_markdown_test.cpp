#include <game/client/components/qmclient/qm_markdown.h>

#include <gtest/gtest.h>

#include <string>

TEST(QmMarkdown, ParsesRestrictedBlocksAndSafeLinks)
{
	const auto vBlocks = qm_md::Parse("# Title\n- **bold** and *italic* [safe](https://example.test)\n> `code`\n---");
	ASSERT_EQ(vBlocks.size(), 4u);
	EXPECT_EQ(vBlocks[0].m_Kind, qm_md::EBlockKind::HEADING1);
	EXPECT_EQ(vBlocks[1].m_Kind, qm_md::EBlockKind::BULLET);
	ASSERT_EQ(vBlocks[1].m_vSpans.size(), 5u);
	EXPECT_TRUE(vBlocks[1].m_vSpans[0].m_Bold);
	EXPECT_TRUE(vBlocks[1].m_vSpans[2].m_Italic);
	EXPECT_EQ(vBlocks[1].m_vSpans[4].m_Link, "https://example.test");
	EXPECT_EQ(vBlocks[2].m_Kind, qm_md::EBlockKind::QUOTE);
	EXPECT_TRUE(vBlocks[2].m_vSpans[0].m_Code);
	EXPECT_EQ(vBlocks[3].m_Kind, qm_md::EBlockKind::SEPARATOR);
}

TEST(QmMarkdown, RejectsUnsafeLinksAndCapsInput)
{
	const auto vBlocks = qm_md::Parse("[unsafe](file:///tmp/a) [safe](http://example.test)", {.m_MaxBlocks = 4, .m_MaxSpans = 4, .m_MaxBytes = 1024});
	ASSERT_EQ(vBlocks.size(), 1u);
	ASSERT_EQ(vBlocks[0].m_vSpans.size(), 2u);
	EXPECT_EQ(vBlocks[0].m_vSpans[0].m_Text, "[unsafe](file:///tmp/a) ");
	EXPECT_EQ(vBlocks[0].m_vSpans[1].m_Link, "http://example.test");
	EXPECT_LE(qm_md::Parse(std::string(512, 'x').c_str(), {.m_MaxBlocks = 3, .m_MaxSpans = 3, .m_MaxBytes = 8})[0].m_vSpans[0].m_Text.size(), 8u);
}

TEST(QmMarkdown, EmitsSettingsButtonsOutsideTextFlow)
{
	const auto vBlocks = qm_md::Parse("before [[settings:qm:lyrics|Open lyrics]] after");
	ASSERT_EQ(vBlocks.size(), 2u);
	EXPECT_EQ(vBlocks[0].m_Kind, qm_md::EBlockKind::PARAGRAPH);
	EXPECT_EQ(vBlocks[1].m_Kind, qm_md::EBlockKind::SETTINGS_BUTTON);
	EXPECT_EQ(vBlocks[1].m_SettingsCardId, "qm:lyrics");
	EXPECT_EQ(vBlocks[1].m_SettingsLabel, "Open lyrics");
}

TEST(QmMarkdown, KeepsUnsupportedSyntaxAsNonClickableText)
{
	const auto vBlocks = qm_md::Parse("[unsafe](javascript:alert(1))\n![image](https://example.test/image.png)\n<script>x</script>");
	ASSERT_EQ(vBlocks.size(), 3u);
	EXPECT_EQ(vBlocks[0].m_vSpans[0].m_Text, "[unsafe](javascript:alert(1))");
	EXPECT_TRUE(vBlocks[0].m_vSpans[0].m_Link.empty());
	EXPECT_EQ(vBlocks[1].m_vSpans[0].m_Text, "![image](https://example.test/image.png)");
	EXPECT_TRUE(vBlocks[1].m_vSpans[0].m_Link.empty());
	EXPECT_EQ(vBlocks[2].m_vSpans[0].m_Text, "<script>x</script>");
}

TEST(QmMarkdown, NormalizesLineEndingsAndNumbers)
{
	const auto vBlocks = qm_md::Parse("1. one\r\n2. two\rparagraph\n1. reset");
	ASSERT_EQ(vBlocks.size(), 4u);
	EXPECT_EQ(vBlocks[0].m_Number, 1);
	EXPECT_EQ(vBlocks[1].m_Number, 2);
	EXPECT_EQ(vBlocks[3].m_Number, 1);
	const auto vGlyphs = qm_md::SplitUtf8("a中🙂");
	ASSERT_EQ(vGlyphs.size(), 3u);
	EXPECT_EQ(vGlyphs[2], "🙂");
}
