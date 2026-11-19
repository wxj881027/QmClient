#include <game/client/prediction/entities/plasma.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

TEST(PlasmaMovement, OrdinaryMoveKeepsPositionAndAcceleration)
{
	vec2 Position(10, 20), Velocity(2, -4);
	ASSERT_TRUE(CPlasma::TryAdvance(Position, Velocity));
	EXPECT_EQ(Position, vec2(12, 16));
	EXPECT_EQ(Velocity, vec2(2.2f, -4.4f));
}

TEST(PlasmaMovement, ZeroVelocityRemainsStationary)
{
	vec2 Position(10, 20), Velocity(0, 0);
	ASSERT_TRUE(CPlasma::TryAdvance(Position, Velocity));
	EXPECT_EQ(Position, vec2(10, 20));
	EXPECT_EQ(Velocity, vec2(0, 0));
}

TEST(PlasmaMovement, InvalidCurrentPositionDoesNotCommitMovement)
{
	vec2 Position(2147483648.0f, 0), Velocity(-256, 0);
	EXPECT_FALSE(CPlasma::TryAdvance(Position, Velocity));
	EXPECT_EQ(Position, vec2(2147483648.0f, 0));
	EXPECT_EQ(Velocity, vec2(-256, 0));
}

TEST(PlasmaMovement, InvalidNextPositionKeepsCurrentState)
{
	const vec2 Before(std::nextafter(2147483648.0f, 0.0f), 0);
	vec2 Position = Before, Velocity(256, 0);
	EXPECT_FALSE(CPlasma::TryAdvance(Position, Velocity));
	EXPECT_EQ(Position, Before);
	EXPECT_EQ(Velocity, vec2(256, 0));
}

TEST(PlasmaMovement, InvalidNextQueryEndpointRejectsOtherwiseSafeStep)
{
	vec2 Position(1073741824.0f, 0), Velocity(536870912.0f, 0);
	EXPECT_FALSE(CPlasma::TryAdvance(Position, Velocity));
	EXPECT_EQ(Position, vec2(1073741824.0f, 0));
	EXPECT_EQ(Velocity, vec2(536870912.0f, 0));
}

TEST(PlasmaMovement, NonFiniteVelocityDoesNotCommitPosition)
{
	for(float Value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Value);
		vec2 Position(10, 20), Velocity(Value, 2);
		EXPECT_FALSE(CPlasma::TryAdvance(Position, Velocity));
		EXPECT_EQ(Position, vec2(10, 20));
		EXPECT_EQ(Velocity.y, 2.0f);
		if(std::isnan(Value))
			EXPECT_TRUE(std::isnan(Velocity.x));
		else
			EXPECT_EQ(Velocity.x, Value);
	}
}

TEST(PlasmaMovement, OffMapStepInsideNumericDomainIsKept)
{
	vec2 Position(-1000, -2000), Velocity(2, 3);
	ASSERT_TRUE(CPlasma::TryAdvance(Position, Velocity));
	EXPECT_EQ(Position, vec2(-998, -1997));
	EXPECT_FLOAT_EQ(Velocity.x, 2.2f);
	EXPECT_FLOAT_EQ(Velocity.y, 3.3f);
}
