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
