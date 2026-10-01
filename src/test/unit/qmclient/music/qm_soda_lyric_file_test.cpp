#include <game/client/components/qmclient/music_lyrics/qm_soda_lyric_file.h>

#include <gtest/gtest.h>

#include <string>

using namespace QmSodaLyricFile;

TEST(QmSodaLyricFile, ParsesKrcLyricJson)
{
	const char *Json = R"({"mediaId":"123","title":"晴天","artist":"周杰伦","album":"叶惠美","coverUrl":"http://x/c.jpg","durationMs":250000,"positionMs":5000,"isPlaying":true,"lyricType":"krc","lyricContent":"[ti:晴天]\n[0,4390]<0,274,0>雨<274,274,0>停<548,274,0>了\n[4390,2000]<4390,500,0>天\n","translationLrc":""})";
	QmMusicLyrics::SLyricsData Data;
	std::string Error;
	ASSERT_TRUE(ParseLyricFileJson(Json, &Data, &Error)) << Error;
	EXPECT_EQ(Data.m_Song.m_Title, "晴天");
	EXPECT_EQ(Data.m_Song.m_Artist, "周杰伦");
	EXPECT_EQ(Data.m_Song.m_DurationMs, 250000);
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 2u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, "雨停了");
	ASSERT_EQ(Data.m_Timeline.m_vLines[0].m_vWords.size(), 3u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_vWords[1].m_StartMs, 274);
	EXPECT_TRUE(Data.m_vTranslations.empty());
}

TEST(QmSodaLyricFile, ParsesLrcLyricJson)
{
	const char *Json = R"({"title":"T","lyricType":"lrc","lyricContent":"[00:01.00]第一句\n[00:03.00]第二句\n","translationLrc":""})";
	QmMusicLyrics::SLyricsData Data;
	std::string Error;
	ASSERT_TRUE(ParseLyricFileJson(Json, &Data, &Error)) << Error;
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 2u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_StartMs, 1000);
	EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, "第二句");
}

TEST(QmSodaLyricFile, AlignsTranslationLrcByTimestamp)
{
	const char *Json = R"({"title":"T","lyricType":"krc","lyricContent":"[0,1000]<0,500,0>雨\n[2000,1000]<0,500,0>天\n","translationLrc":"[00:02.00]天空\n[00:00.00]雨停\n"})";
	QmMusicLyrics::SLyricsData Data;
	std::string Error;
	ASSERT_TRUE(ParseLyricFileJson(Json, &Data, &Error)) << Error;
	ASSERT_EQ(Data.m_vTranslations.size(), 2u);
	EXPECT_EQ(Data.m_vTranslations[0], "雨停");
	EXPECT_EQ(Data.m_vTranslations[1], "天空");
	EXPECT_TRUE(Data.HasTranslation());
}

TEST(QmSodaLyricFile, ParsesQrcRlrcLyricJson)
{
	// 已是抽出后的 rlrc 文本：不应落到 LRC 分支而解析失败。
	const char *Json = R"({"title":"Q","lyricType":"qrc","lyricContent":"[1000,2000]第一句\n[3000,2000]第二句\n","translationLrc":""})";
	QmMusicLyrics::SLyricsData Data;
	std::string Error;
	ASSERT_TRUE(ParseLyricFileJson(Json, &Data, &Error)) << Error;
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 2u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_StartMs, 1000);
	EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, "第二句");
}

TEST(QmSodaLyricFile, ParsesQrcXmlLyricJson)
{
	// 完整 QrcInfos XML：先抽出 LyricContent 属性，再按 rlrc 解析。
	const char *Json = R"({"title":"Q","lyricType":"qrc","lyricContent":"<QrcInfos><LyricInfo LyricContent=\"[1000,2000]天空\n\"/></QrcInfos>","translationLrc":""})";
	QmMusicLyrics::SLyricsData Data;
	std::string Error;
	ASSERT_TRUE(ParseLyricFileJson(Json, &Data, &Error)) << Error;
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 1u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_StartMs, 1000);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, "天空");
}

TEST(QmSodaLyricFile, RejectsQrcXmlWithoutLyricContentEvenWithTimedLine)
{
	const char *Json = R"({"title":"Q","lyricType":"qrc","lyricContent":"<QrcInfos><LyricInfo/>\n[1000,2000]天空\n</QrcInfos>","translationLrc":""})";
	QmMusicLyrics::SLyricsData Data;
	std::string Error;
	EXPECT_FALSE(ParseLyricFileJson(Json, &Data, &Error));
	EXPECT_EQ(Error, "LyricContent not found");
	EXPECT_TRUE(Data.m_Timeline.m_vLines.empty());
}

TEST(QmSodaLyricFile, RejectsMalformedJson)
{
	QmMusicLyrics::SLyricsData Data;
	std::string Error;
	EXPECT_FALSE(ParseLyricFileJson("not json", &Data, &Error));
	EXPECT_FALSE(ParseLyricFileJson("{}", &Data, &Error));
	EXPECT_FALSE(ParseLyricFileJson(R"({"title":"T","lyricType":"krc","lyricContent":"plain"})", &Data, &Error));
}
