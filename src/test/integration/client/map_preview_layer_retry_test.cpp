#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/client/components/qmclient/map_preview_panel.h>
#include <game/layers.h>
#include <game/map/map_renderer.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace
{
	class CPreviewPreparationGraphics : public CIconBenchmarkGraphics
	{
	public:
		enum class EFailure
		{
			NONE,
			ALLOCATION,
			LENGTH,
		};
		EFailure m_ContainerFailure = EFailure::NONE;
		bool m_FailObjectDelete = false, m_FailContainerDelete = false;
		int m_ObjectCreates = 0, m_ObjectDeletes = 0, m_ContainerDeletes = 0;
		EFailure m_TextureFailure = EFailure::NONE, m_TargetFailure = EFailure::NONE;
		int m_TextureDeletes = 0, m_TargetDeletes = 0;
		uint32_t m_ResetVersion = 0;
		bool m_BackendShutdown = false;
		std::unordered_set<int> m_Textures, m_Targets;

		uint32_t GraphicsResourcesResetVersion() const override
		{
			if(m_BackendShutdown)
				throw std::logic_error("graphics accessed after backend shutdown");
			return m_ResetVersion;
		}
		CTextureHandle MakeTexture()
		{
			const int Index = m_NextId++;
			m_Textures.insert(Index);
			return CreateTextureHandle(Index);
		}
		void UnloadTexture(CTextureHandle *pTexture) override
		{
			if(!pTexture->IsValid())
				return;
			if(m_BackendShutdown || !m_Textures.contains(pTexture->Id()))
				throw std::logic_error("texture deleted twice or after backend shutdown");
			++m_TextureDeletes;
			switch(std::exchange(m_TextureFailure, EFailure::NONE))
			{
			case EFailure::ALLOCATION: throw std::bad_alloc();
			case EFailure::LENGTH: throw std::length_error("preview texture delete limit");
			case EFailure::NONE: break;
			}
			m_Textures.erase(pTexture->Id());
			pTexture->Invalidate();
		}
		CRenderTargetHandle CreateRenderTarget(int, int) override
		{
			const int Index = m_NextId++;
			m_Targets.insert(Index);
			return CreateRenderTargetHandle(Index);
		}
		void DestroyRenderTarget(CRenderTargetHandle *pTarget) override
		{
			if(!pTarget->IsValid())
				return;
			if(m_BackendShutdown || !m_Targets.contains(pTarget->Id()))
				throw std::logic_error("render target deleted twice or after backend shutdown");
			++m_TargetDeletes;
			switch(std::exchange(m_TargetFailure, EFailure::NONE))
			{
			case EFailure::ALLOCATION: throw std::bad_alloc();
			case EFailure::LENGTH: throw std::length_error("preview target delete limit");
			case EFailure::NONE: break;
			}
			m_Targets.erase(pTarget->Id());
			pTarget->Invalidate();
		}

		bool IsTileBufferingEnabled() override { return true; }
		bool IsQuadBufferingEnabled() override { return true; }
		int CreateBufferObject(size_t Size, void *pData, int Flags, bool Moved) override
		{
			++m_ObjectCreates;
			return CIconBenchmarkGraphics::CreateBufferObject(Size, pData, Flags, Moved);
		}
		int CreateBufferContainer(SBufferContainerInfo *pInfo) override
		{
			switch(std::exchange(m_ContainerFailure, EFailure::NONE))
			{
			case EFailure::ALLOCATION: throw std::bad_alloc();
			case EFailure::LENGTH: throw std::length_error("preview container command limit");
			case EFailure::NONE: return CIconBenchmarkGraphics::CreateBufferContainer(pInfo);
			}
			throw std::logic_error("unexpected failure");
		}
		void DeleteBufferObject(int Index) override
		{
			++m_ObjectDeletes;
			if(!m_Buffers.contains(Index))
				throw std::logic_error("buffer object deleted twice");
			if(std::exchange(m_FailObjectDelete, false))
				throw std::bad_alloc();
			CIconBenchmarkGraphics::DeleteBufferObject(Index);
		}
		void DeleteBufferContainer(int &Index, bool DestroyAll) override
		{
			++m_ContainerDeletes;
			if(!m_Containers.contains(Index))
				throw std::logic_error("buffer container deleted twice");
			if(std::exchange(m_FailContainerDelete, false))
				throw std::bad_alloc();
			CIconBenchmarkGraphics::DeleteBufferContainer(Index, DestroyAll);
		}
	};

	class CPreviewPreparationImages : public IMapImages
	{
	public:
		IGraphics::CTextureHandle Get(int) const override { return {}; }
		int Num() const override { return 0; }
		IGraphics::CTextureHandle GetEntities(EMapImageEntityLayerType) override { return {}; }
		IGraphics::CTextureHandle GetSpeedupArrow() override { return {}; }
		IGraphics::CTextureHandle GetTuneColors() override { return {}; }
		IGraphics::CTextureHandle GetOverlayBottom() override { return {}; }
		IGraphics::CTextureHandle GetOverlayTop() override { return {}; }
		IGraphics::CTextureHandle GetOverlayCenter() override { return {}; }
	};

	// 不接整套客户端，只把面板调用的两个方法接到共享 renderer 的真实加载与清理入口。
	class CPreviewPreparationRenderer
	{
	public:
		CMapRenderer m_Renderer;
		bool AdvanceMapLoad(int MaxLayers)
		{
			m_Renderer.LoadStep(MaxLayers);
			return m_Renderer.IsLoadFinished();
		}
		void UnloadMap() { m_Renderer.Clear(); }
	};

	// 图片只替换外部资源边界；缓存清理、退役与腾位均调用面板生产入口。
	class CPreviewCacheImages : public CPreviewPreparationImages
	{
		CPreviewPreparationGraphics &m_Graphics;

	public:
		IGraphics::CTextureHandle m_Texture;
		explicit CPreviewCacheImages(CPreviewPreparationGraphics &Graphics) :
			m_Graphics(Graphics), m_Texture(Graphics.MakeTexture()) {}
		void UnloadPreview() { m_Graphics.UnloadTexture(&m_Texture); }
	};

	struct SPreviewCacheObservation
	{
		bool m_Cancelled = false;
		int m_Destroyed = 0;
	};

	struct SPreviewCacheEntry
	{
		char m_aMapName[MAX_MAP_LENGTH] = {};
		std::unique_ptr<CMap> m_pMap;
		std::unique_ptr<CLayers> m_pLayers;
		std::unique_ptr<CPreviewCacheImages> m_pImages;
		std::unique_ptr<CPreviewPreparationRenderer> m_pRenderer;
		IGraphics::CRenderTargetHandle m_RenderTarget;
		uint32_t m_GraphicsResetVersion = 0;
		bool m_Retiring = false, m_LoadFailed = false;
		float m_LastUseTime = 0.0f, m_RetryAfterTime = 0.0f;
		std::shared_ptr<SPreviewCacheObservation> m_pObservation = std::make_shared<SPreviewCacheObservation>();

		~SPreviewCacheEntry() { ++m_pObservation->m_Destroyed; }
		void CancelLoad() noexcept { m_pObservation->m_Cancelled = true; }
	};

	class MapPreviewLayerRetryIntegration : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CMap m_Map;
		CLayers m_Layers;
		CPreviewPreparationGraphics m_Graphics;
		CPreviewPreparationImages m_Images;
		CQmMapPreviewLayerPreparation m_Preparation;
		std::unique_ptr<CPreviewPreparationRenderer> m_pRenderer;
		std::unique_ptr<CPreviewPreparationRenderer> m_pCompleteRenderer;
		int m_RetryFactoryCalls = 0;
		CQmMapPreviewCacheEntries<SPreviewCacheEntry> m_Cache{&m_Graphics};

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "preview-layer-retry.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);
			CMapItemGroup Group{};
			Group.m_Version = 3;
			Group.m_NumLayers = 2;
			Group.m_ParallaxX = Group.m_ParallaxY = 100;
			Writer.AddItem(MAPITEMTYPE_GROUP, 0, sizeof(Group), &Group);
			CTile Tile{};
			Tile.m_Index = 1;
			CMapItemLayerTilemap Layer{};
			Layer.m_Layer.m_Type = LAYERTYPE_TILES;
			Layer.m_Version = 3;
			Layer.m_Width = Layer.m_Height = 1;
			Layer.m_Image = -1;
			Layer.m_Data = Writer.AddData(sizeof(Tile), &Tile);
			Writer.AddItem(MAPITEMTYPE_LAYER, 0, sizeof(Layer), &Layer);
			Writer.AddItem(MAPITEMTYPE_LAYER, 1, sizeof(Layer), &Layer);
			ASSERT_TRUE(Writer.Finish());
			// 只打开隔离 datafile；图层遍历、GPU 数据准备与协程由真实实现负责。
			ASSERT_TRUE(m_Map.GetReader()->Open(m_pStorage.get(), "preview-layer-retry.map", IStorage::TYPE_SAVE));
			m_Layers.Init(&m_Map, false, false);
			m_pRenderer = CreateRenderer();
		}
		void TearDown() override
		{
			m_Cache.Clear();
			if(m_pRenderer)
				m_pRenderer->UnloadMap();
			if(m_pCompleteRenderer)
				m_pCompleteRenderer->UnloadMap();
		}
		std::unique_ptr<CPreviewPreparationRenderer> CreateRenderer()
		{
			auto pRenderer = std::make_unique<CPreviewPreparationRenderer>();
			pRenderer->m_Renderer.OnInit(&m_Graphics, nullptr, nullptr);
			pRenderer->m_Renderer.BeginLoad(ERenderType::RENDERTYPE_FULL_DESIGN, &m_Layers, &m_Images, nullptr);
			return pRenderer;
		}
		std::unique_ptr<SPreviewCacheEntry> CreateCacheEntry(const char *pName, float LastUse)
		{
			auto pEntry = std::make_unique<SPreviewCacheEntry>();
			str_copy(pEntry->m_aMapName, pName);
			pEntry->m_LastUseTime = LastUse;
			pEntry->m_GraphicsResetVersion = m_Graphics.m_ResetVersion;
			pEntry->m_pMap = std::make_unique<CMap>();
			if(!pEntry->m_pMap->GetReader()->Open(m_pStorage.get(), "preview-layer-retry.map", IStorage::TYPE_SAVE))
			{
				ADD_FAILURE() << "Cannot reopen isolated preview map";
				return nullptr;
			}
			pEntry->m_pLayers = std::make_unique<CLayers>();
			pEntry->m_pLayers->Init(pEntry->m_pMap.get(), false, false);
			pEntry->m_pImages = std::make_unique<CPreviewCacheImages>(m_Graphics);
			pEntry->m_pRenderer = std::make_unique<CPreviewPreparationRenderer>();
			pEntry->m_pRenderer->m_Renderer.OnInit(&m_Graphics, nullptr, nullptr);
			pEntry->m_pRenderer->m_Renderer.BeginLoad(ERenderType::RENDERTYPE_FULL_DESIGN, pEntry->m_pLayers.get(), pEntry->m_pImages.get(), nullptr);
			bool Ready = false;
			for(int Step = 0; Step < 4 && !Ready; ++Step)
				Ready = pEntry->m_pRenderer->AdvanceMapLoad(1);
			EXPECT_TRUE(Ready);
			pEntry->m_RenderTarget = m_Graphics.CreateRenderTarget(128, 32);
			return pEntry;
		}
		bool Advance(float Now) { return m_Preparation.Advance(m_pRenderer, Now); }
		void Retry(float Now)
		{
			m_Preparation.Retry(m_pRenderer, Now, [&] {
				++m_RetryFactoryCalls;
				return CreateRenderer();
			});
		}
		void FinishPreparation(float Now)
		{
			bool Ready = false;
			for(int Step = 0; Step < 4 && !Ready; ++Step)
				Ready = Advance(Now);
			ASSERT_TRUE(Ready);
		}
	};
}

TEST_F(MapPreviewLayerRetryIntegration, NormalPreparationKeepsTheOneLayerPerCallBudget)
{
	EXPECT_FALSE(Advance(0.0f));
	EXPECT_EQ(m_Graphics.m_ObjectCreates, 1);
	EXPECT_FALSE(Advance(0.0f));
	EXPECT_EQ(m_Graphics.m_ObjectCreates, 2);
	EXPECT_TRUE(Advance(0.0f));
	EXPECT_EQ(m_Graphics.m_ObjectCreates, 2);
	EXPECT_FALSE(m_Preparation.HasFailed());
}

TEST_F(MapPreviewLayerRetryIntegration, AllocationFailureReleasesPartialResourcesAndRetriesAfterTwoSeconds)
{
	m_Graphics.m_ContainerFailure = CPreviewPreparationGraphics::EFailure::ALLOCATION;
	EXPECT_FALSE(Advance(10.0f));
	EXPECT_TRUE(m_Preparation.HasFailed());
	EXPECT_EQ(m_pRenderer, nullptr);
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	Retry(11.99f);
	EXPECT_EQ(m_RetryFactoryCalls, 0);
	Retry(12.0f);
	EXPECT_EQ(m_RetryFactoryCalls, 1);
	ASSERT_NE(m_pRenderer, nullptr);
	EXPECT_FALSE(m_Preparation.HasFailed());
	FinishPreparation(12.0f);
}

TEST_F(MapPreviewLayerRetryIntegration, LengthFailureUsesTheSameCleanupAndRetryDeadline)
{
	m_Graphics.m_ContainerFailure = CPreviewPreparationGraphics::EFailure::LENGTH;
	EXPECT_FALSE(Advance(3.0f));
	EXPECT_TRUE(m_Preparation.HasFailed());
	EXPECT_EQ(m_pRenderer, nullptr);
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	Retry(4.99f);
	EXPECT_EQ(m_RetryFactoryCalls, 0);
	Retry(5.0f);
	EXPECT_EQ(m_RetryFactoryCalls, 1);
	FinishPreparation(5.0f);
}

TEST_F(MapPreviewLayerRetryIntegration, ObjectDeletionFailureRetainsTheRendererUntilTheNextCleanupSucceeds)
{
	m_Graphics.m_ContainerFailure = CPreviewPreparationGraphics::EFailure::ALLOCATION;
	m_Graphics.m_FailObjectDelete = true;
	EXPECT_FALSE(Advance(0.0f));
	ASSERT_NE(m_pRenderer, nullptr);
	EXPECT_EQ(m_Graphics.m_Buffers.size(), 1U);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 1);
	EXPECT_FALSE(Advance(1.0f));
	Retry(1.99f);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 1);
	EXPECT_EQ(m_RetryFactoryCalls, 0);
	Retry(2.0f);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 2);
	EXPECT_EQ(m_RetryFactoryCalls, 1);
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	FinishPreparation(2.0f);
}

TEST_F(MapPreviewLayerRetryIntegration, RepeatedDeletionFailureDefersCreationForAnotherTwoSeconds)
{
	m_Graphics.m_ContainerFailure = CPreviewPreparationGraphics::EFailure::ALLOCATION;
	m_Graphics.m_FailObjectDelete = true;
	EXPECT_FALSE(Advance(0.0f));
	m_Graphics.m_FailObjectDelete = true;
	Retry(2.0f);
	ASSERT_NE(m_pRenderer, nullptr);
	EXPECT_TRUE(m_Preparation.HasFailed());
	EXPECT_EQ(m_RetryFactoryCalls, 0);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 2);
	Retry(3.99f);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 2);
	Retry(4.0f);
	EXPECT_EQ(m_RetryFactoryCalls, 1);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 3);
	FinishPreparation(4.0f);
}

TEST_F(MapPreviewLayerRetryIntegration, ContainerDeletionFailureRetainsAnEarlierPreparedLayerForCleanupRetry)
{
	EXPECT_FALSE(Advance(0.0f));
	m_Graphics.m_ContainerFailure = CPreviewPreparationGraphics::EFailure::ALLOCATION;
	m_Graphics.m_FailContainerDelete = true;
	EXPECT_FALSE(Advance(0.25f));
	ASSERT_NE(m_pRenderer, nullptr);
	EXPECT_EQ(m_Graphics.m_Buffers.size(), 1U);
	EXPECT_EQ(m_Graphics.m_Containers.size(), 1U);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 1);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 1);
	Retry(2.25f);
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 2);
	EXPECT_EQ(m_RetryFactoryCalls, 1);
	FinishPreparation(2.25f);
}

TEST_F(MapPreviewLayerRetryIntegration, CandidateAllocationFailureKeepsSourcesAvailableForTheNextRetry)
{
	m_Graphics.m_ContainerFailure = CPreviewPreparationGraphics::EFailure::ALLOCATION;
	EXPECT_FALSE(Advance(0.0f));
	m_Preparation.Retry(m_pRenderer, 2.0f, [&]() -> std::unique_ptr<CPreviewPreparationRenderer> {
		++m_RetryFactoryCalls;
		throw std::bad_alloc();
	});
	EXPECT_EQ(m_RetryFactoryCalls, 1);
	EXPECT_EQ(m_pRenderer, nullptr);
	EXPECT_TRUE(m_Preparation.HasFailed());
	EXPECT_EQ(m_Layers.Map(), &m_Map);
	EXPECT_TRUE(m_Map.IsLoaded());
	Retry(3.99f);
	EXPECT_EQ(m_RetryFactoryCalls, 1);
	Retry(4.0f);
	EXPECT_EQ(m_RetryFactoryCalls, 2);
	FinishPreparation(4.0f);
}

TEST_F(MapPreviewLayerRetryIntegration, FailedRequestedRendererKeepsTheAlreadyCompleteRendererAndItsBuffers)
{
	m_pCompleteRenderer = CreateRenderer();
	CQmMapPreviewLayerPreparation CompletePreparation;
	bool Complete = false;
	for(int Step = 0; Step < 4 && !Complete; ++Step)
		Complete = CompletePreparation.Advance(m_pCompleteRenderer, 0.0f);
	ASSERT_TRUE(Complete);
	const auto CompleteBuffers = m_Graphics.m_Buffers;
	const auto CompleteContainers = m_Graphics.m_Containers;
	m_Graphics.m_ContainerFailure = CPreviewPreparationGraphics::EFailure::ALLOCATION;
	m_Graphics.m_FailObjectDelete = true;
	EXPECT_FALSE(Advance(0.0f));
	EXPECT_EQ(m_Graphics.m_Buffers.size(), CompleteBuffers.size() + 1);
	EXPECT_TRUE(CompletePreparation.Advance(m_pCompleteRenderer, 1.0f));
	Retry(2.0f);
	EXPECT_EQ(m_Graphics.m_Buffers, CompleteBuffers);
	EXPECT_EQ(m_Graphics.m_Containers, CompleteContainers);
	EXPECT_TRUE(CompletePreparation.Advance(m_pCompleteRenderer, 2.0f));
	FinishPreparation(2.0f);
}

TEST_F(MapPreviewLayerRetryIntegration, CompletePreparationDoesNotRebuildTheRendererOnLaterAcquire)
{
	FinishPreparation(0.0f);
	auto *pComplete = m_pRenderer.get();
	const int ObjectCreates = m_Graphics.m_ObjectCreates;
	Retry(100.0f);
	EXPECT_EQ(m_pRenderer.get(), pComplete);
	EXPECT_EQ(m_RetryFactoryCalls, 0);
	EXPECT_TRUE(Advance(100.0f));
	EXPECT_EQ(m_Graphics.m_ObjectCreates, ObjectCreates);
}

TEST_F(MapPreviewLayerRetryIntegration, FailedLruCleanupRetainsTheOwnerAndOtherRecentCompleteEntries)
{
	auto *pOld = m_Cache.Insert(CreateCacheEntry("old", 0.0f));
	auto *pRecent = m_Cache.Insert(CreateCacheEntry("recent", 0.8f));
	auto *pNewest = m_Cache.Insert(CreateCacheEntry("newest", 0.9f));
	const auto Buffers = m_Graphics.m_Buffers;
	const auto Containers = m_Graphics.m_Containers;
	const auto pObservation = pOld->m_pObservation;
	m_Graphics.m_FailContainerDelete = true;
	EXPECT_FALSE(m_Cache.MakeRoom(1.0f));
	EXPECT_EQ(m_Cache.Size(), 3U);
	EXPECT_EQ(m_Cache.Find("old"), pOld);
	EXPECT_TRUE(pOld->m_Retiring);
	EXPECT_TRUE(CQmMapPreviewCacheEntries<SPreviewCacheEntry>::IsInitializing(pOld));
	EXPECT_TRUE(CQmMapPreviewCacheEntries<SPreviewCacheEntry>::IsInitializing(nullptr));
	EXPECT_TRUE(pObservation->m_Cancelled);
	EXPECT_EQ(pObservation->m_Destroyed, 0);
	EXPECT_TRUE(pOld->m_pMap->IsLoaded());
	EXPECT_EQ(m_Graphics.m_Buffers, Buffers);
	EXPECT_EQ(m_Graphics.m_Containers, Containers);
	EXPECT_FALSE(CQmMapPreviewCacheEntries<SPreviewCacheEntry>::IsInitializing(pRecent));
	EXPECT_TRUE(pRecent->m_pRenderer->AdvanceMapLoad(1));
	EXPECT_TRUE(pNewest->m_pRenderer->AdvanceMapLoad(1));
	EXPECT_TRUE(m_Cache.MakeRoom(1.01f));
	EXPECT_EQ(m_Cache.Size(), 2U);
	EXPECT_EQ(m_Cache.Find("old"), nullptr);
	EXPECT_EQ(pObservation->m_Destroyed, 1);
	EXPECT_EQ(m_Cache.Find("recent"), pRecent);
	EXPECT_EQ(m_Cache.Find("newest"), pNewest);
	EXPECT_EQ(m_Graphics.m_Buffers.size(), 4U);
	EXPECT_EQ(m_Graphics.m_Textures.size(), 2U);
	EXPECT_EQ(m_Graphics.m_Targets.size(), 2U);
}

TEST_F(MapPreviewLayerRetryIntegration, FailedOldestCleanupCanUseAnotherEligibleSlotWithoutDroppingTheFailedOwner)
{
	auto *pOld = m_Cache.Insert(CreateCacheEntry("old", 0.0f));
	m_Cache.Insert(CreateCacheEntry("middle", 0.1f));
	auto *pNewest = m_Cache.Insert(CreateCacheEntry("newest", 0.2f));
	m_Graphics.m_FailContainerDelete = true;
	EXPECT_TRUE(m_Cache.MakeRoom(1.0f));
	EXPECT_EQ(m_Cache.Size(), 2U);
	EXPECT_EQ(m_Cache.Find("old"), pOld);
	EXPECT_TRUE(pOld->m_Retiring);
	EXPECT_EQ(m_Cache.Find("middle"), nullptr);
	EXPECT_EQ(m_Cache.Find("newest"), pNewest);
	EXPECT_TRUE(pNewest->m_pRenderer->AdvanceMapLoad(1));
}

TEST_F(MapPreviewLayerRetryIntegration, RetiringAFailedLayerKeepsItsBareBufferUntilDeletionCanRetry)
{
	auto *pEntry = m_Cache.Insert(CreateCacheEntry("partial", 0.0f));
	pEntry->m_pRenderer->m_Renderer.BeginLoad(ERenderType::RENDERTYPE_FULL_DESIGN, pEntry->m_pLayers.get(), pEntry->m_pImages.get(), nullptr);
	CQmMapPreviewLayerPreparation Preparation;
	m_Graphics.m_ContainerFailure = CPreviewPreparationGraphics::EFailure::ALLOCATION;
	m_Graphics.m_FailObjectDelete = true;
	EXPECT_FALSE(Preparation.Advance(pEntry->m_pRenderer, 0.0f));
	ASSERT_NE(pEntry->m_pRenderer, nullptr);
	ASSERT_EQ(m_Graphics.m_Buffers.size(), 1U);
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	m_Graphics.m_FailObjectDelete = true;
	EXPECT_FALSE(m_Cache.Clear());
	EXPECT_TRUE(pEntry->m_Retiring);
	EXPECT_TRUE(pEntry->m_pMap->IsLoaded());
	EXPECT_EQ(m_Graphics.m_Buffers.size(), 1U);
	EXPECT_EQ(m_Graphics.m_Textures.size(), 1U);
	EXPECT_EQ(m_Graphics.m_Targets.size(), 1U);
	EXPECT_TRUE(m_Cache.Clear());
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_TRUE(m_Graphics.m_Textures.empty());
	EXPECT_TRUE(m_Graphics.m_Targets.empty());
}

TEST_F(MapPreviewLayerRetryIntegration, ImageDeletionFailureRetainsSourcesAndDoesNotRepeatAcceptedBufferDeletes)
{
	auto *pEntry = m_Cache.Insert(CreateCacheEntry("image-failure", 0.0f));
	auto *pRenderer = pEntry->m_pRenderer.get();
	auto *pImages = pEntry->m_pImages.get();
	m_Graphics.m_TextureFailure = CPreviewPreparationGraphics::EFailure::LENGTH;
	EXPECT_FALSE(m_Cache.Release(*pEntry));
	EXPECT_TRUE(pEntry->m_Retiring);
	EXPECT_EQ(pEntry->m_pRenderer.get(), pRenderer);
	EXPECT_EQ(pEntry->m_pImages.get(), pImages);
	EXPECT_TRUE(pEntry->m_pMap->IsLoaded());
	EXPECT_EQ(pEntry->m_pLayers->Map(), pEntry->m_pMap.get());
	EXPECT_TRUE(pEntry->m_RenderTarget.IsValid());
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	EXPECT_EQ(m_Graphics.m_Textures.size(), 1U);
	const int ContainerDeletes = m_Graphics.m_ContainerDeletes;
	EXPECT_TRUE(m_Cache.Release(*pEntry));
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, ContainerDeletes);
	EXPECT_EQ(m_Graphics.m_TextureDeletes, 2);
	EXPECT_EQ(m_Graphics.m_TargetDeletes, 1);
	EXPECT_FALSE(pEntry->m_pMap->IsLoaded());
	EXPECT_TRUE(m_Cache.Clear());
	EXPECT_EQ(m_Graphics.m_TextureDeletes, 2);
}

TEST_F(MapPreviewLayerRetryIntegration, TargetDeletionFailureRetainsTheEntryUntilClearCanFinish)
{
	auto *pEntry = m_Cache.Insert(CreateCacheEntry("target-failure", 0.0f));
	const auto pObservation = pEntry->m_pObservation;
	m_Graphics.m_TargetFailure = CPreviewPreparationGraphics::EFailure::ALLOCATION;
	EXPECT_FALSE(m_Cache.Clear());
	ASSERT_EQ(m_Cache.Find("target-failure"), pEntry);
	EXPECT_TRUE(pEntry->m_Retiring);
	EXPECT_TRUE(pEntry->m_pMap->IsLoaded());
	EXPECT_NE(pEntry->m_pImages, nullptr);
	EXPECT_FALSE(pEntry->m_pImages->m_Texture.IsValid());
	EXPECT_TRUE(pEntry->m_RenderTarget.IsValid());
	EXPECT_EQ(pObservation->m_Destroyed, 0);
	EXPECT_TRUE(m_Cache.Clear());
	EXPECT_EQ(pObservation->m_Destroyed, 1);
	EXPECT_EQ(m_Graphics.m_TargetDeletes, 2);
	EXPECT_EQ(m_Graphics.m_TextureDeletes, 1);
	EXPECT_TRUE(m_Graphics.m_Targets.empty());
}

TEST_F(MapPreviewLayerRetryIntegration, ClearRetainsOnlyTheFailedOwnerAndStillReleasesOtherEntries)
{
	m_Cache.Insert(CreateCacheEntry("first", 0.0f));
	m_Cache.Insert(CreateCacheEntry("second", 0.1f));
	auto *pFailed = m_Cache.Insert(CreateCacheEntry("failed", 0.2f));
	const auto pObservation = pFailed->m_pObservation;
	m_Graphics.m_FailContainerDelete = true;
	EXPECT_FALSE(m_Cache.Clear());
	EXPECT_EQ(m_Cache.Size(), 1U);
	EXPECT_EQ(m_Cache.Find("failed"), pFailed);
	EXPECT_TRUE(pFailed->m_Retiring);
	EXPECT_TRUE(pFailed->m_pMap->IsLoaded());
	EXPECT_EQ(pObservation->m_Destroyed, 0);
	EXPECT_EQ(m_Graphics.m_Buffers.size(), 2U);
	EXPECT_EQ(m_Graphics.m_Textures.size(), 1U);
	EXPECT_EQ(m_Graphics.m_Targets.size(), 1U);
	EXPECT_TRUE(m_Cache.Clear());
	EXPECT_EQ(pObservation->m_Destroyed, 1);
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_TRUE(m_Graphics.m_Textures.empty());
	EXPECT_TRUE(m_Graphics.m_Targets.empty());
}

TEST_F(MapPreviewLayerRetryIntegration, RecentFullCacheWaitsWithoutRetiringACompletePreview)
{
	auto *pFirst = m_Cache.Insert(CreateCacheEntry("first", 0.0f));
	auto *pSecond = m_Cache.Insert(CreateCacheEntry("second", 0.1f));
	auto *pThird = m_Cache.Insert(CreateCacheEntry("third", 0.2f));
	EXPECT_FALSE(m_Cache.MakeRoom(0.3f));
	EXPECT_EQ(m_Cache.Size(), 3U);
	EXPECT_FALSE(pFirst->m_Retiring);
	EXPECT_FALSE(pSecond->m_Retiring);
	EXPECT_FALSE(pThird->m_Retiring);
	EXPECT_FALSE(pFirst->m_LoadFailed);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 0);
	EXPECT_EQ(m_Graphics.m_TextureDeletes, 0);
	EXPECT_EQ(m_Graphics.m_TargetDeletes, 0);
}

TEST_F(MapPreviewLayerRetryIntegration, DownloadRecheckOnlyWakesTheMatchingMissingEntry)
{
	auto *pMissing = m_Cache.Insert(CreateCacheEntry("missing", 0.0f));
	auto *pOtherMissing = m_Cache.Insert(CreateCacheEntry("other-missing", 0.1f));
	auto *pComplete = m_Cache.Insert(CreateCacheEntry("complete", 0.2f));
	pMissing->m_LoadFailed = pOtherMissing->m_LoadFailed = true;
	pMissing->m_RetryAfterTime = pOtherMissing->m_RetryAfterTime = pComplete->m_RetryAfterTime = 10.0f;
	const auto Buffers = m_Graphics.m_Buffers;
	m_Cache.RetryMissing("missing");
	EXPECT_EQ(pMissing->m_RetryAfterTime, 0.0f);
	EXPECT_EQ(pOtherMissing->m_RetryAfterTime, 10.0f);
	EXPECT_EQ(pComplete->m_RetryAfterTime, 10.0f);
	EXPECT_EQ(m_Graphics.m_Buffers, Buffers);
	EXPECT_EQ(m_Cache.Size(), 3U);
	EXPECT_TRUE(pComplete->m_pRenderer->AdvanceMapLoad(1));
	m_Cache.RetryMissing("unknown");
	m_Cache.RetryMissing("complete");
	EXPECT_EQ(pComplete->m_RetryAfterTime, 10.0f);
	pMissing->m_Retiring = true;
	pMissing->m_RetryAfterTime = 20.0f;
	m_Cache.RetryMissing("missing");
	EXPECT_EQ(pMissing->m_RetryAfterTime, 20.0f);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 0);
}

TEST_F(MapPreviewLayerRetryIntegration, CacheTouchKeepsTheRecentlyUsedEntryOutOfTheNextEviction)
{
	auto *pFirst = m_Cache.Insert(CreateCacheEntry("first", 0.0f));
	m_Cache.Insert(CreateCacheEntry("second", 0.1f));
	auto *pThird = m_Cache.Insert(CreateCacheEntry("third", 0.2f));
	m_Cache.Touch(*pFirst, 0.9f);
	EXPECT_TRUE(m_Cache.MakeRoom(1.0f));
	EXPECT_EQ(m_Cache.Find("first"), pFirst);
	EXPECT_EQ(m_Cache.Find("second"), nullptr);
	EXPECT_EQ(m_Cache.Find("third"), pThird);
	EXPECT_TRUE(pFirst->m_pRenderer->AdvanceMapLoad(1));
}

TEST_F(MapPreviewLayerRetryIntegration, GraphicsResetSkipsOldGpuDeletesAndKeepsNewGenerationResources)
{
	m_Cache.Insert(CreateCacheEntry("stale", 0.0f));
	++m_Graphics.m_ResetVersion;
	m_Graphics.m_Buffers.clear();
	m_Graphics.m_Containers.clear();
	m_Graphics.m_Textures.clear();
	m_Graphics.m_Targets.clear();
	m_Graphics.m_NextId = 1;
	m_pCompleteRenderer = CreateRenderer();
	bool Ready = false;
	for(int Step = 0; Step < 4 && !Ready; ++Step)
		Ready = m_pCompleteRenderer->AdvanceMapLoad(1);
	ASSERT_TRUE(Ready);
	const auto Buffers = m_Graphics.m_Buffers;
	const auto Containers = m_Graphics.m_Containers;
	EXPECT_TRUE(m_Cache.Clear());
	EXPECT_EQ(m_Graphics.m_Buffers, Buffers);
	EXPECT_EQ(m_Graphics.m_Containers, Containers);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 0);
	EXPECT_EQ(m_Graphics.m_TextureDeletes, 0);
	EXPECT_EQ(m_Graphics.m_TargetDeletes, 0);
	EXPECT_TRUE(m_pCompleteRenderer->AdvanceMapLoad(1));
}

TEST_F(MapPreviewLayerRetryIntegration, TerminalCacheDestructionDoesNotCallTheClosedGraphicsBackend)
{
	auto pCache = std::make_unique<CQmMapPreviewCacheEntries<SPreviewCacheEntry>>(&m_Graphics);
	auto *pEntry = pCache->Insert(CreateCacheEntry("terminal", 0.0f));
	const auto pObservation = pEntry->m_pObservation;
	m_Graphics.m_FailContainerDelete = true;
	EXPECT_FALSE(pCache->Clear());
	EXPECT_TRUE(pEntry->m_Retiring);
	EXPECT_EQ(pObservation->m_Destroyed, 0);
	const int ContainerDeletes = m_Graphics.m_ContainerDeletes;
	// 模拟 kernel 已关闭后端；剩余 GPU 对象由后端终态回收。
	m_Graphics.m_Buffers.clear();
	m_Graphics.m_Containers.clear();
	m_Graphics.m_Textures.clear();
	m_Graphics.m_Targets.clear();
	m_Graphics.m_BackendShutdown = true;
	EXPECT_NO_THROW(pCache.reset());
	EXPECT_TRUE(pObservation->m_Cancelled);
	EXPECT_EQ(pObservation->m_Destroyed, 1);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, ContainerDeletes);
	EXPECT_EQ(m_Graphics.m_TextureDeletes, 0);
	EXPECT_EQ(m_Graphics.m_TargetDeletes, 0);
}
