#include <game/client/components/qmclient/colored_parts.h>
#include <game/client/components/qmclient/console_text.h>

#include <gtest/gtest.h>

TEST(QmConsoleTextPipeline, HiddenColorMarkersDoNotShiftLinksOrSearchMatches)
{
	const CColoredParts Text("中文 [[red]]https://example.test", true);
	ASSERT_STREQ(Text.Text(), "中文 https://example.test");
	std::vector<QmConsoleText::SRange> vLinks, vMatches;
	QmConsoleText::CollectLinks(Text.Text(), vLinks);
	QmConsoleText::CollectSearchMatches(Text.Text(), "example", vMatches);
	ASSERT_EQ(vLinks.size(), 1u);
	EXPECT_EQ(vLinks[0].m_StartByte, 7);
	EXPECT_EQ(vLinks[0].m_StartChar, 3);
	ASSERT_EQ(vMatches.size(), 1u);
	EXPECT_EQ(vMatches[0].m_StartByte, 15);
	QmConsoleText::CollectSearchMatches(Text.Text(), "red", vMatches);
	EXPECT_TRUE(vMatches.empty());
}

TEST(QmConsoleTextPipeline, SearchOverridesMarkerColorWhileUnmatchedSuffixKeepsIt)
{
	const CColoredParts Text("中文 [[red]]match suffix", true);
	CTextCursor Cursor;
	Text.AddSplitsToCursor(Cursor);
	ASSERT_EQ(Cursor.m_vColorSplits.size(), 1u);
	const ColorRGBA MarkerColor = Cursor.m_vColorSplits[0].m_Color;
	std::vector<STextColorSplit> vLayers{{0, str_length(Text.Text()), ColorRGBA(1, 1, 1, 1)}};
	vLayers.insert(vLayers.end(), Cursor.m_vColorSplits.begin(), Cursor.m_vColorSplits.end());
	std::vector<QmConsoleText::SRange> vMatches;
	QmConsoleText::CollectSearchMatches(Text.Text(), "match", vMatches);
	ASSERT_EQ(vMatches.size(), 1u);
	const ColorRGBA SearchColor(1, 1, 0, 1);
	vLayers.emplace_back(vMatches[0].m_StartByte, vMatches[0].m_EndByte - vMatches[0].m_StartByte, SearchColor);
	std::vector<STextColorSplit> vResult;
	QmConsoleText::ComposeColorSplits(Text.Text(), vLayers, vResult);
	ASSERT_EQ(vResult.size(), 3u);
	EXPECT_EQ(vResult[1].m_CharIndex, 7);
	EXPECT_EQ(vResult[1].m_Length, 5);
	EXPECT_EQ(vResult[1].m_Color, SearchColor);
	EXPECT_EQ(vResult[2].m_Color, MarkerColor);
}
