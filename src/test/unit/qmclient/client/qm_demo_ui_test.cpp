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

TEST(QmDemoUi, CameraPanelKeepsTheBottomEdgeAndFitsTheShortPlayerViewport)
{
	const CUIRect Screen{40.0f, 20.0f, 400.0f, 300.0f};
	const CUIRect Base = qm_demo_ui::PlayerRect(Screen, false);
	const CUIRect Camera = qm_demo_ui::PlayerRect(Screen, false, true);
	EXPECT_FLOAT_EQ(Camera.x, Base.x);
	EXPECT_FLOAT_EQ(Camera.w, Base.w);
	EXPECT_FLOAT_EQ(Camera.y + Camera.h, Base.y + Base.h);
	EXPECT_GE(Camera.y, Screen.y);
	const CUIRect Dragged = qm_demo_ui::DraggedPlayerRect(Screen, Camera, 1000.0f, 1000.0f);
	EXPECT_LE(Dragged.y + Dragged.h, Screen.y + Screen.h);
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
	EXPECT_FLOAT_EQ(qm_demo_ui::SliceContentHeight(5, true), 414.0f);
	// 顶部 4 + 回放显示 110 + 10 + 视频导出 278 + 10 + 播放选项 110 + 10 + 视频编码 110 = 642。
	// 在线时追加断线提示 30。
	EXPECT_FLOAT_EQ(qm_demo_ui::RenderContentHeight(false), 642.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::RenderContentHeight(true), 672.0f);
}

TEST(QmDemoUi, PresetGopMakesEditingAllIntraAndResetsOthers)
{
	// 剪辑预设必须给出全帧内，否则「任意切点」这条剪辑诉求无法兑现。
	EXPECT_EQ(qm_demo_ui::PresetGop(qm_demo_ui::EXPORT_PRESET_EDITING), 1);
	// 最高画质与剪辑同一 GOP 诉求：保真上限也建立在全帧内之上。
	EXPECT_EQ(qm_demo_ui::PresetGop(qm_demo_ui::EXPORT_PRESET_BEST_QUALITY), 1);
	// 其余预设回到默认间隔：只选过一次剪辑的用户，分享片不应继续体积暴涨。
	EXPECT_EQ(qm_demo_ui::PresetGop(0), 60);
	EXPECT_EQ(qm_demo_ui::PresetGop(1), 60);
	EXPECT_EQ(qm_demo_ui::PresetGop(2), 60);
	EXPECT_EQ(qm_demo_ui::PresetGop(4), 60);
	EXPECT_EQ(qm_demo_ui::PresetGop(qm_demo_ui::EXPORT_PRESET_FASTEST), 60);
}

TEST(QmDemoUi, EditingPresetSelectsFullChromaAndOthersStayAuto)
{
	// 剪辑预设的实际画质收益来自完整色度（4:4:4），必须随预设一起生效。
	EXPECT_EQ(qm_demo_ui::PresetPixelFormat(qm_demo_ui::EXPORT_PRESET_EDITING), qm_demo_ui::PIXFMT_YUV444);
	// 最高画质同理：保真上限必须带完整色度。
	EXPECT_EQ(qm_demo_ui::PresetPixelFormat(qm_demo_ui::EXPORT_PRESET_BEST_QUALITY), qm_demo_ui::PIXFMT_YUV444);
	// 其余预设回到自动：4:4:4 的部分播放器/硬件解码器不支持，不能跟着分享片走。
	EXPECT_EQ(qm_demo_ui::PresetPixelFormat(0), qm_demo_ui::PIXFMT_AUTO);
	EXPECT_EQ(qm_demo_ui::PresetPixelFormat(1), qm_demo_ui::PIXFMT_AUTO);
	EXPECT_EQ(qm_demo_ui::PresetPixelFormat(2), qm_demo_ui::PIXFMT_AUTO);
	EXPECT_EQ(qm_demo_ui::PresetPixelFormat(4), qm_demo_ui::PIXFMT_AUTO);
	EXPECT_EQ(qm_demo_ui::PresetPixelFormat(qm_demo_ui::EXPORT_PRESET_FASTEST), qm_demo_ui::PIXFMT_AUTO);
}

TEST(QmDemoUi, PresetQualityWritesExplicitValuesAndClearsForOthers)
{
	// 最高画质在剪辑组合之上把质量压到 14（肉眼无损档）；最快速度用 23 换编码速度。
	EXPECT_EQ(qm_demo_ui::PresetQuality(qm_demo_ui::EXPORT_PRESET_BEST_QUALITY), 14);
	EXPECT_EQ(qm_demo_ui::PresetQuality(qm_demo_ui::EXPORT_PRESET_FASTEST), 23);
	// 其余预设清空回默认 CRF：一档的残留在下一档等于改画质而不告知用户。
	EXPECT_EQ(qm_demo_ui::PresetQuality(0), qm_demo_ui::PRESET_QUALITY_DEFAULT);
	EXPECT_EQ(qm_demo_ui::PresetQuality(1), qm_demo_ui::PRESET_QUALITY_DEFAULT);
	EXPECT_EQ(qm_demo_ui::PresetQuality(2), qm_demo_ui::PRESET_QUALITY_DEFAULT);
	EXPECT_EQ(qm_demo_ui::PresetQuality(qm_demo_ui::EXPORT_PRESET_EDITING), qm_demo_ui::PRESET_QUALITY_DEFAULT);
	EXPECT_EQ(qm_demo_ui::PresetQuality(4), qm_demo_ui::PRESET_QUALITY_DEFAULT);
}

TEST(QmDemoUi, ResolutionCustomKeepsInputsEqualAndDropDownCapped)
{
	for(const float Width : {300.0f, 360.0f, 500.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Control{10.0f, 20.0f, Width, 20.0f};
		const auto PresetLayout = qm_demo_ui::ResolutionControlLayout(Control, false);
		EXPECT_FLOAT_EQ(PresetLayout.m_DropDown.w, Control.w);
		EXPECT_FLOAT_EQ(PresetLayout.m_WidthInput.w, 0.0f);
		EXPECT_FLOAT_EQ(PresetLayout.m_HeightInput.w, 0.0f);

		const auto CustomLayout = qm_demo_ui::ResolutionControlLayout(Control, true);
		EXPECT_FLOAT_EQ(CustomLayout.m_WidthInput.w, CustomLayout.m_HeightInput.w);
		EXPECT_GT(CustomLayout.m_WidthInput.w, 0.0f);
		EXPECT_LE(CustomLayout.m_DropDown.w, qm_demo_ui::RESOLUTION_DROPDOWN_MAX_WIDTH);
		EXPECT_GE(CustomLayout.m_DropDown.w, 0.0f);
		EXPECT_FLOAT_EQ(CustomLayout.m_DropDown.x + CustomLayout.m_DropDown.w, Control.x + Control.w);
		EXPECT_FLOAT_EQ(CustomLayout.m_WidthInput.x, Control.x);
		EXPECT_LE(CustomLayout.m_HeightInput.x + CustomLayout.m_HeightInput.w, CustomLayout.m_DropDown.x);
		EXPECT_GT(CustomLayout.m_Separator.w, 0.0f);
	}

	EXPECT_EQ(qm_demo_ui::MatchResolutionPreset(0, 0), 0);
	EXPECT_EQ(qm_demo_ui::MatchResolutionPreset(1280, 720), 1);
	EXPECT_EQ(qm_demo_ui::MatchResolutionPreset(1920, 1080), 2);
	EXPECT_EQ(qm_demo_ui::MatchResolutionPreset(1919, 1080), qm_demo_ui::RESOLUTION_PRESET_CUSTOM);
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

TEST(QmDemoUi, PoseMapPopupGrowsWithRowsAndStopsAtTheCap)
{
	// 文件夹选择器是列表：少于 8 项也按 8 行给高度，超过 12 项不再长高。
	EXPECT_EQ(qm_demo_ui::PoseMapVisibleRows(0), qm_demo_ui::POSE_MAP_MIN_ROWS);
	EXPECT_EQ(qm_demo_ui::PoseMapVisibleRows(2), qm_demo_ui::POSE_MAP_MIN_ROWS);
	EXPECT_EQ(qm_demo_ui::PoseMapVisibleRows(8), 8);
	EXPECT_EQ(qm_demo_ui::PoseMapVisibleRows(10), 10);
	EXPECT_EQ(qm_demo_ui::PoseMapVisibleRows(200), qm_demo_ui::POSE_MAP_MAX_ROWS);
	EXPECT_GE(qm_demo_ui::POSE_MAP_MIN_ROWS, 8);

	const float Eight = qm_demo_ui::PoseMapContentHeight(8);
	const float Ten = qm_demo_ui::PoseMapContentHeight(10);
	const float Many = qm_demo_ui::PoseMapContentHeight(200);
	// 每多一行只增加一个行高，不额外引入空档。
	EXPECT_FLOAT_EQ(Ten - Eight, 2.0f * qm_demo_ui::POSE_MAP_ROW_HEIGHT);
	EXPECT_FLOAT_EQ(Many - Ten, 2.0f * qm_demo_ui::POSE_MAP_ROW_HEIGHT);

	// 行节奏与渲染回放弹窗同一套，两个弹窗并排看不会像两代界面。
	EXPECT_FLOAT_EQ(qm_demo_ui::POSE_MAP_ROW_HEIGHT, qm_demo_ui::RENDER_ROW_HEIGHT);
	// 整框仍明显小于渲染回放弹窗（704），不会占满整屏。
	EXPECT_LT(qm_demo_ui::PoseMapContentHeight(200) + qm_demo_ui::POPUP_CHROME_HEIGHT, 460.0f);
}

TEST(QmDemoUi, PoseMapRenderRowsReserveHeightForBothOutputRows)
{
	// 渲染模式比观看模式多三行（输出格式、分辨率倍数、锐化）：高度必须一起长，
	// 否则列表会压到按钮上或把这两行裁掉。
	constexpr int RenderRows = qm_demo_ui::POSE_MAP_RENDER_ROWS;
	EXPECT_EQ(RenderRows, 3);
	EXPECT_FLOAT_EQ(qm_demo_ui::PoseMapRenderRowsHeight(0), 0.0f);
	EXPECT_FLOAT_EQ(qm_demo_ui::PoseMapRenderRowsHeight(RenderRows), RenderRows * (qm_demo_ui::POSE_MAP_ROW_HEIGHT + qm_demo_ui::POSE_MAP_GAP));

	const int MapCount = qm_demo_ui::PoseMapVisibleRows(3);
	EXPECT_FLOAT_EQ(
		qm_demo_ui::PoseMapContentHeight(3, RenderRows) - qm_demo_ui::PoseMapContentHeight(3),
		qm_demo_ui::PoseMapRenderRowsHeight(RenderRows));
	// 行数扩展不影响这两行的占位：地图多少都只差可见行高。
	EXPECT_FLOAT_EQ(
		qm_demo_ui::PoseMapContentHeight(200, RenderRows) - qm_demo_ui::PoseMapContentHeight(200),
		qm_demo_ui::PoseMapRenderRowsHeight(RenderRows));
	EXPECT_EQ(MapCount, qm_demo_ui::POSE_MAP_MIN_ROWS);
}
