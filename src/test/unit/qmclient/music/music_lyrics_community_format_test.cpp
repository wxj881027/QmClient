#include <game/client/components/qmclient/music_lyrics/music_lyrics_community_format.h>

#include <gtest/gtest.h>

#include <limits>

using namespace QmMusicLyrics;

TEST(MusicLyricsCommunityFormat, NeteaseUsesTheNumericSongId)
{
	const auto Song = CommunitySong(1, 42, "unrelated");
	EXPECT_EQ(Song.m_Source, ECommunitySource::NETEASE);
	EXPECT_EQ(Song.m_Id, "42");
	EXPECT_EQ(CommunityCachePath(Song), "qmclient/lyrics/netease/42.yrc");
	EXPECT_EQ(CommunityUrl(Song), "https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/ncm-lyrics/42.yrc");
	EXPECT_EQ(CommunitySong(1, std::numeric_limits<uint64_t>::max(), {}).m_Id, "18446744073709551615");
	EXPECT_EQ(CommunitySong(1, 0, "42").m_Source, ECommunitySource::NONE);
}

TEST(MusicLyricsCommunityFormat, QqUsesOnlyTheFullNativeCatalogIdentifier)
{
	const auto Numeric = CommunitySong(4, 999, "qqmusic:id:42");
	EXPECT_EQ(Numeric.m_Source, ECommunitySource::QQMUSIC);
	EXPECT_EQ(Numeric.m_Id, "42");
	EXPECT_EQ(CommunityCachePath(Numeric), "qmclient/lyrics/qqmusic/42.yrc");
	const auto Mid = CommunitySong(4, 999, "qqmusic:mid:001AbCdEfGhIjK");
	EXPECT_EQ(Mid.m_Source, ECommunitySource::QQMUSIC);
	EXPECT_EQ(Mid.m_Id, "001AbCdEfGhIjK");
	EXPECT_EQ(CommunityUrl(Mid), "https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/qq-lyrics/001AbCdEfGhIjK.yrc");
	EXPECT_EQ(CommunitySong(4, 42, {}).m_Source, ECommunitySource::NONE);
}

TEST(MusicLyricsCommunityFormat, SpotifyKeepsItsCaseSensitiveCatalogId)
{
	const auto Song = CommunitySong(5, 999, "0123456789aBcDeFgHiJkL");
	EXPECT_EQ(Song.m_Source, ECommunitySource::SPOTIFY);
	EXPECT_EQ(Song.m_Id, "0123456789aBcDeFgHiJkL");
	EXPECT_EQ(CommunityCachePath(Song), "qmclient/lyrics/spotify/0123456789aBcDeFgHiJkL.yrc");
	EXPECT_EQ(CommunityUrl(Song), "https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/spotify-lyrics/0123456789aBcDeFgHiJkL.yrc");
}

TEST(MusicLyricsCommunityFormat, RejectsUnknownSourcesAndMalformedCatalogIds)
{
	for(int Source : {0, 2, 3, 6, -1})
		EXPECT_EQ(CommunitySong(Source, 42, "qqmusic:id:42").m_Source, ECommunitySource::NONE);
	for(const char *pId : {"42", "qqmusic:id:0", "qqmusic:id:001", "qqmusic:id:-1", "qqmusic:id:18446744073709551616", "qqmusic:mid:short", "qqmusic:mid:001AbCdEfGhIj/", "qqmusic:id:42?x", "qqmusic:id:42/../43"})
	{
		SCOPED_TRACE(pId);
		EXPECT_EQ(CommunitySong(4, 42, pId).m_Source, ECommunitySource::NONE);
	}
	for(const char *pId : {"", "0123456789aBcDeFgHiJk", "0123456789aBcDeFgHiJkLM", "0123456789aBcDeFgHiJk/", "spotify:track:0123456789aBcDeFgHiJkL"})
	{
		SCOPED_TRACE(pId);
		EXPECT_EQ(CommunitySong(5, 42, pId).m_Source, ECommunitySource::NONE);
	}
}

TEST(MusicLyricsCommunityFormat, PathBuildersValidateManuallyConstructedSongs)
{
	for(const auto Source : {ECommunitySource::NONE, ECommunitySource::NETEASE, ECommunitySource::QQMUSIC, ECommunitySource::SPOTIFY})
	{
		for(const char *pId : {"../outside", "/absolute", "..\\outside", "C:outside", "42?raw=true", "42#anchor", "42%2f..", "", "a\nb"})
		{
			SCOPED_TRACE(pId);
			EXPECT_TRUE(CommunityCachePath({Source, pId}).empty());
			EXPECT_TRUE(CommunityUrl({Source, pId}).empty());
		}
	}
	EXPECT_TRUE(CommunityUrl({static_cast<ECommunitySource>(99), "42"}).empty());
}

TEST(MusicLyricsCommunityFormat, ParsesChineseAndPreservesParenthesesAndPunctuation)
{
	SLyricsData Data;
	ASSERT_TRUE(ParseCommunityLyrics("[1000,1500](1000,500,0)云(1500,1000,0)朵(慢慢走)。", &Data));
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 1u);
	const auto &Line = Data.m_Timeline.m_vLines.front();
	EXPECT_EQ(Line.m_Text, "云朵(慢慢走)。");
	ASSERT_EQ(Line.m_vWords.size(), 2u);
	EXPECT_EQ(Line.m_vWords[1].m_StartMs, 1500);
	EXPECT_EQ(Line.m_vWords[1].m_EndMs, 2500);
	EXPECT_TRUE(HasWordTiming(Data));
}

TEST(MusicLyricsCommunityFormat, UntimedSpacesUseThePreviousWordEnd)
{
	SLyricsData Data;
	ASSERT_TRUE(ParseCommunityLyrics("[1000,2000](1000,500,0)cloud(0,0,0) (2000,1000,0)walk", &Data));
	const auto &Line = Data.m_Timeline.m_vLines.front();
	EXPECT_EQ(Line.m_Text, "cloud walk");
	ASSERT_EQ(Line.m_vWords.size(), 3u);
	EXPECT_EQ(Line.m_vWords[1].m_Text, " ");
	EXPECT_EQ(Line.m_vWords[1].m_StartMs, 1500);
	EXPECT_EQ(Line.m_vWords[1].m_EndMs, 1500);
	EXPECT_EQ(Line.m_vWords[2].m_StartMs, 2000);
}

TEST(MusicLyricsCommunityFormat, LeadingUntimedSpacesUseTheNextWordStart)
{
	SLyricsData Data;
	ASSERT_TRUE(ParseCommunityLyrics("[1000,1000](0,0,0) (0,0,0)  (1200,800,0)云", &Data));
	const auto &Line = Data.m_Timeline.m_vLines.front();
	ASSERT_EQ(Line.m_vWords.size(), 3u);
	EXPECT_EQ(Line.m_Text, "   云");
	EXPECT_EQ(Line.m_vWords[0].m_StartMs, 1200);
	EXPECT_EQ(Line.m_vWords[0].m_EndMs, 1200);
	EXPECT_EQ(Line.m_vWords[1].m_StartMs, 1200);
	EXPECT_TRUE(HasWordTiming(Data));
}

TEST(MusicLyricsCommunityFormat, LastWordExtendsTheLineEnd)
{
	SLyricsData Data;
	ASSERT_TRUE(ParseCommunityLyrics("[1000,100](1000,400,0)走(1400,900,0)远", &Data));
	EXPECT_EQ(Data.m_Timeline.m_vLines.front().m_EndMs, 2300);
}

TEST(MusicLyricsCommunityFormat, PreservesZeroDurationPunctuationAtItsActualTime)
{
	SLyricsData Data;
	ASSERT_TRUE(ParseCommunityLyrics("[1000,1000](1000,400,0)走(1400,0,0)，(1400,600,0)远", &Data));
	const auto &Line = Data.m_Timeline.m_vLines.front();
	ASSERT_EQ(Line.m_vWords.size(), 3u);
	EXPECT_EQ(Line.m_Text, "走，远");
	EXPECT_EQ(Line.m_vWords[1].m_StartMs, 1400);
	EXPECT_EQ(Line.m_vWords[1].m_EndMs, 1400);
}

TEST(MusicLyricsCommunityFormat, SortsCompleteLinesAndAcceptsBomAndBlankLines)
{
	SLyricsData Data;
	ASSERT_TRUE(ParseCommunityLyrics("\xEF\xBB\xBF\r\n[3000,500](3000,500,0)远\r\n\n[1000,500](1000,500,0)走\n", &Data));
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 2u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, "走");
	EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, "远");
}

TEST(MusicLyricsCommunityFormat, RejectsMissingOrIncompleteWordTiming)
{
	for(const char *pText : {"[1000,1000]只有整句", "[1000,1000](1000,0,0)瞬间", "[1000,1000]前缀(1000,1000,0)正文", "[1000,1000](0,0,0) ", "[1000,1000](0,0,0)字(1000,1000,0)词", "[1000,1000](1000,9223372036854775807,0)溢出"})
	{
		SCOPED_TRACE(pText);
		SLyricsData Data;
		EXPECT_FALSE(ParseCommunityLyrics(pText, &Data));
		EXPECT_FALSE(Data.HasLyrics());
	}
}

TEST(MusicLyricsCommunityFormat, RejectsBackwardsWordsAndWordsBeforeTheirLine)
{
	for(const char *pText : {"[1000,2000](2000,500,0)前(1000,500,0)后", "[1000,1000](500,500,0)早(1000,500,0)到"})
	{
		SCOPED_TRACE(pText);
		SLyricsData Data;
		EXPECT_FALSE(ParseCommunityLyrics(pText, &Data));
	}
}

TEST(MusicLyricsCommunityFormat, ACorruptLineRejectsTheWholeResponseAndClearsOldData)
{
	SLyricsData Data;
	ASSERT_TRUE(ParseCommunityLyrics("[1000,500](1000,500,0)旧", &Data));
	Data.m_Song.m_Title = "old song";
	Data.m_vTranslations.push_back("old translation");
	EXPECT_FALSE(ParseCommunityLyrics("[1000,500](1000,500,0)新\n[2000,500](2000,bad,0)坏", &Data));
	EXPECT_FALSE(Data.HasLyrics());
	EXPECT_TRUE(Data.m_Song.m_Title.empty());
	EXPECT_TRUE(Data.m_vTranslations.empty());
}

TEST(MusicLyricsCommunityFormat, RejectsEmptyErrorPagesAndInvalidEncoding)
{
	for(const std::string &Text : {std::string(), std::string(" \r\n "), std::string("404: Not Found"), std::string("<html>error</html>"), std::string("[1000,500](1000,500,0)\xFF"), (std::string("[1000,500](1000,500,0)a") + '\0' + "b")})
	{
		SLyricsData Data;
		EXPECT_FALSE(ParseCommunityLyrics(Text, &Data));
	}
	EXPECT_FALSE(ParseCommunityLyrics("[1000,500](1000,500,0)字", nullptr));
}

TEST(MusicLyricsCommunityFormat, RejectsResponsesBeyondTheCacheLimit)
{
	SLyricsData Data;
	std::string Text = "[1000,500](1000,500,0)";
	Text.resize(COMMUNITY_LYRICS_MAX_BYTES, 'a');
	ASSERT_TRUE(ParseCommunityLyrics(Text, &Data));
	Text.push_back('a');
	EXPECT_FALSE(ParseCommunityLyrics(Text, &Data));
	EXPECT_FALSE(Data.HasLyrics());
}

TEST(MusicLyricsCommunityFormat, WordTimingDetectionUsesCompleteValidWords)
{
	SLyricsData Data;
	EXPECT_FALSE(HasWordTiming(Data));
	ASSERT_TRUE(NeteaseLyrics::ParseLrc("[00:01.00]plain", &Data.m_Timeline));
	EXPECT_FALSE(HasWordTiming(Data));
	ASSERT_TRUE(ParseCommunityLyrics("[1000,1000](1000,400,0)走(1400,600,0)远", &Data));
	EXPECT_TRUE(HasWordTiming(Data));
	Data.m_Timeline.m_vLines.front().m_vWords.pop_back();
	EXPECT_FALSE(HasWordTiming(Data));
}
