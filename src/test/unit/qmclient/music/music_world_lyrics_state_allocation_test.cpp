#include <game/client/components/qmclient/music_lyrics/music_world_lyrics_state.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <new>
#include <stdexcept>
#include <string>

using namespace QmMusicLyrics;

namespace
{
	constexpr int BILINGUAL = static_cast<int>(EDisplayLanguage::BILINGUAL);
	constexpr int TRANSLATION = static_cast<int>(EDisplayLanguage::TRANSLATION);

	struct SInputs
	{
		std::string m_First = std::string(320, 'p');
		std::string m_Current = std::string(512, 'c');
		std::string m_Next = std::string(640, 'n');
		std::string m_Translation = std::string(384, 't');
		std::string m_NewTranslation = std::string(768, 'u');

		SSourceSnapshot First() const { return {42, 1, 100, true, nullptr, m_First, m_Translation, 1000}; }
		SSourceSnapshot Current() const { return {42, 1, 100, true, nullptr, m_Current, m_Translation, 2000}; }
		SSourceSnapshot Next() const { return {42, 1, 100, true, nullptr, m_Next, m_Translation, 3000}; }
	};

	void Seed(CWorldLyricsState &State, const SInputs &Inputs, double FirstNow = 1.0, double CurrentNow = 1.5)
	{
		State.Update(true, 1, Inputs.First(), BILINGUAL, vec2(100, 200), FirstNow);
		State.Update(true, 1, Inputs.Current(), BILINGUAL, vec2(300, 400), CurrentNow);
	}

	struct SAttempt
	{
		size_t m_Allocations = 0;
		bool m_Triggered = false;
		bool m_ResourceExceptionEscaped = false;
	};

	SAttempt UpdateWithFailure(CWorldLyricsState &State, int SourceId, const SSourceSnapshot &Source, int Language, vec2 Anchor, double Now, size_t BeforeFailure, bool Available = true)
	{
		SAttempt Attempt;
		CPredictionAllocationFailure Failure(BeforeFailure);
		try
		{
			State.Update(Available, SourceId, Source, Language, Anchor, Now);
		}
		catch(const std::bad_alloc &)
		{
			Attempt.m_ResourceExceptionEscaped = true;
		}
		catch(const std::length_error &)
		{
			Attempt.m_ResourceExceptionEscaped = true;
		}
		Attempt.m_Allocations = Failure.Attempts();
		Attempt.m_Triggered = Failure.Triggered();
		return Attempt;
	}

	void ExpectSameLyric(const SWorldLyric &Actual, const SWorldLyric &Expected)
	{
		EXPECT_EQ(Actual.m_Active, Expected.m_Active);
		EXPECT_EQ(Actual.m_Anchor, Expected.m_Anchor);
		EXPECT_DOUBLE_EQ(Actual.m_CreatedAt, Expected.m_CreatedAt);
		EXPECT_DOUBLE_EQ(Actual.m_FadeAt, Expected.m_FadeAt);
		EXPECT_FLOAT_EQ(Actual.m_FadeAlpha, Expected.m_FadeAlpha);
		EXPECT_EQ(Actual.m_LineStartMs, Expected.m_LineStartMs);
		EXPECT_EQ(Actual.m_Original, Expected.m_Original);
		EXPECT_EQ(Actual.m_Translation, Expected.m_Translation);
		EXPECT_EQ(Actual.m_aRows, Expected.m_aRows);
		EXPECT_EQ(Actual.m_aCharacterCounts, Expected.m_aCharacterCounts);
		EXPECT_EQ(Actual.m_aVisibleCharacters, Expected.m_aVisibleCharacters);
		EXPECT_EQ(Actual.m_RowCount, Expected.m_RowCount);
	}

	void ExpectNoAllocation(const SAttempt &Attempt)
	{
		EXPECT_FALSE(Attempt.m_Triggered);
		EXPECT_FALSE(Attempt.m_ResourceExceptionEscaped);
		EXPECT_EQ(Attempt.m_Allocations, 0u);
	}
}

TEST(QmWorldLyricsAllocation, NewLineFailuresPreserveBothLinesAndTheOldClockUntilRetry)
{
	const SInputs Inputs;
	const auto Next = Inputs.Next();
	CWorldLyricsState TraceState;
	Seed(TraceState, Inputs);
	const auto Trace = UpdateWithFailure(TraceState, 1, Next, BILINGUAL, vec2(700, 800), 5.0, std::numeric_limits<size_t>::max());
	ASSERT_FALSE(Trace.m_ResourceExceptionEscaped);
	ASSERT_FALSE(Trace.m_Triggered);
	ASSERT_GT(Trace.m_Allocations, 0u);
	ASSERT_LE(Trace.m_Allocations, 64u);
	for(size_t Budget = 0; Budget < Trace.m_Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CWorldLyricsState State;
		Seed(State, Inputs);
		const auto OldCurrent = State.Current();
		const auto OldPrevious = State.Previous();
		const auto Attempt = UpdateWithFailure(State, 1, Next, BILINGUAL, vec2(700, 800), 5.0, Budget);
		EXPECT_TRUE(Attempt.m_Triggered);
		EXPECT_FALSE(Attempt.m_ResourceExceptionEscaped);
		ExpectSameLyric(State.Current(), OldCurrent);
		ExpectSameLyric(State.Previous(), OldPrevious);
		// 失败帧的时刻不应让仍晚于旧帧的输入被误判为时钟回拨。
		ExpectNoAllocation(UpdateWithFailure(State, 1, Inputs.Current(), BILINGUAL, vec2(900, 900), 2.0, 0));
		ExpectSameLyric(State.Current(), OldCurrent);
		ExpectSameLyric(State.Previous(), OldPrevious);
		State.Update(true, 1, Next, BILINGUAL, vec2(700, 800), 5.0);
		EXPECT_EQ(State.Current().m_Original, Inputs.m_Next);
		EXPECT_EQ(State.Current().m_aRows[0], Inputs.m_Next);
		EXPECT_EQ(State.Current().m_aRows[1], Inputs.m_Translation);
		EXPECT_EQ(State.Current().m_Anchor, vec2(700, 800));
		EXPECT_DOUBLE_EQ(State.Current().m_CreatedAt, 5.0);
		EXPECT_EQ(State.Previous().m_Original, OldCurrent.m_Original);
		EXPECT_DOUBLE_EQ(State.Previous().m_FadeAt, 5.0);
	}
}

TEST(QmWorldLyricsAllocation, LanguageFailuresKeepOldRowsAndRetryWithoutRespawningTheLine)
{
	const SInputs Inputs;
	const auto Source = Inputs.Current();
	CWorldLyricsState TraceState;
	Seed(TraceState, Inputs);
	const auto Trace = UpdateWithFailure(TraceState, 1, Source, TRANSLATION, vec2(700, 800), 5.0, std::numeric_limits<size_t>::max());
	ASSERT_FALSE(Trace.m_ResourceExceptionEscaped);
	ASSERT_FALSE(Trace.m_Triggered);
	ASSERT_GT(Trace.m_Allocations, 0u);
	ASSERT_LE(Trace.m_Allocations, 64u);
	for(size_t Budget = 0; Budget < Trace.m_Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CWorldLyricsState State;
		Seed(State, Inputs);
		const auto OldCurrent = State.Current();
		const auto OldPrevious = State.Previous();
		const auto Attempt = UpdateWithFailure(State, 1, Source, TRANSLATION, vec2(700, 800), 5.0, Budget);
		EXPECT_TRUE(Attempt.m_Triggered);
		EXPECT_FALSE(Attempt.m_ResourceExceptionEscaped);
		ExpectSameLyric(State.Current(), OldCurrent);
		ExpectSameLyric(State.Previous(), OldPrevious);
		ExpectNoAllocation(UpdateWithFailure(State, 1, Source, BILINGUAL, vec2(900, 900), 2.0, 0));
		ExpectSameLyric(State.Current(), OldCurrent);
		State.Update(true, 1, Source, TRANSLATION, vec2(700, 800), 5.0);
		EXPECT_EQ(State.Current().m_Anchor, OldCurrent.m_Anchor);
		EXPECT_DOUBLE_EQ(State.Current().m_CreatedAt, OldCurrent.m_CreatedAt);
		EXPECT_EQ(State.Current().m_RowCount, 1);
		EXPECT_EQ(State.Current().m_aRows[0], Inputs.m_Translation);
		EXPECT_TRUE(State.Current().m_aRows[1].empty());
		EXPECT_EQ(State.Current().m_aVisibleCharacters[0], static_cast<int>(Inputs.m_Translation.size()));
		EXPECT_FALSE(State.Previous().m_Active);
		ExpectNoAllocation(UpdateWithFailure(State, 1, Source, TRANSLATION, vec2(900, 900), 5.1, 0));
	}
}

TEST(QmWorldLyricsAllocation, TranslationFailuresKeepOldContentAndRetryWithoutRespawningTheLine)
{
	const SInputs Inputs;
	auto Source = Inputs.Current();
	Source.m_Translation = Inputs.m_NewTranslation;
	CWorldLyricsState TraceState;
	Seed(TraceState, Inputs);
	const auto Trace = UpdateWithFailure(TraceState, 1, Source, BILINGUAL, vec2(700, 800), 5.0, std::numeric_limits<size_t>::max());
	ASSERT_FALSE(Trace.m_ResourceExceptionEscaped);
	ASSERT_FALSE(Trace.m_Triggered);
	ASSERT_GT(Trace.m_Allocations, 0u);
	ASSERT_LE(Trace.m_Allocations, 64u);
	for(size_t Budget = 0; Budget < Trace.m_Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CWorldLyricsState State;
		Seed(State, Inputs);
		const auto OldCurrent = State.Current();
		const auto OldPrevious = State.Previous();
		const auto Attempt = UpdateWithFailure(State, 1, Source, BILINGUAL, vec2(700, 800), 5.0, Budget);
		EXPECT_TRUE(Attempt.m_Triggered);
		EXPECT_FALSE(Attempt.m_ResourceExceptionEscaped);
		ExpectSameLyric(State.Current(), OldCurrent);
		ExpectSameLyric(State.Previous(), OldPrevious);
		ExpectNoAllocation(UpdateWithFailure(State, 1, Inputs.Current(), BILINGUAL, vec2(900, 900), 2.0, 0));
		ExpectSameLyric(State.Current(), OldCurrent);
		State.Update(true, 1, Source, BILINGUAL, vec2(700, 800), 5.0);
		EXPECT_EQ(State.Current().m_Anchor, OldCurrent.m_Anchor);
		EXPECT_DOUBLE_EQ(State.Current().m_CreatedAt, OldCurrent.m_CreatedAt);
		EXPECT_EQ(State.Current().m_Translation, Inputs.m_NewTranslation);
		EXPECT_EQ(State.Current().m_RowCount, 2);
		EXPECT_EQ(State.Current().m_aRows[0], Inputs.m_Current);
		EXPECT_EQ(State.Current().m_aRows[1], Inputs.m_NewTranslation);
		EXPECT_EQ(State.Current().m_aCharacterCounts[1], static_cast<int>(Inputs.m_NewTranslation.size()));
		EXPECT_FALSE(State.Previous().m_Active);
		ExpectNoAllocation(UpdateWithFailure(State, 1, Source, BILINGUAL, vec2(900, 900), 5.1, 0));
	}
}

TEST(QmWorldLyricsAllocation, IdentityFailuresKeepTheOldIdentityAndDiscardOldLinesOnlyAfterRetry)
{
	const SInputs Inputs;
	for(int Field = 0; Field < 4; ++Field)
	{
		SCOPED_TRACE(Field);
		auto Source = Inputs.Next();
		const int SourceId = Field == 0 ? 2 : 1;
		if(Field == 1)
			++Source.m_SongId;
		else if(Field == 2)
			++Source.m_Generation;
		else if(Field == 3)
			++Source.m_ProducerPid;
		CWorldLyricsState TraceState;
		Seed(TraceState, Inputs);
		const auto Trace = UpdateWithFailure(TraceState, SourceId, Source, BILINGUAL, vec2(700, 800), 5.0, std::numeric_limits<size_t>::max());
		ASSERT_FALSE(Trace.m_ResourceExceptionEscaped);
		ASSERT_FALSE(Trace.m_Triggered);
		ASSERT_GT(Trace.m_Allocations, 0u);
		ASSERT_LE(Trace.m_Allocations, 64u);
		for(size_t Budget = 0; Budget < Trace.m_Allocations; ++Budget)
		{
			SCOPED_TRACE(Budget);
			CWorldLyricsState State;
			Seed(State, Inputs);
			const auto OldCurrent = State.Current();
			const auto OldPrevious = State.Previous();
			const auto Attempt = UpdateWithFailure(State, SourceId, Source, BILINGUAL, vec2(700, 800), 5.0, Budget);
			EXPECT_TRUE(Attempt.m_Triggered);
			EXPECT_FALSE(Attempt.m_ResourceExceptionEscaped);
			ExpectSameLyric(State.Current(), OldCurrent);
			ExpectSameLyric(State.Previous(), OldPrevious);
			ExpectNoAllocation(UpdateWithFailure(State, 1, Inputs.Current(), BILINGUAL, vec2(900, 900), 2.0, 0));
			ExpectSameLyric(State.Current(), OldCurrent);
			State.Update(true, SourceId, Source, BILINGUAL, vec2(700, 800), 5.0);
			EXPECT_EQ(State.Current().m_Original, Inputs.m_Next);
			EXPECT_EQ(State.Current().m_Anchor, vec2(700, 800));
			EXPECT_DOUBLE_EQ(State.Current().m_CreatedAt, 5.0);
			EXPECT_FALSE(State.Previous().m_Active);
		}
	}
}

TEST(QmWorldLyricsAllocation, RewindFailuresKeepTheOldPairAndRestartAppearanceOnRetry)
{
	const SInputs Inputs;
	const auto Source = Inputs.Current();
	CWorldLyricsState TraceState;
	Seed(TraceState, Inputs, 5.0, 5.5);
	const auto Trace = UpdateWithFailure(TraceState, 1, Source, BILINGUAL, vec2(700, 800), 2.0, std::numeric_limits<size_t>::max());
	ASSERT_FALSE(Trace.m_ResourceExceptionEscaped);
	ASSERT_FALSE(Trace.m_Triggered);
	ASSERT_GT(Trace.m_Allocations, 0u);
	ASSERT_LE(Trace.m_Allocations, 64u);
	for(size_t Budget = 0; Budget < Trace.m_Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CWorldLyricsState State;
		Seed(State, Inputs, 5.0, 5.5);
		const auto OldCurrent = State.Current();
		const auto OldPrevious = State.Previous();
		const auto Attempt = UpdateWithFailure(State, 1, Source, BILINGUAL, vec2(700, 800), 2.0, Budget);
		EXPECT_TRUE(Attempt.m_Triggered);
		EXPECT_FALSE(Attempt.m_ResourceExceptionEscaped);
		ExpectSameLyric(State.Current(), OldCurrent);
		ExpectSameLyric(State.Previous(), OldPrevious);
		State.Update(true, 1, Source, BILINGUAL, vec2(700, 800), 2.0);
		EXPECT_EQ(State.Current().m_Original, Inputs.m_Current);
		EXPECT_EQ(State.Current().m_Anchor, vec2(700, 800));
		EXPECT_DOUBLE_EQ(State.Current().m_CreatedAt, 2.0);
		EXPECT_FALSE(State.Previous().m_Active);
		EXPECT_FLOAT_EQ(WorldLyricVisual(State.Current(), 2.0).m_Alpha, 0.0f);
		ExpectNoAllocation(UpdateWithFailure(State, 1, Source, BILINGUAL, vec2(900, 900), 2.1, 0));
		EXPECT_EQ(State.Current().m_Anchor, vec2(700, 800));
	}
}

TEST(QmWorldLyricsAllocation, StableFramesUpdateWordTimingAndDisableWithoutAllocation)
{
	const SInputs Inputs;
	SLyricsData Data;
	Data.m_Timeline.m_HasTiming = true;
	Data.m_Timeline.m_vLines.push_back({1000, 5000, Inputs.m_Current, {{1000, 5000, Inputs.m_Current}}});
	const auto &Line = Data.m_Timeline.m_vLines.front();
	SSourceSnapshot Source{42, 1, 100, true, &Data, Line.m_Text, Inputs.m_Translation, 1000, 5000, 1000};
	CWorldLyricsState State;
	State.Update(true, 1, Inputs.First(), BILINGUAL, vec2(100, 200), 1.0);
	State.Update(true, 1, Source, BILINGUAL, vec2(300, 400), 2.0);
	Source.m_PositionMs = 3000;
	ExpectNoAllocation(UpdateWithFailure(State, 1, Source, BILINGUAL, vec2(700, 800), 3.0, 0));
	EXPECT_EQ(WorldLyricVisibleCharacters(State.Current(), 0), 256);
	EXPECT_EQ(WorldLyricVisibleCharacters(State.Current(), 1), 384);
	Source.m_PositionMs = 2000;
	ExpectNoAllocation(UpdateWithFailure(State, 1, Source, BILINGUAL, vec2(900, 900), 4.0, 0));
	EXPECT_EQ(WorldLyricVisibleCharacters(State.Current(), 0), 128);
	EXPECT_EQ(State.Current().m_Anchor, vec2(300, 400));
	EXPECT_DOUBLE_EQ(State.Current().m_CreatedAt, 2.0);
	EXPECT_FALSE(State.Previous().m_Active);
	ExpectNoAllocation(UpdateWithFailure(State, 1, Source, BILINGUAL, vec2(900, 900), 5.0, 0, false));
	EXPECT_FALSE(State.Current().m_Active);
	EXPECT_FALSE(State.Previous().m_Active);
}
