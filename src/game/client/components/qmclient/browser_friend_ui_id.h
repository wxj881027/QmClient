#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_FRIEND_UI_ID_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_FRIEND_UI_ID_H

#include <cstdint>
#include <limits>

template<typename TUiId = uintptr_t>
TUiId QmBrowserFriendUiIdBase(unsigned NameHash, unsigned ClanHash, unsigned AddressHash, unsigned FriendState)
{
	static_assert(std::numeric_limits<TUiId>::is_integer && !std::numeric_limits<TUiId>::is_signed);
	static_assert(std::numeric_limits<TUiId>::digits == 32 || std::numeric_limits<TUiId>::digits == 64);
	uint64_t Identity = (uint64_t(NameHash) << 32) ^ uint64_t(ClanHash) ^ (uint64_t(AddressHash) << 1) ^ (uint64_t(FriendState) << 2);
	// 32 位也保留姓名所在的高半部；所有移位都在足够宽的无符号类型内进行。
	if constexpr(std::numeric_limits<TUiId>::digits == 32)
		Identity ^= Identity >> 32;
	const TUiId Base = static_cast<TUiId>(Identity) << 4;
	return Base != 0 ? Base : TUiId(0x10);
}

#endif
