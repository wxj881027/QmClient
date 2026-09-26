// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

TEST(QmMonitoringAssetsContract, SettingsResourcePreviewUploadBudgetExhaustionRequeuesWithoutFailure)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.cpp");
	const std::string DrainOneBody = ExtractSourceFunctionBody(Source, "bool CSettingsResourcePreviewUploadScheduler::DrainOne(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics)");
	ASSERT_FALSE(DrainOneBody.empty());

	const size_t BudgetExhaustedPos = DrainOneBody.find("!SettingsResourcePreviewConsumeUploadBudget(Budget)");
	const size_t UploadPos = DrainOneBody.find("pGraphics->LoadTextureRawMove");
	ASSERT_NE(BudgetExhaustedPos, std::string::npos);
	ASSERT_NE(UploadPos, std::string::npos);
	ASSERT_LT(BudgetExhaustedPos, UploadPos);
	const std::string BudgetExhaustedBody = DrainOneBody.substr(BudgetExhaustedPos, UploadPos - BudgetExhaustedPos);
	EXPECT_NE(BudgetExhaustedBody.find("m_vUploadQueue.push_front(std::move(Item));"), std::string::npos);
	EXPECT_EQ(BudgetExhaustedBody.find("Item.m_Image.Free()"), std::string::npos);
	EXPECT_EQ(BudgetExhaustedBody.find("Item.m_Finalize(false)"), std::string::npos);
	EXPECT_EQ(BudgetExhaustedBody.find("Cache.MarkPreviewJobDone(Item.m_Key, false)"), std::string::npos);
}

TEST(QmMonitoringAssetsContract, AssetsSharedUploadSchedulerDoesNotRequeueMovedPreviewImages)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	EXPECT_NE(Source.find("DrainSharedResourcePreviewUploadQueue"), std::string::npos);
	EXPECT_NE(Source.find("while(gs_SettingsAssetsResourcePreviewUploadScheduler.QueueDepth() == 0 && !vReadyQueue.empty()"), std::string::npos);
	EXPECT_NE(Source.find("while(gs_SettingsAssetsResourcePreviewUploadScheduler.QueueDepth() == 0 && !WorkshopState.m_vReadyThumbQueue.empty()"), std::string::npos);
	const size_t LocalEnqueue = Source.find("gs_SettingsAssetsResourcePreviewUploadScheduler.EnqueueUploadToTarget(\n\t\t\t\tPreviewKey");
	ASSERT_NE(LocalEnqueue, std::string::npos);
	const size_t LocalFailure = Source.find("if(!gs_SettingsAssetsResourcePreviewUploadScheduler.DrainOne(PreviewUploadBudget", LocalEnqueue);
	ASSERT_NE(LocalFailure, std::string::npos);
	const size_t LocalEnd = Source.find("char aFinalizeUploadExtra", LocalFailure);
	ASSERT_NE(LocalEnd, std::string::npos);
	const std::string LocalFailureBody = Source.substr(LocalFailure, LocalEnd - LocalFailure);
	EXPECT_EQ(LocalFailureBody.find("vReadyQueue.push_front(Handle)"), std::string::npos);
	EXPECT_EQ(LocalFailureBody.find("ResetCustomItemPreviewState(*pItem)"), std::string::npos);
	EXPECT_NE(LocalFailureBody.find("GPU_UPLOAD_BUDGET"), std::string::npos);

	const size_t WorkshopEnqueue = Source.find("gs_SettingsAssetsResourcePreviewUploadScheduler.EnqueueUploadToTarget(\n\t\t\t\t\tPreviewKey", LocalEnd);
	ASSERT_NE(WorkshopEnqueue, std::string::npos);
	const size_t WorkshopFailure = Source.find("if(!gs_SettingsAssetsResourcePreviewUploadScheduler.DrainOne(WorkshopPreviewUploadBudget", WorkshopEnqueue);
	ASSERT_NE(WorkshopFailure, std::string::npos);
	const size_t WorkshopEnd = Source.find("WorkshopThumbUploadedBytesThisFrame", WorkshopFailure);
	ASSERT_NE(WorkshopEnd, std::string::npos);
	const std::string WorkshopFailureBody = Source.substr(WorkshopFailure, WorkshopEnd - WorkshopFailure);
	EXPECT_EQ(WorkshopFailureBody.find("WorkshopState.m_vReadyThumbQueue.push_front"), std::string::npos);
	EXPECT_EQ(WorkshopFailureBody.find("ResetWorkshopThumbReadyState(*pAsset)"), std::string::npos);
	EXPECT_NE(WorkshopFailureBody.find("GPU_UPLOAD_BUDGET"), std::string::npos);
}

TEST(QmMonitoringAssetsContract, SettingsResourcePreviewPipelineExists)
{
	const std::string Header = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.h");
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.cpp");
	const std::string CMake = ReadRepoFile("CMakeLists.txt");

	EXPECT_NE(Header.find("struct SResourcePreviewKey"), std::string::npos);
	EXPECT_NE(Header.find("struct SResourcePreviewState"), std::string::npos);
	EXPECT_NE(Header.find("class CSettingsResourcePreviewCache"), std::string::npos);
	EXPECT_NE(Header.find("class CSettingsResourcePreviewScheduler"), std::string::npos);
	EXPECT_NE(Header.find("class CSettingsResourcePreviewJob"), std::string::npos);
	EXPECT_NE(Header.find("enum class ESettingsResourcePreviewPriority"), std::string::npos);
	EXPECT_NE(Header.find("VISIBLE"), std::string::npos);
	EXPECT_NE(Header.find("NEAR_VISIBLE"), std::string::npos);
	EXPECT_NE(Header.find("BACKGROUND"), std::string::npos);
	EXPECT_NE(Source.find("CSettingsResourcePreviewScheduler::BeginFrame"), std::string::npos);
	const std::string BeginFrameBody = ExtractSourceFunctionBody(Source, "void CSettingsResourcePreviewScheduler::BeginFrame(int VisibleBudget, int NearVisibleBudget, int BackgroundBudget, int UploadBudget)");
	ASSERT_FALSE(BeginFrameBody.empty());
	EXPECT_EQ(BeginFrameBody.find("CanStartPreviewJob("), std::string::npos);
	EXPECT_NE(CMake.find("components/qmclient/settings_resource_preview.cpp"), std::string::npos);
	EXPECT_NE(CMake.find("src/game/client/components/qmclient/settings_resource_preview.cpp"), std::string::npos);
}

TEST(QmMonitoringAssetsContract, TeeListDoesNotExposePartialSkinPreviewUploads)
{
	// The tee list must not render a CSkin while only some sprites have uploaded.
	// Partially uploaded skins looked like broken/default tees in the settings list.
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const CSkin *pSkin = State == CSkins::CSkinContainer::EState::LOADED ? pSkinContainer->Skin().get() : pDefaultSkin;"), std::string::npos);
	EXPECT_EQ(Body.find("const CSkin *pSkin = pSkinContainer->Skin() != nullptr ? pSkinContainer->Skin().get() : pDefaultSkin;"), std::string::npos);
}

TEST(QmMonitoringAssetsContract, SettingsResourcePreviewDrainRejectsInvalidImagesBeforeConsumingBudget)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.cpp");
	const std::string DrainBody = ExtractSourceFunctionBody(Source, "int CSettingsResourcePreviewUploadScheduler::Drain(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics)");
	const std::string DrainOneBody = ExtractSourceFunctionBody(Source, "bool CSettingsResourcePreviewUploadScheduler::DrainOne(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics)");
	ASSERT_FALSE(DrainBody.empty());
	ASSERT_FALSE(DrainOneBody.empty());

	EXPECT_NE(DrainBody.find("DrainOne(Budget, Telemetry, Cache, pGraphics)"), std::string::npos);
	const size_t ValidatePos = DrainOneBody.find("!SettingsResourcePreviewImageValidForUpload(Item.m_Image)");
	const size_t ConsumePos = DrainOneBody.find("SettingsResourcePreviewConsumeUploadBudget(Budget");
	ASSERT_NE(ValidatePos, std::string::npos);
	ASSERT_NE(ConsumePos, std::string::npos);
	EXPECT_LT(ValidatePos, ConsumePos);
	EXPECT_NE(DrainOneBody.find("Cache.MarkUploadFailed(Item.m_Key);"), std::string::npos);
	EXPECT_NE(DrainOneBody.find("Item.m_Finalize(false, IGraphics::CTextureHandle())"), std::string::npos);
	EXPECT_NE(DrainOneBody.find("m_vUploadQueue.push_front(std::move(Item));"), std::string::npos);
}
