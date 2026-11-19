#include <base/system.h>

#include <game/client/components/qmclient/browser_friend_ui_id.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <set>

TEST(BrowserFriendUiId, NamesDistinguishRowsOnThirtyTwoBits)
{
	const unsigned ClanHash = str_quickhash("Clan");
	const unsigned AddressHash = str_quickhash("127.0.0.1:8303");
	EXPECT_NE(QmBrowserFriendUiIdBase<uint32_t>(str_quickhash("Alice"), ClanHash, AddressHash, 2),
		QmBrowserFriendUiIdBase<uint32_t>(str_quickhash("Bob"), ClanHash, AddressHash, 2));
}

TEST(BrowserFriendUiId, HighNameHalfContributesOnThirtyTwoBits)
{
	EXPECT_NE(QmBrowserFriendUiIdBase<uint32_t>(1, 0, 0, 0), QmBrowserFriendUiIdBase<uint32_t>(2, 0, 0, 0));
}

TEST(BrowserFriendUiId, ClanAddressAndFriendStateDistinguishRowsOnThirtyTwoBits)
{
	const uint32_t Initial = QmBrowserFriendUiIdBase<uint32_t>(7, 11, 13, 2);
	EXPECT_NE(Initial, QmBrowserFriendUiIdBase<uint32_t>(7, 19, 13, 2));
	EXPECT_NE(Initial, QmBrowserFriendUiIdBase<uint32_t>(7, 11, 23, 2));
	EXPECT_NE(Initial, QmBrowserFriendUiIdBase<uint32_t>(7, 11, 13, 1));
}

TEST(BrowserFriendUiId, SixtyFourBitsKeepTheEstablishedIdentityLayout)
{
	EXPECT_EQ(QmBrowserFriendUiIdBase<uint64_t>(0x12345678U, 0x87654321U, 0x10203040U, 1), UINT64_C(0x2345678a72523a50));
	EXPECT_NE(QmBrowserFriendUiIdBase<uint64_t>(1, 0, 0, 0), QmBrowserFriendUiIdBase<uint64_t>(2, 0, 0, 0));
}

TEST(BrowserFriendUiId, EmptyOrShiftedOutIdentityKeepsANonzeroBase)
{
	EXPECT_EQ(QmBrowserFriendUiIdBase<uint32_t>(0, 0, 0, 0), 0x10U);
	EXPECT_EQ(QmBrowserFriendUiIdBase<uint64_t>(0, 0, 0, 0), UINT64_C(0x10));
	EXPECT_EQ(QmBrowserFriendUiIdBase<uint32_t>(0xf0000000U, 0, 0, 0), 0x10U);
	EXPECT_EQ(QmBrowserFriendUiIdBase<uint64_t>(0xf0000000U, 0, 0, 0), UINT64_C(0x10));
}

TEST(BrowserFriendUiId, MaximumHashesReserveEveryControlTagBit)
{
	const uint32_t Narrow = QmBrowserFriendUiIdBase<uint32_t>(UINT32_MAX, UINT32_MAX, UINT32_MAX, 2);
	const uint64_t Wide = QmBrowserFriendUiIdBase<uint64_t>(UINT32_MAX, UINT32_MAX, UINT32_MAX, 2);
	EXPECT_EQ(Narrow & 0xfU, 0U);
	EXPECT_EQ(Wide & UINT64_C(0xf), UINT64_C(0));
	EXPECT_NE(Narrow, 0U);
	EXPECT_NE(Wide, UINT64_C(0));
}

TEST(BrowserFriendUiId, TwoRowsKeepTheirSevenControlIdsSeparateOnThirtyTwoBits)
{
	const std::array<uint32_t, 7> aTags = {0x1, 0x3, 0x5, 0x7, 0x9, 0xb, 0xd};
	std::set<uint32_t> Ids;
	for(const char *pName : {"Alice", "Bob"})
	{
		const uint32_t Base = QmBrowserFriendUiIdBase<uint32_t>(str_quickhash(pName), str_quickhash("Clan"), 0, 2);
		for(uint32_t Tag : aTags)
			EXPECT_TRUE(Ids.insert(Base | Tag).second);
	}
	EXPECT_EQ(Ids.size(), 14U);
}
