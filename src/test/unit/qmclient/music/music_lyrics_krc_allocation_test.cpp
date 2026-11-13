#include <base/str.h>

#include <game/client/components/qmclient/music_lyrics/music_lyrics_krc.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstddef>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace
{
	struct SInput
	{
		const std::string m_First = std::string(96, 'f');
		const std::string m_Second = std::string(80, 's');
		const std::string m_Json = "{\"content\":[{\"type\":1,\"lyricContent\":[[\"  " + m_First + "\",\"" + m_Second + "  \"],null,[\"  " + m_Second + "  \"]]}]}";
		const std::string m_Text = MakeTag(m_Json);

		static std::string MakeTag(const std::string &Json)
		{
			std::string Encoded((Json.size() + 2) / 3 * 4 + 1, '\0');
			str_base64(Encoded.data(), (int)Encoded.size(), Json.data(), (int)Json.size());
			Encoded.pop_back();
			return "[language:" + Encoded + "]\n";
		}
	};

	void CheckTranslation(const std::vector<std::string> &Translation, const SInput &Input)
	{
		ASSERT_EQ(Translation.size(), 3u);
		EXPECT_EQ(Translation[0], Input.m_First + Input.m_Second);
		EXPECT_TRUE(Translation[1].empty());
		EXPECT_EQ(Translation[2], Input.m_Second);
	}

	QmMusicLyrics::SLyricsData OldData()
	{
		QmMusicLyrics::SLyricsData Result;
		Result.m_Song = {"old title", "old artist", "old album", "old cover", 42};
		Result.m_Timeline.m_HasTiming = true;
		Result.m_Timeline.m_vLines.push_back({11, 22, "old line", {{11, 12, "old word"}}});
		Result.m_vTranslations = {"old translation"};
		return Result;
	}

	void CheckOldData(const QmMusicLyrics::SLyricsData &Data)
	{
		EXPECT_EQ(Data.m_Song.m_Title, "old title");
		EXPECT_EQ(Data.m_Song.m_Artist, "old artist");
		EXPECT_EQ(Data.m_Song.m_Album, "old album");
		EXPECT_EQ(Data.m_Song.m_CoverPath, "old cover");
		EXPECT_EQ(Data.m_Song.m_DurationMs, 42);
		EXPECT_TRUE(Data.m_Timeline.m_HasTiming);
		ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 1u);
		const auto &Line = Data.m_Timeline.m_vLines[0];
		EXPECT_EQ(Line.m_StartMs, 11);
		EXPECT_EQ(Line.m_EndMs, 22);
		EXPECT_EQ(Line.m_Text, "old line");
		ASSERT_EQ(Line.m_vWords.size(), 1u);
		EXPECT_EQ(Line.m_vWords[0].m_StartMs, 11);
		EXPECT_EQ(Line.m_vWords[0].m_EndMs, 12);
		EXPECT_EQ(Line.m_vWords[0].m_Text, "old word");
		ASSERT_EQ(Data.m_vTranslations.size(), 1u);
		EXPECT_EQ(Data.m_vTranslations[0], "old translation");
	}

	void CheckCombinedData(const QmMusicLyrics::SLyricsData &Data, const SInput &Input)
	{
		EXPECT_TRUE(Data.m_Song.m_Title.empty());
		EXPECT_TRUE(Data.m_Song.m_Artist.empty());
		EXPECT_TRUE(Data.m_Song.m_Album.empty());
		EXPECT_TRUE(Data.m_Song.m_CoverPath.empty());
		EXPECT_EQ(Data.m_Song.m_DurationMs, 0);
		EXPECT_TRUE(Data.m_Timeline.m_HasTiming);
		ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 2u);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_StartMs, 1000);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_EndMs, 1010);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, Input.m_Second);
		EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_StartMs, 2000);
		EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_EndMs, 2010);
		EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, Input.m_First);
		ASSERT_EQ(Data.m_vTranslations.size(), 2u);
		EXPECT_EQ(Data.m_vTranslations[0], Input.m_Second);
		EXPECT_EQ(Data.m_vTranslations[1], Input.m_First + Input.m_Second);
	}
}

TEST(MusicLyricsKrcAllocation, TranslationPreparationFailuresPropagateAndTheSameTextCanRetry)
{
	const SInput Input;
	CheckTranslation(QmMusicLyrics::ExtractKrcTranslation(Input.m_Text), Input);
	std::vector<std::string> Baseline;
	std::size_t Allocations;
	{
		CPredictionAllocationFailure Trace(std::numeric_limits<std::size_t>::max());
		Baseline = QmMusicLyrics::ExtractKrcTranslation(Input.m_Text);
		Allocations = Trace.Attempts();
	}
	CheckTranslation(Baseline, Input);
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 256u);
	std::size_t Exceptions = 0;
	for(std::size_t Allocation = 0; Allocation < Allocations; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		std::vector<std::string> Translation;
		bool Threw = false, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			try
			{
				Translation = QmMusicLyrics::ExtractKrcTranslation(Input.m_Text);
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Failure.Triggered();
		}
		ASSERT_TRUE(Triggered);
		if(Threw)
			++Exceptions;
		else
			CheckTranslation(Translation, Input);
		CheckTranslation(QmMusicLyrics::ExtractKrcTranslation(Input.m_Text), Input);
	}
	// JSON树用malloc；释放由生产RAII静态核对，new矩阵不充当free计数。
	EXPECT_GT(Exceptions, 0u);
}

TEST(MusicLyricsKrcAllocation, CombinedCandidateFailuresKeepBothOldTracksAndTheSameTextCanRetry)
{
	const SInput Input;
	const std::string Text = Input.m_Text + "[2000,10]" + Input.m_First + "\n[1500,10]\n[1000,10]" + Input.m_Second + "\n";
	QmMusicLyrics::SLyricsData Warmup;
	ASSERT_TRUE(QmMusicLyrics::ParseKrcTextData(Text, &Warmup));
	CheckCombinedData(Warmup, Input);
	auto Baseline = OldData();
	bool Parsed;
	std::size_t Allocations;
	{
		CPredictionAllocationFailure Trace(std::numeric_limits<std::size_t>::max());
		Parsed = QmMusicLyrics::ParseKrcTextData(Text, &Baseline);
		Allocations = Trace.Attempts();
	}
	ASSERT_TRUE(Parsed);
	CheckCombinedData(Baseline, Input);
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 256u);
	std::size_t Exceptions = 0;
	for(std::size_t Allocation = 0; Allocation < Allocations; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		auto Out = OldData();
		bool Threw = false, Triggered;
		Parsed = false;
		{
			CPredictionAllocationFailure Failure(Allocation);
			try
			{
				Parsed = QmMusicLyrics::ParseKrcTextData(Text, &Out);
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
			CheckOldData(Out);
		}
		else
		{
			// stable_sort的可选缓冲拒收可以正常完成排序与发布。
			ASSERT_TRUE(Parsed);
			CheckCombinedData(Out, Input);
		}
		ASSERT_TRUE(QmMusicLyrics::ParseKrcTextData(Text, &Out));
		CheckCombinedData(Out, Input);
	}
	EXPECT_GT(Exceptions, 0u);
}
