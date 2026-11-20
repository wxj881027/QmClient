#include <game/client/components/qmclient/console_selection.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <new>
#include <stdexcept>
#include <string>

TEST(QmConsoleSelectionCopy, PublishesCompleteUtf8SelectionWithoutChangingSelection)
{
	CQmConsoleSelection Selection;
	Selection.Begin({1, 1});
	Selection.Extend({2, 2});
	Selection.Finish();
	std::string Clipboard = "previous clipboard";
	int Copies = 0;
	ASSERT_TRUE(QmTryCopyConsoleSelection([&](std::string &Result) {
		Selection.AppendText(1, "A中文🙂B", Result);
		Selection.AppendText(2, "Z尾X", Result);
		return true; }, [&](const char *pText) { Clipboard = pText; ++Copies; }));
	EXPECT_EQ(Copies, 1);
	EXPECT_EQ(Clipboard, "中文🙂B\nZ尾");
	EXPECT_TRUE(Selection.HasSelection());
	EXPECT_FALSE(Selection.IsDragging());
	EXPECT_EQ(Selection.CursorPosition().m_EntryId, 2);
	EXPECT_EQ(Selection.CursorPosition().m_Character, 2);
}

TEST(QmConsoleSelectionCopy, RejectedPartialPreparationDoesNotTouchClipboard)
{
	std::string Clipboard = "kept";
	int Copies = 0;
	EXPECT_FALSE(QmTryCopyConsoleSelection([](std::string &Result) {
		Result.assign(128, 'x');
		return false; }, [&](const char *pText) { Clipboard = pText; ++Copies; }));
	EXPECT_EQ(Copies, 0);
	EXPECT_EQ(Clipboard, "kept");
}

TEST(QmConsoleSelectionCopy, AllocationFailuresDoNotPublishPrefixesAndCanRetry)
{
	CQmConsoleSelection Selection;
	Selection.Begin({1, 0});
	Selection.Extend({2, 128});
	const std::string First(128, 'a'), Second(128, 'b');
	const auto Build = [&](std::string &Result) {
		Selection.AppendText(1, First.c_str(), Result);
		Selection.AppendText(2, Second.c_str(), Result);
		return true;
	};
	for(size_t BeforeFailure : {0u, 1u})
	{
		SCOPED_TRACE(BeforeFailure);
		std::string Clipboard = "kept";
		int Copies = 0;
		const auto Copy = [&](const char *pText) { Clipboard = pText; ++Copies; };
		bool Copied, Triggered;
		{
			CPredictionAllocationFailure Fault(BeforeFailure);
			Copied = QmTryCopyConsoleSelection(Build, Copy);
			Triggered = Fault.Triggered();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_FALSE(Copied);
		EXPECT_EQ(Copies, 0);
		EXPECT_EQ(Clipboard, "kept");
		EXPECT_TRUE(Selection.HasSelection());
		ASSERT_TRUE(QmTryCopyConsoleSelection(Build, Copy));
		EXPECT_EQ(Copies, 1);
		EXPECT_EQ(Clipboard, First + '\n' + Second);
	}
}

TEST(QmConsoleSelectionCopy, EmptyPreparedSelectionDoesNotEraseClipboard)
{
	int Copies = 0;
	EXPECT_TRUE(QmTryCopyConsoleSelection([](std::string &) { return true; }, [&](const char *) { ++Copies; }));
	EXPECT_EQ(Copies, 0);
}

TEST(QmConsoleSelectionCopy, UnexpectedPreparationExceptionsPropagate)
{
	int Copies = 0;
	EXPECT_THROW(QmTryCopyConsoleSelection([](std::string &) -> bool { throw std::runtime_error("unexpected preparation"); },
			     [&](const char *) { ++Copies; }),
		std::runtime_error);
	EXPECT_EQ(Copies, 0);
}

TEST(QmConsoleSelectionCopy, ClipboardExceptionIsNotSilentlyConvertedToPreparationFailure)
{
	int Copies = 0;
	EXPECT_THROW(QmTryCopyConsoleSelection([](std::string &Result) { Result = "complete"; return true; },
			     [&](const char *) { ++Copies; throw std::bad_alloc(); }),
		std::bad_alloc);
	EXPECT_EQ(Copies, 1);
}
