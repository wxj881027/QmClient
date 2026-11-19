#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/pie_menu_friend_action.h>

#include <gtest/gtest.h>
#include <test/support/friends_config_session.h>
#include <test/test.h>

#include <memory>
#include <string>

namespace
{
	class QmPieMenuFriendCommand : public ::testing::Test
	{
		CConfig m_Previous = g_Config;
		CTestInfo m_Info;

	protected:
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CFriendsConfigSession> m_pSession;
		int m_UnexpectedCommands = 0;
		int m_FriendUpdates = 0;

		void SetUp() override
		{
			m_Info.m_DeleteTestStorageFilesOnSuccess = false;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pSession = std::make_unique<CFriendsConfigSession>(m_pStorage.get());
			m_pSession->Console()->Register("qm_hit", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { ++*static_cast<int *>(pUser); }, &m_UnexpectedCommands, "");
			for(const char *pCommand : {"add_friend", "remove_friend"})
				m_pSession->Console()->Chain(pCommand, [](IConsole::IResult *pResult, void *pUser, IConsole::FCommandCallback Callback, void *pCallbackUser) {
					Callback(pResult, pCallbackUser);
					++*static_cast<int *>(pUser); }, &m_FriendUpdates);
		}
		void TearDown() override
		{
			m_pSession.reset();
			m_pStorage.reset();
			g_Config = m_Previous;
		}
		void AddAndRemoveExactIdentity(const char *pName, const char *pClan)
		{
			ASSERT_TRUE(qm_pie_menu::ExecuteFriendCommand(*m_pSession->Console(), false, pName, pClan));
			ASSERT_EQ(m_pSession->Friends()->NumFriends(), 1);
			EXPECT_STREQ(m_pSession->Friends()->GetFriend(0)->m_aName, pName);
			EXPECT_STREQ(m_pSession->Friends()->GetFriend(0)->m_aClan, pClan);
			EXPECT_EQ(m_UnexpectedCommands, 0);
			EXPECT_EQ(m_FriendUpdates, 1);
			ASSERT_TRUE(qm_pie_menu::ExecuteFriendCommand(*m_pSession->Console(), true, pName, pClan));
			EXPECT_EQ(m_pSession->Friends()->NumFriends(), 0);
			EXPECT_EQ(m_UnexpectedCommands, 0);
			EXPECT_EQ(m_FriendUpdates, 2);
		}
	};
}

TEST_F(QmPieMenuFriendCommand, QuotedNameCannotExecuteAnotherLocalCommand)
{
	AddAndRemoveExactIdentity("n\";qm_hit;#", "clan");
}

TEST_F(QmPieMenuFriendCommand, QuotedClanCannotExecuteAnotherLocalCommand)
{
	AddAndRemoveExactIdentity("player", "c\";qm_hit;#");
}

TEST_F(QmPieMenuFriendCommand, MaximumEscapedIdentitySurvivesBothCommandsWithoutTruncation)
{
	const std::string Name(MAX_NAME_LENGTH - 1, '"');
	const std::string Clan(MAX_CLAN_LENGTH - 1, '\\');
	AddAndRemoveExactIdentity(Name.c_str(), Clan.c_str());
}

TEST_F(QmPieMenuFriendCommand, TrailingBackslashesAndQuotesRemainLiteralIdentity)
{
	AddAndRemoveExactIdentity("A\"B\\", "C\\\"");
}

TEST_F(QmPieMenuFriendCommand, SemicolonsAndCommentMarkersRemainInsideArguments)
{
	AddAndRemoveExactIdentity("a;qm_hit;#", "c;#");
}

TEST_F(QmPieMenuFriendCommand, EmptyNameKeepsClanFriendSemantics)
{
	AddAndRemoveExactIdentity("", "Clan");
}

TEST_F(QmPieMenuFriendCommand, UnicodeAndSpacesKeepBothIdentityFields)
{
	AddAndRemoveExactIdentity("玩家 A", "中文");
}

TEST_F(QmPieMenuFriendCommand, InvalidIdentityDoesNotExecuteACommandOrInvokeTheFriendUpdateChain)
{
	const std::string LongName(MAX_NAME_LENGTH, 'n');
	const std::string LongClan(MAX_CLAN_LENGTH, 'c');
	const struct
	{
		const char *m_pName;
		const char *m_pClan;
	} aInputs[] = {{nullptr, "Clan"}, {"Name", nullptr}, {LongName.c_str(), "Clan"}, {"Name", LongClan.c_str()}};
	for(const auto &Input : aInputs)
	{
		SCOPED_TRACE(Input.m_pName != nullptr ? Input.m_pName : "<null>");
		EXPECT_FALSE(qm_pie_menu::ExecuteFriendCommand(*m_pSession->Console(), false, Input.m_pName, Input.m_pClan));
		EXPECT_FALSE(qm_pie_menu::ExecuteFriendCommand(*m_pSession->Console(), true, Input.m_pName, Input.m_pClan));
		EXPECT_EQ(m_pSession->Friends()->NumFriends(), 0);
		EXPECT_EQ(m_UnexpectedCommands, 0);
		EXPECT_EQ(m_FriendUpdates, 0);
	}
}
