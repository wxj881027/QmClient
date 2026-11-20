#include <game/client/components/qmclient/ghost_render.h>

#include <gtest/gtest.h>

#include <cfenv>
#include <limits>
#include <memory>

namespace
{
	struct SPoint
	{
		int m_X;
		int m_Y;
	};
	class QmGhostPosition : public ::testing::Test
	{
		std::fenv_t m_Environment{};

	protected:
		vec2 m_Result{9.0f, 10.0f};
		void SetUp() override
		{
			ASSERT_EQ(std::feholdexcept(&m_Environment), 0);
			ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
		}
		void TearDown() override { std::fesetenv(&m_Environment); }
		void ExpectUnchanged() { EXPECT_EQ(m_Result, vec2(9.0f, 10.0f)); }
	};
	struct SDraw
	{
		int m_Id = 0;
		std::shared_ptr<int> m_pResource;
	};
}

TEST_F(QmGhostPosition, OrdinaryInterpolationKeepsTheNativePosition)
{
	ASSERT_TRUE(qm_ghost_render::Position(SPoint{10, 20}, SPoint{20, 30}, 0.5f, m_Result));
	EXPECT_EQ(m_Result, vec2(15.0f, 25.0f));
}

TEST_F(QmGhostPosition, NegativeCoordinatesRemainLegal)
{
	ASSERT_TRUE(qm_ghost_render::Position(SPoint{-10, -20}, SPoint{-20, -30}, 0.5f, m_Result));
	EXPECT_EQ(m_Result, vec2(-15.0f, -25.0f));
}

TEST_F(QmGhostPosition, IntegerFootOffsetOverflowDoesNotPublishPosition)
{
	EXPECT_FALSE(qm_ghost_render::Position(SPoint{0, 0}, SPoint{0, std::numeric_limits<int>::max()}, 1.0f, m_Result));
	ExpectUnchanged();
}

TEST_F(QmGhostPosition, CurrentXThatRoundsOutsideIntDoesNotPublishPosition)
{
	EXPECT_FALSE(qm_ghost_render::Position(SPoint{0, 0}, SPoint{std::numeric_limits<int>::max(), 0}, 1.0f, m_Result));
	ExpectUnchanged();
}

TEST_F(QmGhostPosition, RepresentableFootIntegerCanStillHaveAnUnsafeFloatConversion)
{
	EXPECT_FALSE(qm_ghost_render::Position(SPoint{0, 0}, SPoint{0, std::numeric_limits<int>::max() - 16}, 1.0f, m_Result));
	ExpectUnchanged();
}

TEST_F(QmGhostPosition, UnsafeInterpolatedXIsRejectedEvenWithASafeCurrentRecord)
{
	EXPECT_FALSE(qm_ghost_render::Position(SPoint{std::numeric_limits<int>::max(), 0}, SPoint{0, 0}, 0.0f, m_Result));
	ExpectUnchanged();
}

TEST_F(QmGhostPosition, UnsafeInterpolatedFootIsRejectedEvenWithASafeCurrentRecord)
{
	EXPECT_FALSE(qm_ghost_render::Position(SPoint{0, std::numeric_limits<int>::max()}, SPoint{0, 0}, 0.0f, m_Result));
	ExpectUnchanged();
}

TEST_F(QmGhostPosition, NanInterpolationDoesNotPublishPosition)
{
	EXPECT_FALSE(qm_ghost_render::Position(SPoint{0, 0}, SPoint{1, 1}, std::numeric_limits<float>::quiet_NaN(), m_Result));
	ExpectUnchanged();
}

TEST_F(QmGhostPosition, InfiniteInterpolationDoesNotPublishPosition)
{
	for(float Intra : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Intra);
		EXPECT_FALSE(qm_ghost_render::Position(SPoint{0, 0}, SPoint{1, 1}, Intra, m_Result));
		ExpectUnchanged();
	}
}

TEST_F(QmGhostPosition, ExtremeInterpolationIsRejectedBeforeFloatMultiplicationOverflows)
{
	std::feclearexcept(FE_OVERFLOW);
	EXPECT_FALSE(qm_ghost_render::Position(SPoint{0, 0}, SPoint{100, 100}, std::numeric_limits<float>::max(), m_Result));
	EXPECT_EQ(std::fetestexcept(FE_OVERFLOW), 0);
	ExpectUnchanged();
}

TEST_F(QmGhostPosition, AnExtremePhaseDoesNotRejectAConstantSafePosition)
{
	ASSERT_TRUE(qm_ghost_render::Position(SPoint{7, 8}, SPoint{7, 8}, std::numeric_limits<float>::max(), m_Result));
	EXPECT_EQ(m_Result, vec2(7.0f, 8.0f));
}

TEST_F(QmGhostPosition, MinimumIntegerCoordinateCanStillBeRoundedSafely)
{
	const int Min = std::numeric_limits<int>::min();
	ASSERT_TRUE(qm_ghost_render::Position(SPoint{Min, 0}, SPoint{Min, 0}, 0.0f, m_Result));
	EXPECT_EQ(round_to_int(m_Result.x), Min);
}

TEST_F(QmGhostPosition, LargestSafeFloatBelowThePositiveIntegerLimitIsAccepted)
{
	const int X = std::numeric_limits<int>::max() - 127;
	ASSERT_TRUE(qm_ghost_render::Position(SPoint{X, 0}, SPoint{X, 0}, 0.0f, m_Result));
	EXPECT_EQ(round_to_int(m_Result.x), X);
}

TEST_F(QmGhostPosition, RepresentableExtrapolationKeepsTheExistingCalculation)
{
	ASSERT_TRUE(qm_ghost_render::Position(SPoint{1, 2}, SPoint{3, 4}, -1.0f, m_Result));
	EXPECT_EQ(m_Result, vec2(-1.0f, 0.0f));
}

TEST(QmGhostFrameCache, EmptyAndOutOfRangeLookupsDoNotExposeEntries)
{
	qm_ghost_render::CFrameCache<SDraw, 2> Cache;
	EXPECT_EQ(Cache.Size(), 0u);
	EXPECT_EQ(Cache.Get(0), nullptr);
	EXPECT_EQ(Cache.Get(2), nullptr);
}

TEST(QmGhostFrameCache, AppendingKeepsOrderAndPreviouslyBorrowedAddresses)
{
	qm_ghost_render::CFrameCache<SDraw, 2> Cache;
	ASSERT_TRUE(Cache.Add(SDraw{1, {}}));
	const SDraw *pFirst = Cache.Get(0);
	ASSERT_TRUE(Cache.Add(SDraw{2, {}}));
	EXPECT_EQ(Cache.Get(0), pFirst);
	ASSERT_NE(Cache.Get(1), nullptr);
	EXPECT_EQ(Cache.Get(1)->m_Id, 2);
}

TEST(QmGhostFrameCache, FullCacheRejectsWithoutTakingTheInputResource)
{
	qm_ghost_render::CFrameCache<SDraw, 1> Cache;
	ASSERT_TRUE(Cache.Add(SDraw{1, {}}));
	SDraw Pending{2, std::make_shared<int>(7)};
	const auto pResource = Pending.m_pResource;
	EXPECT_FALSE(Cache.Add(std::move(Pending)));
	EXPECT_EQ(Cache.Size(), 1u);
	EXPECT_EQ(Pending.m_pResource, pResource);
}

TEST(QmGhostFrameCache, ClearReleasesFrameResourcesAndCanStartAnotherFrame)
{
	qm_ghost_render::CFrameCache<SDraw, 2> Cache;
	auto pResource = std::make_shared<int>(7);
	std::weak_ptr<int> Weak = pResource;
	ASSERT_TRUE(Cache.Add(SDraw{1, std::move(pResource)}));
	EXPECT_FALSE(Weak.expired());
	Cache.Clear();
	EXPECT_TRUE(Weak.expired());
	EXPECT_EQ(Cache.Get(0), nullptr);
	ASSERT_TRUE(Cache.Add(SDraw{2, {}}));
	EXPECT_EQ(Cache.Size(), 1u);
	EXPECT_EQ(Cache.Get(0)->m_Id, 2);
}

TEST(QmGhostFrameCache, DestructionReleasesTheLastFrameResources)
{
	std::weak_ptr<int> Weak;
	{
		qm_ghost_render::CFrameCache<SDraw, 1> Cache;
		auto pResource = std::make_shared<int>(7);
		Weak = pResource;
		ASSERT_TRUE(Cache.Add(SDraw{1, std::move(pResource)}));
	}
	EXPECT_TRUE(Weak.expired());
}

TEST(QmGhostFrameCache, RepeatedClearKeepsTheCacheEmpty)
{
	qm_ghost_render::CFrameCache<SDraw, 2> Cache;
	ASSERT_TRUE(Cache.Add(SDraw{1, {}}));
	Cache.Clear();
	Cache.Clear();
	EXPECT_EQ(Cache.Size(), 0u);
	EXPECT_EQ(Cache.Get(0), nullptr);
}

TEST(QmGhostFrameCache, ZeroCapacityRejectsEntriesWithoutTakingResources)
{
	qm_ghost_render::CFrameCache<SDraw, 0> Cache;
	SDraw Pending{1, std::make_shared<int>(7)};
	EXPECT_FALSE(Cache.Add(std::move(Pending)));
	EXPECT_NE(Pending.m_pResource, nullptr);
	EXPECT_EQ(Cache.Size(), 0u);
}
