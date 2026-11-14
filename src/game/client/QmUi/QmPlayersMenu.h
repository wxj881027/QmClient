#ifndef GAME_CLIENT_QMUI_QMPLAYERSMENU_H
#define GAME_CLIENT_QMUI_QMPLAYERSMENU_H

#include <game/client/QmUi/UiTokens.h>
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

	// 玩家页的两栏：左边玩家列表、右边玩家信息卡。
	// 宽窗口左右分栏，窄窗口把信息卡堆到列表下方；两栏之间的缝一律取
	// ui_token::player_card::LIST_CARD_GAP，不再各写 8，也不再让列表宽度“顺手减 8”。
	struct SPanelLayout
	{
		CUIRect m_List;
		CUIRect m_Details;
	};

	inline SPanelLayout Panels(const CUIRect &View)
	{
		SPanelLayout Layout;
		const float Gap = ui_token::player_card::LIST_CARD_GAP;
		if(View.w >= ui_token::player_card::SIDE_BY_SIDE_MIN_WIDTH)
		{
			CUIRect Card;
			const float CardWidth = std::min(ui_token::player_card::CARD_MAX_WIDTH, View.w * ui_token::player_card::CARD_WIDTH_RATIO);
			View.VSplitRight(CardWidth, &Layout.m_List, &Card);
			// 缝从列表侧让出来：卡片保持右缘对齐，列表宽度里已经扣掉缝。
			Layout.m_List.VSplitRight(std::min(Gap, Layout.m_List.w), &Layout.m_List, nullptr);
			Layout.m_Details = Card;
		}
		else
		{
			CUIRect Card;
			View.HSplitBottom(std::max(0.0f, View.h * ui_token::player_card::STACK_CARD_HEIGHT_RATIO), &Layout.m_List, &Card);
			Layout.m_List.HSplitBottom(std::min(Gap, Layout.m_List.h), &Layout.m_List, nullptr);
			Layout.m_Details = Card;
		}
		return Layout;
	}

} // namespace QmPlayersUi

struct SQmPlayersMenuState
{
	QmPlayersUi::CSelection m_Selection;
	CLineInputBuffered<64> m_Search;
	int m_Filter = 0;
	// 复制类图标按钮的轻提示：记录到期时刻（Client()->GlobalTime() 秒），
	// 到期后不再绘制；文案统一取「Copied」，避免每个字段各造一条翻译。
	float m_CopyToastUntil = 0.0f;
};

#endif
