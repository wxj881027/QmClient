// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/render.h>

#include <gtest/gtest.h>

TEST(SkinTransition, DurationMillisecondsMapToExactSeconds)
{
	EXPECT_FLOAT_EQ(SkinChangeTransitionDurationSeconds(500), 0.5f);
	EXPECT_FLOAT_EQ(SkinChangeTransitionDurationSeconds(0), 0.0f);
}

TEST(SkinTransition, ProgressUsesConfiguredDurationAndZeroMeansDisabled)
{
	EXPECT_FLOAT_EQ(ResolveSkinChangeTransitionProgress(0.25f, 500), 0.5f);
	EXPECT_FLOAT_EQ(ResolveSkinChangeTransitionProgress(0.0f, 0), 1.0f);
	EXPECT_FLOAT_EQ(ResolveSkinChangeTransitionProgress(0.25f, 0), 1.0f);
}

TEST(SkinTransition, ProgressEndsExactlyAtConfiguredDuration)
{
	EXPECT_LT(ResolveSkinChangeTransitionProgress(0.499f, 500), 1.0f);
	EXPECT_FLOAT_EQ(ResolveSkinChangeTransitionProgress(0.5f, 500), 1.0f);
	EXPECT_FLOAT_EQ(ResolveSkinChangeTransitionProgress(0.501f, 500), 1.0f);
}

TEST(SkinTransition, DemoPlaybackDoesNotRunLiveSkinChangeTransition)
{
	EXPECT_FALSE(ShouldRunLiveSkinChangeTransition(true));
	EXPECT_TRUE(ShouldRunLiveSkinChangeTransition(false));
}

TEST(SkinTransition, BlendAtStartShowsCurrentSkinImmediately)
{
	const SSkinChangeTransitionBlend Blend = ComputeSkinChangeTransitionBlend(0.0f, SKIN_CHANGE_TRANSITION_GHOST_POP);
	EXPECT_FLOAT_EQ(Blend.m_PreviousAlpha, 1.0f);
	EXPECT_GT(Blend.m_CurrentAlpha, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousBodyScale.x, 1.0f);
	EXPECT_LT(Blend.m_CurrentBodyScale.x, 1.0f);
	EXPECT_LT(Blend.m_CurrentFeetScale.x, 1.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousAngleOffset, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentAngleOffset, 0.0f);
}

TEST(SkinTransition, BlendAtEndUsesOnlyCurrentSkin)
{
	const SSkinChangeTransitionBlend Blend = ComputeSkinChangeTransitionBlend(1.0f, SKIN_CHANGE_TRANSITION_GHOST_POP);
	EXPECT_FLOAT_EQ(Blend.m_PreviousAlpha, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentAlpha, 1.0f);
	EXPECT_NEAR(Blend.m_CurrentBodyScale.x, 1.0f, 0.0001f);
	EXPECT_NEAR(Blend.m_CurrentFeetScale.x, 1.0f, 0.0001f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousAngleOffset, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentAngleOffset, 0.0f);
}

TEST(SkinTransition, BlendMidpointFavorsCurrentSkinAndKeepsPop)
{
	const SSkinChangeTransitionBlend Blend = ComputeSkinChangeTransitionBlend(0.5f, SKIN_CHANGE_TRANSITION_GHOST_POP);
	EXPECT_LT(Blend.m_PreviousAlpha, 1.0f);
	EXPECT_GT(Blend.m_PreviousAlpha, 0.0f);
	EXPECT_GT(Blend.m_CurrentAlpha, 0.5f);
	EXPECT_LT(Blend.m_PreviousBodyScale.x, 1.0f);
	EXPECT_GT(Blend.m_CurrentBodyScale.x, 1.0f);
	EXPECT_GT(Blend.m_CurrentFeetScale.x, 1.0f);
}

TEST(SkinTransition, FadeScaleTypeOnlyTweaksAlphaAndScale)
{
	const SSkinChangeTransitionBlend Blend = ComputeSkinChangeTransitionBlend(0.25f, SKIN_CHANGE_TRANSITION_FADE_SCALE);
	EXPECT_GT(Blend.m_PreviousAlpha, 0.0f);
	EXPECT_GT(Blend.m_CurrentAlpha, 0.0f);
	EXPECT_LT(Blend.m_CurrentBodyScale.x, 1.0f);
	EXPECT_LT(Blend.m_CurrentFeetScale.x, 1.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousAngleOffset, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentAngleOffset, 0.0f);
}

TEST(SkinTransition, SlideLeftTypeAppliesHorizontalOffsets)
{
	const SSkinChangeTransitionBlend Blend = ComputeSkinChangeTransitionBlend(0.25f, SKIN_CHANGE_TRANSITION_SLIDE_LEFT);
	EXPECT_GT(Blend.m_PreviousAlpha, 0.0f);
	EXPECT_GT(Blend.m_CurrentAlpha, 0.0f);
	EXPECT_LT(Blend.m_PreviousPosOffset.x, 0.0f);
	EXPECT_GT(Blend.m_CurrentPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousPosOffset.y, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentPosOffset.y, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousAngleOffset, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentAngleOffset, 0.0f);
}

TEST(SkinTransition, SpinPopTypeAppliesAngleOffsets)
{
	const SSkinChangeTransitionBlend Blend = ComputeSkinChangeTransitionBlend(0.25f, SKIN_CHANGE_TRANSITION_SPIN_POP);
	EXPECT_GT(Blend.m_PreviousAlpha, 0.0f);
	EXPECT_GT(Blend.m_CurrentAlpha, 0.0f);
	EXPECT_LT(Blend.m_PreviousAngleOffset, 0.0f);
	EXPECT_GT(Blend.m_CurrentAngleOffset, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentPosOffset.x, 0.0f);
}

TEST(SkinTransition, ThemeSwitchTypeUsesVerticalMotion)
{
	const SSkinChangeTransitionBlend Blend = ComputeSkinChangeTransitionBlend(0.25f, SKIN_CHANGE_TRANSITION_THEME_SWITCH);
	EXPECT_GT(Blend.m_PreviousAlpha, 0.0f);
	EXPECT_GT(Blend.m_CurrentAlpha, 0.0f);
	EXPECT_LT(Blend.m_PreviousPosOffset.y, 0.0f);
	EXPECT_GT(Blend.m_CurrentPosOffset.y, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentPosOffset.x, 0.0f);
}

TEST(SkinTransition, CustomIntensityScalesVisualParameters)
{
	const SSkinChangeTransitionBlend Normal = ComputeSkinChangeTransitionBlend(0.25f, SKIN_CHANGE_TRANSITION_SLIDE_LEFT, SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_CUBIC, 100);
	const SSkinChangeTransitionBlend Strong = ComputeSkinChangeTransitionBlend(0.25f, SKIN_CHANGE_TRANSITION_SLIDE_LEFT, SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_CUBIC, 200);

	EXPECT_LT(Strong.m_PreviousPosOffset.x, Normal.m_PreviousPosOffset.x);
	EXPECT_GT(Strong.m_CurrentPosOffset.x, Normal.m_CurrentPosOffset.x);
}

TEST(SkinTransition, EasingChangesIntermediateBlendButKeepsEndpoints)
{
	const SSkinChangeTransitionBlend CubicStart = ComputeSkinChangeTransitionBlend(0.0f, SKIN_CHANGE_TRANSITION_GHOST_POP, SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_CUBIC, 100);
	const SSkinChangeTransitionBlend BackStart = ComputeSkinChangeTransitionBlend(0.0f, SKIN_CHANGE_TRANSITION_GHOST_POP, SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_BACK, 100);
	const SSkinChangeTransitionBlend CubicMid = ComputeSkinChangeTransitionBlend(0.5f, SKIN_CHANGE_TRANSITION_FADE_SCALE, SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_CUBIC, 100);
	const SSkinChangeTransitionBlend BackMid = ComputeSkinChangeTransitionBlend(0.5f, SKIN_CHANGE_TRANSITION_FADE_SCALE, SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_BACK, 100);
	const SSkinChangeTransitionBlend CubicEnd = ComputeSkinChangeTransitionBlend(1.0f, SKIN_CHANGE_TRANSITION_GHOST_POP, SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_CUBIC, 100);
	const SSkinChangeTransitionBlend BackEnd = ComputeSkinChangeTransitionBlend(1.0f, SKIN_CHANGE_TRANSITION_GHOST_POP, SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_BACK, 100);

	EXPECT_FLOAT_EQ(CubicStart.m_CurrentAlpha, BackStart.m_CurrentAlpha);
	EXPECT_NE(CubicMid.m_CurrentAlpha, BackMid.m_CurrentAlpha);
	EXPECT_FLOAT_EQ(CubicEnd.m_CurrentAlpha, BackEnd.m_CurrentAlpha);
}

TEST(SkinTransition, ElasticBackKeepsAlphaInDrawableRange)
{
	const SSkinChangeTransitionBlend SkinBlend = ComputeSkinChangeTransitionBlend(0.5f, SKIN_CHANGE_TRANSITION_FADE_SCALE, SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_BACK, 100);

	EXPECT_GE(SkinBlend.m_PreviousAlpha, 0.0f);
	EXPECT_LE(SkinBlend.m_PreviousAlpha, 1.0f);
	EXPECT_GE(SkinBlend.m_CurrentAlpha, 0.0f);
	EXPECT_LE(SkinBlend.m_CurrentAlpha, 1.0f);
}

TEST(SkinTransition, GlitchTypeAppliesJitterAndStaysDrawable)
{
	const SSkinChangeTransitionBlend Blend = ComputeSkinChangeTransitionBlend(0.25f, SKIN_CHANGE_TRANSITION_GLITCH);
	EXPECT_GT(Blend.m_PreviousAlpha, 0.0f);
	EXPECT_LT(Blend.m_PreviousAlpha, 1.0f);
	EXPECT_GT(Blend.m_CurrentAlpha, 0.0f);
	EXPECT_LT(Blend.m_CurrentAlpha, 1.0f);
	// Glitch always produces horizontal motion (jitter + drift); vertical stays zero.
	EXPECT_NE(Blend.m_PreviousPosOffset.x, 0.0f);
	EXPECT_NE(Blend.m_CurrentPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousPosOffset.y, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentPosOffset.y, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousAngleOffset, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentAngleOffset, 0.0f);
	// Higher intensity widens the drift component of the previous offset.
	const SSkinChangeTransitionBlend Strong = ComputeSkinChangeTransitionBlend(0.25f, SKIN_CHANGE_TRANSITION_GLITCH, SKIN_CHANGE_TRANSITION_EASING_EASE_OUT_CUBIC, 200);
	EXPECT_LT(Strong.m_PreviousPosOffset.x, Blend.m_PreviousPosOffset.x);
}

TEST(SkinTransition, ElasticTypeSquashesVerticallyAndStaysDrawable)
{
	const SSkinChangeTransitionBlend Blend = ComputeSkinChangeTransitionBlend(0.25f, SKIN_CHANGE_TRANSITION_ELASTIC);
	EXPECT_GT(Blend.m_PreviousAlpha, 0.0f);
	EXPECT_LT(Blend.m_PreviousAlpha, 1.0f);
	EXPECT_GT(Blend.m_CurrentAlpha, 0.0f);
	EXPECT_LT(Blend.m_CurrentAlpha, 1.0f);
	// Elastic uses vertical motion, no horizontal drift, no rotation.
	EXPECT_GT(Blend.m_PreviousPosOffset.y, 0.0f);
	EXPECT_LT(Blend.m_CurrentPosOffset.y, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentPosOffset.x, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_PreviousAngleOffset, 0.0f);
	EXPECT_FLOAT_EQ(Blend.m_CurrentAngleOffset, 0.0f);
	// Previous skin squashes (Y scale shrinks) while current is larger than 1 at the pop.
	EXPECT_LT(Blend.m_PreviousBodyScale.y, 1.0f);
}
