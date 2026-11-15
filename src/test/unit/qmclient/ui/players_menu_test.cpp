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
