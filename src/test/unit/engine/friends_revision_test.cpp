#include <engine/client/friends.h>

#include <gtest/gtest.h>

TEST(FriendsRevision, CategoryAndAssignmentChangesInvalidateConsumers)
{
	CFriends Friends;
	const uint64_t Initial = Friends.Revision();
	ASSERT_TRUE(Friends.AddCategory("Teammates"));
	EXPECT_GT(Friends.Revision(), Initial);

	const uint64_t Added = Friends.Revision();
	ASSERT_FALSE(Friends.AddCategory("Teammates"));
	EXPECT_EQ(Friends.Revision(), Added);

	Friends.AddFriend("Alice", "", "Teammates");
	const uint64_t FriendAdded = Friends.Revision();
	ASSERT_TRUE(Friends.SetFriendCategory("Alice", "", IFriends::DEFAULT_CATEGORY));
	EXPECT_GT(Friends.Revision(), FriendAdded);
	const uint64_t Moved = Friends.Revision();
	EXPECT_FALSE(Friends.SetFriendCategory("Alice", "", IFriends::DEFAULT_CATEGORY));
	EXPECT_EQ(Friends.Revision(), Moved);

	Friends.AddFriend("Alice", "", "Teammates");
	EXPECT_GT(Friends.Revision(), Moved);
	const uint64_t Restored = Friends.Revision();
	Friends.AddFriend("Alice", "", "Teammates");
	EXPECT_EQ(Friends.Revision(), Restored);

	ASSERT_TRUE(Friends.RenameCategory("Teammates", "Partners"));
	EXPECT_GT(Friends.Revision(), Restored);
	const uint64_t Renamed = Friends.Revision();
	ASSERT_TRUE(Friends.RemoveCategory("Partners"));
	EXPECT_GT(Friends.Revision(), Renamed);
}
