#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PIE_MENU_FRIEND_ACTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PIE_MENU_FRIEND_ACTION_H

#include <base/str.h>

#include <engine/console.h>
#include <engine/shared/protocol.h>

namespace qm_pie_menu
{
	// 保留控制台的好友刷新链，玩家身份只能作为完整转义的参数进入命令。
	inline bool ExecuteFriendCommand(IConsole &Console, bool Remove, const char *pName, const char *pClan)
	{
		if(pName == nullptr || pClan == nullptr || str_length(pName) >= MAX_NAME_LENGTH || str_length(pClan) >= MAX_CLAN_LENGTH)
			return false;
		char aName[MAX_NAME_LENGTH * 2];
		char aClan[MAX_CLAN_LENGTH * 2];
		char *pNameEnd = aName;
		char *pClanEnd = aClan;
		str_escape(&pNameEnd, pName, aName + sizeof(aName));
		str_escape(&pClanEnd, pClan, aClan + sizeof(aClan));
		char aCommand[sizeof("remove_friend \"\" \"\"") + sizeof(aName) + sizeof(aClan)];
		str_format(aCommand, sizeof(aCommand), "%s \"%s\" \"%s\"", Remove ? "remove_friend" : "add_friend", aName, aClan);
		Console.ExecuteLine(aCommand);
		return true;
	}
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_PIE_MENU_FRIEND_ACTION_H
