#include <game/client/components/qmclient/browser_friend_actions.h>

#include <gtest/gtest.h>

namespace
{
	constexpr CUIRect Content{10.0f, 20.0f, 160.0f, 30.0f};
}

TEST(QmBrowserFriendActions, LeavingUnpinnedPlayerImmediatelyRemovesAllActionTargets)
{
	const auto Hover = QmBrowserFriendActions(Content, true, true, true, false);
	EXPECT_TRUE(Hover.m_Pin);
	EXPECT_TRUE(Hover.m_Follow);
	EXPECT_TRUE(Hover.m_Copy);
	EXPECT_TRUE(Hover.m_Remove);

	const auto Left = QmBrowserFriendActions(Content, false, true, true, false);
	EXPECT_FALSE(Left.m_Pin);
	EXPECT_FALSE(Left.m_Follow);
	EXPECT_FALSE(Left.m_Copy);
	EXPECT_FALSE(Left.m_Remove);
}

TEST(QmBrowserFriendActions, PinnedPlayerKeepsOnlyThePinTargetAfterLeaving)
{
	const auto Hover = QmBrowserFriendActions(Content, true, true, true, true);
	const auto Left = QmBrowserFriendActions(Content, false, true, true, true);
	ASSERT_TRUE(Hover.m_Pin);
	ASSERT_TRUE(Left.m_Pin);
	EXPECT_FLOAT_EQ(Left.m_Pin->x, Hover.m_Pin->x);
	EXPECT_FALSE(Left.m_Follow);
	EXPECT_FALSE(Left.m_Copy);
	EXPECT_FALSE(Left.m_Remove);
}

TEST(QmBrowserFriendActions, NamelessClanOnlyOffersCopyAndRemoveOnHover)
{
	const auto Hover = QmBrowserFriendActions(Content, true, false, false, false);
	EXPECT_FALSE(Hover.m_Pin);
	EXPECT_FALSE(Hover.m_Follow);
	ASSERT_TRUE(Hover.m_Copy);
	EXPECT_TRUE(Hover.m_Remove);
	EXPECT_LT(Hover.m_LabelRight, Hover.m_Copy->x);

	const auto Left = QmBrowserFriendActions(Content, false, false, false, false);
	EXPECT_FALSE(Left.m_Pin);
	EXPECT_FALSE(Left.m_Follow);
	EXPECT_FALSE(Left.m_Copy);
	EXPECT_FALSE(Left.m_Remove);
}

TEST(QmBrowserFriendActions, NamedClanMemberCanBeFollowedWithoutAPinTarget)
{
	const auto Actions = QmBrowserFriendActions(Content, true, false, true, false);
	EXPECT_FALSE(Actions.m_Pin);
	ASSERT_TRUE(Actions.m_Follow);
	EXPECT_LT(Actions.m_LabelRight, Actions.m_Follow->x);
}

TEST(QmBrowserFriendActions, OnlyVisibleActionHitAreasReserveTheLeftPress)
{
	const auto Player = QmBrowserFriendActions(Content, true, true, true, false);
	for(const auto &Icon : {Player.m_Pin, Player.m_Follow, Player.m_Copy, Player.m_Remove})
	{
		ASSERT_TRUE(Icon);
		// 图标上方仍在加高的命中区内；右缘之外是两个动作之间的空隙。
		const vec2 AboveIcon{Icon->Center().x, Icon->y - 1.0f};
		EXPECT_EQ(Player.RowButtonFlags(AboveIcon) & BUTTONFLAG_LEFT, 0u);
		EXPECT_NE(Player.RowButtonFlags(AboveIcon) & BUTTONFLAG_RIGHT, 0u);
		EXPECT_NE(Player.RowButtonFlags({Icon->x + Icon->w, Icon->y}) & BUTTONFLAG_LEFT, 0u);
	}
	const auto Clan = QmBrowserFriendActions(Content, true, false, false, false);
	EXPECT_NE(Clan.RowButtonFlags(Player.m_Follow->Center()) & BUTTONFLAG_LEFT, 0u);
}

TEST(QmBrowserFriendActions, LabelsReserveActionSpaceAcrossHoverAndFollowTagResizing)
{
	constexpr float LabelX = 40.0f;
	const auto Hover = QmBrowserFriendActions(Content, true, true, true, false);
	const auto Left = QmBrowserFriendActions(Content, false, true, true, false);
	const auto Pinned = QmBrowserFriendActions(Content, false, true, true, true);
	ASSERT_TRUE(Hover.m_Pin);
	EXPECT_LT(LabelX + Hover.LabelMaxWidth(LabelX), Hover.m_Pin->x);
	EXPECT_FLOAT_EQ(Left.LabelMaxWidth(LabelX), Hover.LabelMaxWidth(LabelX));
	EXPECT_FLOAT_EQ(Pinned.LabelMaxWidth(LabelX), Hover.LabelMaxWidth(LabelX));

	CUIRect WithTag = Content;
	WithTag.w -= 32.0f;
	const auto Tagged = QmBrowserFriendActions(WithTag, true, true, true, true);
	ASSERT_TRUE(Tagged.m_Remove);
	EXPECT_LE(Tagged.m_Remove->x + Tagged.m_Remove->w, WithTag.x + WithTag.w);
	EXPECT_FLOAT_EQ(Tagged.LabelMaxWidth(LabelX), Hover.LabelMaxWidth(LabelX) - 32.0f);
	EXPECT_FLOAT_EQ(Tagged.LabelMaxWidth(Tagged.m_LabelRight + 1.0f), 0.0f);
}
