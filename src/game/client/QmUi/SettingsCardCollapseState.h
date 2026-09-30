#ifndef GAME_CLIENT_QMUI_SETTINGSCARDCOLLAPSESTATE_H
#define GAME_CLIENT_QMUI_SETTINGSCARDCOLLAPSESTATE_H

#include "QmModuleTypes.h"

#include <array>
#include <cstddef>
#include <map>
#include <string>

namespace qm_card_collapse
{
	// 旧格式的 stable ID 表示折叠；!stable ID 表示显式展开，缺省沿用卡片默认值。
	class CState
	{
	public:
		bool Load(const char *pSerialized);
		bool ImportLegacyQm(const char *pSerialized);
		bool IsCollapsed(const char *pStableId, bool DefaultValue) const;
		bool SetCollapsed(const char *pStableId, bool Collapsed);
		bool Serialize(char *pOut, size_t OutSize) const;

	private:
		std::map<std::string, bool> m_States;
	};

	// 连接 CFGFLAG_SAVE 配置的全局状态。所有设置页面和搜索页共用该实例。
	// 调用一次同步配置，随后可直接读取返回的内存状态，避免页面按卡片重复扫描配置串。
	const CState &CurrentState();
	bool IsCollapsed(const char *pStableId, bool DefaultValue = false);
	bool SetCollapsed(const char *pStableId, bool Collapsed);
	void SyncFromConfig();
	void SyncQmModules(std::array<bool, qm_module::QmModuleCount> &Collapsed);
	bool SetQmModuleCollapsed(qm_module::EQmModuleId Id, bool Collapsed);
}

#endif
