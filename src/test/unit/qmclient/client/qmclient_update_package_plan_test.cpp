#include <game/client/components/qmclient/update_package.h>

#include <gtest/gtest.h>

TEST(QmPackageRanges, SmallPackagesUseOneConnection)
{
	for(int64_t Size : {1LL, 1024LL, 1024LL * 1024})
	{
		SCOPED_TRACE(Size);
		const auto Ranges = qm_update::PlanPackageRanges(Size);
		ASSERT_EQ(Ranges.size(), 1U);
		EXPECT_EQ(Ranges.front().m_First, 0);
		EXPECT_EQ(Ranges.front().m_Last, Size - 1);
	}
}

TEST(QmPackageRanges, UnevenAndLargePackagesCoverEveryByteWithoutOverlap)
{
	for(int64_t Size : {4LL * 1024 * 1024 + 3, 5LL * 1024 * 1024 * 1024})
	{
		SCOPED_TRACE(Size);
		const auto Ranges = qm_update::PlanPackageRanges(Size);
		ASSERT_EQ(Ranges.size(), 4U);
		EXPECT_EQ(Ranges.front().m_First, 0);
		EXPECT_EQ(Ranges.back().m_Last, Size - 1);
		for(size_t Index = 1; Index < Ranges.size(); ++Index)
			EXPECT_EQ(Ranges[Index].m_First, Ranges[Index - 1].m_Last + 1);
	}
}

TEST(QmPackageRanges, InvalidSizeCannotStartSegments)
{
	EXPECT_TRUE(qm_update::PlanPackageRanges(0).empty());
	EXPECT_TRUE(qm_update::PlanPackageRanges(-1).empty());
}
