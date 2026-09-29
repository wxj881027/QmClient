// 单元测试：离散滑块的几何、输入状态机与档位样式（纯逻辑层）。
// UiDiscreteSlider.cpp 不在测试目标内：它与 CUi 的 active/hot item 交接依赖 CUi::FinishCheck
// 在未渲染帧清理 active item，这里只固定状态机对外承诺的 m_Active 语义。
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
	// 指针停在偏移后的位置：保留抓取偏移时应留在第 3 档，忽略偏移则会跳到第 4 档。
	EXPECT_FALSE(Update(StopX(3) + 20.0f, false, true).m_Changed);
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

TEST(DiscreteSliderInputContract, ActiveOnlyTracksTheHeldGrab)
{
	struct SCase
	{
		const char *m_pName;
		float m_Stop;
		bool m_Pressed;
		bool m_Down;
		bool m_Hovered;
		bool m_CanActivate;
		bool m_Enabled;
		bool m_AlreadyActive;
		bool m_ExpectActive;
		int m_ExpectValue;
	};
	// 覆盖调用点交给状态机的每一种输入组合：只有「指针在控件内按下且没有别的控件占用」或
	// 「已经在拖动且按键仍未松开」才对外报告 active。
	const SCase aCases[] = {
		{"hover-only", 5.0f / 6.0f, false, false, true, true, true, false, false, 3},
		{"held-button-without-fresh-press", 5.0f / 6.0f, false, true, true, true, true, false, false, 3},
		{"press-outside-rect", 5.0f / 6.0f, true, true, false, true, true, false, false, 3},
		{"press-while-other-widget-active", 5.0f / 6.0f, true, true, true, false, true, false, false, 3},
		{"press-while-disabled", 5.0f / 6.0f, true, true, true, true, false, false, false, 3},
		{"press-on-track-captures", 5.0f / 6.0f, true, true, true, true, true, false, true, 5},
		{"release-ends-grab", 3.0f / 6.0f, false, false, true, true, true, true, false, 3},
		{"drag-continues-outside-rect", 5.0f / 6.0f, false, true, false, true, true, true, true, 5},
	};

	for(const SCase &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pName);
		ui_widget::SDiscreteSliderState State;
		const ui_widget::SDiscreteSliderGeometry Geometry = ui_widget::ResolveDiscreteSliderGeometry({0.0f, 0.0f, 240.0f, 20.0f});
		ui_widget::SDiscreteSliderInput Input;
		Input.m_MouseX = Geometry.Position(Case.m_Stop);
		Input.m_Pressed = Case.m_Pressed;
		Input.m_Down = Case.m_Down;
		Input.m_Hovered = Case.m_Hovered;
		Input.m_CanActivate = Case.m_CanActivate;
		Input.m_Enabled = Case.m_Enabled;
		Input.m_Active = Case.m_AlreadyActive;
		const ui_widget::SDiscreteSliderResult Result = ui_widget::UpdateDiscreteSlider(State, Geometry, Input, 3, 0, 6);
		EXPECT_EQ(Result.m_Active, Case.m_ExpectActive);
		EXPECT_EQ(Result.m_Value, Case.m_ExpectValue);
		EXPECT_EQ(Result.m_Changed, Case.m_ExpectValue != 3);
	}
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

TEST(DiscreteSliderGeometry, UiScaleKeepsKnobInsideRectAndStopMappingStable)
{
	// UiScale 是布局缩放（Ctx.m_UiScale），只在 Rect 收边允许范围内放大旋钮与圆点。
	const CUIRect Rect{0.0f, 0.0f, 240.0f, 24.0f};
	for(const float Scale : {0.78f, 1.0f, 1.25f, 2.0f})
	{
		SCOPED_TRACE(Scale);
		const auto Geometry = ui_widget::ResolveDiscreteSliderGeometry(Rect, Scale);
		ASSERT_TRUE(Geometry.IsUsable());
		EXPECT_LE(Geometry.m_KnobSize, 18.0f * Scale + 0.001f);
		EXPECT_LE(Geometry.m_KnobSize, Rect.h / 1.12f + 0.001f);
		const CUIRect FirstKnob = Geometry.KnobRect(0.0f, 1.0f);
		const CUIRect LastKnob = Geometry.KnobRect(1.0f, 1.0f);
		EXPECT_GE(FirstKnob.x, Rect.x - 0.001f);
		EXPECT_LE(LastKnob.x + LastKnob.w, Rect.x + Rect.w + 0.001f);
		for(int Stop = 0; Stop <= 6; ++Stop)
		{
			SCOPED_TRACE(Stop);
			ui_widget::SDiscreteSliderState State;
			ui_widget::SDiscreteSliderInput Input;
			Input.m_MouseX = Geometry.Position(Stop / 6.0f);
			Input.m_Hovered = Input.m_Pressed = Input.m_Down = true;
			EXPECT_EQ(ui_widget::UpdateDiscreteSlider(State, Geometry, Input, -1, 0, 6).m_Value, Stop);
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
