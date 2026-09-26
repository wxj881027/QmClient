// QmAnim 行为测试：forms。
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

TEST(InputField, ClearAndTrailingSlotsDoNotOverlap)
{
	const CUIRect Rect{10.0f, 20.0f, 160.0f, 32.0f};
	const ui_widget::SInputFieldLayout Layout = ui_widget::ResolveInputFieldLayout(Rect, false, true, 1.0f, Rect.h);

	EXPECT_GT(Layout.m_ClearRect.w, 0.0f);
	EXPECT_GT(Layout.m_TrailingRect.w, 0.0f);
	EXPECT_LE(Layout.m_TrailingRect.x + Layout.m_TrailingRect.w, Layout.m_ClearRect.x);
	EXPECT_LT(Layout.m_ContentRect.x + Layout.m_ContentRect.w, Layout.m_TrailingRect.x);
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
