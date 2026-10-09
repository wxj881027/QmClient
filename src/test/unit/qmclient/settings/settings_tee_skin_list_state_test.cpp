#include <game/client/QmUi/cards/QmCardCatalogTeeSkinListState.h>

#include <gtest/gtest.h>

#include <string>

TEST(SettingsTeeSkinListState, RebuiltRowsKeepTheirInteractionAndAnimationIds)
{
	CSettingsTeeSkinListState State;
	const auto &First = State.Resolve("bluekitty", std::nullopt);
	const void *pList = &First.m_ListItem;
	const void *pFavorite = &First.m_Favorite;
	const void *pQueue = &First.m_Queue;
	for(int Index = 0; Index < 100; ++Index)
		State.Resolve(("skin" + std::to_string(Index)).c_str(), std::nullopt);
	const auto &Rebuilt = State.Resolve(std::string("bluekitty").c_str(), std::nullopt);
	EXPECT_EQ(&Rebuilt.m_ListItem, pList);
	EXPECT_EQ(&Rebuilt.m_Favorite, pFavorite);
	EXPECT_EQ(&Rebuilt.m_Queue, pQueue);
	EXPECT_NE(pList, pFavorite);
	EXPECT_NE(pList, pQueue);
}

TEST(SettingsTeeSkinListState, DifferentSkinsAndColorVariantsHaveSeparateIds)
{
	CSettingsTeeSkinListState State;
	const auto &Plain = State.Resolve("bluekitty", std::nullopt);
	const auto &Other = State.Resolve("default", std::nullopt);
	const auto &Colored = State.Resolve("bluekitty", SSettingsSkinListColorKey{true, 123, 456});
	const auto &Variant = State.Resolve("bluekitty", SSettingsSkinListColorKey{true, 789, 456});
	EXPECT_NE(&Plain.m_ListItem, &Other.m_ListItem);
	EXPECT_NE(&Plain.m_ListItem, &Colored.m_ListItem);
	EXPECT_NE(&Colored.m_Favorite, &Variant.m_Favorite);
	EXPECT_EQ(&Colored.m_Favorite, &State.Resolve("bluekitty", SSettingsSkinListColorKey{true, 123, 456}).m_Favorite);
}

TEST(SettingsTeeSkinListState, DisabledColorsKeepTheSameVariantIdentity)
{
	CSettingsTeeSkinListState State;
	const auto &First = State.Resolve("bluekitty", SSettingsSkinListColorKey{false, 123, 456});
	const auto &Second = State.Resolve("bluekitty", SSettingsSkinListColorKey{false, 789, 987});
	EXPECT_EQ(&First.m_ListItem, &Second.m_ListItem);
	EXPECT_NE(&First.m_ListItem, &State.Resolve("bluekitty", std::nullopt).m_ListItem);
}

TEST(SettingsTeeSkinListState, BaseSkinSelectionFollowsCurrentTargetWithoutWaitingForListRebuild)
{
	const SQmRecentTeeSkin Main{"bluekitty", true, 123, 456};
	const SQmRecentTeeSkin Dummy{"bluekitty", false, 789, 987};
	EXPECT_TRUE(SettingsTeeSkinEntryMatches("bluekitty", std::nullopt, Main));
	EXPECT_TRUE(SettingsTeeSkinEntryMatches("bluekitty", std::nullopt, Dummy));
	EXPECT_FALSE(SettingsTeeSkinEntryMatches("default", std::nullopt, Dummy));
}

TEST(SettingsTeeSkinListState, SavedColorVariantOnlySelectsTheMatchingAppearance)
{
	const SSettingsSkinListColorKey Color{true, 123, 456};
	EXPECT_TRUE(SettingsTeeSkinEntryMatches("bluekitty", Color, {"bluekitty", true, 123, 456}));
	EXPECT_FALSE(SettingsTeeSkinEntryMatches("bluekitty", Color, {"bluekitty", true, 789, 456}));
	EXPECT_FALSE(SettingsTeeSkinEntryMatches("bluekitty", Color, {"bluekitty", false, 123, 456}));
	EXPECT_FALSE(SettingsTeeSkinEntryMatches("bluekitty", Color, {"default", true, 123, 456}));
	EXPECT_TRUE(SettingsTeeSkinEntryMatches("bluekitty", SSettingsSkinListColorKey{false, 123, 456}, {"bluekitty", false, 789, 987}));
}
