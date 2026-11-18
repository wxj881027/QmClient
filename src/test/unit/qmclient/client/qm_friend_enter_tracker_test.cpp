#include <game/client/components/qmclient/friend_enter_tracker.h>
#include <game/client/components/qmclient/friend_online_tracker.h>

#include <gtest/gtest.h>
#include <test/support/friend_online_tracker_test_support.h>

#include <vector>

using namespace std::chrono_literals;

namespace
{
	constexpr qm_friend_notify::CGreetQueue::CClock::time_point JOIN_TIME{};
}

TEST(QmFriendPresenceMessage, StandardJoinFormsExtractTheCompleteName)
{
	for(const char *pSuffix : {"' entered and joined the game", "' joined the game",
		    "' entered and joined the red team", "' entered and joined the blue team",
		    "' entered and joined the spectators", "' 已进入并加入了 game", "' 已进入并加入了 spectators"})
	{
		SCOPED_TRACE(pSuffix);
		const auto Message = qm_friend_notify::ParsePresenceMessage(-1, 0, std::string("'O'Brien") + pSuffix);
		EXPECT_EQ(Message.m_Event, qm_friend_notify::EPresenceEvent::ENTER);
		EXPECT_EQ(Message.m_Name, "O'Brien");
	}
}

TEST(QmFriendPresenceMessage, PlayerChatAndOtherChannelsCannotTriggerPresence)
{
	const char *pText = "'Alice' entered and joined the game";
	EXPECT_EQ(qm_friend_notify::ParsePresenceMessage(3, 0, pText).m_Event, qm_friend_notify::EPresenceEvent::NONE);
	EXPECT_EQ(qm_friend_notify::ParsePresenceMessage(-1, 1, pText).m_Event, qm_friend_notify::EPresenceEvent::NONE);
	EXPECT_EQ(qm_friend_notify::ParsePresenceMessage(-2, 0, pText).m_Event, qm_friend_notify::EPresenceEvent::NONE);
}

TEST(QmFriendPresenceMessage, Utf8AndMaximumLengthNamesArePreserved)
{
	for(const std::string &Name : {std::string("璇梦"), std::string(MAX_NAME_LENGTH - 1, 'A')})
	{
		const auto Message = qm_friend_notify::ParsePresenceMessage(-1, 0, "'" + Name + "' entered and joined the game");
		EXPECT_EQ(Message.m_Event, qm_friend_notify::EPresenceEvent::ENTER);
		EXPECT_EQ(Message.m_Name, Name);
	}
}

TEST(QmFriendPresenceMessage, MalformedOrUnrelatedMessagesAreIgnored)
{
	for(const char *pText : {"", "Alice entered and joined the game", "'' entered and joined the game",
		    "'Alice' entered and joined the game extra", "notice: 'Alice' entered and joined the game",
		    "'Alice' joined the red team", "'Alice' joined the spectators",
		    "'Alice\nBob' entered and joined the game", "'1234567890123456' entered and joined the game"})
	{
		SCOPED_TRACE(pText);
		EXPECT_EQ(qm_friend_notify::ParsePresenceMessage(-1, 0, pText).m_Event, qm_friend_notify::EPresenceEvent::NONE);
	}
}

TEST(QmFriendPresenceMessage, LeaveMessagesWithOptionalReasonsExtractTheName)
{
	for(const char *pText : {"'Alice' has left the game", "'Alice' has left the game (Timeout)",
		    "'Alice' 离开了游戏", "'Alice' 离开了游戏（超时）"})
	{
		SCOPED_TRACE(pText);
		const auto Message = qm_friend_notify::ParsePresenceMessage(-1, 0, pText);
		EXPECT_EQ(Message.m_Event, qm_friend_notify::EPresenceEvent::LEAVE);
		EXPECT_EQ(Message.m_Name, "Alice");
	}
	EXPECT_EQ(qm_friend_notify::ParsePresenceMessage(-1, 0, "'Alice' has left the game soon").m_Event, qm_friend_notify::EPresenceEvent::NONE);
}

TEST(QmFriendGreetQueue, DuplicateJoinDoesNotPostponeTheOneSecondDeadline)
{
	qm_friend_notify::CGreetQueue Queue;
	Queue.Add("Friend", JOIN_TIME);
	Queue.Add("Friend", JOIN_TIME + 500ms);
	EXPECT_FALSE(Queue.TrySend(JOIN_TIME + 999ms, [](const std::string &) { ADD_FAILURE(); return true; }));
	EXPECT_TRUE(Queue.TrySend(JOIN_TIME + 1s, [](const std::string &Names) {
		EXPECT_EQ(Names, "Friend");
		return true;
	}));
	EXPECT_FALSE(Queue.TrySend(JOIN_TIME + 2s, [](const std::string &) { ADD_FAILURE(); return true; }));
}

TEST(QmFriendGreetQueue, RetryBatchesOnlyFriendsWhoseOwnDelayHasElapsed)
{
	qm_friend_notify::CGreetQueue Queue;
	Queue.Add("Alice", JOIN_TIME);
	Queue.Add("Bob", JOIN_TIME + 200ms);
	EXPECT_FALSE(Queue.TrySend(JOIN_TIME + 1s, [](const std::string &Names) {
		EXPECT_EQ(Names, "Alice");
		return false;
	}));
	Queue.Add("Carol", JOIN_TIME + 1100ms);
	EXPECT_TRUE(Queue.TrySend(JOIN_TIME + 1200ms, [](const std::string &Names) {
		EXPECT_EQ(Names, "Alice Bob");
		return true;
	}));
	EXPECT_TRUE(Queue.TrySend(JOIN_TIME + 2100ms, [](const std::string &Names) {
		EXPECT_EQ(Names, "Carol");
		return true;
	}));
}

TEST(QmFriendGreetQueue, FriendLeavingBeforeSendIsDiscarded)
{
	qm_friend_notify::CGreetQueue Queue;
	Queue.Add("Alice", JOIN_TIME);
	Queue.Remove("Alice");
	EXPECT_FALSE(Queue.TrySend(JOIN_TIME + 1s, [](const std::string &) { ADD_FAILURE(); return true; }));
}

TEST(QmFriendGreetQueue, DisablingOrChangingConnectionCancelsPendingGreeting)
{
	qm_friend_notify::CGreetQueue Queue;
	Queue.Add("Alice", JOIN_TIME);
	Queue.Reset();
	EXPECT_FALSE(Queue.TrySend(JOIN_TIME + 1s, [](const std::string &) { ADD_FAILURE(); return true; }));
}

TEST(QmFriendOnlineScan, InitialAndNewlyCompletedServersAreSilentUntilTheirNextSnapshot)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Existing{"Alice\tClan", "Alice", "First", "server-1"};
	const qm_friend_notify::CFriend Delayed{"Bob\tClan", "Bob", "Second", "server-2"};
	const qm_friend_notify::CFriend NewFriend{"Carol\tClan", "Carol", "Second", "server-2"};
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Existing}, {"server-1"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Existing, Delayed}, {"server-1", "server-2"}).empty());
	const auto vNotifications = QmFriendTests::UpdateOnline(Tracker, {Existing, Delayed, NewFriend}, {"server-1", "server-2"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Name, "Carol");
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Existing, Delayed, NewFriend}, {"server-1", "server-2"}).empty());
	Tracker.Reset();
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Existing, Delayed, NewFriend}, {"server-1", "server-2"}).empty());
}
