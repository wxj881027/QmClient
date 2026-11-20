#include <engine/shared/datafile.h>
#include <engine/shared/map.h>

#include <game/map/render_layer.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
	enum class ELayerKind
	{
		TILE,
		QUAD,
	};

	class CLayerGraphics : public CIconBenchmarkGraphics
	{
	public:
		enum class EContainerFailure
		{
			NONE,
			ALLOCATION,
			LENGTH,
			REJECT,
		};
		EContainerFailure m_ContainerFailure = EContainerFailure::NONE;
		bool m_FailObjectDelete = false, m_FailContainerDelete = false, m_FailIndexNotification = false;
		int m_ObjectCreates = 0, m_ContainerCreates = 0, m_ObjectDeletes = 0, m_ContainerDeletes = 0;
		bool m_DestroyAll = false;

		bool IsTileBufferingEnabled() override { return true; }
		bool IsQuadBufferingEnabled() override { return true; }
		int CreateBufferObject(size_t Size, void *pData, int Flags, bool Moved) override
		{
			++m_ObjectCreates;
			return CIconBenchmarkGraphics::CreateBufferObject(Size, pData, Flags, Moved);
		}
		int CreateBufferContainer(SBufferContainerInfo *pInfo) override
		{
			++m_ContainerCreates;
			// 仅替换设备创建边界；上传数据和属性数组均由真实图层准备。
			switch(std::exchange(m_ContainerFailure, EContainerFailure::NONE))
			{
			case EContainerFailure::ALLOCATION: throw std::bad_alloc();
			case EContainerFailure::LENGTH: throw std::length_error("container command limit");
			case EContainerFailure::REJECT: return -1;
			case EContainerFailure::NONE: return CIconBenchmarkGraphics::CreateBufferContainer(pInfo);
			}
			throw std::logic_error("unexpected container failure");
		}
		void IndicesNumRequiredNotify(unsigned int Count) override
		{
			if(std::exchange(m_FailIndexNotification, false))
				throw std::bad_alloc();
			CIconBenchmarkGraphics::IndicesNumRequiredNotify(Count);
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
			m_DestroyAll = DestroyAll;
			CIconBenchmarkGraphics::DeleteBufferContainer(Index, DestroyAll);
		}
	};

	class CLayerImages : public IMapImages
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

	class MapLayerBufferOwnershipIntegration : public ::testing::TestWithParam<ELayerKind>
	{
	protected:
		CLayerGraphics m_Graphics;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CMap m_Map;
		CLayerImages m_Images;
		CMapItemLayerTilemap m_TileItem{};
		CMapItemLayerQuads m_QuadItem{};
		std::shared_ptr<CEnvelopeManager> m_pEnvelopes;
		std::unique_ptr<CRenderLayer> m_pLayer;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "layer-buffer.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);
			CTile Tile{};
			Tile.m_Index = 1;
			CQuad Quad{};
			Quad.m_PosEnv = Quad.m_ColorEnv = -1;
			m_TileItem.m_Data = Writer.AddData(sizeof(Tile), &Tile);
			m_QuadItem.m_Data = Writer.AddData(sizeof(Quad), &Quad);
			ASSERT_TRUE(Writer.Finish());
			// 不经过地图规则校验，只打开隔离 datafile 供真实数据与 envelope 访问器读取。
			ASSERT_TRUE(m_Map.GetReader()->Open(m_pStorage.get(), "layer-buffer.map", IStorage::TYPE_SAVE));
			m_TileItem.m_Width = m_TileItem.m_Height = 1;
			m_TileItem.m_Image = -1;
			m_QuadItem.m_NumQuads = 1;
			m_QuadItem.m_Image = -1;
			m_pEnvelopes = std::make_shared<CEnvelopeManager>(nullptr, &m_Map);
			if(GetParam() == ELayerKind::TILE)
				m_pLayer = std::make_unique<CRenderLayerTile>(0, 0, 0, &m_TileItem);
			else
				m_pLayer = std::make_unique<CRenderLayerQuads>(0, 0, 0, &m_QuadItem);
			m_pLayer->OnInit(&m_Graphics, nullptr, nullptr, m_pEnvelopes, &m_Map, &m_Images, true);
		}
		void TearDown() override
		{
			if(m_pLayer)
				m_pLayer->Unload();
		}
		void Prepare()
		{
			auto Task = m_pLayer->Prepare();
			Task.Finish();
		}
	};
}

TEST_P(MapLayerBufferOwnershipIntegration, ContainerAllocationFailureKeepsTheCreatedObjectForUnload)
{
	m_Graphics.m_ContainerFailure = CLayerGraphics::EContainerFailure::ALLOCATION;
	EXPECT_THROW(Prepare(), std::bad_alloc);
	ASSERT_EQ(m_Graphics.m_Buffers.size(), 1U);
	EXPECT_EQ(m_Graphics.m_ObjectCreates, 1);
	EXPECT_EQ(m_Graphics.m_ContainerCreates, 1);
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	m_pLayer->Unload();
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 1);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 0);
}

TEST_P(MapLayerBufferOwnershipIntegration, ContainerLengthFailureKeepsTheCreatedObjectForUnload)
{
	m_Graphics.m_ContainerFailure = CLayerGraphics::EContainerFailure::LENGTH;
	EXPECT_THROW(Prepare(), std::length_error);
	ASSERT_EQ(m_Graphics.m_Buffers.size(), 1U);
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	m_pLayer->Unload();
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 1);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 0);
}

TEST_P(MapLayerBufferOwnershipIntegration, RejectedContainerStillAllowsTheBareObjectToBeUnloaded)
{
	m_Graphics.m_ContainerFailure = CLayerGraphics::EContainerFailure::REJECT;
	EXPECT_NO_THROW(Prepare());
	ASSERT_EQ(m_Graphics.m_Buffers.size(), 1U);
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	m_pLayer->Unload();
	m_pLayer->Unload();
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 1);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 0);
}

TEST_P(MapLayerBufferOwnershipIntegration, SuccessfulContainerReleasesItsObjectExactlyOnce)
{
	Prepare();
	ASSERT_EQ(m_Graphics.m_Buffers.size(), 1U);
	ASSERT_EQ(m_Graphics.m_Containers.size(), 1U);
	EXPECT_GT(m_Graphics.m_BufferBytes, 0U);
	m_pLayer->Unload();
	m_pLayer->Unload();
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	EXPECT_TRUE(m_Graphics.m_DestroyAll);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 1);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 0);
}

TEST_P(MapLayerBufferOwnershipIntegration, FailureAfterContainerCreationStillReleasesTheCompletePair)
{
	m_Graphics.m_FailIndexNotification = true;
	EXPECT_THROW(Prepare(), std::bad_alloc);
	ASSERT_EQ(m_Graphics.m_Buffers.size(), 1U);
	ASSERT_EQ(m_Graphics.m_Containers.size(), 1U);
	m_pLayer->Unload();
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 1);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 0);
}

TEST_P(MapLayerBufferOwnershipIntegration, BareObjectDeletionFailureKeepsItsIdentityForTheNextUnload)
{
	m_Graphics.m_ContainerFailure = CLayerGraphics::EContainerFailure::ALLOCATION;
	EXPECT_THROW(Prepare(), std::bad_alloc);
	ASSERT_EQ(m_Graphics.m_Buffers.size(), 1U);
	m_Graphics.m_FailObjectDelete = true;
	EXPECT_THROW(m_pLayer->Unload(), std::bad_alloc);
	EXPECT_EQ(m_Graphics.m_Buffers.size(), 1U);
	m_pLayer->Unload();
	m_pLayer->Unload();
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 2);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 0);
}

TEST_P(MapLayerBufferOwnershipIntegration, ContainerDeletionFailureKeepsBothIdentitiesForTheNextUnload)
{
	Prepare();
	ASSERT_EQ(m_Graphics.m_Buffers.size(), 1U);
	ASSERT_EQ(m_Graphics.m_Containers.size(), 1U);
	m_Graphics.m_FailContainerDelete = true;
	EXPECT_THROW(m_pLayer->Unload(), std::bad_alloc);
	EXPECT_EQ(m_Graphics.m_Buffers.size(), 1U);
	EXPECT_EQ(m_Graphics.m_Containers.size(), 1U);
	m_pLayer->Unload();
	m_pLayer->Unload();
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 2);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 0);
}

TEST_P(MapLayerBufferOwnershipIntegration, EmptyUploadDoesNotCreateOrDeleteGpuResources)
{
	static_cast<CTile *>(m_Map.GetData(m_TileItem.m_Data))->m_Index = 0;
	m_QuadItem.m_NumQuads = 0;
	Prepare();
	m_pLayer->Unload();
	EXPECT_TRUE(m_Graphics.m_Buffers.empty());
	EXPECT_TRUE(m_Graphics.m_Containers.empty());
	EXPECT_EQ(m_Graphics.m_ObjectCreates, 0);
	EXPECT_EQ(m_Graphics.m_ContainerCreates, 0);
	EXPECT_EQ(m_Graphics.m_ObjectDeletes, 0);
	EXPECT_EQ(m_Graphics.m_ContainerDeletes, 0);
}

INSTANTIATE_TEST_SUITE_P(LayerKinds, MapLayerBufferOwnershipIntegration,
	::testing::Values(ELayerKind::TILE, ELayerKind::QUAD),
	[](const ::testing::TestParamInfo<ELayerKind> &Info) {
		return Info.param == ELayerKind::TILE ? "Tile" : "Quad";
	});
