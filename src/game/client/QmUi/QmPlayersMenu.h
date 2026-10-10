#ifndef GAME_CLIENT_QMUI_QMPLAYERSMENU_H
#define GAME_CLIENT_QMUI_QMPLAYERSMENU_H

#include <game/client/lineinput.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <string>

namespace QmPlayersUi
{
struct SIdentity
{
	int m_Id = -1;
	std::string m_Name;
	std::string m_Clan;
};

class CSelection
{
	SIdentity m_Player;

public:
	void Choose(const SIdentity &Player) { m_Player = Player; }
	int Id() const { return m_Player.m_Id; }
	void Validate(const SIdentity *pCurrent)
	{
		// 槽位复用或名字变化后必须重新选择，避免操作落在另一个玩家上。
		if(pCurrent == nullptr || pCurrent->m_Id != m_Player.m_Id || pCurrent->m_Name != m_Player.m_Name || pCurrent->m_Clan != m_Player.m_Clan)
			m_Player = {};
	}
};

struct SPanelLayout
{
	CUIRect m_List;
	CUIRect m_Details;
};

inline SPanelLayout Panels(const CUIRect &View)
{
	SPanelLayout Layout;
	if(View.w >= 580.0f)
	{
		View.VSplitRight(std::min(260.0f, View.w * 0.36f), &Layout.m_List, &Layout.m_Details);
		Layout.m_List.w = std::max(0.0f, Layout.m_List.w - 8.0f);
	}
	else
	{
		View.HSplitBottom(View.h * 0.46f, &Layout.m_List, &Layout.m_Details);
		Layout.m_List.h = std::max(0.0f, Layout.m_List.h - 8.0f);
	}
	return Layout;
}
}

struct SQmPlayersMenuState
{
	QmPlayersUi::CSelection m_Selection;
	CLineInputBuffered<64> m_Search;
	int m_Filter = 0;
};

#endif
