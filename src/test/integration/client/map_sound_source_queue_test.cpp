#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/client/components/map_sound_source_queue.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <array>
#include <climits>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <vector>

namespace
{
	struct SAllocationState
	{
		bool m_Fail = false;
		size_t m_MaxAllocation = std::numeric_limits<size_t>::max();
		size_t m_MaxSize = std::numeric_limits<size_t>::max();
		size_t m_Live = 0;
	};

	template<typename T>
	struct CFailingAllocator
	{
		using value_type = T;
		SAllocationState *m_pState;
		explicit CFailingAllocator(SAllocationState *pState) :
			m_pState(pState) {}
		template<typename U>
		CFailingAllocator(const CFailingAllocator<U> &Other) :
			m_pState(Other.m_pState)
		{
		}
		T *allocate(size_t Count)
		{
			// 仅注入来源条目缓冲；容器通过 rebind 申请的辅助存储不计入。
			if constexpr(std::is_same_v<T, SMapSoundSource>)
			{
				if(m_pState->m_Fail || Count > m_pState->m_MaxAllocation)
					throw std::bad_alloc();
			}
			T *pResult = std::allocator<T>{}.allocate(Count);
			if constexpr(std::is_same_v<T, SMapSoundSource>)
				m_pState->m_Live += Count;
			return pResult;
		}
		void deallocate(T *pData, size_t Count)
		{
			if constexpr(std::is_same_v<T, SMapSoundSource>)
				m_pState->m_Live -= Count;
			std::allocator<T>{}.deallocate(pData, Count);
		}
		size_t max_size() const noexcept
		{
			if constexpr(std::is_same_v<T, SMapSoundSource>)
				return std::min(m_pState->m_MaxSize, std::numeric_limits<size_t>::max() / sizeof(T));
			return std::numeric_limits<size_t>::max() / sizeof(T);
		}
		template<typename U>
		bool operator==(const CFailingAllocator<U> &Other) const
		{
			return m_pState == Other.m_pState;
		}
	};
	using CTestQueue = std::vector<SMapSoundSource, CFailingAllocator<SMapSoundSource>>;

	CSoundSource SourceAt(int X)
	{
		CSoundSource Source{};
		Source.m_Position.x = X;
		return Source;
	}

	class MapSoundSourceQueue : public ::testing::Test
	{
	protected:
		struct SLayer
		{
			int m_Kind = LAYERTYPE_SOUNDS;
			int m_Version = 2;
			int m_Sound = 0;
			int m_Flags = 0;
			std::optional<int> m_DeclaredSources;
			bool m_InvalidData = false;
			std::vector<CSoundSource> m_vSources{SourceAt(100)};
		};
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CMap> m_pMap;
		CLayers m_Layers;
		std::array<int, 2> m_aSounds{17, 23};

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pMap = std::make_unique<CMap>(m_pStorage.get());
		}

		// 用真实文件、CMap 和 CLayers；解码后的样本 ID 是唯一外部输入。
		void Load(const std::vector<SLayer> &vLayers, bool SeparateGroups = false)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "map_sound_queue.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);
			for(int i = 0; i < 2; ++i)
			{
				CMapItemSound Sound{};
				Sound.m_Version = 1;
				Sound.m_SoundName = Writer.AddDataString("sound");
				const std::array<unsigned char, 4> aBytes{};
				Sound.m_SoundData = Writer.AddData(aBytes.size(), aBytes.data());
				Sound.m_SoundDataSize = static_cast<int>(aBytes.size());
				Writer.AddItem(MAPITEMTYPE_SOUND, i, sizeof(Sound), &Sound);
			}
			for(size_t i = 0; i < vLayers.size(); ++i)
			{
				const auto &Input = vLayers[i];
				CMapItemLayerSounds Layer{};
				Layer.m_Layer.m_Type = Input.m_Kind;
				Layer.m_Layer.m_Flags = Input.m_Flags;
				Layer.m_Version = Input.m_Version;
				Layer.m_Sound = Input.m_Sound;
				Layer.m_NumSources = Input.m_DeclaredSources.value_or(static_cast<int>(Input.m_vSources.size()));
				Layer.m_Data = Input.m_vSources.empty() ? -1 : Writer.AddDataSwapped(Input.m_vSources.size() * sizeof(CSoundSource), Input.m_vSources.data());
				if(Input.m_InvalidData)
					Layer.m_Data = INT_MAX;
				Writer.AddItem(MAPITEMTYPE_LAYER, static_cast<int>(i), sizeof(Layer), &Layer);
			}
			CMapItemLayerTilemap Game{};
			Game.m_Layer.m_Type = LAYERTYPE_TILES;
			Game.m_Version = 4;
			Game.m_Width = Game.m_Height = 1;
			Game.m_Flags = TILESLAYERFLAG_GAME;
			Game.m_Color = CColor(255, 255, 255, 255);
			Game.m_ColorEnv = Game.m_Image = -1;
			const CTile Tile{};
			Game.m_Data = Writer.AddData(sizeof(Tile), &Tile);
			Game.m_Tele = Game.m_Speedup = Game.m_Front = Game.m_Switch = Game.m_Tune = -1;
			Writer.AddItem(MAPITEMTYPE_LAYER, static_cast<int>(vLayers.size()), sizeof(Game), &Game);
			const int Groups = SeparateGroups ? static_cast<int>(vLayers.size()) : 1;
			for(int i = 0; i < Groups; ++i)
			{
				CMapItemGroup Group{};
				Group.m_Version = 3;
				Group.m_ParallaxX = Group.m_ParallaxY = 100;
				Group.m_StartLayer = SeparateGroups ? i : 0;
				Group.m_NumLayers = SeparateGroups ? 1 : static_cast<int>(vLayers.size());
				Writer.AddItem(MAPITEMTYPE_GROUP, i, sizeof(Group), &Group);
			}
			CMapItemGroup GameGroup{};
			GameGroup.m_Version = 3;
			GameGroup.m_ParallaxX = GameGroup.m_ParallaxY = 100;
			GameGroup.m_StartLayer = static_cast<int>(vLayers.size());
			GameGroup.m_NumLayers = 1;
			Writer.AddItem(MAPITEMTYPE_GROUP, Groups, sizeof(GameGroup), &GameGroup);
			Writer.Finish();
			ASSERT_TRUE(m_pMap->Load("map_sound_queue.map", IStorage::TYPE_SAVE));
			m_Layers.Init(m_pMap.get(), false);
		}
	};
}

TEST_F(MapSoundSourceQueue, MapWithoutSoundLayersPublishesNoSources)
{
	Load({});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_TRUE(vQueue.empty());
}

TEST_F(MapSoundSourceQueue, LayersAndSourcesKeepTheirOrder)
{
	SLayer First, Second;
	First.m_vSources = {SourceAt(10), SourceAt(20)};
	Second.m_Sound = 1;
	Second.m_vSources = {SourceAt(30)};
	Load({First, Second});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	ASSERT_EQ(vQueue.size(), 3u);
	EXPECT_EQ(vQueue[0].m_pSource->m_Position.x, 10);
	EXPECT_EQ(vQueue[1].m_pSource->m_Position.x, 20);
	EXPECT_EQ(vQueue[2].m_pSource->m_Position.x, 30);
	EXPECT_EQ(vQueue[2].m_Sound, 1);
}

TEST_F(MapSoundSourceQueue, GroupsKeepTheirOrderAndBorrowedIdentity)
{
	SLayer First, Second;
	Second.m_vSources = {SourceAt(200)};
	Load({First, Second}, true);
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	ASSERT_EQ(vQueue.size(), 2u);
	EXPECT_EQ(vQueue[0].m_pGroup, m_Layers.GetGroup(0));
	EXPECT_EQ(vQueue[1].m_pGroup, m_Layers.GetGroup(1));
	EXPECT_EQ(vQueue[0].m_pSource->m_Position.x, 100);
	EXPECT_EQ(vQueue[1].m_pSource->m_Position.x, 200);
}

TEST_F(MapSoundSourceQueue, EntriesBorrowActualMapSourceDataAndStartWithoutVoices)
{
	Load({SLayer{}});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	ASSERT_EQ(vQueue.size(), 1u);
	const auto Layer = MapRenderItems::ReadSoundLayer(*m_pMap, m_pMap->FindItemIndex(MAPITEMTYPE_LAYER, 0));
	ASSERT_TRUE(Layer);
	const auto Data = MapRenderItems::ReadDataItems<CSoundSource>(*m_pMap, Layer->m_Data, 1);
	ASSERT_EQ(Data.size(), 1u);
	EXPECT_EQ(vQueue[0].m_pSource, Data.data());
	EXPECT_FALSE(vQueue[0].m_Voice.IsValid());
}

TEST_F(MapSoundSourceQueue, DetailFlagIsCopiedPerLayer)
{
	SLayer Detailed, Plain;
	Detailed.m_Flags = LAYERFLAG_DETAIL;
	Load({Detailed, Plain});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	ASSERT_EQ(vQueue.size(), 2u);
	EXPECT_TRUE(vQueue[0].m_HighDetail);
	EXPECT_FALSE(vQueue[1].m_HighDetail);
}

TEST_F(MapSoundSourceQueue, FailedSampleSkipsOnlyItsLayers)
{
	SLayer First, Second;
	Second.m_Sound = 1;
	Load({First, Second});
	m_aSounds[0] = -1;
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	ASSERT_EQ(vQueue.size(), 1u);
	EXPECT_EQ(vQueue[0].m_Sound, 1);
}

TEST_F(MapSoundSourceQueue, InvalidMapSoundIndicesAreSkipped)
{
	SLayer Negative, Outside;
	Negative.m_Sound = -1;
	Outside.m_Sound = static_cast<int>(m_aSounds.size());
	Load({Negative, Outside});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_TRUE(vQueue.empty());
}

TEST_F(MapSoundSourceQueue, UnsupportedVersionsAreSkipped)
{
	SLayer Old, Future;
	Old.m_Version = 0;
	Future.m_Version = 3;
	Load({Old, Future});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_TRUE(vQueue.empty());
}

TEST_F(MapSoundSourceQueue, DeprecatedLayersAreSkipped)
{
	SLayer Deprecated;
	Deprecated.m_Kind = LAYERTYPE_SOUNDS_DEPRECATED;
	Load({Deprecated});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_TRUE(vQueue.empty());
}

TEST_F(MapSoundSourceQueue, VersionOneSourcesStillLoad)
{
	SLayer Layer;
	Layer.m_Version = 1;
	Load({Layer});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	ASSERT_EQ(vQueue.size(), 1u);
	EXPECT_EQ(vQueue[0].m_pSource->m_Position.x, 100);
}

TEST_F(MapSoundSourceQueue, TruncatedSourceArrayKeepsAvailablePrefix)
{
	SLayer Layer;
	Layer.m_DeclaredSources = 5;
	Layer.m_vSources = {SourceAt(10), SourceAt(20)};
	Load({Layer});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	ASSERT_EQ(vQueue.size(), 2u);
	EXPECT_EQ(vQueue.back().m_pSource->m_Position.x, 20);
}

TEST_F(MapSoundSourceQueue, ZeroCountPublishesNoSources)
{
	SLayer Empty;
	Empty.m_DeclaredSources = 0;
	Load({Empty});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_TRUE(vQueue.empty());
}

TEST_F(MapSoundSourceQueue, InvalidDataPublishesNoSources)
{
	SLayer Invalid;
	Invalid.m_InvalidData = true;
	Load({Invalid});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_TRUE(vQueue.empty());
}

TEST_F(MapSoundSourceQueue, NegativeCountPublishesNoSources)
{
	SLayer Layer;
	Layer.m_DeclaredSources = -1;
	Load({Layer});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_TRUE(vQueue.empty());
}

TEST_F(MapSoundSourceQueue, PreparingAgainReplacesInsteadOfAppending)
{
	Load({SLayer{}});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_EQ(vQueue.size(), 1u);
}

TEST_F(MapSoundSourceQueue, NoLoadedSamplesReplacesPreviousQueueWithEmpty)
{
	Load({SLayer{}});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	ASSERT_EQ(vQueue.size(), 1u);
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, {}, vQueue));
	EXPECT_TRUE(vQueue.empty());
}

TEST_F(MapSoundSourceQueue, UninitializedLayersClearRetiredQueue)
{
	Load({SLayer{}});
	std::vector<SMapSoundSource> vQueue;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	ASSERT_EQ(vQueue.size(), 1u);
	CLayers Empty;
	EXPECT_FALSE(PrepareMapSoundSourceQueue(Empty, m_aSounds, vQueue));
	EXPECT_TRUE(vQueue.empty());
}

TEST_F(MapSoundSourceQueue, AllocationFailureLeavesEmptyOutputAndReleasesCandidate)
{
	Load({SLayer{}});
	SAllocationState State;
	State.m_Fail = true;
	CTestQueue vQueue{CFailingAllocator<SMapSoundSource>(&State)};
	EXPECT_FALSE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_TRUE(vQueue.empty());
	EXPECT_EQ(State.m_Live, 0u);
}

TEST_F(MapSoundSourceQueue, GrowthFailureDropsWholeCandidateAndReleasesStorage)
{
	SLayer Layer;
	Layer.m_vSources = {SourceAt(10), SourceAt(20), SourceAt(30)};
	Load({Layer});
	SAllocationState State;
	CTestQueue vQueue{CFailingAllocator<SMapSoundSource>(&State)};
	State.m_MaxAllocation = 1;
	EXPECT_FALSE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_TRUE(vQueue.empty());
	EXPECT_EQ(State.m_Live, 0u);
}

TEST_F(MapSoundSourceQueue, CapacityFailureDropsWholeCandidateAndReleasesStorage)
{
	SLayer Layer;
	Layer.m_vSources = {SourceAt(10), SourceAt(20), SourceAt(30)};
	Load({Layer});
	SAllocationState State;
	CTestQueue vQueue{CFailingAllocator<SMapSoundSource>(&State)};
	State.m_MaxSize = 2;
	EXPECT_FALSE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_TRUE(vQueue.empty());
	EXPECT_EQ(State.m_Live, 0u);
}

TEST_F(MapSoundSourceQueue, ExistingCapacityCanBeReusedWithoutAllocating)
{
	SLayer Layer;
	Layer.m_vSources = {SourceAt(10), SourceAt(20), SourceAt(30)};
	Load({Layer});
	SAllocationState State;
	CTestQueue vQueue{CFailingAllocator<SMapSoundSource>(&State)};
	vQueue.reserve(8);
	State.m_Fail = true;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	EXPECT_EQ(vQueue.size(), 3u);
	EXPECT_GE(vQueue.capacity(), 8u);
}

TEST_F(MapSoundSourceQueue, AllocationFailureCanBeRetried)
{
	Load({SLayer{}});
	SAllocationState State;
	State.m_Fail = true;
	CTestQueue vQueue{CFailingAllocator<SMapSoundSource>(&State)};
	EXPECT_FALSE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	State.m_Fail = false;
	ASSERT_TRUE(PrepareMapSoundSourceQueue(m_Layers, m_aSounds, vQueue));
	ASSERT_EQ(vQueue.size(), 1u);
	EXPECT_EQ(vQueue[0].m_pSource->m_Position.x, 100);
}
