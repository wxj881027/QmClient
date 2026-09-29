#include <game/client/QmUi/UiDiscreteSlider.h>

#include <gtest/gtest.h>

namespace
{
	class CDiscreteSliderTest : public ::testing::Test
	{
	protected:
		ui_widget::SDiscreteSliderState m_State;
		ui_widget::SDiscreteSliderGeometry m_Geometry = ui_widget::ResolveDiscreteSliderGeometry({10.0f, 20.0f, 240.0f, 20.0f});
		int m_Value = 0;
		bool m_Active = false;

		ui_widget::SDiscreteSliderResult Update(float MouseX, bool Pressed, bool Down, bool Hovered = true, bool CanActivate = true, bool Enabled = true)
		{
			ui_widget::SDiscreteSliderInput Input;
			Input.m_MouseX = MouseX;
			Input.m_Pressed = Pressed;
			Input.m_Down = Down;
			Input.m_Hovered = Hovered;
			Input.m_Active = m_Active;
			Input.m_CanActivate = CanActivate;
			Input.m_Enabled = Enabled;
			const auto Result = ui_widget::UpdateDiscreteSlider(m_State, m_Geometry, Input, m_Value, 0, 6);
			m_Value = Result.m_Value;
			m_Active = Result.m_Active;
			return Result;
		}

		float StopX(int Stop) const { return m_Geometry.Position(Stop / 6.0f); }
	};
}

TEST_F(CDiscreteSliderTest, FirstPressCapturesAndContinuesDraggingWithoutPriorHover)
{
	EXPECT_TRUE(Update(StopX(2), true, true).m_Changed);
	EXPECT_EQ(m_Value, 2);
	EXPECT_TRUE(m_Active);
	EXPECT_TRUE(Update(StopX(4), false, true).m_Changed);
	EXPECT_EQ(m_Value, 4);
}

TEST_F(CDiscreteSliderTest, DraggingPublishesEachNewStopImmediatelyAndOnlyOnce)
{
	Update(StopX(0), true, true);
	for(int Stop = 1; Stop <= 6; ++Stop)
	{
		SCOPED_TRACE(Stop);
		EXPECT_TRUE(Update(StopX(Stop), false, true).m_Changed);
		EXPECT_EQ(m_Value, Stop);
		EXPECT_FALSE(Update(StopX(Stop), false, true).m_Changed);
	}
	EXPECT_FALSE(Update(StopX(6), false, false).m_Changed);
	EXPECT_FALSE(m_Active);
}

TEST_F(CDiscreteSliderTest, ThumbGrabKeepsItsOffsetWhileCrossingStops)
{
	m_Value = 3;
	const float GrabOffset = m_Geometry.m_KnobSize * 0.4f;
	EXPECT_FALSE(Update(StopX(3) + GrabOffset, true, true).m_Changed);
	EXPECT_EQ(m_Value, 3);
	EXPECT_TRUE(Update(StopX(4) + GrabOffset, false, true).m_Changed);
	EXPECT_EQ(m_Value, 4);
}

TEST_F(CDiscreteSliderTest, NarrowTrackCanSelectStopOverlappedByCurrentThumb)
{
	m_Geometry = ui_widget::ResolveDiscreteSliderGeometry({0.0f, 0.0f, 28.0f, 16.0f});
	for(int Stop = 1; Stop <= 6; ++Stop)
	{
		SCOPED_TRACE(Stop);
		EXPECT_TRUE(Update(StopX(Stop), true, true).m_Changed);
		EXPECT_EQ(m_Value, Stop);
		Update(StopX(Stop), false, false);
	}
}

TEST_F(CDiscreteSliderTest, DragClampsOutsideTrackAndCanReturnBeforeRelease)
{
	Update(StopX(3), true, true);
	EXPECT_TRUE(Update(StopX(6) + 100.0f, false, true, false).m_Changed);
	EXPECT_EQ(m_Value, 6);
	EXPECT_TRUE(m_Active);
	EXPECT_TRUE(Update(StopX(0) - 100.0f, false, true, false).m_Changed);
	EXPECT_EQ(m_Value, 0);
	EXPECT_TRUE(Update(StopX(2), false, true).m_Changed);
	EXPECT_EQ(m_Value, 2);
}

TEST_F(CDiscreteSliderTest, ReleaseCommitsLastPointerPositionAndStopsDragging)
{
	Update(StopX(1), true, true);
	EXPECT_TRUE(Update(StopX(5), false, false, false).m_Changed);
	EXPECT_EQ(m_Value, 5);
	EXPECT_FALSE(m_Active);
	EXPECT_FALSE(Update(StopX(0), false, false).m_Changed);
	EXPECT_EQ(m_Value, 5);
}

TEST_F(CDiscreteSliderTest, HoveringWithButtonAlreadyHeldDoesNotStartDrag)
{
	EXPECT_FALSE(Update(StopX(3), false, true).m_Changed);
	EXPECT_FALSE(m_Active);
	EXPECT_EQ(m_Value, 0);
}

TEST_F(CDiscreteSliderTest, AnotherControlOwnsPressUntilItReleases)
{
	EXPECT_FALSE(Update(StopX(3), true, true, true, false).m_Changed);
	EXPECT_FALSE(m_Active);
	Update(StopX(3), false, false);
	EXPECT_TRUE(Update(StopX(3), true, true).m_Changed);
	EXPECT_TRUE(m_Active);
}

TEST_F(CDiscreteSliderTest, ClippedOrCoveredTrackCannotCapturePress)
{
	EXPECT_FALSE(Update(StopX(3), true, true, false).m_Changed);
	EXPECT_FALSE(m_Active);
	EXPECT_EQ(m_Value, 0);
}

TEST_F(CDiscreteSliderTest, DisabledInputCancelsDragAndAllowsFreshPress)
{
	Update(StopX(2), true, true);
	EXPECT_FALSE(Update(StopX(5), false, true, true, true, false).m_Changed);
	EXPECT_FALSE(m_Active);
	EXPECT_EQ(m_Value, 2);
	EXPECT_FALSE(Update(StopX(5), false, true).m_Changed);
	Update(StopX(5), false, false);
	EXPECT_TRUE(Update(StopX(5), true, true).m_Changed);
	EXPECT_EQ(m_Value, 5);
}

TEST_F(CDiscreteSliderTest, LostCaptureCannotResumeFromHeldButton)
{
	Update(StopX(2), true, true);
	m_Active = false;
	EXPECT_FALSE(Update(StopX(5), false, true).m_Changed);
	EXPECT_FALSE(m_Active);
	EXPECT_EQ(m_Value, 2);
}

TEST_F(CDiscreteSliderTest, IndeterminateSelectionStaysUntouchedUntilExplicitChoice)
{
	m_Value = -1;
	EXPECT_FALSE(Update(StopX(0), false, false).m_Changed);
	EXPECT_EQ(m_Value, -1);
	EXPECT_TRUE(Update(StopX(0), true, true).m_Changed);
	EXPECT_EQ(m_Value, 0);
}

TEST(DiscreteSliderGeometry, DrawnStopsMatchClicksAtDifferentSizesAndOrigins)
{
	const CUIRect aRects[] = {{0.0f, 0.0f, 150.0f, 16.0f}, {42.0f, 10.0f, 300.0f, 24.0f}, {-40.0f, 80.0f, 430.0f, 32.0f}};
	for(const auto &Rect : aRects)
	{
		SCOPED_TRACE(Rect.w);
		const auto Geometry = ui_widget::ResolveDiscreteSliderGeometry(Rect);
		ASSERT_TRUE(Geometry.IsUsable());
		EXPECT_LT(Geometry.m_Track.h, Geometry.m_KnobSize);
		EXPECT_GT(Geometry.m_Track.h, Geometry.m_KnobSize * 0.8f);
		const CUIRect FirstKnob = Geometry.KnobRect(0.0f, 1.0f);
		const CUIRect LastKnob = Geometry.KnobRect(1.0f, 1.0f);
		EXPECT_GE(FirstKnob.x, Rect.x - 0.001f);
		EXPECT_LE(LastKnob.x + LastKnob.w, Rect.x + Rect.w + 0.001f);
		EXPECT_GE(FirstKnob.y, Rect.y - 0.001f);
		EXPECT_LE(FirstKnob.y + FirstKnob.h, Rect.y + Rect.h + 0.001f);
		for(int Stop = 0; Stop <= 6; ++Stop)
		{
			SCOPED_TRACE(Stop);
			ui_widget::SDiscreteSliderState State;
			ui_widget::SDiscreteSliderInput Input;
			Input.m_MouseX = Geometry.Position(Stop / 6.0f);
			Input.m_Hovered = Input.m_Pressed = Input.m_Down = true;
			const auto Result = ui_widget::UpdateDiscreteSlider(State, Geometry, Input, -1, 0, 6);
			EXPECT_EQ(Result.m_Value, Stop);
			EXPECT_TRUE(Result.m_Active);
		}
	}
}

TEST(DiscreteSliderGeometry, TrackClicksChooseNearestStopWithNonzeroMinimum)
{
	const auto Geometry = ui_widget::ResolveDiscreteSliderGeometry({0.0f, 0.0f, 240.0f, 20.0f});
	ui_widget::SDiscreteSliderInput Input;
	Input.m_Hovered = Input.m_Pressed = Input.m_Down = true;
	ui_widget::SDiscreteSliderState State;
	Input.m_MouseX = Geometry.Position(0.24f);
	EXPECT_EQ(ui_widget::UpdateDiscreteSlider(State, Geometry, Input, -1, 10, 16).m_Value, 11);
	Input.m_MouseX = Geometry.Position(0.26f);
	EXPECT_EQ(ui_widget::UpdateDiscreteSlider(State, Geometry, Input, -1, 10, 16).m_Value, 12);
}

TEST(DiscreteSliderGeometry, CollapsedLayoutCancelsCaptureWithoutChangingSelection)
{
	const CUIRect aRects[] = {{0.0f, 0.0f, 0.0f, 20.0f}, {0.0f, 0.0f, 200.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 20.0f}, {0.0f, 0.0f, 1.0f, 20.0f}};
	for(const auto &Rect : aRects)
	{
		SCOPED_TRACE(Rect.w);
		const auto Geometry = ui_widget::ResolveDiscreteSliderGeometry(Rect);
		ui_widget::SDiscreteSliderState State;
		ui_widget::SDiscreteSliderInput Input;
		Input.m_Active = Input.m_Down = true;
		const auto Result = ui_widget::UpdateDiscreteSlider(State, Geometry, Input, 3, 0, 6);
		EXPECT_EQ(Result.m_Value, 3);
		EXPECT_FALSE(Result.m_Changed);
		EXPECT_FALSE(Result.m_Active);
	}
}

TEST(DiscreteSliderGeometry, InvalidRangeKeepsSelectionAndCancelsCapture)
{
	const auto Geometry = ui_widget::ResolveDiscreteSliderGeometry({0.0f, 0.0f, 240.0f, 20.0f});
	ui_widget::SDiscreteSliderState State;
	ui_widget::SDiscreteSliderInput Input;
	Input.m_Active = Input.m_Down = true;
	const auto Result = ui_widget::UpdateDiscreteSlider(State, Geometry, Input, 3, 6, 0);
	EXPECT_EQ(Result.m_Value, 3);
	EXPECT_FALSE(Result.m_Changed);
	EXPECT_FALSE(Result.m_Active);
}

TEST(DiscreteSliderStyle, HighestLevelUsesPurpleGradientAndHighLevelsHaveParticles)
{
	const auto Low = ui_widget::ResolveDiscreteSliderStyle(1, 0, 6);
	const auto High = ui_widget::ResolveDiscreteSliderStyle(5, 0, 6);
	const auto Maximum = ui_widget::ResolveDiscreteSliderStyle(6, 0, 6);
	EXPECT_FALSE(Low.m_Gradient);
	EXPECT_EQ(Low.m_ParticleCount, 0);
	EXPECT_NE(Low.m_Color, High.m_Color);
	EXPECT_GT(High.m_Color.r, High.m_Color.b);
	EXPECT_GT(High.m_Color.b, High.m_Color.g);
	EXPECT_FALSE(High.m_Gradient);
	EXPECT_GT(High.m_ParticleCount, 0);
	EXPECT_TRUE(Maximum.m_Gradient);
	EXPECT_GT(Maximum.m_Color.b, Maximum.m_Color.r);
	EXPECT_GT(Maximum.m_Color.r, Maximum.m_Color.g);
	EXPECT_NE(Maximum.m_GradientStart, Maximum.m_GradientMiddle);
	EXPECT_NE(Maximum.m_GradientMiddle, Maximum.m_GradientEnd);
	EXPECT_GT(Maximum.m_ParticleCount, 0);
}

TEST(DiscreteSliderStyle, CustomAndEmptySelectionsHaveNoParticleEffects)
{
	EXPECT_EQ(ui_widget::ResolveDiscreteSliderStyle(-1, 0, 6).m_ParticleCount, 0);
	EXPECT_EQ(ui_widget::ResolveDiscreteSliderStyle(0, 0, 6).m_ParticleCount, 0);
	EXPECT_FALSE(ui_widget::ResolveDiscreteSliderStyle(-1, 0, 6).m_Gradient);
	EXPECT_FALSE(ui_widget::ResolveDiscreteSliderStyle(0, 0, 0).m_Gradient);
}

TEST(DiscreteSliderStyle, ParticlesStayInsideFilledCapsuleAcrossAnimationFrames)
{
	const CUIRect Fill{17.0f, 31.0f, 190.0f, 16.0f};
	const int Count = ui_widget::ResolveDiscreteSliderStyle(6, 0, 6).m_ParticleCount;
	for(float Time : {0.0f, 0.5f, 4.0f, 100.0f})
	{
		SCOPED_TRACE(Time);
		for(int Index = 0; Index < Count; ++Index)
		{
			SCOPED_TRACE(Index);
			const auto Particle = ui_widget::ResolveDiscreteSliderParticle(Fill, Index, Time);
			EXPECT_GT(Particle.m_Rect.w, 0.0f);
			EXPECT_GE(Particle.m_Rect.x, Fill.x + Fill.h * 0.5f);
			EXPECT_LE(Particle.m_Rect.x + Particle.m_Rect.w, Fill.x + Fill.w - Fill.h * 0.5f);
			EXPECT_GE(Particle.m_Rect.y, Fill.y);
			EXPECT_LE(Particle.m_Rect.y + Particle.m_Rect.h, Fill.y + Fill.h);
			EXPECT_GT(Particle.m_Alpha, 0.0f);
			EXPECT_LE(Particle.m_Alpha, 1.0f);
		}
	}
}

TEST(DiscreteSliderStyle, CollapsedFillDoesNotProduceParticles)
{
	EXPECT_EQ(ui_widget::ResolveDiscreteSliderParticle({0.0f, 0.0f, 5.0f, 16.0f}, 0, 0.0f).m_Alpha, 0.0f);
	EXPECT_EQ(ui_widget::ResolveDiscreteSliderParticle({0.0f, 0.0f, 200.0f, 0.0f}, 0, 0.0f).m_Alpha, 0.0f);
}
