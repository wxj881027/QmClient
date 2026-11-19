#include <engine/shared/map.h>
#include <engine/storage.h>

#include <game/map/render_layer.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <utility>

namespace
{
	class CTileReadOperations : public CDataFileReader::IOperations
	{
	public:
		bool m_FailNextAllocation = false;

		void *Allocate(size_t Size) noexcept override
		{
			if(std::exchange(m_FailNextAllocation, false))
				return nullptr;
			return IOperations::Allocate(Size);
		}
	};

	template<typename TLayer>
	class MapTileLayerReadIntegration : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CTileReadOperations m_Read;
		CMap m_Map;
		CMapItemLayerTilemap m_Item{};

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "tile-layer-read.map"));
			const CSpeedupTile EmptyTile{};
			const int DataIndex = Writer.AddData(sizeof(EmptyTile), &EmptyTile);
			ASSERT_GE(DataIndex, 0);
			ASSERT_TRUE(Writer.Finish());
			ASSERT_TRUE(m_Map.GetReader()->Open(m_pStorage.get(), "tile-layer-read.map", IStorage::TYPE_SAVE, nullptr, &m_Read));

			// 每次只创建一种图层，读取同一个全零记录。
			m_Item.m_Width = m_Item.m_Height = 1;
			m_Item.m_Image = -1;
			m_Item.m_Data = m_Item.m_Front = m_Item.m_Tele = m_Item.m_Speedup = m_Item.m_Switch = m_Item.m_Tune = DataIndex;
		}

		bool LayerIsValid()
		{
			auto pEnvelopeManager = std::make_shared<CEnvelopeManager>(nullptr, &m_Map);
			TLayer Layer(0, 0, 0, &m_Item);
			Layer.OnInit(nullptr, nullptr, nullptr, pEnvelopeManager, &m_Map, nullptr, true);
			return Layer.IsValid();
		}
	};

	using CTileLayerTypes = ::testing::Types<CRenderLayerTile, CRenderLayerEntityGame, CRenderLayerEntityFront,
		CRenderLayerEntityTele, CRenderLayerEntitySpeedup, CRenderLayerEntitySwitch, CRenderLayerEntityTune>;
	TYPED_TEST_SUITE(MapTileLayerReadIntegration, CTileLayerTypes);
}

TYPED_TEST(MapTileLayerReadIntegration, RejectsUnreadableLayerWithDeclaredDataSize)
{
	ASSERT_GT(this->m_Map.GetDataSize(this->m_Item.m_Data), 0);
	this->m_Read.m_FailNextAllocation = true;

	EXPECT_FALSE(this->LayerIsValid());
	EXPECT_FALSE(this->m_Read.m_FailNextAllocation);
}

TYPED_TEST(MapTileLayerReadIntegration, AcceptsLayerAfterReadingItsTiles)
{
	EXPECT_TRUE(this->LayerIsValid());
}
