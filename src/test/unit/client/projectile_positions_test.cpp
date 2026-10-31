#include <game/client/projectile_data.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
	CProjectileData Projectile(vec2 Position, vec2 Velocity = vec2(1, 0))
	{
		CProjectileData Data{};
		Data.m_StartPos = Position;
		Data.m_StartVel = Velocity;
		return Data;
	}
}

TEST(ProjectilePositions, OrdinaryAndOffMapStartsAreAccepted)
{
	EXPECT_TRUE(Projectile(vec2(0.25f, -32.5f)).HasValidStart());
	EXPECT_TRUE(Projectile(vec2(-100000, 100000), vec2(20, -30)).HasValidStart());
}

TEST(ProjectilePositions, RoundedUpMaximumIsRejectedOnBothAxes)
{
	const float Maximum = static_cast<float>(std::numeric_limits<int>::max());
	EXPECT_FALSE(Projectile(vec2(Maximum, 0)).HasValidStart());
	EXPECT_FALSE(Projectile(vec2(0, Maximum)).HasValidStart());
}

TEST(ProjectilePositions, ExactLowerAndNearestUpperPositionAreAccepted)
{
	EXPECT_TRUE(Projectile(vec2(static_cast<float>(std::numeric_limits<int>::min()), std::nextafter(2147483648.0f, 0.0f))).HasValidStart());
}

TEST(ProjectilePositions, PositionBelowLowerBoundaryIsRejected)
{
	const float Value = std::nextafter(-2147483648.0f, -std::numeric_limits<float>::infinity());
	EXPECT_FALSE(Projectile(vec2(Value, 0)).HasValidStart());
}

TEST(ProjectilePositions, NonFinitePositionIsRejected)
{
	for(float Value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Value);
		EXPECT_FALSE(Projectile(vec2(Value, 0)).HasValidStart());
		EXPECT_FALSE(Projectile(vec2(0, Value)).HasValidStart());
	}
}

TEST(ProjectilePositions, NonFiniteVelocityIsRejected)
{
	for(float Value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Value);
		EXPECT_FALSE(Projectile(vec2(10, 20), vec2(Value, 0)).HasValidStart());
		EXPECT_FALSE(Projectile(vec2(10, 20), vec2(0, Value)).HasValidStart());
	}
}

TEST(ProjectilePositions, ZeroVelocityRemainsValid)
{
	EXPECT_TRUE(Projectile(vec2(10, 20), vec2(0, 0)).HasValidStart());
}

TEST(ProjectilePositions, ValidationDoesNotChangeStartOrMetadata)
{
	auto Data = Projectile(vec2(-5.5f, 20.25f), vec2(0.5f, -0.75f));
	Data.m_StartTick = 40;
	Data.m_TuneZone = 7;
	Data.m_Owner = 3;
	ASSERT_TRUE(Data.HasValidStart());
	EXPECT_EQ(Data.m_StartPos, vec2(-5.5f, 20.25f));
	EXPECT_EQ(Data.m_StartVel, vec2(0.5f, -0.75f));
	EXPECT_EQ(Data.m_StartTick, 40);
	EXPECT_EQ(Data.m_TuneZone, 7);
	EXPECT_EQ(Data.m_Owner, 3);
}

TEST(ProjectilePositions, ComputedPairKeepsOrdinaryTrajectory)
{
	const auto Data = Projectile(vec2(10, 20));
	vec2 From, To;
	ASSERT_TRUE(Data.TryCalcPositions(0, 100, 0.5f, 0.25f, From, To));
	EXPECT_EQ(From, vec2(60, 20));
	EXPECT_EQ(To, vec2(35, 20));
}

TEST(ProjectilePositions, InvalidSecondComputedPointKeepsBothPreviousOutputs)
{
	const auto Data = Projectile(vec2(10, 20));
	vec2 From(1, 2), To(3, 4);
	EXPECT_FALSE(Data.TryCalcPositions(0, 100, 0, 100000000.0f, From, To));
	EXPECT_EQ(From, vec2(1, 2));
	EXPECT_EQ(To, vec2(3, 4));
}

TEST(ProjectilePositions, NonFiniteTrajectoryParametersDoNotPublishResults)
{
	const auto Data = Projectile(vec2(10, 20));
	vec2 From(1, 2), To(3, 4);
	const float Invalid = std::numeric_limits<float>::quiet_NaN();
	EXPECT_FALSE(Data.TryCalcPositions(Invalid, 100, 0, 1, From, To));
	EXPECT_FALSE(Data.TryCalcPositions(0, Invalid, 0, 1, From, To));
	EXPECT_FALSE(Data.TryCalcPositions(0, 100, Invalid, 1, From, To));
	EXPECT_FALSE(Data.TryCalcPositions(0, 100, 0, Invalid, From, To));
	EXPECT_EQ(From, vec2(1, 2));
	EXPECT_EQ(To, vec2(3, 4));
}
