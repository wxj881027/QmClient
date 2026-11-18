#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>

#include <gtest/gtest.h>

using qm_card_catalog::FindAudioPackIndexByName;
using qm_card_catalog::SAudioPackEntry;

TEST(QmSoundCardPackSelection, MissingConfiguredPackLeavesTheListUnselected)
{
	std::vector<SAudioPackEntry> vPacks{{"default", 10}, {"custom", 5}};
	EXPECT_EQ(FindAudioPackIndexByName(vPacks, "custom/favorites"), -1);

	vPacks.push_back({"custom/favorites", 3});
	EXPECT_EQ(FindAudioPackIndexByName(vPacks, "custom/favorites"), 2);
}

TEST(QmSoundCardPackSelection, EmptyConfigSelectsTheDefaultPack)
{
	const std::vector<SAudioPackEntry> vPacks{{"default", 10}, {"custom", 5}};
	EXPECT_EQ(FindAudioPackIndexByName(vPacks, ""), 0);
	EXPECT_EQ(FindAudioPackIndexByName(vPacks, "custom"), 1);
}
