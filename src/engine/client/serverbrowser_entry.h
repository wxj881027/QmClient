#ifndef ENGINE_CLIENT_SERVERBROWSER_ENTRY_H
#define ENGINE_CLIENT_SERVERBROWSER_ENTRY_H

#include <engine/serverbrowser.h>

#include <array>
#include <climits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace server_browser
{
	// 容器使用同一主线程；Prepare 只补新条目的元数据，不修改地址、容器或重入。
	// NETADDR 的 hash/比较、erase、pop_back 和释放均不抛；回滚不申请内存。
	template<typename TStorage, typename TServers, typename TAddressMap, typename FPrepare>
	IServerBrowser::CServerEntry *AddEntry(TStorage &Storage, TServers &Servers, TAddressMap &ByAddress,
		const NETADDR *pAddresses, int NumAddresses, FPrepare &&Prepare)
	{
		static_assert(std::is_same_v<typename TAddressMap::key_type, NETADDR>);
		static_assert(std::is_same_v<typename TAddressMap::mapped_type, int>);
		static_assert(noexcept(std::declval<typename TAddressMap::hasher>()(std::declval<const NETADDR &>())));
		static_assert(std::is_nothrow_destructible_v<IServerBrowser::CServerEntry>);
		if(NumAddresses < 0 || NumAddresses > MAX_SERVER_ADDRESSES || (NumAddresses != 0 && pAddresses == nullptr))
			throw std::invalid_argument("invalid server addresses");
		if(Servers.size() >= INT_MAX)
			throw std::length_error("too many server entries");

		std::array<NETADDR, MAX_SERVER_ADDRESSES> aAddresses{};
		for(int Index = 0; Index < NumAddresses; ++Index)
			aAddresses[Index] = pAddresses[Index];
		struct CChange
		{
			NETADDR m_Address;
			int *m_pIndex;
			int m_PreviousIndex;
			bool m_Inserted;
		};
		std::array<CChange, MAX_SERVER_ADDRESSES> aChanges{};
		int NumChanges = 0;
		IServerBrowser::CServerEntry *pEntry = nullptr;
		bool Listed = false;
		try
		{
			pEntry = &Storage.emplace_back();
			for(int Index = 0; Index < NumAddresses; ++Index)
				pEntry->m_Info.m_aAddresses[Index] = aAddresses[Index];
			pEntry->m_Info.m_NumAddresses = NumAddresses;
			Prepare(pEntry->m_Info);
			const int ServerIndex = static_cast<int>(Servers.size());
			pEntry->m_Info.m_ServerIndex = ServerIndex;
			if(Servers.capacity() == 0)
				Servers.reserve(128);
			Servers.push_back(pEntry);
			Listed = true;
			for(int Index = 0; Index < NumAddresses; ++Index)
			{
				auto [It, Inserted] = ByAddress.try_emplace(aAddresses[Index], ServerIndex);
				// 映射值的地址在 rehash 后仍稳定；重复地址也按逆序还原。
				aChanges[NumChanges++] = {aAddresses[Index], &It->second, It->second, Inserted};
				It->second = ServerIndex;
			}
			return pEntry;
		}
		catch(...)
		{
			while(NumChanges != 0)
			{
				const CChange &Change = aChanges[--NumChanges];
				if(Change.m_Inserted)
					ByAddress.erase(Change.m_Address);
				else
					*Change.m_pIndex = Change.m_PreviousIndex;
			}
			if(Listed)
				Servers.pop_back();
			if(pEntry != nullptr)
				Storage.pop_back();
			throw;
		}
	}
}

#endif
