#include <base/str.h>

#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/gamecore.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <iterator>
#include <memory>
#include <span>

namespace
{
	class MapTileskipLoad : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CMap> m_pMap;
		int m_DataIndex = -1;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pMap = std::make_unique<CMap>(m_pStorage.get());
		}

		void WriteMap(const char *pFilename, std::span<const CTile> Data, bool Game = false)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), pFilename));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);
			CMapItemLayerTilemap Layer{};
			Layer.m_Layer.m_Type = LAYERTYPE_TILES;
			Layer.m_Version = 3;
			Layer.m_Width = Layer.m_Height = 2;
			Layer.m_Flags = TILESLAYERFLAG_GAME;
			Layer.m_Color = CColor(255, 255, 255, 255);
			Layer.m_ColorEnv = Layer.m_Image = -1;
			Layer.m_Tele = Layer.m_Speedup = Layer.m_Front = Layer.m_Switch = Layer.m_Tune = -1;
			int LayerCount = 0;
			if(!Game)
			{
				const std::array<CTile, 4> aGame{};
				Layer.m_Data = Writer.AddData(sizeof(aGame), aGame.data());
				StrToInts(Layer.m_aName, std::size(Layer.m_aName), "Game");
				Writer.AddItem(MAPITEMTYPE_LAYER, LayerCount++, sizeof(Layer), &Layer);
				Layer.m_Flags = 0;
			}
			Layer.m_Version = 4;
			m_DataIndex = Layer.m_Data = Writer.AddData(Data.size_bytes(), Data.data());
			StrToInts(Layer.m_aName, std::size(Layer.m_aName), Game ? "Game" : "Tiles");
			Writer.AddItem(MAPITEMTYPE_LAYER, LayerCount++, sizeof(Layer), &Layer);
			CMapItemGroup Group{};
			Group.m_Version = 3;
			Group.m_ParallaxX = Group.m_ParallaxY = 100;
			Group.m_NumLayers = LayerCount;
			StrToInts(Group.m_aName, std::size(Group.m_aName), "Game");
			Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group);
			Writer.Finish();
		}
	};
}

TEST_F(MapTileskipLoad, RegularLayerLazilyExpandsItsStoredRuns)
{
	const std::array<CTile, 2> aSource{{{7, TILEFLAG_XFLIP, 1, 0}, {9, TILEFLAG_YFLIP, 1, 0}}};
	ASSERT_NO_FATAL_FAILURE(WriteMap("regular.map", aSource));
	ASSERT_TRUE(m_pMap->Load("regular.map", IStorage::TYPE_SAVE));
	const auto *pTiles = static_cast<const CTile *>(m_pMap->GetData(m_DataIndex));
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(m_pMap->GetDataSize(m_DataIndex), static_cast<int>(4 * sizeof(CTile)));
	EXPECT_EQ(pTiles[1].m_Index, 7);
	EXPECT_EQ(pTiles[1].m_Flags, TILEFLAG_XFLIP);
	EXPECT_EQ(pTiles[3].m_Index, 9);
	EXPECT_EQ(pTiles[3].m_Flags, TILEFLAG_YFLIP);
	EXPECT_EQ(pTiles[3].m_Skip, 0);
}

TEST_F(MapTileskipLoad, GameLayerExpandsBeforeTheMapIsPublished)
{
	const std::array<CTile, 1> aSource{{{1, 0, 3, 0}}};
	ASSERT_NO_FATAL_FAILURE(WriteMap("game.map", aSource, true));
	ASSERT_TRUE(m_pMap->Load("game.map", IStorage::TYPE_SAVE));
	EXPECT_EQ(m_pMap->GetDataSize(m_DataIndex), static_cast<int>(4 * sizeof(CTile)));
	const auto *pTiles = static_cast<const CTile *>(m_pMap->GetData(m_DataIndex));
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(pTiles[3].m_Index, 1);
}

TEST_F(MapTileskipLoad, TruncatedRegularLayerKeepsItsLazyFailureBehavior)
{
	const std::array<CTile, 1> aSource{{{7, 0, 0, 0}}};
	ASSERT_NO_FATAL_FAILURE(WriteMap("truncated.map", aSource));
	ASSERT_TRUE(m_pMap->Load("truncated.map", IStorage::TYPE_SAVE));
	EXPECT_EQ(m_pMap->GetData(m_DataIndex), nullptr);
	EXPECT_EQ(m_pMap->GetData(m_DataIndex), nullptr);
	EXPECT_TRUE(m_pMap->IsLoaded());
}

TEST_F(MapTileskipLoad, ExtraStoredRunFailsWhenTheLayerIsRead)
{
	const std::array<CTile, 2> aSource{{{7, 0, 3, 0}, {9, 0, 0, 0}}};
	ASSERT_NO_FATAL_FAILURE(WriteMap("extra.map", aSource));
	ASSERT_TRUE(m_pMap->Load("extra.map", IStorage::TYPE_SAVE));
	EXPECT_EQ(m_pMap->GetData(m_DataIndex), nullptr);
}

TEST_F(MapTileskipLoad, NonzeroPaddingFailsWhenTheLayerIsRead)
{
	const std::array<CTile, 2> aSource{{{7, 0, 1, 0}, {9, 0, 1, 1}}};
	ASSERT_NO_FATAL_FAILURE(WriteMap("padding.map", aSource));
	ASSERT_TRUE(m_pMap->Load("padding.map", IStorage::TYPE_SAVE));
	EXPECT_EQ(m_pMap->GetData(m_DataIndex), nullptr);
}

TEST_F(MapTileskipLoad, FinalOversizedRunRetainsItsClippedOutput)
{
	const std::array<CTile, 1> aSource{{{7, TILEFLAG_XFLIP, 255, 0}}};
	ASSERT_NO_FATAL_FAILURE(WriteMap("clipped.map", aSource));
	ASSERT_TRUE(m_pMap->Load("clipped.map", IStorage::TYPE_SAVE));
	const auto *pTiles = static_cast<const CTile *>(m_pMap->GetData(m_DataIndex));
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(m_pMap->GetDataSize(m_DataIndex), static_cast<int>(4 * sizeof(CTile)));
	EXPECT_EQ(pTiles[3].m_Index, 7);
	EXPECT_EQ(pTiles[3].m_Flags, TILEFLAG_XFLIP);
}

TEST_F(MapTileskipLoad, CorrectedMapCanLoadAfterACachedLayerFailure)
{
	const std::array<CTile, 1> aInvalid{{{7, 0, 0, 0}}};
	ASSERT_NO_FATAL_FAILURE(WriteMap("invalid.map", aInvalid));
	ASSERT_TRUE(m_pMap->Load("invalid.map", IStorage::TYPE_SAVE));
	ASSERT_EQ(m_pMap->GetData(m_DataIndex), nullptr);
	const std::array<CTile, 1> aValid{{{9, 0, 3, 0}}};
	ASSERT_NO_FATAL_FAILURE(WriteMap("corrected.map", aValid));
	ASSERT_TRUE(m_pMap->Load("corrected.map", IStorage::TYPE_SAVE));
	const auto *pTiles = static_cast<const CTile *>(m_pMap->GetData(m_DataIndex));
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(pTiles[3].m_Index, 9);
}

TEST_F(MapTileskipLoad, RejectedGameDataPreservesThePreviouslyLoadedMap)
{
	const std::array<CTile, 1> aValid{{{1, 0, 3, 0}}};
	ASSERT_NO_FATAL_FAILURE(WriteMap("valid.map", aValid, true));
	ASSERT_TRUE(m_pMap->Load("valid.map", IStorage::TYPE_SAVE));
	const unsigned PreviousCrc = m_pMap->Crc();
	const int PreviousDataIndex = m_DataIndex;
	const void *pPreviousData = m_pMap->GetData(PreviousDataIndex);
	const std::array<CTile, 1> aInvalid{{{1, 0, 0, 0}}};
	ASSERT_NO_FATAL_FAILURE(WriteMap("invalid.map", aInvalid, true));
	EXPECT_FALSE(m_pMap->Load("invalid.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(m_pMap->IsLoaded());
	EXPECT_EQ(m_pMap->Crc(), PreviousCrc);
	EXPECT_EQ(m_pMap->GetData(PreviousDataIndex), pPreviousData);
}
