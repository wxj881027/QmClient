#include <game/client/components/qmclient/demo_ui.h>

#include <gtest/gtest.h>

TEST(QmDemoUi, PlayerCentersAndDraggingStaysInsideScreen)
{
	const CUIRect Screen{40.0f, 20.0f, 1000.0f, 600.0f};
	const CUIRect Base = qm_demo_ui::PlayerRect(Screen, false);
	EXPECT_FLOAT_EQ(Base.x + Base.w * 0.5f, Screen.x + Screen.w * 0.5f);
	EXPECT_FLOAT_EQ(Base.y + Base.h, Screen.y + Screen.h - 12.0f);
	EXPECT_FLOAT_EQ(Base.w, 640.0f);
	EXPECT_FLOAT_EQ(Base.h, 120.0f);

	const CUIRect BottomRight = qm_demo_ui::DraggedPlayerRect(Screen, Base, 1000.0f, 1000.0f);
	EXPECT_FLOAT_EQ(BottomRight.x + BottomRight.w, Screen.x + Screen.w);
	EXPECT_FLOAT_EQ(BottomRight.y + BottomRight.h, Screen.y + Screen.h);
	const CUIRect TopLeft = qm_demo_ui::DraggedPlayerRect(Screen, Base, -1000.0f, -1000.0f);
	EXPECT_FLOAT_EQ(TopLeft.x, Screen.x);
	EXPECT_FLOAT_EQ(TopLeft.y, Screen.y);

	const CUIRect Narrow = qm_demo_ui::PlayerRect({0.0f, 0.0f, 400.0f, 240.0f}, false);
	EXPECT_FLOAT_EQ(Narrow.x, 12.0f);
	EXPECT_FLOAT_EQ(Narrow.w, 376.0f);
	const CUIRect Tiny{0.0f, 0.0f, 100.0f, 40.0f};
	EXPECT_FLOAT_EQ(qm_demo_ui::DraggedPlayerRect(Tiny, qm_demo_ui::PlayerRect(Tiny, false), 0.0f, 100.0f).y, 0.0f);
}

TEST(QmDemoUi, DisplayOptionsExpandUpwardWithoutMovingTheBottomEdge)
{
	const CUIRect Screen{40.0f, 20.0f, 400.0f, 300.0f};
	const CUIRect Collapsed = qm_demo_ui::PlayerRect(Screen, false);
	const CUIRect Expanded = qm_demo_ui::PlayerRect(Screen, true);
	EXPECT_FLOAT_EQ(Expanded.x, Collapsed.x);
	EXPECT_FLOAT_EQ(Expanded.w, Collapsed.w);
	EXPECT_FLOAT_EQ(Expanded.y + Expanded.h, Collapsed.y + Collapsed.h);
	EXPECT_FLOAT_EQ(Expanded.h - Collapsed.h, qm_demo_ui::DISPLAY_HEIGHT + 6.0f);
	EXPECT_GE(Expanded.y, Screen.y);

	const CUIRect Dragged = qm_demo_ui::DraggedPlayerRect(Screen, Expanded, 1000.0f, 1000.0f);
	EXPECT_FLOAT_EQ(Dragged.x + Dragged.w, Screen.x + Screen.w);
	EXPECT_FLOAT_EQ(Dragged.y + Dragged.h, Screen.y + Screen.h);
	EXPECT_GE(Dragged.y, Screen.y);
}

TEST(QmDemoUi, TransportControlsFitNarrowAndWidePlayerPanels)
{
	for(const float ScreenWidth : {320.0f, 400.0f, 1000.0f})
	{
		for(const bool ShowSkipDuration : {false, true})
		{
			SCOPED_TRACE(::testing::Message() << "width=" << ScreenWidth << " duration=" << ShowSkipDuration);
			const CUIRect Panel = qm_demo_ui::PlayerRect({0.0f, 0.0f, ScreenWidth, 600.0f}, false);
			const float ButtonSize = qm_demo_ui::TransportButtonSize(Panel.w, ShowSkipDuration);
			EXPECT_GT(ButtonSize, 0.0f);
			EXPECT_LE(ButtonSize, 22.0f);
			EXPECT_LE(qm_demo_ui::TransportWidth(ButtonSize, ShowSkipDuration), Panel.w - 16.0f);
		}
	}
}

TEST(QmDemoUi, CutControlsStaySeparateAndInsideThePlayer)
{
	for(const float ScreenWidth : {320.0f, 400.0f, 1000.0f})
	{
		SCOPED_TRACE(ScreenWidth);
		const CUIRect Panel = qm_demo_ui::PlayerRect({40.0f, 20.0f, ScreenWidth, 600.0f}, false);
		const CUIRect Bar{Panel.x + 8.0f, Panel.y + Panel.h - 32.0f, Panel.w - 16.0f, 24.0f};
		const auto Controls = qm_demo_ui::CutControls(Bar, qm_demo_ui::TransportButtonSize(Panel.w, true));
		float PreviousRight = Bar.x;
		for(const CUIRect &Button : {Controls.m_Start, Controls.m_End, Controls.m_Add, Controls.m_Clear, Controls.m_Preview, Controls.m_Export})
		{
			EXPECT_GT(Button.w, 0.0f);
			EXPECT_GE(Button.x, PreviousRight);
			EXPECT_LE(Button.x + Button.w, Bar.x + Bar.w);
			EXPECT_FLOAT_EQ(Button.y, Bar.y);
			EXPECT_FLOAT_EQ(Button.h, Bar.h);
			PreviousRight = Button.x + Button.w;
		}
	}
}

TEST(QmDemoUi, SliceContentAccountsForFourVisibleSegmentsAndDisplayOptions)
{
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(0, false), 154.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(1, false), 204.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(4, false), 264.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(5, false), 280.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(5, true), 400.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::RenderContentHeight(false, false), 120.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::RenderContentHeight(true, true), 270.0f);
}

TEST(QmDemoUi, PopupFitsTheViewportAndKeepsContentHeightWhenPossible)
{
	const CUIRect Screen{40.0f, 20.0f, 1000.0f, 600.0f};
	const CUIRect Full = qm_demo_ui::PopupRect(Screen, 516.0f);
	EXPECT_FLOAT_EQ(Full.x, 260.0f);
	EXPECT_FLOAT_EQ(Full.y, 62.0f);
	EXPECT_FLOAT_EQ(Full.w, 560.0f);
	EXPECT_FLOAT_EQ(Full.h, 516.0f);

	const CUIRect Narrow = qm_demo_ui::PopupRect({0.0f, 0.0f, 400.0f, 240.0f}, 516.0f);
	EXPECT_FLOAT_EQ(Narrow.x, 12.0f);
	EXPECT_FLOAT_EQ(Narrow.y, 12.0f);
	EXPECT_FLOAT_EQ(Narrow.w, 376.0f);
	EXPECT_FLOAT_EQ(Narrow.h, 216.0f);
}
