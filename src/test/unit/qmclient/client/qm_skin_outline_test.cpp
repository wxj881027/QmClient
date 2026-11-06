#include <game/client/components/qmclient/qm_skin_outline.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace
{
	struct SAllocationState
	{
		int m_Calls = 0;
		int m_FailAt = 0;
	};
	thread_local SAllocationState *s_pAllocation = nullptr;
	class CAllocationScope
	{
		SAllocationState *m_pPrevious;

	public:
		explicit CAllocationScope(SAllocationState &State) :
			m_pPrevious(s_pAllocation) { s_pAllocation = &State; }
		~CAllocationScope() { s_pAllocation = m_pPrevious; }
	};
	void *Allocate(size_t Size) noexcept
	{
		if(++s_pAllocation->m_Calls == s_pAllocation->m_FailAt)
			return nullptr;
		return std::malloc(Size);
	}
	struct SImageOwner
	{
		CImageInfo m_Image;
		explicit SImageOwner(CImageInfo Image) :
			m_Image(std::move(Image)) {}
		~SImageOwner() { m_Image.Free(); }
	};
}

class CQmSkinOutlineImageTest : public ::testing::Test
{
protected:
	std::array<uint8_t, 16> m_aPixels;
	CImageInfo m_Source;
	SAllocationState m_Allocation;
	CAllocationScope m_Scope{m_Allocation};
	void SetUp() override
	{
		m_aPixels.fill(255);
		m_Source.m_Width = m_Source.m_Height = 2;
		m_Source.m_Format = CImageInfo::FORMAT_RGBA;
		m_Source.m_pData = m_aPixels.data();
	}
};

TEST(QmSkinOutline, SelectsLocalAndOtherPlayers)
{
	EXPECT_TRUE(QmShouldDrawSkinOutline(0, 0, 1, true, false));
	EXPECT_TRUE(QmShouldDrawSkinOutline(2, 0, 1, false, true));
	EXPECT_FALSE(QmShouldDrawSkinOutline(2, 0, 1, true, false));
	EXPECT_FALSE(QmShouldDrawSkinOutline(-1, 0, 1, true, true));
}

TEST_F(CQmSkinOutlineImageTest, MaskAllocationFailureProducesEmptyImage)
{
	m_Allocation.m_FailAt = 1;
	CQmSkinOutline Outline(m_Source, {0, 0}, {0, 0}, {2, 2}, {2, 2}, Allocate);
	SImageOwner Result(Outline.BuildImage(1));
	EXPECT_EQ(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Image.m_Width, 0u);
	EXPECT_EQ(m_Allocation.m_Calls, 1);
}

TEST_F(CQmSkinOutlineImageTest, ExpansionAllocationFailurePreservesMaskForRetry)
{
	CQmSkinOutline Outline(m_Source, {0, 0}, {0, 0}, {2, 2}, {2, 2}, Allocate);
	m_Allocation.m_FailAt = 2;
	SImageOwner Failed(Outline.BuildImage(1));
	EXPECT_EQ(Failed.m_Image.m_pData, nullptr);
	m_Allocation.m_FailAt = 0;
	SImageOwner Recovered(Outline.BuildImage(1));
	ASSERT_NE(Recovered.m_Image.m_pData, nullptr);
	EXPECT_EQ(Recovered.m_Image.m_Width, 6u);
	EXPECT_EQ(Recovered.m_Image.m_pData[(2 * 6 + 1) * 4 + 3], 255);
	EXPECT_EQ(m_Allocation.m_Calls, 3);
}

TEST_F(CQmSkinOutlineImageTest, OpaqueSquareProducesOnlyTheOuterCircularRing)
{
	CQmSkinOutline Outline(m_Source, {0, 0}, {0, 0}, {2, 2}, {2, 2});
	SImageOwner Result(Outline.BuildImage(1));
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	ASSERT_EQ(Result.m_Image.m_Width, 6u);
	ASSERT_EQ(Result.m_Image.m_Height, 6u);
	const auto Alpha = [&](int X, int Y) { return Result.m_Image.m_pData[(Y * 6 + X) * 4 + 3]; };
	EXPECT_EQ(Alpha(2, 2), 0);
	EXPECT_EQ(Alpha(1, 2), 255);
	EXPECT_EQ(Alpha(2, 1), 255);
	EXPECT_EQ(Alpha(1, 1), 0);
	EXPECT_EQ(Alpha(0, 2), 0);
}

TEST_F(CQmSkinOutlineImageTest, PreparedMaskOwnsPixelsIndependentOfSourceAndEarlierResults)
{
	CQmSkinOutline Outline(m_Source, {0, 0}, {0, 0}, {2, 2}, {2, 2});
	m_aPixels.fill(0);
	SImageOwner First(Outline.BuildImage(1));
	ASSERT_NE(First.m_Image.m_pData, nullptr);
	First.m_Image.m_pData[(2 * 6 + 1) * 4 + 3] = 0;
	SImageOwner Second(Outline.BuildImage(1));
	ASSERT_NE(Second.m_Image.m_pData, nullptr);
	EXPECT_EQ(Second.m_Image.m_pData[(2 * 6 + 1) * 4 + 3], 255);
}

TEST_F(CQmSkinOutlineImageTest, InvalidSourceRegionsDoNotAllocate)
{
	for(const ivec2 Position : {ivec2(-1, 0), ivec2(1, 1)})
	{
		CQmSkinOutline Outline(m_Source, Position, {0, 0}, {2, 2}, {2, 2}, Allocate);
		SImageOwner Result(Outline.BuildImage(1));
		EXPECT_EQ(Result.m_Image.m_pData, nullptr);
	}
	m_Source.m_Width = std::numeric_limits<size_t>::max();
	CQmSkinOutline Overflow(m_Source, {0, 0}, {0, 0}, {2, 2}, {2, 2}, Allocate);
	SImageOwner Result(Overflow.BuildImage(1));
	EXPECT_EQ(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(m_Allocation.m_Calls, 0);
}

TEST_F(CQmSkinOutlineImageTest, DisabledWidthDoesNotAllocateExpandedImage)
{
	CQmSkinOutline Outline(m_Source, {0, 0}, {0, 0}, {2, 2}, {2, 2}, Allocate);
	SImageOwner Zero(Outline.BuildImage(0));
	SImageOwner Negative(Outline.BuildImage(-1));
	EXPECT_EQ(Zero.m_Image.m_pData, nullptr);
	EXPECT_EQ(Negative.m_Image.m_pData, nullptr);
	EXPECT_EQ(m_Allocation.m_Calls, 1);
}

TEST_F(CQmSkinOutlineImageTest, WidthAboveExistingLimitProducesTheSameImage)
{
	CQmSkinOutline Outline(m_Source, {0, 0}, {0, 0}, {2, 2}, {2, 2});
	SImageOwner Limit(Outline.BuildImage(6));
	SImageOwner Above(Outline.BuildImage(100));
	ASSERT_NE(Limit.m_Image.m_pData, nullptr);
	ASSERT_NE(Above.m_Image.m_pData, nullptr);
	EXPECT_TRUE(Limit.m_Image.DataEquals(Above.m_Image));
}

TEST_F(CQmSkinOutlineImageTest, LargeMaskIsResizedBeforeUsingBoundedExpansionQueue)
{
	std::vector<uint8_t> vPixels(256 * 256 * 4, 255);
	m_Source.m_Width = m_Source.m_Height = 256;
	m_Source.m_pData = vPixels.data();
	CQmSkinOutline Outline(m_Source, {0, 0}, {0, 0}, {256, 256}, {64, 64});
	SImageOwner Result(Outline.BuildImage(1));
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	EXPECT_EQ(Result.m_Image.m_Width, 134u);
	EXPECT_EQ(Result.m_Image.m_Height, 134u);
}

TEST_F(CQmSkinOutlineImageTest, FillAndOutlineUseMaximumAlphaWithoutFillingTheInterior)
{
	m_aPixels[3] = 64;
	m_aPixels[7] = 128;
	CQmSkinOutline Outline(m_Source, {0, 0}, {1, 0}, {1, 1}, {1, 1});
	SImageOwner Result(Outline.BuildImage(1));
	ASSERT_NE(Result.m_Image.m_pData, nullptr);
	ASSERT_EQ(Result.m_Image.m_Width, 5u);
	EXPECT_EQ(Result.m_Image.m_pData[(2 * 5 + 1) * 4 + 3], 128);
	EXPECT_EQ(Result.m_Image.m_pData[(2 * 5 + 2) * 4 + 3], 0);
}
