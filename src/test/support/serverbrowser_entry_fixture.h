#ifndef TEST_SUPPORT_SERVERBROWSER_ENTRY_FIXTURE_H
#define TEST_SUPPORT_SERVERBROWSER_ENTRY_FIXTURE_H

#include <engine/client/serverbrowser_entry.h>

#include <gtest/gtest.h>

#include <deque>
#include <initializer_list>
#include <memory>
#include <new>
#include <unordered_map>
#include <vector>

namespace ServerBrowserEntryTests
{
	struct CAllocationState
	{
		bool m_Fail = false;
		bool m_LatchFailure = false;
		int m_Calls = 0;
		int m_FailAt = -1;
		size_t m_LiveBytes = 0;
	};

	template<typename T>
	struct CAllocator
	{
		using value_type = T;
		CAllocationState *m_pState;
		explicit CAllocator(CAllocationState *pState) :
			m_pState(pState) {}
		template<typename U>
		CAllocator(const CAllocator<U> &Other) :
			m_pState(Other.m_pState)
		{
		}
		T *allocate(size_t Count)
		{
			if(++m_pState->m_Calls == m_pState->m_FailAt || m_pState->m_Fail)
			{
				m_pState->m_Fail |= m_pState->m_LatchFailure;
				throw std::bad_alloc();
			}
			T *pResult = std::allocator<T>{}.allocate(Count);
			m_pState->m_LiveBytes += Count * sizeof(T);
			return pResult;
		}
		void deallocate(T *pData, size_t Count) noexcept
		{
			m_pState->m_LiveBytes -= Count * sizeof(T);
			std::allocator<T>{}.deallocate(pData, Count);
		}
		template<typename U>
		bool operator==(const CAllocator<U> &Other) const noexcept
		{
			return m_pState == Other.m_pState;
		}
		template<typename U>
		bool operator!=(const CAllocator<U> &Other) const noexcept
		{
			return !(*this == Other);
		}
	};

	using CEntry = IServerBrowser::CServerEntry;
	using CStorage = std::deque<CEntry, CAllocator<CEntry>>;
	using CServers = std::vector<CEntry *, CAllocator<CEntry *>>;
	using CAddressMap = std::unordered_map<NETADDR, int, std::hash<NETADDR>, std::equal_to<NETADDR>, CAllocator<std::pair<const NETADDR, int>>>;

	struct CCollections
	{
		CStorage m_Storage;
		CServers m_vpServers;
		CAddressMap m_ByAddress;
		CCollections(CAllocationState &Storage, CAllocationState &Servers, CAllocationState &Map) :
			m_Storage(CAllocator<CEntry>(&Storage)),
			m_vpServers(CAllocator<CEntry *>(&Servers)),
			m_ByAddress(0, std::hash<NETADDR>{}, std::equal_to<NETADDR>{}, CAllocator<std::pair<const NETADDR, int>>(&Map)) {}
	};

	class CEntryPublicationFixture : public testing::Test
	{
	protected:
		CAllocationState m_StorageAlloc, m_ServerAlloc, m_MapAlloc;
		std::unique_ptr<CCollections> m_pData;
		void SetUp() override { m_pData = std::make_unique<CCollections>(m_StorageAlloc, m_ServerAlloc, m_MapAlloc); }
		void TearDown() override
		{
			m_pData.reset();
			EXPECT_EQ(m_StorageAlloc.m_LiveBytes, 0U);
			EXPECT_EQ(m_ServerAlloc.m_LiveBytes, 0U);
			EXPECT_EQ(m_MapAlloc.m_LiveBytes, 0U);
		}
		NETADDR Address(int Port)
		{
			NETADDR Result{};
			Result.type = NETTYPE_IPV4;
			Result.ip[0] = 127;
			Result.ip[3] = 1;
			Result.port = static_cast<unsigned short>(Port);
			return Result;
		}
		CEntry *Add(std::initializer_list<NETADDR> Addresses)
		{
			return server_browser::AddEntry(m_pData->m_Storage, m_pData->m_vpServers, m_pData->m_ByAddress,
				Addresses.begin(), static_cast<int>(Addresses.size()), [](CServerInfo &Info) { str_copy(Info.m_aName, "Prepared"); });
		}
		void ExpectCounts(size_t Nodes, size_t Servers, size_t Addresses)
		{
			EXPECT_EQ(m_pData->m_Storage.size(), Nodes);
			EXPECT_EQ(m_pData->m_vpServers.size(), Servers);
			EXPECT_EQ(m_pData->m_ByAddress.size(), Addresses);
			for(const auto &[Address, Index] : m_pData->m_ByAddress)
			{
				ASSERT_GE(Index, 0);
				ASSERT_LT(static_cast<size_t>(Index), m_pData->m_vpServers.size());
				EXPECT_EQ(m_pData->m_vpServers[Index]->m_Info.m_ServerIndex, Index);
			}
		}
	};
}

#endif
