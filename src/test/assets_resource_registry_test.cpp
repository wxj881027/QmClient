#include <gtest/gtest.h>

#include <base/system.h>

#include <game/client/components/assets_resource_registry.h>
#include <game/client/components/background.h>
#include <game/client/components/menus.h>

#include <algorithm>
#include <unordered_map>

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
	EXPECT_NE(FindAssetResourceCategory("extras"), nullptr);
}

TEST(AssetsResourceRegistry, LocalOnlyCategoriesExposeBadgeFlag)
{
	const SAssetResourceCategory *pGuiCursor = FindAssetResourceCategory("gui_cursor");
	const SAssetResourceCategory *pEntityBg = FindAssetResourceCategory("entity_bg");
	const SAssetResourceCategory *pHud = FindAssetResourceCategory("hud");

	ASSERT_NE(pGuiCursor, nullptr);
	ASSERT_NE(pEntityBg, nullptr);
	ASSERT_NE(pHud, nullptr);

	EXPECT_FALSE(pGuiCursor->m_LocalOnlyBadge);
	EXPECT_FALSE(pEntityBg->m_LocalOnlyBadge);
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

TEST(AssetsResourceRegistry, EntityBgWorkshopInstallFolderUsesAssetsDirectory)
{
	const SAssetResourceCategory *pEntityBg = FindAssetResourceCategory("entity_bg");
	ASSERT_NE(pEntityBg, nullptr);
	EXPECT_STREQ(pEntityBg->m_pInstallFolder, "assets/entity_bg");
}

TEST(AssetsResourceRegistry, NamedSingleFileAssetCandidatesPreferCategoryAssetThenLegacyThenBuiltin)
{
	const auto aCandidates = BuildNamedSingleFileAssetCandidates("gui_cursor", "legacy");

	EXPECT_EQ(aCandidates[0], "assets/gui_cursor/legacy.png");
	EXPECT_EQ(aCandidates[1], "assets/gui_cursor/legacy/gui_cursor.png");
	EXPECT_EQ(aCandidates[2], "gui_cursor.png");
	EXPECT_TRUE(aCandidates[3].empty());
}

TEST(AssetsResourceRegistry, NamedSingleFileAssetCandidatesUseBuiltinNamesForDefault)
{
	const auto aGuiCursorCandidates = BuildNamedSingleFileAssetCandidates("gui_cursor", "default");
	const auto aArrowCandidates = BuildNamedSingleFileAssetCandidates("arrow", "default");
	const auto aStrongWeakCandidates = BuildNamedSingleFileAssetCandidates("strong_weak", "default");

	EXPECT_EQ(aGuiCursorCandidates[0], "gui_cursor.png");
	EXPECT_TRUE(aGuiCursorCandidates[1].empty());
	EXPECT_TRUE(aGuiCursorCandidates[2].empty());

	EXPECT_EQ(aArrowCandidates[0], "arrow.png");
	EXPECT_TRUE(aArrowCandidates[1].empty());
	EXPECT_TRUE(aArrowCandidates[2].empty());

	EXPECT_EQ(aStrongWeakCandidates[0], "strong_weak.png");
	EXPECT_TRUE(aStrongWeakCandidates[1].empty());
	EXPECT_TRUE(aStrongWeakCandidates[2].empty());
}

TEST(AssetsResourceRegistry, NamedSingleFileAssetReservedNamesCoverBuiltinAndDefaultAliases)
{
	const SAssetResourceCategory *pArrow = FindAssetResourceCategory("arrow");
	const SAssetResourceCategory *pStrongWeak = FindAssetResourceCategory("strong_weak");
	ASSERT_NE(pArrow, nullptr);
	ASSERT_NE(pStrongWeak, nullptr);

	EXPECT_TRUE(IsReservedNamedSingleFileAssetName(*pArrow, "default"));
	EXPECT_TRUE(IsReservedNamedSingleFileAssetName(*pArrow, "arrow"));
	EXPECT_TRUE(IsReservedNamedSingleFileAssetName(*pStrongWeak, "strong_weak"));
	EXPECT_FALSE(IsReservedNamedSingleFileAssetName(*pArrow, "arrow_pack"));
	EXPECT_FALSE(IsReservedNamedSingleFileAssetName(*pStrongWeak, "strong_weak_pack"));
}

TEST(AssetsResourceRegistry, EnsureDefaultAssetVisibleInjectsAndSortsDefaultFirst)
{
	std::vector<std::string> vAssetNames = {"entity_bg/demo", "entity_bg/alpha"};

	EnsureDefaultAssetVisible(vAssetNames);

	ASSERT_EQ(vAssetNames.size(), 3u);
	EXPECT_EQ(vAssetNames[0], "default");
	EXPECT_EQ(vAssetNames[1], "entity_bg/alpha");
	EXPECT_EQ(vAssetNames[2], "entity_bg/demo");
}

TEST(AssetsResourceRegistry, EnsureDefaultAssetVisibleDoesNotDuplicateDefault)
{
	std::vector<std::string> vAssetNames = {"entity_bg/demo", "default", "entity_bg/alpha"};

	EnsureDefaultAssetVisible(vAssetNames);

	ASSERT_EQ(vAssetNames.size(), 3u);
	EXPECT_EQ(vAssetNames[0], "default");
	EXPECT_EQ(vAssetNames[1], "entity_bg/alpha");
	EXPECT_EQ(vAssetNames[2], "entity_bg/demo");
}

TEST(AssetsResourceRegistry, EntityBgHierarchyViewBuildsFolderEntriesAndShortFileNames)
{
	const std::vector<std::string> vAssetNames = {
		"default",
		"entity_bg/castle/day",
		"entity_bg/castle/night",
		"entity_bg/forest",
		"local_folder/demo",
		"outside",
	};
	const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> vSources = {
		{"default", EEntityBgHierarchyEntrySource::LOCAL},
		{"entity_bg/castle/day", EEntityBgHierarchyEntrySource::WORKSHOP},
		{"entity_bg/castle/night", EEntityBgHierarchyEntrySource::WORKSHOP},
		{"entity_bg/forest", EEntityBgHierarchyEntrySource::WORKSHOP},
		{"local_folder/demo", EEntityBgHierarchyEntrySource::LOCAL},
		{"outside", EEntityBgHierarchyEntrySource::LOCAL},
	};

	const auto vRootEntries = BuildEntityBgHierarchyEntries(vAssetNames, "", true, &vSources);
	EXPECT_TRUE(vRootEntries.size() == 4u);
	EXPECT_TRUE(std::string_view(vRootEntries[0].m_aName) == "entity_bg");
	EXPECT_TRUE(vRootEntries[0].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vRootEntries[0].m_aDisplayName) == "entity_bg (Workshop)");
	EXPECT_TRUE(std::string_view(vRootEntries[1].m_aName) == "local_folder");
	EXPECT_TRUE(vRootEntries[1].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vRootEntries[1].m_aDisplayName) == "local_folder");
	EXPECT_TRUE(std::string_view(vRootEntries[2].m_aName) == "default");
	EXPECT_FALSE(vRootEntries[2].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vRootEntries[2].m_aDisplayName) == "default");
	EXPECT_TRUE(std::string_view(vRootEntries[3].m_aName) == "outside");
	EXPECT_FALSE(vRootEntries[3].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vRootEntries[3].m_aDisplayName) == "outside");

	const auto vWorkshopEntries = BuildEntityBgHierarchyEntries(vAssetNames, "entity_bg", true, &vSources);
	EXPECT_TRUE(vWorkshopEntries.size() == 3u);
	EXPECT_TRUE(std::string_view(vWorkshopEntries[0].m_aName) == "..");
	EXPECT_TRUE(vWorkshopEntries[0].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vWorkshopEntries[1].m_aName) == "entity_bg/castle");
	EXPECT_TRUE(vWorkshopEntries[1].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vWorkshopEntries[1].m_aDisplayName) == "castle");
	EXPECT_TRUE(std::string_view(vWorkshopEntries[2].m_aName) == "entity_bg/forest");
	EXPECT_FALSE(vWorkshopEntries[2].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vWorkshopEntries[2].m_aDisplayName) == "forest");

	const auto vNestedEntries = BuildEntityBgHierarchyEntries(vAssetNames, "entity_bg/castle", true, &vSources);
	EXPECT_TRUE(vNestedEntries.size() == 3u);
	EXPECT_TRUE(std::string_view(vNestedEntries[0].m_aName) == "..");
	EXPECT_TRUE(vNestedEntries[0].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vNestedEntries[0].m_aDisplayName) == "..");
	EXPECT_TRUE(std::string_view(vNestedEntries[1].m_aName) == "entity_bg/castle/day");
	EXPECT_FALSE(vNestedEntries[1].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vNestedEntries[1].m_aDisplayName) == "day");
	EXPECT_TRUE(std::string_view(vNestedEntries[2].m_aName) == "entity_bg/castle/night");
	EXPECT_FALSE(vNestedEntries[2].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vNestedEntries[2].m_aDisplayName) == "night");

	const auto vLocalFolderEntries = BuildEntityBgHierarchyEntries(vAssetNames, "local_folder", true, &vSources);
	EXPECT_TRUE(vLocalFolderEntries.size() == 2u);
	EXPECT_TRUE(std::string_view(vLocalFolderEntries[0].m_aName) == "..");
	EXPECT_TRUE(vLocalFolderEntries[0].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vLocalFolderEntries[1].m_aName) == "local_folder/demo");
	EXPECT_FALSE(vLocalFolderEntries[1].m_IsDirectory);
	EXPECT_TRUE(std::string_view(vLocalFolderEntries[1].m_aDisplayName) == "demo");
}

TEST(AssetsResourceRegistry, EntityBgHierarchyViewUsesRootPriorityAndEntrySources)
{
	const std::vector<std::string> vAssetNames = {
		"zeta",
		"default",
		"entity_bg/workshop_item",
		"local_folder/folder_item",
		"beta",
	};
	const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> vSources = {
		{"zeta", EEntityBgHierarchyEntrySource::LOCAL},
		{"default", EEntityBgHierarchyEntrySource::LOCAL},
		{"entity_bg/workshop_item", EEntityBgHierarchyEntrySource::WORKSHOP},
		{"local_folder/folder_item", EEntityBgHierarchyEntrySource::LOCAL},
		{"beta", EEntityBgHierarchyEntrySource::LOCAL},
	};

	const auto vRootEntries = BuildEntityBgHierarchyEntries(vAssetNames, "", true, &vSources);

	ASSERT_EQ(vRootEntries.size(), 5u);
	EXPECT_STREQ(vRootEntries[0].m_aName, "entity_bg");
	EXPECT_EQ(vRootEntries[0].m_Source, EEntityBgHierarchyEntrySource::WORKSHOP);
	EXPECT_TRUE(vRootEntries[0].m_IsDirectory);

	EXPECT_STREQ(vRootEntries[1].m_aName, "local_folder");
	EXPECT_EQ(vRootEntries[1].m_Source, EEntityBgHierarchyEntrySource::LOCAL);
	EXPECT_TRUE(vRootEntries[1].m_IsDirectory);

	EXPECT_STREQ(vRootEntries[2].m_aName, "default");
	EXPECT_EQ(vRootEntries[2].m_Source, EEntityBgHierarchyEntrySource::LOCAL);
	EXPECT_FALSE(vRootEntries[2].m_IsDirectory);

	EXPECT_STREQ(vRootEntries[3].m_aName, "beta");
	EXPECT_EQ(vRootEntries[3].m_Source, EEntityBgHierarchyEntrySource::LOCAL);
	EXPECT_FALSE(vRootEntries[3].m_IsDirectory);

	EXPECT_STREQ(vRootEntries[4].m_aName, "zeta");
	EXPECT_EQ(vRootEntries[4].m_Source, EEntityBgHierarchyEntrySource::LOCAL);
	EXPECT_FALSE(vRootEntries[4].m_IsDirectory);
}

TEST(AssetsResourceRegistry, EntityBgHierarchyViewCanHideWorkshopFolderAtRoot)
{
	const std::vector<std::string> vAssetNames = {
		"default",
		"entity_bg/workshop_item",
		"local_folder/demo",
	};
	const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> vSources = {
		{"default", EEntityBgHierarchyEntrySource::LOCAL},
		{"entity_bg/workshop_item", EEntityBgHierarchyEntrySource::WORKSHOP},
		{"local_folder/demo", EEntityBgHierarchyEntrySource::LOCAL},
	};

	EXPECT_TRUE(HasEntityBgWorkshopFolder(vAssetNames, &vSources));

	const auto vHiddenWorkshopEntries = BuildEntityBgHierarchyEntries(vAssetNames, "", false, &vSources);

	ASSERT_EQ(vHiddenWorkshopEntries.size(), 2u);
	EXPECT_STREQ(vHiddenWorkshopEntries[0].m_aName, "local_folder");
	EXPECT_STREQ(vHiddenWorkshopEntries[1].m_aName, "default");
}

TEST(AssetsResourceRegistry, EntityBgHierarchyViewCanForceWorkshopFolderAtRootWithoutInstalledAssets)
{
	const std::vector<std::string> vAssetNames = {
		"default",
		"local_folder/demo",
	};
	const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> vSources = {
		{"default", EEntityBgHierarchyEntrySource::LOCAL},
		{"local_folder/demo", EEntityBgHierarchyEntrySource::LOCAL},
	};

	EXPECT_FALSE(HasEntityBgWorkshopFolder(vAssetNames, &vSources));

	const auto vRootEntries = BuildEntityBgHierarchyEntries(vAssetNames, "", true, &vSources, true);

	ASSERT_EQ(vRootEntries.size(), 3u);
	EXPECT_STREQ(vRootEntries[0].m_aName, "entity_bg");
	EXPECT_EQ(vRootEntries[0].m_Source, EEntityBgHierarchyEntrySource::WORKSHOP);
	EXPECT_TRUE(vRootEntries[0].m_IsDirectory);
	EXPECT_STREQ(vRootEntries[0].m_aDisplayName, "entity_bg (Workshop)");
	EXPECT_STREQ(vRootEntries[1].m_aName, "local_folder");
	EXPECT_STREQ(vRootEntries[2].m_aName, "default");
}

TEST(AssetsResourceRegistry, EntityBgHierarchyViewKeepsManagedLocalAssetsVisibleWhenWorkshopIsHidden)
{
	const std::vector<std::string> vAssetNames = {
		"default",
		"entity_bg/local_folder/day",
		"entity_bg/local_item",
		"entity_bg/workshop_item",
	};
	const std::unordered_map<std::string, EEntityBgHierarchyEntrySource> vSources = {
		{"default", EEntityBgHierarchyEntrySource::LOCAL},
		{"entity_bg/local_folder/day", EEntityBgHierarchyEntrySource::LOCAL},
		{"entity_bg/local_item", EEntityBgHierarchyEntrySource::LOCAL},
		{"entity_bg/workshop_item", EEntityBgHierarchyEntrySource::WORKSHOP},
	};

	const auto vRootEntries = BuildEntityBgHierarchyEntries(vAssetNames, "", false, &vSources);
	ASSERT_EQ(vRootEntries.size(), 3u);
	EXPECT_STREQ(vRootEntries[0].m_aName, "local_folder");
	EXPECT_EQ(vRootEntries[0].m_Source, EEntityBgHierarchyEntrySource::LOCAL);
	EXPECT_TRUE(vRootEntries[0].m_IsDirectory);
	EXPECT_STREQ(vRootEntries[1].m_aName, "default");
	EXPECT_EQ(vRootEntries[1].m_Source, EEntityBgHierarchyEntrySource::LOCAL);
	EXPECT_STREQ(vRootEntries[2].m_aName, "entity_bg/local_item");
	EXPECT_EQ(vRootEntries[2].m_Source, EEntityBgHierarchyEntrySource::LOCAL);
	EXPECT_FALSE(vRootEntries[2].m_IsDirectory);

	const auto vFolderEntries = BuildEntityBgHierarchyEntries(vAssetNames, "local_folder", false, &vSources);
	ASSERT_EQ(vFolderEntries.size(), 2u);
	EXPECT_STREQ(vFolderEntries[0].m_aName, "..");
	EXPECT_STREQ(vFolderEntries[1].m_aName, "entity_bg/local_folder/day");
	EXPECT_EQ(vFolderEntries[1].m_Source, EEntityBgHierarchyEntrySource::LOCAL);
	EXPECT_STREQ(vFolderEntries[1].m_aDisplayName, "day");
}

TEST(AssetsResourceRegistry, RebuildEntityBgWorkshopLocalNameRestoresLocalNameFromInstallPath)
{
	EXPECT_EQ(RebuildEntityBgWorkshopLocalName("assets/entity_bg/castle-night.map"), "entity_bg/castle-night");
	EXPECT_EQ(RebuildEntityBgWorkshopLocalName("assets/entity_bg/folder/demo.map"), "entity_bg/folder/demo");
	EXPECT_TRUE(RebuildEntityBgWorkshopLocalName("assets/game/demo.png").empty());
}

TEST(AssetsResourceRegistry, NormalizeEntityBgWorkshopInstallPathForcesMapExtension)
{
	EXPECT_EQ(NormalizeEntityBgWorkshopInstallPath("assets/entity_bg/demo.png"), "assets/entity_bg/demo.map");
	EXPECT_EQ(NormalizeEntityBgWorkshopInstallPath("assets/entity_bg/folder/demo.webp"), "assets/entity_bg/folder/demo.map");
	EXPECT_EQ(NormalizeEntityBgWorkshopInstallPath("assets/entity_bg/demo.map"), "assets/entity_bg/demo.map");
	EXPECT_TRUE(NormalizeEntityBgWorkshopInstallPath("assets/game/demo.png").empty());
}

TEST(AssetsResourceRegistry, EntityBgHierarchyEntryLessKeepsParentBeforeFoldersAndDefault)
{
	SEntityBgHierarchyEntry ParentEntry;
	str_copy(ParentEntry.m_aName, "..");
	str_copy(ParentEntry.m_aDisplayName, "..");
	ParentEntry.m_IsDirectory = true;
	ParentEntry.m_Source = EEntityBgHierarchyEntrySource::NAVIGATION;

	SEntityBgHierarchyEntry FolderEntry;
	str_copy(FolderEntry.m_aName, "alpha");
	str_copy(FolderEntry.m_aDisplayName, "alpha");
	FolderEntry.m_IsDirectory = true;
	FolderEntry.m_Source = EEntityBgHierarchyEntrySource::LOCAL;

	SEntityBgHierarchyEntry DefaultEntry;
	str_copy(DefaultEntry.m_aName, "default");
	str_copy(DefaultEntry.m_aDisplayName, "default");
	DefaultEntry.m_IsDirectory = false;
	DefaultEntry.m_Source = EEntityBgHierarchyEntrySource::LOCAL;

	SEntityBgHierarchyEntry FileEntry;
	str_copy(FileEntry.m_aName, "beta");
	str_copy(FileEntry.m_aDisplayName, "beta");
	FileEntry.m_IsDirectory = false;
	FileEntry.m_Source = EEntityBgHierarchyEntrySource::LOCAL;

	EXPECT_TRUE(EntityBgHierarchyEntryLess(ParentEntry, FolderEntry));
	EXPECT_TRUE(EntityBgHierarchyEntryLess(FolderEntry, DefaultEntry));
	EXPECT_TRUE(EntityBgHierarchyEntryLess(DefaultEntry, FileEntry));
	EXPECT_FALSE(EntityBgHierarchyEntryLess(FileEntry, DefaultEntry));
}

TEST(AssetsResourceRegistry, WorkshopAliasesCoverNewResourceTabs)
{
	const SAssetResourceCategory *pGuiCursor = FindAssetResourceCategory("gui_cursor");
	const SAssetResourceCategory *pArrow = FindAssetResourceCategory("arrow");
	const SAssetResourceCategory *pStrongWeak = FindAssetResourceCategory("strong_weak");
	const SAssetResourceCategory *pEntityBg = FindAssetResourceCategory("entity_bg");
	const SAssetResourceCategory *pExtras = FindAssetResourceCategory("extras");

	ASSERT_NE(pGuiCursor, nullptr);
	ASSERT_NE(pArrow, nullptr);
	ASSERT_NE(pStrongWeak, nullptr);
	ASSERT_NE(pEntityBg, nullptr);
	ASSERT_NE(pExtras, nullptr);

	EXPECT_TRUE(ContainsWorkshopName(pGuiCursor, "鼠标"));
	EXPECT_TRUE(ContainsWorkshopName(pGuiCursor, "gui_cursor"));
	EXPECT_TRUE(ContainsWorkshopName(pGuiCursor, "mouse"));
	EXPECT_TRUE(ContainsWorkshopName(pArrow, "方向键"));
	EXPECT_TRUE(ContainsWorkshopName(pArrow, "arrow"));
	EXPECT_TRUE(ContainsWorkshopName(pStrongWeak, "强弱钩"));
	EXPECT_TRUE(ContainsWorkshopName(pStrongWeak, "strong_weak"));
	EXPECT_TRUE(ContainsWorkshopName(pEntityBg, "实体层背景图"));
	EXPECT_TRUE(ContainsWorkshopName(pEntityBg, "entity_bg"));
	EXPECT_TRUE(ContainsWorkshopName(pExtras, "其他"));
	EXPECT_TRUE(ContainsWorkshopName(pExtras, "extras"));
}

TEST(AssetsResourceRegistry, WorkshopAliasesAcceptInternalIdsForNewResourceTabs)
{
	const SAssetResourceCategory *pGuiCursor = FindAssetResourceCategory("gui_cursor");
	const SAssetResourceCategory *pArrow = FindAssetResourceCategory("arrow");
	const SAssetResourceCategory *pStrongWeak = FindAssetResourceCategory("strong_weak");
	const SAssetResourceCategory *pEntityBg = FindAssetResourceCategory("entity_bg");
	const SAssetResourceCategory *pExtras = FindAssetResourceCategory("extras");

	ASSERT_NE(pGuiCursor, nullptr);
	ASSERT_NE(pArrow, nullptr);
	ASSERT_NE(pStrongWeak, nullptr);
	ASSERT_NE(pEntityBg, nullptr);
	ASSERT_NE(pExtras, nullptr);

	ASSERT_STREQ(pGuiCursor->m_pWorkshopCategory, "gui_cursor");
	ASSERT_STREQ(pArrow->m_pWorkshopCategory, "arrow");
	ASSERT_STREQ(pStrongWeak->m_pWorkshopCategory, "strong_weak");
	ASSERT_STREQ(pEntityBg->m_pWorkshopCategory, "entity_bg");
	ASSERT_STREQ(pExtras->m_pWorkshopCategory, "extras");

	EXPECT_TRUE(ContainsWorkshopName(pGuiCursor, "gui_cursor"));
	EXPECT_TRUE(ContainsWorkshopName(pArrow, "arrow"));
	EXPECT_TRUE(ContainsWorkshopName(pStrongWeak, "strong_weak"));
	EXPECT_TRUE(ContainsWorkshopName(pEntityBg, "entity_bg"));
	EXPECT_TRUE(ContainsWorkshopName(pExtras, "extras"));
}

TEST(BackgroundEntitiesValue, NormalizesLeadingSlashAndMapsPrefix)
{
	char aNormalized[IO_MAX_PATH_LENGTH];
	NormalizeBackgroundEntitiesValue("/maps/entity_bg/demo.map", aNormalized, sizeof(aNormalized));
	EXPECT_STREQ(aNormalized, "entity_bg/demo.map");

	NormalizeBackgroundEntitiesValue("assets/entity_bg/demo.map", aNormalized, sizeof(aNormalized));
	EXPECT_STREQ(aNormalized, "entity_bg/demo.map");
}

TEST(BackgroundEntitiesValue, ExtractsAssetNameFromMapConfig)
{
	char aAssetName[IO_MAX_PATH_LENGTH];
	ASSERT_TRUE(TryGetBackgroundEntitiesAssetName("maps/entity_bg/demo.map", aAssetName, sizeof(aAssetName)));
	EXPECT_STREQ(aAssetName, "entity_bg/demo");
}

TEST(BackgroundEntitiesValue, RejectsDefaultAndCurrentMapValues)
{
	char aAssetName[IO_MAX_PATH_LENGTH];
	EXPECT_FALSE(TryGetBackgroundEntitiesAssetName("", aAssetName, sizeof(aAssetName)));
	EXPECT_FALSE(TryGetBackgroundEntitiesAssetName(CURRENT_MAP, aAssetName, sizeof(aAssetName)));
	ASSERT_TRUE(TryGetBackgroundEntitiesAssetName("entity_bg/demo.png", aAssetName, sizeof(aAssetName)));
	EXPECT_STREQ(aAssetName, "entity_bg/demo.png");
	ASSERT_TRUE(TryGetBackgroundEntitiesAssetName("entity_bg/demo.mp4", aAssetName, sizeof(aAssetName)));
	EXPECT_STREQ(aAssetName, "entity_bg/demo.mp4");
}

TEST(BackgroundEntitiesValue, BuildsConfigValueFromAssetName)
{
	char aConfigValue[IO_MAX_PATH_LENGTH];
	BuildBackgroundEntitiesValueFromAsset("entity_bg/demo", aConfigValue, sizeof(aConfigValue));
	EXPECT_STREQ(aConfigValue, "entity_bg/demo.map");

	BuildBackgroundEntitiesValueFromAsset("default", aConfigValue, sizeof(aConfigValue));
	EXPECT_STREQ(aConfigValue, "");
}

TEST(BackgroundEntitiesValue, PreservesManualMapInputWithoutAssetCoercion)
{
	char aConfigValue[IO_MAX_PATH_LENGTH];

	BuildBackgroundEntitiesValueFromInput("entity_bg/custom.map", aConfigValue, sizeof(aConfigValue));
	EXPECT_STREQ(aConfigValue, "entity_bg/custom.map");

	BuildBackgroundEntitiesValueFromInput("assets/entity_bg/custom.map", aConfigValue, sizeof(aConfigValue));
	EXPECT_STREQ(aConfigValue, "entity_bg/custom.map");

	BuildBackgroundEntitiesValueFromInput("maps/folder/demo.map", aConfigValue, sizeof(aConfigValue));
	EXPECT_STREQ(aConfigValue, "folder/demo.map");

	BuildBackgroundEntitiesValueFromInput(CURRENT_MAP, aConfigValue, sizeof(aConfigValue));
	EXPECT_STREQ(aConfigValue, CURRENT_MAP);
}

TEST(BackgroundEntitiesValue, BuildsCommittedValueAndDetectsChanges)
{
	char aCommittedValue[IO_MAX_PATH_LENGTH];

	EXPECT_TRUE(BuildBackgroundEntitiesCommitValueFromInput("maps/entity_bg/demo.map", CURRENT_MAP, aCommittedValue, sizeof(aCommittedValue)));
	EXPECT_STREQ(aCommittedValue, "entity_bg/demo.map");
}

TEST(BackgroundEntitiesValue, IgnoresEquivalentNormalizedConfigDuringCommit)
{
	char aCommittedValue[IO_MAX_PATH_LENGTH];

	EXPECT_FALSE(BuildBackgroundEntitiesCommitValueFromInput("maps/folder/demo.map", "folder/demo.map", aCommittedValue, sizeof(aCommittedValue)));
	EXPECT_STREQ(aCommittedValue, "folder/demo.map");
}

TEST(BackgroundEntitiesValue, RequestsCommitWhenBlurLeavesPendingEditedValue)
{
	EXPECT_TRUE(ShouldCommitBackgroundEntitiesInputOnBlur(true, false, "maps/entity_bg/demo.map", CURRENT_MAP));
	EXPECT_FALSE(ShouldCommitBackgroundEntitiesInputOnBlur(true, true, "maps/entity_bg/demo.map", CURRENT_MAP));
	EXPECT_FALSE(ShouldCommitBackgroundEntitiesInputOnBlur(false, false, "maps/entity_bg/demo.map", CURRENT_MAP));
	EXPECT_FALSE(ShouldCommitBackgroundEntitiesInputOnBlur(true, false, "maps/entity_bg/demo.map", "entity_bg/demo.map"));
}

TEST(AssetsResourceRegistry, EntityBgDirectoryCardsHidePathSubtitle)
{
	EXPECT_FALSE(ShouldShowEntityBgDirectorySubtitle(true));
	EXPECT_TRUE(ShouldShowEntityBgDirectorySubtitle(false));
}

TEST(AssetsResourceRegistry, EntityBgParentFolderReturnsToRootFromFirstLevelFolder)
{
	char aParent[IO_MAX_PATH_LENGTH];

	BuildEntityBgParentFolder("entity_bg/castle", aParent, sizeof(aParent));
	EXPECT_STREQ(aParent, "entity_bg");

	BuildEntityBgParentFolder("entity_bg/castle/day", aParent, sizeof(aParent));
	EXPECT_STREQ(aParent, "entity_bg/castle");

	BuildEntityBgParentFolder("", aParent, sizeof(aParent));
	EXPECT_STREQ(aParent, "");
}

TEST(AssetsResourceRegistry, AssetCardAuthorRowHiddenForDirectoriesAndEmptyAuthors)
{
	EXPECT_FALSE(ShouldShowAssetCardAuthorRow(false, false));
	EXPECT_FALSE(ShouldShowAssetCardAuthorRow(true, true));
	EXPECT_TRUE(ShouldShowAssetCardAuthorRow(true, false));
}

TEST(AssetsEditorColorOverride, MultipliesRgbAndAlphaChannels)
{
	const ColorRGBA Base(0.50f, 0.25f, 1.00f, 0.80f);
	const ColorRGBA Tint(0.40f, 1.00f, 0.50f, 0.25f);

	const ColorRGBA Result = CMenus::AssetsEditorMultiplyColor(Base, Tint);

	EXPECT_NEAR(Result.r, 0.20f, 0.01f);
	EXPECT_NEAR(Result.g, 0.25f, 0.01f);
	EXPECT_NEAR(Result.b, 0.50f, 0.01f);
	EXPECT_NEAR(Result.a, 0.20f, 0.01f);
}

TEST(AssetsEditorColorOverride, DetectsDefaultWhiteTint)
{
	EXPECT_FALSE(CMenus::AssetsEditorHasColorOverride(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f)));
	EXPECT_TRUE(CMenus::AssetsEditorHasColorOverride(ColorRGBA(1.0f, 0.9f, 1.0f, 1.0f)));
	EXPECT_TRUE(CMenus::AssetsEditorHasColorOverride(ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f)));
}
