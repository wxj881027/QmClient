// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/tclient/swap_countdown_message.h>

#include <gtest/gtest.h>

TEST(SwapCountdownMessage, ParsesEnglishStartMessage)
{
	ESwapCountdownMessageAction Action = ESwapCountdownMessageAction::None;
	ESwapCountdownMessageDirection Direction = ESwapCountdownMessageDirection::Outgoing;
	char aCounterpart[64];
	EXPECT_TRUE(ParseSwapCountdownMessage("Alpha has requested to swap with you. To complete the swap process please wait 3 seconds and then type /swap Alpha.", Action, Direction, aCounterpart, sizeof(aCounterpart)));
	EXPECT_EQ(Action, ESwapCountdownMessageAction::Start);
	EXPECT_EQ(Direction, ESwapCountdownMessageDirection::Incoming);
	EXPECT_STREQ(aCounterpart, "Alpha");
}

TEST(SwapCountdownMessage, ParsesChineseStartMessage)
{
	ESwapCountdownMessageAction Action = ESwapCountdownMessageAction::None;
	ESwapCountdownMessageDirection Direction = ESwapCountdownMessageDirection::Outgoing;
	char aCounterpart[64];
	EXPECT_TRUE(ParseSwapCountdownMessage("Alpha 请求与你交换位置。请等待 3 秒后输入 /swap Alpha 完成交换。", Action, Direction, aCounterpart, sizeof(aCounterpart)));
	EXPECT_EQ(Action, ESwapCountdownMessageAction::Start);
	EXPECT_EQ(Direction, ESwapCountdownMessageDirection::Incoming);
	EXPECT_STREQ(aCounterpart, "Alpha");
}

TEST(SwapCountdownMessage, ParsesEnglishOutgoingStartMessage)
{
	ESwapCountdownMessageAction Action = ESwapCountdownMessageAction::None;
	ESwapCountdownMessageDirection Direction = ESwapCountdownMessageDirection::Incoming;
	char aCounterpart[64];
	EXPECT_TRUE(ParseSwapCountdownMessage("You have requested to swap with Beta. Use /cancelswap to cancel the request.", Action, Direction, aCounterpart, sizeof(aCounterpart)));
	EXPECT_EQ(Action, ESwapCountdownMessageAction::Start);
	EXPECT_EQ(Direction, ESwapCountdownMessageDirection::Outgoing);
	EXPECT_STREQ(aCounterpart, "Beta");
}

TEST(SwapCountdownMessage, ParsesChineseOutgoingStartMessage)
{
	ESwapCountdownMessageAction Action = ESwapCountdownMessageAction::None;
	ESwapCountdownMessageDirection Direction = ESwapCountdownMessageDirection::Incoming;
	char aCounterpart[64];
	EXPECT_TRUE(ParseSwapCountdownMessage("你已向 Beta 发出交换请求。输入 /cancelswap 可取消", Action, Direction, aCounterpart, sizeof(aCounterpart)));
	EXPECT_EQ(Action, ESwapCountdownMessageAction::Start);
	EXPECT_EQ(Direction, ESwapCountdownMessageDirection::Outgoing);
	EXPECT_STREQ(aCounterpart, "Beta");
}

TEST(SwapCountdownMessage, ParsesIncomingCancelMessages)
{
	ESwapCountdownMessageAction Action = ESwapCountdownMessageAction::None;
	ESwapCountdownMessageDirection Direction = ESwapCountdownMessageDirection::Outgoing;
	char aCounterpart[64];
	EXPECT_TRUE(ParseSwapCountdownMessage("Alpha has canceled swap with you.", Action, Direction, aCounterpart, sizeof(aCounterpart)));
	EXPECT_EQ(Action, ESwapCountdownMessageAction::Cancel);
	EXPECT_EQ(Direction, ESwapCountdownMessageDirection::Incoming);
	EXPECT_STREQ(aCounterpart, "Alpha");

	EXPECT_TRUE(ParseSwapCountdownMessage("Alpha 已取消与你的交换。", Action, Direction, aCounterpart, sizeof(aCounterpart)));
	EXPECT_EQ(Action, ESwapCountdownMessageAction::Cancel);
	EXPECT_EQ(Direction, ESwapCountdownMessageDirection::Incoming);
	EXPECT_STREQ(aCounterpart, "Alpha");
}

TEST(SwapCountdownMessage, ParsesOutgoingCancelMessages)
{
	ESwapCountdownMessageAction Action = ESwapCountdownMessageAction::None;
	ESwapCountdownMessageDirection Direction = ESwapCountdownMessageDirection::Incoming;
	char aCounterpart[64];
	EXPECT_TRUE(ParseSwapCountdownMessage("You have canceled swap with Beta.", Action, Direction, aCounterpart, sizeof(aCounterpart)));
	EXPECT_EQ(Action, ESwapCountdownMessageAction::Cancel);
	EXPECT_EQ(Direction, ESwapCountdownMessageDirection::Outgoing);
	EXPECT_STREQ(aCounterpart, "Beta");

	EXPECT_TRUE(ParseSwapCountdownMessage("你已取消与 Beta 的交换。", Action, Direction, aCounterpart, sizeof(aCounterpart)));
	EXPECT_EQ(Action, ESwapCountdownMessageAction::Cancel);
	EXPECT_EQ(Direction, ESwapCountdownMessageDirection::Outgoing);
	EXPECT_STREQ(aCounterpart, "Beta");
}

TEST(SwapCountdownMessage, ParsesCompleteMessages)
{
	ESwapCountdownMessageAction Action = ESwapCountdownMessageAction::None;
	ESwapCountdownMessageDirection Direction = ESwapCountdownMessageDirection::Outgoing;
	char aCounterpart[64];
	EXPECT_TRUE(ParseSwapCountdownMessage("Alpha has swapped with Beta.", Action, Direction, aCounterpart, sizeof(aCounterpart)));
	EXPECT_EQ(Action, ESwapCountdownMessageAction::Complete);
	EXPECT_STREQ(aCounterpart, "");

	EXPECT_TRUE(ParseSwapCountdownMessage("Alpha 与 Beta 已完成交换。", Action, Direction, aCounterpart, sizeof(aCounterpart)));
	EXPECT_EQ(Action, ESwapCountdownMessageAction::Complete);
	EXPECT_STREQ(aCounterpart, "");
}

TEST(SwapCountdownMessage, ParsesCompletionParticipantsWithoutClearingUnrelatedRequests)
{
	char aFirst[64];
	char aSecond[64];
	EXPECT_TRUE(ParseSwapCompletionMessage("Alpha and Local have swapped.", aFirst, sizeof(aFirst), aSecond, sizeof(aSecond)));
	EXPECT_STREQ(aFirst, "Alpha");
	EXPECT_STREQ(aSecond, "Local");

	EXPECT_TRUE(ParseSwapCompletionMessage("Local 与 Beta 已完成交换。", aFirst, sizeof(aFirst), aSecond, sizeof(aSecond)));
	EXPECT_STREQ(aFirst, "Local");
	EXPECT_STREQ(aSecond, "Beta");
}

TEST(SwapCountdownTracker, KeepsMultipleIncomingRequestsNewestFirst)
{
	CSwapCountdownTracker Tracker;
	Tracker.Start("Alpha", false, 100);
	Tracker.Start("Beta", false, 200);

	ASSERT_EQ(Tracker.Entries().size(), 2u);
	EXPECT_EQ(Tracker.Entries()[0].m_Counterpart, "Beta");
	EXPECT_EQ(Tracker.Entries()[1].m_Counterpart, "Alpha");
	EXPECT_FALSE(Tracker.Entries()[0].m_Outgoing);
	EXPECT_NE(Tracker.Entries()[0].m_InstanceId, Tracker.Entries()[1].m_InstanceId);
}

TEST(SwapCountdownTracker, CancelAndCompleteOnlyRemoveTheMatchingRequest)
{
	CSwapCountdownTracker Tracker;
	Tracker.Start("Alpha", false, 100);
	Tracker.Start("Beta", false, 200);
	Tracker.Start("Gamma", false, 300);

	Tracker.Cancel("Beta", false);
	ASSERT_EQ(Tracker.Entries().size(), 2u);
	EXPECT_EQ(Tracker.Entries()[0].m_Counterpart, "Gamma");
	EXPECT_EQ(Tracker.Entries()[1].m_Counterpart, "Alpha");

	Tracker.Remove("Alpha");
	ASSERT_EQ(Tracker.Entries().size(), 1u);
	EXPECT_EQ(Tracker.Entries()[0].m_Counterpart, "Gamma");
}

TEST(SwapCountdownTracker, ReplacesTheSingleOutgoingRequestWithoutDroppingIncomingRequests)
{
	CSwapCountdownTracker Tracker;
	Tracker.Start("Alpha", false, 100);
	Tracker.Start("Beta", true, 200);
	Tracker.Start("Gamma", true, 300);

	ASSERT_EQ(Tracker.Entries().size(), 2u);
	EXPECT_EQ(Tracker.Entries()[0].m_Counterpart, "Gamma");
	EXPECT_TRUE(Tracker.Entries()[0].m_Outgoing);
	EXPECT_EQ(Tracker.Entries()[1].m_Counterpart, "Alpha");
	EXPECT_FALSE(Tracker.Entries()[1].m_Outgoing);
}
