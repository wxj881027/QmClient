#include <gtest/gtest.h>

#include <game/client/components/assets_resource_registry.h>

TEST(AssetsResourceRegistry, IncludesAllApprovedCategories)
{
	EXPECT_NE(FindAssetResourceCategory("gui_cursor"), nullptr);
	EXPECT_NE(FindAssetResourceCategory("arrow"), nullptr);
	EXPECT_NE(FindAssetResourceCategory("strong_weak"), nullptr);
	EXPECT_NE(FindAssetResourceCategory("entity_bg"), nullptr);
}

TEST(AssetsResourceRegistry, LocalOnlyCategoriesExposeBadgeFlag)
{
	const SAssetResourceCategory *pGuiCursor = FindAssetResourceCategory("gui_cursor");
	const SAssetResourceCategory *pEntityBg = FindAssetResourceCategory("entity_bg");
	const SAssetResourceCategory *pHud = FindAssetResourceCategory("hud");

	ASSERT_NE(pGuiCursor, nullptr);
	ASSERT_NE(pEntityBg, nullptr);
	ASSERT_NE(pHud, nullptr);

	EXPECT_TRUE(pGuiCursor->m_LocalOnlyBadge);
	EXPECT_TRUE(pEntityBg->m_LocalOnlyBadge);
	EXPECT_FALSE(pHud->m_LocalOnlyBadge);
}

TEST(AssetsResourceRegistry, WorkshopMetadataMatchesApprovedCategories)
{
	const SAssetResourceCategory *pEntities = FindAssetResourceCategory("entities");
	const SAssetResourceCategory *pHud = FindAssetResourceCategory("hud");
	const SAssetResourceCategory *pArrow = FindAssetResourceCategory("arrow");

	ASSERT_NE(pEntities, nullptr);
	ASSERT_NE(pHud, nullptr);
	ASSERT_NE(pArrow, nullptr);

	EXPECT_TRUE(pEntities->m_WorkshopEnabled);
	EXPECT_STREQ(pEntities->m_pWorkshopCategory, "entities");
	EXPECT_STREQ(pEntities->m_pWorkshopCategoryAlt, "entity");

	EXPECT_TRUE(pHud->m_WorkshopEnabled);
	EXPECT_STREQ(pHud->m_pWorkshopCategory, "hud");
	EXPECT_EQ(pHud->m_pWorkshopCategoryAlt, nullptr);

	EXPECT_FALSE(pArrow->m_WorkshopEnabled);
	EXPECT_EQ(pArrow->m_pWorkshopCategory, nullptr);
	EXPECT_EQ(pArrow->m_pWorkshopCategoryAlt, nullptr);
}

TEST(AssetsResourceRegistry, PreviewAndInstallSemanticsMatchApprovedCategories)
{
	const SAssetResourceCategory *pEntityBg = FindAssetResourceCategory("entity_bg");
	const SAssetResourceCategory *pGame = FindAssetResourceCategory("game");

	ASSERT_NE(pEntityBg, nullptr);
	ASSERT_NE(pGame, nullptr);

	EXPECT_EQ(pEntityBg->m_Kind, EAssetResourceKind::MAP_FILE);
	EXPECT_STREQ(pEntityBg->m_pInstallFolder, "maps");
	EXPECT_EQ(pEntityBg->m_PreviewKind, EAssetPreviewKind::MAP_PREVIEW_OR_ICON);

	EXPECT_EQ(pGame->m_Kind, EAssetResourceKind::DIRECTORY);
	EXPECT_STREQ(pGame->m_pInstallFolder, "assets/game");
	EXPECT_EQ(pGame->m_PreviewKind, EAssetPreviewKind::PNG_TEXTURE);
}
