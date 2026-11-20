#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/support/friends_config_session.h>
#include <test/test.h>

#include <memory>
#include <string>

namespace
{
	class FriendsNotePersistence : public ::testing::Test
	{
		CConfig m_PreviousConfig = g_Config;
		CTestInfo m_Info;

	protected:
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			m_Info.m_DeleteTestStorageFilesOnSuccess = false;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
		}
		void TearDown() override { g_Config = m_PreviousConfig; }
	};
}

TEST_F(FriendsNotePersistence, MaximumEscapedNoteRestoresTheCompleteValue)
{
	const std::string Name(MAX_NAME_LENGTH - 1, '"');
	const std::string Clan(MAX_CLAN_LENGTH - 1, '\\');
	const std::string Note(IFriends::MAX_FRIEND_NOTE_LENGTH - 1, '\\');
	{
		CFriendsConfigSession Session(m_pStorage.get());
		Session.Friends()->AddFriend(Name.c_str(), Clan.c_str());
		ASSERT_TRUE(Session.Friends()->SetFriendNote(Name.c_str(), Clan.c_str(), Note.c_str()));
		ASSERT_TRUE(Session.Friends()->SetFriendPinned(Name.c_str(), Clan.c_str(), true));
		ASSERT_TRUE(Session.Config()->Save());
	}
	{
		CFriendsConfigSession Reloaded(m_pStorage.get());
		ASSERT_TRUE(Reloaded.Load());
		ASSERT_EQ(Reloaded.Friends()->NumFriends(), 1);
		EXPECT_STREQ(Reloaded.Friends()->GetFriendNote(Name.c_str(), Clan.c_str()), Note.c_str());
		EXPECT_TRUE(Reloaded.Friends()->IsFriendPinned(Name.c_str(), Clan.c_str()));
	}
}

TEST_F(FriendsNotePersistence, EscapedPrefixAndUtf8SuffixBothSurviveReload)
{
	constexpr char aSuffix[] = "中文测试内容";
	static_assert(sizeof(aSuffix) < IFriends::MAX_FRIEND_NOTE_LENGTH);
	const std::string Name(MAX_NAME_LENGTH - 1, '"');
	const std::string Clan(MAX_CLAN_LENGTH - 1, '\\');
	const std::string Note = std::string(IFriends::MAX_FRIEND_NOTE_LENGTH - sizeof(aSuffix), '\\') + aSuffix;
	ASSERT_EQ(Note.size(), IFriends::MAX_FRIEND_NOTE_LENGTH - 1U);
	ASSERT_TRUE(str_utf8_check(Note.c_str()));
	{
		CFriendsConfigSession Session(m_pStorage.get());
		Session.Friends()->AddFriend(Name.c_str(), Clan.c_str());
		ASSERT_TRUE(Session.Friends()->SetFriendNote(Name.c_str(), Clan.c_str(), Note.c_str()));
		ASSERT_TRUE(Session.Config()->Save());
	}
	{
		CFriendsConfigSession Reloaded(m_pStorage.get());
		ASSERT_TRUE(Reloaded.Load());
		ASSERT_EQ(Reloaded.Friends()->NumFriends(), 1);
		const char *pNote = Reloaded.Friends()->GetFriendNote(Name.c_str(), Clan.c_str());
		EXPECT_STREQ(pNote, Note.c_str());
		EXPECT_TRUE(str_utf8_check(pNote));
	}
}

TEST_F(FriendsNotePersistence, AShorterNoteReplacesThePreviouslySavedLongNote)
{
	const std::string LongNote(IFriends::MAX_FRIEND_NOTE_LENGTH - 1, '"');
	{
		CFriendsConfigSession Session(m_pStorage.get());
		Session.Friends()->AddFriend("Alice", "Clan");
		ASSERT_TRUE(Session.Friends()->SetFriendNote("Alice", "Clan", LongNote.c_str()));
		ASSERT_TRUE(Session.Config()->Save());
		ASSERT_TRUE(Session.Friends()->SetFriendNote("Alice", "Clan", "short"));
		ASSERT_TRUE(Session.Config()->Save());
	}
	{
		CFriendsConfigSession Reloaded(m_pStorage.get());
		ASSERT_TRUE(Reloaded.Load());
		EXPECT_STREQ(Reloaded.Friends()->GetFriendNote("Alice", "Clan"), "short");
	}
}

TEST_F(FriendsNotePersistence, ClearingANoteDoesNotRestoreItsPreviouslySavedValue)
{
	{
		CFriendsConfigSession Session(m_pStorage.get());
		Session.Friends()->AddFriend("Alice", "Clan");
		ASSERT_TRUE(Session.Friends()->SetFriendNote("Alice", "Clan", "previous note"));
		ASSERT_TRUE(Session.Config()->Save());
		ASSERT_TRUE(Session.Friends()->ClearFriendNote("Alice", "Clan"));
		ASSERT_TRUE(Session.Config()->Save());
	}
	{
		CFriendsConfigSession Reloaded(m_pStorage.get());
		ASSERT_TRUE(Reloaded.Load());
		EXPECT_TRUE(Reloaded.Friends()->IsFriend("Alice", "Clan", true));
		EXPECT_STREQ(Reloaded.Friends()->GetFriendNote("Alice", "Clan"), "");
	}
}
