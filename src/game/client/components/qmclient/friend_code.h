#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_CODE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_CODE_H

#include <engine/friends.h>

#include <cstddef>
#include <string_view>

namespace qm_friend_code
{
	inline constexpr size_t MAX_RAW_BYTES = 1024 * 1024;
	inline constexpr size_t MAX_CODE_BYTES = 2 * 1024 * 1024;

	enum class EImportMode
	{
		MERGE,
		REPLACE,
	};

	CFriendList Capture(const IFriends &Friends);
	bool Encode(const CFriendList &List, std::string &Code);
	bool Decode(std::string_view Code, CFriendList &List);
	// 合并保持已有分组顺序；新增分组按码中顺序追加，重复身份保留本地资料。
	bool Import(IFriends &Friends, const CFriendList &Incoming, EImportMode Mode = EImportMode::MERGE);
}

#endif
