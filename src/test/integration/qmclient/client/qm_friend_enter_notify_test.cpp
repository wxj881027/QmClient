#include <game/client/components/qmclient/friend_enter_notify.h>

#include <gtest/gtest.h>

#include <new>
#include <string>
#include <vector>

namespace
{
	enum class EFailure
	{
		NONE,
		ALLOCATION,
		UNKNOWN,
	};

	void FailIfRequested(EFailure Failure)
	{
		if(Failure == EFailure::ALLOCATION)
			throw std::bad_alloc();
		if(Failure == EFailure::UNKNOWN)
			throw 7;
	}

	class QmFriendEnterNotifyIntegration : public ::testing::Test
	{
	protected:
		using CClient = qm_friend_notify::CEnterTracker::CClient;
		qm_friend_notify::CEnterNotifier m_Notifier;
		qm_friend_notify::CEnterNotifier::CSettings m_Settings{true, true, true, false};
		const CClient m_Existing{0, "Existing", "Clan", true, false};
		const CClient m_Alice{1, "Alice", "Clan", true, false};
		const CClient m_Bob{2, "Bob", "Clan", true, false};
		std::vector<CClient> m_vClients;
		std::vector<std::string> m_vBroadcasts;
		std::vector<std::string> m_vGreets;
		std::vector<std::string> m_vOrder;
		std::string m_GreetText = ": hi";
		int m_Collections = 0;
		int m_Preparations = 0;
		int m_BroadcastAttempts = 0;
		int m_GreetAttempts = 0;
		bool m_ResetDuringBroadcast = false;
		EFailure m_CollectFailure = EFailure::NONE;
		EFailure m_PrepareFailure = EFailure::NONE;
		EFailure m_BroadcastFailure = EFailure::NONE;
		EFailure m_GreetFailure = EFailure::NONE;

		bool Step(float Now)
		{
			return m_Notifier.Update(Now, m_Settings, [&]() {
					++m_Collections;
					m_vOrder.emplace_back("collect");
					auto vClients = m_vClients;
					FailIfRequested(m_CollectFailure);
					return vClients; }, [&](const std::string &Names) {
					++m_Preparations;
					auto Text = "notice:" + Names;
					FailIfRequested(m_PrepareFailure);
					return Text; }, [&](const std::string &Text) {
					++m_BroadcastAttempts;
					m_vOrder.emplace_back("broadcast");
					FailIfRequested(m_BroadcastFailure);
					m_vBroadcasts.push_back(Text);
					if(m_ResetDuringBroadcast)
						m_Notifier.Reset(); }, [&](const std::string &Names) {
					++m_GreetAttempts;
					m_vOrder.emplace_back("greet");
					FailIfRequested(m_GreetFailure);
					m_vGreets.push_back(Names + m_GreetText); });
		}
	};
}

TEST_F(QmFriendEnterNotifyIntegration, InitialRosterIsSilentAndArrivalsShareOriginalGreetDeadline)
{
	m_vClients = {m_Existing};
	ASSERT_TRUE(Step(0.0f));
	EXPECT_TRUE(m_vBroadcasts.empty());
	m_vClients = {m_Existing, m_Alice};
	ASSERT_TRUE(Step(0.25f));
	m_vClients = {m_Existing, m_Alice, m_Bob};
	ASSERT_TRUE(Step(0.5f));
	EXPECT_EQ(m_vBroadcasts, (std::vector<std::string>{"notice:Alice", "notice:Bob"}));
	EXPECT_TRUE(m_vGreets.empty());
	ASSERT_TRUE(Step(3.24f));
	EXPECT_FALSE(Step(3.25f));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice Bob: hi"});
	ASSERT_TRUE(Step(3.5f));
	EXPECT_EQ(m_GreetAttempts, 1);
}

TEST_F(QmFriendEnterNotifyIntegration, MultipleArrivalsInOneRosterUseOneJoinedNotification)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice, m_Bob};
	ASSERT_TRUE(Step(0.25f));
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Alice Bob"});
	ASSERT_TRUE(Step(3.25f));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice Bob: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, CollectionRunsAtTheOriginalTwoHundredMillisecondBoundary)
{
	ASSERT_TRUE(Step(0.0f));
	EXPECT_FALSE(Step(0.125f));
	EXPECT_EQ(m_Collections, 1);
	ASSERT_TRUE(Step(0.2f));
	EXPECT_FALSE(Step(0.375f));
	EXPECT_EQ(m_Collections, 2);
	ASSERT_TRUE(Step(0.4f));
	EXPECT_EQ(m_Collections, 3);
}

TEST_F(QmFriendEnterNotifyIntegration, DisabledModesCancelPendingAndReenableSilently)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	ASSERT_TRUE(Step(0.25f));
	const int Collections = m_Collections;
	m_Settings.m_GreetEnabled = m_Settings.m_BroadcastEnabled = false;
	EXPECT_FALSE(Step(0.3f));
	EXPECT_EQ(m_Collections, Collections);
	m_Settings.m_GreetEnabled = m_Settings.m_BroadcastEnabled = true;
	m_vClients = {m_Bob};
	ASSERT_TRUE(Step(0.375f));
	ASSERT_TRUE(Step(4.0f));
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Alice"});
	EXPECT_TRUE(m_vGreets.empty());
}

TEST_F(QmFriendEnterNotifyIntegration, BroadcastOnlyDoesNotQueueGreet)
{
	m_Settings.m_GreetEnabled = false;
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	ASSERT_TRUE(Step(0.25f));
	ASSERT_TRUE(Step(4.0f));
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Alice"});
	EXPECT_TRUE(m_vGreets.empty());
}

TEST_F(QmFriendEnterNotifyIntegration, GreetOnlyDoesNotPrepareOrShowBroadcast)
{
	m_Settings.m_BroadcastEnabled = false;
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	ASSERT_TRUE(Step(0.25f));
	ASSERT_TRUE(Step(3.25f));
	EXPECT_EQ(m_Preparations, 0);
	EXPECT_EQ(m_BroadcastAttempts, 0);
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, EmptyGreetTextDoesNotQueueNamesForLaterEnable)
{
	m_Settings.m_GreetTextConfigured = false;
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	ASSERT_TRUE(Step(0.25f));
	ASSERT_TRUE(Step(3.25f));
	EXPECT_TRUE(m_vGreets.empty());
	m_Settings.m_GreetTextConfigured = true;
	m_vClients = {m_Alice, m_Bob};
	ASSERT_TRUE(Step(3.5f));
	ASSERT_TRUE(Step(6.5f));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Bob: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, ClearingTextConsumesDueBatchWithoutSending)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	ASSERT_TRUE(Step(0.25f));
	m_Settings.m_GreetTextConfigured = false;
	ASSERT_TRUE(Step(3.25f));
	m_Settings.m_GreetTextConfigured = true;
	ASSERT_TRUE(Step(3.5f));
	EXPECT_EQ(m_GreetAttempts, 0);
	EXPECT_TRUE(m_vGreets.empty());
}

TEST_F(QmFriendEnterNotifyIntegration, GreetUsesCurrentTextWhenTheBatchBecomesDue)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	ASSERT_TRUE(Step(0.25f));
	m_GreetText = ": changed";
	ASSERT_TRUE(Step(3.25f));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: changed"});
}

TEST_F(QmFriendEnterNotifyIntegration, CollectionFailureRetainsBaselineAndWaitsForNextCheck)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	m_CollectFailure = EFailure::ALLOCATION;
	EXPECT_FALSE(Step(0.25f));
	m_CollectFailure = EFailure::NONE;
	const int Collections = m_Collections;
	EXPECT_FALSE(Step(0.375f));
	EXPECT_EQ(m_Collections, Collections);
	ASSERT_TRUE(Step(0.5f));
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Alice"});
	ASSERT_TRUE(Step(3.25f));
	EXPECT_TRUE(m_vGreets.empty());
	ASSERT_TRUE(Step(3.5f));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, FailedFirstCollectionDoesNotInitializeSilentBaseline)
{
	m_vClients = {m_Alice};
	m_CollectFailure = EFailure::ALLOCATION;
	EXPECT_FALSE(Step(0.0f));
	m_CollectFailure = EFailure::NONE;
	ASSERT_TRUE(Step(0.25f));
	ASSERT_TRUE(Step(3.25f));
	EXPECT_TRUE(m_vBroadcasts.empty());
	EXPECT_TRUE(m_vGreets.empty());
}

TEST_F(QmFriendEnterNotifyIntegration, PreparationFailureRetainsExistingNamesAndDeadline)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	ASSERT_TRUE(Step(0.25f));
	m_vClients = {m_Alice, m_Bob};
	m_PrepareFailure = EFailure::ALLOCATION;
	EXPECT_FALSE(Step(0.5f));
	EXPECT_FALSE(Step(3.25f));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Alice"});
	m_PrepareFailure = EFailure::NONE;
	ASSERT_TRUE(Step(3.5f));
	ASSERT_TRUE(Step(6.5f));
	EXPECT_EQ(m_vBroadcasts, (std::vector<std::string>{"notice:Alice", "notice:Bob"}));
	EXPECT_EQ(m_vGreets, (std::vector<std::string>{"Alice: hi", "Bob: hi"}));
}

TEST_F(QmFriendEnterNotifyIntegration, UnknownPreparationFailureAllowsSameArrivalToRetry)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	m_PrepareFailure = EFailure::UNKNOWN;
	EXPECT_FALSE(Step(0.25f));
	EXPECT_TRUE(m_vBroadcasts.empty());
	m_PrepareFailure = EFailure::NONE;
	ASSERT_TRUE(Step(0.5f));
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Alice"});
}

TEST_F(QmFriendEnterNotifyIntegration, FailedObservationDoesNotStartMissingGracePeriod)
{
	m_vClients = {m_Alice};
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Bob};
	m_PrepareFailure = EFailure::ALLOCATION;
	EXPECT_FALSE(Step(0.25f));
	m_PrepareFailure = EFailure::NONE;
	m_vClients.clear();
	ASSERT_TRUE(Step(2.0f));
	m_vClients = {{3, "Alice", "Clan", true, false}};
	ASSERT_TRUE(Step(3.3f));
	EXPECT_TRUE(m_vBroadcasts.empty());
	EXPECT_TRUE(m_vGreets.empty());
}

TEST_F(QmFriendEnterNotifyIntegration, DueGreetRunsBeforeCollectionFailure)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	ASSERT_TRUE(Step(0.25f));
	m_CollectFailure = EFailure::ALLOCATION;
	m_vOrder.clear();
	EXPECT_FALSE(Step(3.25f));
	EXPECT_EQ(m_vOrder, (std::vector<std::string>{"greet", "collect"}));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, GreetFailureIsNotRepeatedAndLaterArrivalsContinue)
{
	for(const EFailure Failure : {EFailure::ALLOCATION, EFailure::UNKNOWN})
	{
		SCOPED_TRACE(static_cast<int>(Failure));
		m_Notifier.Reset();
		m_vClients.clear();
		m_vBroadcasts.clear();
		m_vGreets.clear();
		m_GreetAttempts = 0;
		ASSERT_TRUE(Step(0.0f));
		m_vClients = {m_Alice};
		ASSERT_TRUE(Step(0.25f));
		m_vClients = {m_Alice, m_Bob};
		m_GreetFailure = Failure;
		ASSERT_TRUE(Step(3.25f));
		EXPECT_EQ(m_GreetAttempts, 1);
		EXPECT_TRUE(m_vGreets.empty());
		m_GreetFailure = EFailure::NONE;
		ASSERT_TRUE(Step(3.5f));
		EXPECT_EQ(m_GreetAttempts, 1);
		ASSERT_TRUE(Step(6.25f));
		EXPECT_EQ(m_GreetAttempts, 2);
		EXPECT_EQ(m_vGreets, std::vector<std::string>{"Bob: hi"});
	}
}

TEST_F(QmFriendEnterNotifyIntegration, BroadcastFailureDoesNotReplayOrPreventQueuedGreet)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	m_BroadcastFailure = EFailure::UNKNOWN;
	ASSERT_TRUE(Step(0.25f));
	EXPECT_EQ(m_BroadcastAttempts, 1);
	EXPECT_TRUE(m_vBroadcasts.empty());
	m_BroadcastFailure = EFailure::NONE;
	ASSERT_TRUE(Step(0.5f));
	EXPECT_EQ(m_BroadcastAttempts, 1);
	ASSERT_TRUE(Step(3.25f));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Alice: hi"});
}

TEST_F(QmFriendEnterNotifyIntegration, ResetCancelsPendingAndStartsAnotherSilentRoster)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	ASSERT_TRUE(Step(0.25f));
	m_Notifier.Reset();
	m_vClients = {m_Bob};
	ASSERT_TRUE(Step(0.375f));
	ASSERT_TRUE(Step(4.0f));
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Alice"});
	EXPECT_TRUE(m_vGreets.empty());
}

TEST_F(QmFriendEnterNotifyIntegration, ResetFromBroadcastKeepsPendingBatchCancelled)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {m_Alice};
	m_ResetDuringBroadcast = true;
	ASSERT_TRUE(Step(0.25f));
	m_ResetDuringBroadcast = false;
	ASSERT_TRUE(Step(0.375f));
	ASSERT_TRUE(Step(4.0f));
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Alice"});
	EXPECT_TRUE(m_vGreets.empty());
}

TEST_F(QmFriendEnterNotifyIntegration, IgnoreClanIsForwardedToIdentityMatching)
{
	for(const bool IgnoreClan : {false, true})
	{
		SCOPED_TRACE(IgnoreClan);
		m_Notifier.Reset();
		m_Settings.m_IgnoreClan = IgnoreClan;
		m_vBroadcasts.clear();
		m_vClients = {{1, "Alice", "Old", true, false}};
		ASSERT_TRUE(Step(0.0f));
		m_vClients.clear();
		ASSERT_TRUE(Step(0.25f));
		m_vClients = {{3, "Alice", "New", true, false}};
		ASSERT_TRUE(Step(0.5f));
		EXPECT_EQ(m_vBroadcasts.size(), IgnoreClan ? 0U : 1U);
	}
}

TEST_F(QmFriendEnterNotifyIntegration, LocalMainDummyAndNonFriendsDoNotJoinOutput)
{
	ASSERT_TRUE(Step(0.0f));
	m_vClients = {{10, "Main", "Clan", true, true}, {11, "Dummy", "Clan", true, true},
		{12, "Stranger", "Clan", false, false}, {13, "Remote", "Clan", true, false}};
	ASSERT_TRUE(Step(0.25f));
	EXPECT_EQ(m_vBroadcasts, std::vector<std::string>{"notice:Remote"});
	ASSERT_TRUE(Step(3.25f));
	EXPECT_EQ(m_vGreets, std::vector<std::string>{"Remote: hi"});
}
