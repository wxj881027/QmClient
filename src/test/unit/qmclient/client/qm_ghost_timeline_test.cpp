#include <game/client/components/qmclient/ghost_timeline.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <limits>

namespace
{
	struct SFrame
	{
		int m_Tick;
		int m_AttackTick;
	};
	class QmGhostTimeline : public ::testing::Test
	{
	protected:
		qm_ghost_path::CChunkedPath<SFrame> m_Path;
		void Fill(std::initializer_list<SFrame> Frames)
		{
			for(const auto &Frame : Frames)
				ASSERT_TRUE(m_Path.Add(Frame));
		}
		void ExpectTicks(std::initializer_list<int> Ticks)
		{
			ASSERT_EQ(m_Path.Size(), static_cast<int>(Ticks.size()));
			int i = 0;
			for(int Tick : Ticks)
			{
				ASSERT_NE(m_Path.Get(i), nullptr);
				EXPECT_EQ(m_Path.Get(i++)->m_Tick, Tick);
			}
		}
	};
}

TEST(QmGhostViewRange, OrdinaryRangeKeepsSourceTickCoordinates)
{
	qm_ghost_timeline::CViewRange Range;
	ASSERT_TRUE(Range.Prepare(100, 200));
	EXPECT_EQ(Range.First(), 100);
	EXPECT_EQ(Range.Last(), 300);
	EXPECT_EQ(Range.ToAbsolute(50), 150);
	EXPECT_EQ(Range.ToRelative(150), 50);
}

TEST(QmGhostViewRange, NegativeSourceOriginKeepsExistingZeroFallback)
{
	qm_ghost_timeline::CViewRange Range;
	ASSERT_TRUE(Range.Prepare(-100, 20));
	EXPECT_EQ(Range.First(), 0);
	EXPECT_EQ(Range.Last(), 20);
}

TEST(QmGhostViewRange, ExactPositiveLimitIsAccepted)
{
	qm_ghost_timeline::CViewRange Range;
	ASSERT_TRUE(Range.Prepare(100, std::numeric_limits<int>::max() - 100));
	EXPECT_EQ(Range.Last(), std::numeric_limits<int>::max());
	EXPECT_EQ(Range.ToAbsolute(std::numeric_limits<int>::max()), Range.Last());
}

TEST(QmGhostViewRange, AbsoluteOverflowRetainsThePublishedRange)
{
	qm_ghost_timeline::CViewRange Range;
	ASSERT_TRUE(Range.Prepare(10, 20));
	EXPECT_FALSE(Range.Prepare(1, std::numeric_limits<int>::max()));
	EXPECT_EQ(Range.First(), 10);
	EXPECT_EQ(Range.Last(), 30);
}

TEST(QmGhostViewRange, ZeroDurationDoesNotReplaceThePublishedRange)
{
	qm_ghost_timeline::CViewRange Range;
	ASSERT_TRUE(Range.Prepare(10, 20));
	EXPECT_FALSE(Range.Prepare(0, 0));
	EXPECT_EQ(Range.Duration(), 20);
}

TEST(QmGhostViewRange, NegativeDurationIsRejected)
{
	qm_ghost_timeline::CViewRange Range;
	EXPECT_FALSE(Range.Prepare(0, -1));
	EXPECT_EQ(Range.First(), 0);
	EXPECT_EQ(Range.Last(), 0);
}

TEST(QmGhostViewRange, MinimumExternalTickClampsBeforeNarrowing)
{
	qm_ghost_timeline::CViewRange Range;
	ASSERT_TRUE(Range.Prepare(std::numeric_limits<int>::max() - 10, 10));
	EXPECT_EQ(Range.ToRelative(std::numeric_limits<int>::min()), 0);
}

TEST(QmGhostViewRange, MaximumExternalTickClampsToTheEnd)
{
	qm_ghost_timeline::CViewRange Range;
	ASSERT_TRUE(Range.Prepare(100, 20));
	EXPECT_EQ(Range.ToRelative(std::numeric_limits<int>::max()), 20);
}

TEST(QmGhostViewRange, NegativeRelativeTickMapsToTheFirstTick)
{
	qm_ghost_timeline::CViewRange Range;
	ASSERT_TRUE(Range.Prepare(100, 20));
	EXPECT_EQ(Range.ToAbsolute(std::numeric_limits<int>::min()), 100);
}

TEST(QmGhostViewRange, RelativeTickBeyondTheEndMapsToTheLastTick)
{
	qm_ghost_timeline::CViewRange Range;
	ASSERT_TRUE(Range.Prepare(100, 20));
	EXPECT_EQ(Range.ToAbsolute(std::numeric_limits<int>::max()), 120);
}

TEST(QmGhostViewRange, RepeatedPreparationCanReplaceAnEarlierOrigin)
{
	qm_ghost_timeline::CViewRange Range;
	ASSERT_TRUE(Range.Prepare(100, 20));
	ASSERT_TRUE(Range.Prepare(200, 30));
	EXPECT_EQ(Range.ToAbsolute(15), 215);
	EXPECT_EQ(Range.ToRelative(215), 15);
}

TEST(QmGhostViewRange, DefaultInactiveRangeMapsAllInputsToZero)
{
	qm_ghost_timeline::CViewRange Range;
	EXPECT_EQ(Range.ToAbsolute(std::numeric_limits<int>::max()), 0);
	EXPECT_EQ(Range.ToRelative(std::numeric_limits<int>::min()), 0);
}

TEST_F(QmGhostTimeline, ExplicitTicksResolveTheMissingOriginAndPreserveDuplicates)
{
	ASSERT_NO_FATAL_FAILURE(Fill({{1000, 0}, {1000, 0}, {1010, 0}}));
	int Start = -1;
	ASSERT_TRUE(qm_ghost_timeline::Prepare(m_Path, true, Start));
	EXPECT_EQ(Start, 1000);
	ExpectTicks({1000, 1000, 1010});
}

TEST_F(QmGhostTimeline, ExplicitOriginKeepsRepresentablePreroll)
{
	ASSERT_NO_FATAL_FAILURE(Fill({{-5, 0}, {0, 0}, {5, 0}}));
	int Start = 0;
	ASSERT_TRUE(qm_ghost_timeline::Prepare(m_Path, true, Start));
	EXPECT_EQ(Start, 0);
	ExpectTicks({-5, 0, 5});
}

TEST_F(QmGhostTimeline, DescendingSamplesDoNotPublishAnOrigin)
{
	ASSERT_NO_FATAL_FAILURE(Fill({{100, 0}, {90, 0}}));
	int Start = -1;
	EXPECT_FALSE(qm_ghost_timeline::Prepare(m_Path, true, Start));
	EXPECT_EQ(Start, -1);
	ExpectTicks({100, 90});
}

TEST_F(QmGhostTimeline, PositiveRelativeOverflowIsRejected)
{
	ASSERT_NO_FATAL_FAILURE(Fill({{std::numeric_limits<int>::max(), 0}}));
	int Start = std::numeric_limits<int>::min();
	EXPECT_FALSE(qm_ghost_timeline::Prepare(m_Path, true, Start));
	EXPECT_EQ(Start, std::numeric_limits<int>::min());
}

TEST_F(QmGhostTimeline, NegativeRelativeOverflowIsRejected)
{
	ASSERT_NO_FATAL_FAILURE(Fill({{std::numeric_limits<int>::min(), 0}}));
	int Start = std::numeric_limits<int>::max();
	EXPECT_FALSE(qm_ghost_timeline::Prepare(m_Path, true, Start));
	EXPECT_EQ(Start, std::numeric_limits<int>::max());
}

TEST_F(QmGhostTimeline, LegacyTicksUseTheLastAttackChangeAsBefore)
{
	ASSERT_NO_FATAL_FAILURE(Fill({{7, 0}, {8, 101}, {9, 105}}));
	int Start = -1;
	ASSERT_TRUE(qm_ghost_timeline::Prepare(m_Path, false, Start));
	EXPECT_EQ(Start, 103);
	ExpectTicks({103, 104, 105});
}

TEST_F(QmGhostTimeline, LegacyTicksWithoutAttackChangesStartAtZero)
{
	ASSERT_NO_FATAL_FAILURE(Fill({{7, 5}, {8, 5}}));
	int Start = -1;
	ASSERT_TRUE(qm_ghost_timeline::Prepare(m_Path, false, Start));
	EXPECT_EQ(Start, 0);
	ExpectTicks({0, 1});
}

TEST_F(QmGhostTimeline, LegacyStartUnderflowKeepsEveryOriginalTick)
{
	ASSERT_NO_FATAL_FAILURE(Fill({{7, 0}, {8, std::numeric_limits<int>::min()}}));
	int Start = -1;
	EXPECT_FALSE(qm_ghost_timeline::Prepare(m_Path, false, Start));
	EXPECT_EQ(Start, -1);
	ExpectTicks({7, 8});
}

TEST_F(QmGhostTimeline, LegacyEndOverflowKeepsEveryOriginalTick)
{
	const int Max = std::numeric_limits<int>::max();
	ASSERT_NO_FATAL_FAILURE(Fill({{7, 0}, {8, Max}, {9, Max}}));
	int Start = -1;
	EXPECT_FALSE(qm_ghost_timeline::Prepare(m_Path, false, Start));
	EXPECT_EQ(Start, -1);
	ExpectTicks({7, 8, 9});
}

TEST_F(QmGhostTimeline, LegacyReconstructionKeepsAnExplicitOrigin)
{
	ASSERT_NO_FATAL_FAILURE(Fill({{7, 100}, {8, 101}}));
	int Start = 50;
	ASSERT_TRUE(qm_ghost_timeline::Prepare(m_Path, false, Start));
	EXPECT_EQ(Start, 50);
	ExpectTicks({100, 101});
}

TEST_F(QmGhostTimeline, EmptyPathDoesNotChangeTheOrigin)
{
	int Start = 17;
	EXPECT_FALSE(qm_ghost_timeline::Prepare(m_Path, true, Start));
	EXPECT_EQ(Start, 17);
}

TEST_F(QmGhostTimeline, LegacyRelativeOverflowDoesNotPartiallyRebuild)
{
	ASSERT_NO_FATAL_FAILURE(Fill({{7, 100}, {8, 101}}));
	int Start = std::numeric_limits<int>::min();
	EXPECT_FALSE(qm_ghost_timeline::Prepare(m_Path, false, Start));
	ExpectTicks({7, 8});
}

TEST(QmGhostTimelineMath, OrdinaryAttackMappingKeepsTheOriginalAge)
{
	int Result = 0;
	ASSERT_TRUE(qm_ghost_timeline::MapAttack(950, 1000, 500, 499, Result));
	EXPECT_EQ(Result, 450);
}

TEST(QmGhostTimelineMath, UnrepresentableMappedAttackDoesNotChangeOutput)
{
	int Result = 17;
	EXPECT_FALSE(qm_ghost_timeline::MapAttack(std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), 100, 99, Result));
	EXPECT_EQ(Result, 17);
}

TEST(QmGhostTimelineMath, UnrepresentableRendererSubtractionDoesNotChangeOutput)
{
	int Result = 17;
	EXPECT_FALSE(qm_ghost_timeline::MapAttack(std::numeric_limits<int>::min(), 0, 0, 1, Result));
	EXPECT_EQ(Result, 17);
}

TEST(QmGhostTimelineMath, WideGhostTickIsRejectedBeforeFurtherArithmetic)
{
	int Result = 17;
	EXPECT_FALSE(qm_ghost_timeline::MapAttack(0, std::numeric_limits<int64_t>::max(), 0, 0, Result));
	EXPECT_EQ(Result, 17);
}

TEST(QmGhostTimelineMath, RepresentableNegativeAttackTicksRemainLegal)
{
	int Result = 0;
	ASSERT_TRUE(qm_ghost_timeline::MapAttack(-5, 10, 0, -1, Result));
	EXPECT_EQ(Result, -15);
}

TEST(QmGhostTimelineMath, MaximumRepresentableAttackAgeRemainsLegal)
{
	int Result = 0;
	ASSERT_TRUE(qm_ghost_timeline::MapAttack(0, std::numeric_limits<int>::max(), 0, 0, Result));
	EXPECT_EQ(Result, -std::numeric_limits<int>::max());
}

TEST(QmGhostTimelineMath, MinimumRepresentableAttackAgeRemainsLegal)
{
	int Result = 0;
	ASSERT_TRUE(qm_ghost_timeline::MapAttack(std::numeric_limits<int>::max(), 0, 0, -1, Result));
	EXPECT_EQ(Result, std::numeric_limits<int>::max());
}

TEST(QmGhostTimelineMath, TickTranslationPromotesBeforeAdditionAndSubtraction)
{
	const int Max = std::numeric_limits<int>::max(), Min = std::numeric_limits<int>::min();
	EXPECT_EQ(qm_ghost_timeline::OffsetTick(Max, Max, Min), 6442450942LL);
	EXPECT_EQ(qm_ghost_timeline::OffsetTick(Min, Min, Max), -6442450943LL);
	EXPECT_EQ(qm_ghost_timeline::OffsetTick(100, 25, 10), 115);
}
