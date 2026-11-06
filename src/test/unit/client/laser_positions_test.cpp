#include <game/client/laser_data.h>

#include <gtest/gtest.h>

#include <cmath>
#include <iterator>
#include <limits>

namespace
{
	CLaserData Laser(vec2 From, vec2 To)
	{
		CLaserData Data{};
		Data.m_From = From;
		Data.m_To = To;
		return Data;
	}
}

TEST(LaserPositions, OrdinaryAndOffMapEndpointsAreAccepted)
{
	EXPECT_TRUE(Laser(vec2(0.25f, -32.5f), vec2(64.0f, 128.0f)).HasValidPositions());
	EXPECT_TRUE(Laser(vec2(-100000.0f, 100000.0f), vec2(100000.0f, -100000.0f)).HasValidPositions());
}

TEST(LaserPositions, ExactIntLowerBoundaryRemainsAccepted)
{
	const float Minimum = static_cast<float>(std::numeric_limits<int>::min());
	EXPECT_TRUE(Laser(vec2(Minimum, Minimum), vec2(Minimum, Minimum)).HasValidPositions());
}

TEST(LaserPositions, RoundedUpIntMaximumIsRejectedOnEveryEndpointAxis)
{
	const float Maximum = static_cast<float>(std::numeric_limits<int>::max());
	const CLaserData aCases[] = {
		Laser(vec2(Maximum, 0), vec2(0, 0)),
		Laser(vec2(0, Maximum), vec2(0, 0)),
		Laser(vec2(0, 0), vec2(Maximum, 0)),
		Laser(vec2(0, 0), vec2(0, Maximum)),
	};
	for(size_t i = 0; i < std::size(aCases); ++i)
	{
		SCOPED_TRACE(i);
		EXPECT_FALSE(aCases[i].HasValidPositions());
	}
}

TEST(LaserPositions, NearestFloatBelowUpperBoundaryRemainsAccepted)
{
	const float Maximum = std::nextafter(static_cast<float>(std::numeric_limits<int>::max()), 0.0f);
	EXPECT_TRUE(Laser(vec2(Maximum, Maximum), vec2(Maximum, Maximum)).HasValidPositions());
}

TEST(LaserPositions, FloatBelowIntMinimumIsRejected)
{
	const float TooLow = std::nextafter(static_cast<float>(std::numeric_limits<int>::min()), -std::numeric_limits<float>::infinity());
	EXPECT_FALSE(Laser(vec2(TooLow, 0), vec2(0, 0)).HasValidPositions());
	EXPECT_FALSE(Laser(vec2(0, 0), vec2(0, TooLow)).HasValidPositions());
}

TEST(LaserPositions, NonFiniteEndpointCoordinatesAreRejected)
{
	for(float Value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Value);
		EXPECT_FALSE(Laser(vec2(Value, 0), vec2(0, 0)).HasValidPositions());
		EXPECT_FALSE(Laser(vec2(0, Value), vec2(0, 0)).HasValidPositions());
		EXPECT_FALSE(Laser(vec2(0, 0), vec2(Value, 0)).HasValidPositions());
		EXPECT_FALSE(Laser(vec2(0, 0), vec2(0, Value)).HasValidPositions());
	}
}

TEST(LaserPositions, ValidationDoesNotRewritePositionsOrMetadata)
{
	auto Data = Laser(vec2(-4.5f, 12.25f), vec2(99.5f, -2.0f));
	Data.m_StartTick = 40;
	Data.m_Owner = 3;
	Data.m_TuneZone = 2;
	Data.m_Predict = true;
	ASSERT_TRUE(Data.HasValidPositions());
	EXPECT_EQ(Data.m_From, vec2(-4.5f, 12.25f));
	EXPECT_EQ(Data.m_To, vec2(99.5f, -2.0f));
	EXPECT_EQ(Data.m_StartTick, 40);
	EXPECT_EQ(Data.m_Owner, 3);
	EXPECT_EQ(Data.m_TuneZone, 2);
	EXPECT_TRUE(Data.m_Predict);
}

TEST(LaserPositions, ZeroLengthLaserIsStillAValidPositionPair)
{
	EXPECT_TRUE(Laser(vec2(32, 64), vec2(32, 64)).HasValidPositions());
}

TEST(LaserPositions, NoPredictFlagDoesNotMakeValidGeometryInvalid)
{
	auto Data = Laser(vec2(32, 64), vec2(96, 64));
	Data.m_Predict = false;
	EXPECT_TRUE(Data.HasValidPositions());
	EXPECT_FALSE(Data.m_Predict);
}
