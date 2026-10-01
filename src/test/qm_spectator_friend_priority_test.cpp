#include "test.h"

#include <game/client/components/qmclient/spectator_friend_priority.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

TEST(QmSpectatorFriendPriority, KeepsRelativeOrderInsideEachGroup)
{
	// 好友在下标 1 与 3；分区后好友整体前置，但组内相对顺序不变。
	const bool aIsFriend[] = {false, true, false, true, false};
	std::vector<int> vOrder(5, -1);

	const int FriendCount = qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, 5, vOrder.data());

	EXPECT_EQ(FriendCount, 2);
	const std::vector<int> vExpected = {1, 3, 0, 2, 4};
	EXPECT_EQ(vOrder, vExpected);
}

TEST(QmSpectatorFriendPriority, WithoutFriendsOrderIsUnchanged)
{
	const bool aIsFriend[] = {false, false, false};
	std::vector<int> vOrder(3, -1);

	const int FriendCount = qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, 3, vOrder.data());

	EXPECT_EQ(FriendCount, 0);
	const std::vector<int> vExpected = {0, 1, 2};
	EXPECT_EQ(vOrder, vExpected);
}

TEST(QmSpectatorFriendPriority, AllFriendsFillsOrderWithIdentity)
{
	const bool aIsFriend[] = {true, true, true, true};
	std::vector<int> vOrder(4, -1);

	const int FriendCount = qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, 4, vOrder.data());

	EXPECT_EQ(FriendCount, 4);
	const std::vector<int> vExpected = {0, 1, 2, 3};
	EXPECT_EQ(vOrder, vExpected);
}

TEST(QmSpectatorFriendPriority, EmptyInputWritesNothing)
{
	// Count <= 0 时不得读写 pOrder，返回值即好友分组长度 0。
	bool IsFriend = true;
	int Order = -1;

	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(&IsFriend, 0, &Order), 0);
	EXPECT_EQ(Order, -1);
	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(&IsFriend, -3, &Order), 0);
	EXPECT_EQ(Order, -1);
}

TEST(QmSpectatorFriendPriority, EveryInputIndexAppearsExactlyOnce)
{
	// 分区是重排而非筛选：输出必须是输入下标的一个排列。
	const bool aIsFriend[] = {true, false, true, false, true, false, false};
	std::vector<int> vOrder(7, -1);

	const int FriendCount = qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, 7, vOrder.data());

	EXPECT_EQ(FriendCount, 3);
	std::vector<int> vSorted = vOrder;
	std::sort(vSorted.begin(), vSorted.end());
	const std::vector<int> vExpected = {0, 1, 2, 3, 4, 5, 6};
	EXPECT_EQ(vSorted, vExpected);
	// 前 FriendCount 项必须全为好友。
	for(int i = 0; i < FriendCount; ++i)
		EXPECT_TRUE(aIsFriend[vOrder[i]]);
}

TEST(QmSpectatorFriendPriority, DisablingPriorityRestoresOriginalOrderAndRemovesGroupHeader)
{
	const bool aIsFriend[] = {false, true, false, true};
	std::vector<int> vOrder(4, -1);
	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, 4, vOrder.data(), true), 2);
	EXPECT_EQ(vOrder, (std::vector<int>{1, 3, 0, 2}));

	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, 4, vOrder.data(), false), 0);
	EXPECT_EQ(vOrder, (std::vector<int>{0, 1, 2, 3}));

	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, 4, vOrder.data(), true), 2);
	EXPECT_EQ(vOrder, (std::vector<int>{1, 3, 0, 2}));
}

TEST(QmSpectatorFriendPriority, FullColumnIncludesBothGroupTitles)
{
	EXPECT_FLOAT_EQ(qm_spectator_friends::MaxColumnHeight(8, 1, 8, 60.0f, 15.0f), 510.0f);
	EXPECT_FLOAT_EQ(qm_spectator_friends::MaxColumnHeight(8, 0, 8, 60.0f, 15.0f), 480.0f);
	EXPECT_FLOAT_EQ(qm_spectator_friends::MaxColumnHeight(8, 8, 8, 60.0f, 15.0f), 495.0f);
}

TEST(QmSpectatorFriendPriority, GroupAtColumnBoundaryOnlyAddsHeightToItsOwnColumn)
{
	EXPECT_FLOAT_EQ(qm_spectator_friends::MaxColumnHeight(16, 8, 8, 60.0f, 15.0f), 495.0f);
	EXPECT_FLOAT_EQ(qm_spectator_friends::MaxColumnHeight(16, 1, 8, 60.0f, 15.0f), 510.0f);
	EXPECT_FLOAT_EQ(qm_spectator_friends::MaxColumnHeight(17, 16, 8, 60.0f, 15.0f), 495.0f);
}

TEST(QmSpectatorFriendPriority, DenseColumnsIncludeGroupTitlesWithoutAccumulatingOtherColumns)
{
	EXPECT_FLOAT_EQ(qm_spectator_friends::MaxColumnHeight(64, 1, 16, 30.0f, 15.0f), 510.0f);
	EXPECT_FLOAT_EQ(qm_spectator_friends::MaxColumnHeight(96, 24, 24, 19.0f, 12.0f), 468.0f);
	EXPECT_FLOAT_EQ(qm_spectator_friends::MaxColumnHeight(128, 1, 32, 15.0f, 12.0f), 504.0f);
	EXPECT_FLOAT_EQ(qm_spectator_friends::MaxColumnHeight(0, 0, 8, 60.0f, 15.0f), 0.0f);
}
