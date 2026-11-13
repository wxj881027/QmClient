#include <game/client/components/qmclient/music_lyrics/music_lyrics_community_format.h>
#include <game/client/components/qmclient/music_lyrics/qm_soda_lyric_file.h>

#include <gtest/gtest.h>
#include <qm/music/qm_music_publication.h>
#include <qm/music/qm_qqmusic_protocol.h>

#include <string>

TEST(QmQQMusicQrcPipeline, PreservesTrailingWordMarkersThroughClientPublication){
	const char *pResponse = R"({"req_0":{"code":0,"data":{"songID":7,"lyric":"[ti:测试歌曲]\n[60000,2000]停下(60000,750)脚步(60750,500)吧(61250,750)",
	"trans" : "[01:00.00]Stop now"
}
}
})";
	QmMusicHook::QQMusic::SLyrics Decoded;
ASSERT_TRUE(QmMusicHook::QQMusic::ParseLyricsResponse(pResponse, 7, false, Decoded));
EXPECT_EQ(Decoded.m_Type, "qrc");
EXPECT_EQ(Decoded.m_Content, "[ti:测试歌曲]\n[60000,2000]停下(60000,750)脚步(60750,500)吧(61250,750)");

QmMusicHook::SPlayback Playback;
Playback.m_ProcessId = 123;
Playback.m_HasSong = true;
Playback.m_MediaId = "qqmusic:7";
Playback.m_Title = "测试歌曲";
Playback.m_Artist = "测试歌手";
Playback.m_LyricType = Decoded.m_Type;
Playback.m_LyricContent = Decoded.m_Content;
Playback.m_TranslationLrc = Decoded.m_Translation;
QmMusicHook::CPublication Publication;
ASSERT_TRUE(Publication.Update(Playback));

QmMusicLyrics::SLyricsData Lyrics;
std::string Error;
ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(Publication.LyricJson(), &Lyrics, &Error)) << Error;
EXPECT_EQ(Lyrics.m_Song.m_Title, Playback.m_Title);
ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 1u);
const auto &Line = Lyrics.m_Timeline.m_vLines.front();
EXPECT_EQ(Line.m_StartMs, 60000);
EXPECT_EQ(Line.m_EndMs, 62000);
EXPECT_EQ(Line.m_Text, "停下脚步吧");
ASSERT_EQ(Line.m_vWords.size(), 3u);
EXPECT_EQ(Line.m_vWords[0].m_Text, "停下");
EXPECT_EQ(Line.m_vWords[0].m_StartMs, 60000);
EXPECT_EQ(Line.m_vWords[0].m_EndMs, 60750);
EXPECT_EQ(Line.m_vWords[1].m_Text, "脚步");
EXPECT_EQ(Line.m_vWords[1].m_StartMs, 60750);
EXPECT_EQ(Line.m_vWords[1].m_EndMs, 61250);
EXPECT_EQ(Line.m_vWords[2].m_Text, "吧");
EXPECT_EQ(Line.m_vWords[2].m_StartMs, 61250);
EXPECT_EQ(Line.m_vWords[2].m_EndMs, 62000);
EXPECT_TRUE(QmMusicLyrics::HasWordTiming(Lyrics));
ASSERT_EQ(Lyrics.m_vTranslations.size(), 1u);
EXPECT_EQ(Lyrics.m_vTranslations[0], "Stop now");
}
