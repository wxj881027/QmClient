#include <test/support/serverbrowser_entry_fixture.h>

namespace
{
	class ServerBrowserEntryReplace : public ServerBrowserEntryTests::CEntryPublicationFixture
	{
	protected:
		static void Prepare(CServerInfo &Info)
		{
			str_copy(Info.m_aName, "Replacement");
			str_copy(Info.m_aAddress, "Prepared address");
			str_copy(Info.m_aCommunityId, "new");
			str_copy(Info.m_aCommunityCountry, "New country");
			str_copy(Info.m_aCommunityType, "New type");
			Info.m_QmClientCount = 7;
			Info.m_Latency = 999;
			Info.m_HasRank = CServerInfo::RANK_UNAVAILABLE;
			Info.m_Favorite = TRISTATE::ALL;
			Info.m_FavoriteAllowPing = TRISTATE::SOME;
		}
		void Replace(IServerBrowser::CServerEntry &Entry, std::initializer_list<NETADDR> Addresses)
		{
			server_browser::ReplaceEntry(Entry, m_pData->m_ByAddress, Addresses.begin(), static_cast<int>(Addresses.size()), Prepare);
		}
		void SetLoaded(IServerBrowser::CServerEntry &Entry)
		{
			Entry.m_Info.m_Type = 3;
			Entry.m_Info.m_ReceivedPackets = 13;
			Entry.m_Info.m_LatencyIsEstimated = true;
			str_copy(Entry.m_Info.m_aMap, "Actual map");
			Entry.m_Info.m_MapCrc = 123;
			Entry.m_Info.m_MapSize = 456;
			Entry.m_Info.m_ClientScoreKind = CServerInfo::CLIENT_SCORE_KIND_TIME;
			Entry.m_Info.m_vClients.emplace_back();
			str_copy(Entry.m_Info.m_vClients.back().m_aName, "Player");
			Entry.m_GotInfo = 1;
			Entry.m_RequestTime = 17;
			Entry.m_FriendStateValid = true;
			Entry.m_FriendStateRevision = 23;
		}
		void ExpectLoaded(const IServerBrowser::CServerEntry &Entry, const CServerInfo::CClient *pClients)
		{
			EXPECT_EQ(Entry.m_Info.m_Type, 3);
			EXPECT_EQ(Entry.m_Info.m_ReceivedPackets, 13U);
			EXPECT_TRUE(Entry.m_Info.m_LatencyIsEstimated);
			EXPECT_STREQ(Entry.m_Info.m_aMap, "Actual map");
			EXPECT_EQ(Entry.m_Info.m_MapCrc, 123);
			EXPECT_EQ(Entry.m_Info.m_MapSize, 456);
			EXPECT_EQ(Entry.m_Info.m_ClientScoreKind, CServerInfo::CLIENT_SCORE_KIND_TIME);
			ASSERT_EQ(Entry.m_Info.m_vClients.size(), 1U);
			EXPECT_EQ(Entry.m_Info.m_vClients.data(), pClients);
			EXPECT_STREQ(Entry.m_Info.m_vClients.front().m_aName, "Player");
			EXPECT_EQ(Entry.m_GotInfo, 1);
			EXPECT_EQ(Entry.m_RequestTime, 17);
			EXPECT_EQ(Entry.m_FriendStateRevision, 23U);
		}
		void FailMapAt(int Offset)
		{
			m_pData->m_ByAddress.reserve(64);
			m_MapAlloc.m_FailAt = m_MapAlloc.m_Calls + Offset;
			m_MapAlloc.m_LatchFailure = true;
		}
	};
}

TEST_F(ServerBrowserEntryReplace, SuccessCommitsPreparedFieldsAndKeepsPlayersAndEntryIdentity)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pEntry = Add({A});
	SetLoaded(*pEntry);
	const auto *pClients = pEntry->m_Info.m_vClients.data();
	m_StorageAlloc.m_Fail = m_ServerAlloc.m_Fail = true;
	Replace(*pEntry, {B});
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_vpServers.front(), pEntry);
	EXPECT_EQ(m_pData->m_ByAddress.count(A), 0U);
	EXPECT_EQ(m_pData->m_ByAddress.at(B), 0);
	EXPECT_EQ(pEntry->m_Info.m_aAddresses[0], B);
	EXPECT_STREQ(pEntry->m_Info.m_aName, "Replacement");
	EXPECT_STREQ(pEntry->m_Info.m_aAddress, "Prepared address");
	EXPECT_STREQ(pEntry->m_Info.m_aCommunityId, "new");
	EXPECT_STREQ(pEntry->m_Info.m_aCommunityCountry, "New country");
	EXPECT_STREQ(pEntry->m_Info.m_aCommunityType, "New type");
	EXPECT_EQ(pEntry->m_Info.m_QmClientCount, 7);
	EXPECT_EQ(pEntry->m_Info.m_Latency, 999);
	EXPECT_EQ(pEntry->m_Info.m_Favorite, TRISTATE::ALL);
	EXPECT_EQ(pEntry->m_Info.m_FavoriteAllowPing, TRISTATE::SOME);
	EXPECT_FALSE(pEntry->m_FriendStateValid);
	ExpectLoaded(*pEntry, pClients);
}

TEST_F(ServerBrowserEntryReplace, PreparationAllocationFailurePreservesOldInfoAndAddress)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pEntry = Add({A});
	SetLoaded(*pEntry);
	const auto *pClients = pEntry->m_Info.m_vClients.data();
	EXPECT_THROW(server_browser::ReplaceEntry(*pEntry, m_pData->m_ByAddress, &B, 1, [](CServerInfo &Info) {
		str_copy(Info.m_aName, "Unpublished");
		throw std::bad_alloc();
	}),
		std::bad_alloc);
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
	EXPECT_EQ(m_pData->m_ByAddress.count(B), 0U);
	EXPECT_STREQ(pEntry->m_Info.m_aName, "Prepared");
	EXPECT_TRUE(pEntry->m_FriendStateValid);
	ExpectLoaded(*pEntry, pClients);
}

TEST_F(ServerBrowserEntryReplace, UnknownPreparationExceptionDoesNotRemoveOldIndex)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pEntry = Add({A});
	EXPECT_THROW(server_browser::ReplaceEntry(*pEntry, m_pData->m_ByAddress, &B, 1, [](CServerInfo &) { throw 7; }), int);
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
	EXPECT_EQ(pEntry->m_Info.m_aAddresses[0], A);
}

TEST_F(ServerBrowserEntryReplace, FirstNewNodeAllocationFailureKeepsOldAddress)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pEntry = Add({A});
	FailMapAt(1);
	EXPECT_THROW(Replace(*pEntry, {B}), std::bad_alloc);
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
	EXPECT_EQ(m_MapAlloc.m_Calls, m_MapAlloc.m_FailAt);
}

TEST_F(ServerBrowserEntryReplace, LaterAllocationFailureRetractsNewKeyAndKeepsAllOldKeys)
{
	const auto A = Address(8301), B = Address(8302), C = Address(8303), D = Address(8304);
	auto *pEntry = Add({A, B});
	FailMapAt(2);
	EXPECT_THROW(Replace(*pEntry, {C, D}), std::bad_alloc);
	ExpectCounts(1, 1, 2);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
	EXPECT_EQ(m_pData->m_ByAddress.at(B), 0);
	EXPECT_EQ(m_pData->m_ByAddress.count(C), 0U);
	EXPECT_EQ(m_MapAlloc.m_Calls, m_MapAlloc.m_FailAt);
	EXPECT_EQ(pEntry->m_Info.m_NumAddresses, 2);
}

TEST_F(ServerBrowserEntryReplace, PartialFailureRestoresAnotherEntriesOverwrittenIndex)
{
	const auto A = Address(8301), B = Address(8302), C = Address(8303), D = Address(8304);
	auto *pEntry = Add({A});
	auto *pOther = Add({B});
	FailMapAt(2);
	EXPECT_THROW(Replace(*pEntry, {B, C, D}), std::bad_alloc);
	ExpectCounts(2, 2, 2);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
	EXPECT_EQ(m_pData->m_ByAddress.at(B), 1);
	EXPECT_EQ(m_pData->m_vpServers[1], pOther);
	EXPECT_EQ(m_MapAlloc.m_Calls, m_MapAlloc.m_FailAt);
}

TEST_F(ServerBrowserEntryReplace, RehashThenFailureRestoresOverwrittenIndexWithoutAllocation)
{
	const auto A = Address(8301), B = Address(8302), C = Address(8303), D = Address(8304);
	auto *pEntry = Add({A});
	Add({B});
	m_pData->m_ByAddress.max_load_factor(0.01f);
	const auto Buckets = m_pData->m_ByAddress.bucket_count();
	m_MapAlloc.m_FailAt = m_MapAlloc.m_Calls + 3;
	m_MapAlloc.m_LatchFailure = true;
	EXPECT_THROW(Replace(*pEntry, {B, C, D}), std::bad_alloc);
	EXPECT_GT(m_pData->m_ByAddress.bucket_count(), Buckets);
	ExpectCounts(2, 2, 2);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
	EXPECT_EQ(m_pData->m_ByAddress.at(B), 1);
	EXPECT_EQ(m_MapAlloc.m_Calls, m_MapAlloc.m_FailAt);
}

TEST_F(ServerBrowserEntryReplace, DuplicateNewKeysAreRetractedAfterLaterFailure)
{
	const auto A = Address(8301), B = Address(8302), C = Address(8303);
	auto *pEntry = Add({A});
	FailMapAt(2);
	EXPECT_THROW(Replace(*pEntry, {B, B, C}), std::bad_alloc);
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
	EXPECT_EQ(m_pData->m_ByAddress.count(B), 0U);
}

TEST_F(ServerBrowserEntryReplace, DuplicateOldKeysCanBeRemovedOnSuccessfulReplacement)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pEntry = Add({A, A});
	Replace(*pEntry, {B});
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_ByAddress.count(A), 0U);
	EXPECT_EQ(m_pData->m_ByAddress.at(B), 0);
	EXPECT_EQ(pEntry->m_Info.m_NumAddresses, 1);
}

TEST_F(ServerBrowserEntryReplace, PartialAddressOverlapRetainsSharedKeyAndRemovesOnlyDroppedKey)
{
	const auto A = Address(8301), B = Address(8302), C = Address(8303);
	auto *pEntry = Add({A, B});
	Replace(*pEntry, {B, C});
	ExpectCounts(1, 1, 2);
	EXPECT_EQ(m_pData->m_ByAddress.count(A), 0U);
	EXPECT_EQ(m_pData->m_ByAddress.at(B), 0);
	EXPECT_EQ(m_pData->m_ByAddress.at(C), 0);
}

TEST_F(ServerBrowserEntryReplace, SelfAliasedAddressesRemainReadableWithoutAllocation)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pEntry = Add({A, B});
	m_MapAlloc.m_Fail = true;
	server_browser::ReplaceEntry(*pEntry, m_pData->m_ByAddress, pEntry->m_Info.m_aAddresses, 2, Prepare);
	ExpectCounts(1, 1, 2);
	EXPECT_EQ(pEntry->m_Info.m_aAddresses[0], A);
	EXPECT_EQ(pEntry->m_Info.m_aAddresses[1], B);
}

TEST_F(ServerBrowserEntryReplace, BorrowedAddressSubrangeIsCopiedBeforePublishedArrayChanges)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pEntry = Add({A, B});
	server_browser::ReplaceEntry(*pEntry, m_pData->m_ByAddress, pEntry->m_Info.m_aAddresses + 1, 1, Prepare);
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_ByAddress.count(A), 0U);
	EXPECT_EQ(pEntry->m_Info.m_aAddresses[0], B);
}

TEST_F(ServerBrowserEntryReplace, SameReplacementCanSucceedAfterRegistrationFailure)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pEntry = Add({A});
	FailMapAt(1);
	EXPECT_THROW(Replace(*pEntry, {B}), std::bad_alloc);
	m_MapAlloc.m_Fail = false;
	m_MapAlloc.m_FailAt = -1;
	Replace(*pEntry, {B});
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_ByAddress.count(A), 0U);
	EXPECT_EQ(m_pData->m_ByAddress.at(B), 0);
}

TEST_F(ServerBrowserEntryReplace, EmptyReplacementRemovesOldKeysAndPreservesOwnedEntry)
{
	auto *pEntry = Add({Address(8301)});
	Replace(*pEntry, {});
	ExpectCounts(1, 1, 0);
	EXPECT_EQ(m_pData->m_vpServers.front(), pEntry);
	EXPECT_EQ(pEntry->m_Info.m_NumAddresses, 0);
}

TEST_F(ServerBrowserEntryReplace, InvalidNewRangeRejectsBeforePreparingOrChangingIndex)
{
	const auto A = Address(8301);
	auto *pEntry = Add({A});
	bool Prepared = false;
	auto Callback = [&](CServerInfo &) { Prepared = true; };
	EXPECT_THROW(server_browser::ReplaceEntry(*pEntry, m_pData->m_ByAddress, nullptr, 1, Callback), std::invalid_argument);
	EXPECT_THROW(server_browser::ReplaceEntry(*pEntry, m_pData->m_ByAddress, &A, -1, Callback), std::invalid_argument);
	EXPECT_THROW(server_browser::ReplaceEntry(*pEntry, m_pData->m_ByAddress, &A, MAX_SERVER_ADDRESSES + 1, Callback), std::invalid_argument);
	EXPECT_FALSE(Prepared);
	ExpectCounts(1, 1, 1);
}

TEST_F(ServerBrowserEntryReplace, InvalidOldCountRejectsWithoutReadingBeyondAddressArray)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pEntry = Add({A});
	pEntry->m_Info.m_NumAddresses = MAX_SERVER_ADDRESSES + 1;
	EXPECT_THROW(Replace(*pEntry, {B}), std::invalid_argument);
	ExpectCounts(1, 1, 1);
	EXPECT_EQ(m_pData->m_ByAddress.at(A), 0);
}

TEST_F(ServerBrowserEntryReplace, SuccessfulOverlapKeepsOriginalDestinationIndexOverwriteRule)
{
	const auto A = Address(8301), B = Address(8302);
	auto *pEntry = Add({A});
	auto *pOther = Add({B});
	Replace(*pEntry, {B});
	ExpectCounts(2, 2, 1);
	EXPECT_EQ(m_pData->m_ByAddress.at(B), 0);
	EXPECT_EQ(m_pData->m_vpServers[1], pOther);
}
