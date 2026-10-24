#include <base/str.h>

#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/editor/mapitems/load_resources.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <climits>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <vector>

namespace
{
	class EditorMapResources : public ::testing::Test
	{
	protected:
		struct SImage
		{
			int m_Version = 2;
			size_t m_Size = sizeof(CMapItemImage_v2);
			int m_External = 0;
			int m_MustBe1 = 1;
			int m_Width = 1;
		};
		struct SItems
		{
			std::vector<SImage> m_vImages{{}};
			int m_QuadVersion = 2;
			size_t m_QuadSize = sizeof(CMapItemLayerQuads);
			int m_QuadCount = 1;
			bool m_QuadReferenced = true;
			int m_QuadGroupVersion = 3;
			bool m_InvalidQuadData = false;
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
		// 真实游戏层通过共享加载校验；仅改变编辑器会读取的资源条目。
		void WriteMap(const SItems &Items)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "editor_resources.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);

			for(size_t i = 0; i < Items.m_vImages.size(); ++i)
			{
				const auto &Source = Items.m_vImages[i];
				CMapItemImage_v2 Image{};
				Image.m_Version = Source.m_Version;
				Image.m_Width = Source.m_Width;
				Image.m_Height = 1;
				Image.m_External = Source.m_External;
				Image.m_MustBe1 = Source.m_MustBe1;
				char aName[32];
				str_format(aName, sizeof(aName), "image%d", static_cast<int>(i));
				Image.m_ImageName = Writer.AddDataString(aName);
				const std::vector<uint8_t> vPixels(static_cast<size_t>(Source.m_Width) * 4, 73);
				Image.m_ImageData = Source.m_External ? -1 : Writer.AddData(vPixels.size(), vPixels.data());
				Writer.AddItem(MAPITEMTYPE_IMAGE, static_cast<int>(i), Source.m_Size, &Image);
			}

			CQuad Quad{};
			Quad.m_aPoints[0].x = 123;
			CMapItemLayerQuads Quads{};
			Quads.m_Layer.m_Type = LAYERTYPE_QUADS;
			Quads.m_Version = Items.m_QuadVersion;
			Quads.m_NumQuads = Items.m_QuadCount;
			Quads.m_Data = Writer.AddDataSwapped(sizeof(Quad), &Quad);
			Quads.m_Image = Items.m_vImages.empty() ? -1 : static_cast<int>(Items.m_vImages.size()) - 1;
			if(Items.m_InvalidQuadData)
				Quads.m_Data = INT_MAX;
			StrToInts(Quads.m_aName, std::size(Quads.m_aName), "Named");
			Writer.AddItem(MAPITEMTYPE_LAYER, 0, Items.m_QuadSize, &Quads);

			const std::array<CTile, 4> aTiles{};
			CMapItemLayerTilemap Game{};
			Game.m_Layer.m_Type = LAYERTYPE_TILES;
			Game.m_Version = 4;
			Game.m_Width = Game.m_Height = 2;
			Game.m_Flags = TILESLAYERFLAG_GAME;
			Game.m_Color = CColor(255, 255, 255, 255);
			Game.m_ColorEnv = -1;
			Game.m_Image = -1;
			StrToInts(Game.m_aName, std::size(Game.m_aName), "Game");
			Game.m_Data = Writer.AddData(aTiles.size() * sizeof(CTile), aTiles.data());
			Game.m_Tele = Game.m_Speedup = Game.m_Front = Game.m_Switch = Game.m_Tune = -1;
			Writer.AddItem(MAPITEMTYPE_LAYER, 1, sizeof(Game), &Game);

			CMapItemGroup Decoration{};
			Decoration.m_Version = Items.m_QuadGroupVersion;
			Decoration.m_ParallaxX = Decoration.m_ParallaxY = 100;
			Decoration.m_NumLayers = Items.m_QuadReferenced ? 1 : 0;
			Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Decoration), &Decoration);
			CMapItemGroup GameGroup{};
			GameGroup.m_Version = 3;
			GameGroup.m_ParallaxX = GameGroup.m_ParallaxY = 100;
			GameGroup.m_StartLayer = 1;
			GameGroup.m_NumLayers = 1;
			Writer.AddItem(MAPITEMTYPE_GROUP, 1, sizeof(GameGroup), &GameGroup);
			Writer.Finish();
		}
	};
}

TEST_F(EditorMapResources, UnloadedMapCannotPublishResourceItems)
{
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, MapWithoutImagesStillPreparesResources)
{
	SItems Items;
	Items.m_vImages.clear();
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	EXPECT_TRUE(Resources->m_vImages.empty());
}

TEST_F(EditorMapResources, LegacyImageBaseRemainsLoadableWithoutVersionTwoTail)
{
	SItems Items;
	Items.m_vImages[0].m_Version = 1;
	Items.m_vImages[0].m_Size = sizeof(CMapItemImage_v1);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vImages.size(), 1u);
	EXPECT_EQ(Resources->m_vImages[0].m_Version, 1);
	EXPECT_EQ(Resources->m_vImages[0].m_MustBe1, 1);
}

TEST_F(EditorMapResources, ExternalImageDoesNotRequireEmbeddedPixelDataDuringPreparation)
{
	SItems Items;
	Items.m_vImages[0].m_External = 1;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vImages.size(), 1u);
	EXPECT_EQ(Resources->m_vImages[0].m_External, 1);
	EXPECT_EQ(Resources->m_vImages[0].m_ImageData, -1);
}

TEST_F(EditorMapResources, UnsupportedImageTypeKeepsItsSlotAndOriginalQuadImageIndex)
{
	SItems Items;
	Items.m_vImages[0].m_MustBe1 = 0;
	Items.m_vImages.push_back({});
	Items.m_vImages[1].m_Width = 2;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vImages.size(), 2u);
	EXPECT_EQ(Resources->m_vImages[0].m_MustBe1, 0);
	EXPECT_EQ(Resources->m_vImages[0].m_Width, 1);
	EXPECT_EQ(Resources->m_vImages[1].m_Width, 2);
	EXPECT_STREQ(m_pMap->GetDataString(Resources->m_vImages[1].m_ImageName), "image1");
	const auto *pQuads = MapRenderItems::QuadLayer(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_LAYER, 0));
	ASSERT_NE(pQuads, nullptr);
	EXPECT_EQ(pQuads->m_Image, 1);
}

TEST_F(EditorMapResources, CompleteNewerImageVersionRetainsExistingCompatibility)
{
	SItems Items;
	Items.m_vImages[0].m_Version = 3;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, ShortImageBaseIsRejectedAfterSharedMapValidation)
{
	SItems Items;
	Items.m_vImages[0].m_Size = sizeof(int);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, VersionTwoImageWithoutItsExtensionIsRejected)
{
	SItems Items;
	Items.m_vImages[0].m_Size = sizeof(CMapItemImage_v1);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, LaterBrokenImageDoesNotPublishPartialResourceItems)
{
	SItems Items;
	Items.m_vImages.push_back({});
	Items.m_vImages[1].m_Size = sizeof(int);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, CorrectedImageCanBePreparedAfterRejectedCandidate)
{
	SItems Items;
	Items.m_vImages[0].m_Size = sizeof(CMapItemImage_v1);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	ASSERT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
	m_pMap->Unload();
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, PreparedImageMetadataRemainsOwnedAfterCandidateUnload)
{
	SItems Items;
	Items.m_vImages.push_back({});
	Items.m_vImages[1].m_Width = 2;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	m_pMap->Unload();
	ASSERT_EQ(Resources->m_vImages.size(), 2u);
	EXPECT_EQ(Resources->m_vImages[0].m_Width, 1);
	EXPECT_EQ(Resources->m_vImages[1].m_Width, 2);
}

TEST_F(EditorMapResources, LegacyQuadWithoutNameStillPreparesResources)
{
	SItems Items;
	Items.m_QuadVersion = 1;
	Items.m_QuadSize = offsetof(CMapItemLayerQuads, m_aName);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, CompleteVersionTwoQuadNameStillPreparesResources)
{
	WriteMap({});
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, CompleteNewerQuadVersionRetainsExistingCompatibility)
{
	SItems Items;
	Items.m_QuadVersion = 3;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, ShortQuadFixedHeaderIsRejectedBeforeImport)
{
	SItems Items;
	Items.m_QuadSize = sizeof(CMapItemLayer);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, VersionTwoQuadWithoutNameTailIsRejected)
{
	SItems Items;
	Items.m_QuadSize = offsetof(CMapItemLayerQuads, m_aName);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, VersionTwoQuadMissingLastNameIntegerIsRejected)
{
	SItems Items;
	Items.m_QuadSize = sizeof(CMapItemLayerQuads) - sizeof(int);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, UnreferencedShortQuadIsNotImportedOrRejected)
{
	SItems Items;
	Items.m_QuadReferenced = false;
	Items.m_QuadSize = sizeof(CMapItemLayer);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, QuadInUnsupportedGroupRetainsExistingSkipBehavior)
{
	SItems Items;
	Items.m_QuadGroupVersion = 4;
	Items.m_QuadSize = sizeof(CMapItemLayer);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapResources, InvalidQuadDataKeepsExistingRecoverableCopyFailure)
{
	SItems Items;
	Items.m_InvalidQuadData = true;
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
	const auto *pQuads = MapRenderItems::QuadLayer(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_LAYER, 0));
	ASSERT_NE(pQuads, nullptr);
	EXPECT_TRUE(MapRenderItems::QuadData(*m_pMap, *pQuads).empty());
}

TEST_F(EditorMapResources, WrappingQuadByteCountCannotExposeDataForCopy)
{
	SItems Items;
	// 该数量的字节乘积在 32 位 size_t 中会回绕到一份四边形以内。
	Items.m_QuadCount = static_cast<int>(std::numeric_limits<uint32_t>::max() / sizeof(CQuad) + 1);
	WriteMap(Items);
	ASSERT_TRUE(m_pMap->Load("editor_resources.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
	const auto *pQuads = MapRenderItems::QuadLayer(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_LAYER, 0));
	ASSERT_NE(pQuads, nullptr);
	EXPECT_TRUE(MapRenderItems::QuadData(*m_pMap, *pQuads).empty());
}
