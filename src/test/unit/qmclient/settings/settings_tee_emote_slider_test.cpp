#include <engine/shared/config.h>

#include <game/client/QmUi/cards/QmCardCatalogTeeMetrics.h>

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

TEST(SettingsTeeEmoteSlider, WheelStepsWrapInBothDirections)
{
	EXPECT_EQ(StepTeeEmoteSlider(0, -1), NUM_EMOTES - 1);
	EXPECT_EQ(StepTeeEmoteSlider(NUM_EMOTES - 1, 1), 0);
	EXPECT_EQ(StepTeeEmoteSlider(3, -1), 2);
	EXPECT_EQ(StepTeeEmoteSlider(0, 1), 1);
}

TEST(SettingsTeeEmoteSlider, RepeatedStepsCompleteACycleAndReturnToTheSameEmote)
{
	for(const int Direction : {-1, 1})
	{
		int Emote = 2;
		for(int Step = 0; Step < NUM_EMOTES; ++Step)
			Emote = StepTeeEmoteSlider(Emote, Direction);
		EXPECT_EQ(Emote, 2);
	}
	EXPECT_EQ(StepTeeEmoteSlider(2, NUM_EMOTES * 3 + 1), 3);
	EXPECT_EQ(StepTeeEmoteSlider(2, -NUM_EMOTES * 3 - 1), 1);
}

class CSettingsTeeEmoteSliderMotion : public ::testing::Test
{
protected:
	int m_PreviousMotionLevel = 0;
	CUiV2AnimationRuntime m_Runtime;
	SSettingsContentMetrics m_Metrics;
	static constexpr uint64_t NODE_KEY = 101;

	void SetUp() override
	{
		m_PreviousMotionLevel = g_Config.m_QmUiMotionLevel;
		g_Config.m_QmUiMotionLevel = 2;
		m_Metrics.m_UiScale = 1.0f;
		m_Metrics.m_LineHeight = 24.0f;
	}

	void TearDown() override
	{
		g_Config.m_QmUiMotionLevel = m_PreviousMotionLevel;
	}

	CUIRect ResolveThumb(const CUIRect &View, int Emote, CUiV2AnimationRuntime *pRuntime)
	{
		return ResolveSettingsTeeEmoteSliderThumb(ResolveSettingsTeeEmoteSliderLayout(View, m_Metrics), Emote, m_Metrics.m_UiScale, pRuntime, NODE_KEY);
	}
};

TEST_F(CSettingsTeeEmoteSliderMotion, FirstPresentationMatchesSelectedSlot)
{
	const CUIRect View{20.0f, 50.0f, 600.0f, 44.0f};
	const CUIRect Expected = ResolveThumb(View, 3, nullptr);
	const CUIRect Actual = ResolveThumb(View, 3, &m_Runtime);
	EXPECT_FLOAT_EQ(Actual.x, Expected.x);
	EXPECT_FLOAT_EQ(Actual.y, Expected.y);
	EXPECT_FLOAT_EQ(Actual.w, Expected.w);
	EXPECT_FLOAT_EQ(Actual.h, Expected.h);
	EXPECT_EQ(m_Runtime.ActiveTrackCount(), 0);
}

TEST_F(CSettingsTeeEmoteSliderMotion, TrackTranslationFollowsImmediatelyDuringHorizontalSelection)
{
	const CUIRect View{20.0f, 50.0f, 600.0f, 44.0f};
	const CUIRect Initial = ResolveThumb(View, 0, &m_Runtime);
	const CUIRect Start = ResolveThumb(View, NUM_EMOTES - 1, &m_Runtime);
	EXPECT_FLOAT_EQ(Start.x, Initial.x);
	ASSERT_TRUE(m_Runtime.HasActiveAnimation(NODE_KEY, EUiAnimProperty::POS_X));

	for(int Frame = 1; Frame <= 8; ++Frame)
	{
		SCOPED_TRACE(Frame);
		m_Runtime.Advance(1.0f / 60.0f);
		const CUIRect Before = ResolveThumb(View, NUM_EMOTES - 1, &m_Runtime);
		EXPECT_GT(Before.x, Initial.x);
		CUIRect MovedView = View;
		MovedView.x += 7.0f * Frame;
		MovedView.y -= 12.0f * Frame;
		const CUIRect After = ResolveThumb(MovedView, NUM_EMOTES - 1, &m_Runtime);
		const CUIRect Expected = ResolveThumb(MovedView, NUM_EMOTES - 1, nullptr);
		EXPECT_NEAR(After.x - Before.x, MovedView.x - View.x, 0.001f);
		EXPECT_FLOAT_EQ(After.y, Expected.y);
		EXPECT_FLOAT_EQ(After.w, Expected.w);
		EXPECT_FLOAT_EQ(After.h, Expected.h);
	}
}

TEST_F(CSettingsTeeEmoteSliderMotion, ResizeKeepsThumbAlignedWithCurrentTrack)
{
	ResolveThumb({20.0f, 50.0f, 600.0f, 44.0f}, 3, &m_Runtime);
	m_Metrics.m_UiScale = 1.5f;
	m_Metrics.m_LineHeight = 36.0f;
	const CUIRect View{20.0f, 80.0f, 720.0f, 66.0f};
	const CUIRect Expected = ResolveThumb(View, 3, nullptr);
	const CUIRect Actual = ResolveThumb(View, 3, &m_Runtime);
	EXPECT_FLOAT_EQ(Actual.y, Expected.y);
	EXPECT_FLOAT_EQ(Actual.w, Expected.w);
	EXPECT_FLOAT_EQ(Actual.h, Expected.h);
}

TEST_F(CSettingsTeeEmoteSliderMotion, DisablingMotionSnapsActiveSelectionToMovedTrack)
{
	const CUIRect View{20.0f, 50.0f, 600.0f, 44.0f};
	ResolveThumb(View, 0, &m_Runtime);
	ResolveThumb(View, 5, &m_Runtime);
	m_Runtime.Advance(1.0f / 60.0f);
	ASSERT_TRUE(m_Runtime.HasActiveAnimation(NODE_KEY, EUiAnimProperty::POS_X));
	g_Config.m_QmUiMotionLevel = 0;
	const CUIRect MovedView{30.0f, 10.0f, 600.0f, 44.0f};
	const CUIRect Expected = ResolveThumb(MovedView, 5, nullptr);
	const CUIRect Actual = ResolveThumb(MovedView, 5, &m_Runtime);
	EXPECT_FLOAT_EQ(Actual.x, Expected.x);
	EXPECT_FLOAT_EQ(Actual.y, Expected.y);
	EXPECT_FLOAT_EQ(Actual.w, Expected.w);
	EXPECT_FLOAT_EQ(Actual.h, Expected.h);
	EXPECT_EQ(m_Runtime.ActiveTrackCount(), 0);
}
