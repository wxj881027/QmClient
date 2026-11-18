#include <base/system.h>

#include <engine/favorites.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <limits>
#include <new>

TEST(FavoritesAllocation, RegroupingPreservesEntriesAndPingPermissionOnAllocationFailure)
{
	const NETADDR A{NETTYPE_IPV4, {127, 0, 0, 1}, 8303};
	const NETADDR B{NETTYPE_IPV4, {127, 0, 0, 2}, 8303};
	const NETADDR C{NETTYPE_IPV4, {127, 0, 0, 3}, 8303};
	const NETADDR D{NETTYPE_IPV4, {127, 0, 0, 4}, 8303};
	const NETADDR E{NETTYPE_IPV4, {127, 0, 0, 5}, 8303};
	const std::array aOriginal{A, B};
	const std::array aRegroup{A, A, C, D, E};
	const auto MakeFavorites = [&] {
		auto pFavorites = CreateFavorites();
		pFavorites->Add(aOriginal.data(), aOriginal.size());
		pFavorites->AllowPing(aOriginal.data(), aOriginal.size(), true);
		pFavorites->Add(&C, 1);
		return pFavorites;
	};

	size_t AllocationCount;
	{
		auto pFavorites = MakeFavorites();
		CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
		pFavorites->Add(aRegroup.data(), aRegroup.size());
		AllocationCount = Count.Attempts();
	}
	ASSERT_GT(AllocationCount, 0U);

	for(size_t BeforeFailure = 0; BeforeFailure < AllocationCount; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		auto pFavorites = MakeFavorites();
		bool Threw = false;
		bool Triggered;
		{
			CPredictionAllocationFailure Failure(BeforeFailure);
			try
			{
				pFavorites->Add(aRegroup.data(), aRegroup.size());
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Failure.Triggered();
		}
		ASSERT_TRUE(Triggered);
		ASSERT_FALSE(Threw);

		const IFavorites::CEntry *pEntries;
		int NumEntries;
		pFavorites->AllEntries(&pEntries, &NumEntries);
		ASSERT_EQ(NumEntries, 2);
		ASSERT_EQ(pEntries[0].m_NumAddrs, 2);
		EXPECT_EQ(pEntries[0].m_aAddrs[0], A);
		EXPECT_EQ(pEntries[0].m_aAddrs[1], B);
		ASSERT_EQ(pEntries[1].m_NumAddrs, 1);
		EXPECT_EQ(pEntries[1].m_aAddrs[0], C);
		EXPECT_EQ(pFavorites->IsFavorite(aOriginal.data(), aOriginal.size()), TRISTATE::ALL);
		EXPECT_EQ(pFavorites->IsFavorite(&C, 1), TRISTATE::ALL);
		EXPECT_EQ(pFavorites->IsFavorite(&D, 1), TRISTATE::NONE);
		EXPECT_EQ(pFavorites->IsFavorite(&E, 1), TRISTATE::NONE);
		EXPECT_EQ(pFavorites->IsPingAllowed(&A, 1), TRISTATE::ALL);
		EXPECT_EQ(pFavorites->IsPingAllowed(&C, 1), TRISTATE::NONE);

		pFavorites->Add(aRegroup.data(), aRegroup.size());
		EXPECT_EQ(pFavorites->IsFavorite(aRegroup.data(), aRegroup.size()), TRISTATE::ALL);
		EXPECT_EQ(pFavorites->IsPingAllowed(&A, 1), TRISTATE::NONE);
		EXPECT_EQ(pFavorites->IsPingAllowed(&B, 1), TRISTATE::ALL);
	}
}
