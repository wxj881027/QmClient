#include <game/client/QmUi/UiProgressRing.h>

#include <gtest/gtest.h>

#include <cmath>

TEST(QmProgressRing, BusyArcAdvancesOnWallClockPeriod)
{
	const auto First = ui_widget::ResolveProgressRingArc(-1.0f, 0, true);
	const auto Half = ui_widget::ResolveProgressRingArc(-1.0f, ui_token::motion::PROGRESS_RING_PERIOD_NS / 2, true);
	const auto NextCycle = ui_widget::ResolveProgressRingArc(-1.0f, ui_token::motion::PROGRESS_RING_PERIOD_NS, true);
	EXPECT_FLOAT_EQ(First.m_Sweep, 1.5f * pi);
	EXPECT_FALSE(First.m_DrawTrack);
	EXPECT_NEAR(Half.m_Start - First.m_Start, pi, 0.00001f);
	EXPECT_NEAR(NextCycle.m_Start, First.m_Start, 0.00001f);
}

TEST(QmProgressRing, BusyArcFreezesWhenAnimationIsDisabled)
{
	const auto First = ui_widget::ResolveProgressRingArc(-1.0f, 0, false);
	const auto Later = ui_widget::ResolveProgressRingArc(-1.0f, ui_token::motion::PROGRESS_RING_PERIOD_NS / 2, false);
	EXPECT_FLOAT_EQ(Later.m_Start, First.m_Start);
	EXPECT_FLOAT_EQ(Later.m_Sweep, First.m_Sweep);
}

TEST(QmProgressRing, DeterminateArcClampsAndRejectsNonFiniteInput)
{
	EXPECT_FLOAT_EQ(ui_widget::ResolveProgressRingArc(0.25f, 0, true).m_Sweep, pi / 2.0f);
	const auto Partial = ui_widget::ResolveProgressRingArc(0.25f, 0, true);
	const auto Complete = ui_widget::ResolveProgressRingArc(2.0f, 0, true);
	EXPECT_TRUE(Partial.m_DrawTrack);
	EXPECT_FLOAT_EQ(Complete.m_Sweep, 2.0f * pi);
	EXPECT_FALSE(Complete.m_DrawTrack);
	EXPECT_FLOAT_EQ(ui_widget::ResolveProgressRingArc(-2.0f, 0, true).m_Sweep, 1.5f * pi);
	EXPECT_FLOAT_EQ(ui_widget::ResolveProgressRingArc(NAN, 0, true).m_Sweep, 0.0f);
}
