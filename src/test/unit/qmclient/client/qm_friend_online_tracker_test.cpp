#include <game/client/components/qmclient/friend_online_tracker.h>

#include <gtest/gtest.h>
#include <test/support/friend_online_tracker_test_support.h>
TEST(QmFriendOnlineTracker, FirstSnapshotAndStableFriendsStaySilent)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"}).empty());
}
TEST(QmFriendOnlineTracker, NewFriendNotifiesOnceAfterBaseline)
{
	qm_friend_notify::COnlineTracker Tracker;
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"server"}).empty());
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	const auto vNotifications = QmFriendTests::UpdateOnline(Tracker, {Friend, Friend}, {"server"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Name, "Alice");
	EXPECT_EQ(vNotifications[0].m_Map, "Map");
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, OneMissingSnapshotDoesNotReannounceFriend)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"server"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"server"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, TwoMissingSnapshotsAllowReturnNotification)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"server"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"server"}).empty());
	const auto vNotifications = QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Key, Friend.m_Key);
}

TEST(QmFriendOnlineTracker, MissingOrIncompleteServerDoesNotConfirmOffline)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server", "other"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"other"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {"other"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server", "other"}).empty());
}

TEST(QmFriendOnlineTracker, UnavailableServerBreaksConsecutiveMissingEvidence)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"});
	QmFriendTests::UpdateOnline(Tracker, {}, {"server"});
	QmFriendTests::UpdateOnline(Tracker, {}, {"other"});
	QmFriendTests::UpdateOnline(Tracker, {}, {"server"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, EmptyListsNeitherCreateBaselineNorExpireFriends)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {}, {}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, ClanChangeOnSameServerStaysSilent)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Original{"Alice\tOld", "Alice", "Map", "server"};
	const qm_friend_notify::CFriend Changed{"Alice\tNew", "Alice", "Map", "server"};
	QmFriendTests::UpdateOnline(Tracker, {Original}, {"server"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Changed}, {"server"}).empty());
	QmFriendTests::UpdateOnline(Tracker, {}, {"server"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Changed}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, MapAndServerChangesKeepFriendOnline)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Original{"Alice\tClan", "Alice", "OldMap", "old"};
	const qm_friend_notify::CFriend Moved{"Alice\tClan", "Alice", "NewMap", "new"};
	QmFriendTests::UpdateOnline(Tracker, {Original}, {"old", "new"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Moved}, {"old", "new"}).empty());
	QmFriendTests::UpdateOnline(Tracker, {}, {"old"});
	QmFriendTests::UpdateOnline(Tracker, {}, {"old"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Moved}, {"new"}).empty());
}

TEST(QmFriendOnlineTracker, ResetSilentlyRebuildsBaseline)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	QmFriendTests::UpdateOnline(Tracker, {}, {"server"});
	Tracker.Reset();
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Friend}, {"server"}).empty());
	const qm_friend_notify::CFriend NewFriend{"Bob\tClan", "Bob", "Map", "server"};
	const auto vNotifications = QmFriendTests::UpdateOnline(Tracker, {Friend, NewFriend}, {"server"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Name, "Bob");
}

TEST(QmFriendOnlineTracker, FirstCompleteObservationOfMissingServerStaysSilent)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Existing{"Alice\tClan", "Alice", "Map", "first"};
	const qm_friend_notify::CFriend InitiallyMissing{"Bob\tClan", "Bob", "Map", "second"};
	QmFriendTests::UpdateOnline(Tracker, {Existing}, {"first"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Existing, InitiallyMissing}, {"first", "second"}).empty());
	const qm_friend_notify::CFriend NewFriend{"Carol\tClan", "Carol", "Map", "second"};
	const auto vNotifications = QmFriendTests::UpdateOnline(Tracker, {Existing, InitiallyMissing, NewFriend}, {"first", "second"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Name, "Carol");
}

TEST(QmFriendOnlineTracker, IncompleteServerDoesNotCreateNotificationBaseline)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend PartiallyVisible{"Alice\tClan", "Alice", "Map", "partial"};
	const qm_friend_notify::CFriend InitiallyMissing{"Bob\tClan", "Bob", "Map", "partial"};
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {PartiallyVisible}, {"other"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {PartiallyVisible, InitiallyMissing}, {"other", "partial"}).empty());
	const qm_friend_notify::CFriend NewFriend{"Carol\tClan", "Carol", "Map", "partial"};
	const auto vNotifications = QmFriendTests::UpdateOnline(Tracker, {PartiallyVisible, InitiallyMissing, NewFriend}, {"other", "partial"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Name, "Carol");
}

TEST(QmFriendOnlineTracker, ExistingSameNameBecomingFriendByClanStaysSilent)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Unmatched{"Alice\tOther", "Alice", "Map", "server", false};
	const qm_friend_notify::CFriend Matched{"Alice\tClan", "Alice", "Map", "server"};
	QmFriendTests::UpdateOnline(Tracker, {Unmatched}, {"server"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Matched}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, TemporaryUnmatchedClanKeepsPresenceUntilClanReturns)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Matched{"Alice\tClan", "Alice", "Map", "server"};
	const qm_friend_notify::CFriend Unmatched{"Alice\tOther", "Alice", "Map", "server", false};
	QmFriendTests::UpdateOnline(Tracker, {Matched}, {"server"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Unmatched}, {"server"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Unmatched}, {"server"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Unmatched}, {"server"}).empty());
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Matched}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, NewSameNameWithUnmatchedClanDoesNotNotify)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Unmatched{"Alice\tOther", "Alice", "Map", "server", false};
	QmFriendTests::UpdateOnline(Tracker, {}, {"server"});
	EXPECT_TRUE(QmFriendTests::UpdateOnline(Tracker, {Unmatched}, {"server"}).empty());
}
