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
