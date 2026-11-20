#include <game/client/components/assets_author_persistence.h>

#include <gtest/gtest.h>

TEST(AssetsPreviewScale, LocalAuthorPersistenceTabRoundTripsCategoryIds)
{
	EXPECT_TRUE(SupportsPersistedLocalAssetAuthorCategory("entities"));
	EXPECT_TRUE(SupportsPersistedLocalAssetAuthorCategory("game"));
	EXPECT_TRUE(SupportsPersistedLocalAssetAuthorCategory("hud"));
	EXPECT_TRUE(SupportsPersistedLocalAssetAuthorCategory("entity_bg"));
	EXPECT_TRUE(SupportsPersistedLocalAssetAuthorCategory("extras"));
	EXPECT_FALSE(SupportsPersistedLocalAssetAuthorCategory("missing"));
}

TEST(AssetsPreviewScale, LocalAuthorPersistenceBuildsStablePerTabKeys)
{
	EXPECT_EQ(BuildPersistedLocalAssetAuthorKey("entities", "default"), "entities:default");
	EXPECT_EQ(BuildPersistedLocalAssetAuthorKey("game", "default"), "game:default");
	EXPECT_NE(BuildPersistedLocalAssetAuthorKey("entities", "same"), BuildPersistedLocalAssetAuthorKey("game", "same"));
	EXPECT_TRUE(BuildPersistedLocalAssetAuthorKey(nullptr, "same").empty());
	EXPECT_TRUE(BuildPersistedLocalAssetAuthorKey("entities", "").empty());
}

TEST(AssetsPreviewScale, LocalAuthorPersistencePrimarySourcePathsFollowActualAssetLoadRules)
{
	const SAssetResourceCategory *pGame = FindAssetResourceCategory("game");
	const SAssetResourceCategory *pCursor = FindAssetResourceCategory("gui_cursor");
	const SAssetResourceCategory *pEntityBg = FindAssetResourceCategory("entity_bg");
	const SAssetResourceCategory *pExtras = FindAssetResourceCategory("extras");
	ASSERT_NE(pGame, nullptr);
	ASSERT_NE(pCursor, nullptr);
	ASSERT_NE(pEntityBg, nullptr);
	ASSERT_NE(pExtras, nullptr);

	const auto GamePaths = LocalAssetPrimarySourcePaths(*pGame, "foo");
	EXPECT_EQ(GamePaths.m_PrimaryPath, "assets/game/foo.png");
	EXPECT_EQ(GamePaths.m_FallbackPath, "assets/game/foo/game.png");

	const auto CursorPaths = LocalAssetPrimarySourcePaths(*pCursor, "foo");
	EXPECT_EQ(CursorPaths.m_PrimaryPath, "assets/gui_cursor/foo.png");
	EXPECT_TRUE(CursorPaths.m_FallbackPath.empty());

	const auto EntityBgPaths = LocalAssetPrimarySourcePaths(*pEntityBg, "foo");
	EXPECT_EQ(EntityBgPaths.m_PrimaryPath, "maps/foo.map");
	EXPECT_TRUE(EntityBgPaths.m_FallbackPath.empty());

	const auto ManagedEntityBgPaths = LocalAssetPrimarySourcePaths(*pEntityBg, "entity_bg/foo");
	EXPECT_EQ(ManagedEntityBgPaths.m_PrimaryPath, "assets/entity_bg/foo.map");
	EXPECT_EQ(ManagedEntityBgPaths.m_FallbackPath, "maps/entity_bg/foo.map");

	const auto ExtrasPaths = LocalAssetPrimarySourcePaths(*pExtras, "foo");
	EXPECT_EQ(ExtrasPaths.m_PrimaryPath, "assets/extras/foo.png");
	EXPECT_EQ(ExtrasPaths.m_FallbackPath, "assets/extras/foo/extras.png");
}

TEST(AssetsPreviewScale, LocalAuthorPersistenceResolvesPrimarySourcePathUsingRuntimeSelectionRules)
{
	const SAssetResourceCategory *pGame = FindAssetResourceCategory("game");
	const SAssetResourceCategory *pEntityBg = FindAssetResourceCategory("entity_bg");
	ASSERT_NE(pGame, nullptr);
	ASSERT_NE(pEntityBg, nullptr);

	EXPECT_EQ(ResolveLocalAssetPrimarySourcePath(*pGame, "foo", true, true), "assets/game/foo.png");
	EXPECT_EQ(ResolveLocalAssetPrimarySourcePath(*pGame, "foo", false, true), "assets/game/foo/game.png");

	EXPECT_EQ(ResolveLocalAssetPrimarySourcePath(*pEntityBg, "foo", true, false), "maps/foo.map");
	EXPECT_EQ(ResolveLocalAssetPrimarySourcePath(*pEntityBg, "entity_bg/foo", true, false), "assets/entity_bg/foo.map");
	EXPECT_EQ(ResolveLocalAssetPrimarySourcePath(*pEntityBg, "entity_bg/foo", false, true), "maps/entity_bg/foo.map");
}

TEST(AssetsAuthorPersistence, NullOrEmptyNamesProduceNoSourcePathsForAnyRegisteredCategory)
{
	for(const auto &Category : GetAssetResourceCategories())
	{
		SCOPED_TRACE(Category.m_pId);
		for(const char *pName : {static_cast<const char *>(nullptr), ""})
		{
			const auto Paths = LocalAssetPrimarySourcePaths(Category, pName);
			EXPECT_TRUE(Paths.m_PrimaryPath.empty());
			EXPECT_TRUE(Paths.m_FallbackPath.empty());
			EXPECT_TRUE(ResolveLocalAssetPrimarySourcePath(Category, pName, true, true).empty());
		}
	}
}

TEST(AssetsAuthorPersistence, MissingKeyPartsDoNotCreateCacheIdentity)
{
	EXPECT_TRUE(BuildPersistedLocalAssetAuthorKey(nullptr, "name").empty());
	EXPECT_TRUE(BuildPersistedLocalAssetAuthorKey("", "name").empty());
	EXPECT_TRUE(BuildPersistedLocalAssetAuthorKey("game", nullptr).empty());
	EXPECT_TRUE(BuildPersistedLocalAssetAuthorKey("game", "").empty());
	EXPECT_FALSE(SupportsPersistedLocalAssetAuthorCategory(nullptr));
	EXPECT_FALSE(SupportsPersistedLocalAssetAuthorCategory(""));
}

TEST(AssetsAuthorPersistence, ManagedExplicitMapExtensionIsNotDuplicatedOrRecased)
{
	const auto *pCategory = FindAssetResourceCategory("entity_bg");
	ASSERT_NE(pCategory, nullptr);
	const auto Paths = LocalAssetPrimarySourcePaths(*pCategory, "entity_bg/Name.MAP");
	EXPECT_EQ(Paths.m_PrimaryPath, "assets/entity_bg/Name.MAP");
	EXPECT_EQ(Paths.m_FallbackPath, "maps/entity_bg/Name.MAP");
}

TEST(AssetsAuthorPersistence, ManagedImageNamesKeepSupportedImageExtensions)
{
	const auto *pCategory = FindAssetResourceCategory("entity_bg");
	ASSERT_NE(pCategory, nullptr);
	const auto Png = LocalAssetPrimarySourcePaths(*pCategory, "entity_bg/Name.PNG");
	EXPECT_EQ(Png.m_PrimaryPath, "assets/entity_bg/Name.PNG");
	EXPECT_EQ(Png.m_FallbackPath, "maps/entity_bg/Name.PNG");
	const auto Webp = LocalAssetPrimarySourcePaths(*pCategory, "entity_bg/name.webp");
	EXPECT_EQ(Webp.m_PrimaryPath, "assets/entity_bg/name.webp");
	EXPECT_EQ(Webp.m_FallbackPath, "maps/entity_bg/name.webp");
}

TEST(AssetsAuthorPersistence, UnmanagedImageUsesMapsAndHasNoManagedAssetFallback)
{
	const auto *pCategory = FindAssetResourceCategory("entity_bg");
	ASSERT_NE(pCategory, nullptr);
	const auto Paths = LocalAssetPrimarySourcePaths(*pCategory, "gallery/name.webp");
	EXPECT_EQ(Paths.m_PrimaryPath, "maps/gallery/name.webp");
	EXPECT_TRUE(Paths.m_FallbackPath.empty());
	EXPECT_EQ(ResolveLocalAssetPrimarySourcePath(*pCategory, "gallery/name.webp", false, true), "maps/gallery/name.webp");
}

TEST(AssetsAuthorPersistence, MissingBothCandidatesRetainsPrimaryPathForCallerExistenceCheck)
{
	const auto *pGame = FindAssetResourceCategory("game");
	const auto *pBackground = FindAssetResourceCategory("entity_bg");
	ASSERT_NE(pGame, nullptr);
	ASSERT_NE(pBackground, nullptr);
	EXPECT_EQ(ResolveLocalAssetPrimarySourcePath(*pGame, "name", false, false), "assets/game/name.png");
	EXPECT_EQ(ResolveLocalAssetPrimarySourcePath(*pBackground, "entity_bg/name", false, false), "assets/entity_bg/name.map");
}
