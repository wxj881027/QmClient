#include <gtest/gtest.h>
#include <test/support/friends_config_session.h>
#include <test/test.h>

#include <memory>

namespace
{
	class CFoesConfigPersistence : public testing::Test
	{
		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_Info;

	protected:
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

TEST_F(CFoesConfigPersistence, IgnoreClanKeepsBothStoredIdentitiesAfterReload)
{
	{
		CFriendsConfigSession Session(m_pStorage.get(), true);
		Session.Console()->ExecuteLine("cl_friends_ignore_clan 0");
		Session.Console()->ExecuteLine("add_foe Alice First");
		Session.Console()->ExecuteLine("add_foe Alice Second");
		ASSERT_EQ(Session.Friends()->NumFriends(), 2);
		Session.Console()->ExecuteLine("cl_friends_ignore_clan 1");
		ASSERT_TRUE(Session.Config()->Save());
	}
	{
		CFriendsConfigSession Session(m_pStorage.get(), true);
		g_Config.m_ClFriendsIgnoreClan = 1;
		ASSERT_TRUE(Session.Load());
		EXPECT_EQ(g_Config.m_ClFriendsIgnoreClan, 1);
		ASSERT_EQ(Session.Friends()->NumFriends(), 2);

		Session.Console()->ExecuteLine("cl_friends_ignore_clan 0");
		EXPECT_TRUE(Session.Friends()->IsFriend("Alice", "First", true));
		EXPECT_TRUE(Session.Friends()->IsFriend("Alice", "Second", true));
	}
}
