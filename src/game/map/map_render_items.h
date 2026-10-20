#ifndef GAME_MAP_MAP_RENDER_ITEMS_H
#define GAME_MAP_MAP_RENDER_ITEMS_H

#include <engine/map.h>

#include <game/mapitems.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>

namespace MapRenderItems
{
	enum class EDataCountPolicy
	{
		REQUIRE_ALL,
		ALLOW_PARTIAL,
	};

	// 读取值快照；旧版只含 v1 字段，不能借用成完整 v2 对象读取扩展。
	inline std::optional<CMapItemImage_v2> ReadImage(IMap &Map, int ItemIndex)
	{
		if(ItemIndex < 0 || ItemIndex >= Map.NumItems())
			return std::nullopt;
		const int ItemSize = Map.GetItemSize(ItemIndex);
		if(ItemSize < static_cast<int>(sizeof(CMapItemImage_v1)))
			return std::nullopt;
		int Type;
		const auto *pBase = static_cast<const CMapItemImage_v1 *>(Map.GetItem(ItemIndex, &Type));
		if(pBase == nullptr || Type != MAPITEMTYPE_IMAGE)
			return std::nullopt;
		if(pBase->m_Version > 1 && ItemSize < static_cast<int>(sizeof(CMapItemImage_v2)))
			return std::nullopt;
		CMapItemImage_v2 Image{};
		static_cast<CMapItemImage_v1 &>(Image) = *pBase;
		Image.m_MustBe1 = pBase->m_Version > 1 ? static_cast<const CMapItemImage_v2 *>(pBase)->m_MustBe1 : 1;
		return Image;
	}

	// 只读取实际消费的前缀，废弃的 SoundDataSize 不参与解码长度判断。
	inline std::optional<CMapItemSound> ReadSound(IMap &Map, int ItemIndex)
	{
		if(ItemIndex < 0 || ItemIndex >= Map.NumItems() ||
			Map.GetItemSize(ItemIndex) < static_cast<int>(offsetof(CMapItemSound, m_SoundDataSize)))
			return std::nullopt;
		int Type;
		const auto *pItem = static_cast<const CMapItemSound *>(Map.GetItem(ItemIndex, &Type));
		if(pItem == nullptr || Type != MAPITEMTYPE_SOUND)
			return std::nullopt;
		CMapItemSound Sound{};
		Sound.m_Version = pItem->m_Version;
		Sound.m_External = pItem->m_External;
		Sound.m_SoundName = pItem->m_SoundName;
		Sound.m_SoundData = pItem->m_SoundData;
		return Sound;
	}

	// 未支持的版本只读取版本前缀，保留消费者原有的跳过行为。
	// 名称仅在完整尾部存在时复制；需要名称的调用者仍须检查完整尺寸。
	inline std::optional<CMapItemLayerSounds> ReadSoundLayer(IMap &Map, int ItemIndex)
	{
		if(ItemIndex < 0 || ItemIndex >= Map.NumItems())
			return std::nullopt;
		const int ItemSize = Map.GetItemSize(ItemIndex);
		if(ItemSize < static_cast<int>(offsetof(CMapItemLayerSounds, m_NumSources)))
			return std::nullopt;
		int Type;
		const auto *pItem = static_cast<const CMapItemLayerSounds *>(Map.GetItem(ItemIndex, &Type));
		if(pItem == nullptr || Type != MAPITEMTYPE_LAYER ||
			(pItem->m_Layer.m_Type != LAYERTYPE_SOUNDS && pItem->m_Layer.m_Type != LAYERTYPE_SOUNDS_DEPRECATED))
			return std::nullopt;
		CMapItemLayerSounds Layer{};
		Layer.m_Layer = pItem->m_Layer;
		Layer.m_Version = pItem->m_Version;
		if(Layer.m_Version < 1 || Layer.m_Version > 2)
			return Layer;
		if(ItemSize < static_cast<int>(offsetof(CMapItemLayerSounds, m_aName)))
			return std::nullopt;
		Layer.m_NumSources = pItem->m_NumSources;
		Layer.m_Data = pItem->m_Data;
		Layer.m_Sound = pItem->m_Sound;
		if(ItemSize >= static_cast<int>(sizeof(CMapItemLayerSounds)))
		{
			for(size_t i = 0; i < std::size(Layer.m_aName); ++i)
				Layer.m_aName[i] = pItem->m_aName[i];
		}
		return Layer;
	}

	// 包络渲染只消费固定前缀；编辑器读取名称/同步字段前另查相应尾部。
	inline std::optional<CMapItemEnvelope> ReadEnvelope(IMap &Map, int ItemIndex)
	{
		if(ItemIndex < 0 || ItemIndex >= Map.NumItems())
			return std::nullopt;
		const int ItemSize = Map.GetItemSize(ItemIndex);
		if(ItemSize < static_cast<int>(offsetof(CMapItemEnvelope_v1, m_aName)))
			return std::nullopt;
		int Type;
		const auto *pItem = static_cast<const CMapItemEnvelope_v1 *>(Map.GetItem(ItemIndex, &Type));
		if(pItem == nullptr || Type != MAPITEMTYPE_ENVELOPE)
			return std::nullopt;
		CMapItemEnvelope Envelope{};
		Envelope.m_Version = pItem->m_Version;
		Envelope.m_Channels = pItem->m_Channels;
		Envelope.m_StartPoint = pItem->m_StartPoint;
		Envelope.m_NumPoints = pItem->m_NumPoints;
		if(ItemSize >= static_cast<int>(sizeof(CMapItemEnvelope_v1)))
		{
			for(size_t i = 0; i < std::size(Envelope.m_aName); ++i)
				Envelope.m_aName[i] = pItem->m_aName[i];
		}
		if(Envelope.m_Version >= 2 && ItemSize >= static_cast<int>(sizeof(CMapItemEnvelope)))
			Envelope.m_Synchronized = static_cast<const CMapItemEnvelope *>(pItem)->m_Synchronized;
		return Envelope;
	}

	// 返回地图拥有的固定头；名称是可选尾部，使用名称的调用方还须检查完整尺寸。
	inline CMapItemLayerQuads *QuadLayer(IMap &Map, int ItemIndex)
	{
		if(ItemIndex < 0 || ItemIndex >= Map.NumItems() ||
			Map.GetItemSize(ItemIndex) < static_cast<int>(offsetof(CMapItemLayerQuads, m_aName)))
			return nullptr;
		int Type;
		auto *pLayer = static_cast<CMapItemLayerQuads *>(Map.GetItem(ItemIndex, &Type));
		if(pLayer == nullptr || Type != MAPITEMTYPE_LAYER || pLayer->m_Layer.m_Type != LAYERTYPE_QUADS)
			return nullptr;
		return pLayer;
	}

	// 返回的借用在 UnloadData/地图销毁前有效；部分数量仅用于既有前缀回退。
	template<typename T>
	inline std::span<T> ReadDataItems(IMap &Map, int DataIndex, int NumItems,
		EDataCountPolicy Policy = EDataCountPolicy::REQUIRE_ALL, bool Swap = true)
	{
		if(NumItems <= 0 || DataIndex < 0 || DataIndex >= Map.NumData())
			return {};
		auto *pData = static_cast<T *>(Swap ? Map.GetDataSwapped(DataIndex) : Map.GetData(DataIndex));
		if(pData == nullptr)
			return {};
		const int Size = Map.GetDataSize(DataIndex);
		if(Size < 0)
			return {};
		const size_t Available = static_cast<size_t>(Size) / sizeof(T);
		const size_t Count = static_cast<size_t>(NumItems);
		const size_t ResultCount = Policy == EDataCountPolicy::ALLOW_PARTIAL ? std::min(Count, Available) : Count;
		if(ResultCount > Available)
			return {};
		return {pData, ResultCount};
	}

	// Layer 的固定字段须完整；保留四边形既有的解码前尺寸检查。
	inline std::span<CQuad> QuadData(IMap &Map, const CMapItemLayerQuads &Layer, bool Swap = true)
	{
		if(Layer.m_NumQuads <= 0 || Layer.m_Data < 0 || Layer.m_Data >= Map.NumData())
			return {};
		const size_t Count = static_cast<size_t>(Layer.m_NumQuads);
		const auto Fits = [Count](int Size) { return Size >= 0 && Count <= static_cast<size_t>(Size) / sizeof(CQuad); };
		if(!Fits(Map.GetDataSize(Layer.m_Data)))
			return {};
		return ReadDataItems<CQuad>(Map, Layer.m_Data, Layer.m_NumQuads, EDataCountPolicy::REQUIRE_ALL, Swap);
	}
}

#endif
