#include <engine/shared/datafile.h>

#include <game/client/components/qmclient/browser_map_preview_loader.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <cstddef>
#include <optional>

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
	// 保留有效游戏层，仅改变预览要读取的可选条目尺寸。
	void WritePreviewMap(const CMapItemImage_v2 &Image, size_t ImageSize, int TileImage = 0, std::optional<size_t> QuadSize = std::nullopt)
	{
		CDataFileWriter Writer;
		ASSERT_TRUE(Writer.Open(m_pStorage.get(), "maps/preview.map"));
		CMapItemVersion Version{};
		Version.m_Version = 1;
		Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);

		CMapItemImage_v2 ImageItem = Image;
		ImageItem.m_ImageName = Writer.AddDataString("embedded");
		const std::array<uint8_t, 4> aPixel{73, 2, 3, 255};
		ImageItem.m_ImageData = Writer.AddData(aPixel.size(), aPixel.data());
		Writer.AddItem(MAPITEMTYPE_IMAGE, 0, ImageSize, &ImageItem);

		std::array<CTile, 4> aTiles{};
		CMapItemLayerTilemap Tilemap{};
		Tilemap.m_Layer.m_Type = LAYERTYPE_TILES;
		Tilemap.m_Version = 4;
		Tilemap.m_Width = 2;
		Tilemap.m_Height = 2;
		Tilemap.m_Flags = TILESLAYERFLAG_GAME;
		Tilemap.m_Color = CColor(255, 255, 255, 255);
		Tilemap.m_ColorEnv = -1;
		Tilemap.m_Image = TileImage;
		Tilemap.m_Data = Writer.AddData(sizeof(aTiles), aTiles.data());
		Tilemap.m_Tele = Tilemap.m_Speedup = Tilemap.m_Front = Tilemap.m_Switch = Tilemap.m_Tune = -1;
		StrToInts(Tilemap.m_aName, std::size(Tilemap.m_aName), "Game");
		Writer.AddItem(MAPITEMTYPE_LAYER, 0, sizeof(Tilemap), &Tilemap);
		if(QuadSize)
		{
			CQuad Quad{};
			CMapItemLayerQuads Quads{};
			Quads.m_Layer.m_Type = LAYERTYPE_QUADS;
			Quads.m_Version = 1;
			Quads.m_NumQuads = 1;
			Quads.m_Data = Writer.AddDataSwapped(sizeof(Quad), &Quad);
			Quads.m_Image = -1;
			Writer.AddItem(MAPITEMTYPE_LAYER, 1, *QuadSize, &Quads);
		}
		CMapItemGroup Group{};
		Group.m_Version = 3;
		Group.m_ParallaxX = Group.m_ParallaxY = 100;
		Group.m_NumLayers = QuadSize ? 2 : 1;
		Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group);
		Writer.Finish();
	}
	static CMapItemImage_v2 EmbeddedImage(int Version)
	{
		CMapItemImage_v2 Image{};
		Image.m_Version = Version;
		Image.m_Width = Image.m_Height = 1;
		Image.m_MustBe1 = 1;
		return Image;
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

TEST_F(QmBrowserMapPreviewLoader, VersionOneImageWithoutExtensionStillDecodesPixels)
{
	WritePreviewMap(EmbeddedImage(1), sizeof(CMapItemImage_v1));
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pJob);
	ASSERT_TRUE(pJob->m_Success);
	ASSERT_EQ(pJob->m_vImages.size(), 1u);
	ASSERT_NE(pJob->m_vImages[0].m_pData, nullptr);
	EXPECT_EQ(pJob->m_vImages[0].m_pData[0], 73);
}

TEST_F(QmBrowserMapPreviewLoader, VersionTwoImageWithExtensionStillDecodesPixels)
{
	WritePreviewMap(EmbeddedImage(2), sizeof(CMapItemImage_v2));
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pJob);
	ASSERT_TRUE(pJob->m_Success);
	ASSERT_NE(pJob->m_vImages[0].m_pData, nullptr);
	EXPECT_EQ(pJob->m_vImages[0].m_pData[0], 73);
}

TEST_F(QmBrowserMapPreviewLoader, TruncatedImageItemStopsPreviewPublication)
{
	WritePreviewMap(EmbeddedImage(1), sizeof(CMapItemImage_v1) - sizeof(int));
	CMap Map(m_pStorage.get());
	ASSERT_TRUE(Map.Load("maps/preview.map", IStorage::TYPE_SAVE));
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pJob);
	EXPECT_FALSE(pJob->m_Success);
	ASSERT_EQ(pJob->m_vImages.size(), 1u);
	EXPECT_EQ(pJob->m_vImages[0].m_pData, nullptr);
}

TEST_F(QmBrowserMapPreviewLoader, VersionTwoImageMissingExtensionDoesNotBecomeSuccessfulPreview)
{
	WritePreviewMap(EmbeddedImage(2), sizeof(CMapItemImage_v1));
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pJob);
	EXPECT_FALSE(pJob->m_Success);
}

TEST_F(QmBrowserMapPreviewLoader, TruncatedQuadHeaderStopsBeforeDataAndImageFields)
{
	WritePreviewMap(EmbeddedImage(1), sizeof(CMapItemImage_v1), 0, sizeof(CMapItemLayer));
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pJob);
	EXPECT_FALSE(pJob->m_Success);
}

TEST_F(QmBrowserMapPreviewLoader, LegacyQuadWithoutNameKeepsItsPreview)
{
	WritePreviewMap(EmbeddedImage(1), sizeof(CMapItemImage_v1), 0, offsetof(CMapItemLayerQuads, m_aName));
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pJob);
	EXPECT_TRUE(pJob->m_Success);
}

TEST_F(QmBrowserMapPreviewLoader, UnusedTruncatedImageKeepsUntexturedPreviewAvailable)
{
	WritePreviewMap(EmbeddedImage(1), sizeof(int), -1);
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pJob);
	ASSERT_TRUE(pJob->m_Success);
	EXPECT_EQ(pJob->m_vUsage[0], 0);
	EXPECT_EQ(pJob->m_vImages[0].m_pData, nullptr);
}

TEST_F(QmBrowserMapPreviewLoader, UnsupportedCompleteImageTypeKeepsExistingMissingTextureFallback)
{
	auto Image = EmbeddedImage(2);
	Image.m_MustBe1 = 0;
	WritePreviewMap(Image, sizeof(Image));
	auto pJob = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pJob);
	ASSERT_TRUE(pJob->m_Success);
	EXPECT_EQ(pJob->m_vImages[0].m_pData, nullptr);
}

TEST_F(QmBrowserMapPreviewLoader, CorrectedFileCanBeLoadedAfterTruncatedImageFailure)
{
	WritePreviewMap(EmbeddedImage(2), sizeof(CMapItemImage_v1));
	auto pFailed = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pFailed);
	ASSERT_FALSE(pFailed->m_Success);
	pFailed.reset();
	WritePreviewMap(EmbeddedImage(2), sizeof(CMapItemImage_v2));
	auto pRetry = std::make_shared<CQmBrowserMapPreviewLoadJob>(m_pStorage.get(), "preview", "", false);
	Run(pRetry);
	EXPECT_TRUE(pRetry->m_Success);
}
