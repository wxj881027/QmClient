#include <game/client/components/menus.h>

#include <gtest/gtest.h>

TEST(QmNewUiMenuGameplaySocialContract, FriendCategoryHeaderActionExcludesManageButton)
{
	const CUIRect Header{10.0f, 20.0f, 180.0f, 24.0f};
	CUIRect HeaderAction;
	CUIRect ManageButton;

	CMenus::SplitFriendsCategoryHeaderRects(Header, &HeaderAction, &ManageButton);

	EXPECT_FLOAT_EQ(HeaderAction.x, Header.x);
	EXPECT_FLOAT_EQ(HeaderAction.y, Header.y);
	EXPECT_FLOAT_EQ(HeaderAction.w, Header.w - Header.h);
	EXPECT_FLOAT_EQ(HeaderAction.h, Header.h);
	EXPECT_FLOAT_EQ(ManageButton.x, Header.x + Header.w - Header.h + 2.0f);
	EXPECT_FLOAT_EQ(ManageButton.y, Header.y + 2.0f);
	EXPECT_FLOAT_EQ(ManageButton.w, Header.h - 4.0f);
	EXPECT_FLOAT_EQ(ManageButton.h, Header.h - 4.0f);
	EXPECT_LE(HeaderAction.x + HeaderAction.w, ManageButton.x);
}

TEST(QmNewUiMenuGameplaySocialContract, SecondaryPanelRectClampsNearScreenEdges)
{
	const CUIRect Screen{0.0f, 0.0f, 800.0f, 600.0f};
	const CUIRect Panel = CMenus::SecondaryPanelRect(780.0f, 590.0f, 300.0f, 140.0f, Screen);

	EXPECT_FLOAT_EQ(Panel.w, 300.0f);
	EXPECT_FLOAT_EQ(Panel.h, 140.0f);
	EXPECT_LE(Panel.x + Panel.w, 792.0f);
	EXPECT_LE(Panel.y + Panel.h, 592.0f);
	EXPECT_GE(Panel.x, 8.0f);
	EXPECT_GE(Panel.y, 8.0f);
}

TEST(QmNewUiMenuGameplaySocialContract, FriendAutoFollowDelaysAndStopsAfterTwoJumps)
{
	CMenus::SFriendAutoFollowState State;
	char aConnect[NETADDR_MAXSTRSIZE] = "";

	CMenus::StartFriendAutoFollow(State, "Alice", "Clan", "127.0.0.1:8303");
	EXPECT_TRUE(State.m_Active);
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8303", 10.0f, 3, 2, aConnect, sizeof(aConnect)));

	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 11.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(State.m_HasPendingAddress);
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 13.9f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 14.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_STREQ(aConnect, "127.0.0.1:8304");
	EXPECT_TRUE(State.m_Active);
	EXPECT_EQ(State.m_JumpCount, 1);

	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8305", 20.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8305", 23.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_STREQ(aConnect, "127.0.0.1:8305");
	EXPECT_FALSE(State.m_Active);
	EXPECT_EQ(State.m_JumpCount, 2);
}

TEST(QmNewUiMenuGameplaySocialContract, FriendAutoFollowCancelsWhenTargetGoesOffline)
{
	CMenus::SFriendAutoFollowState State;
	char aConnect[NETADDR_MAXSTRSIZE] = "";

	CMenus::StartFriendAutoFollow(State, "Alice", "Clan", "127.0.0.1:8303");
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 11.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(State.m_HasPendingAddress);
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, false, "", 12.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_FALSE(State.m_Active);
	EXPECT_FALSE(State.m_HasPendingAddress);
}

TEST(QmNewUiMenuGameplaySocialContract, ShortServerNamesCoverKnownFamilies)
{
	CServerInfo Info{};
	char aBuf[sizeof(Info.m_aName)];

	str_copy(Info.m_aName, "KoG | China #12 - HappyHook [kog.tw]");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "China - HappyHook");

	str_copy(Info.m_aName, "Axiom 北京 普通 - CHN1O 钩累死");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "普通 - CHN1O 北京");

	str_copy(Info.m_aName, "DDNet CHN7 西安 - Moderate 中阶");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "中阶图 - CHN7 西安");

	str_copy(Info.m_aName, "DDNet CHN2 上海 - Brutal 高阶");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "高阶图 - CHN2 上海");

	// Axiom 尾部用地区而不是玩法模式(钩累死/AXRace)；名字里没有地区时只留区段标记。
	str_copy(Info.m_aName, "Axiom Novice - CHN12 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "简单 - CHN12");

	str_copy(Info.m_aName, "Axiom Insane - CHN7 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "疯狂 - CHN7");

	str_copy(Info.m_aName, "Axiom ⌬ 上海 ✦ 单人 - CHN1 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "单人 - CHN1 上海");

	str_copy(Info.m_aName, "Axiom Axiom ◇ 广州 ✦ 困难 - CHN9 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "困难 - CHN9 广州");

	str_copy(Info.m_aName, "Axiom Axiom ◇ 北京 ✦ 困难 - CHN10 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "困难 - CHN10 北京");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ DDmaX - CHN7 AXRace");
	str_copy(Info.m_aGameType, "AXRace");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典 - CHN7 广州");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ DDmaX.Pro 古典 - CHN7 AXRace");
	str_copy(Info.m_aGameType, "AXRace");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典 Pro - CHN7 广州");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ 困难 - CHN7 AXRace");
	str_copy(Info.m_aGameType, "AXRace");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "困难 - CHN7 广州");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ 活动 - CHN9 AXRace");
	str_copy(Info.m_aGameType, "AXRace");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "活动 - CHN9 广州");

	// 官方简中 Event 译作「活动」，英文写法也要认出来。
	str_copy(Info.m_aName, "DDNet CHN2 上海 - Event 活动");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "活动图 - CHN2 上海");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ 极限 - CHN9 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "极限 - CHN9 广州");

	str_copy(Info.m_aName, "Axiom ◇ 上海 ✦ 训练 - CHN2 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "训练 - CHN2 上海");

	str_copy(Info.m_aName, "Axiom ◇ 成都 ✦ 娱乐 - CHN12 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "娱乐 - CHN12 成都");

	str_copy(Info.m_aName, "DDNet Moderate - CHN7 西安");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "中阶图 - CHN7 西安");

	str_copy(Info.m_aName, "DDNet CHN2 上海 - DDmaX.Easy 古典");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典图 Easy - CHN2 上海");

	str_copy(Info.m_aName, "DDNet CHN7 西安 - DDmaX.Next");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典图 Next - CHN7 西安");

	str_copy(Info.m_aName, "DDNet CHN3 宁波 - DDmaX.Pro 古典");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典图 Pro - CHN3 宁波");

	str_copy(Info.m_aName, "DDNet CHN4 成都 - DDmaX.Nut 古典");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典图 Nut - CHN4 成都");

	// 只带中文写作的古典服同样不应落到「传统图」。
	str_copy(Info.m_aName, "DDNet CHN2 上海 - 古典 next");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典图 Next - CHN2 上海");

	str_copy(Info.m_aName, "DDNet CHN6 上海 - Oldschool 传统");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "传统图 - CHN6 上海");

	str_copy(Info.m_aName, "DDNet CHN6 上海 - Solo 单人");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "单人图 - CHN6 上海");

	str_copy(Info.m_aName, "DDNet CHN5 上海 - Dummy 分身");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "分身图 - CHN5 上海");

	str_copy(Info.m_aName, "DDNet Taiwan - Moderate");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "中阶图 - Taiwan");

	str_copy(Info.m_aName, "DDNet Taiwan - Brutal");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "高阶图 - Taiwan");

	str_copy(Info.m_aName, "Brutal - CHN5 上海");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "高阶图 - CHN5 上海");

	str_copy(Info.m_aName, "Plain Server Name");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "Plain Server Name");
}
