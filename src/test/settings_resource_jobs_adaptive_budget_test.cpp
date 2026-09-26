// 设置资源任务测试：按资源任务生命周期独立组织。
#include <game/client/components/assets_preview_scale.h>
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/components/settings_warmup.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

TEST(SettingsResourceJobsAdaptiveBudget, AdaptiveBudgetGrowsOnStableFrames)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_FrameMsAverage = 4.0f;
	Input.m_FrameMsP95 = 5.0f;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_BackgroundBacklog = 120;
	Input.m_VisibleWaiting = 0;
	Input.m_WindowActive = true;

	const SSettingsAdaptiveBudgetOutput First = SettingsAdaptiveBudgetStep(Input, State);
	const SSettingsAdaptiveBudgetOutput Second = SettingsAdaptiveBudgetStep(Input, State);

	EXPECT_EQ(First.m_Mode, ESettingsAdaptiveBudgetMode::IDLE);
	EXPECT_EQ(Second.m_Reason, ESettingsAdaptiveBudgetReason::PROGRESS);
	EXPECT_GE(Second.m_BackgroundTokens, First.m_BackgroundTokens);
	EXPECT_GT(Second.m_TextPrebuildTokens, 0);
	EXPECT_GT(Second.m_DemoMetadataTokens, 0);
}

TEST(SettingsResourceJobsAdaptiveBudget, AdaptiveBudgetCutsBackgroundOnFramePressure)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Stable;
	Stable.m_FrameMsAverage = 4.0f;
	Stable.m_FrameMsP95 = 5.0f;
	Stable.m_TargetFrameMs = 8.333f;
	Stable.m_BackgroundBacklog = 120;
	Stable.m_WindowActive = true;
	SettingsAdaptiveBudgetStep(Stable, State);
	SettingsAdaptiveBudgetStep(Stable, State);

	SSettingsAdaptiveBudgetInput Pressure = Stable;
	Pressure.m_FrameMsAverage = 14.0f;
	Pressure.m_FrameMsP95 = 20.0f;
	const SSettingsAdaptiveBudgetOutput Output = SettingsAdaptiveBudgetStep(Pressure, State);

	EXPECT_EQ(Output.m_Mode, ESettingsAdaptiveBudgetMode::FRAME_PRESSURE);
	EXPECT_EQ(Output.m_Reason, ESettingsAdaptiveBudgetReason::FRAME_PRESSURE);
	EXPECT_EQ(Output.m_BackgroundTokens, 0);
	EXPECT_LE(Output.m_PrefetchTokens, 1);
	EXPECT_GE(Output.m_VisibleTokens, 1);
}

TEST(SettingsResourceJobsAdaptiveBudget, AdaptiveBudgetKeepsVisibleTokensDuringScroll)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_FrameMsAverage = 5.0f;
	Input.m_FrameMsP95 = 6.0f;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_BackgroundBacklog = 120;
	Input.m_VisibleWaiting = 12;
	Input.m_ScrollActive = true;
	Input.m_JumpScrollActive = true;
	Input.m_WindowActive = true;

	const SSettingsAdaptiveBudgetOutput Output = SettingsAdaptiveBudgetStep(Input, State);

	EXPECT_EQ(Output.m_Mode, ESettingsAdaptiveBudgetMode::SCROLL_ACTIVE);
	EXPECT_EQ(Output.m_BackgroundTokens, 0);
	EXPECT_EQ(Output.m_PrefetchTokens, 0);
	EXPECT_GE(Output.m_VisibleTokens, 1);
	EXPECT_GE(Output.m_GpuUploadTokens, 1);
}

TEST(SettingsResourceJobsAdaptiveBudget, AdaptiveTextBudgetKeepsLowHardCapWhileScrolling)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_WindowActive = true;
	Input.m_ScrollActive = true;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_FrameMsAverage = 4.0f;
	Input.m_FrameMsP95 = 5.0f;
	Input.m_BackgroundBacklog = 100;
	Input.m_TextScrollHardCap = 2;
	Input.m_TextIdleHardCap = 64;

	const SSettingsAdaptiveBudgetOutput Output = SettingsAdaptiveBudgetStep(Input, State);
	EXPECT_EQ(Output.m_Mode, ESettingsAdaptiveBudgetMode::SCROLL_ACTIVE);
	EXPECT_LE(Output.m_TextContainerTokens, 2);
	EXPECT_LE(Output.m_GlyphRasterizeTokens, 1);
	EXPECT_LE(Output.m_GlyphUploadTokens, 1);
}

TEST(SettingsResourceJobsAdaptiveBudget, AdaptiveTextBudgetCanGrowBeyondSixteenOnStableHighHeadroomFrames)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_WindowActive = true;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_FrameMsAverage = 3.0f;
	Input.m_FrameMsP95 = 4.0f;
	Input.m_BackgroundBacklog = 100;
	Input.m_TextIdleHardCap = 64;
	Input.m_TextScrollHardCap = 2;

	SSettingsAdaptiveBudgetOutput Output;
	for(int i = 0; i < 40; ++i)
		Output = SettingsAdaptiveBudgetStep(Input, State);

	EXPECT_GT(Output.m_TextContainerTokens, 16);
	EXPECT_LE(Output.m_TextContainerTokens, 64);
}

TEST(SettingsResourceJobsAdaptiveBudget, AdaptiveTextBudgetShrinksWhenRecentTextWorkIsExpensive)
{
	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_WindowActive = true;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_FrameMsAverage = 4.0f;
	Input.m_FrameMsP95 = 5.0f;
	Input.m_BackgroundBacklog = 100;
	Input.m_TextIdleHardCap = 64;
	Input.m_TextContainerCreateMsEwma = 3.0f;
	Input.m_GlyphUploadMsEwma = 2.0f;

	const SSettingsAdaptiveBudgetOutput Output = SettingsAdaptiveBudgetStep(Input, State);
	EXPECT_EQ(Output.m_Reason, ESettingsAdaptiveBudgetReason::FRAME_PRESSURE);
	EXPECT_LE(Output.m_TextContainerTokens, 2);
}
