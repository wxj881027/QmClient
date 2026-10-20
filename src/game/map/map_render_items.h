#ifndef GAME_MAP_MAP_RENDER_ITEMS_H
#define GAME_MAP_MAP_RENDER_ITEMS_H

#include <engine/map.h>

#include <game/mapitems.h>

#include <cstddef>
#include <optional>
#include <span>

namespace MapRenderItems
{
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

	// Layer 的固定字段须完整；返回的借用在 UnloadData/地图销毁前有效。
	inline std::span<CQuad> QuadData(IMap &Map, const CMapItemLayerQuads &Layer, bool Swap = true)
	{
		if(Layer.m_NumQuads <= 0 || Layer.m_Data < 0 || Layer.m_Data >= Map.NumData())
			return {};
		const size_t Count = static_cast<size_t>(Layer.m_NumQuads);
		const auto Fits = [Count](int Size) { return Size >= 0 && Count <= static_cast<size_t>(Size) / sizeof(CQuad); };
		if(!Fits(Map.GetDataSize(Layer.m_Data)))
			return {};
		auto *pQuads = static_cast<CQuad *>(Swap ? Map.GetDataSwapped(Layer.m_Data) : Map.GetData(Layer.m_Data));
		// 数据处理器可在解压后替换数据与长度，不能继续使用解压前的尺寸。
		if(pQuads == nullptr || !Fits(Map.GetDataSize(Layer.m_Data)))
			return {};
		return {pQuads, Count};
	}
}

#endif
