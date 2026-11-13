#include <engine/client/friends.h>
#include <engine/shared/config.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
	class FriendsIdentity : public ::testing::Test
	{
		int m_PreviousIgnoreClan = g_Config.m_ClFriendsIgnoreClan;

	protected:
		std::unique_ptr<CFriends> m_pFriends = std::make_unique<CFriends>();
		void SetUp() override { g_Config.m_ClFriendsIgnoreClan = 0; }
		void TearDown() override { g_Config.m_ClFriendsIgnoreClan = m_PreviousIgnoreClan; }
	};
}

TEST_F(FriendsIdentity, TruncatedNameCanBeFoundAndRemovedUsingItsStoredIdentity)
{
	const std::string Name(MAX_NAME_LENGTH + 8, 'N');
	const std::string StoredName(MAX_NAME_LENGTH - 1, 'N');
	m_pFriends->AddFriend(Name.c_str(), "Clan");
	ASSERT_EQ(m_pFriends->NumFriends(), 1);
	EXPECT_STREQ(m_pFriends->GetFriend(0)->m_aName, StoredName.c_str());
	EXPECT_TRUE(m_pFriends->IsFriend(StoredName.c_str(), "Clan", true));
	m_pFriends->RemoveFriend(StoredName.c_str(), "Clan");
	EXPECT_EQ(m_pFriends->NumFriends(), 0);
}

TEST_F(FriendsIdentity, TruncatedClanUsesTheStoredClanForPlayerMatching)
{
	const std::string Clan(MAX_CLAN_LENGTH + 8, 'C');
	const std::string StoredClan(MAX_CLAN_LENGTH - 1, 'C');
	m_pFriends->AddFriend("Alice", Clan.c_str());
	ASSERT_EQ(m_pFriends->NumFriends(), 1);
	EXPECT_STREQ(m_pFriends->GetFriend(0)->m_aClan, StoredClan.c_str());
	EXPECT_EQ(m_pFriends->GetFriendState("Alice", StoredClan.c_str()), IFriends::FRIEND_PLAYER);
	m_pFriends->RemoveFriend("Alice", StoredClan.c_str());
	EXPECT_EQ(m_pFriends->NumFriends(), 0);
}

TEST_F(FriendsIdentity, RepeatingAnOverlongInputKeepsOneEntryAndItsRevision)
{
	const std::string Name(MAX_NAME_LENGTH + 8, 'N');
	m_pFriends->AddFriend(Name.c_str(), "Clan");
	const uint64_t Added = m_pFriends->Revision();
	m_pFriends->AddFriend(Name.c_str(), "Clan");
	EXPECT_EQ(m_pFriends->NumFriends(), 1);
	EXPECT_EQ(m_pFriends->Revision(), Added);
}

TEST_F(FriendsIdentity, RepeatingTheStoredIdentityUpdatesCategoryAndKeepsPinAndNote)
{
	const std::string Name(MAX_NAME_LENGTH + 8, 'N');
	const std::string StoredName(MAX_NAME_LENGTH - 1, 'N');
	ASSERT_TRUE(m_pFriends->AddCategory("Teammates"));
	m_pFriends->AddFriend(Name.c_str(), "Clan");
	ASSERT_TRUE(m_pFriends->SetFriendPinned(StoredName.c_str(), "Clan", true));
	ASSERT_TRUE(m_pFriends->SetFriendNote(StoredName.c_str(), "Clan", "keep note"));
	const uint64_t Before = m_pFriends->Revision();
	m_pFriends->AddFriend(Name.c_str(), "Clan", "Teammates");
	EXPECT_EQ(m_pFriends->NumFriends(), 1);
	EXPECT_TRUE(m_pFriends->IsFriendPinned(StoredName.c_str(), "Clan"));
	EXPECT_STREQ(m_pFriends->GetFriendNote(StoredName.c_str(), "Clan"), "keep note");
	EXPECT_STREQ(m_pFriends->GetFriendCategory(StoredName.c_str(), "Clan"), "Teammates");
	EXPECT_EQ(m_pFriends->Revision(), Before + 1);
}

TEST_F(FriendsIdentity, Utf8TruncationKeepsOnlyTheCompletePrefixAsIdentity)
{
	const std::string Prefix(MAX_NAME_LENGTH - 2, 'A');
	const std::string Name = Prefix + "中";
	m_pFriends->AddFriend(Name.c_str(), "Clan");
	ASSERT_EQ(m_pFriends->NumFriends(), 1);
	EXPECT_STREQ(m_pFriends->GetFriend(0)->m_aName, Prefix.c_str());
	EXPECT_TRUE(m_pFriends->IsFriend(Prefix.c_str(), "Clan", true));
	m_pFriends->RemoveFriend(Prefix.c_str(), "Clan");
	EXPECT_EQ(m_pFriends->NumFriends(), 0);
}

TEST_F(FriendsIdentity, TruncatedClanFriendStillMatchesOtherMembers)
{
	const std::string Clan(MAX_CLAN_LENGTH + 8, 'C');
	const std::string StoredClan(MAX_CLAN_LENGTH - 1, 'C');
	m_pFriends->AddFriend("", Clan.c_str());
	EXPECT_EQ(m_pFriends->GetFriendState("Other", StoredClan.c_str()), IFriends::FRIEND_CLAN);
	m_pFriends->RemoveFriend("", StoredClan.c_str());
	EXPECT_EQ(m_pFriends->NumFriends(), 0);
}

TEST_F(FriendsIdentity, IgnoreClanStillKeepsTheExistingStoredClan)
{
	g_Config.m_ClFriendsIgnoreClan = 1;
	const std::string Name(MAX_NAME_LENGTH + 8, 'N');
	const std::string StoredName(MAX_NAME_LENGTH - 1, 'N');
	m_pFriends->AddFriend(Name.c_str(), "Original");
	m_pFriends->AddFriend(Name.c_str(), "Changed");
	ASSERT_EQ(m_pFriends->NumFriends(), 1);
	EXPECT_STREQ(m_pFriends->GetFriend(0)->m_aClan, "Original");
	EXPECT_TRUE(m_pFriends->IsFriend(StoredName.c_str(), "Changed", true));
}
