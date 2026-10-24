#include <game/client/components/qmclient/colored_parts.h>
#include <game/client/components/qmclient/console_syntax.h>
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

TEST(QmConsoleTextPipeline, SearchOverridesLinkAndCommandSyntaxWithoutChangingUtf8Ranges)
{
	const char *pText = "> echo \"中文 https://example.test\"";
	const auto Palette = QmConsoleAppearance::ResolvePalette(0, 96, {});
	std::vector<STextColorSplit> vLayers{{0, str_length(pText), Palette.m_aColors[QmConsoleAppearance::TEXT]}};
	QmConsoleSyntax::AppendColors(pText + 2, Palette, vLayers, 2);
	std::vector<QmConsoleText::SRange> vLinks, vMatches;
	QmConsoleText::CollectLinks(pText, vLinks);
	QmConsoleText::CollectSearchMatches(pText, "example", vMatches);
	ASSERT_EQ(vLinks.size(), 1u);
	ASSERT_EQ(vMatches.size(), 1u);
	EXPECT_EQ(vLinks[0].m_StartByte, 15);
	EXPECT_EQ(vLinks[0].m_StartChar, 11);
	vLayers.emplace_back(vLinks[0].m_StartByte, vLinks[0].m_EndByte - vLinks[0].m_StartByte, Palette.m_aColors[QmConsoleAppearance::LINK]);
	vLayers.emplace_back(vMatches[0].m_StartByte, vMatches[0].m_EndByte - vMatches[0].m_StartByte, Palette.m_aColors[QmConsoleAppearance::SEARCH_SELECTED]);
	std::vector<STextColorSplit> vResult;
	QmConsoleText::ComposeColorSplits(pText, vLayers, vResult);
	const auto ColorAt = [&](int Byte) {
		for(const auto &Split : vResult)
			if(Byte >= Split.m_CharIndex && Byte < Split.m_CharIndex + Split.m_Length)
				return Split.m_Color;
		return ColorRGBA();
	};
	EXPECT_EQ(ColorAt(2), Palette.m_aColors[QmConsoleAppearance::COMMAND]);
	EXPECT_EQ(ColorAt(8), Palette.m_aColors[QmConsoleAppearance::STRING]);
	EXPECT_EQ(ColorAt(vLinks[0].m_StartByte), Palette.m_aColors[QmConsoleAppearance::LINK]);
	EXPECT_EQ(ColorAt(vMatches[0].m_StartByte), Palette.m_aColors[QmConsoleAppearance::SEARCH_SELECTED]);
	EXPECT_EQ(ColorAt(vMatches[0].m_EndByte), Palette.m_aColors[QmConsoleAppearance::LINK]);
}

TEST(QmConsoleTextPipeline, PreparedDecorationKeepsMarkerSyntaxLinkAndSearchPriority)
{
	const CColoredParts Text("> echo \"中文 [[red]]https://example.test\"", true);
	CTextCursor Cursor;
	Text.AddSplitsToCursor(Cursor);
	ASSERT_EQ(Cursor.m_vColorSplits.size(), 1u);
	const auto Palette = QmConsoleAppearance::ResolvePalette(0, 96, {});
	std::vector<QmConsoleText::SRange> vMatches;
	QmConsoleText::CollectSearchMatches(Text.Text(), "example", vMatches);
	ASSERT_EQ(vMatches.size(), 1u);
	QmConsoleText::CColorPreparation Preparation;
	ASSERT_TRUE(Preparation.Prepare(Text.Text(), Cursor.m_vColorSplits, [&](auto &vLayers, const auto &vLinks) {
		vLayers.emplace_back(0, str_length(Text.Text()), Palette.m_aColors[QmConsoleAppearance::TEXT]);
		QmConsoleSyntax::AppendColors(Text.Text() + 2, Palette, vLayers, 2);
		vLayers.insert(vLayers.end(), Cursor.m_vColorSplits.begin(), Cursor.m_vColorSplits.end());
		for(const auto &Range : vLinks)
			vLayers.emplace_back(Range.m_StartByte, Range.m_EndByte - Range.m_StartByte, Palette.m_aColors[QmConsoleAppearance::LINK]);
		vLayers.emplace_back(vMatches[0].m_StartByte, vMatches[0].m_EndByte - vMatches[0].m_StartByte, Palette.m_aColors[QmConsoleAppearance::SEARCH_SELECTED]);
		return true;
	}));
	ASSERT_EQ(Preparation.Links().size(), 1u);
	EXPECT_EQ(Preparation.Links()[0].m_StartByte, 15);
	EXPECT_EQ(Preparation.Links()[0].m_StartChar, 11);
	const auto ColorAt = [&](int Byte) {
		for(const auto &Split : Cursor.m_vColorSplits)
			if(Byte >= Split.m_CharIndex && Byte < Split.m_CharIndex + Split.m_Length)
				return Split.m_Color;
		return ColorRGBA();
	};
	EXPECT_EQ(ColorAt(2), Palette.m_aColors[QmConsoleAppearance::COMMAND]);
	EXPECT_EQ(ColorAt(9), Palette.m_aColors[QmConsoleAppearance::STRING]);
	EXPECT_EQ(ColorAt(15), Palette.m_aColors[QmConsoleAppearance::LINK]);
	EXPECT_EQ(ColorAt(vMatches[0].m_StartByte), Palette.m_aColors[QmConsoleAppearance::SEARCH_SELECTED]);
	EXPECT_EQ(ColorAt(vMatches[0].m_EndByte), Palette.m_aColors[QmConsoleAppearance::LINK]);
}

TEST(QmConsoleTextPipeline, PreviewLinksKeepTheirUtf8AndNewlineCoordinatesWhenNormalized)
{
	const char *pText = "> echo \"中文\"\nWWW.example.test/path.";
	const auto Palette = QmConsoleAppearance::ResolvePalette(0, 96, {});
	QmConsoleText::CColorPreparation Preparation;
	CTextCursor Cursor;
	ASSERT_TRUE(Preparation.Prepare(pText, Cursor.m_vColorSplits, [&](auto &vLayers, const auto &vLinks) {
		QmConsoleSyntax::AppendColors(pText + 2, Palette, vLayers, 2);
		for(const auto &Range : vLinks)
			vLayers.emplace_back(Range.m_StartByte, Range.m_EndByte - Range.m_StartByte, Palette.m_aColors[QmConsoleAppearance::LINK]);
		return true;
	}));
	ASSERT_EQ(Preparation.Links().size(), 1u);
	const auto &Link = Preparation.Links()[0];
	EXPECT_EQ(Link.m_StartByte, 16);
	EXPECT_EQ(Link.m_StartChar, 11);
	std::string Url;
	ASSERT_TRUE(QmConsoleText::TryLinkUrl(pText, Link, Url));
	EXPECT_EQ(Url, "https://WWW.example.test/path");
	bool LinkColored = false;
	for(const auto &Split : Cursor.m_vColorSplits)
		if(Split.m_CharIndex == Link.m_StartByte)
		{
			EXPECT_EQ(Split.m_Color, Palette.m_aColors[QmConsoleAppearance::LINK]);
			EXPECT_EQ(Split.m_Length, Link.m_EndByte - Link.m_StartByte);
			LinkColored = true;
		}
	EXPECT_TRUE(LinkColored);
}
