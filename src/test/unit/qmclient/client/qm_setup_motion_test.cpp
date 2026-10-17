#include <gtest/gtest.h>
#include <qm/setup/setup_motion.h>

TEST(QmSetupMotion, FinishesAtTheTargetAndStopsAnimating)
{
	QmSetup::CTransition Transition;
	Transition.SetTarget(1.0f, true);
	Transition.Advance(0.12f);
	EXPECT_GT(Transition.Value(), 0.0f);
	EXPECT_LT(Transition.Value(), 1.0f);
	EXPECT_TRUE(Transition.Active());
	Transition.Advance(1.0f);
	EXPECT_FLOAT_EQ(Transition.Value(), 1.0f);
	EXPECT_FALSE(Transition.Active());
}

TEST(QmSetupMotion, EqualElapsedTimeProducesTheSameFrame)
{
	QmSetup::CTransition OneFrame;
	QmSetup::CTransition ManyFrames;
	OneFrame.SetTarget(0.8f, true);
	ManyFrames.SetTarget(0.8f, true);
	OneFrame.Advance(0.12f);
	for(int Frame = 0; Frame < 6; ++Frame)
		ManyFrames.Advance(0.02f);
	EXPECT_NEAR(OneFrame.Value(), ManyFrames.Value(), 0.00001f);
}

TEST(QmSetupMotion, ReversingHoverContinuesFromCurrentValue)
{
	QmSetup::CTransition Transition;
	Transition.SetTarget(1.0f, true);
	Transition.Advance(0.08f);
	const float Before = Transition.Value();
	Transition.SetTarget(0.0f, true);
	EXPECT_FLOAT_EQ(Transition.Value(), Before);
	Transition.Advance(0.001f);
	EXPECT_GT(Transition.Value(), Before);
	Transition.Advance(0.04f);
	EXPECT_LT(Transition.Value(), Before);
	EXPECT_GT(Transition.Value(), 0.0f);
	Transition.Advance(1.0f);
	EXPECT_FLOAT_EQ(Transition.Value(), 0.0f);
}

TEST(QmSetupMotion, DisablingAnimationSettlesEvenWhenTargetIsUnchanged)
{
	QmSetup::CTransition Transition;
	Transition.SetTarget(0.7f, true);
	Transition.Advance(0.04f);
	Transition.SetTarget(0.7f, false);
	EXPECT_FLOAT_EQ(Transition.Value(), 0.7f);
	EXPECT_FALSE(Transition.Active());
}

TEST(QmSetupMotion, RepeatedProgressDoesNotRestartTransition)
{
	QmSetup::CTransition Transition;
	QmSetup::CTransition Reference;
	Transition.SetTarget(0.6f, true);
	Reference.SetTarget(0.6f, true);
	Transition.Advance(0.12f);
	Transition.SetTarget(0.6f, true);
	Transition.Advance(0.12f);
	Reference.Advance(0.24f);
	EXPECT_NEAR(Transition.Value(), Reference.Value(), 0.00001f);
}

TEST(QmSetupMotion, ResetClearsMomentumBeforeTheNextInstall)
{
	QmSetup::CTransition Transition;
	Transition.SetTarget(0.9f, true);
	Transition.Advance(0.04f);
	Transition.Snap(0.0f);
	Transition.Advance(0.1f);
	EXPECT_FLOAT_EQ(Transition.Value(), 0.0f);
	EXPECT_FALSE(Transition.Active());
}
