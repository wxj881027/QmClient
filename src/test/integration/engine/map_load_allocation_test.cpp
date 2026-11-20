#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/gamecore.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>
#include <test/test.h>

#include <array>
#include <memory>

namespace
{
	class MapLoadAllocation : public ::testing::Test
	{
	protected:
		enum class ECorruption
		{
			NONE,
			DUPLICATE_LAYERS,
			SHORT_GAME,
			SHORT_DECOR,
		};
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CMap> m_pMap;
		const void *m_pPreviousGame = nullptr;
		unsigned m_PreviousCrc = 0;

		void Write(const char *pFilename, const char *pTag, ECorruption Corruption = ECorruption::NONE)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), pFilename));
			ASSERT_EQ(Writer.AddDataString(pTag), 0);
			CMapItemVersion Version{};
			Version.m_Version = 1;
			ASSERT_GE(Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version), 0);
			const auto AddLayer = [&](int Id, const char *pName, bool Game, bool Short) {
				std::array<CTile, 4> aTiles{};
				if(!Game)
					aTiles.fill(CTile{9, 0, 0, 0});
				CMapItemLayerTilemap Layer{};
				Layer.m_Version = 3;
				Layer.m_Layer.m_Type = LAYERTYPE_TILES;
				Layer.m_Width = Layer.m_Height = 2;
				Layer.m_Flags = Game ? TILESLAYERFLAG_GAME : 0;
				Layer.m_Image = -1;
				Layer.m_ColorEnv = -1;
				Layer.m_Color = CColor(255, 255, 255, 255);
				Layer.m_Tele = Layer.m_Speedup = Layer.m_Front = Layer.m_Switch = Layer.m_Tune = -1;
				Layer.m_Data = Writer.AddData(Short ? sizeof(CTile) : sizeof(aTiles), aTiles.data());
				ASSERT_EQ(Layer.m_Data, Id + 1);
				StrToInts(Layer.m_aName, std::size(Layer.m_aName), pName);
				ASSERT_GE(Writer.AddItem(MAPITEMTYPE_LAYER, Id, sizeof(Layer), &Layer), 0);
			};
			ASSERT_NO_FATAL_FAILURE(AddLayer(0, "Game", true, Corruption == ECorruption::SHORT_GAME));
			ASSERT_NO_FATAL_FAILURE(AddLayer(1, "Decor", false, Corruption == ECorruption::SHORT_DECOR));
			CMapItemGroup Group{};
			Group.m_Version = 3;
			Group.m_ParallaxX = Group.m_ParallaxY = 100;
			Group.m_NumLayers = 2;
			StrToInts(Group.m_aName, std::size(Group.m_aName), "Game");
			ASSERT_GE(Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group), 0);
			if(Corruption == ECorruption::DUPLICATE_LAYERS)
				ASSERT_GE(Writer.AddItem(MAPITEMTYPE_GROUP, 1, sizeof(Group), &Group), 0);
			ASSERT_TRUE(Writer.Finish());
		}
		void LoadPrevious()
		{
			ASSERT_TRUE(m_pMap->Load("old.map", IStorage::TYPE_SAVE));
			m_pPreviousGame = m_pMap->GetData(1);
			ASSERT_NE(m_pPreviousGame, nullptr);
			m_PreviousCrc = m_pMap->Crc();
		}
		void ExpectPrevious()
		{
			ASSERT_TRUE(m_pMap->IsLoaded());
			EXPECT_STREQ(m_pMap->GetDataString(0), "old");
			EXPECT_EQ(m_pMap->GetData(1), m_pPreviousGame);
			EXPECT_EQ(m_pMap->Crc(), m_PreviousCrc);
		}
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_NO_FATAL_FAILURE(Write("old.map", "old"));
			ASSERT_NO_FATAL_FAILURE(Write("new.map", "new"));
			m_pMap = std::make_unique<CMap>(m_pStorage.get());
			ASSERT_NO_FATAL_FAILURE(LoadPrevious());
		}
	};
}

TEST_F(MapLoadAllocation, ExistingVirtualEntryLoadsAndPublishesTheNewMap)
{
	IEngineMap &Interface = *m_pMap;
	ASSERT_TRUE(Interface.Load("new.map", IStorage::TYPE_SAVE));
	EXPECT_TRUE(m_pMap->IsLoaded());
	EXPECT_STREQ(m_pMap->GetDataString(0), "new");
	EXPECT_NE(m_pMap->GetData(1), nullptr);
}

TEST_F(MapLoadAllocation, SuccessfulPreparationReleasesScratchBeforeReturning)
{
	CTrackedAllocationResource Memory;
	ASSERT_TRUE(m_pMap->Load("new.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_GT(Memory.m_Allocations, 0);
	EXPECT_EQ(Memory.m_Live, 0);
	EXPECT_STREQ(m_pMap->GetDataString(0), "new");
}

TEST_F(MapLoadAllocation, EveryIndexAllocationFailurePreservesThePreviousMapAndCanRetry)
{
	CTrackedAllocationResource Baseline;
	CMap Probe(m_pStorage.get());
	ASSERT_TRUE(Probe.Load("new.map", IStorage::TYPE_SAVE, &Baseline));
	ASSERT_GT(Baseline.m_Allocations, 0);
	ASSERT_EQ(Baseline.m_Live, 0);
	Probe.Unload();
	for(int FailAt = 1; FailAt <= Baseline.m_Allocations; ++FailAt)
	{
		SCOPED_TRACE(FailAt);
		ASSERT_NO_FATAL_FAILURE(LoadPrevious());
		CTrackedAllocationResource Memory;
		Memory.m_FailAt = FailAt;
		bool Loaded = true;
		EXPECT_NO_THROW(Loaded = m_pMap->Load("new.map", IStorage::TYPE_SAVE, &Memory));
		ASSERT_FALSE(Loaded);
		EXPECT_EQ(Memory.m_Allocations, FailAt);
		EXPECT_EQ(Memory.m_Live, 0);
		ExpectPrevious();
		Memory.m_FailAt = -1;
		ASSERT_TRUE(m_pMap->Load("new.map", IStorage::TYPE_SAVE, &Memory));
		EXPECT_EQ(Memory.m_Live, 0);
		EXPECT_STREQ(m_pMap->GetDataString(0), "new");
	}
}

TEST_F(MapLoadAllocation, NullIndexAllocationReturnsFailureWithoutReplacingTheMap)
{
	CTrackedAllocationResource Memory;
	Memory.m_FailAt = 1;
	Memory.m_ReturnNull = true;
	EXPECT_FALSE(m_pMap->Load("new.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_EQ(Memory.m_Live, 0);
	ExpectPrevious();
}

TEST_F(MapLoadAllocation, UnknownPreparationExceptionReturnsFailureAndKeepsTheMap)
{
	CTrackedAllocationResource Memory;
	Memory.m_FailAt = 1;
	Memory.m_ThrowUnknown = true;
	EXPECT_FALSE(m_pMap->Load("new.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_EQ(Memory.m_Live, 0);
	ExpectPrevious();
}

TEST_F(MapLoadAllocation, FailureAfterExplicitUnloadStaysUnloadedAndCanRetry)
{
	m_pMap->Unload();
	CTrackedAllocationResource Memory;
	Memory.m_FailAt = 1;
	EXPECT_FALSE(m_pMap->Load("new.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_FALSE(m_pMap->IsLoaded());
	EXPECT_EQ(m_pMap->File(), nullptr);
	EXPECT_EQ(Memory.m_Live, 0);
	Memory.m_FailAt = -1;
	ASSERT_TRUE(m_pMap->Load("new.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_STREQ(m_pMap->GetDataString(0), "new");
}

TEST_F(MapLoadAllocation, DuplicateLayerReferencesKeepThePreviousMapAndReleaseScratch)
{
	ASSERT_NO_FATAL_FAILURE(Write("duplicate.map", "candidate", ECorruption::DUPLICATE_LAYERS));
	CTrackedAllocationResource Memory;
	EXPECT_FALSE(m_pMap->Load("duplicate.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_EQ(Memory.m_Live, 0);
	ExpectPrevious();
}

TEST_F(MapLoadAllocation, InvalidRequiredGameDataCannotPublishTheCandidate)
{
	ASSERT_NO_FATAL_FAILURE(Write("bad-game.map", "candidate", ECorruption::SHORT_GAME));
	CTrackedAllocationResource Memory;
	EXPECT_FALSE(m_pMap->Load("bad-game.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_EQ(Memory.m_Live, 0);
	ExpectPrevious();
}

TEST_F(MapLoadAllocation, InvalidOptionalTileDataStillFailsLazilyAfterSuccessfulLoad)
{
	ASSERT_NO_FATAL_FAILURE(Write("bad-decor.map", "candidate", ECorruption::SHORT_DECOR));
	CTrackedAllocationResource Memory;
	ASSERT_TRUE(m_pMap->Load("bad-decor.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_EQ(Memory.m_Live, 0);
	EXPECT_EQ(m_pMap->GetData(2), nullptr);
	EXPECT_TRUE(m_pMap->IsLoaded());
	EXPECT_NE(m_pMap->GetData(1), nullptr);
	EXPECT_STREQ(m_pMap->GetDataString(0), "candidate");
}

TEST_F(MapLoadAllocation, TemporaryPreparationDomainIsNotBorrowedByLazyTileProcessing)
{
	{
		CTrackedAllocationResource Memory;
		ASSERT_TRUE(m_pMap->Load("new.map", IStorage::TYPE_SAVE, &Memory));
		EXPECT_EQ(Memory.m_Live, 0);
	}
	const auto *pDecor = static_cast<const CTile *>(m_pMap->GetData(2));
	ASSERT_NE(pDecor, nullptr);
	EXPECT_EQ(m_pMap->GetDataSize(2), 4 * sizeof(CTile));
	EXPECT_EQ(pDecor[3].m_Index, 9);
	m_pMap->Unload();
	EXPECT_FALSE(m_pMap->IsLoaded());
}
