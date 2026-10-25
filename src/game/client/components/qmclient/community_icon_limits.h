#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_LIMITS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_LIMITS_H

#include <cstddef>

namespace qm_community_icons
{
	inline constexpr std::size_t MAX_FILE_BYTES = 16 * 1024 * 1024;
	inline constexpr std::size_t MAX_DECODED_RGBA_BYTES = 16 * 1024 * 1024;
	// 包含已完成但尚未消费的结果，原色/灰度两份像素最多占128MiB。
	inline constexpr std::size_t MAX_PREPARED_LOADS = 4;
}

#endif
