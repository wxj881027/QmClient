#include <game/client/components/qmclient/weapon_trajectory_geometry.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <limits>
#include <new>

TEST(QmWeaponTrajectoryGeometry, NextSuccessfulPreparationReplacesThePreviousDisplayMode)
{
	SQmWeaponTrajectoryScratch Scratch;
	ASSERT_TRUE(Scratch.TryPrepare([&] {
		Scratch.m_vPoints.emplace_back(1, 2);
		return true;
	}));
	ASSERT_TRUE(Scratch.TryPrepare([&] {
		Scratch.m_vLineSegments.emplace_back(vec2(0, 0), vec2(10, 0));
		return Scratch.PrepareQuads(2);
	}));
	EXPECT_TRUE(Scratch.m_vPoints.empty());
	EXPECT_EQ(Scratch.m_vLineSegments.size(), 1u);
	EXPECT_EQ(Scratch.m_vLineQuadSegments.size(), 1u);
	ASSERT_TRUE(Scratch.TryPrepare([&] {
		Scratch.m_vPoints.emplace_back(3, 4);
		return true;
	}));
	EXPECT_EQ(Scratch.m_vPoints.front(), vec2(3, 4));
	EXPECT_TRUE(Scratch.m_vLineSegments.empty());
	EXPECT_TRUE(Scratch.m_vLineQuadSegments.empty());
}

TEST(QmWeaponTrajectoryGeometry, RejectedPreparationClearsEveryPartialResult)
{
	SQmWeaponTrajectoryScratch Scratch;
	EXPECT_FALSE(Scratch.TryPrepare([&] {
		Scratch.m_vPoints.emplace_back(1, 2);
		Scratch.m_vLineSegments.emplace_back(vec2(0, 0), vec2(10, 0));
		Scratch.PrepareQuads(2);
		return false;
	}));
	EXPECT_TRUE(Scratch.m_vPoints.empty());
	EXPECT_TRUE(Scratch.m_vLineSegments.empty());
	EXPECT_TRUE(Scratch.m_vLineQuadSegments.empty());
}

TEST(QmWeaponTrajectoryGeometry, ExceptionClearsResultsRetainsCapacityAndAllowsRetry)
{
	SQmWeaponTrajectoryScratch Scratch;
	Scratch.m_vPoints.reserve(8);
	const auto Capacity = Scratch.m_vPoints.capacity();
	EXPECT_FALSE(Scratch.TryPrepare([&]() -> bool {
		Scratch.m_vPoints.emplace_back(1, 2);
		throw std::bad_alloc();
	}));
	EXPECT_TRUE(Scratch.m_vPoints.empty());
	EXPECT_EQ(Scratch.m_vPoints.capacity(), Capacity);
	ASSERT_TRUE(Scratch.TryPrepare([&] {
		Scratch.m_vPoints.emplace_back(3, 4);
		return true;
	}));
	EXPECT_EQ(Scratch.m_vPoints.front(), vec2(3, 4));
}

TEST(QmWeaponTrajectoryGeometry, WideLinesKeepCornerOrderAndSkipZeroLengthSegments)
{
	SQmWeaponTrajectoryScratch Scratch;
	ASSERT_TRUE(Scratch.TryPrepare([&] {
		Scratch.m_vLineSegments.emplace_back(vec2(0, 0), vec2(10, 0));
		Scratch.m_vLineSegments.emplace_back(vec2(5, 5), vec2(5, 5));
		Scratch.m_vLineSegments.emplace_back(vec2(10, 0), vec2(10, 10));
		return Scratch.PrepareQuads(2);
	}));
	ASSERT_EQ(Scratch.m_vLineQuadSegments.size(), 2u);
	const auto &Horizontal = Scratch.m_vLineQuadSegments[0];
	EXPECT_FLOAT_EQ(Horizontal.m_X0, 10);
	EXPECT_FLOAT_EQ(Horizontal.m_Y0, -2);
	EXPECT_FLOAT_EQ(Horizontal.m_X1, 10);
	EXPECT_FLOAT_EQ(Horizontal.m_Y1, 2);
	EXPECT_FLOAT_EQ(Horizontal.m_X2, 0);
	EXPECT_FLOAT_EQ(Horizontal.m_Y2, -2);
	EXPECT_FLOAT_EQ(Horizontal.m_X3, 0);
	EXPECT_FLOAT_EQ(Horizontal.m_Y3, 2);
	const auto &Vertical = Scratch.m_vLineQuadSegments[1];
	EXPECT_FLOAT_EQ(Vertical.m_X0, 12);
	EXPECT_FLOAT_EQ(Vertical.m_Y0, 10);
	EXPECT_FLOAT_EQ(Vertical.m_X3, 8);
	EXPECT_FLOAT_EQ(Vertical.m_Y3, 0);
}

TEST(QmWeaponTrajectoryGeometry, NonFiniteSegmentRejectsPreviouslyPreparedQuads)
{
	SQmWeaponTrajectoryScratch Scratch;
	EXPECT_FALSE(Scratch.TryPrepare([&] {
		Scratch.m_vLineSegments.emplace_back(vec2(0, 0), vec2(10, 0));
		Scratch.m_vLineSegments.emplace_back(vec2(10, 0), vec2(std::numeric_limits<float>::infinity(), 0));
		return Scratch.PrepareQuads(2);
	}));
	EXPECT_TRUE(Scratch.m_vLineSegments.empty());
	EXPECT_TRUE(Scratch.m_vLineQuadSegments.empty());
}

TEST(QmWeaponTrajectoryGeometry, NegativeBounceCountsDoNotBecomeUnsignedCapacities)
{
	for(float Bounces : {-1000.0f, -2.0f, -1.5f, -1.0f, 0.0f, 1.25f})
	{
		SQmWeaponTrajectoryScratch Scratch;
		ASSERT_TRUE(Scratch.TryPrepare([&] { return Scratch.ReserveBounces(Bounces); }));
		if(Bounces <= -1.5f)
			EXPECT_EQ(Scratch.m_vLineSegments.capacity(), 0u);
		if(Bounces == -1.0f)
			EXPECT_GE(Scratch.m_vLineSegments.capacity(), 1u);
		if(Bounces == 1.25f)
			EXPECT_GE(Scratch.m_vLineSegments.capacity(), 3u);
	}
}

TEST(QmWeaponTrajectoryGeometry, InvalidCapacityHintRejectsThePreparationWithoutPublishingPoints)
{
	for(float Bounces : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max()})
	{
		SQmWeaponTrajectoryScratch Scratch;
		EXPECT_FALSE(Scratch.TryPrepare([&] {
			Scratch.m_vPoints.emplace_back(1, 2);
			return Scratch.ReserveBounces(Bounces);
		}));
		EXPECT_TRUE(Scratch.m_vPoints.empty());
		EXPECT_TRUE(Scratch.m_vLineSegments.empty());
	}
}

TEST(QmWeaponTrajectoryGeometry, BoxProbeChecksTheActualFloatCornersBeforeIntegerRounding)
{
	EXPECT_TRUE(QmWeaponTrajectoryCanTestBox(vec2(128, 256)));
	EXPECT_TRUE(QmWeaponTrajectoryCanTestBox(vec2(-1000, -1000)));
	EXPECT_TRUE(QmWeaponTrajectoryCanTestBox(vec2(static_cast<float>(std::numeric_limits<int>::min()), 0)));
	EXPECT_FALSE(QmWeaponTrajectoryCanTestBox(vec2(static_cast<float>(std::numeric_limits<int>::max()), 0)));
	EXPECT_FALSE(QmWeaponTrajectoryCanTestBox(vec2(0, std::numeric_limits<float>::quiet_NaN())));
	EXPECT_FALSE(QmWeaponTrajectoryCanTestBox(vec2(std::numeric_limits<float>::infinity(), 0)));
}
