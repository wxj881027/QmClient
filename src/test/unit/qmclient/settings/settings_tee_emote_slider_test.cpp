#include <game/client/QmUi/SettingsPageLayout.h>

#include <gtest/gtest.h>

#include <cmath>

TEST(SettingsTeeEmoteSlider, LayoutSizesAndSlotPartition)
{
	for(const float Scale : {0.75f, 1.0f, 1.25f, 1.5f})
	{
		SCOPED_TRACE(Scale);
		SSettingsContentMetrics Metrics;
		Metrics.m_UiScale = Scale;
		Metrics.m_LineHeight = 24.0f * Scale;
		Metrics.m_LineSpacing = 6.0f * Scale;

		for(const float Width : {300.0f, 480.0f, 720.0f, 1000.0f})
		{
			SCOPED_TRACE(Width);
			const CUIRect View{20.0f, 50.0f, Width, 100.0f};
			const SSettingsTeeEmoteSliderLayout Layout = ResolveSettingsTeeEmoteSliderLayout(View, Metrics);

			EXPECT_FLOAT_EQ(Layout.m_TrackRect.x, View.x);
			EXPECT_FLOAT_EQ(Layout.m_TrackRect.y, View.y);
			EXPECT_FLOAT_EQ(Layout.m_TrackRect.w, View.w);
			EXPECT_FLOAT_EQ(Layout.m_TrackRect.h, Layout.m_Height);
			EXPECT_GE(Layout.m_Height, 44.0f * Scale);

			float AccumulatedWidth = 0.0f;
			for(int i = 0; i < NUM_EMOTES; ++i)
			{
				const CUIRect &Slot = Layout.m_aSlotRects[i];
				EXPECT_FLOAT_EQ(Slot.y, Layout.m_TrackRect.y);
				EXPECT_FLOAT_EQ(Slot.h, Layout.m_TrackRect.h);
				EXPECT_NEAR(Slot.x, Layout.m_TrackRect.x + AccumulatedWidth, 0.01f);
				EXPECT_GT(Slot.w, 0.0f);
				AccumulatedWidth += Slot.w;
			}
			EXPECT_NEAR(AccumulatedWidth, Width, 0.05f);
		}
	}
}

TEST(SettingsTeeEmoteSlider, TeeRenderScaleHasSafeMargin)
{
	for(const float Scale : {0.6f, 0.8f, 1.0f, 1.2f, 1.5f, 2.0f})
	{
		SCOPED_TRACE(Scale);
		SSettingsContentMetrics Metrics;
		Metrics.m_UiScale = Scale;
		Metrics.m_LineHeight = 24.0f * Scale;
		Metrics.m_LineSpacing = 6.0f * Scale;

		const CUIRect View{0.0f, 0.0f, 600.0f, 80.0f};
		const SSettingsTeeEmoteSliderLayout Layout = ResolveSettingsTeeEmoteSliderLayout(View, Metrics);

		// 确保 Tee 渲染高度与轨道上下边缘留有充足呼吸距离，避免头脚产生截断
		EXPECT_LE(Layout.m_TeeSize, Layout.m_Height - 8.0f * Scale);
		EXPECT_GE(Layout.m_TeeSize, 20.0f * Scale);
	}
}

TEST(SettingsTeeEmoteSlider, PointToSlotMapping)
{
	const CUIRect Track{100.0f, 50.0f, 600.0f, 44.0f};
	const float SlotWidth = Track.w / static_cast<float>(NUM_EMOTES);

	// 各槽位中心点正确命中对应表情索引
	for(int i = 0; i < NUM_EMOTES; ++i)
	{
		const float CenterX = Track.x + SlotWidth * (static_cast<float>(i) + 0.5f);
		EXPECT_EQ(ResolveTeeEmoteSliderTargetFromPoint(Track, CenterX), i);
	}

	// 边界与超界处理：左端点与向左越界夹紧到 0，右端点与向右越界夹紧到 5
	EXPECT_EQ(ResolveTeeEmoteSliderTargetFromPoint(Track, Track.x), 0);
	EXPECT_EQ(ResolveTeeEmoteSliderTargetFromPoint(Track, Track.x - 200.0f), 0);
	EXPECT_EQ(ResolveTeeEmoteSliderTargetFromPoint(Track, Track.x + Track.w - 0.01f), NUM_EMOTES - 1);
	EXPECT_EQ(ResolveTeeEmoteSliderTargetFromPoint(Track, Track.x + Track.w + 200.0f), NUM_EMOTES - 1);

	// 临界边界：第 0 槽与第 1 槽交界
	EXPECT_EQ(ResolveTeeEmoteSliderTargetFromPoint(Track, Track.x + SlotWidth - 0.01f), 0);
	EXPECT_EQ(ResolveTeeEmoteSliderTargetFromPoint(Track, Track.x + SlotWidth + 0.01f), 1);

	// 异常宽度防护
	const CUIRect EmptyTrack{100.0f, 50.0f, 0.0f, 44.0f};
	EXPECT_EQ(ResolveTeeEmoteSliderTargetFromPoint(EmptyTrack, 150.0f), 0);
}

TEST(SettingsTeeEmoteSlider, StepClamping)
{
	EXPECT_EQ(StepTeeEmoteSlider(0, -1), 0);
	EXPECT_EQ(StepTeeEmoteSlider(0, 1), 1);
	EXPECT_EQ(StepTeeEmoteSlider(NUM_EMOTES - 1, 1), NUM_EMOTES - 1);
	EXPECT_EQ(StepTeeEmoteSlider(3, -1), 2);
	EXPECT_EQ(StepTeeEmoteSlider(2, 3), 5);
	EXPECT_EQ(StepTeeEmoteSlider(4, -5), 0);
}
