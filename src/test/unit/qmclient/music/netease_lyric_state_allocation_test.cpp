#include <game/client/components/qmclient/netease/netease_lyric_state.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstddef>
#include <limits>
#include <new>
#include <string>
#include <string_view>

using namespace NeteaseLyrics;

namespace
{
	struct SApplyObservation
	{
		bool m_Applied = false;
		bool m_Threw = false;
		bool m_Triggered = false;
		size_t m_Attempts = 0;
	};

	SApplyObservation ObserveApply(CLyricState &State, ESource Source, std::string_view Text,
		int64_t Start, int64_t End, int64_t Position, size_t Budget = 0)
	{
		SApplyObservation Result;
		CPredictionAllocationFailure Failure(Budget);
		try
		{
			Result.m_Applied = State.ApplyCurrentLine(Source, Text, Start, End, Position);
		}
		catch(const std::bad_alloc &)
		{
			Result.m_Threw = true;
		}
		Result.m_Triggered = Failure.Triggered();
		Result.m_Attempts = Failure.Attempts();
		return Result;
	}

	void ExpectState(const SCurrentState &Actual, const SCurrentState &Expected)
	{
		EXPECT_EQ(Actual.m_SongId, Expected.m_SongId);
		EXPECT_EQ(Actual.m_Generation, Expected.m_Generation);
		EXPECT_EQ(Actual.m_Source, Expected.m_Source);
		EXPECT_EQ(Actual.m_PositionMs, Expected.m_PositionMs);
		EXPECT_EQ(Actual.m_LineStartMs, Expected.m_LineStartMs);
		EXPECT_EQ(Actual.m_LineEndMs, Expected.m_LineEndMs);
		EXPECT_EQ(Actual.m_HasSong, Expected.m_HasSong);
		EXPECT_EQ(Actual.m_LyricValid, Expected.m_LyricValid);
		EXPECT_EQ(Actual.m_CurrentLyric, Expected.m_CurrentLyric);
	}
}

TEST(NeteaseLyricStateAllocation, SameSourceRefusalKeepsTheWholeStateAndTheRetryReusesCapacity)
{
	CLyricState State;
	ASSERT_TRUE(State.UpdateSong(42));
	State.AdoptGeneration(7);
	ASSERT_TRUE(State.ApplyCurrentLine(ESource::Frontend, "old", 1000, 2000, 1500));
	const SCurrentState Before = State.Snapshot();
	// 合法桥接句子长度，首次赋值需要扩展刚建立状态的短字符串容量。
	const std::string Text(768, 'n');
	const auto Refused = ObserveApply(State, ESource::Frontend, Text, 3000, 5000, 3500);
	EXPECT_TRUE(Refused.m_Triggered);
	EXPECT_TRUE(Refused.m_Threw);
	EXPECT_FALSE(Refused.m_Applied);
	ExpectState(State.Snapshot(), Before);
	ASSERT_TRUE(State.ApplyCurrentLine(ESource::Frontend, Text, 3000, 5000, 3500));
	EXPECT_EQ(State.Snapshot().m_CurrentLyric, Text);
	EXPECT_EQ(State.Snapshot().m_PositionMs, 3500);
	EXPECT_EQ(State.Snapshot().m_LineStartMs, 3000);
	EXPECT_EQ(State.Snapshot().m_LineEndMs, 5000);
	const auto Reused = ObserveApply(State, ESource::Frontend, Text, 3000, 5000, 3600, std::numeric_limits<size_t>::max());
	EXPECT_TRUE(Reused.m_Applied);
	EXPECT_FALSE(Reused.m_Threw);
	EXPECT_EQ(Reused.m_Attempts, 0u);
	EXPECT_EQ(State.Snapshot().m_PositionMs, 3600);
}

TEST(NeteaseLyricStateAllocation, RefusedHigherPriorityLineDoesNotClaimTheSource)
{
	CLyricState State;
	ASSERT_TRUE(State.UpdateSong(42));
	ASSERT_TRUE(State.ApplyCurrentLine(ESource::DesktopLyricsFallback, "old", 1000, 2000, 1500));
	const SCurrentState Before = State.Snapshot();
	const std::string Text(768, 'f');
	const auto Refused = ObserveApply(State, ESource::Frontend, Text, 3000, 5000, 3500);
	EXPECT_TRUE(Refused.m_Triggered);
	EXPECT_TRUE(Refused.m_Threw);
	ExpectState(State.Snapshot(), Before);
	// 不能让失败的新来源抢占内部优先级，阻断原来源后续正常更新。
	ASSERT_TRUE(State.ApplyCurrentLine(ESource::DesktopLyricsFallback, "continued", 2000, 3000, 2500));
	EXPECT_EQ(State.Snapshot().m_CurrentLyric, "continued");
	ASSERT_TRUE(State.ApplyCurrentLine(ESource::Frontend, Text, 3000, 5000, 3500));
	EXPECT_EQ(State.Snapshot().m_Source, ESource::Frontend);
	const SCurrentState Accepted = State.Snapshot();
	EXPECT_FALSE(State.ApplyCurrentLine(ESource::DesktopLyricsFallback, "late", 5000, 6000, 5500));
	ExpectState(State.Snapshot(), Accepted);
}

TEST(NeteaseLyricStateAllocation, InitialLineRefusalDoesNotPublishAValidLyric)
{
	CLyricState State;
	ASSERT_TRUE(State.UpdateSong(42));
	State.AdoptGeneration(7);
	const SCurrentState Before = State.Snapshot();
	const std::string Text(768, 'i');
	const auto Refused = ObserveApply(State, ESource::Frontend, Text, 1000, -1, 1500);
	EXPECT_TRUE(Refused.m_Triggered);
	EXPECT_TRUE(Refused.m_Threw);
	ExpectState(State.Snapshot(), Before);
	ASSERT_TRUE(State.ApplyCurrentLine(ESource::Frontend, Text, 1000, -1, 1500));
	EXPECT_TRUE(State.Snapshot().m_LyricValid);
	EXPECT_EQ(State.Snapshot().m_CurrentLyric, Text);
	EXPECT_EQ(State.Snapshot().m_LineEndMs, -1);
}

TEST(NeteaseLyricStateAllocation, InvalidBoundariesStillReturnFalseWithoutPreparingText)
{
	CLyricState State;
	ASSERT_TRUE(State.UpdateSong(42));
	ASSERT_TRUE(State.ApplyCurrentLine(ESource::Frontend, "old", 1000, 2000, 1500));
	const SCurrentState Before = State.Snapshot();
	const std::string Text(768, 'x');
	const auto Rejected = ObserveApply(State, ESource::Frontend, Text, 3000, 3000, 3500);
	EXPECT_FALSE(Rejected.m_Applied);
	EXPECT_FALSE(Rejected.m_Threw);
	EXPECT_FALSE(Rejected.m_Triggered);
	EXPECT_EQ(Rejected.m_Attempts, 0u);
	ExpectState(State.Snapshot(), Before);
}
