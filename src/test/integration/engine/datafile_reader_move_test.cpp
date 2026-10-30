#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <utility>

namespace
{
	class DatafileReaderMove : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void Write(const char *pFilename, const char *pPayload)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), pFilename));
			ASSERT_EQ(Writer.AddDataString(pPayload), 0);
			CMapItemVersion Version{};
			Version.m_Version = 1;
			ASSERT_GE(Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version), 0);
			const CTile Tile{};
			CMapItemLayerTilemap Game{};
			Game.m_Version = 3;
			Game.m_Layer.m_Type = LAYERTYPE_TILES;
			Game.m_Width = Game.m_Height = 1;
			Game.m_Flags = TILESLAYERFLAG_GAME;
			Game.m_Image = -1;
			Game.m_ColorEnv = -1;
			Game.m_Data = Writer.AddData(sizeof(Tile), &Tile);
			ASSERT_GE(Writer.AddItem(MAPITEMTYPE_LAYER, 0, sizeof(Game), &Game), 0);
			CMapItemGroup Group{};
			Group.m_Version = 3;
			Group.m_ParallaxX = Group.m_ParallaxY = 100;
			Group.m_NumLayers = 1;
			ASSERT_GE(Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group), 0);
			ASSERT_TRUE(Writer.Finish());
		}
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_NO_FATAL_FAILURE(Write("old.map", "old"));
			ASSERT_NO_FATAL_FAILURE(Write("new.map", "new"));
		}
		static std::weak_ptr<int> WatchProcessor(CDataFileReader &Reader)
		{
			auto pLifetime = std::make_shared<int>(1);
			std::weak_ptr<int> Lifetime = pLifetime;
			Reader.AddDataProcessor(0, [pLifetime](void *pData, size_t Size) {
				(void)pLifetime;
				return std::make_pair(pData, Size);
			});
			return Lifetime;
		}
	};
}

TEST_F(DatafileReaderMove, ReplacingOpenReaderReleasesOldProcessorAndKeepsSourceData)
{
	CDataFileReader Target, Source;
	ASSERT_TRUE(Target.Open(m_pStorage.get(), "old.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(Source.Open(m_pStorage.get(), "new.map", IStorage::TYPE_SAVE));
	auto OldLifetime = WatchProcessor(Target);
	auto NewLifetime = WatchProcessor(Source);
	ASSERT_STREQ(Target.GetDataString(0), "old");
	const char *pNewData = Source.GetDataString(0);
	ASSERT_NE(pNewData, nullptr);
	const unsigned NewCrc = Source.Crc();
	EXPECT_EQ(&(Target = std::move(Source)), &Target);
	EXPECT_FALSE(Source.IsOpen());
	EXPECT_TRUE(OldLifetime.expired());
	EXPECT_FALSE(NewLifetime.expired());
	EXPECT_EQ(Target.GetDataString(0), pNewData);
	EXPECT_STREQ(Target.GetDataString(0), "new");
	EXPECT_EQ(Target.Crc(), NewCrc);
	Target.Close();
	EXPECT_TRUE(NewLifetime.expired());
}

TEST_F(DatafileReaderMove, SelfMoveOfOpenReaderPreservesItsResourcesAndContents)
{
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "old.map", IStorage::TYPE_SAVE));
	auto Lifetime = WatchProcessor(Reader);
	const char *pData = Reader.GetDataString(0);
	ASSERT_NE(pData, nullptr);
	CDataFileReader &Alias = Reader;
	EXPECT_EQ(&(Reader = std::move(Alias)), &Reader);
	ASSERT_TRUE(Reader.IsOpen());
	EXPECT_FALSE(Lifetime.expired());
	EXPECT_EQ(Reader.GetDataString(0), pData);
	EXPECT_STREQ(Reader.GetDataString(0), "old");
	Reader.Close();
	EXPECT_TRUE(Lifetime.expired());
}

TEST_F(DatafileReaderMove, MovingClosedReaderIntoOpenReaderReleasesPreviousResources)
{
	CDataFileReader Target, Source;
	ASSERT_TRUE(Target.Open(m_pStorage.get(), "old.map", IStorage::TYPE_SAVE));
	auto Lifetime = WatchProcessor(Target);
	Target = std::move(Source);
	EXPECT_FALSE(Target.IsOpen());
	EXPECT_FALSE(Source.IsOpen());
	EXPECT_TRUE(Lifetime.expired());
	Target.Close();
}

TEST_F(DatafileReaderMove, DestroyingMovedFromReaderDoesNotCloseTransferredFile)
{
	CDataFileReader Target;
	std::weak_ptr<int> Lifetime;
	{
		CDataFileReader Source;
		ASSERT_TRUE(Source.Open(m_pStorage.get(), "new.map", IStorage::TYPE_SAVE));
		Lifetime = WatchProcessor(Source);
		Target = std::move(Source);
		EXPECT_FALSE(Source.IsOpen());
	}
	ASSERT_TRUE(Target.IsOpen());
	EXPECT_FALSE(Lifetime.expired());
	EXPECT_STREQ(Target.GetDataString(0), "new");
	Target.Close();
	EXPECT_TRUE(Lifetime.expired());
}

TEST_F(DatafileReaderMove, RepeatedReplacementReleasesEachPreviousOwner)
{
	CDataFileReader Target, First, Second;
	ASSERT_TRUE(First.Open(m_pStorage.get(), "old.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(Second.Open(m_pStorage.get(), "new.map", IStorage::TYPE_SAVE));
	auto FirstLifetime = WatchProcessor(First);
	auto SecondLifetime = WatchProcessor(Second);
	Target = std::move(First);
	EXPECT_STREQ(Target.GetDataString(0), "old");
	Target = std::move(Second);
	EXPECT_TRUE(FirstLifetime.expired());
	EXPECT_FALSE(SecondLifetime.expired());
	EXPECT_STREQ(Target.GetDataString(0), "new");
	Target.Close();
	EXPECT_TRUE(SecondLifetime.expired());
}

TEST_F(DatafileReaderMove, SelfMoveOfClosedReaderCanStillOpenNormally)
{
	CDataFileReader Reader;
	CDataFileReader &Alias = Reader;
	Reader = std::move(Alias);
	ASSERT_FALSE(Reader.IsOpen());
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "new.map", IStorage::TYPE_SAVE));
	EXPECT_STREQ(Reader.GetDataString(0), "new");
}

TEST_F(DatafileReaderMove, MapAdoptReleasesPreviouslyLoadedMapResources)
{
	CMap Target(m_pStorage.get()), Source(m_pStorage.get());
	ASSERT_TRUE(Target.Load("old.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(Source.Load("new.map", IStorage::TYPE_SAVE));
	auto OldLifetime = WatchProcessor(*Target.GetReader());
	auto NewLifetime = WatchProcessor(*Source.GetReader());
	Target.Adopt(std::move(Source));
	EXPECT_FALSE(Source.IsLoaded());
	ASSERT_TRUE(Target.IsLoaded());
	EXPECT_TRUE(OldLifetime.expired());
	EXPECT_STREQ(Target.GetDataString(0), "new");
	Target.Unload();
	EXPECT_TRUE(NewLifetime.expired());
}

TEST_F(DatafileReaderMove, MapSelfAdoptKeepsTheCurrentMap)
{
	CMap Map(m_pStorage.get());
	ASSERT_TRUE(Map.Load("old.map", IStorage::TYPE_SAVE));
	auto Lifetime = WatchProcessor(*Map.GetReader());
	CMap &Alias = Map;
	Map.Adopt(std::move(Alias));
	ASSERT_TRUE(Map.IsLoaded());
	EXPECT_FALSE(Lifetime.expired());
	EXPECT_STREQ(Map.GetDataString(0), "old");
	Map.Unload();
	EXPECT_TRUE(Lifetime.expired());
}
