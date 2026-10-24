#ifndef GAME_EDITOR_MAPITEMS_LOAD_INFO_H
#define GAME_EDITOR_MAPITEMS_LOAD_INFO_H

#include "map_setting.h"

#include <base/log.h>

#include <engine/map.h>

#include <game/mapitems.h>

#include <cstring>
#include <optional>
#include <vector>

namespace EditorMapLoad
{
	struct SInfoItems
	{
		std::optional<CMapItemInfo> m_Info;
		std::vector<CEditorMapSetting> m_vSettings;
	};

	// 在清空编辑器旧状态前准备信息头和设置值，失败不发布部分命令。
	inline std::optional<SInfoItems> ReadInfoItems(IEngineMap &Map)
	{
		if(!Map.IsLoaded())
			return std::nullopt;
		SInfoItems Info;
		const int ItemIndex = Map.FindItemIndex(MAPITEMTYPE_INFO, 0);
		if(ItemIndex < 0)
			return Info;
		const int ItemSize = Map.GetItemSize(ItemIndex);
		if(ItemSize < static_cast<int>(sizeof(CMapItemInfo)))
		{
			log_error("editor/load", "Map info item is truncated (size %d).", ItemSize);
			return std::nullopt;
		}
		const auto *pItem = static_cast<const CMapItemInfo *>(Map.GetItem(ItemIndex));
		Info.m_Info = *pItem;
		// 旧版只有基础头；其他版本与负索引沿用不加载设置的行为。
		if(pItem->m_Version != 1 || ItemSize < static_cast<int>(sizeof(CMapItemInfoSettings)))
			return Info;
		const int SettingsIndex = static_cast<const CMapItemInfoSettings *>(pItem)->m_Settings;
		if(SettingsIndex < 0)
			return Info;
		if(SettingsIndex >= Map.NumData())
		{
			log_error("editor/load", "Map settings use invalid data index %d.", SettingsIndex);
			return std::nullopt;
		}

		const auto *pData = static_cast<const char *>(Map.GetData(SettingsIndex));
		// 解压或数据处理器会改变长度，必须在读取后重新查询有符号尺寸。
		const int DataSize = Map.GetDataSize(SettingsIndex);
		if(DataSize == 0)
			return Info;
		if(pData == nullptr || DataSize < 0)
		{
			log_error("editor/load", "Map settings data %d could not be read.", SettingsIndex);
			return std::nullopt;
		}
		const char *pNext = pData;
		size_t Remaining = static_cast<size_t>(DataSize);
		while(Remaining > 0)
		{
			const auto *pEnd = static_cast<const char *>(std::memchr(pNext, '\0', Remaining));
			if(pEnd == nullptr)
			{
				log_error("editor/load", "Map settings data %d contain an unterminated command.", SettingsIndex);
				return std::nullopt;
			}
			Info.m_vSettings.emplace_back(pNext);
			const size_t Consumed = static_cast<size_t>(pEnd - pNext) + 1;
			pNext += Consumed;
			Remaining -= Consumed;
		}
		return Info;
	}
}

#endif
