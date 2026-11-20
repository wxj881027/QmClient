#include <generated/protocol.h>

#include <game/client/pickup_data.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
	CPickupData Pickup(vec2 Position)
	{
		CPickupData Data{};
		Data.m_Pos = Position;
		return Data;
	}
}

TEST(PickupPositions, OrdinaryAndOffMapPositionsAreAccepted)
{
	EXPECT_TRUE(Pickup(vec2(0.25f, -32.5f)).HasValidPosition());
	EXPECT_TRUE(Pickup(vec2(-100000, 100000)).HasValidPosition());
}

TEST(PickupPositions, RoundedUpMaximumIsRejectedOnBothAxes)
{
	const float Maximum = static_cast<float>(std::numeric_limits<int>::max());
	EXPECT_FALSE(Pickup(vec2(Maximum, 0)).HasValidPosition());
	EXPECT_FALSE(Pickup(vec2(0, Maximum)).HasValidPosition());
}

TEST(PickupPositions, ExactLowerAndNearestUpperPositionAreAccepted)
{
	EXPECT_TRUE(Pickup(vec2(-2147483648.0f, std::nextafter(2147483648.0f, 0.0f))).HasValidPosition());
}

TEST(PickupPositions, NonFinitePositionIsRejected)
{
	for(float Value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Value);
		EXPECT_FALSE(Pickup(vec2(Value, 0)).HasValidPosition());
		EXPECT_FALSE(Pickup(vec2(0, Value)).HasValidPosition());
	}
}

TEST(PickupPositions, SerializationKeepsOriginalTruncationAndBasicFields)
{
	auto Data = Pickup(vec2(-3.75f, 2.5f));
	Data.m_Type = 2;
	Data.m_Subtype = 3;
	CNetObj_Pickup Out{};
	ASSERT_TRUE(Data.TryFillNetObj(Out));
	EXPECT_EQ(Out.m_X, -3);
	EXPECT_EQ(Out.m_Y, 2);
	EXPECT_EQ(Out.m_Type, 2);
	EXPECT_EQ(Out.m_Subtype, 3);
}

TEST(PickupPositions, FailedSerializationKeepsPreviousOutput)
{
	const auto Data = Pickup(vec2(2147483648.0f, 0));
	CNetObj_Pickup Out{};
	Out.m_X = 10;
	Out.m_Y = 20;
	Out.m_Type = 2;
	Out.m_Subtype = 3;
	EXPECT_FALSE(Data.TryFillNetObj(Out));
	EXPECT_EQ(Out.m_X, 10);
	EXPECT_EQ(Out.m_Y, 20);
	EXPECT_EQ(Out.m_Type, 2);
	EXPECT_EQ(Out.m_Subtype, 3);
}

TEST(PickupPositions, BoundarySerializationRemainsRepresentable)
{
	const auto Data = Pickup(vec2(-2147483648.0f, std::nextafter(2147483648.0f, 0.0f)));
	CNetObj_Pickup Out{};
	ASSERT_TRUE(Data.TryFillNetObj(Out));
	EXPECT_EQ(Out.m_X, std::numeric_limits<int>::min());
	EXPECT_EQ(Out.m_Y, 2147483520);
}

TEST(PickupPositions, ValidationDoesNotChangePositionOrFlags)
{
	auto Data = Pickup(vec2(-20.5f, 13.25f));
	Data.m_Flags = PICKUPFLAG_NO_PREDICT;
	Data.m_SwitchNumber = 4;
	EXPECT_TRUE(Data.HasValidPosition());
	EXPECT_EQ(Data.m_Pos, vec2(-20.5f, 13.25f));
	EXPECT_EQ(Data.m_Flags, PICKUPFLAG_NO_PREDICT);
	EXPECT_EQ(Data.m_SwitchNumber, 4);
}
