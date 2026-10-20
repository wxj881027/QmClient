#include <base/str.h>

#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/editor/mapitems/load_resources.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <climits>
#include <cstddef>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	class CPreparationFailureMap : public CMap
	{
	public:
		enum class EFailure
		{
			NONE,
			ALLOCATION,
			CAPACITY,
		};
		EFailure m_Failure = EFailure::NONE;
		using CMap::CMap;
		void GetType(int Type, int *pStart, int *pNum) override
		{
			// 仅在资源准备调用的外部边界注入异常，不模拟容器或解析算法。
			if(Type == MAPITEMTYPE_AUTOMAPPER_CONFIG)
			{
				if(m_Failure == EFailure::ALLOCATION)
					throw std::bad_alloc();
				if(m_Failure == EFailure::CAPACITY)
					throw std::length_error("injected resource capacity failure");
			}
			CMap::GetType(Type, pStart, pNum);
		}
	};

	class EditorMapAutomapper : public ::testing::Test
	{
	protected:
		struct SLayer
		{
			int m_Type = LAYERTYPE_TILES;
			int m_Version = 2;
			int m_PhysicsFlags = 0;
		};
		struct SGroup
		{
			int m_Version = 3;
			std::vector<SLayer> m_vLayers;
		};
		struct SConfig
		{
			CMapItemAutoMapperConfig m_Item{1, 0, 0, 7, 13, CMapItemAutoMapperConfig::FLAG_AUTOMATIC};
			int m_Extra = 123;
			size_t m_Size = sizeof(CMapItemAutoMapperConfig);
		};
		struct SItems
		{
			std::vector<SGroup> m_vGroups{{3, {{}, {LAYERTYPE_TILES, 2, TILESLAYERFLAG_GAME}}}};
			std::vector<SConfig> m_vConfigs{{}};
		};
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CPreparationFailureMap> m_pMap;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pMap = std::make_unique<CPreparationFailureMap>(m_pStorage.get());
		}
		void WriteTiles(CDataFileWriter &Writer, int Id, int Flags)
		{
			CMapItemLayerTilemap Layer{};
			Layer.m_Layer.m_Type = LAYERTYPE_TILES;
			Layer.m_Version = 3;
			Layer.m_Width = Layer.m_Height = 2;
			Layer.m_Flags = Flags;
			Layer.m_Color = CColor(255, 255, 255, 255);
			Layer.m_ColorEnv = Layer.m_Image = -1;
			Layer.m_Tele = Layer.m_Speedup = Layer.m_Front = Layer.m_Switch = Layer.m_Tune = -1;
			const std::array<CTile, 4> aTiles{};
			Layer.m_Data = Writer.AddData(sizeof(aTiles), aTiles.data());
			if(Flags & TILESLAYERFLAG_TELE)
			{
				const std::array<CTeleTile, 4> aTele{};
				Layer.m_Tele = Writer.AddData(sizeof(aTele), aTele.data());
			}
			if(Flags & TILESLAYERFLAG_SPEEDUP)
			{
				const std::array<CSpeedupTile, 4> aSpeedup{};
				Layer.m_Speedup = Writer.AddData(sizeof(aSpeedup), aSpeedup.data());
			}
			if(Flags & TILESLAYERFLAG_FRONT)
				Layer.m_Front = Writer.AddData(sizeof(aTiles), aTiles.data());
			if(Flags & TILESLAYERFLAG_SWITCH)
			{
				const std::array<CSwitchTile, 4> aSwitch{};
				Layer.m_Switch = Writer.AddData(sizeof(aSwitch), aSwitch.data());
			}
			if(Flags & TILESLAYERFLAG_TUNE)
			{
				const std::array<CTuneTile, 4> aTune{};
				Layer.m_Tune = Writer.AddData(sizeof(aTune), aTune.data());
			}
			Writer.AddItem(MAPITEMTYPE_LAYER, Id, sizeof(Layer), &Layer);
		}
		void WriteLayer(CDataFileWriter &Writer, int Id, const SLayer &Spec)
		{
			if(Spec.m_Type == LAYERTYPE_TILES)
				WriteTiles(Writer, Id, Spec.m_PhysicsFlags);
			else if(Spec.m_Type == LAYERTYPE_QUADS)
			{
				CMapItemLayerQuads Layer{};
				Layer.m_Layer.m_Type = Spec.m_Type;
				Layer.m_Version = Spec.m_Version;
				Layer.m_Data = Layer.m_Image = -1;
				Writer.AddItem(MAPITEMTYPE_LAYER, Id, sizeof(Layer), &Layer);
			}
			else if(Spec.m_Type == LAYERTYPE_SOUNDS || Spec.m_Type == LAYERTYPE_SOUNDS_DEPRECATED)
			{
				CMapItemLayerSounds Layer{};
				Layer.m_Layer.m_Type = Spec.m_Type;
				Layer.m_Version = Spec.m_Version;
				Layer.m_Data = Layer.m_Sound = -1;
				Writer.AddItem(MAPITEMTYPE_LAYER, Id, sizeof(Layer), &Layer);
			}
			else
			{
				CMapItemLayer Layer{};
				Layer.m_Type = Spec.m_Type;
				Writer.AddItem(MAPITEMTYPE_LAYER, Id, sizeof(Layer), &Layer);
			}
		}
		void WriteMap(const SItems &Items)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "automapper.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);
			int LayerId = 0;
			for(size_t g = 0; g < Items.m_vGroups.size(); ++g)
			{
				const auto &Spec = Items.m_vGroups[g];
				CMapItemGroup Group{};
				Group.m_Version = Spec.m_Version;
				Group.m_ParallaxX = Group.m_ParallaxY = 100;
				Group.m_StartLayer = LayerId;
				Group.m_NumLayers = Spec.m_vLayers.size();
				Writer.AddItem(MAPITEMTYPE_GROUP, static_cast<int>(g), sizeof(Group), &Group);
				for(const auto &Layer : Spec.m_vLayers)
					WriteLayer(Writer, LayerId++, Layer);
			}
			for(size_t i = 0; i < Items.m_vConfigs.size(); ++i)
			{
				const auto &Config = Items.m_vConfigs[i];
				Writer.AddItem(MAPITEMTYPE_AUTOMAPPER_CONFIG, static_cast<int>(i), Config.m_Size, &Config);
			}
			Writer.Finish();
		}
		void Load(const SItems &Items)
		{
			ASSERT_NO_FATAL_FAILURE(WriteMap(Items));
			ASSERT_TRUE(m_pMap->Load("automapper.map", IStorage::TYPE_SAVE));
		}
	};
}

TEST_F(EditorMapAutomapper, MissingConfigurationsKeepResourcesLoadable)
{
	SItems Items;
	Items.m_vConfigs.clear();
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	EXPECT_TRUE(Resources->m_vAutoMapperConfigs.empty());
}

TEST_F(EditorMapAutomapper, DecorativeTileConfigurationKeepsAllValues)
{
	ASSERT_NO_FATAL_FAILURE(Load({}));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vAutoMapperConfigs.size(), 1u);
	const auto &Item = Resources->m_vAutoMapperConfigs[0];
	EXPECT_EQ(Item.m_GroupId, 0);
	EXPECT_EQ(Item.m_LayerId, 0);
	EXPECT_EQ(Item.m_AutomapperConfig, 7);
	EXPECT_EQ(Item.m_AutomapperSeed, 13);
	EXPECT_EQ(Item.m_Flags, CMapItemAutoMapperConfig::FLAG_AUTOMATIC);
}

TEST_F(EditorMapAutomapper, StoredDisableValueAndSeedAreNotNormalized)
{
	SItems Items;
	Items.m_vConfigs[0].m_Item.m_AutomapperConfig = -1;
	Items.m_vConfigs[0].m_Item.m_AutomapperSeed = -13;
	Items.m_vConfigs[0].m_Item.m_Flags = 0;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vAutoMapperConfigs.size(), 1u);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs[0].m_AutomapperConfig, -1);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs[0].m_AutomapperSeed, -13);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs[0].m_Flags, 0);
}

TEST_F(EditorMapAutomapper, MissingVersionRejectsCandidate)
{
	SItems Items;
	Items.m_vConfigs[0].m_Size = 0;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapAutomapper, UnsupportedVersionNeedsOnlyItsVersionField)
{
	SItems Items;
	Items.m_vConfigs[0].m_Item.m_Version = 2;
	Items.m_vConfigs[0].m_Size = sizeof(int);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	EXPECT_TRUE(Resources->m_vAutoMapperConfigs.empty());
}

TEST_F(EditorMapAutomapper, VersionOneWithoutGroupFieldRejectsCandidate)
{
	SItems Items;
	Items.m_vConfigs[0].m_Size = sizeof(int);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapAutomapper, InvalidGroupDoesNotConsumeMissingLayerOrSettings)
{
	for(int Group : {-1, INT_MAX})
	{
		SCOPED_TRACE(Group);
		SItems Items;
		Items.m_vConfigs[0].m_Item.m_GroupId = Group;
		Items.m_vConfigs[0].m_Size = offsetof(CMapItemAutoMapperConfig, m_LayerId);
		ASSERT_NO_FATAL_FAILURE(Load(Items));
		const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
		ASSERT_TRUE(Resources);
		EXPECT_TRUE(Resources->m_vAutoMapperConfigs.empty());
		m_pMap->Unload();
	}
}

TEST_F(EditorMapAutomapper, ValidGroupWithoutLayerFieldRejectsCandidate)
{
	SItems Items;
	Items.m_vConfigs[0].m_Size = offsetof(CMapItemAutoMapperConfig, m_LayerId);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapAutomapper, InvalidLayerDoesNotConsumeMissingSettings)
{
	for(int Layer : {-1, INT_MAX})
	{
		SCOPED_TRACE(Layer);
		SItems Items;
		Items.m_vConfigs[0].m_Item.m_LayerId = Layer;
		Items.m_vConfigs[0].m_Size = offsetof(CMapItemAutoMapperConfig, m_AutomapperConfig);
		ASSERT_NO_FATAL_FAILURE(Load(Items));
		const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
		ASSERT_TRUE(Resources);
		EXPECT_TRUE(Resources->m_vAutoMapperConfigs.empty());
		m_pMap->Unload();
	}
}

TEST_F(EditorMapAutomapper, DecorativeTargetRequiresEveryConsumedSetting)
{
	for(size_t Size : {offsetof(CMapItemAutoMapperConfig, m_AutomapperConfig), offsetof(CMapItemAutoMapperConfig, m_AutomapperSeed), offsetof(CMapItemAutoMapperConfig, m_Flags)})
	{
		SCOPED_TRACE(Size);
		SItems Items;
		Items.m_vConfigs[0].m_Size = Size;
		ASSERT_NO_FATAL_FAILURE(Load(Items));
		EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
		m_pMap->Unload();
	}
}

TEST_F(EditorMapAutomapper, PhysicsTargetsDoNotConsumeTheSettingsTail)
{
	for(int Flags : {TILESLAYERFLAG_GAME, TILESLAYERFLAG_TELE, TILESLAYERFLAG_SPEEDUP, TILESLAYERFLAG_FRONT, TILESLAYERFLAG_SWITCH, TILESLAYERFLAG_TUNE})
	{
		SCOPED_TRACE(Flags);
		SItems Items;
		Items.m_vGroups[0].m_vLayers[0].m_PhysicsFlags = Flags;
		Items.m_vConfigs[0].m_Size = offsetof(CMapItemAutoMapperConfig, m_AutomapperConfig);
		ASSERT_NO_FATAL_FAILURE(Load(Items));
		const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
		ASSERT_TRUE(Resources);
		EXPECT_TRUE(Resources->m_vAutoMapperConfigs.empty());
		m_pMap->Unload();
	}
}

TEST_F(EditorMapAutomapper, NonTileTargetsDoNotConsumeTheSettingsTail)
{
	for(int Type : {LAYERTYPE_QUADS, LAYERTYPE_SOUNDS, LAYERTYPE_SOUNDS_DEPRECATED})
	{
		SCOPED_TRACE(Type);
		SItems Items;
		Items.m_vGroups[0].m_vLayers[0].m_Type = Type;
		Items.m_vConfigs[0].m_Size = offsetof(CMapItemAutoMapperConfig, m_AutomapperConfig);
		ASSERT_NO_FATAL_FAILURE(Load(Items));
		const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
		ASSERT_TRUE(Resources);
		EXPECT_TRUE(Resources->m_vAutoMapperConfigs.empty());
		m_pMap->Unload();
	}
}

TEST_F(EditorMapAutomapper, SkippedGroupDoesNotShiftTheImportedGroupIndex)
{
	SItems Items;
	Items.m_vGroups.insert(Items.m_vGroups.begin(), {4, {{LAYERTYPE_QUADS, 2, 0}}});
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vAutoMapperConfigs.size(), 1u);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs[0].m_GroupId, 0);
}

TEST_F(EditorMapAutomapper, RawGroupIndexBeyondImportedGroupsRemainsIgnored)
{
	SItems Items;
	Items.m_vGroups.insert(Items.m_vGroups.begin(), {4, {{LAYERTYPE_QUADS, 2, 0}}});
	Items.m_vConfigs[0].m_Item.m_GroupId = 1;
	Items.m_vConfigs[0].m_Size = offsetof(CMapItemAutoMapperConfig, m_LayerId);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	EXPECT_TRUE(Resources->m_vAutoMapperConfigs.empty());
}

TEST_F(EditorMapAutomapper, SkippedLayerKindsDoNotShiftTheImportedLayerIndex)
{
	SItems Items;
	Items.m_vGroups[0].m_vLayers.insert(Items.m_vGroups[0].m_vLayers.begin(), {LAYERTYPE_SOUNDS, 3, 0});
	Items.m_vGroups[0].m_vLayers.insert(Items.m_vGroups[0].m_vLayers.begin(), {LAYERTYPE_INVALID, 0, 0});
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vAutoMapperConfigs.size(), 1u);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs[0].m_LayerId, 0);
}

TEST_F(EditorMapAutomapper, ImportedNonTileLayerStillOccupiesItsIndex)
{
	SItems Items;
	Items.m_vGroups[0].m_vLayers.insert(Items.m_vGroups[0].m_vLayers.begin(), {LAYERTYPE_QUADS, 1, 0});
	Items.m_vConfigs[0].m_Item.m_LayerId = 1;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vAutoMapperConfigs.size(), 1u);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs[0].m_LayerId, 1);
}

TEST_F(EditorMapAutomapper, EmptyImportedGroupStillOccupiesItsIndex)
{
	SItems Items;
	Items.m_vGroups.insert(Items.m_vGroups.begin(), {3, {}});
	Items.m_vConfigs[0].m_Item.m_GroupId = 1;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vAutoMapperConfigs.size(), 1u);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs[0].m_GroupId, 1);
}

TEST_F(EditorMapAutomapper, LaterTruncatedConfigurationCannotPublishPartialItems)
{
	SItems Items;
	Items.m_vConfigs.push_back({});
	Items.m_vConfigs[1].m_Size = offsetof(CMapItemAutoMapperConfig, m_Flags);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapAutomapper, DuplicateTargetsKeepTheirOriginalApplyOrder)
{
	SItems Items;
	Items.m_vConfigs.push_back({});
	Items.m_vConfigs[1].m_Item.m_AutomapperConfig = 99;
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vAutoMapperConfigs.size(), 2u);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs[0].m_AutomapperConfig, 7);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs[1].m_AutomapperConfig, 99);
}

TEST_F(EditorMapAutomapper, ConfigurationSnapshotsRemainOwnedAfterMapUnload)
{
	ASSERT_NO_FATAL_FAILURE(Load({}));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	m_pMap->Unload();
	ASSERT_EQ(Resources->m_vAutoMapperConfigs.size(), 1u);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs[0].m_AutomapperSeed, 13);
}

TEST_F(EditorMapAutomapper, ExtendedVersionOneKeepsItsKnownPrefix)
{
	SItems Items;
	Items.m_vConfigs[0].m_Size = sizeof(CMapItemAutoMapperConfig) + sizeof(int);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	ASSERT_EQ(Resources->m_vAutoMapperConfigs.size(), 1u);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs[0].m_AutomapperConfig, 7);
}

TEST_F(EditorMapAutomapper, CorrectedCandidateCanBePreparedAfterRejection)
{
	SItems Items;
	Items.m_vConfigs[0].m_Size = sizeof(int);
	ASSERT_NO_FATAL_FAILURE(Load(Items));
	ASSERT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
	m_pMap->Unload();
	ASSERT_NO_FATAL_FAILURE(Load({}));
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}

TEST_F(EditorMapAutomapper, PreparationAllocationFailureReturnsNoItemsAndCanRetry)
{
	ASSERT_NO_FATAL_FAILURE(Load({}));
	m_pMap->m_Failure = CPreparationFailureMap::EFailure::ALLOCATION;
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
	m_pMap->m_Failure = CPreparationFailureMap::EFailure::NONE;
	const auto Resources = EditorMapLoad::ReadResourceItems(*m_pMap);
	ASSERT_TRUE(Resources);
	EXPECT_EQ(Resources->m_vAutoMapperConfigs.size(), 1u);
}

TEST_F(EditorMapAutomapper, PreparationCapacityFailureReturnsNoItemsAndCanRetry)
{
	ASSERT_NO_FATAL_FAILURE(Load({}));
	m_pMap->m_Failure = CPreparationFailureMap::EFailure::CAPACITY;
	EXPECT_FALSE(EditorMapLoad::ReadResourceItems(*m_pMap));
	m_pMap->m_Failure = CPreparationFailureMap::EFailure::NONE;
	EXPECT_TRUE(EditorMapLoad::ReadResourceItems(*m_pMap));
}
