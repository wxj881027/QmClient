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
