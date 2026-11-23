#include <game/client/components/qmclient/practice_light_phase.h>

#include <gtest/gtest.h>

#include <cmath>

TEST(PracticeLightPhase, ZeroTickKeepsMapPhase)
{
	const qm_practice_light::SPhase Phase = qm_practice_light::After(0.5f, 40, 3, 100, 0.1f, 6, 0);
	EXPECT_FLOAT_EQ(Phase.m_Rotation, 0.5f);
	EXPECT_EQ(Phase.m_CurveLength, 40);
	EXPECT_EQ(Phase.m_Speed, 3);
	EXPECT_EQ(qm_practice_light::LastStepTick(6, 0), 0);
}

TEST(PracticeLightPhase, BackwardSeekMatchesForwardSimulation)
{
	const qm_practice_light::SPhase Forward = qm_practice_light::After(1.0f, 10, 4, 80, 0.03f, 6, 600);
	qm_practice_light::SPhase Incremental{1.0f, 10, 0, 4};
	for(int Tick = 6; Tick <= 600; Tick += 6)
		qm_practice_light::Step(Incremental, 80, 0.03f, 6);
	EXPECT_FLOAT_EQ(Forward.m_Rotation, Incremental.m_Rotation);
	EXPECT_EQ(Forward.m_CurveLength, Incremental.m_CurveLength);
	EXPECT_EQ(Forward.m_LengthL, Incremental.m_LengthL);
	EXPECT_EQ(Forward.m_Speed, Incremental.m_Speed);

	const qm_practice_light::SPhase Rewound = qm_practice_light::After(1.0f, 10, 4, 80, 0.03f, 6, 120);
	qm_practice_light::SPhase Continued = Rewound;
	for(int Tick = 126; Tick <= 600; Tick += 6)
		qm_practice_light::Step(Continued, 80, 0.03f, 6);
	EXPECT_FLOAT_EQ(Forward.m_Rotation, Continued.m_Rotation);
	EXPECT_EQ(Forward.m_CurveLength, Continued.m_CurveLength);
}

TEST(PracticeLightPhase, CurveBouncesAndCarriesOverflow)
{
	qm_practice_light::SPhase Phase{0.0f, 90, 0, 5};
	qm_practice_light::Step(Phase, 100, 0.0f, 6);
	EXPECT_EQ(Phase.m_CurveLength, 100);
	EXPECT_EQ(Phase.m_LengthL, 20);
	EXPECT_EQ(Phase.m_Speed, 5);
	qm_practice_light::Step(Phase, 100, 0.0f, 6);
	EXPECT_EQ(Phase.m_CurveLength, 90);
	EXPECT_EQ(Phase.m_LengthL, 0);
	EXPECT_EQ(Phase.m_Speed, -5);
	qm_practice_light::Step(Phase, 100, 0.0f, 6);
	EXPECT_EQ(Phase.m_CurveLength, 60);
}

TEST(PracticeLightPhase, RotationWrapsAtTwoPi)
{
	qm_practice_light::SPhase Phase{2.0f * pi - 0.1f, 0, 0, 0};
	qm_practice_light::Step(Phase, 100, 0.05f, 6);
	EXPECT_LT(Phase.m_Rotation, 2.0f * pi);
	EXPECT_GT(Phase.m_Rotation, 0.0f);
	EXPECT_NEAR(Phase.m_Rotation, 2.0f * pi - 0.1f + 0.3f - 2.0f * pi, 1e-5f);
}
