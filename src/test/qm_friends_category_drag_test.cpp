#include "test.h"

#include <game/client/components/qmclient/friends_category_drag.h>

#include <gtest/gtest.h>

TEST(QmFriendsCategoryDrag, ClickAndSmallMovementDoNotReorder)
{
	SFriendsCategoryDragState State;
	const CUIRect Header{10.0f, 20.0f, 100.0f, 20.0f};
	State.Begin(2, Header, vec2(20.0f, 25.0f));
	EXPECT_EQ(State.m_PressedIndex, 2);
	EXPECT_FALSE(State.Update(vec2(23.0f, 26.0f), true));
	EXPECT_FALSE(State.Update(vec2(30.0f, 25.0f), false));
	EXPECT_EQ(State.m_DraggingIndex, -1);
}

TEST(QmFriendsCategoryDrag, DragStartsOutsideHeaderAfterThreshold)
{
	SFriendsCategoryDragState State;
	const CUIRect Header{10.0f, 20.0f, 100.0f, 20.0f};
	State.Begin(2, Header, vec2(20.0f, 25.0f));
	EXPECT_TRUE(State.Update(vec2(120.0f, 25.0f), true));
	EXPECT_EQ(State.m_DraggingIndex, 2);
	EXPECT_EQ(State.m_GrabOffset.x, 10.0f);
	EXPECT_FALSE(State.Update(vec2(140.0f, 25.0f), true));
}

TEST(QmFriendsPlayerDrag, OnlyNamedPlayerFriendsCanBeDragged)
{
	SFriendsPlayerDragState State;
	const CUIRect Row{10.0f, 20.0f, 100.0f, 30.0f};
	int Item = 0;
	State.Begin(&Item, IFriends::FRIEND_CLAN, "Clan", "", "Default", Row, vec2(20.0f, 25.0f));
	EXPECT_EQ(State.m_pPressedItem, nullptr);
	State.Begin(&Item, IFriends::FRIEND_PLAYER, "", "", "Default", Row, vec2(20.0f, 25.0f));
	EXPECT_EQ(State.m_pPressedItem, nullptr);
	State.Begin(&Item, IFriends::FRIEND_PLAYER, "Friend", "Clan", "Default", Row, vec2(20.0f, 25.0f));
	EXPECT_EQ(State.m_pPressedItem, &Item);
	EXPECT_FALSE(State.Update(vec2(22.0f, 25.0f), true));
	EXPECT_TRUE(State.Update(vec2(30.0f, 25.0f), true));
	EXPECT_FALSE(State.Update(vec2(40.0f, 25.0f), true));
}

TEST(QmFriendsPlayerDrag, OnlyOtherWritableCategoriesAcceptDrop)
{
	SFriendsPlayerDragState State;
	const CUIRect Row{10.0f, 20.0f, 100.0f, 30.0f};
	int Item = 0;
	State.Begin(&Item, IFriends::FRIEND_PLAYER, "Friend", "Clan", "Default", Row, vec2(20.0f, 25.0f));
	EXPECT_FALSE(State.CanDropTo("Another"));
	EXPECT_TRUE(State.Update(vec2(30.0f, 25.0f), true));
	EXPECT_FALSE(State.CanDropTo(nullptr));
	EXPECT_FALSE(State.CanDropTo(""));
	EXPECT_FALSE(State.CanDropTo("default"));
	EXPECT_FALSE(State.CanDropTo(IFriends::CLAN_MEMBERS_CATEGORY));
	EXPECT_FALSE(State.CanDropTo(IFriends::OFFLINE_CATEGORY));
	EXPECT_TRUE(State.CanDropTo("Another"));
}
