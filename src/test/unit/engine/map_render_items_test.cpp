#include <base/mem.h>

#include <game/map/map_render_items.h>

#include <gtest/gtest.h>

#include <array>
#include <climits>
#include <cstdint>
#include <limits>
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
		std::array<CSoundSource, 2> m_aSoundData{};
		bool m_UseSoundData = false;
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
			if(m_DecodeFails)
				return nullptr;
			return m_UseSoundData ? static_cast<void *>(m_aSoundData.data()) : static_cast<void *>(m_aData.data());
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

	CMapItemSound Sound(int Version = 1)
	{
		CMapItemSound Item{};
		Item.m_Version = Version;
		Item.m_External = 1;
		Item.m_SoundName = 5;
		Item.m_SoundData = 6;
		Item.m_SoundDataSize = INT_MAX;
		return Item;
	}

	CMapItemLayerSounds SoundLayer(int Type = LAYERTYPE_SOUNDS, int Version = 2)
	{
		CMapItemLayerSounds Layer{};
		Layer.m_Layer.m_Type = Type;
		Layer.m_Layer.m_Flags = LAYERFLAG_DETAIL;
		Layer.m_Version = Version;
		Layer.m_NumSources = 1;
		Layer.m_Data = 0;
		Layer.m_Sound = 7;
		Layer.m_aName[0] = 11;
		Layer.m_aName[1] = 22;
		Layer.m_aName[2] = 33;
		return Layer;
	}

	void UseSoundData(CItemMap &Map)
	{
		Map.m_UseSoundData = true;
		Map.m_DataSize = sizeof(Map.m_aSoundData);
		Map.m_aSoundData[0].m_Position.x = 123;
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

TEST(MapRenderItemsSound, ConsumedPrefixDoesNotRequireDeprecatedSizeTail)
{
	CItemMap Map;
	Map.SetItem(Sound(), offsetof(CMapItemSound, m_SoundDataSize), MAPITEMTYPE_SOUND);
	const auto Item = MapRenderItems::ReadSound(Map, 0);
	ASSERT_TRUE(Item);
	EXPECT_EQ(Item->m_External, 1);
	EXPECT_EQ(Item->m_SoundName, 5);
	EXPECT_EQ(Item->m_SoundData, 6);
	EXPECT_EQ(Item->m_SoundDataSize, 0);
}

TEST(MapRenderItemsSound, DeprecatedSizeIsNotTrustedEvenWhenPresent)
{
	CItemMap Map;
	Map.SetItem(Sound(), sizeof(CMapItemSound), MAPITEMTYPE_SOUND);
	const auto Item = MapRenderItems::ReadSound(Map, 0);
	ASSERT_TRUE(Item);
	EXPECT_EQ(Item->m_SoundDataSize, 0);
}

TEST(MapRenderItemsSound, ExistingUnknownAssetVersionRemainsReadable)
{
	CItemMap Map;
	Map.SetItem(Sound(7), offsetof(CMapItemSound, m_SoundDataSize), MAPITEMTYPE_SOUND);
	const auto Item = MapRenderItems::ReadSound(Map, 0);
	ASSERT_TRUE(Item);
	EXPECT_EQ(Item->m_Version, 7);
}

TEST(MapRenderItemsSound, ShortPrefixIsRejectedBeforeStorageLookup)
{
	CItemMap Map;
	Map.SetItem(Sound(), offsetof(CMapItemSound, m_SoundData), MAPITEMTYPE_SOUND);
	EXPECT_FALSE(MapRenderItems::ReadSound(Map, 0));
	EXPECT_EQ(Map.m_ItemReads, 0);
}

TEST(MapRenderItemsSound, WrongItemTypeIsRejected)
{
	CItemMap Map;
	Map.SetItem(Sound(), sizeof(CMapItemSound), MAPITEMTYPE_IMAGE);
	EXPECT_FALSE(MapRenderItems::ReadSound(Map, 0));
}

TEST(MapRenderItemsSound, NullItemDoesNotProduceMetadata)
{
	CItemMap Map;
	Map.SetItem(Sound(), sizeof(CMapItemSound), MAPITEMTYPE_SOUND);
	Map.m_NullItem = true;
	EXPECT_FALSE(MapRenderItems::ReadSound(Map, 0));
}

TEST(MapRenderItemsSound, InvalidIndicesDoNotReadStorage)
{
	CItemMap Map;
	for(int Index : {-1, 1, INT_MAX})
		EXPECT_FALSE(MapRenderItems::ReadSound(Map, Index));
	EXPECT_EQ(Map.m_ItemReads, 0);
}

TEST(MapRenderItemsSound, MetadataIsOwnedAfterItemReplacement)
{
	CItemMap Map;
	Map.SetItem(Sound(), sizeof(CMapItemSound), MAPITEMTYPE_SOUND);
	const auto Item = MapRenderItems::ReadSound(Map, 0);
	ASSERT_TRUE(Item);
	Map.SetItem(CMapItemSound{}, sizeof(CMapItemSound), MAPITEMTYPE_SOUND);
	EXPECT_EQ(Item->m_SoundName, 5);
}

TEST(MapRenderItemsSoundLayer, SupportedPrefixDoesNotRequireUnusedName)
{
	CItemMap Map;
	Map.SetItem(SoundLayer(LAYERTYPE_SOUNDS, 1), offsetof(CMapItemLayerSounds, m_aName), MAPITEMTYPE_LAYER);
	const auto Layer = MapRenderItems::ReadSoundLayer(Map, 0);
	ASSERT_TRUE(Layer);
	EXPECT_EQ(Layer->m_NumSources, 1);
	EXPECT_EQ(Layer->m_Sound, 7);
	EXPECT_EQ(Layer->m_Layer.m_Flags, LAYERFLAG_DETAIL);
}

TEST(MapRenderItemsSoundLayer, FullNameIsCopiedAsOwnedMetadata)
{
	CItemMap Map;
	Map.SetItem(SoundLayer(), sizeof(CMapItemLayerSounds), MAPITEMTYPE_LAYER);
	const auto Layer = MapRenderItems::ReadSoundLayer(Map, 0);
	ASSERT_TRUE(Layer);
	Map.SetItem(CMapItemLayerSounds{}, sizeof(CMapItemLayerSounds), MAPITEMTYPE_LAYER);
	EXPECT_EQ(Layer->m_aName[0], 11);
	EXPECT_EQ(Layer->m_aName[2], 33);
}

TEST(MapRenderItemsSoundLayer, MissingVersionIsRejectedBeforeLookup)
{
	CItemMap Map;
	Map.SetItem(SoundLayer(), sizeof(CMapItemLayer), MAPITEMTYPE_LAYER);
	EXPECT_FALSE(MapRenderItems::ReadSoundLayer(Map, 0));
	EXPECT_EQ(Map.m_ItemReads, 0);
}

TEST(MapRenderItemsSoundLayer, SupportedVersionRequiresAllConsumedFixedFields)
{
	CItemMap Map;
	Map.SetItem(SoundLayer(), offsetof(CMapItemLayerSounds, m_Sound), MAPITEMTYPE_LAYER);
	EXPECT_FALSE(MapRenderItems::ReadSoundLayer(Map, 0));
}

TEST(MapRenderItemsSoundLayer, UnknownVersionCanStillBeSkippedUsingOnlyItsVersionPrefix)
{
	CItemMap Map;
	Map.SetItem(SoundLayer(LAYERTYPE_SOUNDS, 3), offsetof(CMapItemLayerSounds, m_NumSources), MAPITEMTYPE_LAYER);
	const auto Layer = MapRenderItems::ReadSoundLayer(Map, 0);
	ASSERT_TRUE(Layer);
	EXPECT_EQ(Layer->m_Version, 3);
	EXPECT_EQ(Layer->m_NumSources, 0);
}

TEST(MapRenderItemsSoundLayer, DeprecatedLayerKindRetainsItsMetadata)
{
	CItemMap Map;
	Map.SetItem(SoundLayer(LAYERTYPE_SOUNDS_DEPRECATED, 1), sizeof(CMapItemLayerSounds), MAPITEMTYPE_LAYER);
	const auto Layer = MapRenderItems::ReadSoundLayer(Map, 0);
	ASSERT_TRUE(Layer);
	EXPECT_EQ(Layer->m_Layer.m_Type, LAYERTYPE_SOUNDS_DEPRECATED);
}

TEST(MapRenderItemsSoundLayer, UnrelatedLayerKindIsRejected)
{
	CItemMap Map;
	Map.SetItem(SoundLayer(LAYERTYPE_TILES), sizeof(CMapItemLayerSounds), MAPITEMTYPE_LAYER);
	EXPECT_FALSE(MapRenderItems::ReadSoundLayer(Map, 0));
}

TEST(MapRenderItemsSoundLayer, WrongMapItemTypeIsRejected)
{
	CItemMap Map;
	Map.SetItem(SoundLayer(), sizeof(CMapItemLayerSounds), MAPITEMTYPE_SOUND);
	EXPECT_FALSE(MapRenderItems::ReadSoundLayer(Map, 0));
}

TEST(MapRenderItemsSoundLayer, PartialNameTailIsNotReadAsACompleteName)
{
	CItemMap Map;
	Map.SetItem(SoundLayer(), sizeof(CMapItemLayerSounds) - sizeof(int), MAPITEMTYPE_LAYER);
	const auto Layer = MapRenderItems::ReadSoundLayer(Map, 0);
	ASSERT_TRUE(Layer);
	EXPECT_EQ(Layer->m_aName[0], 0);
}

TEST(MapRenderItemsSoundData, ExactReadBorrowsDeclaredSwappedSources)
{
	CItemMap Map;
	UseSoundData(Map);
	const auto Data = MapRenderItems::ReadDataItems<CSoundSource>(Map, 0, 1);
	ASSERT_EQ(Data.size(), 1u);
	EXPECT_EQ(Data.data(), Map.m_aSoundData.data());
	EXPECT_EQ(Data[0].m_Position.x, 123);
	EXPECT_EQ(Map.m_SwappedReads, 1);
}

TEST(MapRenderItemsSoundData, PartialPolicyRetainsTheAvailablePrefix)
{
	CItemMap Map;
	UseSoundData(Map);
	Map.m_DataSize = sizeof(CSoundSource);
	const auto Data = MapRenderItems::ReadDataItems<CSoundSource>(Map, 0, 2, MapRenderItems::EDataCountPolicy::ALLOW_PARTIAL);
	EXPECT_EQ(Data.size(), 1u);
}

TEST(MapRenderItemsSoundData, ExactPolicyRejectsMissingSourcesAfterDecode)
{
	CItemMap Map;
	UseSoundData(Map);
	Map.m_DataSize = sizeof(CSoundSource);
	EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSource>(Map, 0, 2).empty());
}

TEST(MapRenderItemsSoundData, InvalidIndexDoesNotStartDecode)
{
	CItemMap Map;
	UseSoundData(Map);
	for(int Index : {-1, 1, INT_MAX})
		EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSource>(Map, Index, 1).empty());
	EXPECT_EQ(Map.m_DataReads, 0);
}

TEST(MapRenderItemsSoundData, NonpositiveCountDoesNotStartDecode)
{
	CItemMap Map;
	UseSoundData(Map);
	for(int Count : {-1, 0})
		EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSource>(Map, 0, Count).empty());
	EXPECT_EQ(Map.m_DataReads, 0);
}

TEST(MapRenderItemsSoundData, PartialPolicyUsesTheSizeAfterDecode)
{
	CItemMap Map;
	UseSoundData(Map);
	Map.m_SizeAfterDecode = sizeof(CSoundSource);
	const auto Data = MapRenderItems::ReadDataItems<CSoundSource>(Map, 0, 2, MapRenderItems::EDataCountPolicy::ALLOW_PARTIAL);
	EXPECT_EQ(Data.size(), 1u);
}

TEST(MapRenderItemsSoundData, ExactPolicyRechecksTheSizeAfterDecode)
{
	CItemMap Map;
	UseSoundData(Map);
	Map.m_SizeAfterDecode = sizeof(CSoundSource);
	EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSource>(Map, 0, 2).empty());
}

TEST(MapRenderItemsSoundData, FailedDecodeDoesNotExposeASourcePointer)
{
	CItemMap Map;
	UseSoundData(Map);
	Map.m_DecodeFails = true;
	EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSource>(Map, 0, 1).empty());
}

TEST(MapRenderItemsSoundData, EmptyProcessedDataCannotCreateSources)
{
	CItemMap Map;
	UseSoundData(Map);
	Map.m_SizeAfterDecode = 0;
	EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSource>(Map, 0, 1, MapRenderItems::EDataCountPolicy::ALLOW_PARTIAL).empty());
}

TEST(MapRenderItemsSoundData, NegativeDataSizeCannotBecomeAnUnsignedCapacity)
{
	CItemMap Map;
	UseSoundData(Map);
	Map.m_DataSize = -1;
	EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSource>(Map, 0, 1, MapRenderItems::EDataCountPolicy::ALLOW_PARTIAL).empty());
}

TEST(MapRenderItemsSoundData, RawReadRouteDoesNotSwapTheData)
{
	CItemMap Map;
	UseSoundData(Map);
	EXPECT_EQ(MapRenderItems::ReadDataItems<CSoundSource>(Map, 0, 1, MapRenderItems::EDataCountPolicy::REQUIRE_ALL, false).size(), 1u);
	EXPECT_EQ(Map.m_SwappedReads, 0);
}

TEST(MapRenderItemsSoundData, WrappingByteCountCannotExposeDataForCopy)
{
	CItemMap Map;
	UseSoundData(Map);
	const int Count = static_cast<int>(std::numeric_limits<uint32_t>::max() / sizeof(CSoundSource) + 1);
	EXPECT_TRUE(MapRenderItems::ReadDataItems<CSoundSource>(Map, 0, Count).empty());
}
