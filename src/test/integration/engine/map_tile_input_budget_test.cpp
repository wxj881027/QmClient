#include <engine/shared/map.h>

#include <game/gamecore.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/support/datafile_reader_operations.h>
#include <test/support/datafile_wire_fixture.h>
#include <test/test.h>

#include <array>
#include <limits>
#include <memory>
#include <vector>

namespace
{
	class MapTileInputBudget : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CTestDatafileReaderOperations m_Operations;
		std::unique_ptr<CMap> m_pMap;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pMap = std::make_unique<CMap>(m_pStorage.get());
		}
		void Write(size_t GameBytes = 4 * sizeof(CTile), size_t DecorBytes = 4 * sizeof(CTile), int LayerVersion = 4, unsigned char GameSkip = 0)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "tiles.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			ASSERT_GE(Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version), 0);
			const auto AddLayer = [&](int Id, const char *pName, size_t Bytes) {
				std::vector<unsigned char> vData(Bytes, 0);
				if(Id == 0 && Bytes >= sizeof(CTile))
				{
					CTile First{};
					First.m_Skip = GameSkip;
					mem_copy(vData.data(), &First, sizeof(First));
				}
				CMapItemLayerTilemap Layer{};
				Layer.m_Version = LayerVersion;
				Layer.m_Layer.m_Type = LAYERTYPE_TILES;
				Layer.m_Width = Layer.m_Height = 2;
				Layer.m_Flags = Id == 0 ? TILESLAYERFLAG_GAME : 0;
				Layer.m_Image = Layer.m_ColorEnv = -1;
				Layer.m_Color = CColor(255, 255, 255, 255);
				Layer.m_Tele = Layer.m_Speedup = Layer.m_Front = Layer.m_Switch = Layer.m_Tune = -1;
				Layer.m_Data = Writer.AddData(vData.size(), vData.data());
				ASSERT_EQ(Layer.m_Data, Id);
				StrToInts(Layer.m_aName, std::size(Layer.m_aName), pName);
				ASSERT_GE(Writer.AddItem(MAPITEMTYPE_LAYER, Id, sizeof(Layer), &Layer), 0);
			};
			ASSERT_NO_FATAL_FAILURE(AddLayer(0, "Game", GameBytes));
			ASSERT_NO_FATAL_FAILURE(AddLayer(1, "Decor", DecorBytes));
			CMapItemGroup Group{};
			Group.m_Version = 3;
			Group.m_ParallaxX = Group.m_ParallaxY = 100;
			Group.m_NumLayers = 2;
			StrToInts(Group.m_aName, std::size(Group.m_aName), "Game");
			ASSERT_GE(Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group), 0);
			ASSERT_TRUE(Writer.Finish());
		}
		void DeclareInputSize(int Index, int Size)
		{
			std::array<int, 9> aHeader{};
			ASSERT_NO_FATAL_FAILURE(ReadDatafileFixtureHeader(m_pStorage.get(), "tiles.map", aHeader));
			ASSERT_EQ(aHeader[1], 4);
			ASSERT_LT(Index, aHeader[6]);
			const int SizeOffset = sizeof(aHeader) + aHeader[4] * 3 * sizeof(int) + aHeader[5] * sizeof(int) + (aHeader[6] + Index) * sizeof(int);
			ASSERT_NO_FATAL_FAILURE(PatchDatafileFixtureWord(m_pStorage.get(), "tiles.map", SizeOffset, Size));
		}
		bool Load()
		{
			return m_pMap->Load("tiles.map", IStorage::TYPE_SAVE, nullptr, &m_Operations);
		}
	};
}

TEST_F(MapTileInputBudget, ExactVersionFourRecordsLoadAndDecodeGameAndDecor)
{
	ASSERT_NO_FATAL_FAILURE(Write());
	ASSERT_TRUE(Load());
	for(int Index = 0; Index < 2; ++Index)
	{
		const auto *pTiles = static_cast<const CTile *>(m_pMap->GetData(Index));
		ASSERT_NE(pTiles, nullptr);
		EXPECT_EQ(m_pMap->GetDataSize(Index), 4 * sizeof(CTile));
		EXPECT_EQ(pTiles[3].m_Skip, 0);
	}
	EXPECT_EQ(m_Operations.m_RejectedAllocations, 0);
}

TEST_F(MapTileInputBudget, IncompleteTrailingRecordBytesKeepExistingAcceptance)
{
	for(size_t Tail = 1; Tail < sizeof(CTile); ++Tail)
	{
		SCOPED_TRACE(Tail);
		m_pMap->Unload();
		ASSERT_NO_FATAL_FAILURE(Write(4 * sizeof(CTile) + Tail));
		ASSERT_TRUE(Load());
		ASSERT_NE(m_pMap->GetData(0), nullptr);
		EXPECT_EQ(m_pMap->GetDataSize(0), 4 * sizeof(CTile));
	}
}

TEST_F(MapTileInputBudget, FinalSkipRunKeepsExistingTruncationToTheLayerExtent)
{
	ASSERT_NO_FATAL_FAILURE(Write(sizeof(CTile), 4 * sizeof(CTile), 4, 255));
	ASSERT_TRUE(Load());
	const auto *pTiles = static_cast<const CTile *>(m_pMap->GetData(0));
	ASSERT_NE(pTiles, nullptr);
	EXPECT_EQ(m_pMap->GetDataSize(0), 4 * sizeof(CTile));
	EXPECT_EQ(pTiles[3].m_Skip, 0);
}

TEST_F(MapTileInputBudget, HugeDeclaredGameInputFailsBeforeAnyRawAllocationAndCanRetry)
{
	ASSERT_NO_FATAL_FAILURE(Write());
	ASSERT_NO_FATAL_FAILURE(DeclareInputSize(0, std::numeric_limits<int>::max()));
	EXPECT_FALSE(Load());
	EXPECT_FALSE(m_pMap->IsLoaded());
	EXPECT_EQ(m_Operations.m_Allocations, 0);
	EXPECT_EQ(m_Operations.m_RejectedAllocations, 0);
	ASSERT_NO_FATAL_FAILURE(DeclareInputSize(0, 4 * sizeof(CTile)));
	EXPECT_TRUE(Load());
}

TEST_F(MapTileInputBudget, TooManyGameRecordsFailBeforeRawAllocation)
{
	ASSERT_NO_FATAL_FAILURE(Write(5 * sizeof(CTile)));
	EXPECT_FALSE(Load());
	EXPECT_EQ(m_Operations.m_Allocations, 0);
	EXPECT_EQ(m_Operations.m_RejectedAllocations, 0);
}

TEST_F(MapTileInputBudget, OversizedOptionalInputStillFailsLazilyAfterGamePublishes)
{
	ASSERT_NO_FATAL_FAILURE(Write());
	ASSERT_NO_FATAL_FAILURE(DeclareInputSize(1, std::numeric_limits<int>::max()));
	ASSERT_TRUE(Load());
	const int Reads = m_Operations.m_Reads;
	const int Allocations = m_Operations.m_Allocations;
	EXPECT_EQ(m_pMap->GetData(1), nullptr);
	EXPECT_EQ(m_Operations.m_Reads, Reads);
	EXPECT_EQ(m_Operations.m_Allocations, Allocations);
	EXPECT_EQ(m_Operations.m_RejectedAllocations, 0);
	EXPECT_TRUE(m_pMap->IsLoaded());
	EXPECT_NE(m_pMap->GetData(0), nullptr);
	m_pMap->UnloadData(1);
	EXPECT_EQ(m_pMap->GetData(1), nullptr);
}

TEST_F(MapTileInputBudget, LegacyLayerExtraTilesKeepExistingAcceptance)
{
	ASSERT_NO_FATAL_FAILURE(Write(5 * sizeof(CTile), 4 * sizeof(CTile), 3));
	ASSERT_TRUE(Load());
	EXPECT_NE(m_pMap->GetData(0), nullptr);
	EXPECT_EQ(m_pMap->GetDataSize(0), 5 * sizeof(CTile));
}
