#include <engine/client/friends.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/support/friends_config_session.h>
#include <test/test.h>

#include <memory>

namespace
{
	class CFriendsPinPersistence : public testing::Test
	{
	protected:
		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
		}

		void TearDown() override { g_Config = m_PreviousConfig; }
	};
}

TEST_F(CFriendsPinPersistence, SavedPinsRestoreAlongsideCategoriesAndRegularFriends)
{
	{
		CFriendsConfigSession Session(m_pStorage.get());
		ASSERT_TRUE(Session.Friends()->AddCategory("Teammates"));
		Session.Friends()->AddFriend("Alice", "Clan", "Teammates");
		Session.Friends()->AddFriend("Bob", "Clan", "Teammates");
		ASSERT_TRUE(Session.Friends()->SetFriendPinned("Alice", "Clan", true));
		ASSERT_TRUE(Session.Config()->Save());
	}

	{
		CFriendsConfigSession Reloaded(m_pStorage.get());
		ASSERT_TRUE(Reloaded.Load());
		EXPECT_EQ(Reloaded.Friends()->NumFriends(), 2);
		EXPECT_TRUE(Reloaded.Friends()->IsFriendPinned("Alice", "Clan"));
		EXPECT_FALSE(Reloaded.Friends()->IsFriendPinned("Bob", "Clan"));
		EXPECT_STREQ(Reloaded.Friends()->GetFriendCategory("Alice", "Clan"), "Teammates");
	}
}

TEST_F(CFriendsPinPersistence, SavedPinRestoresEscapedNameAndClan)
{
	constexpr const char *pName = "Ali\"ce\\1";
	constexpr const char *pClan = "Cl\\\"an";
	{
		CFriendsConfigSession Session(m_pStorage.get());
		Session.Friends()->AddFriend(pName, pClan);
		ASSERT_TRUE(Session.Friends()->SetFriendPinned(pName, pClan, true));
		ASSERT_TRUE(Session.Config()->Save());
	}

	{
		CFriendsConfigSession Reloaded(m_pStorage.get());
		ASSERT_TRUE(Reloaded.Load());
		ASSERT_EQ(Reloaded.Friends()->NumFriends(), 1);
		EXPECT_TRUE(Reloaded.Friends()->IsFriendPinned(pName, pClan));
		EXPECT_STREQ(Reloaded.Friends()->GetFriend(0)->m_aName, pName);
		EXPECT_STREQ(Reloaded.Friends()->GetFriend(0)->m_aClan, pClan);
	}
}

TEST_F(CFriendsPinPersistence, SavingAfterUnpinDoesNotRestorePreviousPin)
{
	{
		CFriendsConfigSession Session(m_pStorage.get());
		Session.Friends()->AddFriend("Alice", "Clan");
		ASSERT_TRUE(Session.Friends()->SetFriendPinned("Alice", "Clan", true));
		ASSERT_TRUE(Session.Config()->Save());
		ASSERT_TRUE(Session.Friends()->SetFriendPinned("Alice", "Clan", false));
		ASSERT_TRUE(Session.Config()->Save());
	}

	{
		CFriendsConfigSession Reloaded(m_pStorage.get());
		ASSERT_TRUE(Reloaded.Load());
		EXPECT_TRUE(Reloaded.Friends()->IsFriend("Alice", "Clan", true));
		EXPECT_FALSE(Reloaded.Friends()->IsFriendPinned("Alice", "Clan"));
	}
}

TEST_F(CFriendsPinPersistence, ExistingAddFriendCommandsKeepPlayersUnpinned)
{
	CFriendsConfigSession Session(m_pStorage.get());
	Session.Console()->ExecuteLine("add_friend Alice");
	Session.Console()->ExecuteLine("add_friend Bob Clan");
	Session.Console()->ExecuteLine("add_friend Carol Clan Friends");
	EXPECT_EQ(Session.Friends()->NumFriends(), 3);
	EXPECT_FALSE(Session.Friends()->IsFriendPinned("Alice", ""));
	EXPECT_FALSE(Session.Friends()->IsFriendPinned("Bob", "Clan"));
	EXPECT_FALSE(Session.Friends()->IsFriendPinned("Carol", "Clan"));
}

TEST_F(CFriendsPinPersistence, ConsoleCommandTogglesExplicitPlayersPin)
{
	CFriendsConfigSession Session(m_pStorage.get());
	Session.Console()->ExecuteLine("add_friend Alice Clan");
	Session.Console()->ExecuteLine("qm_friend_pin Alice Clan 1");
	EXPECT_TRUE(Session.Friends()->IsFriendPinned("Alice", "Clan"));
	Session.Console()->ExecuteLine("qm_friend_pin Alice Clan 0");
	EXPECT_FALSE(Session.Friends()->IsFriendPinned("Alice", "Clan"));
}
