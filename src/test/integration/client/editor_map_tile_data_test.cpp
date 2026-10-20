#include <base/str.h>

#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/gamecore.h>
#include <game/map/map_render_items.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <iterator>
#include <memory>
#include <span>
#include <vector>

namespace
{
	class EditorMapTileData : public ::testing::Test
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
		// 保持游戏层有效，由真实 CMap 的惰性处理器校验另一物理层。
		template<typename T>
		void Load(const std::vector<T> &vData, int PhysicsFlag)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "physics_data.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);
			const std::array<CTile, 4> aTiles{};
			CMapItemLayerTilemap Game{};
			Game.m_Layer.m_Type = LAYERTYPE_TILES;
			Game.m_Version = 3;
			Game.m_Width = Game.m_Height = 2;
			Game.m_Flags = TILESLAYERFLAG_GAME;
			Game.m_Color = CColor(255, 255, 255, 255);
			Game.m_ColorEnv = Game.m_Image = -1;
			Game.m_Tele = Game.m_Speedup = Game.m_Front = Game.m_Switch = Game.m_Tune = -1;
			Game.m_Data = Writer.AddData(sizeof(aTiles), aTiles.data());
			StrToInts(Game.m_aName, std::size(Game.m_aName), "Game");
			Writer.AddItem(MAPITEMTYPE_LAYER, 0, sizeof(Game), &Game);
			CMapItemLayerTilemap Physics = Game;
			Physics.m_Flags = PhysicsFlag;
			Physics.m_Data = Writer.AddData(sizeof(aTiles), aTiles.data());
			m_DataIndex = Writer.AddData(vData.size() * sizeof(T), vData.data());
			if(PhysicsFlag == TILESLAYERFLAG_TELE)
			{
				Physics.m_Tele = m_DataIndex;
				StrToInts(Physics.m_aName, std::size(Physics.m_aName), "Tele");
			}
			else
			{
				Physics.m_Speedup = m_DataIndex;
				StrToInts(Physics.m_aName, std::size(Physics.m_aName), "Speedup");
			}
			Writer.AddItem(MAPITEMTYPE_LAYER, 1, sizeof(Physics), &Physics);
			CMapItemGroup Group{};
			Group.m_Version = 3;
			Group.m_ParallaxX = Group.m_ParallaxY = 100;
			Group.m_NumLayers = 2;
			StrToInts(Group.m_aName, std::size(Group.m_aName), "Game");
			Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group);
			Writer.Finish();
			ASSERT_TRUE(m_pMap->Load("physics_data.map", IStorage::TYPE_SAVE));
		}
		std::vector<CSpeedupTile> SpeedupData(size_t Count = 4)
		{
			std::vector<CSpeedupTile> vData(Count);
			for(size_t i = 0; i < Count; ++i)
			{
				vData[i].m_Force = static_cast<unsigned char>(10 + i);
				vData[i].m_MaxSpeed = 20;
				vData[i].m_Type = TILE_BOOST;
				vData[i].m_Angle = -45;
			}
			return vData;
		}
	};
}

TEST_F(EditorMapTileData, ValidSpeedupDataCopiesItsRawValues)
{
	ASSERT_NO_FATAL_FAILURE(Load(SpeedupData(), TILESLAYERFLAG_SPEEDUP));
	std::array<CSpeedupTile, 4> aDestination{};
	ASSERT_TRUE(MapRenderItems::CopyTileData<CSpeedupTile>(*m_pMap, m_DataIndex, aDestination));
	EXPECT_EQ(aDestination[0].m_Force, 10);
	EXPECT_EQ(aDestination[3].m_Force, 13);
	EXPECT_EQ(aDestination[0].m_MaxSpeed, 20);
	EXPECT_EQ(aDestination[0].m_Type, TILE_BOOST);
	EXPECT_EQ(aDestination[0].m_Angle, -45);
}

TEST_F(EditorMapTileData, InvalidSpeedupPaddingDoesNotCopyDespiteSufficientDeclaredSize)
{
	auto vData = SpeedupData();
	vData[1].m_MustBe0 = 1;
	ASSERT_NO_FATAL_FAILURE(Load(vData, TILESLAYERFLAG_SPEEDUP));
	std::array<CSpeedupTile, 4> aDestination{};
	for(auto &Tile : aDestination)
		Tile.m_Force = 77;
	EXPECT_FALSE(MapRenderItems::CopyTileData<CSpeedupTile>(*m_pMap, m_DataIndex, aDestination));
	EXPECT_EQ(m_pMap->GetData(m_DataIndex), nullptr);
	EXPECT_EQ(m_pMap->GetDataSize(m_DataIndex), static_cast<int>(4 * sizeof(CSpeedupTile)));
	for(const auto &Tile : aDestination)
		EXPECT_EQ(Tile.m_Force, 77);
}

TEST_F(EditorMapTileData, CachedDecodeFailureNeverOverwritesTheDestination)
{
	auto vData = SpeedupData();
	vData[1].m_MustBe0 = 1;
	ASSERT_NO_FATAL_FAILURE(Load(vData, TILESLAYERFLAG_SPEEDUP));
	std::array<CSpeedupTile, 4> aDestination{};
	aDestination[0].m_Force = 77;
	ASSERT_FALSE(MapRenderItems::CopyTileData<CSpeedupTile>(*m_pMap, m_DataIndex, aDestination));
	EXPECT_FALSE(MapRenderItems::CopyTileData<CSpeedupTile>(*m_pMap, m_DataIndex, aDestination));
	EXPECT_EQ(aDestination[0].m_Force, 77);
}

TEST_F(EditorMapTileData, ShortSpeedupArrayLeavesTheDestinationUntouched)
{
	ASSERT_NO_FATAL_FAILURE(Load(SpeedupData(1), TILESLAYERFLAG_SPEEDUP));
	std::array<CSpeedupTile, 4> aDestination{};
	aDestination[0].m_Force = 77;
	EXPECT_FALSE(MapRenderItems::CopyTileData<CSpeedupTile>(*m_pMap, m_DataIndex, aDestination));
	EXPECT_EQ(aDestination[0].m_Force, 77);
}

TEST_F(EditorMapTileData, CorrectedCandidateCanCopyAfterARejectedPayload)
{
	auto vData = SpeedupData();
	vData[0].m_MustBe0 = 1;
	ASSERT_NO_FATAL_FAILURE(Load(vData, TILESLAYERFLAG_SPEEDUP));
	std::array<CSpeedupTile, 4> aDestination{};
	ASSERT_FALSE(MapRenderItems::CopyTileData<CSpeedupTile>(*m_pMap, m_DataIndex, aDestination));
	m_pMap->Unload();
	ASSERT_NO_FATAL_FAILURE(Load(SpeedupData(), TILESLAYERFLAG_SPEEDUP));
	ASSERT_TRUE(MapRenderItems::CopyTileData<CSpeedupTile>(*m_pMap, m_DataIndex, aDestination));
	EXPECT_EQ(aDestination[0].m_Force, 10);
}

TEST_F(EditorMapTileData, ExtraStoredRecordsOnlyCopyTheDestinationPrefix)
{
	ASSERT_NO_FATAL_FAILURE(Load(SpeedupData(5), TILESLAYERFLAG_SPEEDUP));
	std::array<CSpeedupTile, 5> aDestination{};
	aDestination[4].m_Force = 77;
	ASSERT_TRUE(MapRenderItems::CopyTileData<CSpeedupTile>(*m_pMap, m_DataIndex, std::span(aDestination).first(4)));
	EXPECT_EQ(aDestination[3].m_Force, 13);
	EXPECT_EQ(aDestination[4].m_Force, 77);
}

TEST_F(EditorMapTileData, ValidTeleDataCopiesItsNumbersAndTypes)
{
	std::vector<CTeleTile> vData(4);
	for(size_t i = 0; i < vData.size(); ++i)
	{
		vData[i].m_Number = static_cast<unsigned char>(i + 1);
		vData[i].m_Type = TILE_TELEIN;
	}
	ASSERT_NO_FATAL_FAILURE(Load(vData, TILESLAYERFLAG_TELE));
	std::array<CTeleTile, 4> aDestination{};
	ASSERT_TRUE(MapRenderItems::CopyTileData<CTeleTile>(*m_pMap, m_DataIndex, aDestination));
	EXPECT_EQ(aDestination[0].m_Number, 1);
	EXPECT_EQ(aDestination[3].m_Number, 4);
	EXPECT_EQ(aDestination[3].m_Type, TILE_TELEIN);
}

TEST_F(EditorMapTileData, ShortTeleArrayLeavesTheDestinationUntouched)
{
	const std::vector<CTeleTile> vData{{1, TILE_TELEIN}};
	ASSERT_NO_FATAL_FAILURE(Load(vData, TILESLAYERFLAG_TELE));
	std::array<CTeleTile, 4> aDestination{};
	aDestination[0].m_Number = 77;
	EXPECT_FALSE(MapRenderItems::CopyTileData<CTeleTile>(*m_pMap, m_DataIndex, aDestination));
	EXPECT_EQ(aDestination[0].m_Number, 77);
}
