#include <test/support/serverbrowser_entry_fixture.h>

using ServerBrowserEntryPublication = ServerBrowserEntryTests::CEntryPublicationFixture;

TEST_F(ServerBrowserEntryPublication, SuccessOwnsNodeAndRegistersAllAddressesAtOnePublishedIndex)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pEntry = Add({A, B});
	ExpectCounts(1, 1, 2);
	EXPECT_EQ(&m_pData->m_Storage.front(), pEntry);
	EXPECT_EQ(m_pData->m_vpServers.front(), pEntry);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
	EXPECT_EQ(m_pData->m_ByAddress.at(B), 0);
	EXPECT_EQ(pEntry->m_Info.m_NumAddresses, 2);
	EXPECT_STREQ(pEntry->m_Info.m_aName, "Prepared");
}

TEST_F(ServerBrowserEntryPublication, PreparationAllocationFailureRemovesUnpublishedNode)
{
	const auto A = Address(8301);
	EXPECT_THROW(server_browser::AddEntry(m_pData->m_Storage, m_pData->m_vpServers, m_pData->m_ByAddress, &A, 1,
			     [](CServerInfo &Info) { Info.m_vClients.emplace_back(); throw std::bad_alloc(); }),
		std::bad_alloc);
	ExpectCounts(0, 0, 0);
}

TEST_F(ServerBrowserEntryPublication, UnknownPreparationExceptionPropagatesWithoutPublication)
{
	const auto A = Address(8301);
	EXPECT_THROW(server_browser::AddEntry(m_pData->m_Storage, m_pData->m_vpServers, m_pData->m_ByAddress, &A, 1,
			     [](CServerInfo &) { throw 7; }),
		int);
	ExpectCounts(0, 0, 0);
}

TEST_F(ServerBrowserEntryPublication, StorageGrowthFailureKeepsPreviouslyPublishedEntries)
{
	m_StorageAlloc.m_Fail = true;
	bool Failed = false;
	// deque 的预分配块大小依实现而异，等到真实分配边界，循环严格有界。
	for(int Index = 0; Index < 256 && !Failed; ++Index)
	{
		const size_t Before = m_pData->m_vpServers.size();
		try
		{
			Add({Address(8300 + Index)});
		}
		catch(const std::bad_alloc &)
		{
			Failed = true;
			ExpectCounts(Before, Before, Before);
		}
	}
	EXPECT_TRUE(Failed);
}

TEST_F(ServerBrowserEntryPublication, InitialPointerReservationFailureLeavesNoAddressIndex)
{
	m_ServerAlloc.m_Fail = true;
	EXPECT_THROW(Add({Address(8301)}), std::bad_alloc);
	ExpectCounts(0, 0, 0);
}

TEST_F(ServerBrowserEntryPublication, PointerListGrowthFailureKeepsOldNodesAndMappings)
{
	auto *pOld = Add({Address(8301)});
	const size_t Capacity = m_pData->m_vpServers.capacity();
	ASSERT_LE(Capacity, 4096U);
	for(size_t Index = 1; Index < Capacity; ++Index)
		Add({Address(8301 + static_cast<int>(Index))});
	m_ServerAlloc.m_Fail = true;
	EXPECT_THROW(Add({Address(20000)}), std::bad_alloc);
	ExpectCounts(Capacity, Capacity, Capacity);
	EXPECT_EQ(m_pData->m_vpServers.front(), pOld);
	EXPECT_EQ(m_pData->m_ByAddress.count(Address(20000)), 0U);
}

TEST_F(ServerBrowserEntryPublication, FirstAddressAllocationFailureRetractsPointerAndNode)
{
	m_pData->m_ByAddress.reserve(64);
	m_MapAlloc.m_Fail = true;
	EXPECT_THROW(Add({Address(8301)}), std::bad_alloc);
	ExpectCounts(0, 0, 0);
}

TEST_F(ServerBrowserEntryPublication, LaterAddressAllocationFailureRemovesEarlierNewMappings)
{
	m_pData->m_ByAddress.reserve(64);
	m_MapAlloc.m_FailAt = m_MapAlloc.m_Calls + 2;
	EXPECT_THROW(Add({Address(8301), Address(8302)}), std::bad_alloc);
	ExpectCounts(0, 0, 0);
}

TEST_F(ServerBrowserEntryPublication, PartialFailureRestoresOverwrittenOldAddressAndRemovesNewAddress)
{
	const auto A = Address(8301), B = Address(8302), C = Address(8303);
	auto *pOld = Add({A});
	m_pData->m_ByAddress.reserve(64);
	m_MapAlloc.m_FailAt = m_MapAlloc.m_Calls + 2;
	EXPECT_THROW(Add({A, B, C}), std::bad_alloc);
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
	EXPECT_EQ(m_pData->m_vpServers.front(), pOld);
	EXPECT_EQ(m_pData->m_ByAddress.count(B), 0U);
	EXPECT_EQ(m_pData->m_ByAddress.count(C), 0U);
}

TEST_F(ServerBrowserEntryPublication, RollbackAfterRehashRestoresOldMappingThroughStableValueAddress)
{
	const auto A = Address(8301), B = Address(8302), C = Address(8303);
	Add({A});
	m_pData->m_ByAddress.max_load_factor(0.01f);
	const auto Buckets = m_pData->m_ByAddress.bucket_count();
	// 新节点和扩桶成功后，下一次节点分配失败，检查已保存映射值的地址仍有效。
	m_MapAlloc.m_FailAt = m_MapAlloc.m_Calls + 3;
	EXPECT_THROW(Add({A, B, C}), std::bad_alloc);
	EXPECT_GT(m_pData->m_ByAddress.bucket_count(), Buckets);
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
}

TEST_F(ServerBrowserEntryPublication, DuplicateNewAddressIsRemovedAfterLaterAllocationFailure)
{
	const auto A = Address(8301);
	m_pData->m_ByAddress.reserve(64);
	m_MapAlloc.m_FailAt = m_MapAlloc.m_Calls + 2;
	EXPECT_THROW(Add({A, A, Address(8302)}), std::bad_alloc);
	ExpectCounts(0, 0, 0);
}

TEST_F(ServerBrowserEntryPublication, DuplicateOldAddressRestoresOriginalIndexAfterFailure)
{
	const auto A = Address(8301);
	Add({A});
	m_pData->m_ByAddress.reserve(64);
	m_MapAlloc.m_FailAt = m_MapAlloc.m_Calls + 1;
	EXPECT_THROW(Add({A, A, Address(8302)}), std::bad_alloc);
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
}

TEST_F(ServerBrowserEntryPublication, RetryingSameAddressesAfterFailurePublishesCompactIndices)
{
	const auto A = Address(8301), B = Address(8302);
	m_ServerAlloc.m_Fail = true;
	EXPECT_THROW(Add({A}), std::bad_alloc);
	m_ServerAlloc.m_Fail = false;
	Add({A});
	Add({B});
	ExpectCounts(2, 2, 2);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
	EXPECT_EQ(m_pData->m_ByAddress.at(B), 1);
}

TEST_F(ServerBrowserEntryPublication, SuccessfulOverlapPreservesExistingLastEntryMappingRule)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pOld = Add({A});
	auto *pNew = Add({A, B});
	ExpectCounts(2, 2, 2);
	EXPECT_EQ(m_pData->m_vpServers[0], pOld);
	EXPECT_EQ(m_pData->m_vpServers[1], pNew);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 1);
	EXPECT_EQ(m_pData->m_ByAddress.at(B), 1);
}

TEST_F(ServerBrowserEntryPublication, MaximumAddressCountPublishesEveryAddress)
{
	std::array<NETADDR, MAX_SERVER_ADDRESSES> aAddresses;
	for(int Index = 0; Index < MAX_SERVER_ADDRESSES; ++Index)
		aAddresses[Index] = Address(8300 + Index);
	server_browser::AddEntry(m_pData->m_Storage, m_pData->m_vpServers, m_pData->m_ByAddress,
		aAddresses.data(), static_cast<int>(aAddresses.size()), [](CServerInfo &) {});
	ExpectCounts(1, 1, MAX_SERVER_ADDRESSES);
}

TEST_F(ServerBrowserEntryPublication, InvalidAddressRangeRejectsBeforePreparingOrPublishing)
{
	const auto A = Address(8301);
	bool Prepared = false;
	auto Prepare = [&](CServerInfo &) { Prepared = true; };
	EXPECT_THROW(server_browser::AddEntry(m_pData->m_Storage, m_pData->m_vpServers, m_pData->m_ByAddress, &A, -1, Prepare), std::invalid_argument);
	EXPECT_THROW(server_browser::AddEntry(m_pData->m_Storage, m_pData->m_vpServers, m_pData->m_ByAddress, &A, MAX_SERVER_ADDRESSES + 1, Prepare), std::invalid_argument);
	EXPECT_THROW(server_browser::AddEntry(m_pData->m_Storage, m_pData->m_vpServers, m_pData->m_ByAddress, nullptr, 1, Prepare), std::invalid_argument);
	EXPECT_FALSE(Prepared);
	ExpectCounts(0, 0, 0);
}

TEST_F(ServerBrowserEntryPublication, ZeroAddressCountDoesNotReadNullInput)
{
	auto *pEntry = server_browser::AddEntry(m_pData->m_Storage, m_pData->m_vpServers, m_pData->m_ByAddress,
		nullptr, 0, [](CServerInfo &) {});
	ASSERT_NE(pEntry, nullptr);
	ExpectCounts(1, 1, 0);
	EXPECT_EQ(pEntry->m_Info.m_NumAddresses, 0);
}
