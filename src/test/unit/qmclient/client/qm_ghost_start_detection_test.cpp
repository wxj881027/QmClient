#include <game/client/components/qmclient/ghost_start_detection.h>

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	using qm_ghost_start::FindStartTick;
	using qm_ghost_start::SSample;

	struct CStartProbe
	{
		std::vector<std::pair<vec2, vec2>> m_vSegments;
		bool m_Hit = false;

		bool operator()(vec2 Prev, vec2 Cur)
		{
			// 发生扫描上限回归时立即失败，避免测试自身循环数亿次。
			if(m_vSegments.size() >= 150)
				throw std::runtime_error("ghost start scan exceeded three seconds");
			m_vSegments.emplace_back(Prev, Cur);
			return m_Hit;
		}
	};
}

TEST(QmGhostStartDetection, DumpZeroPreviousTickUsesPreviousSnapshotTime)
{
	CStartProbe Probe;
	const SSample Prev{0, 343632408, vec2(0, 2)};
	const SSample Cur{343632410, 343632410, vec2(4, 6)};
	EXPECT_EQ(FindStartTick(Prev, Cur, -1, false, Probe), -1);
	ASSERT_EQ(Probe.m_vSegments.size(), 2u);
	EXPECT_EQ(Probe.m_vSegments[0].first, vec2(0, 2));
	EXPECT_EQ(Probe.m_vSegments[0].second, vec2(2, 4));
	EXPECT_EQ(Probe.m_vSegments[1].first, vec2(2, 4));
	EXPECT_EQ(Probe.m_vSegments[1].second, vec2(4, 6));
}

TEST(QmGhostStartDetection, ZeroCurrentTickUsesCurrentSnapshotTime)
{
	CStartProbe Probe;
	Probe.m_Hit = true;
	EXPECT_EQ(FindStartTick({100, 100, vec2(0, 0)}, {0, 102, vec2(2, 0)}, -1, true, Probe), 102);
	EXPECT_EQ(Probe.m_vSegments.size(), 2u);
}

TEST(QmGhostStartDetection, BothZeroTicksUseTheirOwnSnapshotTimes)
{
	CStartProbe Probe;
	Probe.m_Hit = true;
	EXPECT_EQ(FindStartTick({0, 100, vec2(0, 0)}, {0, 103, vec2(3, 0)}, -1, true, Probe), 103);
	EXPECT_EQ(Probe.m_vSegments.size(), 3u);
}

TEST(QmGhostStartDetection, PositiveCharacterTicksKeepTheirOriginalTime)
{
	CStartProbe Probe;
	Probe.m_Hit = true;
	EXPECT_EQ(FindStartTick({100, 110, vec2(0, 0)}, {102, 112, vec2(2, 0)}, -1, true, Probe), 102);
	EXPECT_EQ(Probe.m_vSegments.size(), 2u);
}

TEST(QmGhostStartDetection, NormalStartStopsAtFirstMatchingSegment)
{
	int Calls = 0;
	const int Tick = FindStartTick({100, 100, vec2(0, 0)}, {105, 105, vec2(10, 0)}, -1, false,
		[&Calls](vec2 Prev, vec2 Cur) {
			++Calls;
			return Prev.x >= 2 && Cur.x <= 8;
		});
	EXPECT_EQ(Tick, 102);
	EXPECT_EQ(Calls, 2);
}

TEST(QmGhostStartDetection, RestartUsesLastMatchingSegment)
{
	int Calls = 0;
	const int Tick = FindStartTick({100, 100, vec2(0, 0)}, {105, 105, vec2(10, 0)}, -1, true,
		[&Calls](vec2 Prev, vec2 Cur) {
			++Calls;
			return Prev.x >= 2 && Cur.x <= 8;
		});
	EXPECT_EQ(Tick, 104);
	EXPECT_EQ(Calls, 5);
}

TEST(QmGhostStartDetection, ThreeSecondIntervalStillChecksEverySegment)
{
	CStartProbe Probe;
	Probe.m_Hit = true;
	EXPECT_EQ(FindStartTick({100, 100, vec2(0, 0)}, {250, 250, vec2(150, 0)}, -1, true, Probe), 250);
	EXPECT_EQ(Probe.m_vSegments.size(), 150u);
}

TEST(QmGhostStartDetection, IntervalBeyondThreeSecondsDoesNotScan)
{
	CStartProbe Probe;
	Probe.m_Hit = true;
	EXPECT_EQ(FindStartTick({100, 100, vec2(0, 0)}, {251, 251, vec2(151, 0)}, -1, true, Probe), -1);
	EXPECT_TRUE(Probe.m_vSegments.empty());
}

TEST(QmGhostStartDetection, HugePositiveIntervalDoesNotScan)
{
	CStartProbe Probe;
	Probe.m_Hit = true;
	EXPECT_EQ(FindStartTick({1, 1, vec2(0, 0)}, {343632410, 343632410, vec2(2, 0)}, -1, true, Probe), -1);
	EXPECT_TRUE(Probe.m_vSegments.empty());
}

TEST(QmGhostStartDetection, ZeroTickFallbackCannotBypassScanLimit)
{
	CStartProbe Probe;
	Probe.m_Hit = true;
	EXPECT_EQ(FindStartTick({0, 1, vec2(0, 0)}, {0, 343632410, vec2(2, 0)}, -1, true, Probe), -1);
	EXPECT_TRUE(Probe.m_vSegments.empty());
}

TEST(QmGhostStartDetection, EqualOrBackwardTicksDoNotScan)
{
	for(const int CurTick : {100, 99})
	{
		SCOPED_TRACE(CurTick);
		CStartProbe Probe;
		EXPECT_EQ(FindStartTick({100, 100, vec2(0, 0)}, {CurTick, CurTick, vec2(2, 0)}, -1, false, Probe), -1);
		EXPECT_TRUE(Probe.m_vSegments.empty());
	}
}

TEST(QmGhostStartDetection, NegativeCharacterTicksDoNotFallBackToSnapshotTime)
{
	for(const auto &Ticks : {std::pair{-1, 102}, std::pair{100, -1}})
	{
		SCOPED_TRACE(Ticks.first);
		SCOPED_TRACE(Ticks.second);
		CStartProbe Probe;
		EXPECT_EQ(FindStartTick({Ticks.first, 100, vec2(0, 0)}, {Ticks.second, 102, vec2(2, 0)}, -1, false, Probe), -1);
		EXPECT_TRUE(Probe.m_vSegments.empty());
	}
}

TEST(QmGhostStartDetection, InvalidSnapshotFallbackDoesNotScan)
{
	CStartProbe Probe;
	EXPECT_EQ(FindStartTick({0, -1, vec2(0, 0)}, {0, 1, vec2(2, 0)}, -1, false, Probe), -1);
	EXPECT_TRUE(Probe.m_vSegments.empty());
}

TEST(QmGhostStartDetection, IntegerExtremeDifferencesDoNotWrapIntoSmallIntervals)
{
	constexpr int Min = std::numeric_limits<int>::min();
	constexpr int Max = std::numeric_limits<int>::max();
	for(const auto &Ticks : {std::pair{Min, Max}, std::pair{Max, Min}})
	{
		SCOPED_TRACE(Ticks.first);
		SCOPED_TRACE(Ticks.second);
		CStartProbe Probe;
		EXPECT_EQ(FindStartTick({Ticks.first, 100, vec2(0, 0)}, {Ticks.second, 102, vec2(2, 0)}, -1, false, Probe), -1);
		EXPECT_TRUE(Probe.m_vSegments.empty());
	}
}

TEST(QmGhostStartDetection, ValidIntervalNearIntegerLimitReturnsExactTick)
{
	constexpr int Max = std::numeric_limits<int>::max();
	CStartProbe Probe;
	Probe.m_Hit = true;
	EXPECT_EQ(FindStartTick({Max - 2, Max - 2, vec2(0, 0)}, {Max, Max, vec2(2, 0)}, -1, true, Probe), Max);
	EXPECT_EQ(Probe.m_vSegments.size(), 2u);
}

TEST(QmGhostStartDetection, DeathAtOrAfterPreviousSamplePreventsStart)
{
	for(const int DeathTick : {100, 101, 102})
	{
		SCOPED_TRACE(DeathTick);
		CStartProbe Probe;
		Probe.m_Hit = true;
		EXPECT_EQ(FindStartTick({0, 100, vec2(0, 0)}, {102, 102, vec2(2, 0)}, DeathTick, false, Probe), -1);
		EXPECT_TRUE(Probe.m_vSegments.empty());
	}
}

TEST(QmGhostStartDetection, ZeroTickSampleAfterDeathCanStartAgain)
{
	CStartProbe Probe;
	Probe.m_Hit = true;
	EXPECT_EQ(FindStartTick({0, 100, vec2(0, 0)}, {102, 102, vec2(2, 0)}, 99, false, Probe), 101);
	EXPECT_EQ(Probe.m_vSegments.size(), 1u);
}

TEST(QmGhostStartDetection, ValidSampleAfterRejectedGapCanStart)
{
	CStartProbe Probe;
	Probe.m_Hit = true;
	EXPECT_EQ(FindStartTick({1, 1, vec2(0, 0)}, {1000, 1000, vec2(2, 0)}, -1, false, Probe), -1);
	EXPECT_TRUE(Probe.m_vSegments.empty());
	EXPECT_EQ(FindStartTick({1000, 1000, vec2(2, 0)}, {1002, 1002, vec2(4, 0)}, -1, false, Probe), 1001);
	EXPECT_EQ(Probe.m_vSegments.size(), 1u);
}
