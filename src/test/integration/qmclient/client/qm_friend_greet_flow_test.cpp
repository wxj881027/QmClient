#include <game/client/components/qmclient/friend_enter_tracker.h>
#include <game/client/components/qmclient/pending_chat_messages.h>

#include <gtest/gtest.h>

TEST(QmFriendGreetFlow, LiveRosterGreetsWithoutAnyServerBrowserRefresh)
{
	qm_friend_notify::CEnterTracker Tracker;
	qm_friend_notify::CGreetQueue Queue;
	Tracker.Update({}, 0.0, false);
	const std::vector<qm_friend_notify::CEnterTracker::CClient> vClients = {{1, "Alice", "Clan", true, false}};
	Queue.Update(Tracker.Update(vClients, 0.2, false), vClients);
	EXPECT_TRUE(Queue.TrySend([](const std::string &Names) {
		EXPECT_EQ(Names, "Alice");
		return true;
	}));
	Queue.Update(Tracker.Update(vClients, 0.4, false), vClients);
	EXPECT_FALSE(Queue.TrySend([](const std::string &) { ADD_FAILURE(); return true; }));
}

TEST(QmFriendGreetFlow, BusyChatRetainsGreetingUntilExistingAdmissionAllowsSend)
{
	qm_friend_notify::CEnterTracker Tracker;
	qm_friend_notify::CGreetQueue Queue;
	Tracker.Update({}, 0.0, false);
	const std::vector<qm_friend_notify::CEnterTracker::CClient> vClients = {{1, "Alice", "Clan", true, false}};
	Queue.Update(Tracker.Update(vClients, 0.2, false), vClients);
	const auto TryChat = [&](int64_t Now) {
		return Queue.TrySend([&](const std::string &Names) {
			EXPECT_EQ(Names, "Alice");
			int PendingCount = 0;
			return CPendingChatMessages::Reserve(100, Now, 100, PendingCount) == CPendingChatMessages::EAdmission::SENT;
		});
	};
	EXPECT_FALSE(TryChat(150));
	Queue.Update(Tracker.Update(vClients, 0.4, false), vClients);
	EXPECT_TRUE(TryChat(201));
	EXPECT_FALSE(TryChat(400));
}
