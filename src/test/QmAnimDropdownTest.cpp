// QmAnim 行为测试：dropdown。
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

TEST(UiV2DropdownGeometry, PositionsPopupRelativeToScrolledAnchor)
{
	CUIRect Viewport;
	Viewport.x = 0.0f;
	Viewport.y = 0.0f;
	Viewport.w = 320.0f;
	Viewport.h = 240.0f;
	CUIRect Anchor;
	Anchor.x = 48.0f;
	Anchor.y = 18.0f;
	Anchor.w = 120.0f;
	Anchor.h = 24.0f;
	SQmDropdownGeometryConfig Config;
	Config.m_Width = Anchor.w;
	Config.m_Height = 80.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 8.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_TRUE(Result.m_AnchorVisible);
	EXPECT_TRUE(Result.m_PlacedBelow);
	EXPECT_NEAR(Result.m_Rect.x, Anchor.x, 0.001f);
	EXPECT_NEAR(Result.m_Rect.y, Anchor.y + Anchor.h + Config.m_Gap, 0.001f);
	EXPECT_NEAR(Result.m_Rect.w, Anchor.w, 0.001f);
	EXPECT_NEAR(Result.m_Rect.h, Config.m_Height, 0.001f);
}
TEST(UiV2DropdownGeometry, RejectsPartiallyVisibleAnchorBeforeOpening)
{
	const CUIRect Viewport{0.0f, 0.0f, 320.0f, 240.0f};
	const CUIRect Anchor{48.0f, -18.0f, 120.0f, 24.0f};
	SQmDropdownGeometryConfig Config;
	Config.m_Width = Anchor.w;
	Config.m_Height = 80.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 8.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_FALSE(Result.m_AnchorVisible);
}
TEST(UiV2DropdownVisuals, SettingsStyleUsesOpaqueElevatedPopupAndAccentBorder)
{
	const SUiTheme Theme = ResolveUiTheme(ColorHSLA(0.20f, 0.50f, 0.40f, 1.0f), 0.75f);
	const SQmDropdownVisualStyle Style = QmSettingsDropdownVisualStyle(Theme);
	EXPECT_FLOAT_EQ(Style.m_TriggerColor.r, Theme.m_InputSurface.r);
	EXPECT_FLOAT_EQ(Style.m_TriggerColor.g, Theme.m_InputSurface.g);
	EXPECT_FLOAT_EQ(Style.m_TriggerColor.b, Theme.m_InputSurface.b);
	EXPECT_FLOAT_EQ(Style.m_PopupBackgroundColor.r, ui_token::color::SURFACE_ELEVATED.r);
	EXPECT_FLOAT_EQ(Style.m_PopupBackgroundColor.g, ui_token::color::SURFACE_ELEVATED.g);
	EXPECT_FLOAT_EQ(Style.m_PopupBackgroundColor.b, ui_token::color::SURFACE_ELEVATED.b);
	EXPECT_FLOAT_EQ(Style.m_PopupBackgroundColor.a, 1.0f);
	EXPECT_TRUE(Style.m_TransparentEntries);
	EXPECT_FLOAT_EQ(Style.m_PopupBorderColor.r, Theme.m_Accent.r);
	EXPECT_FLOAT_EQ(Style.m_PopupBorderColor.g, Theme.m_Accent.g);
	EXPECT_FLOAT_EQ(Style.m_PopupBorderColor.b, Theme.m_Accent.b);
	EXPECT_FLOAT_EQ(Style.m_PopupBorderColor.a, 1.0f);
	EXPECT_FLOAT_EQ(Style.m_ActiveEntryColor.r, Theme.m_Selected.r);
	EXPECT_FLOAT_EQ(Style.m_ActiveEntryColor.g, Theme.m_Selected.g);
	EXPECT_FLOAT_EQ(Style.m_ActiveEntryColor.b, Theme.m_Selected.b);
	EXPECT_FLOAT_EQ(Style.m_ActiveEntryColor.a, Theme.m_Selected.a);
}
TEST(UiV2DropdownGeometry, FlipsAboveWhenBelowWouldOverflow)
{
	CUIRect Viewport;
	Viewport.x = 0.0f;
	Viewport.y = 0.0f;
	Viewport.w = 320.0f;
	Viewport.h = 240.0f;
	CUIRect Anchor;
	Anchor.x = 48.0f;
	Anchor.y = 210.0f;
	Anchor.w = 120.0f;
	Anchor.h = 24.0f;
	SQmDropdownGeometryConfig Config;
	Config.m_Width = Anchor.w;
	Config.m_Height = 96.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 8.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_TRUE(Result.m_AnchorVisible);
	EXPECT_FALSE(Result.m_PlacedBelow);
	EXPECT_NEAR(Result.m_Rect.y, Anchor.y - Config.m_Gap - Config.m_Height, 0.001f);
}
TEST(UiV2DropdownGeometry, ClipsToCompleteRowsInsteadOfShowingAHalfRow)
{
	const CUIRect Viewport{0.0f, 0.0f, 320.0f, 240.0f};
	const CUIRect Anchor{48.0f, 168.0f, 120.0f, 24.0f};
	SQmDropdownGeometryConfig Config;
	Config.m_Width = Anchor.w;
	Config.m_Height = 160.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 8.0f;
	Config.m_RowHeight = 20.0f;
	Config.m_RowSpacing = 4.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_TRUE(Result.m_PopupVisible);
	EXPECT_FALSE(Result.m_PlacedBelow);
	EXPECT_NEAR(Result.m_Rect.h, 140.0f, 0.001f);
	EXPECT_NEAR(std::fmod(Result.m_Rect.h, Config.m_RowHeight + Config.m_RowSpacing), 20.0f, 0.001f);
}
TEST(UiV2DropdownGeometry, EmptyMessageDoesNotReservePhantomTextHeight)
{
	EXPECT_FLOAT_EQ(QmDropdownFixedHeight(false, 18.0f, 16.0f), 16.0f);
	EXPECT_FLOAT_EQ(QmDropdownFixedHeight(true, 18.0f, 16.0f), 34.0f);

	const CUIRect Viewport{0.0f, 0.0f, 320.0f, 260.0f};
	const CUIRect Anchor{48.0f, 8.0f, 120.0f, 24.0f};
	SQmDropdownGeometryConfig Config;
	Config.m_Width = Anchor.w;
	Config.m_RowHeight = 20.0f;
	Config.m_RowSpacing = 4.0f;
	Config.m_FixedHeight = QmDropdownFixedHeight(false, 18.0f, 16.0f);
	Config.m_Height = Config.m_FixedHeight + 8.0f * Config.m_RowHeight + 7.0f * Config.m_RowSpacing;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_TRUE(Result.m_PlacedBelow);
	EXPECT_NEAR(Result.m_Rect.h, Config.m_Height, 0.001f);
}
TEST(UiV2DropdownGeometry, ChoosesTheSideWithMoreCompleteRowsWhenBothSidesAreShort)
{
	const CUIRect Viewport{0.0f, 0.0f, 320.0f, 180.0f};
	const CUIRect Anchor{48.0f, 90.0f, 120.0f, 24.0f};
	SQmDropdownGeometryConfig Config;
	Config.m_Width = Anchor.w;
	Config.m_Height = 160.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 8.0f;
	Config.m_RowHeight = 20.0f;
	Config.m_RowSpacing = 4.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_TRUE(Result.m_PopupVisible);
	EXPECT_GT(Result.m_Rect.h, 0.0f);
	EXPECT_LE(Result.m_Rect.y + Result.m_Rect.h, Anchor.y - Config.m_Gap + 0.001f);
	EXPECT_NEAR(std::fmod(Result.m_Rect.h, Config.m_RowHeight + Config.m_RowSpacing), 20.0f, 0.001f);
}
TEST(UiV2DropdownGeometry, ClampsOversizedPopupInsideViewportMargins)
{
	CUIRect Viewport;
	Viewport.x = 10.0f;
	Viewport.y = 20.0f;
	Viewport.w = 120.0f;
	Viewport.h = 90.0f;
	CUIRect Anchor;
	Anchor.x = 100.0f;
	Anchor.y = 130.0f;
	Anchor.w = 80.0f;
	Anchor.h = 20.0f;
	SQmDropdownGeometryConfig Config;
	Config.m_Width = 200.0f;
	Config.m_Height = 120.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 6.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_FALSE(Result.m_AnchorVisible);
	EXPECT_TRUE(Result.m_Clamped);
	EXPECT_NEAR(Result.m_Rect.x, Viewport.x + Config.m_Margin, 0.001f);
	EXPECT_NEAR(Result.m_Rect.y, Viewport.y + Config.m_Margin, 0.001f);
	EXPECT_NEAR(Result.m_Rect.w, Viewport.w - Config.m_Margin * 2.0f, 0.001f);
	EXPECT_NEAR(Result.m_Rect.h, Viewport.h - Config.m_Margin * 2.0f, 0.001f);
}
TEST(UiV2DropdownGeometry, KeepsPopupVisibleWhenAnchorScrolledOut)
{
	CUIRect Viewport;
	Viewport.x = 0.0f;
	Viewport.y = 0.0f;
	Viewport.w = 320.0f;
	Viewport.h = 240.0f;
	CUIRect Anchor;
	Anchor.x = 48.0f;
	Anchor.y = -160.0f;
	Anchor.w = 120.0f;
	Anchor.h = 24.0f;
	SQmDropdownGeometryConfig Config;
	Config.m_Width = Anchor.w;
	Config.m_Height = 80.0f;
	Config.m_Gap = 4.0f;
	Config.m_Margin = 8.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_FALSE(Result.m_AnchorVisible);
	EXPECT_TRUE(Result.m_PopupVisible);
	EXPECT_TRUE(Result.m_Clamped);
	EXPECT_GE(Result.m_Rect.y, Viewport.y + Config.m_Margin);
	EXPECT_LE(Result.m_Rect.y + Result.m_Rect.h, Viewport.y + Viewport.h - Config.m_Margin);
}
TEST(UiV2DropdownGeometry, MarksPopupInvisibleWhenViewportHasNoUsableArea)
{
	CUIRect Viewport;
	Viewport.x = 0.0f;
	Viewport.y = 0.0f;
	Viewport.w = 12.0f;
	Viewport.h = 12.0f;
	CUIRect Anchor;
	Anchor.x = 4.0f;
	Anchor.y = 4.0f;
	Anchor.w = 16.0f;
	Anchor.h = 16.0f;
	SQmDropdownGeometryConfig Config;
	Config.m_Width = 120.0f;
	Config.m_Height = 80.0f;
	Config.m_Margin = 8.0f;

	const SQmDropdownGeometryResult Result = QmComputeDropdownPopupGeometry(Anchor, Viewport, Config);

	EXPECT_FALSE(Result.m_PopupVisible);
	EXPECT_NEAR(Result.m_Rect.w, 0.0f, 0.001f);
	EXPECT_NEAR(Result.m_Rect.h, 0.0f, 0.001f);
}
TEST(UiV2DropdownGeometry, AnchorMustRemainFullyInsideItsOwningContainer)
{
	const CUIRect Viewport{10.0f, 20.0f, 200.0f, 100.0f};
	EXPECT_TRUE(QmDropdownAnchorFullyVisible({20.0f, 30.0f, 80.0f, 20.0f}, Viewport));
	EXPECT_TRUE(QmDropdownAnchorFullyVisible(Viewport, Viewport));
	// 布局浮点误差不能让贴着卡片底边的下拉框在下一帧立即关闭。
	EXPECT_TRUE(QmDropdownAnchorFullyVisible({20.0f, 99.999f, 80.0f, 20.01f}, Viewport));
	EXPECT_FALSE(QmDropdownAnchorFullyVisible({9.0f, 30.0f, 80.0f, 20.0f}, Viewport));
	EXPECT_FALSE(QmDropdownAnchorFullyVisible({20.0f, 105.0f, 80.0f, 20.0f}, Viewport));
	EXPECT_FALSE(QmDropdownAnchorFullyVisible({20.0f, 30.0f, 0.0f, 20.0f}, Viewport));
}
TEST(UiV2DropdownScroll, ActiveItemOnlyRequestsAutoScrollOnOpenOrKeyboardNavigation)
{
	EXPECT_TRUE(QmDropdownActiveItemShouldScrollIntoView(true, true));
	EXPECT_FALSE(QmDropdownActiveItemShouldScrollIntoView(false, true));
	EXPECT_FALSE(QmDropdownActiveItemShouldScrollIntoView(true, false));
	EXPECT_FALSE(QmDropdownShouldRequestActiveScroll(true, 4, 4));
	EXPECT_FALSE(QmDropdownShouldRequestActiveScroll(false, 4, 5));
	EXPECT_TRUE(QmDropdownShouldRequestActiveScroll(true, 4, 5));
}
TEST(UiV2DropdownScroll, DraggedPopupOffsetSurvivesFollowingFrameWithoutNewTarget)
{
	const SQmScrollMetrics Metrics{160.0f, 320.0f};
	SQmScrollConfig Config;
	Config.m_WheelScale = 20.0f;
	Config.m_NativeWheelAnimationTime = 0.0f;
	CQmScrollState State;

	// 模拟打开后滚轮向下，再模拟用户把滚动条拖到末尾。
	State.AddWheelImpulse(-120.0f, Metrics, Config);
	State.SetOffset(Metrics.MaxOffset(), Metrics, Config);
	ASSERT_FLOAT_EQ(State.Offset(), Metrics.MaxOffset());

	// 下一个绘制帧没有 active-index 变化时不应自动定位回顶部。
	State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_FLOAT_EQ(State.Offset(), Metrics.MaxOffset());
}
TEST(UiV2DropdownPolicy, OwnsWheelWheneverViewportClipsContent)
{
	const SQmDropdownPopupPolicy ShortPolicy = QmResolveDropdownPopupPolicy(QM_POPUP_LIST_MAX_VISIBLE_ITEMS, 20.0f, 5.0f, false, 0.0f, 10.0f);
	EXPECT_EQ(ShortPolicy.m_MaxVisibleItems, QM_POPUP_LIST_MAX_VISIBLE_ITEMS);
	EXPECT_NEAR(ShortPolicy.m_ContentHeight, ShortPolicy.m_PreferredHeight, 0.001f);
	EXPECT_FALSE(QmDropdownPopupScrollable(ShortPolicy, ShortPolicy.m_PreferredHeight));

	const SQmDropdownPopupPolicy LongPolicy = QmResolveDropdownPopupPolicy(QM_POPUP_LIST_MAX_VISIBLE_ITEMS + 1, 20.0f, 5.0f, false, 0.0f, 10.0f);
	EXPECT_EQ(LongPolicy.m_MaxVisibleItems, QM_POPUP_LIST_MAX_VISIBLE_ITEMS);
	EXPECT_GT(LongPolicy.m_ContentHeight, LongPolicy.m_PreferredHeight);
	EXPECT_TRUE(QmDropdownPopupScrollable(LongPolicy, LongPolicy.m_PreferredHeight));

	EXPECT_TRUE(QmDropdownPopupScrollable(ShortPolicy, ShortPolicy.m_PreferredHeight - 1.0f));
}
TEST(UiV2DropdownPolicy, PopupAlwaysBlocksUnderlyingWheelButOnlyShowsRailOnOverflow)
{
	const SQmDropdownPopupPolicy EightRows = QmResolveDropdownPopupPolicy(QM_POPUP_LIST_MAX_VISIBLE_ITEMS, 20.0f, 5.0f, false, 0.0f, 10.0f);
	EXPECT_FALSE(QmDropdownPopupScrollable(EightRows, EightRows.m_PreferredHeight));
	EXPECT_TRUE(QmDropdownPopupBlocksUnderlying(true));

	const SQmDropdownPopupPolicy NineRows = QmResolveDropdownPopupPolicy(QM_POPUP_LIST_MAX_VISIBLE_ITEMS + 1, 20.0f, 5.0f, false, 0.0f, 10.0f);
	EXPECT_TRUE(QmDropdownPopupScrollable(NineRows, NineRows.m_PreferredHeight));
	EXPECT_TRUE(QmDropdownPopupBlocksUnderlying(true));
	EXPECT_FALSE(QmDropdownPopupBlocksUnderlying(false));
}
TEST(UiV2DropdownLifecycle, SourceMustRefreshInTheCurrentFrame)
{
	EXPECT_TRUE(QmDropdownSourceAlive(42, 42, true));
	EXPECT_FALSE(QmDropdownSourceAlive(42, 41, true));
	EXPECT_FALSE(QmDropdownSourceAlive(42, 42, false));
}
TEST(UiV2DropdownPolicy, MapPickerIncludesPopupChromeBeforeTestingEightRowOverflow)
{
	const float OuterHeight = CUi::PopupMenuContentInset();
	const SQmDropdownPopupPolicy NoRows = QmResolveDropdownPopupPolicy(0, 20.0f, 0.0f, false, 0.0f, OuterHeight, 1);
	EXPECT_EQ(NoRows.m_ItemCount, 0);
	EXPECT_NEAR(NoRows.m_PreferredHeight - OuterHeight, 20.0f, 0.001f);
	EXPECT_FALSE(QmDropdownPopupScrollable(NoRows, NoRows.m_PreferredHeight));

	const SQmDropdownPopupPolicy OneRow = QmResolveDropdownPopupPolicy(1, 20.0f, 0.0f, false, 0.0f, OuterHeight, 1);
	EXPECT_EQ(OneRow.m_ItemCount, 1);
	EXPECT_NEAR(OneRow.m_PreferredHeight - OuterHeight, 20.0f, 0.001f);
	EXPECT_NEAR(OneRow.m_ContentHeight, OneRow.m_PreferredHeight, 0.001f);
	EXPECT_FALSE(QmDropdownPopupScrollable(OneRow, OneRow.m_PreferredHeight));

	const SQmDropdownPopupPolicy EightRows = QmResolveDropdownPopupPolicy(8, 20.0f, 0.0f, false, 0.0f, OuterHeight);
	EXPECT_NEAR(EightRows.m_PreferredHeight, 8.0f * 20.0f + OuterHeight, 0.001f);
	EXPECT_NEAR(EightRows.m_ContentHeight, EightRows.m_PreferredHeight, 0.001f);
	EXPECT_FALSE(QmDropdownPopupScrollable(EightRows, EightRows.m_PreferredHeight));

	const SQmDropdownPopupPolicy NineRows = QmResolveDropdownPopupPolicy(9, 20.0f, 0.0f, false, 0.0f, OuterHeight);
	EXPECT_NEAR(NineRows.m_PreferredHeight, EightRows.m_PreferredHeight, 0.001f);
	EXPECT_GT(NineRows.m_ContentHeight, NineRows.m_PreferredHeight);
	EXPECT_TRUE(QmDropdownPopupScrollable(NineRows, NineRows.m_PreferredHeight));
}
TEST(UiV2DropdownPolicy, FirstFrameContentHintMatchesScrollableInnerContent)
{
	const float OuterHeight = CUi::PopupMenuContentInset();
	const SQmDropdownPopupPolicy EightRows = QmResolveDropdownPopupPolicy(8, 20.0f, 5.0f, false, 0.0f, OuterHeight);
	const SQmDropdownPopupPolicy NineRows = QmResolveDropdownPopupPolicy(9, 20.0f, 5.0f, false, 0.0f, OuterHeight);
	EXPECT_FLOAT_EQ(EightRows.m_ContentHeight - OuterHeight, EightRows.m_PreferredHeight - OuterHeight);
	EXPECT_GT(NineRows.m_ContentHeight - OuterHeight, NineRows.m_PreferredHeight - OuterHeight);
}
TEST(UiV2DropdownIntegration, LongPopupConsumesWheelBeforeParent)
{
	CScrollWheelOwnership Ownership;
	int ParentRegion = 0;
	int SelectionPopupContext = 0;
	CQmScrollState ParentState;
	CQmScrollState PopupState;
	const SQmScrollMetrics ParentMetrics{300.0f, 900.0f};
	const SQmScrollMetrics PopupMetrics{160.0f, 320.0f};
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.0f);
	const CUIRect ParentRect{0.0f, 0.0f, 300.0f, 300.0f};
	const CUIRect PopupRect{20.0f, 40.0f, 160.0f, 160.0f};
	const vec2 Pointer{50.0f, 80.0f};
	ASSERT_TRUE(Ownership.BeginFrame(41, -120.0f, false));
	QmRegisterWheelOwnerCandidate(Ownership, {&SelectionPopupContext, EUiWheelOwnerPriority::POPUP, PopupRect, true}, Pointer, true);
	QmRegisterWheelOwnerCandidate(Ownership, {&ParentRegion, EUiWheelOwnerPriority::PAGE, ParentRect, true}, Pointer, true);
	float Delta = 0.0f;
	EXPECT_FALSE(QmTryConsumeWheel(Ownership, &ParentRegion, &Delta));
	ASSERT_TRUE(QmTryConsumeWheel(Ownership, &SelectionPopupContext, &Delta));
	PopupState.AddWheelImpulse(Delta, PopupMetrics, Config);
	ParentState.Advance(1.0f / 60.0f, ParentMetrics, Config);
	PopupState.Advance(1.0f / 60.0f, PopupMetrics, Config);
	EXPECT_FLOAT_EQ(ParentState.Offset(), 0.0f);
	EXPECT_GT(PopupState.Offset(), 0.0f);
}
TEST(UiV2DropdownIntegration, ShortPopupHidesRailAndBlocksParentWheel)
{
	const SQmDropdownPopupPolicy Policy = QmResolveDropdownPopupPolicy(4, 20.0f, 5.0f, false, 0.0f, 10.0f);
	EXPECT_FALSE(QmDropdownPopupScrollable(Policy, Policy.m_PreferredHeight));
	CScrollWheelOwnership Ownership;
	int ParentRegion = 0;
	int SelectionPopupContext = 0;
	const CUIRect ParentRect{0.0f, 0.0f, 300.0f, 300.0f};
	const CUIRect PopupRect{20.0f, 40.0f, 160.0f, Policy.m_PreferredHeight};
	const vec2 Pointer{50.0f, 80.0f};
	ASSERT_TRUE(Ownership.BeginFrame(41, -120.0f, false));
	QmRegisterWheelOwnerCandidate(Ownership, {&SelectionPopupContext, EUiWheelOwnerPriority::POPUP, PopupRect, QmDropdownPopupBlocksUnderlying(true)}, Pointer, true);
	QmRegisterWheelOwnerCandidate(Ownership, {&ParentRegion, EUiWheelOwnerPriority::PAGE, ParentRect, true}, Pointer, true);
	float Delta = 0.0f;
	EXPECT_FALSE(QmTryConsumeWheel(Ownership, &ParentRegion, &Delta));
	EXPECT_TRUE(QmTryConsumeWheel(Ownership, &SelectionPopupContext, &Delta));
	EXPECT_FLOAT_EQ(Delta, -120.0f);
}
TEST(UiV2DropdownState, OpensWithCurrentItemAndClosesOnEscape)
{
	CQmDropdownState State;
	SQmDropdownInput Input;
	Input.m_TogglePressed = true;
	Input.m_InitialIndex = 2;

	SQmDropdownUpdateResult Result = State.Update(Input, 3);
	EXPECT_TRUE(Result.m_Opened);
	EXPECT_TRUE(State.IsOpen());
	EXPECT_EQ(State.ActiveIndex(), 2);

	Input = {};
	Input.m_KeyEscape = true;
	Result = State.Update(Input, 3);
	EXPECT_TRUE(Result.m_Closed);
	EXPECT_FALSE(State.IsOpen());
	EXPECT_EQ(State.ActiveIndex(), -1);
}
TEST(UiV2DropdownState, InvalidCurrentItemFallsBackToFirstItem)
{
	CQmDropdownState State;
	SQmDropdownInput Input;
	Input.m_TogglePressed = true;
	Input.m_InitialIndex = 9;

	const SQmDropdownUpdateResult Result = State.Update(Input, 3);
	EXPECT_TRUE(Result.m_Opened);
	EXPECT_EQ(State.ActiveIndex(), 0);
}
TEST(UiV2DropdownState, KeyboardNavigationWrapsAndEnterSelects)
{
	CQmDropdownState State;
	SQmDropdownInput Input;
	Input.m_TogglePressed = true;
	State.Update(Input, 3);

	Input = {};
	Input.m_KeyUp = true;
	SQmDropdownUpdateResult Result = State.Update(Input, 3);
	EXPECT_FALSE(Result.m_Selected);
	EXPECT_EQ(State.ActiveIndex(), 2);

	Input = {};
	Input.m_KeyDown = true;
	Result = State.Update(Input, 3);
	EXPECT_EQ(State.ActiveIndex(), 0);

	Input = {};
	Input.m_KeyEnter = true;
	Result = State.Update(Input, 3);
	EXPECT_TRUE(Result.m_Selected);
	EXPECT_EQ(Result.m_SelectedIndex, 0);
	EXPECT_TRUE(Result.m_Closed);
	EXPECT_FALSE(State.IsOpen());
}
TEST(UiV2DropdownState, MouseHoverAndClickSelectsHoveredItem)
{
	CQmDropdownState State;
	SQmDropdownInput Input;
	Input.m_TogglePressed = true;
	State.Update(Input, 4);

	Input = {};
	Input.m_HoveredIndex = 2;
	SQmDropdownUpdateResult Result = State.Update(Input, 4);
	EXPECT_FALSE(Result.m_Selected);
	EXPECT_EQ(State.ActiveIndex(), 2);

	Input.m_MouseSelectPressed = true;
	Result = State.Update(Input, 4);
	EXPECT_TRUE(Result.m_Selected);
	EXPECT_EQ(Result.m_SelectedIndex, 2);
	EXPECT_FALSE(State.IsOpen());
}
