#include <game/client/QmUi/QmVoteMenu.h>

#include <gtest/gtest.h>

TEST(QmVotePlayerTargets, KickAllowsOtherPlayersAndSpectatorsButRejectsSelfAndMissingPlayers)
{
	EXPECT_TRUE(QmVotePlayerEligible(EQmVoteMenuPage::KICK, 2, 1, true, false));
	EXPECT_TRUE(QmVotePlayerEligible(EQmVoteMenuPage::KICK, 2, 1, true, true));
	EXPECT_FALSE(QmVotePlayerEligible(EQmVoteMenuPage::KICK, 1, 1, true, false));
	EXPECT_FALSE(QmVotePlayerEligible(EQmVoteMenuPage::KICK, 2, 1, false, false));
}

TEST(QmVotePlayerTargets, SpectateExcludesPlayersAlreadySpectating)
{
	EXPECT_TRUE(QmVotePlayerEligible(EQmVoteMenuPage::SPECTATE, 2, 1, true, false));
	EXPECT_FALSE(QmVotePlayerEligible(EQmVoteMenuPage::SPECTATE, 2, 1, true, true));
	EXPECT_FALSE(QmVotePlayerEligible(EQmVoteMenuPage::SPECTATE, 1, 1, true, false));
}

TEST(QmVotePlayerTargets, OptionsPageAndInvalidIdsNeverAcceptPlayerActions)
{
	EXPECT_FALSE(QmVotePlayerEligible(EQmVoteMenuPage::OPTIONS, 2, 1, true, false));
	EXPECT_FALSE(QmVotePlayerEligible(EQmVoteMenuPage::KICK, -1, 1, true, false));
	EXPECT_FALSE(QmVotePlayerEligible(EQmVoteMenuPage::KICK, MAX_CLIENTS, 1, true, false));
}
