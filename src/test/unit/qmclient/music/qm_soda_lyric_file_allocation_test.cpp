#include <game/client/components/qmclient/music_lyrics/qm_soda_lyric_file.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstddef>
#include <limits>
#include <new>
#include <string>

namespace
{
	struct SInput
	{
		const std::string m_Title = std::string(96, 't');
		const std::string m_Artist = std::string(64, 'a');
		const std::string m_Album = std::string(64, 'b');
		const std::string m_Cover = "https://covers.example/" + std::string(64, 'c');
		const std::string m_Words = std::string(96, 'w');
		const std::string m_Translation = std::string(64, 'x');
		const std::string m_Json = "{\"title\":\"" + m_Title + "\",\"artist\":\"" + m_Artist + "\",\"album\":\"" + m_Album +
					   "\",\"coverUrl\":\"" + m_Cover + "\",\"durationMs\":1250.75,\"lyricType\":\"lrc\",\"lyricContent\":\"[00:01.00]" +
					   m_Words + "\\n[00:02.00]tail\",\"translationLrc\":\"[00:01.00]" + m_Translation + "\\n[00:02.00]tail-t\"}";
	};

	void CheckLyrics(const QmMusicLyrics::SLyricsData &Data, const SInput &Input)
	{
		EXPECT_EQ(Data.m_Song.m_Title, Input.m_Title);
		EXPECT_EQ(Data.m_Song.m_Artist, Input.m_Artist);
		EXPECT_EQ(Data.m_Song.m_Album, Input.m_Album);
		EXPECT_EQ(Data.m_Song.m_CoverPath, Input.m_Cover);
		EXPECT_EQ(Data.m_Song.m_DurationMs, 1250);
		ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 2u);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_StartMs, 1000);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, Input.m_Words);
		EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, "tail");
		ASSERT_EQ(Data.m_vTranslations.size(), 2u);
		EXPECT_EQ(Data.m_vTranslations[0], Input.m_Translation);
		EXPECT_EQ(Data.m_vTranslations[1], "tail-t");
	}
}

TEST(QmSodaLyricFileAllocation, NativePreparationFailuresPropagateAndTheSameInputCanRetry)
{
	const SInput Input;
	QmMusicLyrics::SLyricsData Warmup;
	ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(Input.m_Json, &Warmup));
	QmMusicLyrics::SLyricsData Baseline;
	bool Parsed;
	std::size_t Allocations;
	{
		CPredictionAllocationFailure Trace(std::numeric_limits<std::size_t>::max());
		Parsed = QmSodaLyricFile::ParseLyricFileJson(Input.m_Json, &Baseline);
		Allocations = Trace.Attempts();
	}
	ASSERT_TRUE(Parsed);
	CheckLyrics(Baseline, Input);
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 256u);
	std::size_t Exceptions = 0;
	for(std::size_t Allocation = 0; Allocation < Allocations; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		QmMusicLyrics::SLyricsData Out;
		ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(R"({"title":"old title","lyricType":"lrc","lyricContent":"[00:01.00]old line"})", &Out));
		bool Threw = false, Triggered;
		Parsed = false;
		{
			CPredictionAllocationFailure Failure(Allocation);
			try
			{
				Parsed = QmSodaLyricFile::ParseLyricFileJson(Input.m_Json, &Out);
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Failure.Triggered();
		}
		ASSERT_TRUE(Triggered);
		if(Threw)
		{
			++Exceptions;
			EXPECT_NE(Out.m_Song.m_Title, "old title");
			for(const auto &Line : Out.m_Timeline.m_vLines)
				EXPECT_NE(Line.m_Text, "old line");
		}
		else
		{
			// stable_sort缓冲拒收可回退成功，不能要求每次new故障都向外抛。
			ASSERT_TRUE(Parsed);
			CheckLyrics(Out, Input);
		}
		QmMusicLyrics::SLyricsData Retry;
		ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(Input.m_Json, &Retry));
		CheckLyrics(Retry, Input);
	}
	// C解析树使用malloc，释放由生产RAII静态核对；new故障不冒充释放计数。
	EXPECT_GT(Exceptions, 0u);
}
