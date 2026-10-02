// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <engine/kernel.h>
#include <engine/map.h>
#include <engine/shared/datafile.h>
#include <engine/shared/protocol.h>
#include <engine/storage.h>

#include <game/gamecore.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <vector>

namespace
{
	constexpr int INVALID_DATA_INDEX = 0x7fffffff;

	// 写入一个最小地图；通过参数构造应当被拒绝的畸形地图
	void WriteMap(IStorage *pStorage, const char *pFilename, int Width, int Height, int LayerFlags, int GroupNumLayers, bool TruncateGroup, int DataIndex = -1)
	{
		CDataFileWriter Writer;
		ASSERT_TRUE(Writer.Open(pStorage, pFilename));

		CMapItemVersion Version = {};
		Version.m_Version = 1;
		Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);

		const size_t TileCount = (size_t)maximum(Width, 0) * (size_t)maximum(Height, 0);
		std::vector<CTile> vTiles(TileCount);
		const int RealDataIndex = Writer.AddData(vTiles.size() * sizeof(CTile), vTiles.data());

		CMapItemLayerTilemap Tilemap = {};
		Tilemap.m_Layer.m_Version = 1;
		Tilemap.m_Layer.m_Type = LAYERTYPE_TILES;
		Tilemap.m_Version = 4;
		Tilemap.m_Width = Width;
		Tilemap.m_Height = Height;
		Tilemap.m_Flags = LayerFlags;
		Tilemap.m_Color = CColor(255, 255, 255, 255);
		Tilemap.m_ColorEnv = -1;
		Tilemap.m_Image = -1;
		Tilemap.m_Data = DataIndex == -1 ? RealDataIndex : DataIndex;
		Tilemap.m_Tele = -1;
		Tilemap.m_Speedup = -1;
		Tilemap.m_Front = -1;
		Tilemap.m_Switch = -1;
		Tilemap.m_Tune = -1;
		StrToInts(Tilemap.m_aName, std::size(Tilemap.m_aName), (LayerFlags & TILESLAYERFLAG_GAME) != 0 ? "Game" : "Tiles");
		Writer.AddItem(MAPITEMTYPE_LAYER, 0, sizeof(Tilemap), &Tilemap);

		CMapItemGroup Group = {};
		Group.m_Version = 4;
		Group.m_ParallaxX = 100;
		Group.m_ParallaxY = 100;
		Group.m_StartLayer = 0;
		Group.m_NumLayers = GroupNumLayers;
		Writer.AddItem(MAPITEMTYPE_GROUP, 0, TruncateGroup ? sizeof(CMapItemGroup_v1) - 4 : sizeof(Group), &Group);

		Writer.Finish();
	}

	// 用最小 kernel 注册 storage 与地图组件，再走真实的 CMap::Load 校验链
	bool LoadMap(IStorage *pStorage, const char *pFilename)
	{
		std::unique_ptr<IKernel> pKernel(IKernel::Create());
		pKernel->RegisterInterface(pStorage, false);
		IEngineMap *pMap = CreateEngineMap();
		pKernel->RegisterInterface(pMap);
		return pMap->Load(pFilename, IStorage::TYPE_SAVE);
	}
}

TEST(MapLoad, LoadsMinimalValidMap)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "valid.map", 2, 2, TILESLAYERFLAG_GAME, 1, false);
	EXPECT_TRUE(LoadMap(pStorage.get(), "valid.map"));
}

TEST(MapLoad, EditorReaderLoadsWithoutKernelRegistration)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "editor.map", 3, 2, TILESLAYERFLAG_GAME, 1, false);
	std::unique_ptr<IEngineMap> pMap(CreateEngineMap(pStorage.get()));

	ASSERT_TRUE(pMap->Load("editor.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(pMap->IsLoaded());
	int Start, Count;
	pMap->GetType(MAPITEMTYPE_LAYER, &Start, &Count);
	ASSERT_EQ(Count, 1);
	const auto *pLayer = static_cast<const CMapItemLayerTilemap *>(pMap->GetItem(Start));
	ASSERT_NE(pLayer, nullptr);
	EXPECT_EQ(pLayer->m_Width, 3);
	EXPECT_EQ(pLayer->m_Height, 2);
}

TEST(MapLoad, EditorReaderLeavesRegisteredGameMapUntouched)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "game.map", 2, 2, TILESLAYERFLAG_GAME, 1, false);
	WriteMap(pStorage.get(), "editor.map", 4, 3, TILESLAYERFLAG_GAME, 1, false);
	std::unique_ptr<IKernel> pKernel(IKernel::Create());
	pKernel->RegisterInterface(pStorage.get(), false);
	IEngineMap *pGameMap = CreateEngineMap();
	pKernel->RegisterInterface(pGameMap);
	ASSERT_TRUE(pGameMap->Load("game.map", IStorage::TYPE_SAVE));
	const unsigned GameCrc = pGameMap->Crc();

	{
		std::unique_ptr<IEngineMap> pEditorMap(CreateEngineMap(pStorage.get()));
		ASSERT_TRUE(pEditorMap->Load("editor.map", IStorage::TYPE_SAVE));
		EXPECT_NE(pEditorMap->Crc(), GameCrc);
		EXPECT_EQ(pKernel->RequestInterface<IEngineMap>(), pGameMap);
	}
	EXPECT_TRUE(pGameMap->IsLoaded());
	EXPECT_EQ(pGameMap->Crc(), GameCrc);
}

TEST(MapLoad, IndependentEditorReadersRetainTheirOwnMap)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "first.map", 2, 2, TILESLAYERFLAG_GAME, 1, false);
	WriteMap(pStorage.get(), "second.map", 5, 4, TILESLAYERFLAG_GAME, 1, false);
	std::unique_ptr<IEngineMap> pFirst(CreateEngineMap(pStorage.get()));
	std::unique_ptr<IEngineMap> pSecond(CreateEngineMap(pStorage.get()));
	ASSERT_TRUE(pFirst->Load("first.map", IStorage::TYPE_SAVE));
	const unsigned FirstCrc = pFirst->Crc();
	ASSERT_TRUE(pSecond->Load("second.map", IStorage::TYPE_SAVE));

	EXPECT_TRUE(pFirst->IsLoaded());
	EXPECT_EQ(pFirst->Crc(), FirstCrc);
	EXPECT_NE(pSecond->Crc(), FirstCrc);
}

TEST(MapLoad, EditorReaderCanRetryAfterMissingFile)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "valid.map", 2, 2, TILESLAYERFLAG_GAME, 1, false);
	std::unique_ptr<IEngineMap> pMap(CreateEngineMap(pStorage.get()));

	EXPECT_FALSE(pMap->Load("missing.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(pMap->IsLoaded());
	EXPECT_TRUE(pMap->Load("valid.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(pMap->IsLoaded());
}

TEST(MapLoad, FailedEditorReloadPreservesLastValidMap)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "valid.map", 2, 2, TILESLAYERFLAG_GAME, 1, false);
	WriteMap(pStorage.get(), "invalid.map", 1, 2, TILESLAYERFLAG_GAME, 1, false);
	std::unique_ptr<IEngineMap> pMap(CreateEngineMap(pStorage.get()));
	ASSERT_TRUE(pMap->Load("valid.map", IStorage::TYPE_SAVE));
	const unsigned PreviousCrc = pMap->Crc();

	EXPECT_FALSE(pMap->Load("invalid.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(pMap->IsLoaded());
	EXPECT_EQ(pMap->Crc(), PreviousCrc);
}

TEST(MapLoad, EditorReaderSupportsAbsoluteAndUnicodePaths)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "编辑器测试.map", 2, 2, TILESLAYERFLAG_GAME, 1, false);
	char aPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "编辑器测试.map", aPath, sizeof(aPath));
	std::unique_ptr<IEngineMap> pMap(CreateEngineMap(pStorage.get()));

	EXPECT_TRUE(pMap->Load(aPath, IStorage::TYPE_ABSOLUTE));
}

TEST(MapLoad, RejectsTileLayerWidthBelowTwo)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "narrow.map", 1, 2, TILESLAYERFLAG_GAME, 1, false);
	EXPECT_FALSE(LoadMap(pStorage.get(), "narrow.map"));
}

TEST(MapLoad, RejectsMissingGameLayer)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "nogame.map", 2, 2, 0, 1, false);
	EXPECT_FALSE(LoadMap(pStorage.get(), "nogame.map"));
}

TEST(MapLoad, RejectsInvalidFlagCombination)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "flags.map", 2, 2, TILESLAYERFLAG_GAME | TILESLAYERFLAG_TELE, 1, false);
	EXPECT_FALSE(LoadMap(pStorage.get(), "flags.map"));
}

TEST(MapLoad, RejectsTruncatedGroupItem)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "truncated_group.map", 2, 2, TILESLAYERFLAG_GAME, 1, true);
	EXPECT_FALSE(LoadMap(pStorage.get(), "truncated_group.map"));
}

TEST(MapLoad, RejectsGroupWithOutOfRangeLayers)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "range.map", 2, 2, TILESLAYERFLAG_GAME, 2, false);
	EXPECT_FALSE(LoadMap(pStorage.get(), "range.map"));
}

TEST(MapLoad, RejectsMissingGameLayerData)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteMap(pStorage.get(), "nodata.map", 2, 2, TILESLAYERFLAG_GAME, 1, false, INVALID_DATA_INDEX);
	EXPECT_FALSE(LoadMap(pStorage.get(), "nodata.map"));
}
