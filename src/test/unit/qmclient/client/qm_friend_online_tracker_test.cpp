#include <game/client/components/qmclient/friend_online_tracker.h>

#include <gtest/gtest.h>
TEST(QmFriendOnlineTracker, FirstSnapshotAndStableFriendsStaySilent)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	EXPECT_TRUE(Tracker.Update({Friend}, {"server"}).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, {"server"}).empty());
}
TEST(QmFriendOnlineTracker, NewFriendNotifiesOnceAfterBaseline)
{
	qm_friend_notify::COnlineTracker Tracker;
	EXPECT_TRUE(Tracker.Update({}, {"server"}).empty());
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	const auto vNotifications = Tracker.Update({Friend, Friend}, {"server"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Name, "Alice");
	EXPECT_EQ(vNotifications[0].m_Map, "Map");
	EXPECT_TRUE(Tracker.Update({Friend}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, OneMissingSnapshotDoesNotReannounceFriend)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	Tracker.Update({Friend}, {"server"});
	EXPECT_TRUE(Tracker.Update({}, {"server"}).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, {"server"}).empty());
	EXPECT_TRUE(Tracker.Update({}, {"server"}).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, TwoMissingSnapshotsAllowReturnNotification)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	Tracker.Update({Friend}, {"server"});
	EXPECT_TRUE(Tracker.Update({}, {"server"}).empty());
	EXPECT_TRUE(Tracker.Update({}, {"server"}).empty());
	const auto vNotifications = Tracker.Update({Friend}, {"server"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Key, Friend.m_Key);
}

TEST(QmFriendOnlineTracker, MissingOrIncompleteServerDoesNotConfirmOffline)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	Tracker.Update({Friend}, {"server", "other"});
	EXPECT_TRUE(Tracker.Update({}, {"other"}).empty());
	EXPECT_TRUE(Tracker.Update({}, {"other"}).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, {"server", "other"}).empty());
}

TEST(QmFriendOnlineTracker, UnavailableServerBreaksConsecutiveMissingEvidence)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	Tracker.Update({Friend}, {"server"});
	Tracker.Update({}, {"server"});
	Tracker.Update({}, {"other"});
	Tracker.Update({}, {"server"});
	EXPECT_TRUE(Tracker.Update({Friend}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, EmptyListsNeitherCreateBaselineNorExpireFriends)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	EXPECT_TRUE(Tracker.Update({}, {}).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, {"server"}).empty());
	EXPECT_TRUE(Tracker.Update({}, {}).empty());
	EXPECT_TRUE(Tracker.Update({}, {}).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, ClanChangeOnSameServerStaysSilent)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Original{"Alice\tOld", "Alice", "Map", "server"};
	const qm_friend_notify::CFriend Changed{"Alice\tNew", "Alice", "Map", "server"};
	Tracker.Update({Original}, {"server"});
	EXPECT_TRUE(Tracker.Update({Changed}, {"server"}).empty());
	Tracker.Update({}, {"server"});
	EXPECT_TRUE(Tracker.Update({Changed}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, MapAndServerChangesKeepFriendOnline)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Original{"Alice\tClan", "Alice", "OldMap", "old"};
	const qm_friend_notify::CFriend Moved{"Alice\tClan", "Alice", "NewMap", "new"};
	Tracker.Update({Original}, {"old", "new"});
	EXPECT_TRUE(Tracker.Update({Moved}, {"old", "new"}).empty());
	Tracker.Update({}, {"old"});
	Tracker.Update({}, {"old"});
	EXPECT_TRUE(Tracker.Update({Moved}, {"new"}).empty());
}

TEST(QmFriendOnlineTracker, ResetSilentlyRebuildsBaseline)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Friend{"Alice\tClan", "Alice", "Map", "server"};
	Tracker.Update({}, {"server"});
	Tracker.Reset();
	EXPECT_TRUE(Tracker.Update({Friend}, {"server"}).empty());
	const qm_friend_notify::CFriend NewFriend{"Bob\tClan", "Bob", "Map", "server"};
	const auto vNotifications = Tracker.Update({Friend, NewFriend}, {"server"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Name, "Bob");
}

TEST(QmFriendOnlineTracker, FirstCompleteObservationOfMissingServerStaysSilent)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Existing{"Alice\tClan", "Alice", "Map", "first"};
	const qm_friend_notify::CFriend InitiallyMissing{"Bob\tClan", "Bob", "Map", "second"};
	Tracker.Update({Existing}, {"first"});
	EXPECT_TRUE(Tracker.Update({Existing, InitiallyMissing}, {"first", "second"}).empty());
	const qm_friend_notify::CFriend NewFriend{"Carol\tClan", "Carol", "Map", "second"};
	const auto vNotifications = Tracker.Update({Existing, InitiallyMissing, NewFriend}, {"first", "second"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Name, "Carol");
}

TEST(QmFriendOnlineTracker, IncompleteServerDoesNotCreateNotificationBaseline)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend PartiallyVisible{"Alice\tClan", "Alice", "Map", "partial"};
	const qm_friend_notify::CFriend InitiallyMissing{"Bob\tClan", "Bob", "Map", "partial"};
	EXPECT_TRUE(Tracker.Update({PartiallyVisible}, {"other"}).empty());
	EXPECT_TRUE(Tracker.Update({PartiallyVisible, InitiallyMissing}, {"other", "partial"}).empty());
	const qm_friend_notify::CFriend NewFriend{"Carol\tClan", "Carol", "Map", "partial"};
	const auto vNotifications = Tracker.Update({PartiallyVisible, InitiallyMissing, NewFriend}, {"other", "partial"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Name, "Carol");
}

TEST(QmFriendOnlineTracker, ExistingSameNameBecomingFriendByClanStaysSilent)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Unmatched{"Alice\tOther", "Alice", "Map", "server", false};
	const qm_friend_notify::CFriend Matched{"Alice\tClan", "Alice", "Map", "server"};
	Tracker.Update({Unmatched}, {"server"});
	EXPECT_TRUE(Tracker.Update({Matched}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, TemporaryUnmatchedClanKeepsPresenceUntilClanReturns)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Matched{"Alice\tClan", "Alice", "Map", "server"};
	const qm_friend_notify::CFriend Unmatched{"Alice\tOther", "Alice", "Map", "server", false};
	Tracker.Update({Matched}, {"server"});
	EXPECT_TRUE(Tracker.Update({Unmatched}, {"server"}).empty());
	EXPECT_TRUE(Tracker.Update({Unmatched}, {"server"}).empty());
	EXPECT_TRUE(Tracker.Update({Unmatched}, {"server"}).empty());
	EXPECT_TRUE(Tracker.Update({Matched}, {"server"}).empty());
}

TEST(QmFriendOnlineTracker, NewSameNameWithUnmatchedClanDoesNotNotify)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Unmatched{"Alice\tOther", "Alice", "Map", "server", false};
	Tracker.Update({}, {"server"});
	EXPECT_TRUE(Tracker.Update({Unmatched}, {"server"}).empty());
}
