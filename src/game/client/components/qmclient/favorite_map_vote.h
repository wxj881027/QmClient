#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FAVORITE_MAP_VOTE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FAVORITE_MAP_VOTE_H

#include <engine/map.h>
#include <engine/shared/protocol.h>

#include <string>

// 仅在线双击时生成命令，引用完整地图名，避免空格、引号或截断改变目标。
inline std::string QmFavoriteMapVoteCommand(const std::string &MapName, bool Online, bool DoubleClicked)
{
	if(!Online || !DoubleClicked || MapName.empty() || MapName.size() >= MAX_MAP_LENGTH)
		return {};

	std::string Command = "/map \"";
	for(const unsigned char Char : MapName)
	{
		if(Char < 0x20 || Char == 0x7f)
			return {};
		if(Char == '\\' || Char == '"')
			Command += '\\';
		Command += Char;
	}
	Command += '"';
	return Command.size() < MAX_CHAT_LENGTH ? Command : std::string{};
}

#endif
