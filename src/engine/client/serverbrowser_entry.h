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
	inline void ValidateAddressRange(const NETADDR *pAddresses, int NumAddresses)
	{
		if(NumAddresses < 0 || NumAddresses > MAX_SERVER_ADDRESSES || (NumAddresses != 0 && pAddresses == nullptr))
			throw std::invalid_argument("invalid server addresses");
	}

	// 只在同一主线程内登记地址；hash/比较和 erase 不抛，回滚不申请内存。
	template<typename TAddressMap>
	class CAddressUpdate
	{
		struct CChange
		{
			NETADDR m_Address;
			int *m_pIndex;
			int m_PreviousIndex;
			bool m_Inserted;
		};
		TAddressMap &m_ByAddress;
		std::array<CChange, MAX_SERVER_ADDRESSES> m_aChanges{};
		int m_NumChanges = 0;

	public:
		explicit CAddressUpdate(TAddressMap &ByAddress) :
			m_ByAddress(ByAddress)
		{
			static_assert(std::is_same_v<typename TAddressMap::key_type, NETADDR>);
			static_assert(std::is_same_v<typename TAddressMap::mapped_type, int>);
			static_assert(noexcept(std::declval<typename TAddressMap::hasher>()(std::declval<const NETADDR &>())));
		}
		CAddressUpdate(const CAddressUpdate &) = delete;
		CAddressUpdate &operator=(const CAddressUpdate &) = delete;
		~CAddressUpdate() { Rollback(); }
		void Assign(const NETADDR &Address, int ServerIndex)
		{
			dbg_assert(m_NumChanges < MAX_SERVER_ADDRESSES, "too many address changes");
			auto [It, Inserted] = m_ByAddress.try_emplace(Address, ServerIndex);
			// 映射值地址跨 rehash 稳定；重复地址也按相反顺序恢复。
			m_aChanges[m_NumChanges++] = {Address, &It->second, It->second, Inserted};
			It->second = ServerIndex;
		}
		void Rollback() noexcept
		{
			while(m_NumChanges != 0)
			{
				const CChange &Change = m_aChanges[--m_NumChanges];
				if(Change.m_Inserted)
					m_ByAddress.erase(Change.m_Address);
				else
					*Change.m_pIndex = Change.m_PreviousIndex;
			}
		}
		void Commit() noexcept { m_NumChanges = 0; }
	};

	// Prepare 仅补新条目元数据，不改地址、容器或重入；释放和容器撤回不抛。
	template<typename TStorage, typename TServers, typename TAddressMap, typename FPrepare>
	IServerBrowser::CServerEntry *AddEntry(TStorage &Storage, TServers &Servers, TAddressMap &ByAddress,
		const NETADDR *pAddresses, int NumAddresses, FPrepare &&Prepare)
	{
		static_assert(std::is_nothrow_destructible_v<IServerBrowser::CServerEntry>);
		ValidateAddressRange(pAddresses, NumAddresses);
		if(Servers.size() >= INT_MAX)
			throw std::length_error("too many server entries");

		std::array<NETADDR, MAX_SERVER_ADDRESSES> aAddresses{};
		for(int Index = 0; Index < NumAddresses; ++Index)
			aAddresses[Index] = pAddresses[Index];
		CAddressUpdate Changes(ByAddress);
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
				Changes.Assign(aAddresses[Index], ServerIndex);
			Changes.Commit();
			return pEntry;
		}
		catch(...)
		{
			Changes.Rollback();
			if(Listed)
				Servers.pop_back();
			if(pEntry != nullptr)
				Storage.pop_back();
			throw;
		}
	}

	// Prepare 同 AddEntry；新键登记后，删除旧键与固定字段提交均不抛。
	template<typename TAddressMap, typename FPrepare>
	void ReplaceEntry(IServerBrowser::CServerEntry &Entry, TAddressMap &ByAddress,
		const NETADDR *pAddresses, int NumAddresses, FPrepare &&Prepare)
	{
		ValidateAddressRange(pAddresses, NumAddresses);
		ValidateAddressRange(Entry.m_Info.m_aAddresses, Entry.m_Info.m_NumAddresses);
		CServerInfo Candidate{};
		for(int Index = 0; Index < NumAddresses; ++Index)
			Candidate.m_aAddresses[Index] = pAddresses[Index];
		Candidate.m_NumAddresses = NumAddresses;
		Prepare(Candidate);
		CAddressUpdate Changes(ByAddress);
		for(int Index = 0; Index < NumAddresses; ++Index)
			Changes.Assign(Candidate.m_aAddresses[Index], Entry.m_Info.m_ServerIndex);
		for(int OldIndex = 0; OldIndex < Entry.m_Info.m_NumAddresses; ++OldIndex)
		{
			const NETADDR &Old = Entry.m_Info.m_aAddresses[OldIndex];
			bool Retained = false;
			for(int Index = 0; Index < NumAddresses; ++Index)
				Retained |= Old == Candidate.m_aAddresses[Index];
			if(!Retained)
				ByAddress.erase(Old);
		}
		// 只替换原 ReplaceEntry 会改变的固定字段，保留玩家存储和其余缓存。
		CServerInfo &Info = Entry.m_Info;
		for(int Index = 0; Index < NumAddresses; ++Index)
			Info.m_aAddresses[Index] = Candidate.m_aAddresses[Index];
		Info.m_NumAddresses = NumAddresses;
		Info.m_QmClientCount = Candidate.m_QmClientCount;
		Info.m_Latency = Candidate.m_Latency;
		Info.m_HasRank = Candidate.m_HasRank;
		Info.m_Favorite = Candidate.m_Favorite;
		Info.m_FavoriteAllowPing = Candidate.m_FavoriteAllowPing;
		str_copy(Info.m_aAddress, Candidate.m_aAddress);
		str_copy(Info.m_aName, Candidate.m_aName);
		str_copy(Info.m_aCommunityId, Candidate.m_aCommunityId);
		str_copy(Info.m_aCommunityCountry, Candidate.m_aCommunityCountry);
		str_copy(Info.m_aCommunityType, Candidate.m_aCommunityType);
		Entry.m_FriendStateValid = false;
		Changes.Commit();
	}
}

#endif
