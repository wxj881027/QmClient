#include "test.h"

#include <base/io.h>
#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>
#include <game/client/ui/card_registry.h>
#include <game/client/ui/asset_page_resources.h>
#include <game/client/ui/card_ui_model.h>
#include <game/client/ui/resource_page_cache.h>
#include <game/client/ui/resource_page_loader.h>

#include <gtest/gtest.h>

#include <algorithm>

TEST(CardUiModel, AppliesPerPlacementVisibilityAndPageOrder)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "A", {}, "a", {}, {}, ECardOwner::QM, 20}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.b", "B", {}, "b", {}, {}, ECardOwner::QM, 10}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "ui.home", 0, {"qm.b", "qm.a"}}));
	ASSERT_TRUE(Registry.Freeze());
	CCardUiModel Ui(Registry);
	// 页面声明顺序即默认列内顺序。
	ASSERT_EQ(Ui.CardsForPage("home").size(), 2);
	EXPECT_EQ(Ui.CardsForPage("home").front()->m_Id, "qm.b");
	// 隐藏是 (page, card) 级别的偏好，只影响本页投影。
	ASSERT_TRUE(Ui.SetPreferences("home", "qm.b", {false, false}));
	ASSERT_EQ(Ui.CardsForPage("home").size(), 1);
	EXPECT_EQ(Ui.CardsForPage("home").front()->m_Id, "qm.a");
	EXPECT_FALSE(Ui.Preferences("home", "qm.b").m_Visible);
	EXPECT_FALSE(Ui.SetPreferences("home", "qm.missing", {true, false}));
	EXPECT_FALSE(Ui.SetPreferences("other", "qm.a", {true, false}));
}

TEST(CardUiModel, PreferencesArePerPlacementNotGlobal)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "A", {}, "a"}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.a"}}));
	ASSERT_TRUE(Registry.RegisterPage({"other", "Other", 1, {"qm.a"}}));
	ASSERT_TRUE(Registry.Freeze());
	CCardUiModel Ui(Registry);
	ASSERT_TRUE(Ui.SetPreferences("home", "qm.a", {false, true}));
	EXPECT_FALSE(Ui.Preferences("home", "qm.a").m_Visible);
	EXPECT_TRUE(Ui.Preferences("home", "qm.a").m_Collapsed);
	EXPECT_TRUE(Ui.Preferences("other", "qm.a").m_Visible);
	EXPECT_FALSE(Ui.Preferences("other", "qm.a").m_Collapsed);
	EXPECT_EQ(Ui.CardsForPage("other").size(), 1);
	// 同一卡片在多页的放置各自独立导出。
	const SCardUiState State = Ui.ExportState();
	ASSERT_EQ(State.m_vPlacements.size(), 1);
	EXPECT_EQ(State.m_vPlacements.front().m_PageId, "home");
	EXPECT_EQ(State.m_vPlacements.front().m_CardId, "qm.a");
	EXPECT_FALSE(State.m_vPlacements.front().m_Visible);
}

TEST(CardUiModel, UsesDescriptorDefaultsAndRoundTripsState)
{
	CCardRegistry Registry;
	SFeatureModel Feature{"qm.feature", "Feature", true, false};
	ASSERT_TRUE(Registry.RegisterFeature(Feature));
	ASSERT_TRUE(Registry.RegisterCard({"qm.feature.card", "Feature", {}, "icon", Feature.m_Id, {}, ECardOwner::QM, 4, true, "panel", 0, true}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.feature.card"}}));
	ASSERT_TRUE(Registry.Freeze());
	CCardUiModel Ui(Registry);
	// 未设置偏好时回退到 descriptor 默认（可见、折叠）。
	EXPECT_TRUE(Ui.Preferences("home", "qm.feature.card").m_Visible);
	EXPECT_TRUE(Ui.Preferences("home", "qm.feature.card").m_Collapsed);
	// 功能不可用时卡片不投影到页面。
	EXPECT_TRUE(Ui.CardsForPage("home").empty());
	// 折叠覆盖默认（默认折叠=true），可见保持默认。
	ASSERT_TRUE(Ui.SetPreferences("home", "qm.feature.card", {true, false}));
	Feature.m_Available = true;
	EXPECT_EQ(Ui.CardsForPage("home").size(), 1);
	const SCardUiState Exported = Ui.ExportState();
	ASSERT_EQ(Exported.m_vPlacements.size(), 1);
	CCardUiModel Imported(Registry);
	std::string Error;
	ASSERT_TRUE(Imported.ImportState(Exported, Error)) << Error;
	EXPECT_TRUE(Imported.Preferences("home", "qm.feature.card").m_Visible);
	EXPECT_FALSE(Imported.Preferences("home", "qm.feature.card").m_Collapsed);
}

TEST(CardUiModel, FailedImportDoesNotPartiallyModifyState)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "A", {}, "icon"}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.a"}}));
	ASSERT_TRUE(Registry.Freeze());
	CCardUiModel Ui(Registry);
	ASSERT_TRUE(Ui.SetPreferences("home", "qm.a", {true, true}));
	const SCardUiState Before = Ui.ExportState();
	ASSERT_EQ(Before.m_vPlacements.size(), 1);
	SCardUiState Bad = Before;
	Bad.m_vPlacements.push_back({"qm.missing", "home", ECardColumn::FULL, 0, true, true, false});
	std::string Error;
	EXPECT_FALSE(Ui.ImportState(Bad, Error));
	EXPECT_FALSE(Error.empty());
	EXPECT_TRUE(Ui.Preferences("home", "qm.a").m_Collapsed);
	EXPECT_EQ(Ui.ExportState().m_vPlacements.size(), Before.m_vPlacements.size());
}

TEST(CardUiModel, DuplicatePlacementImportIsRejectedWithoutMutation)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "A", {}, "icon"}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.a"}}));
	ASSERT_TRUE(Registry.Freeze());
	CCardUiModel Ui(Registry);
	SCardUiState State;
	State.m_vPlacements.push_back({"qm.a", "home", ECardColumn::FULL, 0, true, true, false});
	State.m_vPlacements.push_back({"qm.a", "home", ECardColumn::FULL, 1, true, true, false});
	std::string Error;
	EXPECT_FALSE(Ui.ImportState(State, Error));
	EXPECT_TRUE(Ui.ExportState().m_vPlacements.empty());
}

TEST(CardUiModel, RelativeMoveRespectsPageAndColumnBoundaries)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "A", {}, "icon"}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.b", "B", {}, "icon"}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.c", "C", {}, "icon"}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.a", "qm.b"}}));
	ASSERT_TRUE(Registry.RegisterPage({"other", "Other", 1, {"qm.c"}}));
	ASSERT_TRUE(Registry.Freeze());
	CCardUiModel Model(Registry);
	ASSERT_TRUE(Model.MoveCardRelative("home", "qm.a", "qm.b", true));
	EXPECT_EQ(Model.OrderModel().Find("home", "qm.a")->m_Order, 1);
	// 目标在另一页：相对移动不跨页。
	EXPECT_FALSE(Model.MoveCardRelative("home", "qm.a", "qm.c", true));
	ASSERT_TRUE(Model.MoveCard("qm.a", "home", "other", ECardColumn::RIGHT, 0));
	EXPECT_FALSE(Model.OrderModel().Find("home", "qm.a")->m_Present);
	ASSERT_TRUE(Model.MoveCardRelative("other", "qm.a", "qm.c", true));
	EXPECT_EQ(Model.OrderModel().Find("other", "qm.a")->m_Column, ECardColumn::FULL);
	EXPECT_EQ(Model.OrderModel().Find("other", "qm.a")->m_Order, 1);
	ASSERT_TRUE(Model.MoveCardRelative("other", "qm.a", "qm.c", false));
	EXPECT_EQ(Model.OrderModel().Find("other", "qm.a")->m_Order, 0);
	const unsigned Revision = Model.Revision();
	EXPECT_FALSE(Model.MoveCardRelative("other", "qm.a", "qm.a", false));
	EXPECT_FALSE(Model.MoveCardRelative("other", "missing", "qm.c", true));
	EXPECT_FALSE(Model.MoveCardRelative("other", "qm.a", "missing", true));
	EXPECT_EQ(Model.Revision(), Revision);
}

TEST(CardUiModel, ResetPageRestoresDeclarationAndClearsPlacementPreferences)
{
	CCardRegistry Registry;
	ASSERT_TRUE(Registry.RegisterCard({"qm.a", "A", {}, "a", {}, {}, ECardOwner::QM, 0, true, "default", 0, false, ECardColumn::LEFT}));
	ASSERT_TRUE(Registry.RegisterCard({"qm.b", "B", {}, "b"}));
	ASSERT_TRUE(Registry.RegisterPage({"home", "Home", 0, {"qm.a", "qm.b"}}));
	ASSERT_TRUE(Registry.Freeze());
	CCardUiModel Ui(Registry);
	ASSERT_TRUE(Ui.MoveCard("qm.a", "home", "home", ECardColumn::RIGHT, 0));
	ASSERT_TRUE(Ui.SetPreferences("home", "qm.a", {false, true}));
	ASSERT_TRUE(Ui.ResetPagePreferences("home"));
	EXPECT_EQ(Ui.OrderModel().Find("home", "qm.a")->m_Column, ECardColumn::LEFT);
	EXPECT_EQ(Ui.OrderModel().Find("home", "qm.a")->m_Order, 0);
	EXPECT_TRUE(Ui.Preferences("home", "qm.a").m_Visible);
	EXPECT_FALSE(Ui.Preferences("home", "qm.a").m_Collapsed);
	EXPECT_TRUE(Ui.ExportState().m_vPlacements.empty());
	// 未知页面被拒绝。
	EXPECT_FALSE(Ui.ResetPagePreferences("missing"));
}

TEST(ResourcePageCache, DeduplicatesLoadsAndRejectsStaleResults)
{
	CResourcePageCache Cache;
	ASSERT_TRUE(Cache.RegisterPage("settings"));
	EXPECT_TRUE(Cache.NeedsLoad("settings", 1));
	ASSERT_TRUE(Cache.BeginLoad("settings", 1));
	const uint64_t Request1 = Cache.Find("settings")->m_RequestId;
	EXPECT_FALSE(Cache.NeedsLoad("settings", 1));
	EXPECT_FALSE(Cache.BeginLoad("settings", 1));
	EXPECT_FALSE(Cache.CompleteLoad("settings", 0, Request1, true));
	ASSERT_TRUE(Cache.CompleteLoad("settings", 1, Request1, true));
	EXPECT_FALSE(Cache.NeedsLoad("settings", 1));
	EXPECT_FALSE(Cache.BeginLoad("settings", 1));
	ASSERT_TRUE(Cache.BeginLoad("settings", 2));
	const uint64_t Request2 = Cache.Find("settings")->m_RequestId;
	EXPECT_NE(Request1, Request2);
	ASSERT_TRUE(Cache.CompleteLoad("settings", 2, Request2, false, "decode failed"));
	ASSERT_NE(Cache.Find("settings"), nullptr);
	EXPECT_EQ(Cache.Find("settings")->m_State, EResourcePageState::FAILED);
	EXPECT_EQ(Cache.Find("settings")->m_Error, "decode failed");
}

TEST(ResourcePageCache, InvalidateStartsNewGeneration)
{
	CResourcePageCache Cache;
	ASSERT_TRUE(Cache.RegisterPage("assets"));
	ASSERT_TRUE(Cache.BeginLoad("assets", 3));
	ASSERT_TRUE(Cache.CompleteLoad("assets", 3, Cache.Find("assets")->m_RequestId, true));
	Cache.Invalidate(4);
	EXPECT_EQ(Cache.Find("assets")->m_State, EResourcePageState::UNLOADED);
	EXPECT_EQ(Cache.Find("assets")->m_Generation, 4u);
}

TEST(ResourcePageLoader, RegistersManifestAndRequiresRuntimeAdapters)
{
	CResourcePageLoader Loader(nullptr, nullptr);
	EXPECT_TRUE(Loader.RegisterManifest({"settings", {{"data/settings.png"}}}));
	EXPECT_FALSE(Loader.RegisterManifest({"settings", {{"data/other.png"}}}));
	EXPECT_FALSE(Loader.RegisterManifest({"broken", {{""}}}));
	EXPECT_FALSE(Loader.RegisterManifest({"absolute", {{"/data/settings.png"}}}));
	EXPECT_FALSE(Loader.RegisterManifest({"traversal", {{"../settings.png"}}}));
	EXPECT_FALSE(Loader.RegisterManifest({"duplicate", {{"settings.png"}, {"settings.png"}}}));
	EXPECT_FALSE(Loader.Request("settings", 1));
	EXPECT_TRUE(Loader.Cache().Find("settings") != nullptr);
	EXPECT_EQ(Loader.Cache().Find("settings")->m_State, EResourcePageState::UNLOADED);
}

TEST(ResourcePageCache, SameGenerationResizeRejectsPreviousRequest)
{
	CResourcePageCache Cache;
	ASSERT_TRUE(Cache.RegisterPage("assets"));
	ASSERT_TRUE(Cache.BeginLoad("assets", 3));
	const uint64_t OldRequest = Cache.Find("assets")->m_RequestId;
	Cache.Invalidate(3);
	ASSERT_TRUE(Cache.BeginLoad("assets", 3));
	const uint64_t NewRequest = Cache.Find("assets")->m_RequestId;
	EXPECT_NE(OldRequest, NewRequest);
	EXPECT_FALSE(Cache.CompleteLoad("assets", 3, OldRequest, true));
	EXPECT_TRUE(Cache.CompleteLoad("assets", 3, NewRequest, true));
}

namespace
{
// 测试显式控制启动时机，仍由真实官方 job pool 执行后台读取和解码。
class CResourceTestEngine final : public IEngine
{
	std::vector<std::shared_ptr<IJob>> m_vJobs;

public:
	void Init() override {}
	void AddJob(std::shared_ptr<IJob> pJob) override { m_vJobs.push_back(std::move(pJob)); }
	void SetAdditionalLogger(std::shared_ptr<ILogger> &&pLogger) override {}
	void ShutdownJobs() override
	{
		if(m_vJobs.empty())
			return;
		CJobPool Pool;
		Pool.Init(1);
		for(auto &pJob : m_vJobs)
			Pool.Add(std::move(pJob));
		m_vJobs.clear();
		Pool.Shutdown();
	}
	~CResourceTestEngine() override { ShutdownJobs(); }
};

class CResourceTestSink final : public IResourcePageSink
{
public:
	int m_Uploads = 0;
	int m_Commits = 0;
	int m_Rollbacks = 0;
	bool m_RejectUpload = false;
	unsigned m_ExpectedWidth = 2;
	unsigned m_ExpectedHeight = 2;
	std::vector<std::string> m_vPaths;

	bool Upload(const char *pPageId, SResourcePageArtifact &&Artifact) override
	{
		EXPECT_STREQ(pPageId, "assets");
		EXPECT_NE(Artifact.m_Image.m_pData, nullptr);
		EXPECT_EQ(Artifact.m_Image.m_Width, m_ExpectedWidth);
		EXPECT_EQ(Artifact.m_Image.m_Height, m_ExpectedHeight);
		++m_Uploads;
		m_vPaths.push_back(Artifact.m_Path);
		Artifact.m_Image.Free();
		return !m_RejectUpload;
	}
	void Finish(const char *pPageId, bool Success) override
	{
		if(Success)
			++m_Commits;
		else
			++m_Rollbacks;
	}
};
}

class CResourceLoaderTest : public ::testing::Test
{
protected:
	CTestInfo m_TestInfo;
	std::unique_ptr<IStorage> m_pStorage;
	CResourceTestEngine m_Engine;
	CResourceTestSink m_Sink;
	std::unique_ptr<CResourcePageLoader> m_pLoader;

	void SetUp() override
	{
		m_TestInfo.m_DeleteTestStorageFilesOnSuccess = true;
		m_pStorage = m_TestInfo.CreateTestStorage();
		ASSERT_NE(m_pStorage, nullptr);
		m_pLoader = std::make_unique<CResourcePageLoader>(&m_Engine, m_pStorage.get());
		for(const char *pPath : {"one.png", "two.png"})
		{
			CImageInfo Image;
			Image.m_Width = 2;
			Image.m_Height = 2;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.AllocateFillZero();
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			const bool Saved = CImageLoader::SavePng(File, pPath, Image);
			ASSERT_TRUE(Saved);
		}
	}

	void TearDown() override
	{
		m_pLoader.reset();
		m_Engine.ShutdownJobs();
		m_pStorage.reset();
	}
};

TEST_F(CResourceLoaderTest, UploadBudgetAndCacheHitUseRealDecodedImages)
{
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"one.png"}, {"two.png"}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	EXPECT_FALSE(m_pLoader->Request("assets", 1));
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 0);
	EXPECT_EQ(m_Sink.m_Uploads, 0);
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink, 0), 0);
	EXPECT_EQ(m_Sink.m_Uploads, 0);
	EXPECT_EQ(m_pLoader->Poll(m_Sink, 1), 0);
	EXPECT_EQ(m_Sink.m_Uploads, 1);
	EXPECT_EQ(m_pLoader->Cache().Find("assets")->m_State, EResourcePageState::LOADING);
	EXPECT_EQ(m_pLoader->Poll(m_Sink, 1), 1);
	EXPECT_EQ(m_Sink.m_Commits, 1);
	EXPECT_EQ(m_Sink.m_vPaths, (std::vector<std::string>{"one.png", "two.png"}));
	EXPECT_EQ(m_pLoader->Cache().Find("assets")->m_State, EResourcePageState::READY);
	EXPECT_FALSE(m_pLoader->Request("assets", 1));
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 0);
	EXPECT_EQ(m_Sink.m_Uploads, 2);
	const auto *pMetrics = m_pLoader->Metrics("assets");
	ASSERT_NE(pMetrics, nullptr);
	EXPECT_EQ(pMetrics->m_LoadAttempts, 1);
	EXPECT_EQ(pMetrics->m_CacheHits, 1);
	EXPECT_EQ(pMetrics->m_DeduplicatedRequests, 1);
	EXPECT_EQ(pMetrics->m_UploadedResources, 2);
	EXPECT_EQ(pMetrics->m_LastDecodedBytes, 32);
	EXPECT_GE(pMetrics->m_PeakDecodedBytes, pMetrics->m_LastDecodedBytes);
	EXPECT_GT(pMetrics->m_LastLoadNanoseconds, 0);
}

TEST_F(CResourceLoaderTest, UploadFailureNeverPublishesReady)
{
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"one.png"}, {"two.png"}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 0);
	m_Sink.m_RejectUpload = true;
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 1);
	EXPECT_EQ(m_Sink.m_Commits, 0);
	EXPECT_EQ(m_Sink.m_Rollbacks, 1);
	EXPECT_EQ(m_pLoader->Cache().Find("assets")->m_State, EResourcePageState::FAILED);
	EXPECT_EQ(m_pLoader->Cache().Find("assets")->m_Error, "resource texture upload failed");
	EXPECT_EQ(m_pLoader->Metrics("assets")->m_FailedLoads, 1);
}

TEST_F(CResourceLoaderTest, ReadFailureRollsBackWithoutUploading)
{
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"one.png"}, {"missing.png"}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 1);
	EXPECT_EQ(m_Sink.m_Uploads, 0);
	EXPECT_EQ(m_Sink.m_Rollbacks, 1);
	EXPECT_EQ(m_pLoader->Cache().Find("assets")->m_State, EResourcePageState::FAILED);
}

TEST_F(CResourceLoaderTest, CancelQueuedRequestAndResizeWithSameGeneration)
{
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"one.png"}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 7));
	const uint64_t OldRequest = m_pLoader->Cache().Find("assets")->m_RequestId;
	m_pLoader->Invalidate(7, m_Sink);
	ASSERT_TRUE(m_pLoader->Request("assets", 7));
	EXPECT_NE(m_pLoader->Cache().Find("assets")->m_RequestId, OldRequest);
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 1);
	EXPECT_EQ(m_Sink.m_Uploads, 1);
	EXPECT_EQ(m_Sink.m_Commits, 1);
	EXPECT_GE(m_pLoader->Metrics("assets")->m_CancelledLoads, 1);
}

TEST_F(CResourceLoaderTest, InvalidateAfterPartialUploadDiscardsRemainder)
{
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"one.png"}, {"two.png"}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 0);
	EXPECT_EQ(m_Sink.m_Uploads, 1);
	m_pLoader->Invalidate(2, m_Sink);
	EXPECT_EQ(m_Sink.m_Rollbacks, 1);
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 0);
	EXPECT_EQ(m_Sink.m_Uploads, 1);
	EXPECT_EQ(m_Sink.m_Commits, 0);
	EXPECT_EQ(m_pLoader->Cache().Find("assets")->m_State, EResourcePageState::UNLOADED);
}

TEST_F(CResourceLoaderTest, ShutdownRejectsFurtherRequestsAndDrainsCancelledJobs)
{
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"one.png"}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	m_pLoader->Shutdown(m_Sink);
	EXPECT_EQ(m_Sink.m_Rollbacks, 1);
	EXPECT_FALSE(m_pLoader->Request("assets", 1));
	EXPECT_FALSE(m_pLoader->RegisterManifest({"late", {{"two.png"}}}));
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 0);
	EXPECT_EQ(m_Sink.m_Uploads, 0);
}

TEST_F(CResourceLoaderTest, RejectsOversizedHeaderBeforeDecoding)
{
	const unsigned char aPng[] = {
		137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
		0, 0, 32, 0, 0, 0, 32, 0, 8, 6, 0, 0, 0, 0, 0, 0, 0};
	IOHANDLE File = m_pStorage->OpenFile("large.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const unsigned Written = io_write(File, aPng, sizeof(aPng));
	const int Closed = io_close(File);
	ASSERT_EQ(Written, sizeof(aPng));
	ASSERT_EQ(Closed, 0);
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"large.png"}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 1);
	EXPECT_EQ(m_Sink.m_Uploads, 0);
	EXPECT_EQ(m_pLoader->Cache().Find("assets")->m_State, EResourcePageState::FAILED);
	EXPECT_EQ(m_pLoader->Cache().Find("assets")->m_Error, "resource PNG header invalid or exceeds limit");
}

TEST_F(CResourceLoaderTest, BoundsQueuedWorkAcrossRepeatedInvalidations)
{
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"one.png"}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	m_pLoader->Invalidate(2, m_Sink);
	ASSERT_TRUE(m_pLoader->Request("assets", 2));
	m_pLoader->Invalidate(3, m_Sink);
	EXPECT_FALSE(m_pLoader->Request("assets", 3));
	EXPECT_EQ(m_pLoader->Metrics("assets")->m_DeduplicatedRequests, 0);
	EXPECT_EQ(m_pLoader->Metrics("assets")->m_CancelledLoads, 2);
	EXPECT_EQ(m_pLoader->Cache().Find("assets")->m_State, EResourcePageState::UNLOADED);
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 0);
	ASSERT_TRUE(m_pLoader->Request("assets", 3));
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 1);
	EXPECT_EQ(m_Sink.m_Uploads, 1);
}

TEST_F(CResourceLoaderTest, MissingAndCorruptPrimaryFallBackUnderOriginalResourceKey)
{
	IOHANDLE File = m_pStorage->OpenFile("corrupt.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const char aInvalid[] = "invalid PNG";
	EXPECT_EQ(io_write(File, aInvalid, sizeof(aInvalid)), sizeof(aInvalid));
	EXPECT_EQ(io_close(File), 0);
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"missing.png", {"corrupt.png", "one.png"}}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 1);
	EXPECT_EQ(m_Sink.m_Commits, 1);
	EXPECT_EQ(m_Sink.m_vPaths, (std::vector<std::string>{"missing.png"}));
}

TEST_F(CResourceLoaderTest, PreviewResizePreservesAspectAndBoundsRetainedMemory)
{
	CImageInfo Image;
	Image.m_Width = 128;
	Image.m_Height = 64;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.AllocateFillZero();
	IOHANDLE File = m_pStorage->OpenFile("wide.png", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	EXPECT_TRUE(CImageLoader::SavePng(File, "wide.png", Image));
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"wide.png", {}, 32}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	m_Engine.ShutdownJobs();
	m_Sink.m_ExpectedWidth = 32;
	m_Sink.m_ExpectedHeight = 16;
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 1);
	EXPECT_EQ(m_pLoader->Metrics("assets")->m_LastDecodedBytes, 32u * 16u * 4u);
}

TEST_F(CResourceLoaderTest, ForgetAllowsSameGenerationReloadWithoutLateCommit)
{
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"one.png"}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	m_pLoader->Forget("assets", 1, m_Sink);
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 1);
	EXPECT_EQ(m_Sink.m_Commits, 1);
	EXPECT_EQ(m_Sink.m_Uploads, 1);
	EXPECT_EQ(m_pLoader->Metrics("assets")->m_CancelledLoads, 1);
}

TEST(AssetPageResources, AllOfficialKindsRegisterWithBoundedFallbacks)
{
	CResourcePageLoader Loader(nullptr, nullptr);
	for(int Index = 0; Index < static_cast<int>(EAssetPageKind::COUNT); ++Index)
	{
		const auto Kind = static_cast<EAssetPageKind>(Index);
		for(const char *pName : {"default", "custom"})
		{
			const auto Spec = QmAssetPreviewSpec(Kind, pName, 256);
			ASSERT_FALSE(Spec.m_PageId.empty());
			EXPECT_TRUE(Loader.RegisterManifest({Spec.m_PageId, {Spec.m_Resource}}));
		}
	}
	const auto Entities = QmAssetPreviewSpec(EAssetPageKind::ENTITIES, "custom", 1024);
	EXPECT_EQ(Entities.m_Resource.m_Path, "assets/entities/custom/ddnet.png");
	ASSERT_EQ(Entities.m_Resource.m_vFallbackPaths.size(), 7);
	EXPECT_EQ(Entities.m_Resource.m_vFallbackPaths.front(), "assets/entities/custom.png");
	EXPECT_EQ(Entities.m_Resource.m_PreviewSize, 512);
	EXPECT_TRUE(QmAssetPreviewSpec(EAssetPageKind::COUNT, "default", 256).m_PageId.empty());
	EXPECT_TRUE(QmAssetPreviewSpec(EAssetPageKind::GAME, nullptr, 256).m_PageId.empty());
}

TEST(AssetPageResources, MissingAdaptersAreVisibleFailureAndShutdownIsIdempotent)
{
	CAssetPageResources Resources(nullptr, nullptr, nullptr);
	EXPECT_EQ(Resources.BeginFrame(EAssetPageKind::GAME).m_State, EAssetListState::FAILED);
	Resources.Shutdown();
	Resources.Shutdown();
	EXPECT_EQ(Resources.BeginFrame(EAssetPageKind::GAME).m_State, EAssetListState::UNLOADED);
}

TEST_F(CResourceLoaderTest, AssetCatalogReopenCacheAndInvalidationRejectOldScan)
{
	CAssetPageResources Resources(&m_Engine, m_pStorage.get(), nullptr);
	ASSERT_TRUE(m_pStorage->CreateFolder("assets", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->CreateFolder("assets/game", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->CreateFolder("assets/game/custom", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->CreateFolder("assets/game/.hidden", IStorage::TYPE_SAVE));
	EXPECT_EQ(Resources.BeginFrame(EAssetPageKind::GAME).m_State, EAssetListState::LOADING);
	Resources.Invalidate();
	m_Engine.ShutdownJobs();
	EXPECT_EQ(Resources.BeginFrame(EAssetPageKind::GAME).m_State, EAssetListState::LOADING);
	m_Engine.ShutdownJobs();
	const auto Snapshot = Resources.BeginFrame(EAssetPageKind::GAME);
	ASSERT_EQ(Snapshot.m_State, EAssetListState::READY);
	ASSERT_NE(Snapshot.m_pNames, nullptr);
	EXPECT_EQ(std::count(Snapshot.m_pNames->begin(), Snapshot.m_pNames->end(), "custom"), 1);
	EXPECT_EQ(std::count(Snapshot.m_pNames->begin(), Snapshot.m_pNames->end(), "default"), 1);
	EXPECT_EQ(std::count(Snapshot.m_pNames->begin(), Snapshot.m_pNames->end(), ".hidden"), 0);
	Resources.EndFrame();
	Resources.EndFrame();
	const auto Reopened = Resources.BeginFrame(EAssetPageKind::GAME);
	EXPECT_EQ(Reopened.m_State, EAssetListState::READY);
	EXPECT_EQ(Reopened.m_Revision, Snapshot.m_Revision);
	Resources.Shutdown();
	m_Engine.ShutdownJobs();
}

TEST_F(CResourceLoaderTest, AssetFailureDoesNotRetryEachFrameAndDpiHasNewRequest)
{
	CAssetPageResources Resources(&m_Engine, m_pStorage.get(), nullptr);
	Resources.BeginFrame(EAssetPageKind::GAME);
	Resources.Preview(EAssetPageKind::GAME, "missing", 64);
	m_Engine.ShutdownJobs();
	Resources.BeginFrame(EAssetPageKind::GAME);
	EXPECT_EQ(Resources.PreviewState(EAssetPageKind::GAME, "missing"), EResourcePageState::FAILED);
	for(int Frame = 0; Frame < 20; ++Frame)
	{
		Resources.Preview(EAssetPageKind::GAME, "missing", 64);
		Resources.EndFrame();
		Resources.BeginFrame(EAssetPageKind::GAME);
	}
	ASSERT_NE(Resources.PreviewMetrics(EAssetPageKind::GAME, "missing", 64), nullptr);
	EXPECT_EQ(Resources.PreviewMetrics(EAssetPageKind::GAME, "missing", 64)->m_LoadAttempts, 1);
	Resources.Preview(EAssetPageKind::GAME, "missing", 128);
	EXPECT_EQ(Resources.PreviewState(EAssetPageKind::GAME, "missing"), EResourcePageState::LOADING);
	Resources.Shutdown();
	m_Engine.ShutdownJobs();
}

TEST_F(CResourceLoaderTest, AssetLruBoundsMetadataAsWellAsTextures)
{
	CAssetPageResources Resources(&m_Engine, m_pStorage.get(), nullptr);
	Resources.BeginFrame(EAssetPageKind::GAME);
	for(int Index = 0; Index < 80; ++Index)
	{
		const std::string Name = "missing-" + std::to_string(Index);
		Resources.Preview(EAssetPageKind::GAME, Name.c_str(), 64);
		m_Engine.ShutdownJobs();
		Resources.EndFrame();
		Resources.BeginFrame(EAssetPageKind::GAME);
		ASSERT_LE(Resources.ResidentCount(), 64u);
		ASSERT_EQ(Resources.ManifestCount(), Resources.ResidentCount());
	}
	EXPECT_EQ(Resources.ResidentCount(), 64u);
	EXPECT_EQ(Resources.PreviewState(EAssetPageKind::GAME, "missing-0"), EResourcePageState::UNLOADED);
	EXPECT_EQ(Resources.PreviewState(EAssetPageKind::GAME, "missing-79"), EResourcePageState::FAILED);
	Resources.Invalidate();
	EXPECT_EQ(Resources.ResidentCount(), 0u);
	EXPECT_EQ(Resources.ManifestCount(), 0u);
}

TEST_F(CResourceLoaderTest, UnregisterAndReRegisterRejectsOldSameNameJob)
{
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"one.png"}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	const auto OldRequest = m_pLoader->Cache().Find("assets")->m_RequestId;
	m_pLoader->UnregisterManifest("assets", 1, m_Sink);
	EXPECT_EQ(m_pLoader->ManifestCount(), 0u);
	ASSERT_TRUE(m_pLoader->RegisterManifest({"assets", {{"two.png"}}}));
	ASSERT_TRUE(m_pLoader->Request("assets", 1));
	EXPECT_NE(m_pLoader->Cache().Find("assets")->m_RequestId, OldRequest);
	m_Engine.ShutdownJobs();
	EXPECT_EQ(m_pLoader->Poll(m_Sink), 1);
	EXPECT_EQ(m_Sink.m_vPaths, (std::vector<std::string>{"two.png"}));
}
