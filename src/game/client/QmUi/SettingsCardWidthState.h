#ifndef GAME_CLIENT_QMUI_SETTINGSCARDWIDTHSTATE_H
#define GAME_CLIENT_QMUI_SETTINGSCARDWIDTHSTATE_H

#include "SettingsCardCollapseState.h"

#include <cstddef>

namespace qm_card_width
{
	// 宽度独立于列与排序保存：stable ID 为全宽，!stable ID 为半宽，缺省沿用卡片默认宽度。
	class CState
	{
	public:
		bool Load(const char *pSerialized);
		bool IsFullWidth(const char *pStableId, bool DefaultValue) const;
		bool SetFullWidth(const char *pStableId, bool FullWidth);
		bool Serialize(char *pOut, size_t OutSize) const;

	private:
		// 只复用稳定 ID 布尔值的解析与序列化，不共享折叠配置或迁移状态。
		qm_card_collapse::CState m_States;
	};

	// 普通页面和搜索页通过同一配置状态读取宽度。
	const CState &CurrentState();
	bool IsFullWidth(const char *pStableId, bool DefaultValue = false);
	bool SetFullWidth(const char *pStableId, bool FullWidth);
	void SyncFromConfig();
}

#endif
