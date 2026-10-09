// QmAnim 行为测试：forms。
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmAnimCurves.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/QmTree.h>
#include <game/client/QmUi/SettingsCardGeometry.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtonStyle.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/QmUi/UiFormLogic.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiMotion.h>
#include <game/client/QmUi/UiOverlays.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_rect.h>
#include <game/client/ui_scrollregion.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <cmath>

namespace
{

}

TEST(InputField, ClearAndTrailingSlotsDoNotOverlap)
{
	const CUIRect Rect{10.0f, 20.0f, 160.0f, 32.0f};
	const ui_widget::SInputFieldLayout Layout = ui_widget::ResolveInputFieldLayout(Rect, false, true, 1.0f, Rect.h);

	EXPECT_GT(Layout.m_ClearRect.w, 0.0f);
	EXPECT_GT(Layout.m_TrailingRect.w, 0.0f);
	EXPECT_LE(Layout.m_TrailingRect.x + Layout.m_TrailingRect.w, Layout.m_ClearRect.x);
	EXPECT_LT(Layout.m_ContentRect.x + Layout.m_ContentRect.w, Layout.m_TrailingRect.x);
}

TEST(InputField, ActivationRequiresPressStartingInsideField)
{
	EXPECT_TRUE(QmEditBoxShouldStartActivation(true, true));
	EXPECT_FALSE(QmEditBoxShouldStartActivation(true, false));
	EXPECT_FALSE(QmEditBoxShouldStartActivation(false, true));
}

TEST(SettingsPageLayout, ConfigRowsIncludePaddingAndResponsiveControlBlock)
{
	const SSettingsConfigRowMetrics Wide = ResolveSettingsConfigRowMetrics(false, false, 20.0f, 5.0f, 10.0f, 20.0f, 5.0f);
	EXPECT_FLOAT_EQ(Wide.m_ControlBlockHeight, 20.0f);
	EXPECT_FLOAT_EQ(Wide.m_RowHeight, 42.0f);

	const SSettingsConfigRowMetrics Narrow = ResolveSettingsConfigRowMetrics(false, true, 20.0f, 5.0f, 10.0f, 20.0f, 5.0f);
	EXPECT_FLOAT_EQ(Narrow.m_ControlBlockHeight, 45.0f);
	EXPECT_FLOAT_EQ(Narrow.m_RowHeight, 67.0f);

	const SSettingsConfigRowMetrics CompactNarrow = ResolveSettingsConfigRowMetrics(true, true, 20.0f, 5.0f, 10.0f, 24.0f, 5.0f);
	EXPECT_FLOAT_EQ(CompactNarrow.m_ControlLineHeight, 24.0f);
	EXPECT_FLOAT_EQ(CompactNarrow.m_ControlBlockHeight, 49.0f);
	EXPECT_FLOAT_EQ(CompactNarrow.m_RowHeight, 59.0f);
}
TEST(NumericField, FormatsAndParsesIntegerDecimalAndInfinity)
{
	ui_widget::SNumericValueFormat Integer;
	Integer.m_DisplayDivisor = 1;
	Integer.m_Precision = 0;
	EXPECT_EQ(ui_widget::FormatNumericFieldValue(42, Integer), "42");

	ui_widget::SNumericValueFormat Decimal;
	Decimal.m_DisplayDivisor = 100;
	Decimal.m_Precision = 2;
	EXPECT_EQ(ui_widget::FormatNumericFieldValue(125, Decimal), "1.25");
	int Stored = 0;
	EXPECT_TRUE(ui_widget::ParseNumericFieldValue("-3.50", Decimal, -1000, 1000, &Stored));
	EXPECT_EQ(Stored, -350);

	Decimal.m_AllowInfinite = true;
	Decimal.m_InfiniteStoredValue = 0;
	EXPECT_TRUE(ui_widget::ParseNumericFieldValue("∞", Decimal, -1000, 1000, &Stored));
	EXPECT_EQ(Stored, 0);
	EXPECT_EQ(ui_widget::FormatNumericFieldValue(0, Decimal), "∞");
}
TEST(NumericField, TextInputUsesIndependentBoundAndPreservesInfinity)
{
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(0, 1, 0, 1000, 10000, false), 0);
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(5, 1, 0, 1000, 10000, false), 5);
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(5000, 1, 0, 1000, 10000, false), 5000);
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(10001, 1, 0, 1000, 10000, false), 10000);
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(10001, 1, 10, 1000, -1, false), 1000);
	EXPECT_EQ(ui_widget::NumericFieldTextInputStoredValue(1000, 1, 0, 1000, 10000, true), 0);
}
TEST(NumericField, QuantizedValuesRespectSliderAndExtendedInputBounds)
{
	EXPECT_EQ(ui_widget::QuantizeNumericFieldStoredValue(149, 100, 3000, 100), 100);
	EXPECT_EQ(ui_widget::QuantizeNumericFieldStoredValue(151, 100, 3000, 100), 200);
	EXPECT_EQ(ui_widget::QuantizeNumericFieldStoredValue(5001, 0, 10000, 100), 5000);
	EXPECT_EQ(ui_widget::QuantizeNumericFieldStoredValue(10001, 0, 10000, 100), 10000);
}
TEST(NumericField, DelayPolicyCommitsOnlyOnReleaseSubmitOrBlur)
{
	ui_widget::SInputFieldResult Editing;
	Editing.m_Changed = true;
	EXPECT_FALSE(ui_widget::NumericFieldShouldCommit(ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, false, Editing));
	Editing.m_Submitted = true;
	EXPECT_TRUE(ui_widget::NumericFieldShouldCommit(ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, false, Editing));
	Editing.m_Submitted = false;
	Editing.m_Deactivated = true;
	EXPECT_TRUE(ui_widget::NumericFieldShouldCommit(ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, false, Editing));
	EXPECT_TRUE(ui_widget::NumericFieldShouldCommit(ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, true, {}));
}
TEST(NumericField, DelayPolicyStagesSliderValueUntilRelease)
{
	ui_widget::SNumericFieldCommitState State;
	int StoredValue = 10;

	EXPECT_FALSE(ui_widget::UpdateNumericFieldSliderCommit(State, ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, true, false, 25, &StoredValue));
	EXPECT_EQ(StoredValue, 10);
	EXPECT_TRUE(State.m_HasPendingValue);
	EXPECT_EQ(State.m_PendingStoredValue, 25);

	EXPECT_TRUE(ui_widget::UpdateNumericFieldSliderCommit(State, ui_widget::EInputCommitPolicy::ON_RELEASE_OR_SUBMIT, false, true, 10, &StoredValue));
	EXPECT_EQ(StoredValue, 25);
	EXPECT_FALSE(State.m_HasPendingValue);
}
TEST(NumericField, FallsBackToTwoRowsBeforeCollapsingSliderTrack)
{
	const ui_widget::SNumericFieldLayout Wide = ui_widget::ResolveNumericFieldLayout({0.0f, 0.0f, 500.0f, 36.0f}, true, true, 1.0f);
	const ui_widget::SNumericFieldLayout Narrow = ui_widget::ResolveNumericFieldLayout({0.0f, 0.0f, 260.0f, 36.0f}, true, true, 1.0f);
	EXPECT_FALSE(Wide.m_TwoRows);
	EXPECT_GE(Wide.m_SliderRect.w, 96.0f);
	EXPECT_TRUE(Narrow.m_TwoRows);
	EXPECT_GE(Narrow.m_SliderRect.w, 96.0f);
}
TEST(UiForms, SliderInputValueMappingPreservesStoredScaleAndInfiniteSentinel)
{
	EXPECT_EQ(ui_widget::SliderInputStoredMinimum(100, 20), 5);
	EXPECT_EQ(ui_widget::SliderInputStoredMaximum(300, 20), 15);
	EXPECT_EQ(ui_widget::SliderInputDisplayValue(5, 20), 100);
	EXPECT_EQ(ui_widget::SliderInputStoredValue(200, 20), 10);
	EXPECT_EQ(ui_widget::SliderInputStoredValue(201, 20), 10);
	EXPECT_TRUE(ui_widget::SliderInputIsInfiniteValue(0, true));
	EXPECT_FALSE(ui_widget::SliderInputIsInfiniteValue(1, true));
	EXPECT_EQ(ui_widget::SliderInputWheelStoredValue(0, 1, 1001, true, -28), 973);
	EXPECT_EQ(ui_widget::SliderInputWheelStoredValue(0, 1, 1001, true, 28), 0);
	EXPECT_NEAR(ui_widget::NumericFieldInfiniteEndpointStart(240.0f, 1.0f), 0.95f, 0.001f);
	EXPECT_LE(ui_widget::NumericFieldInfiniteEndpointStart(240.0f, 1.0f), 0.96f);
	EXPECT_GE(ui_widget::NumericFieldInfiniteEndpointStart(240.0f, 1.0f), 0.94f);
}

TEST(BooleanControlLayout, LabelAndSwitchStaySeparateInNarrowAndScaledRows)
{
	for(const CUIRect Rect : {CUIRect{10, 20, 300, 20}, CUIRect{10, 20, 300, 40}, CUIRect{10, 20, 12, 20}})
	{
		SCOPED_TRACE(::testing::Message() << "width=" << Rect.w << " height=" << Rect.h);
		const auto Layout = ui_widget::ResolveBooleanControlLayout(Rect, true);
		EXPECT_GE(Layout.m_LabelRect.w, 0.0f);
		EXPECT_LE(Layout.m_LabelRect.x + Layout.m_LabelRect.w, Layout.m_ControlRect.x);
		EXPECT_GE(Layout.m_ControlRect.x, Rect.x);
		EXPECT_LE(Layout.m_ControlRect.x + Layout.m_ControlRect.w, Rect.x + Rect.w);
	}
}

TEST(BooleanControlLayout, UnlabelledSwitchIsCenteredWithinItsHitArea)
{
	const CUIRect Rect{10, 20, 60, 20};
	const auto Layout = ui_widget::ResolveBooleanControlLayout(Rect, false);
	EXPECT_FLOAT_EQ(Layout.m_ControlRect.x + Layout.m_ControlRect.w * 0.5f, Rect.x + Rect.w * 0.5f);
	EXPECT_FLOAT_EQ(Layout.m_LabelRect.w, 0.0f);
}

TEST(ToggleLayout, KnobStaysInsideTrackAcrossEndpointsAndSmallDimensions)
{
	for(const CUIRect Rect : {CUIRect{10, 20, 33, 20}, CUIRect{10, 20, 10, 24}, CUIRect{10, 20, 1, 1}, CUIRect{10, 20, 0, 20}})
	{
		for(float Progress : {-1.0f, 0.0f, 0.5f, 1.0f, 2.0f})
		{
			SCOPED_TRACE(::testing::Message() << "width=" << Rect.w << " progress=" << Progress);
			const auto Layout = ui_widget::ResolveToggleLayout(Rect, Progress);
			EXPECT_GE(Layout.m_Knob.w, 0.0f);
			EXPECT_GE(Layout.m_Knob.x, Layout.m_Track.x);
			EXPECT_GE(Layout.m_Knob.y, Layout.m_Track.y);
			EXPECT_LE(Layout.m_Knob.x + Layout.m_Knob.w, Layout.m_Track.x + Layout.m_Track.w + 0.001f);
			EXPECT_LE(Layout.m_Knob.y + Layout.m_Knob.h, Layout.m_Track.y + Layout.m_Track.h + 0.001f);
		}
	}
}

TEST(ToggleStyle, OnUsesAccentAndOffPreservesConfiguredControlSurface)
{
	SUiTheme Theme{};
	Theme.m_Accent = ColorRGBA(0.7f, 0.2f, 0.5f, 1.0f);
	const ColorRGBA Surface(0.1f, 0.3f, 0.2f, 0.4f), Backdrop(0, 0, 0, 1);
	const auto Off = ResolveUiToggleStyle(Theme, Surface, Backdrop, false, true);
	const auto On = ResolveUiToggleStyle(Theme, Surface, Backdrop, true, true);
	const auto Disabled = ResolveUiToggleStyle(Theme, Surface, Backdrop, true, false);
	EXPECT_EQ(Off.m_Track, Surface);
	EXPECT_EQ(On.m_Track, Theme.m_Accent);
	EXPECT_LT(Disabled.m_Track.a, On.m_Track.a);
	EXPECT_LT(Disabled.m_Knob.a, On.m_Knob.a);
}

TEST(ToggleStyle, HoverAndPressKeepKnobColorAcrossForegroundThreshold)
{
	const ColorRGBA Backdrop(0, 0, 0, 1);
	for(const float Gray : {0.42f, 0.48f})
	{
		const ColorRGBA Surface(Gray, Gray, Gray, 1);
		SUiTheme Theme{};
		Theme.m_Accent = Surface;
		for(const bool Value : {false, true})
		{
			SCOPED_TRACE(::testing::Message() << "gray=" << Gray << " value=" << Value);
			const auto Base = ResolveUiToggleStyle(Theme, Surface, Backdrop, Value, true);
			const auto Idle = ResolveUiToggleFeedbackStyle(Base, Base.m_Track, Backdrop, true, false, false);
			const auto Hover = ResolveUiToggleFeedbackStyle(Base, Base.m_Track, Backdrop, true, true, false);
			const auto Press = ResolveUiToggleFeedbackStyle(Base, Base.m_Track, Backdrop, true, true, true);
			EXPECT_EQ(Idle.m_Knob, Gray < 0.45f ? ColorRGBA(1, 1, 1, 1) : ColorRGBA(0, 0, 0, 1));
			EXPECT_EQ(Hover.m_Knob, Idle.m_Knob);
			EXPECT_EQ(Press.m_Knob, Idle.m_Knob);
			EXPECT_NE(Hover.m_Track, Idle.m_Track);
			EXPECT_NE(Press.m_Track, Hover.m_Track);
			EXPECT_GT(Hover.m_Border.a, Idle.m_Border.a);
		}
	}
}

TEST(ToggleStyle, ReleaseAndDraggingOutsideRestoreFeedbackWithoutChangingKnob)
{
	SUiTheme Theme{};
	Theme.m_Accent = ColorRGBA(0.42f, 0.42f, 0.42f, 1);
	const ColorRGBA Backdrop(0, 0, 0, 1);
	const auto Base = ResolveUiToggleStyle(Theme, ColorRGBA(), Backdrop, true, true);
	const auto Hover = ResolveUiToggleFeedbackStyle(Base, Base.m_Track, Backdrop, true, true, false);
	for(int Interaction = 0; Interaction < 3; ++Interaction)
	{
		const auto Press = ResolveUiToggleFeedbackStyle(Base, Base.m_Track, Backdrop, true, true, true);
		const auto Release = ResolveUiToggleFeedbackStyle(Base, Base.m_Track, Backdrop, true, true, false);
		const auto Outside = ResolveUiToggleFeedbackStyle(Base, Base.m_Track, Backdrop, true, false, true);
		EXPECT_EQ(Press.m_Knob, Base.m_Knob);
		EXPECT_EQ(Release.m_Track, Hover.m_Track);
		EXPECT_EQ(Release.m_Knob, Base.m_Knob);
		EXPECT_EQ(Outside.m_Track, Base.m_Track);
		EXPECT_EQ(Outside.m_Knob, Base.m_Knob);
	}
}

TEST(ToggleStyle, AnimatedTrackDoesNotOverrideStableKnobColor)
{
	SUiTheme Theme{};
	Theme.m_Accent = ColorRGBA(0.48f, 0.48f, 0.48f, 1);
	const ColorRGBA Backdrop(0, 0, 0, 1);
	const auto Base = ResolveUiToggleStyle(Theme, ColorRGBA(), Backdrop, true, true);
	const ColorRGBA AnimatedTrack(0.2f, 0.2f, 0.2f, 0.5f);
	const auto Style = ResolveUiToggleFeedbackStyle(Base, AnimatedTrack, Backdrop, true, false, false);
	EXPECT_EQ(Style.m_Track, AnimatedTrack);
	EXPECT_EQ(Style.m_Knob, Base.m_Knob);
}

TEST(ToggleStyle, DisabledKeepsMutedKnobAndIgnoresPointerFeedback)
{
	SUiTheme Theme{};
	Theme.m_Accent = ColorRGBA(0.42f, 0.42f, 0.42f, 0.2f);
	const ColorRGBA Backdrop(0, 0, 0, 1);
	const auto Base = ResolveUiToggleStyle(Theme, ColorRGBA(), Backdrop, true, false);
	const auto Idle = ResolveUiToggleFeedbackStyle(Base, Base.m_Track, Backdrop, false, false, false);
	const auto Press = ResolveUiToggleFeedbackStyle(Base, Base.m_Track, Backdrop, false, true, true);
	EXPECT_EQ(Press.m_Track, Idle.m_Track);
	EXPECT_EQ(Press.m_Knob, Idle.m_Knob);
	EXPECT_EQ(Press.m_Border, Idle.m_Border);
	EXPECT_FLOAT_EQ(Idle.m_Knob.a, 0.65f);
}

TEST(UiThemeAccent, OpacityPreservesRgbAndAllowsFullyTransparentAccent)
{
	const ColorHSLA Accent(0.6f, 0.7f, 0.5f, 1);
	const auto Opaque = ResolveUiTheme(ColorHSLA(0), 1, ColorHSLA(0), Accent);
	for(const float Opacity : {0.0f, 0.2f, 0.75f, 1.0f})
	{
		SCOPED_TRACE(Opacity);
		const auto Theme = ResolveUiTheme(ColorHSLA(0), 1, ColorHSLA(0), Accent.WithAlpha(Opacity));
		EXPECT_FLOAT_EQ(Theme.m_Accent.a, Opacity);
		EXPECT_FLOAT_EQ(Theme.m_Accent.r, Opaque.m_Accent.r);
		EXPECT_FLOAT_EQ(Theme.m_Accent.g, Opaque.m_Accent.g);
		EXPECT_FLOAT_EQ(Theme.m_Accent.b, Opaque.m_Accent.b);
		EXPECT_FLOAT_EQ(Theme.m_BorderHovered.a, 0.45f * Opacity);
		EXPECT_FLOAT_EQ(Theme.m_BorderFocused.a, 0.75f * Opacity);
		EXPECT_EQ(Theme.m_FocusRing, Opaque.m_FocusRing);
	}
}

TEST(UiThemeAccent, TransparentEnabledTrackKeepsKnobVisibleAgainstBackdrop)
{
	const auto Theme = ResolveUiTheme(ColorHSLA(0), 1, ColorHSLA(0), ColorHSLA(0, 0, 1, 0));
	const auto Dark = ResolveUiToggleStyle(Theme, ColorRGBA(), ColorRGBA(0, 0, 0, 1), true, true);
	const auto Light = ResolveUiToggleStyle(Theme, ColorRGBA(), ColorRGBA(1, 1, 1, 1), true, true);
	EXPECT_FLOAT_EQ(Dark.m_Track.a, 0.0f);
	EXPECT_FLOAT_EQ(Light.m_Track.a, 0.0f);
	EXPECT_EQ(Dark.m_Knob, ColorRGBA(1, 1, 1, 1));
	EXPECT_EQ(Light.m_Knob, ColorRGBA(0, 0, 0, 1));
}

class CUiAccentConfigTest : public ::testing::Test
{
	unsigned m_PreviousColor = g_Config.m_QmUiAccentColor;
	int m_PreviousOpacity = g_Config.m_QmUiAccentOpacity;

protected:
	void SetUp() override
	{
		g_Config.m_QmUiAccentColor = DefaultConfig::QmUiAccentColor;
		g_Config.m_QmUiAccentOpacity = 100;
	}

	void TearDown() override
	{
		g_Config.m_QmUiAccentColor = m_PreviousColor;
		g_Config.m_QmUiAccentOpacity = m_PreviousOpacity;
	}
};

TEST_F(CUiAccentConfigTest, FallbackThemeReflectsOpacityChangesAndRestoration)
{
	const auto Before = ResolveInputFallbackTheme(g_Config.m_QmUiFocusColor);
	EXPECT_FLOAT_EQ(Before.m_Accent.a, 1.0f);
	for(const int Opacity : {20, 0, 100})
	{
		g_Config.m_QmUiAccentOpacity = Opacity;
		const auto Theme = ResolveInputFallbackTheme(g_Config.m_QmUiFocusColor);
		EXPECT_FLOAT_EQ(Theme.m_Accent.a, Opacity / 100.0f);
		EXPECT_FLOAT_EQ(Theme.m_Accent.r, Before.m_Accent.r);
		EXPECT_FLOAT_EQ(Theme.m_Accent.g, Before.m_Accent.g);
		EXPECT_FLOAT_EQ(Theme.m_Accent.b, Before.m_Accent.b);
	}
}

TEST(SliderLayout, FillFollowsClampedValueAndMatchesHandleCenter)
{
	const CUIRect Rect{10, 20, 200, 20};
	for(float Value : {-1.0f, 0.0f, 0.5f, 1.0f, 2.0f})
	{
		const auto Layout = ui_widget::ResolveHorizontalSliderLayout(Rect, Value);
		EXPECT_NEAR(Layout.m_Fill.x + Layout.m_Fill.w, Layout.m_Handle.x + Layout.m_Handle.w * 0.5f, 0.001f);
		EXPECT_GE(Layout.m_Fill.w, 0.0f);
		EXPECT_LE(Layout.m_Fill.w, Layout.m_Track.w);
		EXPECT_GE(Layout.m_Handle.x, Rect.x);
		EXPECT_LE(Layout.m_Handle.x + Layout.m_Handle.w, Rect.x + Rect.w);
	}
}

TEST(SliderLayout, NarrowAndEmptyTracksKeepNonnegativeGeometry)
{
	for(const CUIRect Rect : {CUIRect{10, 20, 4, 20}, CUIRect{10, 20, 0, 20}, CUIRect{10, 20, 20, 0}})
	{
		const auto Layout = ui_widget::ResolveHorizontalSliderLayout(Rect, 0.5f);
		EXPECT_GE(Layout.m_Handle.w, 0.0f);
		EXPECT_GE(Layout.m_Track.w, 0.0f);
		EXPECT_GE(Layout.m_Track.h, 0.0f);
		EXPECT_LE(Layout.m_Handle.x + Layout.m_Handle.w, Rect.x + Rect.w);
	}
}

TEST(SliderStyle, RailAndHandleRemainVisibleOnLightAndDarkBackgrounds)
{
	SUiTheme Theme{};
	Theme.m_Accent = ColorRGBA(1, 1, 1, 1);
	const auto Dark = ResolveUiSliderStyle(Theme, ColorRGBA(0, 0, 0, 1), false, false);
	const auto Light = ResolveUiSliderStyle(Theme, ColorRGBA(1, 1, 1, 1), false, false);
	EXPECT_GT(Dark.m_Track.r, Light.m_Track.r);
	EXPECT_GT(Dark.m_Handle.r, Light.m_Handle.r);
	EXPECT_GT(Dark.m_Track.a, 0.0f);
	EXPECT_GT(Light.m_Track.a, 0.0f);
}

TEST(SliderStyle, DisabledIgnoresHoverAndPressFeedback)
{
	SUiTheme Theme{};
	Theme.m_Accent = ColorRGBA(0.2f, 0.5f, 0.8f, 0.85f);
	const ColorRGBA Backdrop(0, 0, 0, 1);
	const auto Idle = ResolveUiSliderStyle(Theme, Backdrop, false, false);
	const auto Pressed = ResolveUiSliderStyle(Theme, Backdrop, true, true);
	const auto Disabled = ResolveUiSliderStyle(Theme, Backdrop, false, false, false);
	const auto DisabledPress = ResolveUiSliderStyle(Theme, Backdrop, true, true, false);
	EXPECT_GT(Pressed.m_Border.a, Idle.m_Border.a);
	EXPECT_LT(Disabled.m_Handle.a, Idle.m_Handle.a);
	EXPECT_EQ(DisabledPress.m_Handle, Disabled.m_Handle);
	EXPECT_EQ(DisabledPress.m_Border, Disabled.m_Border);
}

TEST(SliderStyle, AccentOpacityRemainsEffectiveDuringHoverAndPress)
{
	const ColorRGBA Backdrop(0, 0, 0, 1);
	for(const float Opacity : {0.0f, 0.2f, 1.0f})
	{
		SUiTheme Theme{};
		Theme.m_Accent = ColorRGBA(0.2f, 0.5f, 0.8f, Opacity);
		const auto Idle = ResolveUiSliderStyle(Theme, Backdrop, false, false);
		const auto Hover = ResolveUiSliderStyle(Theme, Backdrop, true, false);
		const auto Press = ResolveUiSliderStyle(Theme, Backdrop, true, true);
		EXPECT_FLOAT_EQ(Idle.m_Fill.a, 0.85f * Opacity);
		EXPECT_FLOAT_EQ(Idle.m_Handle.a, Opacity);
		EXPECT_EQ(Hover.m_Fill, Idle.m_Fill);
		EXPECT_EQ(Hover.m_Handle, Idle.m_Handle);
		EXPECT_EQ(Press.m_Fill, Idle.m_Fill);
		EXPECT_EQ(Press.m_Handle, Idle.m_Handle);
		EXPECT_GT(Hover.m_Border.a, Idle.m_Border.a);
	}
}
