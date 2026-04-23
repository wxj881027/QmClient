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
