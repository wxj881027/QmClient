#include <game/client/QmUi/QmPlayersMenu.h>

#include <gtest/gtest.h>

TEST(QmPlayerActionPress, ReplacementNeedsItsOwnPressOnTheSameSlotButton)
{
	QmPlayersUi::CActionPress Press;
	const int SlotButton = 0;

	EXPECT_FALSE(Press.ProcessButton(&SlotButton, "Alice", "Clan", true, false));
	Press.CancelIfInactive(&SlotButton);
	EXPECT_FALSE(Press.ProcessButton(&SlotButton, "Bob", "Clan", false, true));

	EXPECT_FALSE(Press.ProcessButton(&SlotButton, "Bob", "Clan", true, false));
	Press.CancelIfInactive(&SlotButton);
	EXPECT_TRUE(Press.ProcessButton(&SlotButton, "Bob", "Clan", false, true));
}

TEST(QmPlayerActionPress, CancelledPressCannotCommitWhenTheSameButtonReturns)
{
	QmPlayersUi::CActionPress Press;
	const int SlotButton = 0;

	EXPECT_FALSE(Press.ProcessButton(&SlotButton, "Alice", "Clan", true, false));
	Press.CancelIfInactive(nullptr);
	EXPECT_FALSE(Press.ProcessButton(&SlotButton, "Alice", "Clan", false, true));
}
