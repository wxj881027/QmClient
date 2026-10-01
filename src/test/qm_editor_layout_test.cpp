#include <game/editor/qm_editor_layout.h>

#include <gtest/gtest.h>

#include <array>

namespace
{
	QmEditorLayout::SWorkspace Layout(float Width, float Height = 600.0f, float LayersWidth = 168.0f, float InspectorWidth = 220.0f, float ExtraHeight = 0.0f, bool GuiActive = true, bool InspectorVisible = true, bool ExtraVisible = false)
	{
		return QmEditorLayout::Calculate({0.0f, 0.0f, Width, Height}, LayersWidth, InspectorWidth, ExtraHeight, GuiActive, InspectorVisible, ExtraVisible);
	}

	bool Overlaps(const CUIRect &Left, const CUIRect &Right)
	{
		return Left.w > 0.0f && Left.h > 0.0f && Right.w > 0.0f && Right.h > 0.0f &&
		       Left.x < Right.x + Right.w && Right.x < Left.x + Left.w &&
		       Left.y < Right.y + Right.h && Right.y < Left.y + Left.h;
	}
}

TEST(QmEditorLayout, DesktopKeepsThreePanesAndUncoveredCanvas)
{
	const auto Workspace = Layout(1066.0f);
	EXPECT_FLOAT_EQ(Workspace.m_Layers.w, 168.0f);
	EXPECT_FLOAT_EQ(Workspace.m_Inspector.w, 220.0f);
	EXPECT_GE(Workspace.m_Canvas.w, QmEditorLayout::MIN_CANVAS_WIDTH);
	EXPECT_FALSE(Overlaps(Workspace.m_Canvas, Workspace.m_Layers));
	EXPECT_FALSE(Overlaps(Workspace.m_Canvas, Workspace.m_Inspector));
	EXPECT_FLOAT_EQ(Workspace.m_Canvas.y, Workspace.m_Toolbar.y + Workspace.m_Toolbar.h);
}

TEST(QmEditorLayout, OversizedPanesCannotConsumeMinimumCanvas)
{
	const auto Workspace = Layout(800.0f, 600.0f, 10000.0f, 10000.0f);
	EXPECT_GE(Workspace.m_Layers.w, QmEditorLayout::MIN_LAYERS_WIDTH);
	EXPECT_GE(Workspace.m_Inspector.w, QmEditorLayout::MIN_INSPECTOR_WIDTH);
	EXPECT_GE(Workspace.m_Canvas.w, QmEditorLayout::MIN_CANVAS_WIDTH);
}

TEST(QmEditorLayout, CompactWindowHidesInspectorBeforeShrinkingCanvas)
{
	const auto Workspace = Layout(540.0f);
	EXPECT_FLOAT_EQ(Workspace.m_Inspector.w, 0.0f);
	EXPECT_FLOAT_EQ(Workspace.m_InspectorSplitter.w, 0.0f);
	EXPECT_GE(Workspace.m_Canvas.w, QmEditorLayout::MIN_CANVAS_WIDTH);
}

TEST(QmEditorLayout, ExplicitlyHiddenInspectorReturnsSpaceToCanvas)
{
	const auto Visible = Layout(1066.0f);
	const auto Hidden = Layout(1066.0f, 600.0f, 168.0f, 220.0f, 0.0f, true, false);
	EXPECT_FLOAT_EQ(Hidden.m_Inspector.w, 0.0f);
	EXPECT_FLOAT_EQ(Hidden.m_Canvas.w, Visible.m_Canvas.w + Visible.m_Inspector.w + Visible.m_InspectorSplitter.w);
}

TEST(QmEditorLayout, BottomEditorResizesCanvasWithoutCoveringSidePanes)
{
	const auto Workspace = Layout(1066.0f, 600.0f, 168.0f, 220.0f, 10000.0f, true, true, true);
	EXPECT_GE(Workspace.m_Canvas.h, QmEditorLayout::MIN_CANVAS_HEIGHT);
	EXPECT_GT(Workspace.m_ExtraEditor.h, 0.0f);
	EXPECT_FALSE(Overlaps(Workspace.m_Canvas, Workspace.m_ExtraEditor));
	EXPECT_FALSE(Overlaps(Workspace.m_Inspector, Workspace.m_ExtraEditor));
	EXPECT_FALSE(Overlaps(Workspace.m_Layers, Workspace.m_ExtraEditor));
	EXPECT_FLOAT_EQ(Workspace.m_ExtraEditor.y + Workspace.m_ExtraEditor.h, Workspace.m_Status.y);
}

TEST(QmEditorLayout, HiddenGuiUsesWholeScreenForCanvas)
{
	const auto Workspace = Layout(1066.0f, 600.0f, 168.0f, 220.0f, 250.0f, false, true, true);
	EXPECT_FLOAT_EQ(Workspace.m_Canvas.x, 0.0f);
	EXPECT_FLOAT_EQ(Workspace.m_Canvas.y, 0.0f);
	EXPECT_FLOAT_EQ(Workspace.m_Canvas.w, 1066.0f);
	EXPECT_FLOAT_EQ(Workspace.m_Canvas.h, 600.0f);
	EXPECT_FLOAT_EQ(Workspace.m_Toolbar.h, 0.0f);
	EXPECT_FLOAT_EQ(Workspace.m_Inspector.w, 0.0f);
	EXPECT_FLOAT_EQ(Workspace.m_ExtraEditor.h, 0.0f);
}

TEST(QmEditorLayout, SmallAndOffsetScreensKeepNonOverlappingRectanglesInsideScreen)
{
	for(const CUIRect Screen : std::array<CUIRect, 5>{{{10.0f, 20.0f, 320.0f, 240.0f}, {0.0f, 0.0f, 640.0f, 480.0f}, {0.0f, 0.0f, 1600.0f, 600.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 120.0f, 100.0f}}})
	{
		SCOPED_TRACE(::testing::Message() << Screen.w << "x" << Screen.h);
		const auto Workspace = QmEditorLayout::Calculate(Screen, 800.0f, 800.0f, 800.0f, true, true, true);
		const std::array Rects{Workspace.m_Menu, Workspace.m_MapTabs, Workspace.m_Toolbar, Workspace.m_Layers, Workspace.m_LayersSplitter, Workspace.m_Canvas, Workspace.m_InspectorSplitter, Workspace.m_Inspector, Workspace.m_ExtraEditor, Workspace.m_Status};
		for(size_t Index = 0; Index < Rects.size(); ++Index)
		{
			const CUIRect &Rect = Rects[Index];
			EXPECT_GE(Rect.w, 0.0f);
			EXPECT_GE(Rect.h, 0.0f);
			if(Rect.w > 0.0f && Rect.h > 0.0f)
			{
				EXPECT_GE(Rect.x, Screen.x);
				EXPECT_GE(Rect.y, Screen.y);
				EXPECT_LE(Rect.x + Rect.w, Screen.x + Screen.w);
				EXPECT_LE(Rect.y + Rect.h, Screen.y + Screen.h);
			}
			for(size_t Other = Index + 1; Other < Rects.size(); ++Other)
				EXPECT_FALSE(Overlaps(Rect, Rects[Other]));
		}
	}
}
