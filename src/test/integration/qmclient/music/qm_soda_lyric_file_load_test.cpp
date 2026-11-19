#include <base/system.h>

#include <engine/storage.h>

#include <game/client/components/qmclient/music_lyrics/music_lyrics_display.h>
#include <game/client/components/qmclient/music_lyrics/qm_soda_lyric_file.h>

#include <gtest/gtest.h>
#include <qm/music/qm_music_publication.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/test.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace
{
	using CFile = std::unique_ptr<void, decltype(&io_close)>;

	struct SInput
	{
		const std::string m_Title = std::string(96, 't');
		const std::string m_Artist = std::string(80, 'a');
		const std::string m_Album = std::string(64, 'b');
		const std::string m_Cover = "https://covers.example/" + std::string(64, 'c');
		const std::string m_First = std::string(96, 'w');
		const std::string m_Translation = std::string(64, 'x');
		std::string m_Json;

		SInput()
		{
			QmMusicHook::SPlayback Playback;
			Playback.m_HasSong = true;
			Playback.m_MediaId = "qqmusic:123";
			Playback.m_Title = m_Title;
			Playback.m_Artist = m_Artist;
			Playback.m_Album = m_Album;
			Playback.m_CoverUrl = m_Cover;
			Playback.m_DurationMs = 3000;
			Playback.m_LyricType = "lrc";
			Playback.m_LyricContent = "[00:01.00]" + m_First + "\n[00:02.00]tail";
			Playback.m_TranslationLrc = "[00:01.00]" + m_Translation + "\n[00:02.00]tail-t";
			QmMusicHook::CPublication Publication;
			Publication.Update(Playback);
			m_Json = Publication.LyricJson();
		}
	};

	class QmSodaLyricFileLoad : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		char m_aPath[IO_MAX_PATH_LENGTH]{};
		SInput m_Input;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "歌词.json", m_aPath, sizeof(m_aPath));
		}

		void Write(std::string_view Json)
		{
			CFile File(io_open(m_aPath, IOFLAG_WRITE), io_close);
			ASSERT_NE(File, nullptr);
			ASSERT_EQ(io_write(File.get(), Json.data(), static_cast<unsigned>(Json.size())), Json.size());
			ASSERT_EQ(io_close(File.release()), 0);
		}

		void PrepareOld(QmMusicLyrics::SLyricsData &Data)
		{
			ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(
				R"({"title":"old","artist":"old-a","album":"old-b","coverUrl":"old-c","durationMs":1000,"lyricType":"krc","lyricContent":"[100,900]<0,900,0>old-word","translationLrc":"[00:00.10]old-t"})",
				&Data));
		}

		void ExpectOld(const QmMusicLyrics::SLyricsData &Data)
		{
			EXPECT_EQ(Data.m_Song.m_Title, "old");
			EXPECT_EQ(Data.m_Song.m_Artist, "old-a");
			EXPECT_EQ(Data.m_Song.m_Album, "old-b");
			EXPECT_EQ(Data.m_Song.m_CoverPath, "old-c");
			EXPECT_EQ(Data.m_Song.m_DurationMs, 1000);
			EXPECT_TRUE(Data.HasLyrics());
			EXPECT_TRUE(Data.HasTranslation());
			ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 1u);
			const auto &Line = Data.m_Timeline.m_vLines[0];
			EXPECT_EQ(Line.m_Text, "old-word");
			EXPECT_EQ(Line.m_StartMs, 100);
			EXPECT_EQ(Line.m_EndMs, 1000);
			ASSERT_EQ(Line.m_vWords.size(), 1u);
			EXPECT_EQ(Line.m_vWords[0].m_Text, "old-word");
			EXPECT_EQ(Line.m_vWords[0].m_StartMs, 100);
			EXPECT_EQ(Line.m_vWords[0].m_EndMs, 1000);
			ASSERT_EQ(Data.m_vTranslations.size(), 1u);
			EXPECT_EQ(Data.m_vTranslations[0], "old-t");
		}

		void ExpectNew(const QmMusicLyrics::SLyricsData &Data)
		{
			EXPECT_EQ(Data.m_Song.m_Title, m_Input.m_Title);
			EXPECT_EQ(Data.m_Song.m_Artist, m_Input.m_Artist);
			EXPECT_EQ(Data.m_Song.m_Album, m_Input.m_Album);
			EXPECT_EQ(Data.m_Song.m_CoverPath, m_Input.m_Cover);
			EXPECT_EQ(Data.m_Song.m_DurationMs, 3000);
			EXPECT_TRUE(Data.HasLyrics());
			EXPECT_FALSE(QmMusicLyrics::HasWordTiming(Data));
			ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 2u);
			EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, m_Input.m_First);
			EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_StartMs, 1000);
			EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_EndMs, 2000);
			EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, "tail");
			EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_StartMs, 2000);
			ASSERT_EQ(Data.m_vTranslations.size(), 2u);
			EXPECT_EQ(Data.m_vTranslations[0], m_Input.m_Translation);
			EXPECT_EQ(Data.m_vTranslations[1], "tail-t");
		}

		void Retry(QmMusicLyrics::SLyricsData &Data)
		{
			ASSERT_NO_FATAL_FAILURE(Write(m_Input.m_Json));
			ASSERT_TRUE(QmSodaLyricFile::LoadLyricFile(m_aPath, &Data));
			ExpectNew(Data);
		}
	};
}

TEST_F(QmSodaLyricFileLoad, LoadsPublishedMetadataAndBothTracksFromUtf8Filename)
{
	ASSERT_NO_FATAL_FAILURE(Write(m_Input.m_Json));
	QmMusicLyrics::SLyricsData Data;
	ASSERT_NO_FATAL_FAILURE(PrepareOld(Data));
	ASSERT_TRUE(QmSodaLyricFile::LoadLyricFile(m_aPath, &Data));
	ExpectNew(Data);
}

TEST_F(QmSodaLyricFileLoad, KrcFilePublishesWordTimingAndSeparateTranslationTogether)
{
	ASSERT_NO_FATAL_FAILURE(Write(R"({"title":"K","lyricType":"krc","lyricContent":"[0,1000]<0,500,0>word","translationLrc":"[00:00.00]translated"})"));
	QmMusicLyrics::SLyricsData Data;
	ASSERT_NO_FATAL_FAILURE(PrepareOld(Data));
	ASSERT_TRUE(QmSodaLyricFile::LoadLyricFile(m_aPath, &Data));
	EXPECT_EQ(Data.m_Song.m_Title, "K");
	EXPECT_TRUE(QmMusicLyrics::HasWordTiming(Data));
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 1u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, "word");
	ASSERT_EQ(Data.m_vTranslations.size(), 1u);
	EXPECT_EQ(Data.m_vTranslations[0], "translated");
}

TEST_F(QmSodaLyricFileLoad, MissingFilePreservesCompleteOldLyricsAndCanRetry)
{
	QmMusicLyrics::SLyricsData Data;
	ASSERT_NO_FATAL_FAILURE(PrepareOld(Data));
	EXPECT_FALSE(QmSodaLyricFile::LoadLyricFile(m_aPath, &Data));
	ExpectOld(Data);
	Retry(Data);
}

TEST_F(QmSodaLyricFileLoad, EmptyFilePreservesCompleteOldLyricsAndCanRetry)
{
	ASSERT_NO_FATAL_FAILURE(Write(""));
	QmMusicLyrics::SLyricsData Data;
	ASSERT_NO_FATAL_FAILURE(PrepareOld(Data));
	EXPECT_FALSE(QmSodaLyricFile::LoadLyricFile(m_aPath, &Data));
	ExpectOld(Data);
	Retry(Data);
}

TEST_F(QmSodaLyricFileLoad, MalformedJsonPreservesCompleteOldLyricsAndCanRetry)
{
	ASSERT_NO_FATAL_FAILURE(Write(R"({"title":"incomplete)"));
	QmMusicLyrics::SLyricsData Data;
	ASSERT_NO_FATAL_FAILURE(PrepareOld(Data));
	EXPECT_FALSE(QmSodaLyricFile::LoadLyricFile(m_aPath, &Data));
	ExpectOld(Data);
	Retry(Data);
}

TEST_F(QmSodaLyricFileLoad, UnusableLyricsDoNotPublishNewMetadataAndCanRetry)
{
	ASSERT_NO_FATAL_FAILURE(Write(R"({"title":"new metadata","lyricType":"lrc","lyricContent":"not timed"})"));
	QmMusicLyrics::SLyricsData Data;
	ASSERT_NO_FATAL_FAILURE(PrepareOld(Data));
	EXPECT_FALSE(QmSodaLyricFile::LoadLyricFile(m_aPath, &Data));
	ExpectOld(Data);
	Retry(Data);
}

TEST_F(QmSodaLyricFileLoad, DirectoryIsRejectedWithoutChangingOldLyrics)
{
	char aDirectory[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aDirectory, sizeof(aDirectory));
	QmMusicLyrics::SLyricsData Data;
	ASSERT_NO_FATAL_FAILURE(PrepareOld(Data));
	EXPECT_FALSE(QmSodaLyricFile::LoadLyricFile(aDirectory, &Data));
	ExpectOld(Data);
	Retry(Data);
}

TEST_F(QmSodaLyricFileLoad, ProductionBudgetIncludesLimitAndRejectsOneMoreByte)
{
	CFile File(io_open(m_aPath, IOFLAG_WRITE), io_close);
	ASSERT_NE(File, nullptr);
	ASSERT_LT(m_Input.m_Json.size(), QmSodaLyricFile::MAX_FILE_BYTES);
	ASSERT_EQ(io_write(File.get(), m_Input.m_Json.data(), static_cast<unsigned>(m_Input.m_Json.size())), m_Input.m_Json.size());
	// 用合法的尾随空白填到真实预算，避免坏 JSON 掩盖预算回归。
	const std::string Padding(64 * 1024, ' ');
	size_t Remaining = QmSodaLyricFile::MAX_FILE_BYTES - m_Input.m_Json.size();
	while(Remaining != 0)
	{
		const auto Bytes = static_cast<unsigned>(std::min(Remaining, Padding.size()));
		ASSERT_EQ(io_write(File.get(), Padding.data(), Bytes), Bytes);
		Remaining -= Bytes;
	}
	ASSERT_EQ(io_close(File.release()), 0);
	QmMusicLyrics::SLyricsData Data;
	ASSERT_TRUE(QmSodaLyricFile::LoadLyricFile(m_aPath, &Data));
	ExpectNew(Data);
	File.reset(io_open(m_aPath, IOFLAG_APPEND));
	ASSERT_NE(File, nullptr);
	ASSERT_EQ(io_write(File.get(), "\n", 1), 1u);
	ASSERT_EQ(io_close(File.release()), 0);
	ASSERT_NO_FATAL_FAILURE(PrepareOld(Data));
	EXPECT_FALSE(QmSodaLyricFile::LoadLyricFile(m_aPath, &Data));
	ExpectOld(Data);
	Retry(Data);
}

TEST_F(QmSodaLyricFileLoad, NativePreparationFailuresPreserveOldLyricsAndSameFileRetries)
{
	ASSERT_NO_FATAL_FAILURE(Write(m_Input.m_Json));
	QmMusicLyrics::SLyricsData Warmup;
	ASSERT_TRUE(QmSodaLyricFile::LoadLyricFile(m_aPath, &Warmup));
	bool Loaded;
	std::size_t Allocations;
	{
		CPredictionAllocationFailure Trace(std::numeric_limits<std::size_t>::max());
		Loaded = QmSodaLyricFile::LoadLyricFile(m_aPath, &Warmup);
		Allocations = Trace.Attempts();
	}
	ASSERT_TRUE(Loaded);
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 256u);
	std::size_t Rejected = 0;
	for(std::size_t Allocation = 0; Allocation < Allocations; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		QmMusicLyrics::SLyricsData Data;
		ASSERT_NO_FATAL_FAILURE(PrepareOld(Data));
		bool Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Loaded = QmSodaLyricFile::LoadLyricFile(m_aPath, &Data);
			Triggered = Failure.Triggered();
		}
		ASSERT_TRUE(Triggered);
		if(Loaded)
		{
			// 稳定排序的可选 scratch 分配拒收仍可通过原地路径成功。
			ExpectNew(Data);
		}
		else
		{
			++Rejected;
			ExpectOld(Data);
		}
		ASSERT_TRUE(QmSodaLyricFile::LoadLyricFile(m_aPath, &Data));
		ExpectNew(Data);
	}
	EXPECT_GT(Rejected, 0u);
}
