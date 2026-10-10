#include <game/client/QmUi/QmPlayersMenu.h>

#include <gtest/gtest.h>

using namespace QmPlayersUi;

TEST(PlayersMenu, KeepsSelectionWhileSamePlayerRemainsInSlot)
{
	CSelection Selection;
	const SIdentity Player{7, "Player", "Clan"};
	Selection.Choose(Player);
	Selection.Validate(&Player);
	EXPECT_EQ(Selection.Id(), 7);
}

TEST(PlayersMenu, ClearsSelectionWhenSlotIsReusedOrPlayerLeaves)
{
	CSelection Selection;
	Selection.Choose({7, "Player", "Clan"});
	const SIdentity Replacement{7, "Other", "Clan"};
	Selection.Validate(&Replacement);
	EXPECT_EQ(Selection.Id(), -1);
	Selection.Choose({7, "Player", "Clan"});
	Selection.Validate(nullptr);
	EXPECT_EQ(Selection.Id(), -1);
}

TEST(PlayersMenu, ChangedClanRequiresExplicitReselection)
{
	CSelection Selection;
	Selection.Choose({7, "Player", "Old"});
	const SIdentity Current{7, "Player", "New"};
	Selection.Validate(&Current);
	EXPECT_EQ(Selection.Id(), -1);
	Selection.Choose(Current);
	EXPECT_EQ(Selection.Id(), 7);
}

TEST(PlayersMenu, PanelsRemainDisjointInsideWideAndNarrowViews)
{
	for(const CUIRect View : {CUIRect{10, 20, 800, 400}, CUIRect{10, 20, 360, 300}})
	{
		const auto Layout = Panels(View);
		for(const auto Rect : {Layout.m_List, Layout.m_Details})
		{
			EXPECT_GE(Rect.x, View.x);
			EXPECT_GE(Rect.y, View.y);
			EXPECT_GT(Rect.w, 0);
			EXPECT_GT(Rect.h, 0);
			EXPECT_LE(Rect.x + Rect.w, View.x + View.w);
			EXPECT_LE(Rect.y + Rect.h, View.y + View.h);
		}
		EXPECT_TRUE(Layout.m_List.x + Layout.m_List.w <= Layout.m_Details.x || Layout.m_List.y + Layout.m_List.h <= Layout.m_Details.y);
	}
}
