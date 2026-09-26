// QmAnim 行为测试：scroll。
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmAnimCurves.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/QmTree.h>
#include <game/client/QmUi/SettingsCardGeometry.h>
#include <game/client/QmUi/SettingsPageLayout.h>
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

#include <array>
#include <cmath>

namespace
{

}

TEST(UiV2ScrollPhysics, WheelImpulseDecaysAndClampsToRange)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;

	SQmScrollConfig Config;
	Config.m_WheelScale = 1.0f;
	Config.m_NativeWheelStep = false;
	Config.m_MaxOverscroll = 72.0f;

	CQmScrollState State;
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	EXPECT_GT(State.Offset(), 0.0f);
	EXPECT_GT(State.Velocity(), 0.0f);

	const float InitialOffset = State.Offset();
	State.Advance(0.2f, Metrics, Config);
	EXPECT_GT(State.Offset(), InitialOffset);
	EXPECT_LT(State.Offset(), Metrics.MaxOffset());
	EXPECT_GT(State.Velocity(), 0.0f);

	for(int i = 0; i < 240; ++i)
		State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_GE(State.Offset(), 0.0f);
	EXPECT_LE(State.Offset(), Metrics.MaxOffset());
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.5f);
}
TEST(UiV2ScrollPhysics, NativeWheelStepMatchesDdnetScrollUnit)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 10.0f;
	Config.m_NativeWheelStep = true;
	Config.m_NativeWheelAnimationTime = 0.5f;

	CQmScrollState State;
	State.AddWheelImpulse(-120.0f, Metrics, Config);

	EXPECT_NEAR(State.Offset(), 0.0f, 0.01f);
	EXPECT_NEAR(State.Velocity(), 60.0f, 0.01f);

	State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_GT(State.Offset(), 0.0f);
	EXPECT_LT(State.Offset(), 10.0f);
	EXPECT_GT(State.Velocity(), 0.0f);
	EXPECT_LT(State.Velocity(), 60.0f);

	for(int i = 0; i < 40; ++i)
		State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 10.0f, 0.01f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.01f);

	State.AddWheelImpulse(-120.0f, Metrics, Config);
	for(int i = 0; i < 40; ++i)
		State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 20.0f, 0.01f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.01f);
}
TEST(UiV2ScrollPhysics, RepeatedAndReversedWheelEventsPreservePositionAndVelocity)
{
	const SQmScrollMetrics Metrics{100.0f, 500.0f};
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.5f);
	CQmScrollState State;
	State.SetOffset(100.0f, Metrics, Config);
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.125f, Metrics, Config);
	for(const float WheelDelta : {-120.0f, 240.0f})
	{
		const float Before = State.Offset();
		const float Velocity = State.Velocity();
		ASSERT_GT(Velocity, 0.0f);
		State.AddWheelImpulse(WheelDelta, Metrics, Config);
		EXPECT_FLOAT_EQ(State.Offset(), Before);
		EXPECT_FLOAT_EQ(State.Velocity(), Velocity);
		State.Advance(0.0001f, Metrics, Config);
		EXPECT_NEAR((State.Offset() - Before) / 0.0001f, Velocity, 0.5f);
	}
	State.Advance(0.5f, Metrics, Config);
	EXPECT_FLOAT_EQ(State.Offset(), 100.0f);
	EXPECT_FLOAT_EQ(State.Velocity(), 0.0f);
	EXPECT_FALSE(State.Animating());
}

TEST(UiV2ScrollPhysics, RetargetedWheelTrajectoryDoesNotDependOnFramePartition)
{
	const SQmScrollMetrics Metrics{100.0f, 500.0f};
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.5f);
	const auto Sample = [&](int RefreshRate) {
		CQmScrollState State;
		State.SetOffset(100.0f, Metrics, Config);
		const auto AdvanceSpan = [&](float Seconds) {
			const int Frames = std::max(1, static_cast<int>(std::ceil(Seconds * RefreshRate)));
			for(int Frame = 0; Frame < Frames; ++Frame)
				State.Advance(Seconds / Frames, Metrics, Config);
		};
		State.AddWheelImpulse(-120.0f, Metrics, Config);
		AdvanceSpan(0.125f);
		State.AddWheelImpulse(-120.0f, Metrics, Config);
		AdvanceSpan(0.125f);
		State.AddWheelImpulse(240.0f, Metrics, Config);
		AdvanceSpan(0.15f);
		return std::array<float, 2>{State.Offset(), State.Velocity()};
	};
	const auto Expected = Sample(1);
	for(const int RefreshRate : {60, 144, 240, 360})
	{
		SCOPED_TRACE(RefreshRate);
		const auto Actual = Sample(RefreshRate);
		EXPECT_NEAR(Actual[0], Expected[0], 0.001f);
		EXPECT_NEAR(Actual[1], Expected[1], 0.01f);
	}
}

TEST(UiV2ScrollPhysics, WheelBoundsInstantModeAndDirectDragStopMomentum)
{
	const SQmScrollMetrics Metrics{100.0f, 500.0f};
	SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.5f);
	CQmScrollState State;
	State.SetOffset(395.0f, Metrics, Config);
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.1f, Metrics, Config);
	State.AddWheelImpulse(-1200.0f, Metrics, Config);
	for(int Frame = 0; Frame < 180; ++Frame)
	{
		State.Advance(1.0f / 360.0f, Metrics, Config);
		EXPECT_GE(State.Offset(), 0.0f);
		EXPECT_LE(State.Offset(), Metrics.MaxOffset());
		if(State.Offset() == Metrics.MaxOffset())
			EXPECT_FLOAT_EQ(State.Velocity(), 0.0f);
	}
	State.Advance(1.0f, Metrics, Config);
	EXPECT_FLOAT_EQ(State.Offset(), 400.0f);
	EXPECT_FLOAT_EQ(State.Velocity(), 0.0f);
	State.AddWheelImpulse(120.0f, Metrics, Config);
	State.Advance(0.1f, Metrics, Config);
	State.SetOffset(150.0f, Metrics, Config);
	State.Advance(0.1f, Metrics, Config);
	EXPECT_FLOAT_EQ(State.Offset(), 150.0f);
	EXPECT_FLOAT_EQ(State.Velocity(), 0.0f);
	EXPECT_FALSE(State.Animating());
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.1f, Metrics, Config);
	Config.m_NativeWheelAnimationTime = 0.0f;
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	EXPECT_FLOAT_EQ(State.Offset(), 170.0f);
	EXPECT_FLOAT_EQ(State.Velocity(), 0.0f);
	EXPECT_FALSE(State.Animating());
}

TEST(UiV2ScrollPhysics, NativeWheelStepPreservesWheelMagnitude)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.0f);

	CQmScrollState State;
	State.AddWheelImpulse(-360.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 30.0f, 0.01f);

	State.AddWheelImpulse(240.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 10.0f, 0.01f);
}
TEST(UiV2WheelOwnership, AltMagnitudeReachesNativeScrollStateOnce)
{
	CScrollWheelOwnership Router;
	ASSERT_TRUE(Router.BeginFrame(41, -120.0f, true));
	CQmScrollState State;
	Router.Register(&State, EUiWheelOwnerPriority::PAGE, true);
	float Delta = 0.0f;
	ASSERT_TRUE(Router.TryConsume(&State, &Delta));
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	State.AddWheelImpulse(Delta, Metrics, QmNativeWheelScrollConfig(1.0f, 0.0f));
	EXPECT_NEAR(State.Offset(), 30.0f, 0.01f);
	EXPECT_FALSE(Router.TryConsume(&State, &Delta));
}
TEST(UiV2ScrollPhysics, NativeWheelAnimationMatchesScrollRegionEaseOut)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 10.0f;
	Config.m_NativeWheelStep = true;
	Config.m_NativeWheelAnimationTime = 0.5f;

	CQmScrollState State;
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.125f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 5.78125f, 0.001f);

	State.Advance(0.125f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 8.75f, 0.001f);

	State.Advance(0.25f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 10.0f, 0.001f);

	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.25f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 18.75f, 0.001f);
}
TEST(UiV2ScrollPhysics, NativeWheelAnimationPausesWhileModifierIsPressed)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 10.0f;
	Config.m_NativeWheelStep = true;
	Config.m_NativeWheelAnimationTime = 0.5f;

	CQmScrollState State;
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.125f, Metrics, Config);
	const float OffsetBeforeModifier = State.Offset();

	State.Advance(0.25f, Metrics, Config, true);
	EXPECT_NEAR(State.Offset(), OffsetBeforeModifier, 0.001f);

	State.Advance(0.125f, Metrics, Config);
	State.Advance(0.25f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 10.0f, 0.001f);
}
TEST(UiV2ScrollPhysics, NativeWheelAnimationConsumesLargeFrameDeltaLikeScrollRegion)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 10.0f;
	Config.m_NativeWheelStep = true;
	Config.m_NativeWheelAnimationTime = 0.5f;

	CQmScrollState State;
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.5f, Metrics, Config);

	EXPECT_NEAR(State.Offset(), 10.0f, 0.001f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.01f);
}
TEST(UiV2ScrollContainer, ModifierDoesNotGloballySuppressWheelAndAltAcceleratesIt)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_ModifierPressed = true;
	Input.m_WheelDelta = -120.0f;
	const SQmScrollContainerFrame ModifierFrame = Container.Update(State, View, 300.0f, 1.0f / 60.0f, Input);
	EXPECT_NEAR(ModifierFrame.m_Offset, 10.0f, 0.001f);

	Input.m_ModifierPressed = false;
	Input.m_AltPressed = true;
	const SQmScrollContainerFrame WheelFrame = Container.Update(State, View, 300.0f, 0.5f, Input);
	EXPECT_NEAR(WheelFrame.m_Offset, 40.0f, 0.001f);

	Input.m_WheelDelta = 0.0f;
	const SQmScrollContainerFrame DoneFrame = Container.Update(State, View, 300.0f, 0.5f, Input);
	EXPECT_NEAR(DoneFrame.m_Offset, 40.0f, 0.001f);
}
TEST(UiV2ScrollOwnership, PopupConsumesWheelWithoutLeakingToUnderlyingRegion)
{
	EXPECT_FALSE(QmScrollRegionCanConsumeWheel(true, false, true, false));
	EXPECT_TRUE(QmScrollRegionCanConsumeWheel(false, true, true, true));
	EXPECT_TRUE(QmScrollRegionCanConsumeWheel(false, true, false, false));
	EXPECT_TRUE(QmScrollRegionCanConsumeWheel(true, false, false, false));
}
TEST(UiV2WheelOwnership, HighestEligibleOwnerConsumesRawWheelOnce)
{
	CScrollWheelOwnership Router;
	ASSERT_TRUE(Router.BeginFrame(41, -120.0f, false));
	Router.Register(reinterpret_cast<void *>(1), EUiWheelOwnerPriority::PAGE, true);
	Router.Register(reinterpret_cast<void *>(2), EUiWheelOwnerPriority::POPUP, true);
	float Delta = 0.0f;
	EXPECT_FALSE(Router.TryConsume(reinterpret_cast<void *>(1), &Delta));
	EXPECT_TRUE(Router.TryConsume(reinterpret_cast<void *>(2), &Delta));
	EXPECT_FLOAT_EQ(Delta, -120.0f);
	EXPECT_FALSE(Router.TryConsume(reinterpret_cast<void *>(2), &Delta));
}
TEST(UiV2WheelOwnership, AltAcceleratesButOtherModifiersDoNotDiscardWheel)
{
	CScrollWheelOwnership Router;
	ASSERT_TRUE(Router.BeginFrame(41, -120.0f, true));
	Router.Register(reinterpret_cast<void *>(1), EUiWheelOwnerPriority::PAGE, true);
	float Delta = 0.0f;
	ASSERT_TRUE(Router.TryConsume(reinterpret_cast<void *>(1), &Delta));
	EXPECT_FLOAT_EQ(Delta, -360.0f);
}
TEST(UiV2WheelOwnership, LaterEqualPriorityOwnerWinsAndIneligibleOwnerCannotWin)
{
	CScrollWheelOwnership Router;
	int Outer = 0;
	int Inner = 0;
	int Disabled = 0;
	ASSERT_TRUE(Router.BeginFrame(41, 120.0f, false));
	Router.Register(&Outer, EUiWheelOwnerPriority::COMPOSITE_CONTROL, true);
	Router.Register(&Disabled, EUiWheelOwnerPriority::POPUP, false);
	Router.Register(&Inner, EUiWheelOwnerPriority::COMPOSITE_CONTROL, true);
	float Delta = 0.0f;
	EXPECT_FALSE(Router.TryConsume(&Outer, &Delta));
	EXPECT_FALSE(Router.TryConsume(&Disabled, &Delta));
	EXPECT_TRUE(Router.TryConsume(&Inner, &Delta));
}
TEST(UiV2WheelOwnership, BeginFrameIsIdempotentForOneProductionFrame)
{
	CScrollWheelOwnership Router;
	int Popup = 0;
	ASSERT_TRUE(Router.BeginFrame(41, -120.0f, false));
	Router.Register(&Popup, EUiWheelOwnerPriority::POPUP, true);
	EXPECT_FALSE(Router.BeginFrame(41, 120.0f, true));
	float Delta = 0.0f;
	ASSERT_TRUE(Router.TryConsume(&Popup, &Delta));
	EXPECT_FLOAT_EQ(Delta, -120.0f);
	EXPECT_TRUE(Router.BeginFrame(42, 120.0f, false));
	EXPECT_FALSE(Router.TryConsume(&Popup, &Delta));
}
TEST(UiV2WheelOwnership, CandidateOutsideHotRectCannotConsumeWheel)
{
	CScrollWheelOwnership Router;
	int Owner = 0;
	ASSERT_TRUE(Router.BeginFrame(41, -120.0f, false));
	QmRegisterWheelOwnerCandidate(Router, {&Owner, EUiWheelOwnerPriority::PAGE, {10.0f, 10.0f, 30.0f, 30.0f}, true}, {50.0f, 50.0f}, true);
	float Delta = 0.0f;
	EXPECT_FALSE(QmTryConsumeWheel(Router, &Owner, &Delta));
}
TEST(UiV2ScrollPolicy, ListBoxExplicitScrollbarMetricsOverridePolicyDefaults)
{
	EXPECT_NEAR(QmListBoxScrollbarMetric(20.0f, 15.0f, false), 20.0f, 0.001f);
	EXPECT_NEAR(QmListBoxScrollbarMetric(20.0f, 15.0f, true), 15.0f, 0.001f);
}
TEST(UiV2ScrollPolicy, ScrollbarReservationPreventsNestedListWidthJitter)
{
	EXPECT_FALSE(QmScrollRegionShouldReserveScrollbarSpace(false, false));
	EXPECT_TRUE(QmScrollRegionShouldReserveScrollbarSpace(false, true));
	EXPECT_TRUE(QmScrollRegionShouldReserveScrollbarSpace(true, false));
	EXPECT_TRUE(QmScrollRegionShouldReserveScrollbarSpace(true, true));
}
TEST(UiV2ScrollPolicy, ScrollbarOverflowIgnoresSubpixelLayoutRemainder)
{
	EXPECT_FALSE(QmScrollRegionContentOverflows(100.0f, 100.0f, 0.5f));
	EXPECT_FALSE(QmScrollRegionContentOverflows(100.125f, 100.0f, 0.5f));
	EXPECT_TRUE(QmScrollRegionContentOverflows(100.126f, 100.0f, 0.5f));
	EXPECT_FALSE(QmScrollRegionContentOverflows(100.0625f, 100.0f, 0.25f));
	EXPECT_TRUE(QmScrollRegionContentOverflows(100.0626f, 100.0f, 0.25f));
}
TEST(UiV2ScrollPolicy, ListBoxInitialSelectionRequestsOneAnimatedReveal)
{
	EXPECT_TRUE(QmListBoxShouldScrollToInitialSelection(true, 4));
	EXPECT_FALSE(QmListBoxShouldScrollToInitialSelection(true, -1));
	EXPECT_FALSE(QmListBoxShouldScrollToInitialSelection(false, 4));
	EXPECT_TRUE(QmListBoxInitialScrollRemainsPending(true, -1));
	EXPECT_FALSE(QmListBoxInitialScrollRemainsPending(true, 4));
	EXPECT_FALSE(QmListBoxInitialScrollRemainsPending(false, -1));
}
TEST(UiV2ScrollPolicy, ListBoxEntryAnimationStartsAfterAnInactiveGap)
{
	EXPECT_TRUE(QmListBoxShouldStartEntryAnimation(true, false, 0, 1000, 400));
	EXPECT_TRUE(QmListBoxShouldStartEntryAnimation(true, false, 500, 1000, 400));
	EXPECT_FALSE(QmListBoxShouldStartEntryAnimation(true, false, 600, 1000, 400));
	EXPECT_FALSE(QmListBoxShouldStartEntryAnimation(false, false, 0, 1000, 400));
	EXPECT_FALSE(QmListBoxShouldStartEntryAnimation(true, true, 0, 1000, 400));
	EXPECT_NEAR(QmListBoxEntryOffset(0.0f, 0.16f, 12.0f), -12.0f, 0.001f);
	EXPECT_LT(QmListBoxEntryOffset(0.08f, 0.16f, 12.0f), 0.0f);
	EXPECT_NEAR(QmListBoxEntryOffset(0.16f, 0.16f, 12.0f), 0.0f, 0.001f);
	EXPECT_FALSE(QmListBoxEntryAnimationFinished(true, false, 0.08f, 0.16f));
	EXPECT_TRUE(QmListBoxEntryAnimationFinished(true, false, 0.16f, 0.16f));
	EXPECT_TRUE(QmListBoxEntryAnimationFinished(true, false, 0.20f, 0.16f));
	EXPECT_TRUE(QmListBoxEntryAnimationFinished(false, false, 0.0f, 0.16f));
	EXPECT_FALSE(QmListBoxEntryAnimationFinished(true, true, 0.20f, 0.16f));
	const CUIRect BaseRect{10.0f, 20.0f, 100.0f, 18.0f};
	const CUIRect AnimatedRect = QmListBoxEntryAnimatedRect(BaseRect, -12.0f);
	EXPECT_FLOAT_EQ(AnimatedRect.x, BaseRect.x);
	EXPECT_FLOAT_EQ(AnimatedRect.y, 8.0f);
	EXPECT_FLOAT_EQ(AnimatedRect.w, BaseRect.w);
	EXPECT_FLOAT_EQ(AnimatedRect.h, BaseRect.h);
}
TEST(UiV2ScrollPolicy, ResolvesSharedVisualAndInteractionProfiles)
{
	SQmScrollRequest Settings;
	Settings.m_Profile = EQmScrollProfile::SETTINGS_OUTER;
	const SQmResolvedScrollPolicy SettingsPolicy = QmResolveScrollPolicy(Settings, 1.0f, 0.5f);
	EXPECT_NEAR(SettingsPolicy.m_Style.m_ScrollbarWidth, 20.0f, 0.01f);
	EXPECT_NEAR(SettingsPolicy.m_Style.m_ScrollbarMargin, 5.0f, 0.01f);
	EXPECT_TRUE(SettingsPolicy.m_Style.m_ReserveScrollbarSpace);
	EXPECT_TRUE(SettingsPolicy.m_ScrollbarAlwaysReserved);
	EXPECT_NEAR(SettingsPolicy.m_Config.m_WheelScale, 120.0f, 0.01f);
	EXPECT_NEAR(SettingsPolicy.m_AltMultiplier, 3.0f, 0.01f);
	EXPECT_EQ(SettingsPolicy.m_RailVisibility, EQmScrollRailVisibility::AUTO);

	SQmScrollRequest FilterGrid;
	FilterGrid.m_Profile = EQmScrollProfile::FILTER_GRID;
	FilterGrid.m_RowExtent = 18.0f;
	const SQmResolvedScrollPolicy FilterPolicy = QmResolveScrollPolicy(FilterGrid, 1.0f, 0.0f);
	EXPECT_EQ(FilterPolicy.m_RailVisibility, EQmScrollRailVisibility::HIDDEN);
	EXPECT_NEAR(FilterPolicy.m_Config.m_WheelScale, 36.0f, 0.01f);
	EXPECT_FALSE(FilterPolicy.m_ContentDragAllowed);

	SQmScrollRequest Grid;
	Grid.m_Profile = EQmScrollProfile::SETTINGS_GRID;
	Grid.m_RowExtent = 18.0f;
	Grid.m_RowsPerStep = 1;
	const SQmResolvedScrollPolicy GridPolicy = QmResolveScrollPolicy(Grid, 1.0f, 0.0f);
	EXPECT_EQ(GridPolicy.m_RailVisibility, EQmScrollRailVisibility::AUTO);
	EXPECT_NEAR(GridPolicy.m_Config.m_WheelScale, 18.0f, 0.01f);
	EXPECT_FALSE(GridPolicy.m_ContentDragAllowed);

	SQmScrollRequest Popup;
	Popup.m_Profile = EQmScrollProfile::POPUP_LIST;
	Popup.m_RowExtent = 20.0f;
	const SQmResolvedScrollPolicy PopupPolicy = QmResolveScrollPolicy(Popup, 1.0f, 0.0f);
	EXPECT_EQ(PopupPolicy.m_MaxVisibleItems, QM_POPUP_LIST_MAX_VISIBLE_ITEMS);
	EXPECT_NEAR(PopupPolicy.m_Config.m_WheelScale, 60.0f, 0.01f);
}
TEST(UiV2ScrollController, SettingsGridShowsRailOnlyWhenContentOverflows)
{
	const SQmResolvedScrollPolicy Policy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_GRID, EQmScrollAxis::VERTICAL, 40.0f, 2});
	CQmScrollController Controller;
	CQmScrollState State;
	const CUIRect View{0.0f, 0.0f, 200.0f, 100.0f};
	const SQmScrollContainerFrame Fits = Controller.PreviewFrame(State, View, 100.0f, Policy.m_Style);
	EXPECT_FALSE(Fits.m_Scrollable);
	EXPECT_FALSE(Fits.m_ScrollbarVisible);
	EXPECT_FLOAT_EQ(Fits.m_ClipRect.w, View.w);

	const SQmScrollContainerFrame Overflows = Controller.PreviewFrame(State, View, 101.0f, Policy.m_Style);
	EXPECT_TRUE(Overflows.m_Scrollable);
	EXPECT_TRUE(Overflows.m_ScrollbarVisible);
	EXPECT_LT(Overflows.m_ClipRect.w, View.w);
	EXPECT_EQ(Policy.m_RailVisibility, EQmScrollRailVisibility::AUTO);
	EXPECT_FALSE(Policy.m_ContentDragAllowed);
}
TEST(UiV2ScrollController, SettingsOuterReservesSlotBeforeOverflowIsKnown)
{
	const SQmResolvedScrollPolicy Policy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER});
	CQmScrollController Controller;
	CQmScrollState State;
	const SQmScrollContainerFrame Frame = Controller.PreviewFrame(State, {0.0f, 0.0f, 200.0f, 100.0f}, 100.0f, Policy.m_Style);
	EXPECT_FALSE(Frame.m_Scrollable);
	EXPECT_FALSE(Frame.m_ScrollbarVisible);
	EXPECT_FLOAT_EQ(Frame.m_ClipRect.w, 180.0f);
}
TEST(UiV2ScrollPolicy, NonCardMenuListAndFilterGridUseResolvedSteps)
{
	SQmScrollRequest ListRequest;
	ListRequest.m_Profile = EQmScrollProfile::MENU_LIST;
	ListRequest.m_RowExtent = 24.0f;
	ListRequest.m_RowsPerStep = 3;
	const SQmResolvedScrollPolicy List = QmResolveScrollPolicy(ListRequest, 1.0f, 0.0f);
	EXPECT_FLOAT_EQ(List.m_Config.m_WheelScale, 72.0f);
	EXPECT_EQ(List.m_RailVisibility, EQmScrollRailVisibility::AUTO);
	EXPECT_FLOAT_EQ(List.m_AltMultiplier, 3.0f);

	SQmScrollRequest GridRequest;
	GridRequest.m_Profile = EQmScrollProfile::FILTER_GRID;
	GridRequest.m_RowExtent = 30.0f;
	GridRequest.m_RowsPerStep = 2;
	const SQmResolvedScrollPolicy Grid = QmResolveScrollPolicy(GridRequest, 1.0f, 0.0f);
	EXPECT_FLOAT_EQ(Grid.m_Config.m_WheelScale, 60.0f);
	EXPECT_EQ(Grid.m_RailVisibility, EQmScrollRailVisibility::HIDDEN);
	EXPECT_FALSE(Grid.m_ContentDragAllowed);
}
TEST(UiV2ScrollPolicy, FinalPresetMatrixCoversLargeMediumSmallAndHorizontal)
{
	const SQmResolvedScrollPolicy Outer = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER, EQmScrollAxis::VERTICAL, 0.0f, 0}, 0.78f, 0.12f);
	const SQmResolvedScrollPolicy Medium = QmResolveScrollPolicy({EQmScrollProfile::MENU_LIST, EQmScrollAxis::VERTICAL, 24.0f, 3}, 0.78f, 0.0f);
	const SQmResolvedScrollPolicy Small = QmResolveScrollPolicy({EQmScrollProfile::POPUP_LIST, EQmScrollAxis::VERTICAL, 24.0f, 1}, 1.0f, 0.0f);
	const SQmResolvedScrollPolicy Horizontal = QmResolveScrollPolicy({EQmScrollProfile::POPUP_LIST, EQmScrollAxis::HORIZONTAL, 24.0f, 1}, 1.0f, 0.0f);

	EXPECT_FLOAT_EQ(Outer.m_Style.m_ScrollbarWidth, 20.0f);
	EXPECT_FLOAT_EQ(Outer.m_Style.m_ScrollbarMargin, 5.0f);
	EXPECT_GT(Outer.m_Style.m_ScrollbarWidth, Medium.m_Style.m_ScrollbarWidth);
	EXPECT_GT(Medium.m_Style.m_ScrollbarWidth, Small.m_Style.m_ScrollbarWidth);
	EXPECT_EQ(Horizontal.m_Style.m_Axis, EQmScrollAxis::HORIZONTAL);
	EXPECT_EQ(Small.m_MaxVisibleItems, 8);
	EXPECT_FLOAT_EQ(Medium.m_AltMultiplier, 3.0f);
}
TEST(UiV2ScrollController, HiddenRailKeepsScrollableContentAtFullWidth)
{
	CQmScrollState State;
	CQmScrollController Controller;
	SQmScrollRequest Request;
	Request.m_Profile = EQmScrollProfile::FILTER_GRID;
	Request.m_RowExtent = 20.0f;
	CUIRect View{10.0f, 20.0f, 200.0f, 100.0f};
	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_WheelDelta = -120.0f;
	const SQmScrollContainerFrame Frame = Controller.Update(State, View, 300.0f, 0.0f, Input, Request, 1.0f, 0.0f);
	EXPECT_TRUE(Frame.m_Scrollable);
	EXPECT_FALSE(Frame.m_ScrollbarVisible);
	EXPECT_NEAR(Frame.m_ClipRect.w, View.w, 0.001f);
	EXPECT_GT(Frame.m_Offset, 0.0f);
}
TEST(UiV2ScrollPhysics, PresetsExposeSharedSmallMediumLargeGeometry)
{
	const SQmScrollContainerStyle Small = QmScrollContainerStyleForSize(EQmScrollSize::SMALL, 1.0f);
	EXPECT_NEAR(Small.m_ScrollbarWidth, 10.0f, 0.01f);
	EXPECT_NEAR(Small.m_ScrollbarMargin, 2.0f, 0.01f);
	EXPECT_NEAR(Small.m_MinThumbHeight, 36.0f, 0.01f);

	const SQmScrollContainerStyle Medium = QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, 1.0f);
	EXPECT_NEAR(Medium.m_ScrollbarWidth, 20.0f, 0.01f);
	EXPECT_NEAR(Medium.m_ScrollbarMargin, 5.0f, 0.01f);
	EXPECT_NEAR(Medium.m_MinThumbHeight, 42.0f, 0.01f);

	const SQmScrollContainerStyle Large = QmScrollContainerStyleForSize(EQmScrollSize::LARGE, 1.0f);
	EXPECT_NEAR(Large.m_ScrollbarWidth, 28.0f, 0.01f);
	EXPECT_NEAR(Large.m_ScrollbarMargin, 8.0f, 0.01f);
	EXPECT_NEAR(Large.m_MinThumbHeight, 48.0f, 0.01f);

	const SQmScrollConfig NativeWheel = QmNativeWheelScrollConfig(1.0f, 0.5f);
	EXPECT_NEAR(NativeWheel.m_WheelScale, 10.0f, 0.01f);
	EXPECT_TRUE(NativeWheel.m_NativeWheelStep);
	EXPECT_NEAR(NativeWheel.m_NativeWheelAnimationTime, 0.5f, 0.01f);
	EXPECT_NEAR(NativeWheel.m_MaxOverscroll, 0.0f, 0.01f);

	const SQmScrollConfig ScaledNativeWheel = QmNativeWheelScrollConfig(2.0f, 0.5f);
	EXPECT_NEAR(ScaledNativeWheel.m_WheelScale, 10.0f, 0.01f);

	const SQmScrollConfig InstantNativeWheel = QmNativeWheelScrollConfig(1.0f, 0.0f);
	EXPECT_NEAR(InstantNativeWheel.m_NativeWheelAnimationTime, 0.0f, 0.01f);

	const SQmScrollConfig SettingsWheel = QmSettingsScrollConfig(1.0f, 0.5f);
	EXPECT_NEAR(SettingsWheel.m_WheelScale, 120.0f, 0.01f);
	EXPECT_TRUE(SettingsWheel.m_NativeWheelStep);
	EXPECT_NEAR(SettingsWheel.m_NativeWheelAnimationTime, 0.5f, 0.01f);
}
TEST(UiV2ScrollPhysics, ScrollRegionParamsUseSharedQmScrollPreset)
{
	const SQmScrollContainerStyle Medium = QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, 1.0f);
	const CScrollRegionParams Params = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM, 1.0f);
	const CScrollRegionParams DefaultParams;

	EXPECT_NEAR(Params.m_ScrollbarThickness, Medium.m_ScrollbarWidth, 0.01f);
	EXPECT_NEAR(Params.m_ScrollbarMargin, Medium.m_ScrollbarMargin, 0.01f);
	EXPECT_NEAR(Params.m_SliderMinSize, Medium.m_MinThumbHeight, 0.01f);
	EXPECT_NEAR(Params.m_ScrollUnit, QmNativeWheelScrollConfig(1.0f, 0.0f).m_WheelScale, 0.01f);
	EXPECT_FALSE(Params.m_ScrollHorizontal);
	EXPECT_NEAR(DefaultParams.m_ScrollbarThickness, Medium.m_ScrollbarWidth, 0.01f);
	EXPECT_NEAR(DefaultParams.m_ScrollbarMargin, Medium.m_ScrollbarMargin, 0.01f);
	EXPECT_NEAR(DefaultParams.m_SliderMinSize, 25.0f, 0.01f);
	EXPECT_NEAR(DefaultParams.m_ScrollUnit, QmNativeWheelScrollConfig(1.0f, 0.0f).m_WheelScale, 0.01f);

	const CScrollRegionParams Horizontal = QmScrollRegionParamsForSize(EQmScrollSize::SMALL, 1.0f, EQmScrollAxis::HORIZONTAL);
	EXPECT_TRUE(Horizontal.m_ScrollHorizontal);
	EXPECT_NEAR(Horizontal.m_ScrollbarThickness, QmScrollContainerStyleForSize(EQmScrollSize::SMALL, 1.0f).m_ScrollbarWidth, 0.01f);
}
TEST(UiV2ScrollPhysics, OverscrollSpringsBackIntoRange)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 300.0f;
	SQmScrollConfig Config;
	Config.m_NativeWheelStep = false;
	Config.m_MaxOverscroll = 72.0f;

	CQmScrollState State;
	State.SetOffset(Metrics.MaxOffset() + 40.0f, Metrics, Config, true);

	State.Advance(0.1f, Metrics, Config);
	EXPECT_GT(State.Offset(), Metrics.MaxOffset());
	EXPECT_LT(State.Offset(), Metrics.MaxOffset() + 40.0f);

	for(int i = 0; i < 240; ++i)
		State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), Metrics.MaxOffset(), 0.75f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.75f);
}
TEST(UiV2ScrollPhysics, NonScrollableContentResetsState)
{
	SQmScrollMetrics ScrollableMetrics;
	ScrollableMetrics.m_ViewportSize = 100.0f;
	ScrollableMetrics.m_ContentSize = 500.0f;
	SQmScrollMetrics NonScrollableMetrics;
	NonScrollableMetrics.m_ViewportSize = 300.0f;
	NonScrollableMetrics.m_ContentSize = 120.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 1.0f;
	Config.m_NativeWheelStep = false;
	Config.m_MaxOverscroll = 72.0f;

	CQmScrollState State;
	State.SetOffset(80.0f, ScrollableMetrics);
	State.AddWheelImpulse(-120.0f, ScrollableMetrics, Config);
	EXPECT_GT(State.Offset(), 0.0f);
	EXPECT_GT(State.Velocity(), 0.0f);

	State.Advance(0.0f, NonScrollableMetrics, Config);
	EXPECT_NEAR(State.Offset(), 0.0f, 1e-6f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 1e-6f);

	State.SetOffset(80.0f, ScrollableMetrics);
	State.AddWheelImpulse(-120.0f, ScrollableMetrics, Config);
	EXPECT_GT(State.Offset(), 0.0f);
	EXPECT_GT(State.Velocity(), 0.0f);

	State.Advance(0.2f, NonScrollableMetrics, Config);
	EXPECT_NEAR(State.Offset(), 0.0f, 1e-6f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 1e-6f);
}
TEST(UiV2ScrollPhysics, ShrinkingScrollableContentClampsOffsetToNewRange)
{
	SQmScrollMetrics TallMetrics;
	TallMetrics.m_ViewportSize = 100.0f;
	TallMetrics.m_ContentSize = 500.0f;
	SQmScrollMetrics ShortMetrics;
	ShortMetrics.m_ViewportSize = 100.0f;
	ShortMetrics.m_ContentSize = 220.0f;

	CQmScrollState State;
	State.SetOffset(400.0f, TallMetrics);
	EXPECT_NEAR(State.Offset(), TallMetrics.MaxOffset(), 1e-6f);

	State.Advance(0.0f, ShortMetrics);
	EXPECT_NEAR(State.Offset(), ShortMetrics.MaxOffset(), 1e-6f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 1e-6f);

	State.SetOffset(400.0f, TallMetrics);
	State.AddWheelImpulse(-120.0f, TallMetrics);
	EXPECT_GT(State.Offset(), ShortMetrics.MaxOffset());

	State.Advance(1.0f / 60.0f, ShortMetrics);
	EXPECT_LE(State.Offset(), ShortMetrics.MaxOffset());
}
TEST(UiV2ScrollContainer, ComputesContentRectAndScrollbarVisibility)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	Container.ScrollByWheel(State, -120.0f, View.h, 300.0f);
	const SQmScrollContainerFrame Frame = Container.Update(State, View, 300.0f, 1.0f / 60.0f);

	EXPECT_TRUE(Frame.m_ScrollbarVisible);
	EXPECT_NEAR(Frame.m_ClipRect.x, View.x, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.y, View.y, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.w, View.w - SQmScrollContainerStyle().m_ScrollbarWidth, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.h, View.h, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.x, View.x, 1e-6f);
	EXPECT_LT(Frame.m_ContentRect.y, View.y);
	EXPECT_NEAR(Frame.m_ContentRect.w, View.w - SQmScrollContainerStyle().m_ScrollbarWidth, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.h, 300.0f, 1e-6f);
	EXPECT_GT(Frame.m_Offset, 0.0f);

	State.Reset();
	const SQmScrollContainerFrame NonOverflowFrame = Container.Update(State, View, 80.0f, 0.0f);
	EXPECT_FALSE(NonOverflowFrame.m_ScrollbarVisible);
	EXPECT_NEAR(NonOverflowFrame.m_ClipRect.w, View.w, 1e-6f);
}
TEST(UiV2ScrollContainer, DefaultWheelInputUsesDdnetNativeStep)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	Container.ScrollByWheel(State, -120.0f, View.h, 300.0f);
	const SQmScrollContainerFrame FirstFrame = Container.Update(State, View, 300.0f, 0.0f);
	EXPECT_NEAR(FirstFrame.m_Offset, 10.0f, 0.001f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.001f);
}
TEST(UiV2ScrollContainer, ExplicitDdnetSmoothTimeUsesEaseOutStep)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.5f);

	Container.ScrollByWheel(State, -120.0f, View.h, 300.0f, Config);
	const SQmScrollContainerFrame FirstFrame = Container.Update(State, View, 300.0f, 0.0f, Config);
	EXPECT_NEAR(FirstFrame.m_Offset, 0.0f, 0.001f);

	const SQmScrollContainerFrame HalfFrame = Container.Update(State, View, 300.0f, 0.25f, Config);
	EXPECT_NEAR(HalfFrame.m_Offset, 8.75f, 0.001f);

	const SQmScrollContainerFrame DoneFrame = Container.Update(State, View, 300.0f, 0.25f, Config);
	EXPECT_NEAR(DoneFrame.m_Offset, 10.0f, 0.001f);

	Container.ScrollByWheel(State, -360.0f, View.h, 300.0f, Config);
	Container.Update(State, View, 300.0f, 0.5f, Config);
	Container.Update(State, View, 300.0f, 0.125f, Config);
	Container.Update(State, View, 300.0f, 0.25f, Config);
	Container.Update(State, View, 300.0f, 0.25f, Config);
	EXPECT_NEAR(State.Offset(), 40.0f, 0.001f);
	EXPECT_NEAR(State.Velocity(), 0.0f, 0.001f);
}
TEST(UiV2ScrollContainer, NonScrollableContentKeepsContentAtViewOrigin)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 4.0f;
	View.y = 8.0f;
	View.w = 180.0f;
	View.h = 120.0f;

	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.0f);
	Container.ScrollByWheel(State, -120.0f, View.h, 400.0f, Config);
	EXPECT_GT(State.Offset(), 0.0f);

	const SQmScrollContainerFrame Frame = Container.Update(State, View, 80.0f, 0.0f, Config);
	EXPECT_FALSE(Frame.m_ScrollbarVisible);
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.x, View.x, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.y, View.y, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.w, View.w, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.h, 80.0f, 1e-6f);
	EXPECT_NEAR(State.Offset(), 0.0f, 1e-6f);
}
TEST(UiV2ScrollContainer, WheelInputOnlyMovesWhenHovered)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = false;
	Input.m_WheelDelta = -120.0f;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 300.0f, 1.0f / 60.0f, Input);
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_Hovered = true;
	Frame = Container.Update(State, View, 300.0f, 1.0f / 60.0f, Input);
	EXPECT_GT(Frame.m_Offset, 0.0f);
	EXPECT_LT(Frame.m_ContentRect.y, View.y);
}
TEST(UiV2ScrollContainer, ComputesScrollbarTrackAndThumbGeometry)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_WheelDelta = -120.0f;
	const SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 1.0f / 60.0f, Input, Style);

	EXPECT_TRUE(Frame.m_ScrollbarVisible);
	EXPECT_NEAR(Frame.m_ClipRect.w, View.w - Style.m_ScrollbarWidth, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.x, View.x + View.w - Style.m_ScrollbarWidth + Style.m_ScrollbarMargin, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.y, View.y + Style.m_ScrollbarMargin, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.w, Style.m_ScrollbarWidth - Style.m_ScrollbarMargin * 2.0f, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.h, View.h - Style.m_ScrollbarMargin * 2.0f, 1e-6f);
	EXPECT_GE(Frame.m_ScrollbarThumbRect.h, Style.m_MinThumbHeight);
	EXPECT_GE(Frame.m_ScrollbarThumbRect.y, Frame.m_ScrollbarTrackRect.y);
	EXPECT_LE(Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h, Frame.m_ScrollbarTrackRect.y + Frame.m_ScrollbarTrackRect.h);
}
TEST(UiV2ScrollContainer, ComputesHorizontalContentRectAndScrollbarGeometry)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_Axis = EQmScrollAxis::HORIZONTAL;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_WheelDelta = -120.0f;
	const SQmScrollContainerFrame Frame = Container.Update(State, View, 500.0f, 1.0f / 60.0f, Input, Style);

	EXPECT_TRUE(Frame.m_ScrollbarVisible);
	EXPECT_NEAR(Frame.m_ClipRect.x, View.x, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.y, View.y, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.w, View.w, 1e-6f);
	EXPECT_NEAR(Frame.m_ClipRect.h, View.h - Style.m_ScrollbarWidth, 1e-6f);
	EXPECT_LT(Frame.m_ContentRect.x, View.x);
	EXPECT_NEAR(Frame.m_ContentRect.y, View.y, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.w, 500.0f, 1e-6f);
	EXPECT_NEAR(Frame.m_ContentRect.h, View.h - Style.m_ScrollbarWidth, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.x, View.x + Style.m_ScrollbarMargin, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.y, View.y + View.h - Style.m_ScrollbarWidth + Style.m_ScrollbarMargin, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.w, View.w - Style.m_ScrollbarMargin * 2.0f, 1e-6f);
	EXPECT_NEAR(Frame.m_ScrollbarTrackRect.h, Style.m_ScrollbarWidth - Style.m_ScrollbarMargin * 2.0f, 1e-6f);
	EXPECT_GE(Frame.m_ScrollbarThumbRect.w, Style.m_MinThumbHeight);
	EXPECT_GE(Frame.m_ScrollbarThumbRect.x, Frame.m_ScrollbarTrackRect.x);
	EXPECT_LE(Frame.m_ScrollbarThumbRect.x + Frame.m_ScrollbarThumbRect.w, Frame.m_ScrollbarTrackRect.x + Frame.m_ScrollbarTrackRect.w);
}
TEST(UiV2ScrollContainer, OverscrollKeepsScrollbarThumbInsideTrack)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_WheelDelta = 1000.0f;
	SQmScrollConfig Config;
	Config.m_WheelScale = 1.0f;
	Config.m_NativeWheelStep = false;
	Config.m_MaxOverscroll = 72.0f;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 1.0f / 60.0f, Input, Style, Config);
	EXPECT_LT(Frame.m_Offset, 0.0f);
	EXPECT_GE(Frame.m_ScrollbarThumbRect.y, Frame.m_ScrollbarTrackRect.y);

	State.Reset();
	Input.m_WheelDelta = -100000.0f;
	Frame = Container.Update(State, View, 400.0f, 1.0f / 60.0f, Input, Style, Config);
	EXPECT_GT(Frame.m_Offset, 300.0f);
	EXPECT_LE(Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h, Frame.m_ScrollbarTrackRect.y + Frame.m_ScrollbarTrackRect.h);
}
TEST(UiV2ScrollContainer, DraggingScrollbarThumbMapsMouseToOffset)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Frame.m_ScrollbarVisible);

	Input.m_MouseY = Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h * 0.5f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ThumbHovered = true;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	EXPECT_TRUE(Container.ScrollbarDragActive(State));

	Input.m_MousePressed = false;
	Input.m_MouseY = Frame.m_ScrollbarTrackRect.y + Frame.m_ScrollbarTrackRect.h - Frame.m_ScrollbarThumbRect.h * 0.5f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	EXPECT_NEAR(Frame.m_Offset, 300.0f, 1.0f);

	Input.m_MouseDown = false;
	Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	EXPECT_FALSE(Container.ScrollbarDragActive(State));
}
TEST(UiV2ScrollContainer, DraggingHorizontalScrollbarThumbMapsMouseToOffset)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_Axis = EQmScrollAxis::HORIZONTAL;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Frame.m_ScrollbarVisible);

	Input.m_MouseX = Frame.m_ScrollbarThumbRect.x + Frame.m_ScrollbarThumbRect.w * 0.5f;
	Input.m_MouseY = Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h * 0.5f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ThumbHovered = true;
	Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_TRUE(Container.ScrollbarDragActive(State));

	Input.m_MousePressed = false;
	Input.m_MouseX = Frame.m_ScrollbarTrackRect.x + Frame.m_ScrollbarTrackRect.w - Frame.m_ScrollbarThumbRect.w * 0.5f;
	Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_NEAR(Frame.m_Offset, 300.0f, 1.0f);

	Input.m_MouseDown = false;
	Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_FALSE(Container.ScrollbarDragActive(State));
}
TEST(UiV2ScrollContainer, ClickingScrollbarTrackPagesTowardMouse)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Frame.m_ScrollbarVisible);

	Input.m_MouseY = Frame.m_ScrollbarTrackRect.y + Frame.m_ScrollbarTrackRect.h - 2.0f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_TrackHovered = true;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);

	EXPECT_NEAR(Frame.m_Offset, 100.0f, 1e-6f);
	EXPECT_TRUE(Container.ScrollbarDragActive(State));
}
TEST(UiV2ScrollState, ProgrammaticTargetSharesNativeAnimationAndClampsAfterContentShrink)
{
	SQmScrollMetrics TallMetrics;
	TallMetrics.m_ViewportSize = 100.0f;
	TallMetrics.m_ContentSize = 600.0f;
	SQmScrollMetrics ShortMetrics;
	ShortMetrics.m_ViewportSize = 100.0f;
	ShortMetrics.m_ContentSize = 180.0f;
	SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.5f);

	CQmScrollState State;
	State.ScrollTo(300.0f, TallMetrics, Config);
	EXPECT_TRUE(State.Animating());
	State.Advance(0.25f, TallMetrics, Config);
	EXPECT_GT(State.Offset(), 0.0f);
	EXPECT_LT(State.Offset(), 300.0f);

	State.Advance(0.0f, ShortMetrics, Config);
	EXPECT_NEAR(State.Offset(), ShortMetrics.MaxOffset(), 1e-6f);
	EXPECT_FALSE(State.Animating());
}
TEST(UiV2ScrollState, DeferredProgrammaticTargetUsesFinalContentMetrics)
{
	SQmScrollMetrics TallMetrics;
	TallMetrics.m_ViewportSize = 100.0f;
	TallMetrics.m_ContentSize = 600.0f;
	SQmScrollMetrics FinalMetrics;
	FinalMetrics.m_ViewportSize = 100.0f;
	FinalMetrics.m_ContentSize = 300.0f;
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.5f);

	CQmScrollState State;
	State.SetOffset(40.0f, TallMetrics, Config);
	State.RequestScrollTo(900.0f);
	State.Advance(0.0f, FinalMetrics, Config);

	EXPECT_TRUE(State.Animating());
	State.Advance(0.5f, FinalMetrics, Config);
	EXPECT_NEAR(State.Offset(), FinalMetrics.MaxOffset(), 1e-6f);
}
TEST(UiV2ScrollState, UserWheelSupersedesDeferredProgrammaticTarget)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 600.0f;
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.5f);

	CQmScrollState State;
	State.SetOffset(100.0f, Metrics, Config);
	State.RequestScrollTo(400.0f);
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.Advance(0.0f, Metrics, Config);

	State.Advance(0.5f, Metrics, Config);
	EXPECT_NEAR(State.Offset(), 100.0f + Config.m_WheelScale, 1e-6f);
}
TEST(UiV2ScrollState, NonScrollableResetPreservesActiveThumbGrabUntilRelease)
{
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 500.0f;

	CQmScrollState State;
	State.SetOffset(80.0f, Metrics);
	State.BeginThumbDrag(12.0f);
	State.ResetForNonScrollableContent(true);
	EXPECT_NEAR(State.Offset(), 0.0f, 1e-6f);
	EXPECT_TRUE(State.ThumbDragActive());
	EXPECT_NEAR(State.ThumbDragGrabOffset(), 12.0f, 1e-6f);

	State.ResetForNonScrollableContent(false);
	EXPECT_FALSE(State.ThumbDragActive());
	EXPECT_NEAR(State.ThumbDragGrabOffset(), 0.0f, 1e-6f);
}
TEST(UiV2ScrollContainer, PreviewFrameDoesNotCancelActiveScrollbarDrag)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Frame.m_ScrollbarVisible);

	Input.m_MouseY = Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h * 0.5f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ThumbHovered = true;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Container.ScrollbarDragActive(State));

	const SQmScrollContainerFrame Preview = Container.PreviewFrame(State, View, 400.0f, Style);
	EXPECT_TRUE(Preview.m_ScrollbarVisible);
	EXPECT_TRUE(Container.ScrollbarDragActive(State));

	Input.m_MousePressed = false;
	Input.m_ThumbHovered = false;
	Input.m_MouseY = Frame.m_ScrollbarTrackRect.y + Frame.m_ScrollbarTrackRect.h - Frame.m_ScrollbarThumbRect.h * 0.5f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	EXPECT_NEAR(Frame.m_Offset, 300.0f, 1.0f);
}
TEST(UiV2ScrollContainer, DraggingContentMovesOffsetWithPointer)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_MouseY = 70.0f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ContentDragAllowed = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MousePressed = false;
	Input.m_MouseY = 67.0f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MouseY = 40.0f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_NEAR(Frame.m_Offset, 30.0f, 1e-6f);
	EXPECT_TRUE(Container.ContentDragActive(State));

	Input.m_MouseDown = false;
	Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
}
TEST(UiV2ScrollContainer, DraggingHorizontalContentUsesPointerX)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_Axis = EQmScrollAxis::HORIZONTAL;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_MouseX = 80.0f;
	Input.m_MouseY = 70.0f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ContentDragAllowed = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MousePressed = false;
	Input.m_MouseX = 76.0f;
	Input.m_MouseY = 30.0f;
	Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MouseX = 40.0f;
	Frame = Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_NEAR(Frame.m_Offset, 40.0f, 1e-6f);
	EXPECT_TRUE(Container.ContentDragActive(State));
	EXPECT_LT(Frame.m_ContentRect.x, View.x);
	EXPECT_NEAR(Frame.m_ContentRect.y, View.y, 1e-6f);

	Input.m_MouseDown = false;
	Container.Update(State, View, 500.0f, 0.0f, Input, Style);
	EXPECT_FALSE(Container.ContentDragActive(State));
}
TEST(UiV2ScrollContainer, ScrollbarDragDoesNotStartContentDrag)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerStyle Style;
	Style.m_ScrollbarWidth = 8.0f;
	Style.m_ScrollbarMargin = 2.0f;
	Style.m_MinThumbHeight = 24.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_ContentDragAllowed = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input, Style);
	ASSERT_TRUE(Frame.m_ScrollbarVisible);

	Input.m_MouseY = Frame.m_ScrollbarThumbRect.y + Frame.m_ScrollbarThumbRect.h * 0.5f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ThumbHovered = true;
	Container.Update(State, View, 400.0f, 0.0f, Input, Style);

	EXPECT_TRUE(Container.ScrollbarDragActive(State));
	EXPECT_FALSE(Container.ContentDragActive(State));
}
TEST(UiV2ScrollContainer, BlockedContentDragDoesNotMoveOffset)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_MouseY = 70.0f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ContentDragAllowed = true;
	Input.m_ContentDragBlocked = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input);

	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MousePressed = false;
	Input.m_MouseY = 40.0f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);
}
TEST(UiV2ScrollContainer, BlockedContentDragCancelsPendingCandidate)
{
	CQmScrollState State;
	CQmScrollContainer Container;
	CUIRect View;
	View.x = 10.0f;
	View.y = 20.0f;
	View.w = 200.0f;
	View.h = 100.0f;

	SQmScrollContainerInput Input;
	Input.m_Hovered = true;
	Input.m_MouseY = 70.0f;
	Input.m_MousePressed = true;
	Input.m_MouseDown = true;
	Input.m_ContentDragAllowed = true;
	SQmScrollContainerFrame Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_MousePressed = false;
	Input.m_ContentDragBlocked = true;
	Input.m_MouseY = 40.0f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);

	Input.m_ContentDragBlocked = false;
	Input.m_MouseY = 20.0f;
	Frame = Container.Update(State, View, 400.0f, 0.0f, Input);
	EXPECT_FALSE(Container.ContentDragActive(State));
	EXPECT_NEAR(Frame.m_Offset, 0.0f, 1e-6f);
}
