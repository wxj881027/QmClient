// 设置资源任务测试：按资源任务生命周期独立组织。
#include <game/client/components/assets_preview_scale.h>
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/components/settings_warmup.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

TEST(SettingsResourceJobsAssetsContract, AssetsLocalListFinalizesHeavyPreviewWorkOnlyAfterListEnd)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t LocalListPos = Source.find("if(!UsesCombinedAssetList(pCurrentCategory))");
	ASSERT_NE(LocalListPos, std::string::npos);
	const size_t LocalListEnd = Source.find("auto ResetSelectedAssetToDefault = [&](const char *pDeletedName) {", LocalListPos);
	ASSERT_NE(LocalListEnd, std::string::npos);
	const std::string LocalListBody = Source.substr(LocalListPos, LocalListEnd - LocalListPos);

	const size_t DoEndPos = LocalListBody.find("const int NewSelected = s_ListBox.DoEnd();");
	const size_t ScrollPos = LocalListBody.find("const bool ListScrollActive = QmMenuUiScrollPerfActive(");
	const size_t FrameContextPos = LocalListBody.find("const SSettingsResourceFrameContext PreviewUploadFrameContext = SettingsBuildFrameContext(");
	const size_t ClampPos = LocalListBody.find("RemainingHeavyResourceBatches = SettingsResourceClampSharedHeavyBudget(");
	const size_t FinalizePos = LocalListBody.find("FinalizeReadyPreviewDecodes(PreviewUploadFrameContext);");
	const size_t DrainPos = LocalListBody.find("DrainReadyPreviewUploadsAfterList(PreviewUploadFrameContext);");

	ASSERT_NE(DoEndPos, std::string::npos);
	ASSERT_NE(ScrollPos, std::string::npos);
	ASSERT_NE(FrameContextPos, std::string::npos);
	ASSERT_NE(ClampPos, std::string::npos);
	ASSERT_NE(FinalizePos, std::string::npos);
	ASSERT_NE(DrainPos, std::string::npos);

	EXPECT_LT(DoEndPos, ScrollPos);
	EXPECT_LT(ScrollPos, FrameContextPos);
	EXPECT_LT(FrameContextPos, ClampPos);
	EXPECT_LT(ClampPos, FinalizePos);
	EXPECT_LT(FinalizePos, DrainPos);
}

TEST(SettingsResourceJobsAssetsContract, AssetsWorkshopListFinalizesPreviewAndThumbWorkOnlyAfterListEnd)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	const size_t WorkshopListPos = Source.find("static CListBox s_WorkshopAssetsListBox;");
	ASSERT_NE(WorkshopListPos, std::string::npos);
	const size_t WorkshopListEnd = Source.find("if(DeleteLocalRequested)", WorkshopListPos);
	ASSERT_NE(WorkshopListEnd, std::string::npos);
	const std::string WorkshopListBody = Source.substr(WorkshopListPos, WorkshopListEnd - WorkshopListPos);

	const size_t DoEndPos = WorkshopListBody.find("const int NewCombinedSelected = s_WorkshopAssetsListBox.DoEnd();");
	const size_t ScrollPos = WorkshopListBody.find("const bool WorkshopListScrollActive = QmMenuUiScrollPerfActive(");
	const size_t FrameContextPos = WorkshopListBody.find("const SSettingsResourceFrameContext WorkshopUploadFrameContext = SettingsBuildFrameContext(");
	const size_t ClampPos = WorkshopListBody.find("RemainingHeavyResourceBatches = SettingsResourceClampSharedHeavyBudget(");
	const size_t PreviewFinalizePos = WorkshopListBody.find("FinalizeReadyPreviewDecodes(WorkshopUploadFrameContext);");
	const size_t PreviewDrainPos = WorkshopListBody.find("DrainReadyPreviewUploadsAfterList(WorkshopUploadFrameContext);");
	const size_t ThumbFinalizePos = WorkshopListBody.find("FinalizeWorkshopReadyThumbs(WorkshopUploadFrameContext);");
	const size_t ThumbDrainPos = WorkshopListBody.find("DrainWorkshopReadyThumbUploads(WorkshopUploadFrameContext);");

	ASSERT_NE(DoEndPos, std::string::npos);
	ASSERT_NE(ScrollPos, std::string::npos);
	ASSERT_NE(FrameContextPos, std::string::npos);
	ASSERT_NE(ClampPos, std::string::npos);
	ASSERT_NE(PreviewFinalizePos, std::string::npos);
	ASSERT_NE(PreviewDrainPos, std::string::npos);
	ASSERT_NE(ThumbFinalizePos, std::string::npos);
	ASSERT_NE(ThumbDrainPos, std::string::npos);

	EXPECT_LT(DoEndPos, ScrollPos);
	EXPECT_LT(ScrollPos, FrameContextPos);
	EXPECT_LT(FrameContextPos, ClampPos);
	EXPECT_LT(ClampPos, PreviewFinalizePos);
	EXPECT_LT(PreviewFinalizePos, PreviewDrainPos);
	EXPECT_LT(PreviewDrainPos, ThumbFinalizePos);
	EXPECT_LT(ThumbFinalizePos, ThumbDrainPos);
}

TEST(SettingsResourceJobsAssetsContract, AssetsListsBuildFrameContextFromJumpScrollStateBeforeHeavyStages)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("const bool ListJumpScrollActive ="), std::string::npos);
	EXPECT_NE(Source.find("const bool WorkshopListJumpScrollActive ="), std::string::npos);
	EXPECT_NE(Source.find("s_AssetsScrollCooldownFrames > 0, ListScrollActive, ListJumpScrollActive, s_AssetsPostScrollRecoveryFrames"), std::string::npos);
	EXPECT_NE(Source.find("s_AssetsScrollCooldownFrames > 0, WorkshopListScrollActive, WorkshopListJumpScrollActive, s_AssetsPostScrollRecoveryFrames"), std::string::npos);
	EXPECT_NE(Source.find("frame_context=%s jump_scroll=%d"), std::string::npos);
}

TEST(SettingsResourceJobsAssetsContract, AssetsFocusHandlingDoesNotUseWindowRecoveryFrames)
{
	std::ifstream MenusHeaderFile(TestSourcePath("src/game/client/components/menus.h"));
	ASSERT_TRUE(MenusHeaderFile.good());
	std::stringstream MenusHeaderBuffer;
	MenusHeaderBuffer << MenusHeaderFile.rdbuf();
	const std::string MenusHeaderSource = MenusHeaderBuffer.str();

	EXPECT_EQ(MenusHeaderSource.find("m_LastWindowActive"), std::string::npos);
	EXPECT_EQ(MenusHeaderSource.find("m_WindowRecoveryFrames"), std::string::npos);

	std::ifstream MenusSourceFile(TestSourcePath("src/game/client/components/menus.cpp"));
	ASSERT_TRUE(MenusSourceFile.good());
	std::stringstream MenusBuffer;
	MenusBuffer << MenusSourceFile.rdbuf();
	const std::string MenusSource = MenusBuffer.str();

	EXPECT_EQ(MenusSource.find("m_WindowRecoveryFrames = 10"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_LastWindowActive = CurrentWindowActive"), std::string::npos);
}

TEST(SettingsResourceJobsAssetsContract, AssetsInactiveWindowBehaviorSkipsRecoveryPurgeAndUsesDirectWindowGate)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_EQ(Source.find("Graphics()->UnloadTexture(&Entity.m_RenderTexture);"), std::string::npos);
	EXPECT_EQ(Source.find("EffectiveMaxPreviewUploadsPerFrame = m_WindowRecoveryFrames > 0 ? 0 : MaxPreviewUploadsPerFrame"), std::string::npos);
	EXPECT_EQ(Source.find("EffectiveMaxWorkshopThumbUploadsPerFrame = m_WindowRecoveryFrames > 0 ? 0 : MaxWorkshopThumbUploadsPerFrame"), std::string::npos);
	EXPECT_NE(Source.find("const bool WindowActive = pEngineGraphics == nullptr || pEngineGraphics->WindowActive() != 0;"), std::string::npos);
	EXPECT_NE(Source.find("if(!SettingsAssetWorkAllowedWhileWindowInactive(WindowActive, HighPriority))"), std::string::npos);
	EXPECT_NE(Source.find("if(!SettingsAssetWorkAllowedWhileWindowInactive(WindowActive, Asset.m_ThumbHighPriority))"), std::string::npos);
	EXPECT_NE(Source.find("if(!WindowActive)\n\t\t\treturn;"), std::string::npos);
	EXPECT_NE(Source.find("LogAssetsPerfStageForClient(Client(), \"assets_window_focus\""), std::string::npos);
}

TEST(SettingsResourceJobsAssetsContract, AssetsFocusLogsIncludeTextureMemoryAndResidentPreviewBytes)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("TextureMemoryUsage()"), std::string::npos);
	EXPECT_NE(Source.find("resident_preview_bytes"), std::string::npos);
	EXPECT_NE(Source.find("workshop_resident_preview_bytes"), std::string::npos);
}

TEST(SettingsResourceJobsAssetsContract, EntityBgCorruptInstallProbeReadsOnlyFileHeader)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("IOHANDLE File = pStorage->OpenFile(Asset.m_InstallPath.c_str(), IOFLAG_READ, IStorage::TYPE_SAVE);"), std::string::npos);
	EXPECT_NE(Source.find("unsigned char aHeader[16] = {};"), std::string::npos);
	EXPECT_NE(Source.find("const unsigned BytesRead = io_read(File, aHeader, sizeof(aHeader));"), std::string::npos);
	EXPECT_EQ(Source.find("pStorage->ReadFile(Asset.m_InstallPath.c_str(), IStorage::TYPE_SAVE, &pFileData, &FileSize)"), std::string::npos);
}

TEST(SettingsResourceJobsAssetsContract, AssetsFocusObservationUsesResumeFrameContextAndSwapTelemetry)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("focus_resume=%d"), std::string::npos);
	EXPECT_NE(Source.find("graphics_swap"), std::string::npos);
	EXPECT_NE(Source.find("LogAssetsPerfStageForClient(Client(), \"assets_focus_observation\""), std::string::npos);
}

TEST(SettingsResourceJobsAssetsContract, WorkshopThumbStartAvoidsDuplicateQueuePushForMatchingState)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("Asset.m_ThumbQueuedTier"), std::string::npos);
	EXPECT_NE(Source.find("Asset.m_ThumbQueuedEpoch"), std::string::npos);
	EXPECT_NE(Source.find("Asset.m_ThumbQueuedTab"), std::string::npos);
}

TEST(SettingsResourceJobsAssetsContract, PreviewTierUpgradeReplacesExistingTexturesInsteadOfLeakingOrDropping)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("UnloadTexture(&pItem->m_RenderTexture);"), std::string::npos);
	EXPECT_NE(Source.find("pGraphics->UnloadTexture(&Asset.m_ThumbTexture);"), std::string::npos);
	EXPECT_NE(Source.find("Item.m_PreviewResidentBytes = 0;"), std::string::npos);
	EXPECT_NE(Source.find("SettingsAssetPreviewResidentTextureSatisfiesRequest(\n\t\t\t\t\t\ttrue,\n\t\t\t\t\t\tpAsset->m_ThumbResidentBytes,\n\t\t\t\t\t\tpAsset->m_ThumbRequestedTextureSize)"), std::string::npos);
}

TEST(SettingsResourceJobsAssetsContract, WorkshopRefreshPreservesPreviewRuntimeMetadata)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings_assets.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("NewAsset.m_ThumbRequestedTextureSize = ExistingAsset.m_ThumbRequestedTextureSize;"), std::string::npos);
	EXPECT_NE(Source.find("NewAsset.m_ThumbResidentBytes = ExistingAsset.m_ThumbResidentBytes;"), std::string::npos);
	EXPECT_NE(Source.find("NewAsset.m_ThumbQueuedTier = ExistingAsset.m_ThumbQueuedTier;"), std::string::npos);
	EXPECT_NE(Source.find("NewAsset.m_ThumbQueuedEpoch = ExistingAsset.m_ThumbQueuedEpoch;"), std::string::npos);
	EXPECT_NE(Source.find("NewAsset.m_ThumbQueuedTab = ExistingAsset.m_ThumbQueuedTab;"), std::string::npos);
}
