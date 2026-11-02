#include <game/client/components/qmclient/music_lyrics/music_lyrics_model.h>
#include <game/client/components/qmclient/netease_hook/qm_netease_lyrics_channel.h>

#include <gtest/gtest.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <string>

namespace
{
	std::wstring NextEndpoint()
	{
		static std::atomic_uint s_Next{0};
		return L"Local\\QmClient.Tests.Lyrics." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++s_Next);
	}

	class CNeteaseLyricsChannelTest : public ::testing::Test
	{
	protected:
		std::wstring m_Endpoint = NextEndpoint();
		QmNeteaseHook::CLyricsChannel m_Writer{m_Endpoint};
		QmNeteaseHook::CLyricsChannel m_Reader{m_Endpoint};
		uint64_t m_Revision = 0;
	};
}

TEST_F(CNeteaseLyricsChannelTest, PublishedOriginalAndTranslationCanBeParsedTogether)
{
	NeteaseLyrics::SRawLyrics Raw;
	Raw.m_Lrc = "[00:01.00]one\n[00:03.00]two";
	Raw.m_TranslationLrc = "[00:01.00]一\n[00:03.00]二";
	ASSERT_TRUE(m_Writer.Publish(100, 42, 7, Raw));
	NeteaseLyrics::SRawLyrics Received;
	ASSERT_TRUE(m_Reader.Read(100, 42, 7, m_Revision, Received));
	QmMusicLyrics::SLyricsData Data;
	ASSERT_TRUE(NeteaseLyrics::ParseRawLyrics(Received, &Data.m_Timeline));
	QmMusicLyrics::AlignTranslations(Data.m_Timeline, Received.m_TranslationLrc, &Data.m_vTranslations);
	ASSERT_EQ(Data.m_vTranslations.size(), 2u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, "two");
	EXPECT_EQ(Data.m_vTranslations[1], "二");
	EXPECT_FALSE(m_Reader.Read(100, 42, 7, m_Revision, Received));
}

TEST_F(CNeteaseLyricsChannelTest, SongProcessAndGenerationMustMatchTheCurrentBridge)
{
	NeteaseLyrics::SRawLyrics Raw;
	Raw.m_Lrc = "[00:01]one";
	ASSERT_TRUE(m_Writer.Publish(100, 42, 7, Raw));
	NeteaseLyrics::SRawLyrics Received;
	EXPECT_FALSE(m_Reader.Read(101, 42, 7, m_Revision, Received));
	EXPECT_FALSE(m_Reader.Read(100, 43, 7, m_Revision, Received));
	EXPECT_FALSE(m_Reader.Read(100, 42, 8, m_Revision, Received));
	EXPECT_TRUE(m_Reader.Read(100, 42, 7, m_Revision, Received));
}

TEST_F(CNeteaseLyricsChannelTest, OversizedPublicationKeepsThePreviousCompletePayload)
{
	NeteaseLyrics::SRawLyrics Raw;
	Raw.m_Lrc = "[00:01]previous";
	ASSERT_TRUE(m_Writer.Publish(100, 42, 7, Raw));
	NeteaseLyrics::SRawLyrics Oversized;
	Oversized.m_Yrc.assign(512 * 1024 + 1, 'x');
	EXPECT_FALSE(m_Writer.Publish(100, 42, 7, Oversized));
	NeteaseLyrics::SRawLyrics Received;
	ASSERT_TRUE(m_Reader.Read(100, 42, 7, m_Revision, Received));
	EXPECT_EQ(Received.m_Lrc, Raw.m_Lrc);
	EXPECT_TRUE(Received.m_Yrc.empty());
}

TEST_F(CNeteaseLyricsChannelTest, ReopenedWriterPublishesANewRevisionForAnExistingReader)
{
	NeteaseLyrics::SRawLyrics Raw;
	Raw.m_Lrc = "[00:01]old";
	ASSERT_TRUE(m_Writer.Publish(100, 42, 7, Raw));
	NeteaseLyrics::SRawLyrics Received;
	ASSERT_TRUE(m_Reader.Read(100, 42, 7, m_Revision, Received));
	m_Writer.Close();
	Raw.m_Lrc = "[00:01]new";
	ASSERT_TRUE(m_Writer.Publish(100, 42, 8, Raw));
	ASSERT_TRUE(m_Reader.Read(100, 42, 8, m_Revision, Received));
	EXPECT_EQ(Received.m_Lrc, Raw.m_Lrc);
}

TEST_F(CNeteaseLyricsChannelTest, ClearingTheLyricsPublishesAnEmptyRevision)
{
	NeteaseLyrics::SRawLyrics Raw;
	Raw.m_Lrc = "[00:01]old";
	ASSERT_TRUE(m_Writer.Publish(100, 42, 7, Raw));
	NeteaseLyrics::SRawLyrics Received;
	ASSERT_TRUE(m_Reader.Read(100, 42, 7, m_Revision, Received));
	ASSERT_TRUE(m_Writer.Publish(100, 42, 7, {}));
	ASSERT_TRUE(m_Reader.Read(100, 42, 7, m_Revision, Received));
	EXPECT_TRUE(Received.m_Lrc.empty());
	EXPECT_TRUE(Received.m_Yrc.empty());
	EXPECT_TRUE(Received.m_TranslationLrc.empty());
}

TEST_F(CNeteaseLyricsChannelTest, MissingWriterReturnsWithoutCreatingAMapping)
{
	NeteaseLyrics::SRawLyrics Received;
	EXPECT_FALSE(m_Reader.Read(100, 42, 7, m_Revision, Received));
}

#endif
