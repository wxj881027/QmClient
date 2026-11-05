#include <engine/client/friends.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/friend_enter_notify.h>

#include <gtest/gtest.h>

#include <memory>
#include <new>
#include <string>
#include <vector>

namespace
{
	enum class EFailure
	{
		NONE,
		ALLOCATION,
		UNKNOWN
	};
	void FailIfRequested(EFailure Failure)
	{
		if(Failure == EFailure::ALLOCATION)
			throw std::bad_alloc();
		if(Failure == EFailure::UNKNOWN)
			throw 7;
	}
	class QmFriendEnterNotifyIntegration : public testing::Test
	{
	protected:
		int m_PreviousIgnoreClan = g_Config.m_ClFriendsIgnoreClan;
		std::unique_ptr<CFriends> m_pFriends = std::make_unique<CFriends>();
		qm_friend_notify::CEnterNotifier m_Notifier;
		qm_friend_notify::CEnterNotifier::CSettings m_Settings{true, true, true};
		std::vector<std::string> m_vBroadcasts;
		std::vector<std::string> m_vGreets;
		std::string m_GreetText = ": hi";
		std::string m_MainName = "Main";
		std::string m_DummyName = "Dummy";
		int m_Preparations = 0;
		int m_BroadcastAttempts = 0;
		int m_GreetAttempts = 0;
		bool m_GreetAccepted = true;
		bool m_ResetDuringBroadcast = false;
		EFailure m_ParseFailure = EFailure::NONE;
		EFailure m_PrepareFailure = EFailure::NONE;
		EFailure m_BroadcastFailure = EFailure::NONE;
		EFailure m_GreetFailure = EFailure::NONE;
		void SetUp() override
		{
			g_Config.m_ClFriendsIgnoreClan = 0;
			m_pFriends->AddFriend("Alice", "SavedClan");
			m_pFriends->AddFriend("Bob", "OtherClan");
		}
		void TearDown() override { g_Config.m_ClFriendsIgnoreClan = m_PreviousIgnoreClan; }
		bool Retry()
		{
			return m_Notifier.TrySend(m_Settings, [&](const std::string &Names) {
				++m_GreetAttempts;
				FailIfRequested(m_GreetFailure);
				if(!m_GreetAccepted)
					return false;
				m_vGreets.push_back(Names + m_GreetText);
				return true;
			});
		}
		bool Step(const char *pText)
		{
			const bool Entered = m_Notifier.OnMessage(m_Settings, [&](qm_friend_notify::CEnterTracker &Tracker) {
     const auto Message = Tracker.OnMessage(-1, 0, pText, *m_pFriends, m_MainName, m_DummyName);
     FailIfRequested(m_ParseFailure);
     return Message; }, [&](const std::string &Name) {
     ++m_Preparations;
     auto Text = "notice:" + Name;
     FailIfRequested(m_PrepareFailure);
     return Text; }, [&](const std::string &Text) {
     ++m_BroadcastAttempts;
     FailIfRequested(m_BroadcastFailure);
     m_vBroadcasts.push_back(Text);
     if(m_ResetDuringBroadcast)
      m_Notifier.Reset(); });
			if(Entered)
				Retry();
			return Entered;
		}
	};
}

TEST_F(QmFriendEnterNotifyIntegration, ServerEnterImmediatelyBroadcastsAndAttemptsGreet)
{
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Alice"});
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, DuplicateMessagesDoNotReplayAcceptedArrival)
{
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	EXPECT_FALSE(Step("'Alice' entered and joined the game"));
	EXPECT_EQ(m_BroadcastAttempts, 1);
	EXPECT_EQ(m_GreetAttempts, 1);
}

TEST_F(QmFriendEnterNotifyIntegration, DeniedGreetMergesALaterServerArrival)
{
	m_GreetAccepted = false;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	ASSERT_TRUE(Step("'Bob' entered and joined the game"));
	m_GreetAccepted = true;
	EXPECT_TRUE(Retry());
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice Bob: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, LeaveCancelsPendingAndAllowsImmediateReentry)
{
	m_GreetAccepted = false;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	EXPECT_FALSE(Step("'Alice' has left the game (Timeout)"));
	m_GreetAccepted = true;
	EXPECT_FALSE(Retry());
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, DisabledModesCancelPendingAndAcceptFreshMessagesAfterReenable)
{
	m_GreetAccepted = false;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	m_Settings.m_GreetEnabled = m_Settings.m_BroadcastEnabled = false;
	EXPECT_FALSE(Step("'Bob' entered and joined the game"));
	m_Settings.m_GreetEnabled = m_Settings.m_BroadcastEnabled = true;
	m_GreetAccepted = true;
	ASSERT_TRUE(Step("'Bob' entered and joined the game"));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Bob: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, BroadcastOnlyDoesNotQueueGreeting)
{
	m_Settings.m_GreetEnabled = false;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Alice"});
	EXPECT_FALSE(Retry());
	EXPECT_TRUE(m_vGreets.empty());
}

TEST_F(QmFriendEnterNotifyIntegration, GreetOnlyDoesNotPrepareBroadcast)
{
	m_Settings.m_BroadcastEnabled = false;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	EXPECT_EQ(m_Preparations, 0);
	EXPECT_EQ(m_BroadcastAttempts, 0);
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, EmptyTextDoesNotQueueOldNamesForLaterEnable)
{
	m_Settings.m_GreetTextConfigured = false;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	m_Settings.m_GreetTextConfigured = true;
	EXPECT_FALSE(Step("'Alice' entered and joined the game"));
	ASSERT_TRUE(Step("'Bob' entered and joined the game"));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Bob: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, ClearingTextCancelsDeniedNames)
{
	m_GreetAccepted = false;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	m_Settings.m_GreetTextConfigured = false;
	EXPECT_FALSE(Retry());
	m_Settings.m_GreetTextConfigured = true;
	m_GreetAccepted = true;
	EXPECT_FALSE(Retry());
	EXPECT_TRUE(m_vGreets.empty());
}

TEST_F(QmFriendEnterNotifyIntegration, RetriedGreetUsesCurrentText)
{
	m_GreetAccepted = false;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	m_GreetText = ": changed";
	m_GreetAccepted = true;
	EXPECT_TRUE(Retry());
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: changed"});
}

TEST_F(QmFriendEnterNotifyIntegration, ParserFailureCannotCommitPartiallyObservedPresence)
{
	for(const EFailure Failure : {EFailure::ALLOCATION, EFailure::UNKNOWN})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		m_Notifier.Reset();
		m_vGreets.clear();
		m_ParseFailure = Failure;
		EXPECT_FALSE(Step("'Alice' entered and joined the game"));
		m_ParseFailure = EFailure::NONE;
		ASSERT_TRUE(Step("'Alice' entered and joined the game"));
		EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
	}
}

TEST_F(QmFriendEnterNotifyIntegration, PreparationFailureKeepsDeniedNamesAndLeavesNewMessageUnaccepted)
{
	m_GreetAccepted = false;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	m_PrepareFailure = EFailure::ALLOCATION;
	EXPECT_FALSE(Step("'Bob' entered and joined the game"));
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Alice"});
	m_PrepareFailure = EFailure::NONE;
	m_GreetAccepted = true;
	ASSERT_TRUE(Step("'Bob' entered and joined the game"));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice Bob: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, UnknownPreparationFailureAllowsSameMessageToBeAcceptedLater)
{
	m_PrepareFailure = EFailure::UNKNOWN;
	EXPECT_FALSE(Step("'Alice' entered and joined the game"));
	m_PrepareFailure = EFailure::NONE;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, SendExceptionKeepsUnconfirmedNamesForRetry)
{
	for(const EFailure Failure : {EFailure::ALLOCATION, EFailure::UNKNOWN})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		m_Notifier.Reset();
		m_vGreets.clear();
		m_GreetFailure = Failure;
		ASSERT_TRUE(Step("'Alice' entered and joined the game"));
		m_GreetFailure = EFailure::NONE;
		EXPECT_TRUE(Retry());
		EXPECT_FALSE(Retry());
		EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
	}
}

TEST_F(QmFriendEnterNotifyIntegration, BroadcastFailureDoesNotReplayOrBlockPreparedGreet)
{
	m_BroadcastFailure = EFailure::UNKNOWN;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	m_BroadcastFailure = EFailure::NONE;
	EXPECT_FALSE(Step("'Alice' entered and joined the game"));
	EXPECT_EQ(m_BroadcastAttempts, 1);
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, ResetCancelsPendingAndAcceptsNextServerMessage)
{
	m_GreetAccepted = false;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	m_Notifier.Reset();
	m_GreetAccepted = true;
	ASSERT_TRUE(Step("'Bob' entered and joined the game"));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Bob: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, ResetFromBroadcastKeepsNewPendingGreetingCancelled)
{
	m_ResetDuringBroadcast = true;
	ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	m_ResetDuringBroadcast = false;
	EXPECT_FALSE(Retry());
	EXPECT_EQ(m_GreetAttempts, 0);
}

TEST_F(QmFriendEnterNotifyIntegration, ExactNicknameMatchingKeepsCurrentMainClanRule)
{
	for(const int IgnoreClan : {0, 1})
	{
		SCOPED_TRACE(IgnoreClan);
		g_Config.m_ClFriendsIgnoreClan = IgnoreClan;
		m_Notifier.Reset();
		EXPECT_FALSE(Step("'alice' entered and joined the game"));
		ASSERT_TRUE(Step("'Alice' entered and joined the game"));
	}
	EXPECT_EQ(m_vGreets, (std::vector<std::string>{"Alice: hi", "Alice: hi"}));
}

TEST_F(QmFriendEnterNotifyIntegration, LocalConnectionsAndNonFriendsDoNotEnterOutput)
{
	m_MainName = "Alice";
	EXPECT_FALSE(Step("'Alice' entered and joined the game"));
	m_MainName = "Main";
	m_DummyName = "Bob";
	EXPECT_FALSE(Step("'Bob' entered and joined the game"));
	EXPECT_FALSE(Step("'Stranger' entered and joined the game"));
	EXPECT_TRUE(m_vGreets.empty());
	EXPECT_TRUE(m_vBroadcasts.empty());
}
