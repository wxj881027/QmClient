#include <game/client/components/qmclient/watermark_compositor.h>

#include <gtest/gtest.h>

#include <cstdlib>

namespace
{
	class CQmWatermarkCompositor : public ::testing::Test
	{
	protected:
		CImageInfo m_Image, m_Stamp;
		SQmWatermarkLayer m_Layer;
		void SetUp() override
		{
			m_Image.m_Width = m_Image.m_Height = 100;
			m_Stamp.m_Width = 20;
			m_Stamp.m_Height = 10;
			for(auto *pImage : {&m_Image, &m_Stamp})
			{
				pImage->m_Format = CImageInfo::FORMAT_RGBA;
				pImage->m_pData = static_cast<uint8_t *>(std::calloc(pImage->DataSize(), 1));
				ASSERT_NE(pImage->m_pData, nullptr);
			}
			for(size_t Y = 0; Y < m_Stamp.m_Height; ++Y)
				for(size_t X = 0; X < m_Stamp.m_Width; ++X)
					m_Stamp.SetPixelColor(X, Y, ColorRGBA(1, 0, 0, 1));
			m_Layer.m_X = m_Layer.m_Y = 500;
			m_Layer.m_Size = 20;
			m_Layer.m_Opacity = 100;
		}
		void TearDown() override
		{
			m_Image.Free();
			m_Stamp.Free();
		}
		int VisiblePixels() const
		{
			int Count = 0;
			for(size_t Y = 0; Y < m_Image.m_Height; ++Y)
				for(size_t X = 0; X < m_Image.m_Width; ++X)
					Count += m_Image.PixelColor(X, Y).a > 0;
			return Count;
		}
	};
}

TEST_F(CQmWatermarkCompositor, TransparentDestinationKeepsSourceColorAndOpacity)
{
	m_Layer.m_Opacity = 50;
	ASSERT_TRUE(QmCompositeWatermark(m_Image, m_Stamp, m_Layer));
	const auto Center = m_Image.PixelColor(50, 50);
	EXPECT_NEAR(Center.r, 1, 0.01);
	EXPECT_NEAR(Center.a, 0.5, 0.01);
	EXPECT_EQ(m_Image.PixelColor(0, 0).a, 0);
}

TEST_F(CQmWatermarkCompositor, ZeroOpacityLeavesDestinationUntouched)
{
	m_Layer.m_Opacity = 0;
	ASSERT_TRUE(QmCompositeWatermark(m_Image, m_Stamp, m_Layer));
	EXPECT_EQ(VisiblePixels(), 0);
}

TEST_F(CQmWatermarkCompositor, TopLeftAndBottomRightMovePixelsToChosenCorner)
{
	m_Layer.m_X = m_Layer.m_Y = 0;
	ASSERT_TRUE(QmCompositeWatermark(m_Image, m_Stamp, m_Layer));
	EXPECT_GT(m_Image.PixelColor(10, 5).a, 0.9f);
	EXPECT_EQ(m_Image.PixelColor(90, 95).a, 0);
	m_Layer.m_X = m_Layer.m_Y = 1000;
	ASSERT_TRUE(QmCompositeWatermark(m_Image, m_Stamp, m_Layer));
	EXPECT_GT(m_Image.PixelColor(90, 95).a, 0.9f);
}

TEST_F(CQmWatermarkCompositor, QuarterTurnMakesWideLogoVertical)
{
	m_Layer.m_Angle = 90;
	ASSERT_TRUE(QmCompositeWatermark(m_Image, m_Stamp, m_Layer));
	EXPECT_GT(m_Image.PixelColor(50, 42).a, 0.9f);
	EXPECT_EQ(m_Image.PixelColor(42, 50).a, 0);
}

TEST_F(CQmWatermarkCompositor, TilingRepeatsOutsideSingleStampBounds)
{
	ASSERT_TRUE(QmCompositeWatermark(m_Image, m_Stamp, m_Layer));
	const int Single = VisiblePixels();
	m_Layer.m_Tile = true;
	ASSERT_TRUE(QmCompositeWatermark(m_Image, m_Stamp, m_Layer));
	EXPECT_GT(VisiblePixels(), Single * 4);
}

TEST_F(CQmWatermarkCompositor, TwoLayersBlendInDrawingOrder)
{
	ASSERT_TRUE(QmCompositeWatermark(m_Image, m_Stamp, m_Layer));
	for(size_t Y = 0; Y < m_Stamp.m_Height; ++Y)
		for(size_t X = 0; X < m_Stamp.m_Width; ++X)
			m_Stamp.SetPixelColor(X, Y, ColorRGBA(0, 0, 1, 1));
	m_Layer.m_Opacity = 50;
	ASSERT_TRUE(QmCompositeWatermark(m_Image, m_Stamp, m_Layer));
	const auto Center = m_Image.PixelColor(50, 50);
	EXPECT_NEAR(Center.r, 0.5, 0.01);
	EXPECT_NEAR(Center.b, 0.5, 0.01);
	EXPECT_EQ(Center.a, 1);
}

TEST_F(CQmWatermarkCompositor, TallLogoFitsInsideImageAfterRotation)
{
	m_Layer.m_Size = 70;
	m_Layer.m_Angle = 45;
	const auto Bounds = QmWatermarkBounds(100, 50, 1, 1000, m_Layer);
	EXPECT_GE(Bounds.m_X, 0);
	EXPECT_GE(Bounds.m_Y, 0);
	EXPECT_LE(Bounds.m_X + Bounds.m_Width, 100);
	EXPECT_LE(Bounds.m_Y + Bounds.m_Height, 50);
}

TEST_F(CQmWatermarkCompositor, EmptyStampFailsWithoutChangingImage)
{
	CImageInfo Empty;
	EXPECT_FALSE(QmCompositeWatermark(m_Image, Empty, m_Layer));
	EXPECT_EQ(VisiblePixels(), 0);
}
