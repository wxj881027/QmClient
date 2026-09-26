#include "test.h"

#include <game/client/components/qmclient/friend_enter_tracker.h>
#include <game/client/components/qmclient/friend_online_tracker.h>

#include <gtest/gtest.h>

#include <vector>

TEST(QmFriendEnterTracker, InitialRosterIsSilentAndNewFriendEntersOnce)
{
	qm_friend_notify::CEnterTracker Tracker;
	const qm_friend_notify::CEnterTracker::CClient Existing{1, "Existing", "Clan", true, false};
	const qm_friend_notify::CEnterTracker::CClient NewFriend{2, "NewFriend", "Clan", true, false};
	EXPECT_TRUE(Tracker.Update({Existing}, 0.0, false).empty());
	EXPECT_EQ(Tracker.Update({Existing, NewFriend}, 0.2, false), std::vector<std::string>{"NewFriend"});
	EXPECT_TRUE(Tracker.Update({Existing, NewFriend}, 0.4, false).empty());
}

TEST(QmFriendEnterTracker, SameSlotIdentityAndFriendChangesAreSilent)
{
	qm_friend_notify::CEnterTracker Tracker;
	EXPECT_TRUE(Tracker.Update({{1, "Stranger", "OldClan", false, false}}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "OldClan", true, false}}, 0.2, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "NewClan", true, false}}, 0.4, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "NewClan", false, false}}, 0.6, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "NewClan", true, false}}, 0.8, false).empty());
}

TEST(QmFriendEnterTracker, ShortSnapshotAbsenceDoesNotReannounceFriend)
{
	qm_friend_notify::CEnterTracker Tracker;
	const qm_friend_notify::CEnterTracker::CClient Friend{1, "Friend", "Clan", true, false};
	EXPECT_TRUE(Tracker.Update({Friend}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 1.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 3.8, false).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, 3.9, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 4.0, false).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, 6.9, false).empty());
}

TEST(QmFriendEnterTracker, ConfirmedAbsenceAllowsReentryAtGraceBoundary)
{
	qm_friend_notify::CEnterTracker Tracker;
	const qm_friend_notify::CEnterTracker::CClient Friend{1, "Friend", "Clan", true, false};
	EXPECT_TRUE(Tracker.Update({Friend}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 1.0, false).empty());
	EXPECT_EQ(Tracker.Update({Friend}, 4.0, false), std::vector<std::string>{"Friend"});
	EXPECT_TRUE(Tracker.Update({Friend}, 4.2, false).empty());
}

TEST(QmFriendEnterTracker, IdentityMovingSlotsWithinGraceIsSilent)
{
	qm_friend_notify::CEnterTracker Tracker;
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "Clan", true, false}}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 1.0, false).empty());
	EXPECT_TRUE(Tracker.Update({{2, "Friend", "Clan", true, false}}, 3.9, false).empty());
	EXPECT_EQ(Tracker.Update({{1, "NewFriend", "Clan", true, false}, {2, "Friend", "Clan", true, false}}, 4.0, false),
		std::vector<std::string>{"NewFriend"});
}

TEST(QmFriendEnterTracker, IdentityMovingSlotsAfterConfirmedAbsenceEnters)
{
	qm_friend_notify::CEnterTracker Tracker;
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "Clan", true, false}}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 1.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 4.0, false).empty());
	EXPECT_EQ(Tracker.Update({{2, "Friend", "Clan", true, false}}, 4.2, false), std::vector<std::string>{"Friend"});
}

TEST(QmFriendEnterTracker, SlotMigrationRespectsIgnoreClan)
{
	for(const bool IgnoreClan : {false, true})
	{
		qm_friend_notify::CEnterTracker Tracker;
		EXPECT_TRUE(Tracker.Update({{1, "Friend", "OldClan", true, false}}, 0.0, IgnoreClan).empty());
		EXPECT_TRUE(Tracker.Update({}, 1.0, IgnoreClan).empty());
		const auto vNames = Tracker.Update({{2, "Friend", "NewClan", true, false}}, 1.2, IgnoreClan);
		EXPECT_EQ(vNames, IgnoreClan ? std::vector<std::string>{} : std::vector<std::string>{"Friend"});
	}
}

TEST(QmFriendEnterTracker, LocalPlayersAndNonFriendsDoNotNotify)
{
	qm_friend_notify::CEnterTracker Tracker;
	EXPECT_TRUE(Tracker.Update({}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Main", "Clan", true, true}, {2, "Dummy", "Clan", true, true}, {3, "Stranger", "Clan", false, false}}, 0.2, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Main", "Clan", true, false}, {2, "Dummy", "Clan", true, false}, {3, "Stranger", "Clan", true, false}}, 0.4, false).empty());
}

TEST(QmFriendEnterTracker, ResetBuildsANewSilentBaseline)
{
	qm_friend_notify::CEnterTracker Tracker;
	EXPECT_TRUE(Tracker.Update({}, 0.0, false).empty());
	EXPECT_EQ(Tracker.Update({{1, "Friend", "Clan", true, false}}, 0.2, false), std::vector<std::string>{"Friend"});
	Tracker.Reset();
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "Clan", true, false}, {2, "Another", "Clan", true, false}}, 1.0, false).empty());
}

TEST(QmFriendOnlineScan, InitialAndNewlyCompletedServersAreSilentUntilTheirNextSnapshot)
{
	qm_friend_notify::COnlineTracker Tracker;
	const qm_friend_notify::CFriend Existing{"Alice\tClan", "Alice", "First", "server-1"};
	const qm_friend_notify::CFriend Delayed{"Bob\tClan", "Bob", "Second", "server-2"};
	const qm_friend_notify::CFriend NewFriend{"Carol\tClan", "Carol", "Second", "server-2"};
	EXPECT_TRUE(Tracker.Update({Existing}, {"server-1"}).empty());
	EXPECT_TRUE(Tracker.Update({Existing, Delayed}, {"server-1", "server-2"}).empty());
	const auto vNotifications = Tracker.Update({Existing, Delayed, NewFriend}, {"server-1", "server-2"});
	ASSERT_EQ(vNotifications.size(), 1u);
	EXPECT_EQ(vNotifications[0].m_Name, "Carol");
	EXPECT_TRUE(Tracker.Update({Existing, Delayed, NewFriend}, {"server-1", "server-2"}).empty());
	Tracker.Reset();
	EXPECT_TRUE(Tracker.Update({Existing, Delayed, NewFriend}, {"server-1", "server-2"}).empty());
}
