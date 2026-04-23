#include <gtest/gtest.h>

#include <game/client/components/assets_resource_registry.h>

#include <algorithm>

namespace
{
bool ContainsWorkshopName(const SAssetResourceCategory *pCategory, const char *pWorkshopName)
{
	if(pCategory == nullptr || pWorkshopName == nullptr)
		return false;

	if(pCategory->m_pWorkshopCategory != nullptr && std::string_view(pCategory->m_pWorkshopCategory) == pWorkshopName)
		return true;
	if(pCategory->m_pWorkshopCategoryAlt != nullptr && std::string_view(pCategory->m_pWorkshopCategoryAlt) == pWorkshopName)
		return true;

	return std::any_of(pCategory->m_vWorkshopCategoryAliases.begin(), pCategory->m_vWorkshopCategoryAliases.end(), [pWorkshopName](const char *pAlias) {
		return pAlias != nullptr && std::string_view(pAlias) == pWorkshopName;
	});
}
}

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

TEST(AssetsResourceRegistry, KeepsLegacyWorkshopCategoryNames)
{
	const SAssetResourceCategory *pHud = FindAssetResourceCategory("hud");
	const SAssetResourceCategory *pEntities = FindAssetResourceCategory("entities");
	const SAssetResourceCategory *pGame = FindAssetResourceCategory("game");
	const SAssetResourceCategory *pEmoticons = FindAssetResourceCategory("emoticons");
	const SAssetResourceCategory *pParticles = FindAssetResourceCategory("particles");

	ASSERT_NE(pHud, nullptr);
	ASSERT_NE(pEntities, nullptr);
	ASSERT_NE(pGame, nullptr);
	ASSERT_NE(pEmoticons, nullptr);
	ASSERT_NE(pParticles, nullptr);

	EXPECT_TRUE(ContainsWorkshopName(pHud, "HUD"));
	EXPECT_TRUE(ContainsWorkshopName(pHud, "界面（HUD）"));

	EXPECT_TRUE(ContainsWorkshopName(pEntities, "实体层"));
	EXPECT_TRUE(ContainsWorkshopName(pEntities, "实体（Entities）"));
	EXPECT_TRUE(ContainsWorkshopName(pEntities, "实体（ENTITIES）"));

	EXPECT_TRUE(ContainsWorkshopName(pGame, "游戏"));
	EXPECT_TRUE(ContainsWorkshopName(pGame, "游戏（Game）"));
	EXPECT_TRUE(ContainsWorkshopName(pGame, "游戏（GAME）"));

	EXPECT_TRUE(ContainsWorkshopName(pEmoticons, "表情"));
	EXPECT_TRUE(ContainsWorkshopName(pEmoticons, "表情（Emoticons）"));
	EXPECT_TRUE(ContainsWorkshopName(pEmoticons, "表情（EMOTICONS）"));

	EXPECT_TRUE(ContainsWorkshopName(pParticles, "粒子"));
	EXPECT_TRUE(ContainsWorkshopName(pParticles, "粒子（Particles）"));
	EXPECT_TRUE(ContainsWorkshopName(pParticles, "粒子（PARTICLES）"));
}
