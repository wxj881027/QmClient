#ifndef GAME_CLIENT_QMUI_SETTINGSPERSONALIZATION_H
#define GAME_CLIENT_QMUI_SETTINGSPERSONALIZATION_H

#include "SettingsContentLayout.h"

#include <map>
#include <string>

// 仅保存本次客户端会话的展开状态，不修改功能配置。稳定卡片 ID 让搜索页与分类页共用状态。
class CSettingsPersonalizationState
{
	std::map<std::string, bool, std::less<>> m_Expanded;

public:
	bool Expanded(const char *pStableId) const
	{
		const auto It = m_Expanded.find(pStableId);
		return It != m_Expanded.end() && It->second;
	}

	bool &Section(const char *pStableId)
	{
		const auto It = m_Expanded.find(pStableId);
		return It != m_Expanded.end() ? It->second : m_Expanded.emplace(pStableId, false).first->second;
	}
};

// 声明、输入与绘制走同一入口；收起时调用方不声明细调行，因此不会保留空白或隐藏热区。
template<typename TButton>
bool SettingsPersonalizationSection(CSettingsPersonalizationState &State, CUIRect &Content, const char *pStableId, float RowHeight, float Spacing, bool ReadOnly, TButton &&Button)
{
	bool &Expanded = State.Section(pStableId);
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	if(Flow.Control("personalization", RowHeight, Row))
	{
		const bool Clicked = Button(&Expanded, Row, Expanded);
		if(Clicked && Flow.Input() && !ReadOnly)
			Expanded = !Expanded;
	}
	Flow.Space(Spacing);
	return Expanded;
}

#endif
