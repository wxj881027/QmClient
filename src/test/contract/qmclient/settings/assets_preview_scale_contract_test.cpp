#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

TEST(AssetsPreviewScale, WorkshopThumbDecodeResizesOffMainThread)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("Asset.m_pDecodeJob = std::make_shared<CFullAsyncImageLoadJob>(std::move(vPossiblePaths), pStorage, Asset.m_Name.c_str(), IStorage::TYPE_SAVE, MaxTextureSize);"), std::string::npos);
	EXPECT_NE(Source.find("SettingsAssetPreviewBudgetedTextureSize("), std::string::npos);
	EXPECT_NE(Source.find("const SWorkshopPreviewDecodeSourcePlan SourcePlan = BuildWorkshopPreviewDecodeSourcePlan(pCategoryId, Asset.m_Installed, !Asset.m_ThumbCachePath.empty());"), std::string::npos);
	EXPECT_NE(Source.find("if(SourcePlan.m_UseInstallSource && !Asset.m_InstallPath.empty())"), std::string::npos);
	EXPECT_NE(Source.find("if(SourcePlan.m_UseThumbCache)"), std::string::npos);
	EXPECT_EQ(Source.find("const SPreviewTargetSize TargetSize = ComputePreviewTargetSize(Result.m_Image.m_Width, Result.m_Image.m_Height, WORKSHOP_ASSET_PREVIEW_MAX_TEXTURE_SIZE);"), std::string::npos);
}

TEST(AssetsPreviewScale, EntitiesPreviewTileSizeIsClampedByBothAxes)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	// TileSize now comes from the shared ComputeEntityPreviewTileSize helper
	// (behavior-tested in EntityPreviewTileSizeFitsBothAxes), not an inline minimum.
	EXPECT_NE(Source.find("TileSize = ComputeEntityPreviewTileSize(PreviewRect.w, PreviewRect.h, COLS, ROWS);"), std::string::npos);
}

TEST(AssetsPreviewScale, CursorAndArrowPreviewCardsUseSmallerContentBounds)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("else if(s_CurCustomTab == ASSETS_TAB_GUI_CURSOR)\n\t{\n\t\tSearchListSize = gs_vpSearchGuiCursorList.size();\n\t\tTextureWidth = 96;\n\t\tTextureHeight = 96;"), std::string::npos);
	EXPECT_NE(Source.find("else if(s_CurCustomTab == ASSETS_TAB_ARROW)\n\t{\n\t\tSearchListSize = gs_vpSearchArrowList.size();\n\t\tTextureWidth = 96;\n\t\tTextureHeight = 96;"), std::string::npos);
}

TEST(AssetsPreviewScale, WorkshopAndLocalCardsUseSharedPreviewContentSizing)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("auto ComputeAssetPreviewContentSize = [&](bool WorkshopCard)"), std::string::npos);
	EXPECT_NE(Source.find("const float TileContentSize = WorkshopCard ? 112.0f : 104.0f;"), std::string::npos);
	EXPECT_NE(Source.find("ContentWidth = 76.0f;"), std::string::npos);
	EXPECT_NE(Source.find("ResolveLocalAssetStatusLabel(pItem, ShowLocalOnlyBadge)"), std::string::npos);
	EXPECT_NE(Source.find("LayoutAssetsCardShell(CardRect, HasDeleteButton, pLocalStatusLabel, ShowLocalOnlyBadge, ShowAuthorRow)"), std::string::npos);
	EXPECT_NE(Source.find("TitleProps.m_StopAtEnd = true;"), std::string::npos);
	EXPECT_NE(Source.find("TitleProps.m_EllipsisAtEnd = true;"), std::string::npos);
	EXPECT_NE(Source.find("AuthorProps.m_StopAtEnd = true;"), std::string::npos);
	EXPECT_NE(Source.find("AuthorProps.m_EllipsisAtEnd = true;"), std::string::npos);
}

TEST(AssetsPreviewScale, PreviewFrameAppliesInnerContentInset)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("PreviewFrame.Margin(3.0f, &PreviewFrame);"), std::string::npos);
	EXPECT_NE(Source.find("if(s_CurCustomTab != ASSETS_TAB_GAME && s_CurCustomTab != ASSETS_TAB_STRONG_WEAK)\n\t\t\tPreviewFrame.Margin(8.0f, &PreviewFrame);"), std::string::npos);
}

TEST(AssetsPreviewScale, WorkshopRootFolderUsesPinkAccent)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("const bool IsWorkshopRootFolder = IsEntityBgDirectory &&"), std::string::npos);
	EXPECT_NE(Source.find("TextRender()->TextColor(ColorRGBA(1.0f, 0.78f, 0.78f, 1.0f));"), std::string::npos);
	EXPECT_NE(Source.find("IsEntityBgWorkshopFolderPath(pItem->m_aName);"), std::string::npos);
}

TEST(AssetsPreviewScale, StatusTagTextUsesSingleLineShrinkBeforeWrapping)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("StatusLabelProps.m_StopAtEnd = true;"), std::string::npos);
	EXPECT_NE(Source.find("StatusLabelProps.m_EllipsisAtEnd = true;"), std::string::npos);
}

TEST(AssetsPreviewScale, AssetCardHeaderPrioritizesRightSideControls)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("constexpr float AssetCardHeaderMargin = 3.0f;"), std::string::npos);
	EXPECT_NE(Source.find("constexpr float AssetCardHeaderControlMargin = 1.0f;"), std::string::npos);
	EXPECT_NE(Source.find("TitleRect.VSplitRight(16.0f, &TitleRect, &Shell.m_ActionButtonRect);"), std::string::npos);
	EXPECT_NE(Source.find("const float BadgeGap = 2.0f;"), std::string::npos);
	EXPECT_NE(Source.find("const float TitleMinWidth = 12.0f;"), std::string::npos);
	EXPECT_NE(Source.find("return pWorkshopAsset != nullptr ? Localize(\"Downloaded\") : Localize(\"Local\");"), std::string::npos);
}

TEST(AssetsPreviewScale, LocalAssetCardsUsePersistedAuthorMetadata)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	std::ifstream HelperFile(TestSourcePath("src/game/client/components/assets_author_persistence.h"));
	ASSERT_TRUE(File.good());
	ASSERT_TRUE(HelperFile.good());
	std::stringstream Buffer;
	std::stringstream HelperBuffer;
	Buffer << File.rdbuf();
	HelperBuffer << HelperFile.rdbuf();
	const std::string Source = Buffer.str();
	const std::string HelperSource = HelperBuffer.str();

	EXPECT_NE(HelperSource.find("qmclient/workshop/local_asset_authors.json"), std::string::npos);
	EXPECT_NE(Source.find("FindPersistedLocalAssetAuthor("), std::string::npos);
	EXPECT_NE(Source.find("pItem->m_aAuthor[0] != '\\0' ? pItem->m_aAuthor : \"--\""), std::string::npos);
	EXPECT_NE(Source.find("\\\"content_hash\\\":\\\""), std::string::npos);
	EXPECT_NE(Source.find("TryGetLocalAssetContentHash("), std::string::npos);
	EXPECT_EQ(Source.find("else if(TryGetLocalAssetContentHash(pStorage, Tab, pLocalName, CurrentContentHash))"), std::string::npos);
	EXPECT_NE(Source.find("const char *pAuthor = pAsset->m_aAuthor;"), std::string::npos);
	EXPECT_NE(Source.find("PopulateLocalAssetAuthor(Item, ASSETS_TAB_ENTITY_BG, Storage());"), std::string::npos);
	EXPECT_NE(Source.find("Asset.m_Installed = true;\n\t\t\t\t\tPersistLocalAssetAuthorForWorkshopAsset(s_CurCustomTab, Asset, Storage());"), std::string::npos);
	EXPECT_NE(Source.find("FlushPersistedLocalAssetAuthorsIfDirty(Storage(), s_CurCustomTab);"), std::string::npos);
	EXPECT_NE(Source.find("\\\"tab\\\":\\\""), std::string::npos);
	EXPECT_NE(Source.find("SupportsPersistedLocalAssetAuthor(Tab)"), std::string::npos);
	EXPECT_NE(Source.find("m_vContentHashByKey"), std::string::npos);
	EXPECT_EQ(Source.find("PersistLocalAssetAuthor(Tab, Item.m_aName, pWorkshopAuthor, pStorage);"), std::string::npos);
	EXPECT_EQ(Source.find("FindWorkshopAuthorByLocalName(pWorkshopState, Item.m_aName)"), std::string::npos);
	EXPECT_EQ(Source.find("\"modified\":"), std::string::npos);
}

TEST(AssetsPreviewScale, WorkshopMergeDoesNotReappendExistingUninstalledAssets)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("ExistingAsset = std::move(NewAsset);"), std::string::npos);
	EXPECT_NE(Source.find("continue;"), std::string::npos);
	EXPECT_NE(Source.find("WorkshopState.m_vAssets.push_back(std::move(NewAsset));"), std::string::npos);
}

TEST(AssetsPreviewScale, InstalledWorkshopEntityBgThumbTakeoverSkipsLocalDecodeFallback)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("QueueWorkshopReadyThumb(State, *pAsset, CurTab);\n\t\t\treturn true;"), std::string::npos);
	EXPECT_NE(Source.find("QueueWorkshopDecodeThumb(State, *pAsset, CurTab);\n\t\t\treturn true;"), std::string::npos);
	EXPECT_NE(Source.find("if(pAsset->m_pThumbTask)\n\t\t\treturn true;"), std::string::npos);
	EXPECT_NE(Source.find("++ThumbStartsThisFrame;\n\t\tQueueWorkshopDecodeThumb(State, *pAsset, CurTab);\n\t\treturn true;"), std::string::npos);
}

TEST(AssetsPreviewScale, InstalledWorkshopEntityBgWithoutThumbCacheFallsBackToRemotePreview)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("const bool HasUsableInstalledThumb = WorkshopAssetCanDecodePreviewFromInstall(*pAsset, CurTab);"), std::string::npos);
	EXPECT_NE(Source.find("return StartWorkshopRemoteThumbRequest(*pAsset, CurTab, PreviewEpoch, TargetTextureSize, ThumbStartsThisFrame, pStorage, pHttp);"), std::string::npos);
	EXPECT_NE(Source.find("const bool HasUsableInstalledThumb = WorkshopAssetCanDecodePreviewFromInstall(Asset, s_CurCustomTab);"), std::string::npos);
	EXPECT_NE(Source.find("if(!StartWorkshopRemoteThumbRequest(Asset, s_CurCustomTab, PreviewEpoch, TargetTextureSize, WorkshopThumbStartsThisFrame, Storage(), Http()))"), std::string::npos);
}
