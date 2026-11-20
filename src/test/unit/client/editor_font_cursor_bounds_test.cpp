#include <game/editor/font_typer.h>

#include <gtest/gtest.h>

#include <limits>

TEST(EditorFontCursorBounds, ActivationOutsideLayerSetsUsableCoordinatesImmediately)
{
	CFontTyper::CState State{};
	State.SetCursorFromWorld(vec2(-64.0f, 640.0f), 4, 3);
	EXPECT_EQ(State.m_TextIndex, ivec2(0, 2));
}

TEST(EditorFontCursorBounds, ActivationAtRightEdgeKeepsWrapPosition)
{
	CFontTyper::CState State{};
	State.SetCursorFromWorld(vec2(128.0f, 32.0f), 4, 3);
	EXPECT_EQ(State.m_TextIndex, ivec2(4, 1));
}

TEST(EditorFontCursorBounds, LayerShrinkLimitsCursorAndLineStartTogether)
{
	CFontTyper::CState State{};
	State.SetCursorFromWorld(vec2(256.0f, 128.0f), 10, 6);
	State.m_LineStart = State.m_TextIndex.x;
	State.ClampCursor(4, 2);
	EXPECT_EQ(State.m_TextIndex, ivec2(4, 1));
	ASSERT_TRUE(State.m_LineStart.has_value());
	EXPECT_EQ(*State.m_LineStart, 3);
}

TEST(EditorFontCursorBounds, NonFiniteProjectionStartsAtOrigin)
{
	CFontTyper::CState State{};
	State.SetCursorFromWorld(vec2(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()), 4, 3);
	EXPECT_EQ(State.m_TextIndex, ivec2(0, 0));
}
