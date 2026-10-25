#include <game/client/components/particles.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmParticlePosition, OrdinaryAndOffMapCoordinatesCanCollide)
{
	EXPECT_TRUE(CParticle::CanCollideAt(vec2(0.0f, 0.0f)));
	EXPECT_TRUE(CParticle::CanCollideAt(vec2(-0.75f, 32.5f)));
	EXPECT_TRUE(CParticle::CanCollideAt(vec2(-100000.0f, 100000.0f)));
}

TEST(QmParticlePosition, KeepsExactlyRepresentableIntLowerBoundary)
{
	EXPECT_TRUE(CParticle::CanCollideAt(vec2(-2147483648.0f, 0.0f)));
	EXPECT_FALSE(CParticle::CanCollideAt(vec2(std::nextafter(-2147483648.0f, -std::numeric_limits<float>::infinity()), 0.0f)));
}

TEST(QmParticlePosition, RejectsIntMaximumRoundedUpToFloat)
{
	EXPECT_FALSE(CParticle::CanCollideAt(vec2(static_cast<float>(std::numeric_limits<int>::max()), 0.0f)));
	EXPECT_TRUE(CParticle::CanCollideAt(vec2(std::nextafter(2147483648.0f, 0.0f), 0.0f)));
	EXPECT_FALSE(CParticle::CanCollideAt(vec2(0.0f, 2147483648.0f)));
}

TEST(QmParticlePosition, RejectsNonFiniteCollisionCoordinates)
{
	for(float Value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		EXPECT_FALSE(CParticle::CanCollideAt(vec2(Value, 0.0f)));
		EXPECT_FALSE(CParticle::CanCollideAt(vec2(0.0f, Value)));
	}
}

TEST(QmParticleMovement, ValidationDoesNotChangeOrdinaryTrajectory)
{
	CParticle Particle;
	Particle.SetDefault();
	Particle.m_Pos = vec2(10.25f, -4.5f);
	Particle.m_Vel = vec2(20.0f, 30.0f);
	EXPECT_TRUE(Particle.CanMove(vec2(2.0f, 3.0f)));
	EXPECT_EQ(Particle.m_Pos, vec2(10.25f, -4.5f));
	EXPECT_EQ(Particle.m_Vel, vec2(20.0f, 30.0f));
}

TEST(QmParticleMovement, RejectsInvalidCurrentPointEvenWhenEndpointIsValid)
{
	CParticle Particle;
	Particle.SetDefault();
	Particle.m_Pos = vec2(2147483648.0f, 0.0f);
	EXPECT_FALSE(Particle.CanMove(vec2(-2147483648.0f, 0.0f)));
}

TEST(QmParticleMovement, RejectsEndpointOutsideCollisionIntegerRange)
{
	CParticle Particle;
	Particle.SetDefault();
	Particle.m_Pos = vec2(std::nextafter(2147483648.0f, 0.0f), 0.0f);
	EXPECT_FALSE(Particle.CanMove(vec2(256.0f, 0.0f)));
	EXPECT_TRUE(Particle.CanMove(vec2(-256.0f, 0.0f)));
}

TEST(QmParticleMovement, OffMapMotionInsideNumericDomainIsKept)
{
	CParticle Particle;
	Particle.SetDefault();
	Particle.m_Pos = vec2(-100000.0f, 100000.0f);
	EXPECT_TRUE(Particle.CanMove(vec2(-50.0f, 50.0f)));
}

TEST(QmParticleMovement, RejectsOverflowedEndpointWithoutCollision)
{
	CParticle Particle;
	Particle.SetDefault();
	Particle.m_Collides = false;
	Particle.m_Pos = vec2(std::numeric_limits<float>::max(), 0.0f);
	EXPECT_FALSE(Particle.CanMove(vec2(std::numeric_limits<float>::max(), 0.0f)));
}

TEST(QmParticleMovement, RejectsNonFiniteDisplacement)
{
	CParticle Particle;
	Particle.SetDefault();
	for(float Value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		EXPECT_FALSE(Particle.CanMove(vec2(Value, 0.0f)));
		EXPECT_FALSE(Particle.CanMove(vec2(0.0f, Value)));
	}
}

TEST(QmParticleMovement, NonCollidingParticleKeepsLargeFiniteCoordinates)
{
	CParticle Particle;
	Particle.SetDefault();
	Particle.m_Collides = false;
	Particle.m_Pos = vec2(4294967296.0f, -4294967296.0f);
	EXPECT_TRUE(Particle.CanMove(vec2(0.0f, 0.0f)));
	EXPECT_TRUE(Particle.CanMove(vec2(1024.0f, -1024.0f)));
}

TEST(QmParticleMovement, NonCollidingParticleStillRejectsNonFiniteState)
{
	CParticle Particle;
	Particle.SetDefault();
	Particle.m_Collides = false;
	Particle.m_Pos = vec2(std::numeric_limits<float>::quiet_NaN(), 0.0f);
	EXPECT_FALSE(Particle.CanMove(vec2(0.0f, 0.0f)));
}
