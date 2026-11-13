#include <game/client/prediction/entities/door.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <utility>

TEST(PredictionDoorGeometry, OrdinarySnapshotKeepsDirectionAndRoundedLength)
{
	const auto Geometry = CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(200, 0), -1, -1, ivec2(20, 20));
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_Direction, vec2(1, 0));
	EXPECT_EQ(Geometry->m_Length, 200);
}

TEST(PredictionDoorGeometry, FractionalEndpointsUseNormalizedSnapshotDirection)
{
	const auto Geometry = CDoor::CalcCollisionGeometry(vec2(1.25f, -2.5f), vec2(4.25f, 1.5f), -1, -1, ivec2(20, 20));
	ASSERT_TRUE(Geometry);
	EXPECT_NEAR(Geometry->m_Direction.x, 0.6f, 0.00001f);
	EXPECT_NEAR(Geometry->m_Direction.y, 0.8f, 0.00001f);
	EXPECT_EQ(Geometry->m_Length, 5);
}

TEST(PredictionDoorGeometry, PositiveSubpixelLineKeepsExistingZeroRoundedLength)
{
	const auto Geometry = CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(0.1f, 0), -1, -1, ivec2(20, 20));
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_Length, 0);
}

TEST(PredictionDoorGeometry, SnapshotLengthKeepsHalfUnitRounding)
{
	for(const auto &[Length, Expected] : {std::pair{0.49f, 0}, {0.5f, 1}, {1.49f, 1}, {1.5f, 2}})
	{
		SCOPED_TRACE(Length);
		const auto Geometry = CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(Length, 0), -1, -1, ivec2(20, 20));
		ASSERT_TRUE(Geometry);
		EXPECT_EQ(Geometry->m_Length, Expected);
	}
}

TEST(PredictionDoorGeometry, MapConfiguredLengthRestoresShortenedSnapshot)
{
	const auto Geometry = CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(32, 0), 2, 96, ivec2(20, 20));
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_Length, 96);
	EXPECT_NEAR(Geometry->m_Direction.x, 1.0f, 0.00001f);
	EXPECT_NEAR(Geometry->m_Direction.y, 0.0f, 0.00001f);
}

TEST(PredictionDoorGeometry, MapConfiguredDiagonalKeepsTileDirection)
{
	const auto Geometry = CDoor::CalcCollisionGeometry(vec2(100, 100), vec2(116, 116), 1, 288, ivec2(100, 100));
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_Length, 288);
	EXPECT_NEAR(Geometry->m_Direction.x, 0.70710678f, 0.00001f);
	EXPECT_NEAR(Geometry->m_Direction.y, 0.70710678f, 0.00001f);
}

TEST(PredictionDoorGeometry, EmptyOrNegativeMapExtentIsRejected)
{
	for(const ivec2 Size : {ivec2(0, 1), ivec2(1, 0), ivec2(-1, 1), ivec2(1, -1)})
		EXPECT_FALSE(CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(32, 0), 2, 96, Size));
}

TEST(PredictionDoorGeometry, ZeroLengthSnapshotIsRejected)
{
	EXPECT_FALSE(CDoor::CalcCollisionGeometry(vec2(5, 5), vec2(5, 5), -1, -1, ivec2(20, 20)));
}

TEST(PredictionDoorGeometry, NonfiniteAndUnrepresentableEndpointsAreRejected)
{
	const float Nan = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();
	for(const vec2 Invalid : {vec2(Nan, 0), vec2(0, Infinity), vec2(-Infinity, 0), vec2(2147483648.0f, 0)})
	{
		EXPECT_FALSE(CDoor::CalcCollisionGeometry(Invalid, vec2(0, 0), -1, -1, ivec2(20, 20)));
		EXPECT_FALSE(CDoor::CalcCollisionGeometry(vec2(0, 0), Invalid, -1, -1, ivec2(20, 20)));
	}
}

TEST(PredictionDoorGeometry, FallbackLengthMustFitIntEvenWithLargePerimeter)
{
	// 只准备数学输入，不加载或分配这张大地图，也不执行长遍历。
	EXPECT_FALSE(CDoor::CalcCollisionGeometry(vec2(-1073741824.0f, 0), vec2(1073741824.0f, 0), -1, -1, ivec2(100000000, 1)));
}

TEST(PredictionDoorGeometry, RoundedLastSampleCannotEscapeRepresentableEndpointInputs)
{
	EXPECT_FALSE(CDoor::CalcCollisionGeometry(vec2(192, 0), vec2(2147483520.0f, 0), -1, -1, ivec2(100000000, 1)));
}

TEST(PredictionDoorGeometry, RestoredMapLengthCannotExtendSamplesBeyondIntRange)
{
	EXPECT_FALSE(CDoor::CalcCollisionGeometry(vec2(2147483392.0f, 0), vec2(2147483520.0f, 0), 2, 288, ivec2(100000000, 1)));
}

TEST(PredictionDoorGeometry, OnlyActualSamplesConstrainRestoredEndpoint)
{
	// 原循环末个 i 为 length-2；不因从未写入的完整延长端点拒绝安全采样。
	const auto Geometry = CDoor::CalcCollisionGeometry(vec2(2147483392.0f, 0), vec2(2147483520.0f, 0), 2, 192, ivec2(100000000, 1));
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_Length, 192);
}

TEST(PredictionDoorGeometry, LargeNumericallyValidLengthDoesNotInvokeTraversal)
{
	const auto Geometry = CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(2147483520.0f, 0), -1, -1, ivec2(100000000, 1));
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_Length, 2147483520);
}

TEST(PredictionDoorGeometry, InvalidConfiguredSideOrLengthIsRejected)
{
	EXPECT_FALSE(CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(32, 0), -1, 96, ivec2(20, 20)));
	EXPECT_FALSE(CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(32, 0), 8, 96, ivec2(20, 20)));
	EXPECT_FALSE(CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(32, 0), 2, 0, ivec2(20, 20)));
	EXPECT_FALSE(CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(32, 0), 2, -2, ivec2(20, 20)));
}

TEST(PredictionDoorGeometry, MinimumRepresentableOriginStillSupportsLocalLine)
{
	const auto Geometry = CDoor::CalcCollisionGeometry(vec2(-2147483648.0f, 0), vec2(-2147483520.0f, 0), -1, -1, ivec2(20, 20));
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_Length, 128);
	EXPECT_EQ(Geometry->m_Direction, vec2(1, 0));
}

TEST(PredictionDoorGeometry, MapPerimeterSumDoesNotOverflowBeforeFloatConversion)
{
	// 此用例只验证正整数尺寸的算术域，不声称这些尺寸可通过地图加载预算。
	const auto Geometry = CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(64, 0), -1, -1, ivec2(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_Length, 64);
}

TEST(PredictionDoorGeometry, ExistingMapPerimeterLimitIsPreserved)
{
	EXPECT_TRUE(CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(480, 0), -1, -1, ivec2(10, 5)));
	EXPECT_FALSE(CDoor::CalcCollisionGeometry(vec2(0, 0), vec2(481, 0), -1, -1, ivec2(10, 5)));
}
