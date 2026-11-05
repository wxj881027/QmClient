#include <engine/client/friends.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/friend_enter_tracker.h>
#include <game/client/components/qmclient/pending_chat_messages.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
	class CQmFriendGreetFlow : public testing::Test
	{
	protected:
		int m_PreviousIgnoreClan = g_Config.m_ClFriendsIgnoreClan;
		std::unique_ptr<CFriends> m_pFriends = std::make_unique<CFriends>();
		qm_friend_notify::CEnterTracker m_Tracker;
		qm_friend_notify::CGreetQueue m_Queue;

		void SetUp() override
		{
			g_Config.m_ClFriendsIgnoreClan = 0;
			m_pFriends->AddFriend("Alice", "SavedClan");
		}
		void TearDown() override { g_Config.m_ClFriendsIgnoreClan = m_PreviousIgnoreClan; }

		qm_friend_notify::CPresenceMessage Handle(const char *pText, const char *pMain = "Main", const char *pDummy = "Dummy")
		{
			const auto Message = m_Tracker.OnMessage(-1, 0, pText, *m_pFriends, pMain, pDummy);
			if(Message.m_Event == qm_friend_notify::EPresenceEvent::ENTER)
				m_Queue.Add(Message.m_Name);
			else if(Message.m_Event == qm_friend_notify::EPresenceEvent::LEAVE)
				m_Queue.Remove(Message.m_Name);
			return Message;
		}
	};
}

TEST_F(CQmFriendGreetFlow, JoinMessageGreetsWithoutPlayerSnapshotOrLocalClock)
{
	EXPECT_EQ(Handle("'Alice' entered and joined the game").m_Name, "Alice");
	EXPECT_TRUE(m_Queue.TrySend([](const std::string &Names) {
		EXPECT_EQ(Names, "Alice");
		return true;
	}));
	EXPECT_EQ(Handle("'Alice' entered and joined the game").m_Event, qm_friend_notify::EPresenceEvent::NONE);
	EXPECT_FALSE(m_Queue.TrySend([](const std::string &) { ADD_FAILURE(); return true; }));
}

TEST_F(CQmFriendGreetFlow, ExactNicknameMatchesRegardlessOfSavedClanOrGlobalClanSetting)
{
	for(const int IgnoreClan : {0, 1})
	{
		g_Config.m_ClFriendsIgnoreClan = IgnoreClan;
		m_Tracker.Reset();
		EXPECT_EQ(Handle("'alice' entered and joined the game").m_Event, qm_friend_notify::EPresenceEvent::NONE);
		EXPECT_EQ(Handle("'Ali' entered and joined the game").m_Event, qm_friend_notify::EPresenceEvent::NONE);
		EXPECT_EQ(Handle("'Alice' entered and joined the game").m_Event, qm_friend_notify::EPresenceEvent::ENTER);
	}
}

TEST_F(CQmFriendGreetFlow, ClanOnlyFriendsAndOwnConnectionsDoNotGreet)
{
	m_pFriends->AddFriend("", "Clan");
	EXPECT_EQ(Handle("'Stranger' entered and joined the game").m_Event, qm_friend_notify::EPresenceEvent::NONE);
	EXPECT_EQ(Handle("'Alice' entered and joined the game", "Alice", "Dummy").m_Event, qm_friend_notify::EPresenceEvent::NONE);
	EXPECT_EQ(Handle("'Alice' entered and joined the game", "Main", "Alice").m_Event, qm_friend_notify::EPresenceEvent::NONE);
}

TEST_F(CQmFriendGreetFlow, BusyChatRetainsGreetingUntilExistingAdmissionAllowsSend)
{
	Handle("'Alice' entered and joined the game");
	const auto TryChat = [&](int64_t Now) {
		return m_Queue.TrySend([&](const std::string &Names) {
			EXPECT_EQ(Names, "Alice");
			int PendingCount = 0;
			return CPendingChatMessages::Reserve(100, Now, 100, PendingCount) == CPendingChatMessages::EAdmission::SENT;
		});
	};
	EXPECT_FALSE(TryChat(150));
	EXPECT_TRUE(TryChat(201));
	EXPECT_FALSE(TryChat(400));
}

TEST_F(CQmFriendGreetFlow, LeavingCancelsPendingGreetingAndAllowsImmediateReentry)
{
	Handle("'Alice' entered and joined the game");
	Handle("'Alice' has left the game (Timeout)");
	EXPECT_FALSE(m_Queue.TrySend([](const std::string &) { ADD_FAILURE(); return true; }));
	EXPECT_EQ(Handle("'Alice' entered and joined the game").m_Event, qm_friend_notify::EPresenceEvent::ENTER);
	EXPECT_TRUE(m_Queue.TrySend([](const std::string &Names) { return Names == "Alice"; }));
}

TEST_F(CQmFriendGreetFlow, ResetCancelsOldGreetingAndAcceptsTheNextJoinMessage)
{
	Handle("'Alice' entered and joined the game");
	m_Tracker.Reset();
	m_Queue.Reset();
	EXPECT_FALSE(m_Queue.TrySend([](const std::string &) { ADD_FAILURE(); return true; }));
	EXPECT_EQ(Handle("'Alice' entered and joined the game").m_Event, qm_friend_notify::EPresenceEvent::ENTER);
}

TEST_F(CQmFriendGreetFlow, RenamingCancelsOldGreetingAndReleasesTheOldNickname)
{
	for(const char *pText : {"'Alice' changed name to 'Bob'", "'Alice' 改名为 'Bob'"})
	{
		Handle("'Alice' entered and joined the game");
		EXPECT_EQ(Handle(pText).m_Event, qm_friend_notify::EPresenceEvent::LEAVE);
		EXPECT_FALSE(m_Queue.TrySend([](const std::string &) { ADD_FAILURE(); return true; }));
		EXPECT_EQ(Handle("'Alice' entered and joined the game").m_Event, qm_friend_notify::EPresenceEvent::ENTER);
		m_Tracker.Reset();
		m_Queue.Reset();
	}
}
