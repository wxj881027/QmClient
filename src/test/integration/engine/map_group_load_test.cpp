#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/gamecore.h>
#include <game/layers.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <iterator>
#include <memory>
#include <optional>
#include <vector>

namespace
{
	// v2 在基础字段之后增加五个裁剪整数，还没有 v3 的名称。
	constexpr size_t GROUP_V2_SIZE = sizeof(CMapItemGroup_v1) + 5 * sizeof(int);

	class MapGroupLoad : public ::testing::Test
	{
	protected:
		struct SGroupFormat
		{
			int m_Version;
			size_t m_Size;
		};
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		void WriteMap(const char *pName, SGroupFormat GameGroup, int Width = 2, std::optional<SGroupFormat> ExtraGroup = std::nullopt)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), pName));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);

			const std::vector<CTile> vTiles(static_cast<size_t>(Width) * 2);
			CMapItemLayerTilemap Game{};
			Game.m_Layer.m_Type = LAYERTYPE_TILES;
			Game.m_Version = 4;
			Game.m_Width = Width;
			Game.m_Height = 2;
			Game.m_Flags = TILESLAYERFLAG_GAME;
			Game.m_Color = CColor(255, 255, 255, 255);
			Game.m_ColorEnv = -1;
			Game.m_Image = -1;
			Game.m_Data = Writer.AddData(vTiles.size() * sizeof(CTile), vTiles.data());
			Game.m_Tele = Game.m_Speedup = Game.m_Front = Game.m_Switch = Game.m_Tune = -1;
			StrToInts(Game.m_aName, std::size(Game.m_aName), "Game");
			Writer.AddItem(MAPITEMTYPE_LAYER, 0, sizeof(Game), &Game);

			const auto AddGroup = [&](int Id, SGroupFormat Format, bool IsGame) {
				CMapItemGroup Group{};
				Group.m_Version = Format.m_Version;
				Group.m_OffsetX = 12;
				Group.m_OffsetY = 18;
				Group.m_ParallaxX = 80;
				Group.m_ParallaxY = 90;
				Group.m_NumLayers = IsGame ? 1 : 0;
				Group.m_UseClipping = 1;
				Group.m_ClipX = 11;
				Group.m_ClipY = 22;
				Group.m_ClipW = 33;
				Group.m_ClipH = 44;
				StrToInts(Group.m_aName, std::size(Group.m_aName), IsGame ? "keep-name" : "extra");
				Writer.AddItem(MAPITEMTYPE_GROUP, Id, Format.m_Size, &Group);
			};
			if(ExtraGroup)
				AddGroup(0, *ExtraGroup, false);
			AddGroup(ExtraGroup ? 1 : 0, GameGroup, true);
			Writer.Finish();
		}
	};
}

TEST_F(MapGroupLoad, VersionOneBaseHeaderStillInitializesGameLayers)
{
	WriteMap("v1.map", {1, sizeof(CMapItemGroup_v1)});
	CMap Map(m_pStorage.get());
	ASSERT_TRUE(Map.Load("v1.map", IStorage::TYPE_SAVE));
	CLayers Layers;
	Layers.Init(&Map, false);
	ASSERT_NE(Layers.GameLayer(), nullptr);
	ASSERT_NE(Layers.GameGroup(), nullptr);
	EXPECT_EQ(Layers.GameLayer()->m_Width, 2);
	EXPECT_EQ(Layers.GameGroup()->m_OffsetX, 0);
	EXPECT_EQ(Layers.GameGroup()->m_ParallaxX, 100);
}

TEST_F(MapGroupLoad, VersionTwoWithoutNameStillResetsGameClippingSafely)
{
	WriteMap("v2.map", {2, GROUP_V2_SIZE});
	CMap Map(m_pStorage.get());
	ASSERT_TRUE(Map.Load("v2.map", IStorage::TYPE_SAVE));
	CLayers Layers;
	Layers.Init(&Map, false);
	ASSERT_NE(Layers.GameGroup(), nullptr);
	EXPECT_EQ(Layers.GameGroup()->m_UseClipping, 0);
	EXPECT_EQ(Layers.GameGroup()->m_ClipX, 0);
	EXPECT_EQ(Layers.GameGroup()->m_ClipY, 0);
	EXPECT_EQ(Layers.GameGroup()->m_ClipW, 0);
	EXPECT_EQ(Layers.GameGroup()->m_ClipH, 0);
}

TEST_F(MapGroupLoad, VersionThreeKeepsItsNameAfterLayerInitialization)
{
	WriteMap("v3.map", {3, sizeof(CMapItemGroup)});
	CMap Map(m_pStorage.get());
	ASSERT_TRUE(Map.Load("v3.map", IStorage::TYPE_SAVE));
	CLayers Layers;
	Layers.Init(&Map, false);
	ASSERT_NE(Layers.GameGroup(), nullptr);
	char aName[12];
	ASSERT_TRUE(IntsToStr(Layers.GameGroup()->m_aName, std::size(Layers.GameGroup()->m_aName), aName, sizeof(aName)));
	EXPECT_STREQ(aName, "keep-name");
}

TEST_F(MapGroupLoad, CompleteNewerVersionRetainsExistingLoadCompatibility)
{
	WriteMap("v4.map", {4, sizeof(CMapItemGroup)});
	CMap Map(m_pStorage.get());
	ASSERT_TRUE(Map.Load("v4.map", IStorage::TYPE_SAVE));
	CLayers Layers;
	Layers.Init(&Map, false);
	ASSERT_NE(Layers.GameLayer(), nullptr);
	EXPECT_EQ(Layers.GameLayer()->m_Width, 2);
}

TEST_F(MapGroupLoad, VersionTwoMissingClippingFieldsIsRejectedBeforePublication)
{
	WriteMap("truncated.map", {2, sizeof(CMapItemGroup_v1)});
	CMap Map(m_pStorage.get());
	EXPECT_FALSE(Map.Load("truncated.map", IStorage::TYPE_SAVE));
	EXPECT_FALSE(Map.IsLoaded());
}

TEST_F(MapGroupLoad, VersionTwoMissingLastClippingFieldIsRejected)
{
	WriteMap("truncated.map", {2, GROUP_V2_SIZE - sizeof(int)});
	CMap Map(m_pStorage.get());
	EXPECT_FALSE(Map.Load("truncated.map", IStorage::TYPE_SAVE));
}

TEST_F(MapGroupLoad, VersionThreeMissingNameFieldsIsRejected)
{
	WriteMap("truncated.map", {3, GROUP_V2_SIZE});
	CMap Map(m_pStorage.get());
	EXPECT_FALSE(Map.Load("truncated.map", IStorage::TYPE_SAVE));
}

TEST_F(MapGroupLoad, VersionThreeMissingLastNameFieldIsRejected)
{
	WriteMap("truncated.map", {3, sizeof(CMapItemGroup) - sizeof(int)});
	CMap Map(m_pStorage.get());
	EXPECT_FALSE(Map.Load("truncated.map", IStorage::TYPE_SAVE));
}

TEST_F(MapGroupLoad, TruncatedNonGameGroupCannotBypassValidation)
{
	WriteMap("truncated.map", {3, sizeof(CMapItemGroup)}, 2, SGroupFormat{2, sizeof(CMapItemGroup_v1)});
	CMap Map(m_pStorage.get());
	EXPECT_FALSE(Map.Load("truncated.map", IStorage::TYPE_SAVE));
}

TEST_F(MapGroupLoad, CompleteVersionTwoNonGameGroupKeepsClippingValues)
{
	WriteMap("extra.map", {3, sizeof(CMapItemGroup)}, 2, SGroupFormat{2, GROUP_V2_SIZE});
	CMap Map(m_pStorage.get());
	ASSERT_TRUE(Map.Load("extra.map", IStorage::TYPE_SAVE));
	CLayers Layers;
	Layers.Init(&Map, false);
	ASSERT_EQ(Layers.NumGroups(), 2);
	const CMapItemGroup *pExtra = Layers.GetGroup(0);
	EXPECT_EQ(pExtra->m_UseClipping, 1);
	EXPECT_EQ(pExtra->m_ClipX, 11);
	EXPECT_EQ(pExtra->m_ClipH, 44);
}

TEST_F(MapGroupLoad, FailedReloadPreservesMapAndBorrowedLayerState)
{
	WriteMap("valid.map", {1, sizeof(CMapItemGroup_v1)});
	WriteMap("invalid.map", {2, sizeof(CMapItemGroup_v1)}, 3);
	CMap Map(m_pStorage.get());
	ASSERT_TRUE(Map.Load("valid.map", IStorage::TYPE_SAVE));
	CLayers Layers;
	Layers.Init(&Map, false);
	const auto *pGroup = Layers.GameGroup();
	const auto *pLayer = Layers.GameLayer();
	ASSERT_NE(pLayer, nullptr);
	const auto *pData = Map.GetData(pLayer->m_Data);
	const unsigned Crc = Map.Crc();
	EXPECT_FALSE(Map.Load("invalid.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(Map.IsLoaded());
	EXPECT_EQ(Map.Crc(), Crc);
	EXPECT_EQ(Layers.GameGroup(), pGroup);
	EXPECT_EQ(Layers.GameLayer(), pLayer);
	EXPECT_EQ(Map.GetData(pLayer->m_Data), pData);
	EXPECT_EQ(pLayer->m_Width, 2);
}

TEST_F(MapGroupLoad, CorrectedGroupCanBeLoadedAfterInitialFailure)
{
	WriteMap("retry.map", {2, sizeof(CMapItemGroup_v1)});
	CMap Map(m_pStorage.get());
	ASSERT_FALSE(Map.Load("retry.map", IStorage::TYPE_SAVE));
	WriteMap("retry.map", {2, GROUP_V2_SIZE}, 3);
	ASSERT_TRUE(Map.Load("retry.map", IStorage::TYPE_SAVE));
	CLayers Layers;
	Layers.Init(&Map, false);
	ASSERT_NE(Layers.GameLayer(), nullptr);
	EXPECT_EQ(Layers.GameLayer()->m_Width, 3);
}
