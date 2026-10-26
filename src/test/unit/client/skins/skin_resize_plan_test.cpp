#include <base/system.h>

#include <game/client/components/skins.h>

#include <gtest/gtest.h>

#include <cstdlib>

namespace
{
	void KeepOriginalImage(CImageInfo &, int, int) {}
	void UnexpectedResize(CImageInfo &, int, int) { ADD_FAILURE() << "An already divisible image must not be resized"; }

	class CSkinResizePlan : public ::testing::Test
	{
	protected:
		CImageInfo m_Image;
		CSkins::SSkinDataPlan m_Plan;
		const CSkins::SSkinSpriteSpec m_Body{8, 4, 0, 0, 3, 3};
		const CSkins::SSkinSpriteSpec m_BodyOutline{8, 4, 3, 0, 3, 3};
		const CSkins::SSkinSpriteSpec m_Feet{8, 4, 6, 1, 2, 1};
		const CSkins::SSkinSpriteSpec m_FeetOutline{8, 4, 6, 2, 2, 1};

		void Prepare(int Width, int Height)
		{
			m_Image.m_Width = Width;
			m_Image.m_Height = Height;
			m_Image.m_Format = CImageInfo::FORMAT_RGBA;
			m_Image.m_pData = static_cast<uint8_t *>(malloc(m_Image.DataSize()));
			ASSERT_NE(m_Image.m_pData, nullptr);
			mem_set(m_Image.m_pData, 255, m_Image.DataSize());
			m_Plan.m_Body.m_MaxWidth = 77;
			m_Plan.m_Feet.m_MaxHeight = 88;
		}
		void TearDown() override { m_Image.Free(); }
		bool Build(void (*pfnResize)(CImageInfo &, int, int) = ResizeImage)
		{
			return CSkins::BuildSkinDataPlan(m_Image, m_Body, m_BodyOutline, m_Feet, m_FeetOutline, m_Plan, pfnResize);
		}
	};
}

TEST_F(CSkinResizePlan, FailedWidthResizeKeepsTheOriginalImageAndPlan)
{
	Prepare(1, 4);
	const auto *pPixels = m_Image.m_pData;
	EXPECT_FALSE(Build(KeepOriginalImage));
	EXPECT_EQ(m_Image.m_Width, 1u);
	EXPECT_EQ(m_Image.m_Height, 4u);
	EXPECT_EQ(m_Image.m_pData, pPixels);
	EXPECT_EQ(m_Plan.m_Body.m_MaxWidth, 77);
	EXPECT_EQ(m_Plan.m_Feet.m_MaxHeight, 88);
}

TEST_F(CSkinResizePlan, FailedHeightResizeKeepsTheOriginalImageAndPlan)
{
	Prepare(8, 1);
	const auto *pPixels = m_Image.m_pData;
	EXPECT_FALSE(Build(KeepOriginalImage));
	EXPECT_EQ(m_Image.m_Width, 8u);
	EXPECT_EQ(m_Image.m_Height, 1u);
	EXPECT_EQ(m_Image.m_pData, pPixels);
	EXPECT_EQ(m_Plan.m_Body.m_MaxWidth, 77);
	EXPECT_EQ(m_Plan.m_Feet.m_MaxHeight, 88);
}

TEST_F(CSkinResizePlan, DivisibleImageDoesNotRequireTheResizeBoundary)
{
	Prepare(8, 4);
	ASSERT_TRUE(Build(UnexpectedResize));
	EXPECT_GT(m_Plan.m_Body.m_MaxWidth, 0);
	EXPECT_GT(m_Plan.m_Body.m_MaxHeight, 0);
	EXPECT_GT(m_Plan.m_Feet.m_MaxWidth, 0);
	EXPECT_GT(m_Plan.m_Feet.m_MaxHeight, 0);
}

TEST_F(CSkinResizePlan, NativeResizeOfATinyImageProducesUsableMetrics)
{
	Prepare(1, 1);
	ASSERT_TRUE(Build());
	EXPECT_EQ(m_Image.m_Width, 8u);
	EXPECT_EQ(m_Image.m_Height, 4u);
	EXPECT_GT(m_Plan.m_Body.m_MaxWidth, 0);
	EXPECT_GT(m_Plan.m_Body.m_MaxHeight, 0);
	EXPECT_GT(m_Plan.m_Feet.m_MaxWidth, 0);
	EXPECT_GT(m_Plan.m_Feet.m_MaxHeight, 0);
}

TEST_F(CSkinResizePlan, NativeResizeCanRetryAfterTheBoundaryKeptTheOriginalImage)
{
	Prepare(1, 4);
	EXPECT_FALSE(Build(KeepOriginalImage));
	ASSERT_TRUE(Build());
	EXPECT_EQ(m_Image.m_Width, 8u);
	EXPECT_EQ(m_Image.m_Height, 4u);
	EXPECT_GT(m_Plan.m_Body.m_MaxWidth, 0);
	EXPECT_GT(m_Plan.m_Feet.m_MaxHeight, 0);
}
