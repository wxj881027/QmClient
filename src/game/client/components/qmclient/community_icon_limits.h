#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_LIMITS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_COMMUNITY_ICON_LIMITS_H

#include <cstddef>

namespace qm_community_icons
{
	inline constexpr std::size_t MAX_FILE_BYTES = 16 * 1024 * 1024;
	inline constexpr std::size_t MAX_DECODED_RGBA_BYTES = 16 * 1024 * 1024;
	// 包含已完成但尚未消费的结果，原色/灰度两份像素最多占128MiB。
	inline constexpr std::size_t MAX_PREPARED_LOADS = 4;
	// 逻辑持有的RGBA像素及完整mip链，不含后端延迟释放、对齐和驱动开销。
	inline constexpr std::size_t MAX_STORE_TEXTURE_PIXEL_BYTES = 128 * 1024 * 1024;
}

#endif
