#include <engine/map.h>
#include <engine/shared/datafile.h>
#include <engine/storage.h>

#include <game/layers.h>
#include <game/map/map_load_task.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/test.h>

namespace
{
	// 两层使用各自的数据块。每行第六格有实体，跳距不能跨过它。
	int WriteLayer(CDataFileWriter &Writer, int Id, int Flags, int Height)
	{
		constexpr int Width = 128;
		std::vector<CTile> vTiles(Width * Height);
		for(int Row = 0; Row < Height; ++Row)
			vTiles[Row * Width + 5].m_Index = TILE_SOLID;

		CMapItemLayerTilemap Layer{};
		Layer.m_Layer.m_Version = 1;
		Layer.m_Layer.m_Type = LAYERTYPE_TILES;
		Layer.m_Version = 3;
		Layer.m_Width = Width;
		Layer.m_Height = Height;
		Layer.m_Flags = Flags;
		Layer.m_Image = -1;
		Layer.m_Data = Writer.AddData(vTiles.size() * sizeof(CTile), vTiles.data());
		Writer.AddItem(MAPITEMTYPE_LAYER, Id, sizeof(Layer), &Layer);
		return Layer.m_Data;
	}
}

TEST(MapDesignTileSkips, LivePreparationKeepsGameTileSkipsAndResumesDesignRows)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage.get(), "layers.map"));
	CMapItemVersion Version{};
	Version.m_Version = 1;
	Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);
	const int GameIndex = WriteLayer(Writer, 0, TILESLAYERFLAG_GAME, 1);
	const int DesignIndex = WriteLayer(Writer, 1, 0, 256);
	CMapItemGroup Group{};
	Group.m_Version = 3;
	Group.m_NumLayers = 2;
	Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group);
	Writer.Finish();

	std::unique_ptr<IEngineMap> pMap(CreateEngineMap(pStorage.get()));
	ASSERT_TRUE(pMap->Load("layers.map", IStorage::TYPE_SAVE));
	CLayers Layers;
	Layers.Init(pMap.get(), false, true, true);
	const auto *pGame = static_cast<const CTile *>(pMap->GetData(GameIndex));
	const auto *pDesign = static_cast<const CTile *>(pMap->GetData(DesignIndex));
	ASSERT_NE(pGame, nullptr);
	ASSERT_NE(pDesign, nullptr);
	EXPECT_EQ(pGame[1].m_Skip, 3);
	EXPECT_EQ(pDesign[1].m_Skip, 0);
	auto Task = Layers.PrepareDesignTileSkips();
	for(int Step = 0; Step < 8 && pDesign[1].m_Skip == 0; ++Step)
		ASSERT_FALSE(Task.RunUntil(CMapLoadTask::TClock::time_point::min()));
	EXPECT_EQ(pDesign[1].m_Skip, 3);
	EXPECT_EQ(pDesign[255 * 128 + 1].m_Skip, 0);
	Task.Finish();
	EXPECT_EQ(pDesign[255 * 128 + 1].m_Skip, 3);
	EXPECT_EQ(pDesign[255 * 128 + 5].m_Index, TILE_SOLID);
}
