#include <game/client/QmUi/UiDiscreteSliderStyle.h>

#include <gtest/gtest.h>

TEST(DiscreteSliderStyle, ProgressMovesFromLightBlueToPurple)
{
	const ui_widget::SDiscreteSliderStyle Low = ui_widget::ResolveDiscreteSliderStyle(0.0f);
	const ui_widget::SDiscreteSliderStyle High = ui_widget::ResolveDiscreteSliderStyle(0.75f);
	const ui_widget::SDiscreteSliderStyle Maximum = ui_widget::ResolveDiscreteSliderStyle(1.0f);

	EXPECT_GT(Low.m_Color.b, Low.m_Color.g);
	EXPECT_GT(High.m_Color.r, High.m_Color.g);
	EXPECT_GT(Maximum.m_Color.b, Maximum.m_Color.g);
	EXPECT_TRUE(Maximum.m_Gradient);
	EXPECT_NE(Low.m_Color, High.m_Color);
	EXPECT_NE(Maximum.m_GradientStart, Maximum.m_GradientMiddle);
	EXPECT_NE(Maximum.m_GradientMiddle, Maximum.m_GradientEnd);
}

TEST(DiscreteSliderStyle, HighLevelsAnimateParticlesInsideTheFill)
{
	const CUIRect Fill{17.0f, 31.0f, 190.0f, 16.0f};
	const int Count = ui_widget::ResolveDiscreteSliderStyle(0.9f).m_ParticleCount;
	ASSERT_GT(Count, 0);
	for(float Time : {0.0f, 0.5f, 4.0f, 100.0f})
	{
		SCOPED_TRACE(Time);
		for(int Index = 0; Index < Count; ++Index)
		{
			SCOPED_TRACE(Index);
			const ui_widget::SDiscreteSliderParticle Particle = ui_widget::ResolveDiscreteSliderParticle(Fill, Index, Time);
			EXPECT_GE(Particle.m_Rect.x, Fill.x + Fill.h * 0.5f);
			EXPECT_LE(Particle.m_Rect.x + Particle.m_Rect.w, Fill.x + Fill.w - Fill.h * 0.5f);
			EXPECT_GE(Particle.m_Rect.y, Fill.y);
			EXPECT_LE(Particle.m_Rect.y + Particle.m_Rect.h, Fill.y + Fill.h);
			EXPECT_GT(Particle.m_Alpha, 0.0f);
			EXPECT_LE(Particle.m_Alpha, 1.0f);
		}
	}
}
