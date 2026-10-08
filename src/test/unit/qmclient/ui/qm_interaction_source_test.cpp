#include <game/client/QmUi/QmPopupSource.h>

#include <gtest/gtest.h>

TEST(QmInteractionSource, AStillVisibleCardKeepsItsInteractionAlive)
{
	int Deck = 0;
	CQmUiInteractionSource Source;
	Source.Assign(&Deck, "qm:translate", 10);
	const char aEquivalentId[] = "qm:translate";
	Source.Refresh(&Deck, aEquivalentId, 11);
	EXPECT_FALSE(Source.Expired(11, false));
	EXPECT_TRUE(Source.Expired(12, false));
}

TEST(QmInteractionSource, FilteringOutACardExpiresItsInteractionDespiteOtherCards)
{
	int Deck = 0;
	CQmUiInteractionSource Source;
	Source.Assign(&Deck, "qm:translate", 10);
	Source.Refresh(&Deck, "qm:ime", 11);
	EXPECT_TRUE(Source.Expired(11, false));
}

TEST(QmInteractionSource, OpeningTheSameCardInAnotherPageDoesNotKeepTheOldPopup)
{
	int OriginalPage = 0;
	int SearchPage = 0;
	CQmUiInteractionSource Source;
	Source.Assign(&OriginalPage, "qm:translate", 10);
	Source.Refresh(&SearchPage, "qm:translate", 11);
	EXPECT_TRUE(Source.Expired(11, false));
	EXPECT_FALSE(Source.Matches(&SearchPage, "qm:translate"));
}

TEST(QmInteractionSource, UpdateBeforeRenderingAllowsOnlyThePreviousUiFrame)
{
	int Deck = 0;
	CQmUiInteractionSource Source;
	Source.Assign(&Deck, "qm:translate", 10);
	EXPECT_FALSE(Source.Expired(11, true));
	EXPECT_TRUE(Source.Expired(12, true));
}

TEST(QmInteractionSource, ReopeningCanAssignANewSourceWithoutKeepingTheOldOne)
{
	int Deck = 0;
	CQmUiInteractionSource Source;
	Source.Assign(&Deck, "qm:translate", 10);
	Source.Assign(&Deck, "qm:ime", 11);
	Source.Refresh(&Deck, "qm:translate", 12);
	EXPECT_TRUE(Source.Matches(&Deck, "qm:ime"));
	EXPECT_TRUE(Source.Expired(12, false));
}

TEST(QmInteractionSource, InputsOutsideCardsDoNotRequireCardRefresh)
{
	CQmUiInteractionSource Source;
	Source.Assign(nullptr, nullptr, 10);
	EXPECT_FALSE(Source.Expired(12, false));
	EXPECT_FALSE(Source.Matches(nullptr, "qm:translate"));
}
