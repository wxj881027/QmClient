#include <game/client/components/qmclient/browser_map_preview_loader.h>

#include <gtest/gtest.h>
#include <test/test.h>

class QmBrowserMapPreviewLoader : public ::testing::Test
{
protected:
	CTestInfo m_Info;
	std::unique_ptr<IStorage> m_pStorage;

	void SetUp() override
	{
		m_pStorage = m_Info.CreateTestStorage();
		ASSERT_NE(m_pStorage, nullptr);
		ASSERT_TRUE(m_pStorage->CreateFolder("maps", IStorage::TYPE_SAVE));
		ASSERT_TRUE(m_pStorage->CreateFolder("downloadedmaps", IStorage::TYPE_SAVE));
	}
	void CopyMap(const char *pTarget)
	{
		void *pData = nullptr;
		unsigned Size = 0;
		ASSERT_TRUE(m_pStorage->ReadFile("maps/dm1.map", IStorage::TYPE_ALL, &pData, &Size));
		ASSERT_GT(Size, 0u);
		IOHANDLE File = m_pStorage->OpenFile(pTarget, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr);
		EXPECT_EQ(io_write(File, pData, Size), Size);
		free(pData);
		EXPECT_EQ(io_close(File), 0);
	}
	void Run(const std::shared_ptr<CQmBrowserMapPreviewLoadJob> &pJob)
	{
		CJobPool Pool;
		Pool.Init(1);
		Pool.Add(pJob);
		Pool.Shutdown();
		ASSERT_EQ(pJob->State(), IJob::STATE_DONE);
	}
};

TEST_F(QmBrowserMapPreviewLoader, WorkerPublishesIndependentMapLayersWithoutGraphics)
{
	CopyMap("maps/preview.map");
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pJob);
	ASSERT_TRUE(pJob->m_Success);
	ASSERT_NE(pJob->m_pLayers->GameLayer(), nullptr);
	EXPECT_GT(pJob->m_pLayers->GameLayer()->m_Width, 0);
	EXPECT_EQ(pJob->m_pLayers->Map(), pJob->m_pMap.get());
	EXPECT_TRUE(pJob->m_Start.has_value());
	EXPECT_EQ(pJob->m_vImages.size(), pJob->m_vUsage.size());
	EXPECT_TRUE(std::any_of(pJob->m_vImages.begin(), pJob->m_vImages.end(), [](const CImageInfo &Image) { return Image.m_pData != nullptr; }));
}

TEST_F(QmBrowserMapPreviewLoader, ExplicitCurrentPathTakesPriorityOverNamedMap)
{
	CopyMap("downloadedmaps/current_12345678.map");
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "missing-name", "downloadedmaps/current_12345678.map", false);
	Run(pJob);
	ASSERT_TRUE(pJob->m_Success);
	EXPECT_NE(pJob->m_pLayers->GameLayer(), nullptr);
}

TEST_F(QmBrowserMapPreviewLoader, MissingMapCanBeRetriedAfterDownload)
{
	auto pMissing = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pMissing);
	EXPECT_FALSE(pMissing->m_Success);
	CopyMap("downloadedmaps/preview_12345678.map");
	auto pRetry = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pRetry);
	EXPECT_TRUE(pRetry->m_Success);
}

TEST_F(QmBrowserMapPreviewLoader, CancelledCacheEntryDoesNotPublishMap)
{
	CopyMap("maps/preview.map");
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	pJob->m_Cancelled.store(true);
	Run(pJob);
	EXPECT_FALSE(pJob->m_Success);
	EXPECT_EQ(pJob->m_pLayers, nullptr);
}

TEST_F(QmBrowserMapPreviewLoader, PrefixCollisionDoesNotLoadAnotherMap)
{
	CopyMap("downloadedmaps/previewExtra_12345678.map");
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pJob);
	EXPECT_FALSE(pJob->m_Success);
}

TEST_F(QmBrowserMapPreviewLoader, InvalidMapDoesNotPublishLayers)
{
	IOHANDLE File = m_pStorage->OpenFile("maps/broken.map", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	io_write(File, "invalid", 7);
	io_close(File);
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "broken", "", false);
	Run(pJob);
	EXPECT_FALSE(pJob->m_Success);
	EXPECT_EQ(pJob->m_pLayers, nullptr);
}
