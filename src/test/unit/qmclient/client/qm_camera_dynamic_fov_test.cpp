#include <game/client/components/camera.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using QmCameraEffects::CDynamicFovZoom;

TEST(QmDynamicFovZoom, InitialStateAndRepeatedRemovalLeaveBaseUntouched)
{
	CDynamicFovZoom Zoom;
	EXPECT_FALSE(Zoom.Active());
	EXPECT_FLOAT_EQ(Zoom.BaseZoom(1.75f), 1.75f);
	EXPECT_FLOAT_EQ(Zoom.Remove(1.75f), 1.75f);
	EXPECT_FLOAT_EQ(Zoom.Remove(1.75f), 1.75f);
}

TEST(QmDynamicFovZoom, NormalRangePreservesLegacyFloatMultiplicationAndDivision)
{
	CDynamicFovZoom Zoom;
	const float Effective = Zoom.Apply(1.6f, 1.25f);
	EXPECT_FLOAT_EQ(Effective, 2.0f);
	EXPECT_TRUE(Zoom.Active());
	EXPECT_FLOAT_EQ(Zoom.BaseZoom(Effective), 1.6f);
	EXPECT_FLOAT_EQ(Zoom.Remove(Effective), 1.6f);
	EXPECT_FALSE(Zoom.Active());
}

TEST(QmDynamicFovZoom, IdentityAndInvalidFactorsLeaveBaseUnchanged)
{
	for(float Factor : {1.0f, 0.0f, -1.0f, 0.5f, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		SCOPED_TRACE(Factor);
		CDynamicFovZoom Zoom;
		Zoom.Apply(std::numeric_limits<float>::max(), 5.0f);
		EXPECT_FLOAT_EQ(Zoom.Apply(2.0f, Factor), 2.0f);
		EXPECT_FALSE(Zoom.Active());
		EXPECT_FLOAT_EQ(Zoom.Remove(2.0f), 2.0f);
	}
}

TEST(QmDynamicFovZoom, LargestFiniteBaseIsKeptWhenEffectiveZoomClips)
{
	CDynamicFovZoom Zoom;
	const float Base = std::numeric_limits<float>::max();
	const float Effective = Zoom.Apply(Base, 5.0f);
	EXPECT_EQ(Effective, Base);
	EXPECT_TRUE(std::isfinite(Effective));
	EXPECT_TRUE(Zoom.Active());
	EXPECT_EQ(Zoom.BaseZoom(Effective), Base);
	EXPECT_EQ(Zoom.Remove(Effective), Base);
	EXPECT_FALSE(Zoom.Active());
	EXPECT_EQ(Zoom.Remove(Base), Base);
}

TEST(QmDynamicFovZoom, NonPowerOfTwoClippedBaseIsRestoredExactly)
{
	CDynamicFovZoom Zoom;
	const float Base = std::nextafter(std::numeric_limits<float>::max() * 0.6f, 0.0f);
	const float Effective = Zoom.Apply(Base, 2.0f);
	EXPECT_EQ(Effective, std::numeric_limits<float>::max());
	EXPECT_EQ(Zoom.BaseZoom(Effective), Base);
	EXPECT_EQ(Zoom.Remove(Effective), Base);
}

TEST(QmDynamicFovZoom, SmallestRepresentableExpansionAtMaximumDoesNotCreateInfinity)
{
	CDynamicFovZoom Zoom;
	const float Base = std::numeric_limits<float>::max();
	const float Factor = std::nextafter(1.0f, std::numeric_limits<float>::infinity());
	ASSERT_GT(Factor, 1.0f);
	const float Effective = Zoom.Apply(Base, Factor);
	EXPECT_EQ(Effective, Base);
	EXPECT_TRUE(std::isfinite(Effective));
	EXPECT_EQ(Zoom.Remove(Effective), Base);
}

TEST(QmDynamicFovZoom, RepresentableNearMaximumKeepsOriginalFloatPath)
{
	CDynamicFovZoom Zoom;
	const float Base = std::numeric_limits<float>::max() / 4.0f;
	const float Effective = Zoom.Apply(Base, 2.0f);
	EXPECT_EQ(Effective, std::numeric_limits<float>::max() / 2.0f);
	EXPECT_EQ(Zoom.BaseZoom(Effective), Base);
	EXPECT_EQ(Zoom.Remove(Effective), Base);
}

TEST(QmDynamicFovZoom, RepeatedClippedFramesDoNotAccumulateLoss)
{
	CDynamicFovZoom Zoom;
	const float Base = std::nextafter(std::numeric_limits<float>::max() * 0.75f, 0.0f);
	float Current = Base;
	for(int Frame = 0; Frame < 64; ++Frame)
	{
		SCOPED_TRACE(Frame);
		Current = Zoom.Apply(Current, 2.0f + Frame % 4);
		ASSERT_TRUE(std::isfinite(Current));
		EXPECT_EQ(Current, std::numeric_limits<float>::max());
		EXPECT_EQ(Zoom.BaseZoom(Current), Base);
		Current = Zoom.Remove(Current);
		EXPECT_EQ(Current, Base);
		EXPECT_FALSE(Zoom.Active());
	}
}

TEST(QmDynamicFovZoom, NewExplicitBaseReplacesAnEarlierClippedState)
{
	CDynamicFovZoom Zoom;
	Zoom.Apply(std::numeric_limits<float>::max(), 5.0f);
	const float Effective = Zoom.Apply(3.0f, 2.0f);
	EXPECT_FLOAT_EQ(Effective, 6.0f);
	EXPECT_FLOAT_EQ(Zoom.BaseZoom(Effective), 3.0f);
	EXPECT_FLOAT_EQ(Zoom.Remove(Effective), 3.0f);
}

TEST(QmDynamicFovZoom, ResetDiscardsClippedBaseBeforeNewView)
{
	CDynamicFovZoom Zoom;
	Zoom.Apply(std::numeric_limits<float>::max(), 5.0f);
	Zoom.Reset();
	EXPECT_FALSE(Zoom.Active());
	EXPECT_FLOAT_EQ(Zoom.BaseZoom(1.25f), 1.25f);
	EXPECT_FLOAT_EQ(Zoom.Remove(1.25f), 1.25f);
}

TEST(QmDynamicFovZoom, ResetDiscardsOrdinaryAppliedFactor)
{
	CDynamicFovZoom Zoom;
	Zoom.Apply(2.0f, 2.0f);
	Zoom.Reset();
	EXPECT_FALSE(Zoom.Active());
	EXPECT_FLOAT_EQ(Zoom.BaseZoom(1.25f), 1.25f);
	EXPECT_FLOAT_EQ(Zoom.Remove(1.25f), 1.25f);
}

TEST(QmDynamicFovZoom, RepeatedNormalRemovalDoesNotDivideAgain)
{
	CDynamicFovZoom Zoom;
	const float Base = Zoom.Remove(Zoom.Apply(1.6f, 1.25f));
	EXPECT_FLOAT_EQ(Base, 1.6f);
	EXPECT_FLOAT_EQ(Zoom.Remove(Base), Base);
	EXPECT_FALSE(Zoom.Active());
}

TEST(QmDynamicFovZoom, PreexistingInvalidBaseIsNotReplacedByOlderState)
{
	for(float Base : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		CDynamicFovZoom Zoom;
		Zoom.Apply(std::numeric_limits<float>::max(), 5.0f);
		const float Effective = Zoom.Apply(Base, 2.0f);
		EXPECT_FALSE(Zoom.Active());
		if(std::isnan(Base))
			EXPECT_TRUE(std::isnan(Effective));
		else
			EXPECT_EQ(Effective, Base);
	}
}
