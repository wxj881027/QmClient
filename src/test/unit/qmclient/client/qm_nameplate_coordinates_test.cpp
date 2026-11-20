#include <game/client/components/qmclient/nameplate_coordinates.h>

#include <gtest/gtest.h>

#include <array>

TEST(QmNameplateCoordinates, NormalValuesKeepTheExistingFloatRounding)
{
	struct SCase
	{
		float m_Tiles;
		int64_t m_Expected;
	};
	const std::array<SCase, 8> aCases{{{0.0f, 0}, {0.004f, 0}, {0.005f, 1}, {-0.005f, -1},
		{12.345f, 1235}, {-12.345f, -1235}, {83886.09f, 8388610}, {-83886.09f, -8388610}}};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_Tiles);
		const auto Result = QmNameplateCentitiles(Case.m_Tiles);
		ASSERT_TRUE(Result.has_value());
		EXPECT_EQ(*Result, Case.m_Expected);
	}
}

TEST(QmNameplateCoordinates, DisplayUsesTheExistingNormalizedFloatValue)
{
	const auto Positive = QmNameplateDisplayCoordinate(12.345f);
	const auto Negative = QmNameplateDisplayCoordinate(-12.345f);
	ASSERT_TRUE(Positive);
	ASSERT_TRUE(Negative);
	EXPECT_FLOAT_EQ(*Positive, 12.35f);
	EXPECT_FLOAT_EQ(*Negative, -12.35f);
}

TEST(QmNameplateCoordinates, CoordinatesBeyondTheOldIntRangeRemainRepresentable)
{
	const auto Positive = QmNameplateCentitiles(67108864.0f);
	const auto Negative = QmNameplateCentitiles(-67108864.0f);
	ASSERT_TRUE(Positive);
	ASSERT_TRUE(Negative);
	EXPECT_EQ(*Positive, INT64_C(6710886400));
	EXPECT_EQ(*Negative, -INT64_C(6710886400));
}

TEST(QmNameplateCoordinates, NonFiniteCoordinatesHaveNoIntegerResult)
{
	EXPECT_FALSE(QmNameplateCentitiles(std::numeric_limits<float>::quiet_NaN()));
	EXPECT_FALSE(QmNameplateCentitiles(std::numeric_limits<float>::infinity()));
	EXPECT_FALSE(QmNameplateCentitiles(-std::numeric_limits<float>::infinity()));
}

TEST(QmNameplateCoordinates, FiniteValuesOutsideInt64AreRejectedBeforeConversion)
{
	EXPECT_FALSE(QmNameplateCentitiles(std::numeric_limits<float>::max()));
	EXPECT_FALSE(QmNameplateCentitiles(-std::numeric_limits<float>::max()));
	EXPECT_FALSE(QmNameplateCentitiles(1.0e18f));
}

TEST(QmNameplateCoordinates, Int64BoundaryIsInclusiveOnlyAtItsNegativeEnd)
{
	const float BoundaryTiles = 0x1p63f / 100.0f;
	EXPECT_FALSE(QmNameplateCentitiles(BoundaryTiles));
	EXPECT_TRUE(QmNameplateCentitiles(std::nextafter(BoundaryTiles, 0.0f)));
	const auto Minimum = QmNameplateCentitiles(-BoundaryTiles);
	ASSERT_TRUE(Minimum);
	EXPECT_EQ(*Minimum, std::numeric_limits<int64_t>::min());
	EXPECT_FALSE(QmNameplateCentitiles(std::nextafter(-BoundaryTiles, -std::numeric_limits<float>::infinity())));
}

TEST(QmNameplateCoordinates, InvalidCoordinatesDoNotProduceADisplayValue)
{
	EXPECT_FALSE(QmNameplateDisplayCoordinate(std::numeric_limits<float>::quiet_NaN()));
	EXPECT_FALSE(QmNameplateDisplayCoordinate(std::numeric_limits<float>::max()));
}

TEST(QmNameplateCoordinates, DistanceCanExceedTheEntireSignedIntRange)
{
	EXPECT_EQ(QmNameplateCoordinateDistance(INT64_C(2147483647), -INT64_C(2147483648)), UINT64_C(4294967295));
	EXPECT_EQ(QmNameplateCoordinateDistance(-INT64_C(2147483648), INT64_C(2147483647)), UINT64_C(4294967295));
}

TEST(QmNameplateCoordinates, DistanceIsDefinedAcrossTheFullSignedInt64Domain)
{
	const int64_t Low = std::numeric_limits<int64_t>::min(), High = std::numeric_limits<int64_t>::max();
	EXPECT_EQ(QmNameplateCoordinateDistance(Low, High), std::numeric_limits<uint64_t>::max());
	EXPECT_EQ(QmNameplateCoordinateDistance(High, Low), std::numeric_limits<uint64_t>::max());
	EXPECT_EQ(QmNameplateCoordinateDistance(Low, 0), UINT64_C(9223372036854775808));
}

TEST(QmNameplateCoordinates, OrdinaryAlignmentDistancesRemainSymmetric)
{
	EXPECT_EQ(QmNameplateCoordinateDistance(-1, 2), 3u);
	EXPECT_EQ(QmNameplateCoordinateDistance(2, -1), 3u);
	EXPECT_EQ(QmNameplateCoordinateDistance(-1, -1), 0u);
}
