#include <base/str.h>

#include <engine/shared/jsonwriter.h>

#include <game/client/components/qmclient/music_lyrics/qm_soda_lyric_file.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstddef>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	std::string TranslationTag(std::string_view Json)
	{
		std::string Encoded((Json.size() + 2) / 3 * 4 + 1, '\0');
		str_base64(Encoded.data(), (int)Encoded.size(), Json.data(), (int)Json.size());
		Encoded.pop_back();
		return "[language:" + Encoded + "]\n";
	}

	std::string SodaJson(const std::string &Text, const char *pSeparate = nullptr, const std::string &Title = "metadata kept")
	{
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Writer.WriteAttribute("title");
		Writer.WriteStrValue(Title.c_str());
		Writer.WriteAttribute("lyricType");
		Writer.WriteStrValue("krc");
		Writer.WriteAttribute("lyricContent");
		Writer.WriteStrValue(Text.c_str());
		if(pSeparate != nullptr)
		{
			Writer.WriteAttribute("translationLrc");
			Writer.WriteStrValue(pSeparate);
		}
		Writer.EndObject();
		return Writer.GetOutputString();
	}

	struct SInput
	{
		const std::string m_Title = std::string(96, 't');
		const std::string m_Late = std::string(96, 'l');
		const std::string m_Early = std::string(80, 'e');
		const std::string m_LateTranslation = std::string(80, 'x');
		const std::string m_EarlyTranslation = std::string(64, 'y');
		const std::string m_Text = TranslationTag("{\"content\":[{\"type\":1,\"lyricContent\":[[\"" + m_LateTranslation + "\"],[\"empty-t\"],[\"" + m_EarlyTranslation + "\"]]}]}") +
					   "[2000,10]" + m_Late + "\n[1500,10]\n[1000,10]" + m_Early + "\n";
		const std::string m_Json = SodaJson(m_Text, nullptr, m_Title);
	};

	void CheckData(const QmMusicLyrics::SLyricsData &Data, const SInput &Input)
	{
		EXPECT_EQ(Data.m_Song.m_Title, Input.m_Title);
		EXPECT_TRUE(Data.m_Timeline.m_HasTiming);
		ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 2u);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_StartMs, 1000);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_EndMs, 1010);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, Input.m_Early);
		EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_StartMs, 2000);
		EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_EndMs, 2010);
		EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, Input.m_Late);
		ASSERT_EQ(Data.m_vTranslations.size(), 2u);
		EXPECT_EQ(Data.m_vTranslations[0], Input.m_EarlyTranslation);
		EXPECT_EQ(Data.m_vTranslations[1], Input.m_LateTranslation);
	}
}

TEST(QmSodaKrcTranslation, EmbeddedTrackFollowsSourceRowsAndKeepsSongMetadata)
{
	const std::string Text = TranslationTag(R"({"content":[{"type":1,"lyricContent":[["late-t"],["empty-t"],["broken-t"],["early-first-t"],["early-second-t"]]}]})") +
				 "[ti:title]\n[ar:artist, another]\n\nplain noise\n"
				 "[2000,0]late\n[1500,0]\n[1200,100]<broken>discarded\n[1000,0]<0,100>early-first\n[1000,50]early-second\n";
	QmMusicLyrics::SLyricsData Data;
	ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(SodaJson(Text), &Data));
	EXPECT_EQ(Data.m_Song.m_Title, "metadata kept");
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 3u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, "early-first");
	EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, "early-second");
	EXPECT_EQ(Data.m_Timeline.m_vLines[2].m_Text, "late");
	const std::vector<std::string> Expected{"early-first-t", "early-second-t", "late-t"};
	EXPECT_EQ(Data.m_vTranslations, Expected);
	EXPECT_TRUE(Data.HasTranslation());
}

TEST(QmSodaKrcTranslation, EveryNonemptySeparateTrackOverridesEmbeddedEvenWhenUnavailable)
{
	struct SCase
	{
		const char *m_pSeparate;
		std::vector<std::string> m_vExpected;
	};
	const SCase aCases[] = {
		{nullptr, {"early-t", "late-t"}},
		{"", {"early-t", "late-t"}},
		{"[00:02.00]separate-late\n[00:01.00]separate-early", {"separate-early", "separate-late"}},
		{"not a timed track", {}},
		{" \t", {}},
	};
	const std::string Text = TranslationTag(R"({"content":[{"type":1,"lyricContent":[["late-t"],["early-t"]]}]})") + "[2000,100]late\n[1000,100]early\n";
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pSeparate == nullptr ? "missing" : Case.m_pSeparate);
		QmMusicLyrics::SLyricsData Data;
		ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(SodaJson(Text, Case.m_pSeparate), &Data));
		EXPECT_EQ(Data.m_Song.m_Title, "metadata kept");
		ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 2u);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, "early");
		EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, "late");
		EXPECT_EQ(Data.m_vTranslations, Case.m_vExpected);
	}
}

TEST(QmSodaKrcTranslation, NativeCandidateFailuresKeepTheExistingFailureShapeAndTheSameFileRetries)
{
	const SInput Input;
	const char *pOld = R"({"title":"old title","lyricType":"lrc","lyricContent":"[00:01.00]old line","translationLrc":"[00:01.00]old translation"})";
	QmMusicLyrics::SLyricsData Warmup;
	ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(Input.m_Json, &Warmup));
	CheckData(Warmup, Input);
	QmMusicLyrics::SLyricsData Baseline;
	ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(pOld, &Baseline));
	bool Parsed;
	std::size_t Allocations;
	{
		CPredictionAllocationFailure Trace(std::numeric_limits<std::size_t>::max());
		Parsed = QmSodaLyricFile::ParseLyricFileJson(Input.m_Json, &Baseline);
		Allocations = Trace.Attempts();
	}
	ASSERT_TRUE(Parsed);
	CheckData(Baseline, Input);
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 256u);
	std::size_t Exceptions = 0;
	for(std::size_t Allocation = 0; Allocation < Allocations; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		QmMusicLyrics::SLyricsData Out;
		ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(pOld, &Out));
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
			// Soda先清旧数据，Song字段允许已准备的新metadata；两条歌词轨不半发布。
			EXPECT_TRUE(Out.m_Song.m_Title.empty() || Out.m_Song.m_Title == Input.m_Title);
			EXPECT_FALSE(Out.m_Timeline.m_HasTiming);
			EXPECT_TRUE(Out.m_Timeline.m_vLines.empty());
			EXPECT_TRUE(Out.m_vTranslations.empty());
		}
		else
		{
			ASSERT_TRUE(Parsed);
			CheckData(Out, Input);
		}
		ASSERT_TRUE(QmSodaLyricFile::ParseLyricFileJson(Input.m_Json, &Out));
		CheckData(Out, Input);
	}
	EXPECT_GT(Exceptions, 0u);
}
