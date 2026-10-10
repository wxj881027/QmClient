// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/qmclient/console_selection.h>

#include <gtest/gtest.h>

TEST(QmConsoleSelection, DragExtendsAcrossEntriesWithoutMovingAnchor)
{
	CQmConsoleSelection Selection;
	Selection.Begin({10, 2});
	Selection.Extend({12, 3});
	Selection.Extend({30, 1});

	const auto Anchor = Selection.RangeForEntry(10, 8);
	ASSERT_TRUE(Anchor.has_value());
	EXPECT_EQ(Anchor->m_Start, 2);
	EXPECT_EQ(Anchor->m_End, 8);
	const auto Middle = Selection.RangeForEntry(20, 6);
	ASSERT_TRUE(Middle.has_value());
	EXPECT_EQ(Middle->m_Start, 0);
	EXPECT_EQ(Middle->m_End, 6);
	const auto End = Selection.RangeForEntry(30, 5);
	ASSERT_TRUE(End.has_value());
	EXPECT_EQ(End->m_End, 1);
}

TEST(QmConsoleSelection, ReverseDragCopiesInLogOrder)
{
	CQmConsoleSelection Selection;
	Selection.Begin({30, 2});
	Selection.Extend({10, 1});
	std::string Text;
	Selection.AppendText(10, "old", Text);
	Selection.AppendText(20, "middle", Text);
	Selection.AppendText(30, "new", Text);
	EXPECT_EQ(Text, "ld\nmiddle\nne");
}

TEST(QmConsoleSelection, ReleasedSelectionKeepsOffscreenEntriesAndIgnoresNewLogs)
{
	CQmConsoleSelection Selection;
	Selection.Begin({10, 1});
	Selection.Extend({30, 2});
	Selection.Finish();
	Selection.Extend({40, 5});

	std::string Text;
	Selection.AppendText(5, "before", Text);
	Selection.AppendText(10, "first", Text);
	Selection.AppendText(20, "offscreen", Text);
	Selection.AppendText(30, "last", Text);
	Selection.AppendText(40, "appended", Text);
	EXPECT_EQ(Text, "irst\noffscreen\nla");
	EXPECT_TRUE(Selection.HasSelection());
	EXPECT_FALSE(Selection.IsDragging());
	std::string RepeatedCopy;
	Selection.AppendText(10, "first", RepeatedCopy);
	Selection.AppendText(20, "offscreen", RepeatedCopy);
	Selection.AppendText(30, "last", RepeatedCopy);
	EXPECT_EQ(RepeatedCopy, Text);
}

TEST(QmConsoleSelection, CopiesDecodedChineseAndEmojiOffsets)
{
	CQmConsoleSelection Selection;
	Selection.Begin({1, 1});
	Selection.Extend({1, 4});
	std::string Text;
	Selection.AppendText(1, "A中文🙂B", Text);
	EXPECT_EQ(Text, "中文🙂");
}

TEST(QmConsoleSelection, PreservesNewlinesAtEmptySelectionBoundaries)
{
	CQmConsoleSelection Selection;
	Selection.Begin({1, 3});
	Selection.Extend({3, 0});
	std::string Text;
	Selection.AppendText(1, "one", Text);
	Selection.AppendText(2, "", Text);
	Selection.AppendText(3, "three", Text);
	EXPECT_EQ(Text, "\n\n");
}

TEST(QmConsoleSelection, SameEntryDragCanReverseAroundAnchor)
{
	CQmConsoleSelection Selection;
	Selection.Begin({1, 3});
	Selection.Extend({1, 5});
	Selection.Extend({1, 1});
	std::string Text;
	Selection.AppendText(1, "abcdef", Text);
	EXPECT_EQ(Text, "bc");
	Selection.Extend({1, 3});
	EXPECT_FALSE(Selection.HasSelection());
}

TEST(QmConsoleSelection, RemovedEndpointCancelsSelectionBeforeBufferReuse)
{
	CQmConsoleSelection Selection;
	Selection.Begin({10, 1});
	Selection.Extend({20, 2});
	Selection.OnEntryRemoved(5);
	EXPECT_TRUE(Selection.HasSelection());
	Selection.OnEntryRemoved(10);
	EXPECT_FALSE(Selection.HasSelection());
	EXPECT_FALSE(Selection.IsDragging());
	Selection.Extend({30, 4});
	EXPECT_FALSE(Selection.HasSelection());
}

TEST(QmConsoleSelection, ClearAllowsFreshSelectionWithReusedIds)
{
	CQmConsoleSelection Selection;
	Selection.Begin({1, 0});
	Selection.Extend({2, 3});
	Selection.Clear();
	EXPECT_FALSE(Selection.RangeForEntry(1, 4).has_value());
	Selection.Begin({1, 1});
	Selection.Extend({1, 2});
	std::string Text;
	Selection.AppendText(1, "new", Text);
	EXPECT_EQ(Text, "e");
}

TEST(QmConsoleSelection, InvalidHitDoesNotStartOrReplaceDrag)
{
	CQmConsoleSelection Selection;
	Selection.Begin({});
	EXPECT_FALSE(Selection.IsDragging());
	Selection.Begin({1, 1});
	Selection.Extend({1, 2});
	Selection.Extend({});
	const auto Range = Selection.RangeForEntry(1, 3);
	ASSERT_TRUE(Range.has_value());
	EXPECT_EQ(Range->m_Start, 1);
	EXPECT_EQ(Range->m_End, 2);
}

TEST(QmConsoleSelection, AutoScrollNeedsDragAndPointerOutsideLog)
{
	CQmConsoleSelection Selection;
	EXPECT_EQ(Selection.AutoScroll(0.0f, 20.0f, 200.0f, 10.0f, 0.1f), 0);
	Selection.Begin({1, 0});
	EXPECT_EQ(Selection.AutoScroll(50.0f, 20.0f, 200.0f, 10.0f, 0.1f), 0);
	EXPECT_GT(Selection.AutoScroll(0.0f, 20.0f, 200.0f, 10.0f, 0.1f), 0);
	EXPECT_LT(Selection.AutoScroll(220.0f, 20.0f, 200.0f, 10.0f, 0.1f), 0);
	Selection.Finish();
	EXPECT_EQ(Selection.AutoScroll(0.0f, 20.0f, 200.0f, 10.0f, 0.1f), 0);
}

TEST(QmConsoleSelection, AutoScrollAccumulatesShortFramesAndResetsOnReturn)
{
	CQmConsoleSelection Selection;
	Selection.Begin({1, 0});
	EXPECT_EQ(Selection.AutoScroll(10.0f, 20.0f, 200.0f, 10.0f, 0.025f), 0);
	EXPECT_EQ(Selection.AutoScroll(10.0f, 20.0f, 200.0f, 10.0f, 0.025f), 1);
	EXPECT_EQ(Selection.AutoScroll(10.0f, 20.0f, 200.0f, 10.0f, 0.025f), 0);
	EXPECT_EQ(Selection.AutoScroll(50.0f, 20.0f, 200.0f, 10.0f, 0.025f), 0);
	EXPECT_EQ(Selection.AutoScroll(10.0f, 20.0f, 200.0f, 10.0f, 0.025f), 0);
}

TEST(QmConsoleSelection, AutoScrollBoundsFrameStallsAndRejectsEmptyViewport)
{
	CQmConsoleSelection Selection;
	Selection.Begin({1, 0});
	EXPECT_EQ(Selection.AutoScroll(-1000.0f, 20.0f, 200.0f, 10.0f, 5.0f), 6);
	EXPECT_EQ(Selection.AutoScroll(0.0f, 20.0f, 20.0f, 10.0f, 0.1f), 0);
	EXPECT_EQ(Selection.AutoScroll(0.0f, 20.0f, 200.0f, 0.0f, 0.1f), 0);
}

TEST(QmConsoleSelection, ClickRetainsCaretAfterReleaseWithoutSelectingOrCopyingText)
{
	CQmConsoleSelection Selection;
	EXPECT_FALSE(Selection.CursorForEntry(7, 10).has_value());
	Selection.Begin({7, 3});
	Selection.Finish();
	ASSERT_TRUE(Selection.CursorForEntry(7, 10).has_value());
	EXPECT_EQ(*Selection.CursorForEntry(7, 10), 3);
	EXPECT_FALSE(Selection.CursorForEntry(8, 10).has_value());
	EXPECT_FALSE(Selection.HasSelection());
	EXPECT_FALSE(Selection.IsDragging());
	std::string Text;
	Selection.AppendText(7, "read only", Text);
	EXPECT_TRUE(Text.empty());
}

TEST(QmConsoleSelection, CaretFollowsDragEndpointAndIgnoresExtensionAfterRelease)
{
	CQmConsoleSelection Selection;
	Selection.Begin({7, 3});
	Selection.Extend({8, 4});
	EXPECT_FALSE(Selection.CursorForEntry(7, 10).has_value());
	ASSERT_TRUE(Selection.CursorForEntry(8, 10).has_value());
	EXPECT_EQ(*Selection.CursorForEntry(8, 10), 4);
	Selection.Extend({7, 1});
	Selection.Finish();
	Selection.Extend({8, 5});
	ASSERT_TRUE(Selection.CursorForEntry(7, 10).has_value());
	EXPECT_EQ(*Selection.CursorForEntry(7, 10), 1);
	EXPECT_TRUE(Selection.HasSelection());
}

TEST(QmConsoleSelection, CaretClampsToTextLengthAndClearsWhenClickedEntryIsRemoved)
{
	CQmConsoleSelection Selection;
	Selection.Begin({7, 30});
	Selection.Finish();
	EXPECT_EQ(Selection.CursorForEntry(7, 5), 5);
	EXPECT_EQ(Selection.CursorForEntry(7, 0), 0);
	EXPECT_EQ(Selection.CursorForEntry(7, -1), 0);
	Selection.OnEntryRemoved(8);
	EXPECT_TRUE(Selection.CursorForEntry(7, 5).has_value());
	Selection.OnEntryRemoved(7);
	EXPECT_FALSE(Selection.CursorForEntry(7, 5).has_value());
	Selection.Begin({7, 2});
	Selection.Clear();
	EXPECT_FALSE(Selection.CursorForEntry(7, 5).has_value());
	Selection.Begin({7, 2});
	Selection.Begin({});
	EXPECT_FALSE(Selection.CursorForEntry(7, 5).has_value());
}

TEST(QmConsoleCaretMotion, ClickMovesSmoothlyWhileScrollAndReleaseStayOnTheCharacter)
{
	using namespace std::chrono_literals;
	CQmConsoleSelection Selection;
	CQmConsoleCaretMotion Motion;
	auto Now = 1s;
	Selection.Begin({1, 0});
	Selection.Finish();
	EXPECT_EQ(Motion.Resolve(Selection.CursorPosition(), vec2(10, 20), 10, Now, 2, Selection.HasSelection()), vec2(10, 20));
	Selection.Begin({2, 5});
	Selection.Finish();
	EXPECT_EQ(Motion.Resolve(Selection.CursorPosition(), vec2(100, 80), 10, Now, 2, Selection.HasSelection()), vec2(10, 20));
	auto Middle = Motion.Resolve(Selection.CursorPosition(), vec2(100, 80), 10, Now + 40ms, 2, Selection.HasSelection());
	EXPECT_GT(Middle.x, 10);
	EXPECT_LT(Middle.x, 100);
	EXPECT_GT(Middle.y, 20);
	EXPECT_LT(Middle.y, 80);
	EXPECT_EQ(Motion.Resolve(Selection.CursorPosition(), vec2(100, 50), 10, Now + 41ms, 2, Selection.HasSelection()), vec2(100, 50));
	Selection.Begin({2, 5});
	Selection.Extend({3, 8});
	Selection.Finish();
	EXPECT_FALSE(Selection.IsDragging());
	EXPECT_TRUE(Selection.HasSelection());
	EXPECT_EQ(Motion.Resolve(Selection.CursorPosition(), vec2(50, 100), 10, Now + 42ms, 2, Selection.HasSelection()), vec2(50, 100));
	Motion.Reset();
	Selection.Begin({4, 0});
	Selection.Finish();
	EXPECT_EQ(Motion.Resolve(Selection.CursorPosition(), vec2(200, 200), 1, Now + 43ms, 2, false), vec2(200, 200));
}
