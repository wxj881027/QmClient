#include <game/client/components/qmclient/music_lyrics/qm_soda_lyric_file.h>

#include <gtest/gtest.h>
#include <qm/music/qm_music_publication.h>

namespace
{
	QmMusicHook::SPlayback Song(const char *pId)
	{
		QmMusicHook::SPlayback Result;
		Result.m_ProcessId = 123;
		Result.m_HasSong = true;
		Result.m_MediaId = pId;
		Result.m_Title = "歌曲";
		Result.m_LyricType = "qrc";
		return Result;
	}
}

TEST(QmMusicTransport, FullIdentityDistinguishesSameDigitsAndLongPrefix)
{
	QmMusicHook::CPublication State;
	auto Playback = Song("a123");
	ASSERT_TRUE(State.Update(Playback));
	const auto First = State.Generation();
	Playback.m_MediaId = "b123";
	EXPECT_TRUE(State.Update(Playback));
	EXPECT_GT(State.Generation(), First);
	Playback.m_MediaId.assign(100, 'a');
	ASSERT_TRUE(State.Update(Playback));
	const auto BeforeSuffix = State.Generation();
	Playback.m_MediaId += "b";
	EXPECT_TRUE(State.Update(Playback));
	EXPECT_GT(State.Generation(), BeforeSuffix);
	EXPECT_NE(State.LyricJson().find(Playback.m_MediaId), std::string::npos);
}

TEST(QmMusicTransport, LateLyricsAndReplacementAdvanceGeneration)
{
	QmMusicHook::CPublication State;
	auto Playback = Song("qq123");
	ASSERT_TRUE(State.Update(Playback));
	const auto WithoutLyrics = State.Generation();
	Playback.m_LyricContent = "[0,1000]你(0,500)好(500,500)";
	EXPECT_TRUE(State.Update(Playback));
	EXPECT_GT(State.Generation(), WithoutLyrics);
	EXPECT_FALSE(State.Update(Playback));
	const auto WithLyrics = State.Generation();
	Playback.m_TranslationLrc = "[00:00.00]Hello";
	EXPECT_TRUE(State.Update(Playback));
	EXPECT_GT(State.Generation(), WithLyrics);
}

TEST(QmMusicTransport, PauseResumeAndSeekPublishWithoutReloadingLyrics)
{
	QmMusicHook::CPublication State;
	auto Playback = Song("qq123");
	Playback.m_PositionValid = true;
	Playback.m_PositionMs = 2000;
	Playback.m_Playing = true;
	State.Update(Playback);
	const auto Generation = State.Generation();
	Playback.m_Playing = false;
	Playback.m_PositionMs = 1000;
	EXPECT_FALSE(State.Update(Playback));
	const auto Paused = State.Snapshot(100, "");
	EXPECT_EQ(Paused.m_PositionMs, 1000);
	EXPECT_EQ(Paused.m_Flags & QmSodaHook::FLAG_PLAYING, 0u);
	Playback.m_Playing = true;
	Playback.m_PositionMs = 5000;
	EXPECT_FALSE(State.Update(Playback));
	EXPECT_EQ(State.Generation(), Generation);
	EXPECT_NE(State.Snapshot(101, "").m_Flags & QmSodaHook::FLAG_PLAYING, 0u);
}

TEST(QmMusicTransport, ExitAndRestartInvalidatePreviousSong)
{
	QmMusicHook::CPublication State;
	auto Playback = Song("qq123");
	Playback.m_LyricContent = "[0,1000]你(0,1000)";
	State.Update(Playback);
	const auto First = State.Generation();
	Playback.m_ProcessId = 124;
	EXPECT_TRUE(State.Update(Playback));
	EXPECT_GT(State.Generation(), First);
	Playback = {};
	EXPECT_TRUE(State.Update(Playback));
	const auto Empty = State.Snapshot(100, "old.json");
	EXPECT_EQ(Empty.m_Flags & (QmSodaHook::FLAG_HAS_SONG | QmSodaHook::FLAG_HAS_LYRIC_FILE), 0u);
	EXPECT_EQ(Empty.m_aLyricFilePath[0], '\0');
}

TEST(QmMusicTransport, IndependentSourcesDoNotSharePublicationState)
{
	QmMusicHook::CPublication Kugou;
	QmMusicHook::CPublication QQMusic;
	Kugou.Update(Song("kugou123"));
	QQMusic.Update(Song("qq123"));
	Kugou.Update({});
	EXPECT_EQ(Kugou.Snapshot(100, "").m_Flags & QmSodaHook::FLAG_HAS_SONG, 0u);
	EXPECT_STREQ(QQMusic.Snapshot(100, "").m_aMediaId, "qq123");
}

TEST(QmMusicTransport, TrackWithoutLyricsClearsPreviousFileReference)
{
	QmMusicHook::CPublication State;
	auto Playback = Song("qq123");
	Playback.m_LyricContent = "[0,1000]你(0,1000)";
	State.Update(Playback);
	ASSERT_NE(State.Snapshot(100, "previous.json").m_Flags & QmSodaHook::FLAG_HAS_LYRIC_FILE, 0u);
	State.Update(Song("qq456"));
	const auto Next = State.Snapshot(101, "previous.json");
	EXPECT_STREQ(Next.m_aMediaId, "qq456");
	EXPECT_EQ(Next.m_Flags & QmSodaHook::FLAG_HAS_LYRIC_FILE, 0u);
	EXPECT_EQ(Next.m_aLyricFilePath[0], '\0');
}

TEST(QmMusicTransport, ParsesPlainQrcAndEscapedMetadata)
{
	QmMusicHook::CPublication State;
	auto Playback = Song("qq123");
	Playback.m_Title = "引号\"与\\换行\n";
	Playback.m_LyricContent = "[0,1000]你(0,500)好(500,500)";
	State.Update(Playback);
	QmMusicLyrics::SLyricsData Lyrics;
	std::string Error;
	ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(State.LyricJson(), &Lyrics, &Error)) << Error;
	EXPECT_EQ(Lyrics.m_Song.m_Title, Playback.m_Title);
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 1u);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "你好");
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines[0].m_vWords.size(), 2u);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_vWords[1].m_StartMs, 500);
}

TEST(QmMusicTransport, ParsesQrcXmlAndRejectsMalformedQrc)
{
	QmMusicHook::CPublication State;
	auto Playback = Song("qq123");
	Playback.m_LyricContent = "<QrcInfos><Lyric_1 LyricContent=\"[0,1000]你(0,1000)\"/></QrcInfos>";
	State.Update(Playback);
	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(State.LyricJson(), &Lyrics));
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 1u);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "你");
	Playback.m_LyricContent = "<QrcInfos/>";
	State.Update(Playback);
	EXPECT_FALSE(QmSodaLyricFile::ParseLyricFileJson(State.LyricJson(), &Lyrics));
}
