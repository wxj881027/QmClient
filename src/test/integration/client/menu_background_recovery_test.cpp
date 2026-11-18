#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/client/components/qmclient/menu_background_prepare.h>
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
	class CRecoveryGraphics : public CIconBenchmarkGraphics
	{
	public:
		enum class EFailure
		{
			NONE,
			ALLOCATION,
			LENGTH,
		};
		EFailure m_ContainerFailure = EFailure::NONE;
		bool m_FailObjectDelete = false;
		int m_ObjectCreates = 0, m_ObjectDeletes = 0, m_ContainerDeletes = 0;
		int m_FailContainerDeleteAt = 0;

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
			case EFailure::LENGTH: throw std::length_error("menu background container command limit");
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
			if(m_ContainerDeletes == m_FailContainerDeleteAt)
				throw std::bad_alloc();
			CIconBenchmarkGraphics::DeleteBufferContainer(Index, DestroyAll);
		}
	};

	// 图片来源是外部边界；回收顺序与 owner 交接使用生产函数。
	class CRecoveryImages : public IMapImages
	{
		CRecoveryGraphics &m_Graphics;
		int &m_UnloadCalls;
		size_t &m_LiveBuffersAtUnload;

	public:
		bool m_FailUnload = false;
		CRecoveryImages(CRecoveryGraphics &Graphics, int &UnloadCalls, size_t &LiveBuffersAtUnload) :
			m_Graphics(Graphics), m_UnloadCalls(UnloadCalls), m_LiveBuffersAtUnload(LiveBuffersAtUnload) {}
		void Unload()
		{
			++m_UnloadCalls;
			m_LiveBuffersAtUnload = m_Graphics.m_Buffers.size();
			if(std::exchange(m_FailUnload, false))
				throw std::bad_alloc();
		}
		IGraphics::CTextureHandle Get(int) const override { return {}; }
		int Num() const override { return 0; }
		IGraphics::CTextureHandle GetEntities(EMapImageEntityLayerType) override { return {}; }
		IGraphics::CTextureHandle GetSpeedupArrow() override { return {}; }
		IGraphics::CTextureHandle GetTuneColors() override { return {}; }
		IGraphics::CTextureHandle GetOverlayBottom() override { return {}; }
		IGraphics::CTextureHandle GetOverlayTop() override { return {}; }
		IGraphics::CTextureHandle GetOverlayCenter() override { return {}; }
	};

	class CRecoveryRenderer
	{
	public:
		CMapRenderer m_Renderer;
		int m_AdvanceCalls = 0;
		bool AdvanceMapLoad(int MaxLayers = 2)
		{
			++m_AdvanceCalls;
			m_Renderer.LoadStep(MaxLayers);
			return m_Renderer.IsLoadFinished();
		}
		void UnloadMap() { m_Renderer.Clear(); }
	};

	class MenuBackgroundRecoveryIntegration : public ::testing::Test
	{
	protected:
		using SBackground = SMenuBackgroundPrepared<CMap, CLayers, CRecoveryImages, CRecoveryRenderer>;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CRecoveryGraphics m_Graphics;
		int m_ImageUnloadCalls = 0;
		size_t m_LiveBuffersAtImageUnload = 0;
		CMenuBackgroundMapPreparation m_Preparation;
		std::unique_ptr<SBackground> m_pCurrent;
		std::unique_ptr<SBackground> m_pPrevious;
		bool m_PreviousRetiring = false;
		int m_ReleasePreviousCalls = 0, m_PreserveCurrentCalls = 0, m_DiscardCurrentCalls = 0;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(CreateBackground("current.map", m_pCurrent));
		}
		void TearDown() override
		{
			m_Graphics.m_FailObjectDelete = false;
			m_Graphics.m_FailContainerDeleteAt = 0;
			if(m_pCurrent)
				m_pCurrent->m_pImages->m_FailUnload = false;
			if(m_pPrevious)
				m_pPrevious->m_pImages->m_FailUnload = false;
			bool CurrentRetiring = false;
			EXPECT_TRUE(ReleaseMenuBackgroundResources(m_pCurrent, CurrentRetiring));
			EXPECT_TRUE(ReleaseMenuBackgroundResources(m_pPrevious, m_PreviousRetiring));
		}
		bool CreateBackground(const char *pName, std::unique_ptr<SBackground> &pBackground)
		{
			CDataFileWriter Writer;
			if(!Writer.Open(m_pStorage.get(), pName))
				return false;
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
			if(!Writer.Finish())
				return false;
			auto pCandidate = std::make_unique<SBackground>();
			pCandidate->m_pMap = std::make_unique<CMap>();
			pCandidate->m_pLayers = std::make_unique<CLayers>();
			pCandidate->m_pImages = std::make_unique<CRecoveryImages>(m_Graphics, m_ImageUnloadCalls, m_LiveBuffersAtImageUnload);
			pCandidate->m_pRenderer = std::make_unique<CRecoveryRenderer>();
			// 真实 datafile、图层与 renderer；不链接客户端或替换图层准备算法。
			if(!pCandidate->m_pMap->GetReader()->Open(m_pStorage.get(), pName, IStorage::TYPE_SAVE))
				return false;
			pCandidate->m_pLayers->Init(pCandidate->m_pMap.get(), false, false);
			pCandidate->m_pRenderer->m_Renderer.OnInit(&m_Graphics, nullptr, nullptr);
			pCandidate->m_pRenderer->m_Renderer.BeginLoad(ERenderType::RENDERTYPE_FULL_DESIGN,
				pCandidate->m_pLayers.get(), pCandidate->m_pImages.get(), nullptr);
			pBackground = std::move(pCandidate);
			return true;
		}
		bool Finish(SBackground &Background)
		{
			CMenuBackgroundMapPreparation Preparation;
			bool Ready = false;
			for(int Step = 0; Step < 4 && !Ready; ++Step)
				Ready = Preparation.Advance(*Background.m_pRenderer);
			return Ready;
		}
		bool PreparePrevious()
		{
			return CreateBackground("previous.map", m_pPrevious) && Finish(*m_pPrevious);
		}
		bool Advance() { return m_Preparation.Advance(*m_pCurrent->m_pRenderer); }
		bool PrepareChange(bool CurrentReady)
		{
			return PrepareMenuBackgroundChange(CurrentReady, m_PreviousRetiring,
				[&] {
					++m_ReleasePreviousCalls;
					return ReleaseMenuBackgroundResources(m_pPrevious, m_PreviousRetiring);
				},
				[&] {
					++m_PreserveCurrentCalls;
					return true;
				},
				[&] {
					++m_DiscardCurrentCalls;
					return m_Preparation.Cancel(*m_pCurrent->m_pRenderer);
				});
		}
	};
}

TEST_F(MenuBackgroundRecoveryIntegration, NormalPreparationKeepsTheExistingTwoLayerBudget)
{
	EXPECT_FALSE(Advance());
	EXPECT_EQ(m_Graphics.m_ObjectCreates, 2);
	EXPECT_TRUE(Advance());
	EXPECT_EQ(m_Graphics.m_ObjectCreates, 2);
	EXPECT_FALSE(m_Preparation.HasFailed());
}

TEST_F(MenuBackgroundRecoveryIntegration, FailedCandidateStopsResumeAndKeepsThePreparedPreviousBackground)
{
	ASSERT_TRUE(PreparePrevious());
	const auto PreviousBuffers = m_Graphics.m_Buffers;
	const auto PreviousContainers = m_Graphics.m_Containers;
	m_Graphics.m_ContainerFailure = CRecoveryGraphics::EFailure::ALLOCATION;
	EXPECT_FALSE(Advance());
	EXPECT_TRUE(m_Preparation.HasFailed());
	EXPECT_TRUE(CanRenderMenuBackgroundPrevious(m_pPrevious, m_PreviousRetiring));
	EXPECT_EQ(m_Graphics.m_Buffers, PreviousBuffers);
	EXPECT_EQ(m_Graphics.m_Containers, PreviousContainers);
	EXPECT_FALSE(Advance());
	EXPECT_EQ(m_pCurrent->m_pRenderer->m_AdvanceCalls, 1);
	EXPECT_TRUE(m_pPrevious->m_pRenderer->AdvanceMapLoad());
}

TEST_F(MenuBackgroundRecoveryIntegration, FirstBackgroundLengthFailureKeepsTheExistingNoPreviousFallback)
{
	m_Graphics.m_ContainerFailure = CRecoveryGraphics::EFailure::LENGTH;
	EXPECT_FALSE(Advance());
	EXPECT_TRUE(m_Preparation.HasFailed());
	EXPECT_FALSE(CanRenderMenuBackgroundPrevious(m_pPrevious, m_PreviousRetiring));
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	EXPECT_FALSE(Advance());
	EXPECT_EQ(m_pCurrent->m_pRenderer->m_AdvanceCalls, 1);
}

TEST_F(MenuBackgroundRecoveryIntegration, FailedCurrentCleanupBlocksReplacementAndRetainsBothOwners)
{
	ASSERT_TRUE(PreparePrevious());
	auto *pCurrent = m_pCurrent.get();
	auto *pPrevious = m_pPrevious.get();
	m_Graphics.m_ContainerFailure = CRecoveryGraphics::EFailure::ALLOCATION;
	m_Graphics.m_FailObjectDelete = true;
	EXPECT_FALSE(Advance());
	m_Graphics.m_FailObjectDelete = true;
	EXPECT_FALSE(PrepareChange(false));
	EXPECT_EQ(m_pCurrent.get(), pCurrent);
	EXPECT_EQ(m_pPrevious.get(), pPrevious);
	EXPECT_EQ(m_ReleasePreviousCalls, 0);
	EXPECT_EQ(m_PreserveCurrentCalls, 0);
	EXPECT_EQ(m_DiscardCurrentCalls, 1);
	EXPECT_TRUE(CanRenderMenuBackgroundPrevious(m_pPrevious, m_PreviousRetiring));
	EXPECT_FALSE(Advance());
	EXPECT_EQ(m_pCurrent->m_pRenderer->m_AdvanceCalls, 1);
	EXPECT_TRUE(PrepareChange(false));
}

TEST_F(MenuBackgroundRecoveryIntegration, SwitchingAgainDuringPreparationKeepsTheCompletePreviousOwner)
{
	ASSERT_TRUE(PreparePrevious());
	auto *pPrevious = m_pPrevious.get();
	const auto PreviousBuffers = m_Graphics.m_Buffers;
	EXPECT_FALSE(Advance());
	EXPECT_TRUE(PrepareChange(false));
	EXPECT_EQ(m_pPrevious.get(), pPrevious);
	EXPECT_EQ(m_ReleasePreviousCalls, 0);
	EXPECT_EQ(m_PreserveCurrentCalls, 0);
	EXPECT_EQ(m_DiscardCurrentCalls, 1);
	EXPECT_EQ(m_Graphics.m_Buffers, PreviousBuffers);
	EXPECT_TRUE(CanRenderMenuBackgroundPrevious(m_pPrevious, m_PreviousRetiring));
}

TEST_F(MenuBackgroundRecoveryIntegration, PartialPreviousDeletionKeepsItsOwnerAndRejectsPromotionOfCurrent)
{
	ASSERT_TRUE(PreparePrevious());
	ASSERT_TRUE(Finish(*m_pCurrent));
	auto *pPrevious = m_pPrevious.get();
	m_Graphics.m_FailContainerDeleteAt = m_Graphics.m_ContainerDeletes + 2;
	EXPECT_FALSE(PrepareChange(true));
	EXPECT_EQ(m_pPrevious.get(), pPrevious);
	EXPECT_TRUE(m_PreviousRetiring);
	EXPECT_TRUE(m_pPrevious->m_pMap->IsLoaded());
	EXPECT_FALSE(CanRenderMenuBackgroundPrevious(m_pPrevious, m_PreviousRetiring));
	EXPECT_EQ(m_PreserveCurrentCalls, 0);
	EXPECT_EQ(m_DiscardCurrentCalls, 0);
	EXPECT_EQ(m_ImageUnloadCalls, 0);
	EXPECT_EQ(m_Graphics.m_Buffers.size(), 3U);
	EXPECT_TRUE(m_pCurrent->m_pRenderer->AdvanceMapLoad());
	EXPECT_TRUE(ReleaseMenuBackgroundResources(m_pPrevious, m_PreviousRetiring));
	EXPECT_EQ(m_Graphics.m_Buffers.size(), 2U);
}

TEST_F(MenuBackgroundRecoveryIntegration, ImageDeletionFailureRetainsSourcesAfterRendererCleanupUntilRetry)
{
	ASSERT_TRUE(PreparePrevious());
	m_pPrevious->m_pImages->m_FailUnload = true;
	EXPECT_FALSE(ReleaseMenuBackgroundResources(m_pPrevious, m_PreviousRetiring));
	ASSERT_NE(m_pPrevious, nullptr);
	EXPECT_TRUE(m_PreviousRetiring);
	EXPECT_TRUE(m_pPrevious->m_pMap->IsLoaded());
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_EQ(m_ImageUnloadCalls, 1);
	const int ContainerDeletes = m_Graphics.m_ContainerDeletes;
	EXPECT_TRUE(ReleaseMenuBackgroundResources(m_pPrevious, m_PreviousRetiring));
	EXPECT_EQ(m_pPrevious, nullptr);
	EXPECT_FALSE(m_PreviousRetiring);
	EXPECT_EQ(m_ImageUnloadCalls, 2);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, ContainerDeletes);
}

TEST_F(MenuBackgroundRecoveryIntegration, PreviousReleaseClearsBuffersBeforeImagesAndDoesNotDeleteObjectsTwice)
{
	ASSERT_TRUE(PreparePrevious());
	EXPECT_TRUE(ReleaseMenuBackgroundResources(m_pPrevious, m_PreviousRetiring));
	EXPECT_EQ(m_pPrevious, nullptr);
	EXPECT_FALSE(m_PreviousRetiring);
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	EXPECT_EQ(m_LiveBuffersAtImageUnload, 0U);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 2);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 0);
	EXPECT_TRUE(ReleaseMenuBackgroundResources(m_pPrevious, m_PreviousRetiring));
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 2);
}

TEST_F(MenuBackgroundRecoveryIntegration, RetiringPreviousBlocksAnUnreadyCurrentFromBeingDiscardedOrOverwritten)
{
	ASSERT_TRUE(PreparePrevious());
	m_Graphics.m_FailContainerDeleteAt = m_Graphics.m_ContainerDeletes + 2;
	ASSERT_FALSE(ReleaseMenuBackgroundResources(m_pPrevious, m_PreviousRetiring));
	auto *pCurrent = m_pCurrent.get();
	auto *pPrevious = m_pPrevious.get();
	m_Graphics.m_FailContainerDeleteAt = m_Graphics.m_ContainerDeletes + 1;
	EXPECT_FALSE(PrepareChange(false));
	EXPECT_EQ(m_pCurrent.get(), pCurrent);
	EXPECT_EQ(m_pPrevious.get(), pPrevious);
	EXPECT_EQ(m_ReleasePreviousCalls, 1);
	EXPECT_EQ(m_PreserveCurrentCalls, 0);
	EXPECT_EQ(m_DiscardCurrentCalls, 0);
	EXPECT_TRUE(m_PreviousRetiring);
}

TEST_F(MenuBackgroundRecoveryIntegration, ExplicitRestartAfterCleanupPreparesANewTaskInsteadOfResumingTheFailedOne)
{
	m_Graphics.m_ContainerFailure = CRecoveryGraphics::EFailure::ALLOCATION;
	EXPECT_FALSE(Advance());
	ASSERT_TRUE(m_Preparation.HasFailed());
	ASSERT_TRUE(PrepareChange(false));
	m_pCurrent->m_pRenderer->m_Renderer.BeginLoad(ERenderType::RENDERTYPE_FULL_DESIGN,
		m_pCurrent->m_pLayers.get(), m_pCurrent->m_pImages.get(), nullptr);
	m_Preparation.Reset();
	EXPECT_FALSE(Advance());
	EXPECT_TRUE(Advance());
	EXPECT_FALSE(m_Preparation.HasFailed());
	EXPECT_EQ(m_Graphics.m_Buffers.size(), 2U);
	EXPECT_EQ(m_Graphics.m_Containers.size(), 2U);
}
