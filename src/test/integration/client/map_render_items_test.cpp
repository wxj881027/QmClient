#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/gamecore.h>
#include <game/layers.h>
#include <game/map/map_render_items.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <climits>
#include <memory>

namespace
{
	class MapRenderItemsFile : public ::testing::Test
	{
	protected:
		struct SItems
		{
			int m_ImageVersion = 1;
			size_t m_ImageSize = sizeof(CMapItemImage_v1);
			size_t m_QuadSize = sizeof(CMapItemLayerQuads);
			int m_QuadCount = 1;
			bool m_InvalidDataIndex = false;
		};
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CMap> m_pMap;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pMap = std::make_unique<CMap>(m_pStorage.get());
		}
		// 有效游戏层通过真实 CMap 校验，只有可选资源条目的形状由测试指定。
		void WriteMap(const SItems &Items)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "render_items.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);

			CMapItemImage_v2 Image{};
			Image.m_Version = Items.m_ImageVersion;
			Image.m_Width = Image.m_Height = 1;
			Image.m_ImageName = Writer.AddDataString("embedded");
			const std::array<uint8_t, 4> aPixel{73, 2, 3, 255};
			Image.m_ImageData = Writer.AddData(aPixel.size(), aPixel.data());
			Image.m_MustBe1 = 1;
			Writer.AddItem(MAPITEMTYPE_IMAGE, 0, Items.m_ImageSize, &Image);

			CQuad Quad{};
			Quad.m_aPoints[0].x = 123;
			CMapItemLayerQuads QuadLayer{};
			QuadLayer.m_Layer.m_Type = LAYERTYPE_QUADS;
			QuadLayer.m_Version = 1;
			QuadLayer.m_NumQuads = Items.m_QuadCount;
			QuadLayer.m_Data = Writer.AddDataSwapped(sizeof(Quad), &Quad);
			if(Items.m_InvalidDataIndex)
				QuadLayer.m_Data = INT_MAX;
			Writer.AddItem(MAPITEMTYPE_LAYER, 0, Items.m_QuadSize, &QuadLayer);

			const std::array<CTile, 4> aTiles{};
			CMapItemLayerTilemap Game{};
			Game.m_Layer.m_Type = LAYERTYPE_TILES;
			Game.m_Version = 4;
			Game.m_Width = Game.m_Height = 2;
			Game.m_Flags = TILESLAYERFLAG_GAME;
			Game.m_Color = CColor(255, 255, 255, 255);
			Game.m_ColorEnv = -1;
			Game.m_Image = -1;
			Game.m_Data = Writer.AddData(aTiles.size() * sizeof(CTile), aTiles.data());
			Game.m_Tele = Game.m_Speedup = Game.m_Front = Game.m_Switch = Game.m_Tune = -1;
			StrToInts(Game.m_aName, std::size(Game.m_aName), "Game");
			Writer.AddItem(MAPITEMTYPE_LAYER, 1, sizeof(Game), &Game);

			CMapItemGroup Group{};
			Group.m_Version = 3;
			Group.m_ParallaxX = Group.m_ParallaxY = 100;
			Group.m_NumLayers = 2;
			Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group);
			Writer.Finish();
		}
	};
}

TEST_F(MapRenderItemsFile, VersionOneImageReadsItsRealNameAndPixelData)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("render_items.map", IStorage::TYPE_SAVE));
	const auto Item = MapRenderItems::ReadImage(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_IMAGE, 0));
	ASSERT_TRUE(Item);
	EXPECT_EQ(Item->m_Version, 1);
	EXPECT_EQ(Item->m_MustBe1, 1);
	EXPECT_STREQ(m_pMap->GetDataString(Item->m_ImageName), "embedded");
	const auto *pPixel = static_cast<const uint8_t *>(m_pMap->GetData(Item->m_ImageData));
	ASSERT_NE(pPixel, nullptr);
	EXPECT_EQ(pPixel[0], 73);
}

TEST_F(MapRenderItemsFile, TruncatedVersionTwoExtensionDoesNotEscapeAsMetadata)
{
	SItems Items;
	Items.m_ImageVersion = 2;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("render_items.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(MapRenderItems::ReadImage(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_IMAGE, 0)));
}

TEST_F(MapRenderItemsFile, TruncatedQuadHeaderIsRejectedAfterMapLoad)
{
	SItems Items;
	Items.m_QuadSize = sizeof(CMapItemLayer);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("render_items.map", IStorage::TYPE_SAVE));
	EXPECT_EQ(MapRenderItems::QuadLayer(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_LAYER, 0)), nullptr);
}

TEST_F(MapRenderItemsFile, LegacyQuadWithoutNameRetainsItsRealData)
{
	SItems Items;
	Items.m_QuadSize = offsetof(CMapItemLayerQuads, m_aName);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("render_items.map", IStorage::TYPE_SAVE));
	CLayers Layers;
	Layers.Init(m_pMap.get(), false);
	const auto *pLayer = MapRenderItems::QuadLayer(*Layers.Map(), m_pMap->FindItemIndex(MAPITEMTYPE_LAYER, 0));
	ASSERT_NE(pLayer, nullptr);
	const auto Data = MapRenderItems::QuadData(*Layers.Map(), *pLayer);
	ASSERT_EQ(Data.size(), 1u);
	EXPECT_EQ(Data[0].m_aPoints[0].x, 123);
}

TEST_F(MapRenderItemsFile, QuadCountBeyondItsRealDataIsRejected)
{
	SItems Items;
	Items.m_QuadCount = 2;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("render_items.map", IStorage::TYPE_SAVE));
	const auto *pLayer = MapRenderItems::QuadLayer(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_LAYER, 0));
	ASSERT_NE(pLayer, nullptr);
	EXPECT_TRUE(MapRenderItems::QuadData(*m_pMap, *pLayer).empty());
}

TEST_F(MapRenderItemsFile, InvalidQuadDataIndexIsRejectedWithoutDereferencing)
{
	SItems Items;
	Items.m_InvalidDataIndex = true;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("render_items.map", IStorage::TYPE_SAVE));
	const auto *pLayer = MapRenderItems::QuadLayer(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_LAYER, 0));
	ASSERT_NE(pLayer, nullptr);
	EXPECT_TRUE(MapRenderItems::QuadData(*m_pMap, *pLayer).empty());
}

TEST_F(MapRenderItemsFile, ImageMetadataSnapshotSurvivesMapUnload)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("render_items.map", IStorage::TYPE_SAVE));
	const auto Item = MapRenderItems::ReadImage(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_IMAGE, 0));
	ASSERT_TRUE(Item);
	m_pMap->Unload();
	EXPECT_EQ(Item->m_Version, 1);
	EXPECT_EQ(Item->m_Width, 1);
	EXPECT_EQ(Item->m_MustBe1, 1);
}
