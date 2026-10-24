#include <game/client/components/qmclient/netease_hook/qm_netease_hook_protocol.h>

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <utility>

namespace
{
	QmNeteaseHook::SSnapshot MakeSnapshot(int64_t Duration, int64_t Position)
	{
		QmNeteaseHook::SSnapshot Snapshot{};
		Snapshot.m_Sequence = 2;
		Snapshot.m_Status = QmNeteaseHook::STATUS_HAS_MEDIA;
		Snapshot.m_DurationMs = Duration;
		Snapshot.m_PositionMs = Position;
		QmNeteaseHook::FinalizeSnapshot(&Snapshot);
		return Snapshot;
	}
}

TEST(QmNeteaseHookNumericValidation, RejectsNonFinitePlaybackRates)
{
	for(double Rate : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
	{
		SCOPED_TRACE(Rate);
		auto Snapshot = MakeSnapshot(10000, 1000);
		Snapshot.m_PlaybackRate = Rate;
		QmNeteaseHook::FinalizeSnapshot(&Snapshot);
		EXPECT_FALSE(QmNeteaseHook::ValidateSnapshot(Snapshot));
	}
}

TEST(QmNeteaseHookNumericValidation, KeepsTheSupportedClosedPlaybackRateInterval)
{
	const std::pair<double, bool> aCases[] = {{-1.0, false}, {0.0, true}, {0.5, true}, {8.0, true}, {9.0, false}};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.first);
		auto Snapshot = MakeSnapshot(10000, 1000);
		Snapshot.m_PlaybackRate = Case.first;
		QmNeteaseHook::FinalizeSnapshot(&Snapshot);
		EXPECT_EQ(QmNeteaseHook::ValidateSnapshot(Snapshot), Case.second);
	}
}

TEST(QmNeteaseHookNumericValidation, AcceptsMaximumDurationAndPosition)
{
	const int64_t Max = std::numeric_limits<int64_t>::max();
	EXPECT_TRUE(QmNeteaseHook::ValidateSnapshot(MakeSnapshot(Max, Max)));
}

TEST(QmNeteaseHookNumericValidation, RetainsTheOneSecondPositionMarginNearIntegerLimits)
{
	const int64_t Max = std::numeric_limits<int64_t>::max();
	struct SCase
	{
		int64_t m_Duration;
		int64_t m_Position;
		bool m_Valid;
	};
	const SCase aCases[] = {{10000, 11000, true}, {10000, 11001, false}, {Max - 500, Max, true}, {Max - 1000, Max, true}, {Max - 1001, Max, false}};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_Duration);
		SCOPED_TRACE(Case.m_Position);
		EXPECT_EQ(QmNeteaseHook::ValidateSnapshot(MakeSnapshot(Case.m_Duration, Case.m_Position)), Case.m_Valid);
	}
}

TEST(QmNeteaseHookNumericValidation, UnknownDurationDoesNotLimitANonnegativePosition)
{
	EXPECT_TRUE(QmNeteaseHook::ValidateSnapshot(MakeSnapshot(0, std::numeric_limits<int64_t>::max())));
}

TEST(QmNeteaseHookNumericValidation, RejectsNegativeDuration)
{
	EXPECT_FALSE(QmNeteaseHook::ValidateSnapshot(MakeSnapshot(-1, 0)));
}

TEST(QmNeteaseHookNumericValidation, RejectsNegativePosition)
{
	EXPECT_FALSE(QmNeteaseHook::ValidateSnapshot(MakeSnapshot(10000, -1)));
}

TEST(QmNeteaseHookNumericValidation, AcceptsCurrentLineBoundariesAtTheMaximumDuration)
{
	const int64_t Max = std::numeric_limits<int64_t>::max();
	auto Snapshot = MakeSnapshot(Max, Max);
	Snapshot.m_Status |= QmNeteaseHook::STATUS_HAS_CURRENT_LINE;
	std::strcpy(Snapshot.m_aCurrentLine, "line");
	Snapshot.m_CurrentLineStartMs = Max - 1;
	Snapshot.m_CurrentLineEndMs = Max;
	QmNeteaseHook::FinalizeSnapshot(&Snapshot);
	EXPECT_TRUE(QmNeteaseHook::ValidateSnapshot(Snapshot));
}

TEST(QmNeteaseHookNumericValidation, CurrentLineEndKeepsTheSameOneSecondDurationMargin)
{
	auto Snapshot = MakeSnapshot(10000, 1000);
	Snapshot.m_Status |= QmNeteaseHook::STATUS_HAS_CURRENT_LINE;
	std::strcpy(Snapshot.m_aCurrentLine, "line");
	Snapshot.m_CurrentLineStartMs = 10000;
	Snapshot.m_CurrentLineEndMs = 11000;
	QmNeteaseHook::FinalizeSnapshot(&Snapshot);
	EXPECT_TRUE(QmNeteaseHook::ValidateSnapshot(Snapshot));
	Snapshot.m_CurrentLineEndMs = 11001;
	QmNeteaseHook::FinalizeSnapshot(&Snapshot);
	EXPECT_FALSE(QmNeteaseHook::ValidateSnapshot(Snapshot));
}

TEST(QmNeteaseHookChecksum, KeepsTheIeeeCrc32ReferenceValue)
{
	constexpr char aText[] = "123456789";
	EXPECT_EQ(QmNeteaseHook::Crc32(aText, sizeof(aText) - 1), 0xCBF43926U);
}

TEST(QmNeteaseHookChecksum, EmptyPayloadDoesNotReadTheInput)
{
	EXPECT_EQ(QmNeteaseHook::Crc32(nullptr, 0), 0U);
}
