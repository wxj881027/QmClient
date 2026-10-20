#include <base/mem.h>

#include <game/map/map_render_items.h>

#include <gtest/gtest.h>

#include <array>
#include <climits>
#include <vector>

namespace
{
	// 仅替换地图存储边界；尺寸检查、类型检查与字段读取均使用生产接口。
	class CItemMap : public IMap
	{
	public:
		std::vector<uint8_t> m_vItem;
		int m_Type = MAPITEMTYPE_IMAGE;
		int m_ItemReads = 0;
		int m_DataReads = 0;
		int m_SwappedReads = 0;
		std::array<CQuad, 2> m_aData{};
		int m_DataSize = sizeof(m_aData);
		int m_SizeAfterDecode = -1;
		bool m_NullItem = false;
		bool m_DecodeFails = false;

		template<typename T>
		void SetItem(const T &Item, size_t Size, int Type)
		{
			m_vItem.resize(Size);
			mem_copy(m_vItem.data(), &Item, Size);
			m_Type = Type;
		}
		int GetDataSize(int) const override { return m_DataSize; }
		void *GetData(int) override
		{
			++m_DataReads;
			if(m_SizeAfterDecode >= 0)
				m_DataSize = m_SizeAfterDecode;
			return m_DecodeFails ? nullptr : m_aData.data();
		}
		void *GetDataSwapped(int Index) override
		{
			++m_SwappedReads;
			return GetData(Index);
		}
		const char *GetDataString(int) override { return nullptr; }
		void UnloadData(int) override {}
		int NumData() const override { return 1; }
		int GetItemSize(int) override { return static_cast<int>(m_vItem.size()); }
		void *GetItem(int, int *pType, int *pId) override
		{
			++m_ItemReads;
			if(pType)
				*pType = m_Type;
			if(pId)
				*pId = 0;
			return m_NullItem ? nullptr : m_vItem.data();
		}
		void GetType(int Type, int *pStart, int *pNum) override
		{
			*pStart = 0;
			*pNum = Type == m_Type ? 1 : 0;
		}
		int FindItemIndex(int Type, int Id) override { return Type == m_Type && Id == 0 ? 0 : -1; }
		void *FindItem(int Type, int Id) override { return FindItemIndex(Type, Id) == 0 ? GetItem(0, nullptr, nullptr) : nullptr; }
		int NumItems() const override { return 1; }
	};

	CMapItemImage_v2 Image(int Version = 1)
	{
		CMapItemImage_v2 Item{};
		Item.m_Version = Version;
		Item.m_Width = 32;
		Item.m_Height = 64;
		Item.m_External = 1;
		Item.m_ImageName = 5;
		Item.m_ImageData = 6;
		Item.m_MustBe1 = 0;
		return Item;
	}

	CMapItemLayerQuads Quads()
	{
		CMapItemLayerQuads Layer{};
		Layer.m_Layer.m_Type = LAYERTYPE_QUADS;
		Layer.m_Version = 1;
		Layer.m_NumQuads = 1;
		Layer.m_Data = 0;
		Layer.m_Image = 7;
		return Layer;
	}
}

TEST(MapRenderItemsImage, VersionOneCopiesOnlyBaseAndSuppliesCompatibleExtension)
{
	CItemMap Map;
	Map.SetItem(Image(), sizeof(CMapItemImage_v1), MAPITEMTYPE_IMAGE);
	const auto Item = MapRenderItems::ReadImage(Map, 0);
	ASSERT_TRUE(Item);
	EXPECT_EQ(Item->m_Width, 32);
	EXPECT_EQ(Item->m_Height, 64);
	EXPECT_EQ(Item->m_External, 1);
	EXPECT_EQ(Item->m_ImageName, 5);
	EXPECT_EQ(Item->m_ImageData, 6);
	EXPECT_EQ(Item->m_MustBe1, 1);
}

TEST(MapRenderItemsImage, VersionTwoRetainsItsActualTypeField)
{
	CItemMap Map;
	Map.SetItem(Image(2), sizeof(CMapItemImage_v2), MAPITEMTYPE_IMAGE);
	const auto Item = MapRenderItems::ReadImage(Map, 0);
	ASSERT_TRUE(Item);
	EXPECT_EQ(Item->m_Version, 2);
	EXPECT_EQ(Item->m_MustBe1, 0);
}

TEST(MapRenderItemsImage, TruncatedBaseIsRejectedBeforeItemLookup)
{
	CItemMap Map;
	Map.SetItem(Image(), sizeof(int), MAPITEMTYPE_IMAGE);
	EXPECT_FALSE(MapRenderItems::ReadImage(Map, 0));
	EXPECT_EQ(Map.m_ItemReads, 0);
}

TEST(MapRenderItemsImage, VersionTwoWithoutExtensionIsRejected)
{
	CItemMap Map;
	Map.SetItem(Image(2), sizeof(CMapItemImage_v1), MAPITEMTYPE_IMAGE);
	EXPECT_FALSE(MapRenderItems::ReadImage(Map, 0));
}

TEST(MapRenderItemsImage, WrongItemTypeIsNotInterpretedAsAnImage)
{
	CItemMap Map;
	Map.SetItem(Image(), sizeof(CMapItemImage_v2), MAPITEMTYPE_LAYER);
	EXPECT_FALSE(MapRenderItems::ReadImage(Map, 0));
}

TEST(MapRenderItemsImage, NullItemDoesNotProduceMetadata)
{
	CItemMap Map;
	Map.SetItem(Image(), sizeof(CMapItemImage_v1), MAPITEMTYPE_IMAGE);
	Map.m_NullItem = true;
	EXPECT_FALSE(MapRenderItems::ReadImage(Map, 0));
}

TEST(MapRenderItemsImage, SnapshotDoesNotBorrowTheItemStorage)
{
	CItemMap Map;
	Map.SetItem(Image(), sizeof(CMapItemImage_v1), MAPITEMTYPE_IMAGE);
	const auto Item = MapRenderItems::ReadImage(Map, 0);
	ASSERT_TRUE(Item);
	Map.SetItem(CMapItemImage_v2{}, sizeof(CMapItemImage_v2), MAPITEMTYPE_IMAGE);
	EXPECT_EQ(Item->m_Width, 32);
	EXPECT_EQ(Item->m_ImageName, 5);
}

TEST(MapRenderItemsQuad, InvalidItemIndicesDoNotReadItemStorage)
{
	CItemMap Map;
	for(int Index : {-1, 1, INT_MAX})
	{
		SCOPED_TRACE(Index);
		EXPECT_EQ(MapRenderItems::QuadLayer(Map, Index), nullptr);
		EXPECT_FALSE(MapRenderItems::ReadImage(Map, Index));
	}
	EXPECT_EQ(Map.m_ItemReads, 0);
}

TEST(MapRenderItemsQuad, FixedHeaderWithoutNameKeepsItsDataAndImageIndices)
{
	CItemMap Map;
	Map.SetItem(Quads(), offsetof(CMapItemLayerQuads, m_aName), MAPITEMTYPE_LAYER);
	const auto *pLayer = MapRenderItems::QuadLayer(Map, 0);
	ASSERT_NE(pLayer, nullptr);
	EXPECT_EQ(pLayer->m_NumQuads, 1);
	EXPECT_EQ(pLayer->m_Data, 0);
	EXPECT_EQ(pLayer->m_Image, 7);
}

TEST(MapRenderItemsQuad, TruncatedHeaderIsRejectedBeforeItemLookup)
{
	CItemMap Map;
	Map.SetItem(Quads(), sizeof(CMapItemLayer), MAPITEMTYPE_LAYER);
	EXPECT_EQ(MapRenderItems::QuadLayer(Map, 0), nullptr);
	EXPECT_EQ(Map.m_ItemReads, 0);
}

TEST(MapRenderItemsQuad, NonQuadLayerIsNotInterpretedAsQuadMetadata)
{
	CItemMap Map;
	auto Layer = Quads();
	Layer.m_Layer.m_Type = LAYERTYPE_TILES;
	Map.SetItem(Layer, sizeof(Layer), MAPITEMTYPE_LAYER);
	EXPECT_EQ(MapRenderItems::QuadLayer(Map, 0), nullptr);
}

TEST(MapRenderItemsQuad, InvalidItemTypeDoesNotProduceQuadMetadata)
{
	CItemMap Map;
	Map.SetItem(Quads(), sizeof(CMapItemLayerQuads), MAPITEMTYPE_IMAGE);
	EXPECT_EQ(MapRenderItems::QuadLayer(Map, 0), nullptr);
}

TEST(MapRenderItemsQuad, ValidDataBorrowsExactlyTheDeclaredSwappedQuads)
{
	CItemMap Map;
	const auto Data = MapRenderItems::QuadData(Map, Quads());
	ASSERT_EQ(Data.size(), 1u);
	EXPECT_EQ(Data.data(), Map.m_aData.data());
	EXPECT_EQ(Map.m_SwappedReads, 1);
}

TEST(MapRenderItemsQuad, UnswappedReaderRetainsItsExistingDataRoute)
{
	CItemMap Map;
	EXPECT_EQ(MapRenderItems::QuadData(Map, Quads(), false).size(), 1u);
	EXPECT_EQ(Map.m_DataReads, 1);
	EXPECT_EQ(Map.m_SwappedReads, 0);
}

TEST(MapRenderItemsQuad, InvalidDataIndicesDoNotStartDecode)
{
	CItemMap Map;
	for(int Index : {-1, 1, INT_MAX})
	{
		auto Layer = Quads();
		Layer.m_Data = Index;
		EXPECT_TRUE(MapRenderItems::QuadData(Map, Layer).empty());
	}
	EXPECT_EQ(Map.m_DataReads, 0);
}

TEST(MapRenderItemsQuad, EmptyOrNegativeCountsDoNotStartDecode)
{
	CItemMap Map;
	for(int Count : {-1, 0})
	{
		auto Layer = Quads();
		Layer.m_NumQuads = Count;
		EXPECT_TRUE(MapRenderItems::QuadData(Map, Layer).empty());
	}
	EXPECT_EQ(Map.m_DataReads, 0);
}

TEST(MapRenderItemsQuad, OversizedCountIsRejectedWithoutMultiplicationOverflow)
{
	CItemMap Map;
	auto Layer = Quads();
	Layer.m_NumQuads = INT_MAX;
	EXPECT_TRUE(MapRenderItems::QuadData(Map, Layer).empty());
	EXPECT_EQ(Map.m_DataReads, 0);
}

TEST(MapRenderItemsQuad, InvalidDataLengthIsRejectedBeforeDecode)
{
	CItemMap Map;
	for(int Size : {-1, 0, static_cast<int>(sizeof(CQuad)) - 1})
	{
		SCOPED_TRACE(Size);
		Map.m_DataSize = Size;
		EXPECT_TRUE(MapRenderItems::QuadData(Map, Quads()).empty());
	}
	EXPECT_EQ(Map.m_DataReads, 0);
}

TEST(MapRenderItemsQuad, FailedDecodeDoesNotPublishADataSpan)
{
	CItemMap Map;
	Map.m_DecodeFails = true;
	EXPECT_TRUE(MapRenderItems::QuadData(Map, Quads()).empty());
}

TEST(MapRenderItemsQuad, SizeChangedDuringDecodeIsCheckedBeforePublishing)
{
	CItemMap Map;
	Map.m_SizeAfterDecode = sizeof(CQuad) - 1;
	EXPECT_TRUE(MapRenderItems::QuadData(Map, Quads()).empty());
	EXPECT_EQ(Map.m_DataReads, 1);
}
