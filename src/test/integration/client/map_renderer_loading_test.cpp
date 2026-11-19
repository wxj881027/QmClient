#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/gamecore.h>
#include <game/map/map_renderer.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <iterator>

TEST(MapRendererLoadingIntegration, ExpiredDeadlineYieldsAfterSkippingEmptyGroup)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage.get(), "empty-group.map"));

	CMapItemVersion Version{};
	Version.m_Version = 1;
	Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);

	constexpr int Size = 2;
	CTile aTiles[Size * Size]{};
	aTiles[0].m_Index = TILE_SOLID;
	CMapItemLayerTilemap Game{};
	Game.m_Layer.m_Type = LAYERTYPE_TILES;
	Game.m_Version = 3;
	Game.m_Width = Game.m_Height = Size;
	Game.m_Flags = TILESLAYERFLAG_GAME;
	Game.m_Color = CColor(255, 255, 255, 255);
	Game.m_Image = Game.m_ColorEnv = -1;
	Game.m_Tele = Game.m_Speedup = Game.m_Front = Game.m_Switch = Game.m_Tune = -1;
	Game.m_Data = Writer.AddData(sizeof(aTiles), aTiles);
	StrToInts(Game.m_aName, std::size(Game.m_aName), "Game");
	Writer.AddItem(MAPITEMTYPE_LAYER, 0, sizeof(Game), &Game);

	CMapItemGroup Group{};
	Group.m_Version = 3;
	StrToInts(Group.m_aName, std::size(Group.m_aName), "Empty");
	Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group);
	Group.m_NumLayers = 1;
	StrToInts(Group.m_aName, std::size(Group.m_aName), "Game");
	Writer.AddItem(MAPITEMTYPE_GROUP, 1, sizeof(Group), &Group);
	ASSERT_TRUE(Writer.Finish());

	CMap Map(pStorage.get());
	ASSERT_TRUE(Map.Load("empty-group.map", IStorage::TYPE_SAVE));
	CLayers Layers;
	Layers.Init(&Map, true);

	// 背景在游戏层前结束，此例无需图形或贴图资源。
	CMapRenderer Renderer;
	Renderer.BeginLoad(RENDERTYPE_BACKGROUND, &Layers, nullptr, nullptr);
	Renderer.LoadStep(1, CMapLoadTask::TClock::time_point::min());
	EXPECT_FALSE(Renderer.IsLoadFinished());

	Renderer.LoadStep(1);
	EXPECT_TRUE(Renderer.IsLoadFinished());
}
