#include <game/client/components/system_media_controls_timeline.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	constexpr int64_t MinTime = std::numeric_limits<int64_t>::min();
	constexpr int64_t MaxTime = std::numeric_limits<int64_t>::max();
	constexpr int64_t Second = SystemMediaControls::HUNDRED_NS_PER_SECOND;
}

TEST(QmMediaTimeline, SubtractsStartBeforeConvertingToMilliseconds)
{
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({Second, 16 * Second, 4 * Second, 0}, 0, 1234, 1000);
	EXPECT_EQ(Snapshot.m_DurationMs, 15000);
	EXPECT_EQ(Snapshot.m_PositionMs, 3000);
	EXPECT_EQ(Snapshot.m_PositionUpdatedTick, 1234);
}

TEST(QmMediaTimeline, ZeroDurationKeepsPositivePosition)
{
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({Second, 0, 4 * Second, 0}, 0, 0, 1000);
	EXPECT_EQ(Snapshot.m_DurationMs, 0);
	EXPECT_EQ(Snapshot.m_PositionMs, 3000);
}

TEST(QmMediaTimeline, PositionPastEndIsCappedToPositiveDuration)
{
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({Second, 4 * Second, 16 * Second, 0}, 0, 0, 1000);
	EXPECT_EQ(Snapshot.m_DurationMs, 3000);
	EXPECT_EQ(Snapshot.m_PositionMs, 3000);
}

TEST(QmMediaTimeline, PositiveSubmillisecondDurationStillCapsPosition)
{
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({0, 9999, Second, 0}, 0, 0, 1000);
	EXPECT_EQ(Snapshot.m_DurationMs, 0);
	EXPECT_EQ(Snapshot.m_PositionMs, 0);
}

TEST(QmMediaTimeline, FullSignedSpanKeepsRepresentableMilliseconds)
{
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({MinTime, MaxTime, MaxTime, 0}, 0, 0, 1000);
	constexpr int64_t FullSpanMs = static_cast<int64_t>(std::numeric_limits<uint64_t>::max() / 10000);
	EXPECT_EQ(Snapshot.m_DurationMs, FullSpanMs);
	EXPECT_EQ(Snapshot.m_PositionMs, FullSpanMs);
}

TEST(QmMediaTimeline, FullSignedPositionRemainsAvailableWithoutDuration)
{
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({MinTime, MinTime, MaxTime, 0}, 0, 0, 1000);
	EXPECT_EQ(Snapshot.m_DurationMs, 0);
	EXPECT_EQ(Snapshot.m_PositionMs, static_cast<int64_t>(std::numeric_limits<uint64_t>::max() / 10000));
}

TEST(QmMediaTimeline, ReversedSignedSpanClampsToZero)
{
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({MaxTime, MinTime, MinTime, 0}, 0, 0, 1000);
	EXPECT_EQ(Snapshot.m_DurationMs, 0);
	EXPECT_EQ(Snapshot.m_PositionMs, 0);
}

TEST(QmMediaTimeline, WidePositionIsCappedBeforeMillisecondConversion)
{
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({MinTime, -1, MaxTime, 0}, 0, 0, 1000);
	EXPECT_EQ(Snapshot.m_DurationMs, MaxTime / 10000);
	EXPECT_EQ(Snapshot.m_PositionMs, MaxTime / 10000);
}

TEST(QmMediaTimeline, ValidTimestampBackdatesTheObservation)
{
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({0, 0, 0, 98 * Second}, 100 * Second, 100000, 1000);
	EXPECT_EQ(Snapshot.m_PositionUpdatedTick, 98000);
}

TEST(QmMediaTimeline, InvalidFutureAndExpiredTimestampsKeepObservation)
{
	struct SCase
	{
		int64_t m_Updated;
		int64_t m_Observed;
	};
	const SCase aCases[] = {
		{MinTime, MaxTime},
		{0, MaxTime},
		{MaxTime, MinTime},
		{1, SystemMediaControls::MAX_TIMELINE_AGE_100NS + 2},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_Updated);
		const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({0, 0, 0, Case.m_Updated}, Case.m_Observed, 4567, Second);
		EXPECT_EQ(Snapshot.m_PositionUpdatedTick, 4567);
	}
}

TEST(QmMediaTimeline, MaximumAcceptedAgeStillBackdates)
{
	constexpr int64_t Age = SystemMediaControls::MAX_TIMELINE_AGE_100NS;
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({0, 0, 0, 5}, Age + 5, Age + 10, Second);
	EXPECT_EQ(Snapshot.m_PositionUpdatedTick, 10);
}

TEST(QmMediaTimeline, NonpositiveAgeOrFrequencyProducesNoElapsedTicks)
{
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(0, MaxTime), 0);
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(MinTime, MaxTime), 0);
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(MaxTime, 0), 0);
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(MaxTime, MinTime), 0);
}

TEST(QmMediaTimeline, FractionalAgeRetainsARepresentableQuotientAtMaximumFrequency)
{
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(Second / 2, MaxTime), MaxTime / 2);
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(1, MaxTime), MaxTime / Second);
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(Second - 1, MaxTime), MaxTime - (MaxTime / Second + 1));
}

TEST(QmMediaTimeline, WholeAndFractionalSecondsAreAddedExactly)
{
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(Second + Second / 2, 1000), 1500);
	constexpr int64_t Frequency = MaxTime / 2;
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(Second + Second / 2, Frequency), Frequency + Frequency / 2);
}

TEST(QmMediaTimeline, UnrepresentableElapsedTicksSaturate)
{
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(Second + Second / 2, MaxTime), MaxTime);
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(2 * Second, MaxTime), MaxTime);
}

TEST(QmMediaTimeline, MaximumRepresentableElapsedTicksAreKept)
{
	EXPECT_EQ(SystemMediaControls::TimelineAgeToTicks(MaxTime, Second), MaxTime);
}

TEST(QmMediaTimeline, UnrepresentableBackdateKeepsObservation)
{
	constexpr int64_t Observation = MinTime + Second - 1;
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({0, 0, 0, 1}, Second + 1, Observation, Second);
	EXPECT_EQ(Snapshot.m_PositionUpdatedTick, Observation);
}

TEST(QmMediaTimeline, ExactLowerBoundBackdateIsRepresentable)
{
	const auto Snapshot = SystemMediaControls::NormalizeTimelineProperties({0, 0, 0, 1}, Second + 1, MinTime + Second, Second);
	EXPECT_EQ(Snapshot.m_PositionUpdatedTick, MinTime);
}
