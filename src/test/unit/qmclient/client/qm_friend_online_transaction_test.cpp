#include <game/client/components/qmclient/friend_online_tracker.h>

#include <gtest/gtest.h>
#include <test/support/friend_online_tracker_test_support.h>

namespace
{
	const qm_friend_notify::CFriend s_Alice{"Alice\tClan", "Alice", "Map", "server"};
	const qm_friend_notify::CFriend s_Bob{"Bob\tClan", "Bob", "Map", "server"};
}

TEST(QmFriendOnlineTransaction, FailedInitialObservationDoesNotPublishServerBaseline)
{
	qm_friend_notify::COnlineTracker Tracker;
	QmFriendTests::CFailingFriendCopy Copy{2};
	EXPECT_FALSE(Tracker.Update({s_Alice, s_Bob}, {"server"}, Copy).has_value());
	EXPECT_EQ(Copy.m_Calls, 2);
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {s_Bob}, {"server"}).empty());
	const auto vNotifications = QmFriendTests::UpdateOnline(Tracker, {s_Alice, s_Bob}, {"server"});
	ASSERT_EQ(vNotifications.size(), 1U);
	EXPECT_EQ(vNotifications[0].m_Name, "Alice");
}

TEST(QmFriendOnlineTransaction, LaterCopyFailureKeepsEarlierFriendEligibleForRetry)
{
	qm_friend_notify::COnlineTracker Tracker;
	ASSERT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"server"}).empty());
	QmFriendTests::CFailingFriendCopy Copy{2};
	EXPECT_FALSE(Tracker.Update({s_Alice, s_Bob}, {"server"}, Copy).has_value());
	const auto vNotifications = QmFriendTests::UpdateOnline(Tracker, {s_Alice, s_Bob}, {"server"});
	ASSERT_EQ(vNotifications.size(), 2U);
	EXPECT_EQ(vNotifications[0].m_Name, "Alice");
	EXPECT_EQ(vNotifications[1].m_Name, "Bob");
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {s_Alice, s_Bob}, {"server"}).empty());
}

TEST(QmFriendOnlineTransaction, FailureAfterClanMigrationRetainsOriginalIdentity)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Original{"Alice\tOld", "Alice", "Map", "first"};
	const qm_friend_notify::CFriend Changed{"Alice\tNew", "Alice", "Map", "first"};
	const qm_friend_notify::CFriend Moved{"Alice\tOld", "Alice", "Map", "second"};
	ASSERT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Original}, {"first", "second", "server"}).empty());
	QmFriendTests::CFailingFriendCopy Copy{2};
	EXPECT_FALSE(Tracker.Update({Changed, s_Bob}, {"first", "second", "server"}, Copy).has_value());
	// 原身份随后换服仍是已知好友；失败候选里的新 Clan 身份不得取代它。
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Moved}, {"first", "second", "server"}).empty());
}

TEST(QmFriendOnlineTransaction, FailedPresenceDoesNotResetConsecutiveMissingEvidence)
{
	qm_friend_notify::COnlineTracker Tracker;
	ASSERT_TRUE(QmFriendTests::UpdateOnline(Tracker, {s_Alice}, {"server"}).empty());
	ASSERT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"server"}).empty());
	QmFriendTests::CFailingFriendCopy Copy{2};
	EXPECT_FALSE(Tracker.Update({s_Alice, s_Bob}, {"server"}, Copy).has_value());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"server"}).empty());
	const auto vNotifications = QmFriendTests::UpdateOnline(Tracker, {s_Alice}, {"server"});
	ASSERT_EQ(vNotifications.size(), 1U);
	EXPECT_EQ(vNotifications[0].m_Name, "Alice");
}

TEST(QmFriendOnlineTransaction, UnknownCopyFailurePublishesNoPartialObservation)
{
	qm_friend_notify::COnlineTracker Tracker;
	ASSERT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"server"}).empty());
	int Calls = 0;
	const auto Copy = [&](const qm_friend_notify::CFriend &Friend) {
		if(++Calls == 2)
			throw 7;
		return Friend;
	};
	EXPECT_FALSE(Tracker.Update({s_Alice, s_Bob}, {"server"}, Copy).has_value());
	const auto vNotifications = QmFriendTests::UpdateOnline(Tracker, {s_Alice, s_Bob}, {"server"});
	ASSERT_EQ(vNotifications.size(), 2U);
	EXPECT_EQ(vNotifications[0].m_Name, "Alice");
	EXPECT_EQ(vNotifications[1].m_Name, "Bob");
}

TEST(QmFriendOnlineTransaction, ResetAfterFailureRestoresFirstObservationSilence)
{
	qm_friend_notify::COnlineTracker Tracker;
	ASSERT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"server"}).empty());
	QmFriendTests::CFailingFriendCopy Copy{2};
	ASSERT_FALSE(Tracker.Update({s_Alice, s_Bob}, {"server"}, Copy).has_value());
	Tracker.Reset();
	auto Notifications = Tracker.Update({s_Alice, s_Bob}, {"server"});
	ASSERT_TRUE(Notifications.has_value());
	EXPECT_TRUE(Notifications->empty());
}
