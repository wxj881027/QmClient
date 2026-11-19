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
	// 准备失败返回 false，不发布空快照、不修改已有输出。
	bool TryCapture(const IFriends &Friends, CFriendList &List);
	// 编解码的分配准备失败返回 false，保留已有输出。
	bool Encode(const CFriendList &List, std::string &Code);
	bool Decode(std::string_view Code, CFriendList &List);
	// 合并保持已有分组顺序；新增分组按码中顺序追加，重复身份保留本地资料。
	// 准备失败保留原名单；最终 ReplaceAll 的异常交由调用方处理。
	bool Import(IFriends &Friends, const CFriendList &Incoming, EImportMode Mode = EImportMode::MERGE);
}

#endif
