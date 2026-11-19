#include <game/client/components/qmclient/friend_online_notify.h>

#include <gtest/gtest.h>
#include <test/support/friend_online_tracker_test_support.h>

#include <string>
#include <vector>

namespace
{
	class QmFriendOnlineNotify : public ::testing::Test
	{
	protected:
		qm_friend_notify::COnlineNotifier m_Notifier;
		const qm_friend_notify::CFriend m_Alice{"Alice\tClan", "Alice", "Map", "server"};
		const qm_friend_notify::CFriend m_Bob{"Bob\tClan", "Bob", "Other", "server"};
		std::vector<std::string> m_vNames;

		bool Update(const std::vector<qm_friend_notify::CFriend> &vFriends, float Now)
		{
			m_Notifier.Prune(Now);
			return m_Notifier.Update(vFriends, {"server"}, Now, [&](const auto &Friend) { m_vNames.push_back(Friend.m_Name); });
		}
	};
}

TEST_F(QmFriendOnlineNotify, InitialFriendsStaySilentAndNewFriendNotifiesOnce)
{
	ASSERT_TRUE(Update({m_Alice}, 0.0f));
	EXPECT_TRUE(m_vNames.empty());
	ASSERT_TRUE(Update({m_Alice, m_Bob}, 1.0f));
	EXPECT_EQ(m_vNames, std::vector<std::string>{"Bob"});
	ASSERT_TRUE(Update({m_Alice, m_Bob}, 2.0f));
	EXPECT_EQ(m_vNames, std::vector<std::string>{"Bob"});
}

TEST_F(QmFriendOnlineNotify, ReturnAtTenSecondBoundaryStaysSuppressed)
{
	ASSERT_TRUE(Update({m_Alice}, 0.0f));
	ASSERT_TRUE(Update({}, 1.0f));
	ASSERT_TRUE(Update({}, 2.0f));
	ASSERT_TRUE(Update({m_Alice}, 10.0f));
	EXPECT_TRUE(m_vNames.empty());
}

TEST_F(QmFriendOnlineNotify, ReturnAfterTenSecondsNotifies)
{
	ASSERT_TRUE(Update({m_Alice}, 0.0f));
	ASSERT_TRUE(Update({}, 1.0f));
	ASSERT_TRUE(Update({}, 2.0f));
	ASSERT_TRUE(Update({m_Alice}, 10.01f));
	EXPECT_EQ(m_vNames, std::vector<std::string>{"Alice"});
}

TEST_F(QmFriendOnlineNotify, UnmatchedClanPresenceDoesNotExtendSuppressionTimestamp)
{
	const qm_friend_notify::CFriend Unmatched{"Alice\tOther", "Alice", "Map", "server", false};
	ASSERT_TRUE(Update({m_Alice}, 0.0f));
	ASSERT_TRUE(Update({Unmatched}, 9.0f));
	ASSERT_TRUE(Update({}, 9.1f));
	ASSERT_TRUE(Update({}, 9.2f));
	ASSERT_TRUE(Update({m_Alice}, 10.01f));
	EXPECT_EQ(m_vNames, std::vector<std::string>{"Alice"});
}

TEST_F(QmFriendOnlineNotify, PreparationFailureKeepsBaselineAndSuppressionEligibleForRetry)
{
	ASSERT_TRUE(Update({}, 0.0f));
	QmFriendTests::CFailingFriendCopy Copy{2};
	const auto Notify = [&](const auto &Friend) { m_vNames.push_back(Friend.m_Name); };
	EXPECT_FALSE(m_Notifier.Update({m_Alice, m_Bob}, {"server"}, 1.0f, Notify, Copy));
	EXPECT_TRUE(m_vNames.empty());
	ASSERT_TRUE(Update({m_Alice, m_Bob}, 2.0f));
	EXPECT_EQ(m_vNames, (std::vector<std::string>{"Alice", "Bob"}));
}

TEST_F(QmFriendOnlineNotify, FailedObservationDoesNotRefreshExistingSuppressionTimestamp)
{
	ASSERT_TRUE(Update({m_Alice}, 0.0f));
	ASSERT_TRUE(Update({}, 1.0f));
	ASSERT_TRUE(Update({}, 2.0f));
	QmFriendTests::CFailingFriendCopy Copy{1};
	const auto Notify = [&](const auto &Friend) { m_vNames.push_back(Friend.m_Name); };
	EXPECT_FALSE(m_Notifier.Update({m_Alice}, {"server"}, 9.0f, Notify, Copy));
	ASSERT_TRUE(Update({m_Alice}, 10.01f));
	EXPECT_EQ(m_vNames, std::vector<std::string>{"Alice"});
}

TEST_F(QmFriendOnlineNotify, OutputAllocationFailureContinuesLaterFriendsWithoutReplay)
{
	ASSERT_TRUE(Update({}, 0.0f));
	std::vector<std::string> vAttempted;
	const auto Notify = [&](const auto &Friend) {
		vAttempted.push_back(Friend.m_Name);
		if(vAttempted.size() == 1U)
			throw std::bad_alloc();
		m_vNames.push_back(Friend.m_Name);
	};
	ASSERT_TRUE(m_Notifier.Update({m_Alice, m_Bob}, {"server"}, 1.0f, Notify));
	EXPECT_EQ(vAttempted, (std::vector<std::string>{"Alice", "Bob"}));
	EXPECT_EQ(m_vNames, std::vector<std::string>{"Bob"});
	ASSERT_TRUE(m_Notifier.Update({m_Alice, m_Bob}, {"server"}, 2.0f, Notify));
	EXPECT_EQ(vAttempted.size(), 2U);
}

TEST_F(QmFriendOnlineNotify, UnknownOutputFailureDoesNotEscapeOrPreventLaterFriend)
{
	ASSERT_TRUE(Update({}, 0.0f));
	int Calls = 0;
	const auto Notify = [&](const auto &Friend) {
		if(++Calls == 1)
			throw 7;
		m_vNames.push_back(Friend.m_Name);
	};
	EXPECT_TRUE(m_Notifier.Update({m_Alice, m_Bob}, {"server"}, 1.0f, Notify));
	EXPECT_EQ(Calls, 2);
	EXPECT_EQ(m_vNames, std::vector<std::string>{"Bob"});
}

TEST_F(QmFriendOnlineNotify, ResetSilentlyRebuildsBaselineAndClearsSuppression)
{
	ASSERT_TRUE(Update({}, 0.0f));
	ASSERT_TRUE(Update({m_Alice}, 1.0f));
	ASSERT_EQ(m_vNames, std::vector<std::string>{"Alice"});
	m_Notifier.Reset();
	ASSERT_TRUE(Update({m_Alice, m_Bob}, 2.0f));
	EXPECT_EQ(m_vNames, std::vector<std::string>{"Alice"});
	ASSERT_TRUE(Update({}, 3.0f));
	ASSERT_TRUE(Update({}, 4.0f));
	ASSERT_TRUE(Update({m_Alice}, 13.0f));
	EXPECT_EQ(m_vNames, (std::vector<std::string>{"Alice", "Alice"}));
}
