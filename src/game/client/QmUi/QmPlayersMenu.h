#ifndef GAME_CLIENT_QMUI_QMPLAYERSMENU_H
#define GAME_CLIENT_QMUI_QMPLAYERSMENU_H

#include <base/str.h>

#include <engine/shared/protocol.h>

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

	// 选中态仍被投票页的踢人/观战列表复用（玩家页本身已改成整行卡片，不再有选中概念）。
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

	class CActionPress
	{
		const void *m_pButton = nullptr;
		char m_aName[MAX_NAME_LENGTH] = {};
		char m_aClan[MAX_CLAN_LENGTH] = {};

	public:
		void CancelIfInactive(const void *pActiveButton)
		{
			if(m_pButton != pActiveButton)
				m_pButton = nullptr;
		}

		bool ProcessButton(const void *pButton, const char *pName, const char *pClan, bool Pressed, bool Clicked)
		{
			if(Pressed)
			{
				m_pButton = pButton;
				str_copy(m_aName, pName);
				str_copy(m_aClan, pClan);
			}
			if(m_pButton != pButton)
				return false;

			// 槽位复用时按钮地址不变，用按下时的玩家身份校验这次点击。
			const bool SamePlayer = str_comp(m_aName, pName) == 0 && str_comp(m_aClan, pClan) == 0;
			if(Clicked || !SamePlayer)
				m_pButton = nullptr;
			return Clicked && SamePlayer;
		}
	};

} // namespace QmPlayersUi

struct SQmPlayersMenuState
{
	CLineInputBuffered<64> m_Search;
	QmPlayersUi::CActionPress m_ActionPress;
	int m_Filter = 0;
	// 复制类图标按钮的轻提示：记录到期时刻（Client()->GlobalTime() 秒），
	// 到期后不再绘制；文案统一取「Copied」，避免每个字段各造一条翻译。
	float m_CopyToastUntil = 0.0f;
};

#endif
