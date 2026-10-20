#include <game/client/components/qmclient/chat_message_filter.h>

#include <gtest/gtest.h>

namespace
{
	class QmChatMessageFilterTest : public ::testing::Test
	{
	protected:
		QmChatMessageFilter::SSettings m_Settings{true, true, true};
		QmChatMessageFilter::SMessage m_Message;

		QmChatMessageFilterTest()
		{
			m_Message.m_ClientId = 7;
			m_Message.m_OtherTeam = true;
		}

		bool Visible() const { return QmChatMessageFilter::ShouldShow(m_Settings, m_Message); }
	};
}

TEST_F(QmChatMessageFilterTest, MentionFromStrangerBypassesBothSocialFilters)
{
	m_Message.m_Mentioned = QmChatMessageFilter::MentionsName("Local Tee: hello", "Local Tee", true);
	EXPECT_TRUE(Visible());
}

TEST_F(QmChatMessageFilterTest, OrdinaryMessagesStayHiddenEvenFromFriendsAndTeammates)
{
	for(bool Friend : {false, true})
		for(bool OtherTeam : {false, true})
		{
			SCOPED_TRACE(::testing::Message() << Friend << "/" << OtherTeam);
			m_Message.m_Friend = Friend;
			m_Message.m_OtherTeam = OtherTeam;
			EXPECT_FALSE(Visible());
		}
}

TEST_F(QmChatMessageFilterTest, OwnPublicAndTeamMessagesRemainVisible)
{
	m_Message.m_CurrentLocal = true;
	m_Message.m_Local = true;
	for(int Team : {0, 1})
	{
		SCOPED_TRACE(Team);
		m_Message.m_Team = Team;
		EXPECT_TRUE(Visible());
	}
}

TEST_F(QmChatMessageFilterTest, OwnDummyMessagesRemainVisible)
{
	m_Message.m_Local = true;
	EXPECT_TRUE(Visible());
}

TEST_F(QmChatMessageFilterTest, IncomingWhisperFromStrangerRemainsVisible)
{
	m_Message.m_Team = TEAM_WHISPER_RECV;
	EXPECT_TRUE(Visible());
}

TEST_F(QmChatMessageFilterTest, OutgoingWhisperToStrangerRemainsVisible)
{
	// 发送私聊时消息中的玩家编号是收件人，不能依赖本地发件人标记。
	m_Message.m_Team = TEAM_WHISPER_SEND;
	EXPECT_TRUE(Visible());
}

TEST_F(QmChatMessageFilterTest, BlockedPlayerCannotBypassMuteByMentioningUs)
{
	m_Message.m_Mentioned = true;
	m_Message.m_Blocked = true;
	EXPECT_FALSE(Visible());
	m_Message.m_Blocked = false;
	EXPECT_TRUE(Visible());
}

TEST_F(QmChatMessageFilterTest, BlockedWhispersRemainHidden)
{
	m_Message.m_Blocked = true;
	for(int Team : {TEAM_WHISPER_SEND, TEAM_WHISPER_RECV})
	{
		SCOPED_TRACE(Team);
		m_Message.m_Team = Team;
		EXPECT_FALSE(Visible());
	}
}

TEST_F(QmChatMessageFilterTest, SystemAndClientMessagesKeepTheirSeparateDisplayRules)
{
	for(int ClientId : {-1, -2})
	{
		SCOPED_TRACE(ClientId);
		m_Message.m_ClientId = ClientId;
		EXPECT_TRUE(Visible());
	}
}

TEST_F(QmChatMessageFilterTest, DisablingRelatedModeRestoresFriendAndTeamRestrictions)
{
	m_Message.m_Mentioned = true;
	for(bool Enabled : {true, false, true, false})
	{
		SCOPED_TRACE(Enabled);
		m_Settings.m_RelatedOnly = Enabled;
		EXPECT_EQ(Visible(), Enabled);
		EXPECT_TRUE(m_Settings.m_FriendsOnly);
		EXPECT_TRUE(m_Settings.m_TeamOnly);
	}
}

TEST_F(QmChatMessageFilterTest, DisabledModeRequiresBothFriendAndTeamConditions)
{
	m_Settings.m_RelatedOnly = false;
	EXPECT_FALSE(Visible());
	m_Message.m_Friend = true;
	EXPECT_FALSE(Visible());
	m_Message.m_OtherTeam = false;
	EXPECT_TRUE(Visible());
	m_Message.m_Friend = false;
	EXPECT_FALSE(Visible());
}

TEST_F(QmChatMessageFilterTest, DisabledModePreservesCurrentCharacterException)
{
	m_Settings.m_RelatedOnly = false;
	m_Message.m_Local = true;
	EXPECT_FALSE(Visible());
	m_Message.m_CurrentLocal = true;
	EXPECT_TRUE(Visible());
}

TEST_F(QmChatMessageFilterTest, DisabledModeDoesNotExemptNonFriendWhispers)
{
	m_Settings.m_RelatedOnly = false;
	m_Message.m_Team = TEAM_WHISPER_RECV;
	EXPECT_FALSE(Visible());
	m_Settings.m_FriendsOnly = false;
	m_Settings.m_TeamOnly = false;
	EXPECT_TRUE(Visible());
}

TEST(QmChatMentions, ExistingNameAndAddressSyntaxRemainRecognized)
{
	for(const char *pText : {"Tee", "Tee: hello", "Hello tee!", "Hi Tee.", "Hey Tee, join us", "Tee?", "Tee welcome"})
	{
		SCOPED_TRACE(pText);
		EXPECT_TRUE(QmChatMessageFilter::MentionsName(pText, "Tee"));
	}
}

TEST(QmChatMentions, RelatedModeRecognizesExplicitAtSyntax)
{
	for(const char *pText : {"@Tee", "Hi @Tee!", "@Tee: hello"})
	{
		SCOPED_TRACE(pText);
		EXPECT_FALSE(QmChatMessageFilter::MentionsName(pText, "Tee"));
		EXPECT_TRUE(QmChatMessageFilter::MentionsName(pText, "Tee", true));
	}
}

TEST(QmChatMentions, PartialNamesAndEmbeddedAtSignsDoNotCount)
{
	for(const char *pText : {"Teeworlds", "OtherTee", "@Teeworlds", "other@Tee", "@@Tee", "ordinary chat", ""})
	{
		SCOPED_TRACE(pText);
		EXPECT_FALSE(QmChatMessageFilter::MentionsName(pText, "Tee", true));
	}
}

TEST(QmChatMentions, LaterCompleteOccurrenceStillCounts)
{
	EXPECT_TRUE(QmChatMessageFilter::MentionsName("OtherTee, hello Tee!", "Tee"));
	EXPECT_TRUE(QmChatMessageFilter::MentionsName("Teeworlds @Tee: hello", "Tee", true));
}

TEST(QmChatMentions, NamesWithSpacesAndNonAsciiCharactersAreSupported)
{
	EXPECT_TRUE(QmChatMessageFilter::MentionsName("Local Tee: hello", "Local Tee"));
	EXPECT_TRUE(QmChatMessageFilter::MentionsName("@奇门: 你好", "奇门", true));
	EXPECT_TRUE(QmChatMessageFilter::MentionsName("Привет ТИ!", "ти"));
	EXPECT_FALSE(QmChatMessageFilter::MentionsName("@奇门甲: 你好", "奇门", true));
}

TEST(QmChatMentions, EmptyLocalNameNeverMatches)
{
	EXPECT_FALSE(QmChatMessageFilter::MentionsName("hello", ""));
	EXPECT_FALSE(QmChatMessageFilter::MentionsName("@ hello", "", true));
}
