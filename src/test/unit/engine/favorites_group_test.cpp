#include <base/system.h>

#include <engine/favorites.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>

namespace
{
	class FavoritesGroup : public ::testing::Test
	{
	protected:
		std::unique_ptr<IFavorites> m_pFavorites = CreateFavorites();
		NETADDR m_A{};
		NETADDR m_B{};
		NETADDR m_Other{};

		void SetUp() override
		{
			ASSERT_EQ(net_addr_from_str(&m_A, "127.0.0.1:8303"), 0);
			ASSERT_EQ(net_addr_from_str(&m_B, "127.0.0.2:8303"), 0);
			ASSERT_EQ(net_addr_from_str(&m_Other, "127.0.0.3:8303"), 0);
		}
	};
}

TEST_F(FavoritesGroup, DuplicateAddressesKeepTheirFirstOccurrence)
{
	const std::array<NETADDR, 3> aGroup{m_A, m_A, m_B};
	m_pFavorites->Add(aGroup.data(), static_cast<int>(aGroup.size()));
	const IFavorites::CEntry *pEntries;
	int NumEntries;
	m_pFavorites->AllEntries(&pEntries, &NumEntries);
	ASSERT_EQ(NumEntries, 1);
	ASSERT_EQ(pEntries[0].m_NumAddrs, 2);
	EXPECT_EQ(pEntries[0].m_aAddrs[0], m_A);
	EXPECT_EQ(pEntries[0].m_aAddrs[1], m_B);
	EXPECT_EQ(m_pFavorites->IsFavorite(aGroup.data(), static_cast<int>(aGroup.size())), TRISTATE::ALL);
}

TEST_F(FavoritesGroup, ReaddingTheSameAddressTwiceDoesNotReadARetiredEntry)
{
	m_pFavorites->Add(&m_A, 1);
	const std::array<NETADDR, 2> aGroup{m_A, m_A};
	m_pFavorites->Add(aGroup.data(), static_cast<int>(aGroup.size()));
	const IFavorites::CEntry *pEntries;
	int NumEntries;
	m_pFavorites->AllEntries(&pEntries, &NumEntries);
	ASSERT_EQ(NumEntries, 1);
	ASSERT_EQ(pEntries[0].m_NumAddrs, 1);
	EXPECT_EQ(pEntries[0].m_aAddrs[0], m_A);
	EXPECT_EQ(m_pFavorites->IsFavorite(&m_A, 1), TRISTATE::ALL);
}

TEST_F(FavoritesGroup, RegroupingDuplicatesKeepsTheUnselectedAddress)
{
	const std::array<NETADDR, 2> aOriginal{m_A, m_B};
	m_pFavorites->Add(aOriginal.data(), static_cast<int>(aOriginal.size()));
	m_pFavorites->AllowPing(aOriginal.data(), static_cast<int>(aOriginal.size()), true);
	const std::array<NETADDR, 2> aRepeated{m_A, m_A};
	m_pFavorites->Add(aRepeated.data(), static_cast<int>(aRepeated.size()));
	m_pFavorites->Remove(&m_A, 1);
	EXPECT_EQ(m_pFavorites->IsFavorite(&m_A, 1), TRISTATE::NONE);
	EXPECT_EQ(m_pFavorites->IsFavorite(&m_B, 1), TRISTATE::ALL);
	EXPECT_EQ(m_pFavorites->IsPingAllowed(&m_B, 1), TRISTATE::ALL);
	const IFavorites::CEntry *pEntries;
	int NumEntries;
	m_pFavorites->AllEntries(&pEntries, &NumEntries);
	ASSERT_EQ(NumEntries, 1);
	ASSERT_EQ(pEntries[0].m_NumAddrs, 1);
	EXPECT_EQ(pEntries[0].m_aAddrs[0], m_B);
}

TEST_F(FavoritesGroup, CompactingAfterDuplicateRegroupingKeepsAddressIndexesValid)
{
	m_pFavorites->Add(&m_Other, 1);
	const std::array<NETADDR, 3> aGroup{m_A, m_A, m_B};
	m_pFavorites->Add(aGroup.data(), static_cast<int>(aGroup.size()));
	m_pFavorites->Add(&m_A, 1);
	m_pFavorites->Remove(&m_A, 1);
	EXPECT_NO_THROW(m_pFavorites->Remove(&m_Other, 1));
	EXPECT_EQ(m_pFavorites->IsFavorite(&m_A, 1), TRISTATE::NONE);
	EXPECT_EQ(m_pFavorites->IsFavorite(&m_B, 1), TRISTATE::ALL);
	EXPECT_EQ(m_pFavorites->IsFavorite(&m_Other, 1), TRISTATE::NONE);
	const IFavorites::CEntry *pEntries;
	int NumEntries;
	m_pFavorites->AllEntries(&pEntries, &NumEntries);
	ASSERT_EQ(NumEntries, 1);
	ASSERT_EQ(pEntries[0].m_NumAddrs, 1);
	EXPECT_EQ(pEntries[0].m_aAddrs[0], m_B);
}

TEST_F(FavoritesGroup, RegroupingABorrowedAddressListCopiesItBeforeCompaction)
{
	const std::array<NETADDR, 2> aGroup{m_A, m_B};
	m_pFavorites->Add(aGroup.data(), static_cast<int>(aGroup.size()));
	m_pFavorites->Add(&m_Other, 1);
	const IFavorites::CEntry *pEntries;
	int NumEntries;
	m_pFavorites->AllEntries(&pEntries, &NumEntries);
	ASSERT_EQ(NumEntries, 2);
	m_pFavorites->Add(pEntries[0].m_aAddrs, pEntries[0].m_NumAddrs);
	EXPECT_EQ(m_pFavorites->IsFavorite(aGroup.data(), static_cast<int>(aGroup.size())), TRISTATE::ALL);
	EXPECT_EQ(m_pFavorites->IsFavorite(&m_Other, 1), TRISTATE::ALL);
	m_pFavorites->AllEntries(&pEntries, &NumEntries);
	ASSERT_EQ(NumEntries, 2);
	EXPECT_EQ(pEntries[1].m_NumAddrs, 2);
	EXPECT_EQ(pEntries[1].m_aAddrs[0], m_A);
	EXPECT_EQ(pEntries[1].m_aAddrs[1], m_B);
}
