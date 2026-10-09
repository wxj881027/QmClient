#include <game/client/components/qmclient/console_text.h>

#include <gtest/gtest.h>

namespace
{
	std::vector<std::string> LinkUrls(const char *pText)
	{
		std::vector<QmConsoleText::SRange> vLinks;
		QmConsoleText::CollectLinks(pText, vLinks);
		std::vector<std::string> vUrls;
		for(const auto &Link : vLinks)
			vUrls.push_back(QmConsoleText::LinkUrl(pText, Link));
		return vUrls;
	}

	const STextColorSplit *ColorAt(const std::vector<STextColorSplit> &vSplits, int Byte)
	{
		for(const auto &Split : vSplits)
			if(Byte >= Split.m_CharIndex && Byte < Split.m_CharIndex + Split.m_Length)
				return &Split;
		return nullptr;
	}
}

TEST(QmConsoleLinks, ChinesePrefixKeepsByteAndCharacterCoordinatesSeparate)
{
	std::vector<QmConsoleText::SRange> vLinks;
	QmConsoleText::CollectLinks("中文 https://example.test", vLinks);
	ASSERT_EQ(vLinks.size(), 1u);
	EXPECT_EQ(vLinks[0].m_StartByte, 7);
	EXPECT_EQ(vLinks[0].m_StartChar, 3);
	EXPECT_EQ(vLinks[0].m_EndByte, 27);
	EXPECT_EQ(vLinks[0].m_EndChar, 23);
}

TEST(QmConsoleLinks, ChinesePunctuationSeparatesAdjacentLinks)
{
	EXPECT_EQ(LinkUrls("地址：https://one.test，https://two.test。后文"),
		(std::vector<std::string>{"https://one.test", "https://two.test"}));
}

TEST(QmConsoleLinks, ExplicitNewlineDoesNotShiftLinkSelectionGlyphs)
{
	std::vector<QmConsoleText::SRange> vLinks;
	QmConsoleText::CollectLinks("前\nhttps://example.test", vLinks);
	ASSERT_EQ(vLinks.size(), 1u);
	EXPECT_EQ(vLinks[0].m_StartByte, 4);
	EXPECT_EQ(vLinks[0].m_StartChar, 1);
	EXPECT_EQ(vLinks[0].m_EndChar, 21);
}

TEST(QmConsoleLinks, QuotesAndUnicodeWhitespaceEndLinks)
{
	EXPECT_EQ(LinkUrls("“https://one.test”\u00a0'https://two.test'"),
		(std::vector<std::string>{"https://one.test", "https://two.test"}));
}

TEST(QmConsoleLinks, BalancedPathParenthesesAndIpv6BracketsRemainInUrl)
{
	EXPECT_EQ(LinkUrls("(https://example.test/wiki/Name_(topic)). https://[::1]:8080/path"),
		(std::vector<std::string>{"https://example.test/wiki/Name_(topic)", "https://[::1]:8080/path"}));
}

TEST(QmConsoleLinks, UrlQueryPunctuationIsKeptWhileSentenceSuffixIsRemoved)
{
	EXPECT_EQ(LinkUrls("https://example.test/?a=1,2&b=x:y!"),
		(std::vector<std::string>{"https://example.test/?a=1,2&b=x:y"}));
}

TEST(QmConsoleLinks, SchemesAreNormalizedWithoutChangingPathCase)
{
	EXPECT_EQ(LinkUrls("HTTPS://Example.test/Case HTTP://Example.test/Other www.example.test/Path"),
		(std::vector<std::string>{"https://Example.test/Case", "http://Example.test/Other", "https://www.example.test/Path"}));
}

TEST(QmConsoleLinks, EmbeddedPrefixesAndEmptyHostsAreRejected)
{
	EXPECT_TRUE(LinkUrls("fakewww.example.test user@www.example.test abchttps://example.test https:// http:///path www.").empty());
}

TEST(QmConsoleLinks, LongUrlsAreNotTruncatedBeforeOpening)
{
	const std::string Url = "https://example.test/" + std::string(700, 'a');
	EXPECT_EQ(LinkUrls(Url.c_str()), (std::vector<std::string>{Url}));
}

TEST(QmConsoleLinks, RepeatedCollectionClearsPreviousRanges)
{
	std::vector<QmConsoleText::SRange> vLinks;
	QmConsoleText::CollectLinks("https://example.test", vLinks);
	ASSERT_EQ(vLinks.size(), 1u);
	QmConsoleText::CollectLinks("plain text", vLinks);
	EXPECT_TRUE(vLinks.empty());
	QmConsoleText::CollectLinks(nullptr, vLinks);
	EXPECT_TRUE(vLinks.empty());
}

TEST(QmConsoleLinks, WrappedHitBoxesRejectWhitespaceBetweenAndAfterLines)
{
	const std::vector<IGraphics::CQuadItem> vQuads{{30.0f, 20.0f, 70.0f, 10.0f}, {0.0f, 31.0f, 40.0f, 10.0f}};
	EXPECT_TRUE(QmConsoleText::ContainsPoint(vQuads, vec2(31.0f, 25.0f)));
	EXPECT_TRUE(QmConsoleText::ContainsPoint(vQuads, vec2(10.0f, 35.0f)));
	EXPECT_FALSE(QmConsoleText::ContainsPoint(vQuads, vec2(10.0f, 25.0f)));
	EXPECT_FALSE(QmConsoleText::ContainsPoint(vQuads, vec2(41.0f, 35.0f)));
	EXPECT_FALSE(QmConsoleText::ContainsPoint(vQuads, vec2(50.0f, 30.5f)));
	EXPECT_FALSE(QmConsoleText::ContainsPoint(vQuads, vec2(100.0f, 25.0f)));
}

TEST(QmConsoleText, SearchRangesUseMatchedUtf8Bytes)
{
	std::vector<QmConsoleText::SRange> vMatches;
	QmConsoleText::CollectSearchMatches("前缀中文 中文", "中文", vMatches);
	ASSERT_EQ(vMatches.size(), 2u);
	EXPECT_EQ(vMatches[0].m_StartByte, 6);
	EXPECT_EQ(vMatches[0].m_EndByte, 12);
	EXPECT_EQ(vMatches[0].m_StartChar, 2);
	EXPECT_EQ(vMatches[1].m_StartByte, 13);
	EXPECT_EQ(vMatches[1].m_EndByte, 19);
}

TEST(QmConsoleText, CaseInsensitiveSearchUsesActualMatchedLength)
{
	std::vector<QmConsoleText::SRange> vMatches;
	// Kelvin 符号与 k 大小写等价，但 UTF-8 字节长度不同。
	QmConsoleText::CollectSearchMatches("K k", "k", vMatches);
	ASSERT_EQ(vMatches.size(), 2u);
	EXPECT_EQ(vMatches[0].m_StartByte, 0);
	EXPECT_EQ(vMatches[0].m_EndByte, 3);
	EXPECT_EQ(vMatches[1].m_StartByte, 4);
	EXPECT_EQ(vMatches[1].m_EndByte, 5);
}

TEST(QmConsoleText, EmptySearchClearsOldMatches)
{
	std::vector<QmConsoleText::SRange> vMatches;
	QmConsoleText::CollectSearchMatches("match", "match", vMatches);
	ASSERT_EQ(vMatches.size(), 1u);
	QmConsoleText::CollectSearchMatches("match", "", vMatches);
	EXPECT_TRUE(vMatches.empty());
}

TEST(QmConsoleText, StoredCharacterSpansConvertChineseNamesToByteSpans)
{
	const auto Split = QmConsoleText::ColorSplitForCharacters("日志 中文: 内容", 3, 2, ColorRGBA(1, 0, 0, 1));
	EXPECT_EQ(Split.m_CharIndex, 7);
	EXPECT_EQ(Split.m_Length, 6);
}

TEST(QmConsoleText, SearchOverridesOnlyMatchedPartOfLinkAndRestoresLinkColorAfterward)
{
	const char *pText = "中文 https://example.test/end";
	const ColorRGBA Base(1, 1, 1, 1), Link(0, 0, 1, 1), Search(1, 1, 0, 1);
	std::vector<QmConsoleText::SRange> vLinks, vMatches;
	QmConsoleText::CollectLinks(pText, vLinks);
	QmConsoleText::CollectSearchMatches(pText, "example", vMatches);
	ASSERT_EQ(vLinks.size(), 1u);
	ASSERT_EQ(vMatches.size(), 1u);
	const std::vector<STextColorSplit> vLayers{
		{0, str_length(pText), Base},
		{vLinks[0].m_StartByte, vLinks[0].m_EndByte - vLinks[0].m_StartByte, Link},
		{vMatches[0].m_StartByte, vMatches[0].m_EndByte - vMatches[0].m_StartByte, Search}};
	std::vector<STextColorSplit> vResult;
	QmConsoleText::ComposeColorSplits(pText, vLayers, vResult);
	ASSERT_NE(ColorAt(vResult, 0), nullptr);
	EXPECT_EQ(ColorAt(vResult, 0)->m_Color, Base);
	ASSERT_NE(ColorAt(vResult, 7), nullptr);
	EXPECT_EQ(ColorAt(vResult, 7)->m_Color, Link);
	ASSERT_NE(ColorAt(vResult, vMatches[0].m_StartByte), nullptr);
	EXPECT_EQ(ColorAt(vResult, vMatches[0].m_StartByte)->m_Color, Search);
	ASSERT_NE(ColorAt(vResult, vMatches[0].m_EndByte), nullptr);
	EXPECT_EQ(ColorAt(vResult, vMatches[0].m_EndByte)->m_Color, Link);
	for(size_t i = 1; i < vResult.size(); ++i)
		EXPECT_EQ(vResult[i - 1].m_CharIndex + vResult[i - 1].m_Length, vResult[i].m_CharIndex);
}

TEST(QmConsoleText, EndingSearchRestoresUnderlyingColorsWithoutChangingLayers)
{
	const ColorRGBA Base(1, 1, 1, 1), Link(0, 0, 1, 1), Search(1, 1, 0, 1);
	std::vector<STextColorSplit> vLayers{{0, 20, Base}, {3, 10, Link}, {5, 4, Search}};
	std::vector<STextColorSplit> vResult;
	QmConsoleText::ComposeColorSplits("01234567890123456789", vLayers, vResult);
	ASSERT_NE(ColorAt(vResult, 6), nullptr);
	EXPECT_EQ(ColorAt(vResult, 6)->m_Color, Search);
	vLayers.pop_back();
	QmConsoleText::ComposeColorSplits("01234567890123456789", vLayers, vResult);
	ASSERT_NE(ColorAt(vResult, 6), nullptr);
	EXPECT_EQ(ColorAt(vResult, 6)->m_Color, Link);
	ASSERT_NE(ColorAt(vResult, 13), nullptr);
	EXPECT_EQ(ColorAt(vResult, 13)->m_Color, Base);
	EXPECT_EQ(vLayers[1].m_CharIndex, 3);
	EXPECT_EQ(vLayers[1].m_Length, 10);
}

TEST(QmConsoleText, AdjacentSearchMatchesRemainDistinctFromFollowingBaseColor)
{
	const ColorRGBA Base(1, 1, 1, 1), Selected(1, 1, 0, 1), Other(1, 0, 0, 1);
	const std::vector<STextColorSplit> vLayers{{0, 8, Base}, {0, 3, Selected}, {3, 3, Other}};
	std::vector<STextColorSplit> vResult;
	QmConsoleText::ComposeColorSplits("01234567", vLayers, vResult);
	ASSERT_EQ(vResult.size(), 3u);
	EXPECT_EQ(vResult[0].m_Color, Selected);
	EXPECT_EQ(vResult[1].m_Color, Other);
	EXPECT_EQ(vResult[2].m_Color, Base);
}

TEST(QmConsoleText, EmptyTextClearsPreviousColorSplits)
{
	const std::vector<STextColorSplit> vLayers{{0, -1, ColorRGBA(1, 0, 0, 1)}};
	std::vector<STextColorSplit> vResult;
	QmConsoleText::ComposeColorSplits("0123456789", vLayers, vResult);
	ASSERT_EQ(vResult.size(), 1u);
	QmConsoleText::ComposeColorSplits("", vLayers, vResult);
	EXPECT_TRUE(vResult.empty());
}

TEST(QmConsoleText, NonGlyphNewlineLayerDoesNotDelayNextLineLinkColor)
{
	const char *pText = "中\nhttps://example.test";
	const ColorRGBA Base(1, 1, 1, 1), Search(1, 1, 0, 1), Link(0, 0, 1, 1);
	const std::vector<STextColorSplit> vLayers{{0, str_length(pText), Base}, {0, 3, Search}, {4, 20, Link}};
	std::vector<STextColorSplit> vResult;
	QmConsoleText::ComposeColorSplits(pText, vLayers, vResult);
	ASSERT_EQ(vResult.size(), 2u);
	EXPECT_EQ(vResult[0].m_Color, Search);
	EXPECT_EQ(vResult[1].m_CharIndex, 4);
	EXPECT_EQ(vResult[1].m_Color, Link);
}
