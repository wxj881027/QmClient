#include <game/client/QmUi/QmImageView.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmImageView, FitCentersLargeImageWithoutUpscaling)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1920, 1080);
	const auto Rect = View.ImageRect();
	EXPECT_FLOAT_EQ(Rect.m_Width, 800.0f);
	EXPECT_FLOAT_EQ(Rect.m_Height, 450.0f);
	EXPECT_FLOAT_EQ(Rect.m_X, 0.0f);
	EXPECT_FLOAT_EQ(Rect.m_Y, 75.0f);
}

TEST(QmImageView, SettingViewportBeforeImageStillFits)
{
	// 卡片矩形依赖屏幕尺寸：先设视口、后设图片也必须得到同样的适应结果。
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 1200);
	EXPECT_FLOAT_EQ(View.Scale(), 0.5f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Width, 800.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Height, 600.0f);
}

TEST(QmImageView, ActualSizeSwitchesBetweenPixelScaleAndFit)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 1200);
	View.ActualSize();
	EXPECT_FLOAT_EQ(View.Scale(), 1.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Width, 1600.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_X, -400.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Y, -300.0f);
	View.ToggleFit();
	EXPECT_TRUE(View.IsFit());
	EXPECT_FLOAT_EQ(View.Scale(), 0.5f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_X, 0.0f);
}

TEST(QmImageView, FitFollowsViewportResize)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 1200);
	View.SetViewportSize(400, 600);
	EXPECT_FLOAT_EQ(View.Scale(), 0.25f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Height, 300.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Y, 150.0f);
}

TEST(QmImageView, ManualScaleSurvivesViewportResize)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 1200);
	View.ActualSize();
	View.Pan(100.0f, 100.0f);
	View.SetViewportSize(2000, 1600);
	EXPECT_FLOAT_EQ(View.Scale(), 1.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_X, 200.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Y, 200.0f);
}

TEST(QmImageView, ZoomAtAnchorKeepsImagePointUnderCursor)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 1200);
	// 缩放前鼠标位于图片的 (400, 300) 像素，该像素缩放后仍在鼠标下。
	View.ZoomAt(2.0f, 200.0f, 150.0f);
	const auto Rect = View.ImageRect();
	EXPECT_FLOAT_EQ(Rect.m_X + 400.0f * View.Scale(), 200.0f);
	EXPECT_FLOAT_EQ(Rect.m_Y + 300.0f * View.Scale(), 150.0f);
}

TEST(QmImageView, ZoomIsClampedToDefaultRange)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 1200);
	View.ZoomAt(1e30f, 400.0f, 300.0f);
	EXPECT_FLOAT_EQ(View.Scale(), 32.0f);
	View.ZoomAt(1e-30f, 400.0f, 300.0f);
	EXPECT_FLOAT_EQ(View.Scale(), 0.01f);
}

TEST(QmImageView, CustomScaleLimitsFollowFitScale)
{
	// 照片卡以贴合卡片为 1x，滚轮范围 1-8 倍即 [fit, fit * 8]。
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(4000, 3000);
	View.SetFitAllowsUpscale(true);
	EXPECT_FLOAT_EQ(View.FitScale(), 0.2f);
	View.SetScaleLimits(View.FitScale(), View.FitScale() * 8.0f);
	View.ZoomAt(10.0f, 400.0f, 300.0f);
	EXPECT_FLOAT_EQ(View.Scale(), 1.6f);
	View.ZoomAt(1e-6f, 400.0f, 300.0f);
	EXPECT_FLOAT_EQ(View.Scale(), 0.2f);
	EXPECT_FLOAT_EQ(View.RelativeZoom(), 1.0f);
}

TEST(QmImageView, FillFitUpscalesSmallImageToViewport)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(200, 100);
	View.SetFitAllowsUpscale(true);
	EXPECT_FLOAT_EQ(View.Scale(), 4.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Width, 800.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Height, 400.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Y, 100.0f);
}

TEST(QmImageView, PanClampsToImageEdges)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 1200);
	View.ActualSize();
	View.Pan(10000.0f, 10000.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_X, 0.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Y, 0.0f);
	View.Pan(-10000.0f, -10000.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_X + View.ImageRect().m_Width, 800.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Y + View.ImageRect().m_Height, 600.0f);
}

TEST(QmImageView, FittedImageStaysCenteredWhenPanned)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 800);
	View.Pan(500.0f, -500.0f);
	EXPECT_FLOAT_EQ(View.Scale(), 0.5f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_X, 0.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Y, 100.0f);
}

TEST(QmImageView, RelativeZoomTracksManualScaleFromFit)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 1200);
	EXPECT_FLOAT_EQ(View.RelativeZoom(), 1.0f);
	View.SetScale(1.5f);
	EXPECT_FALSE(View.IsFit());
	EXPECT_FLOAT_EQ(View.RelativeZoom(), 3.0f);
}

TEST(QmImageView, PixelSamplingStartsAtSixtyPercentScale)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 1200);
	EXPECT_EQ(View.Sampling(), CQmImageView::ESampling::SMOOTH);
	View.SetScale(0.6f);
	EXPECT_EQ(View.Sampling(), CQmImageView::ESampling::PIXEL);
	View.SetScale(0.59f);
	EXPECT_EQ(View.Sampling(), CQmImageView::ESampling::SMOOTH);
}

TEST(QmImageView, PixelModeForcesNearestSampling)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 1200);
	View.SetPixelMode(true);
	EXPECT_EQ(View.Sampling(), CQmImageView::ESampling::PIXEL);
	View.SetPixelMode(false);
	EXPECT_EQ(View.Sampling(), CQmImageView::ESampling::SMOOTH);
}

TEST(QmImageView, ReplacingImageResetsZoomAndPan)
{
	CQmImageView View;
	View.SetViewportSize(800, 600);
	View.SetImageSize(1600, 1200);
	View.ActualSize();
	View.Pan(200.0f, 100.0f);
	View.SetImageSize(400, 200);
	EXPECT_TRUE(View.IsFit());
	EXPECT_FLOAT_EQ(View.Scale(), 1.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_X, 200.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Y, 200.0f);
}

TEST(QmImageView, DegenerateGeometryProducesEmptyRect)
{
	CQmImageView View;
	View.SetViewportSize(0, -1);
	View.SetImageSize(1600, 1200);
	View.ZoomAt(2.0f, 0.0f, 0.0f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_Width, 0.0f);
	View.SetViewportSize(800, 600);
	View.ZoomAt(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
	View.ZoomAt(-1.0f, 0.0f, 0.0f);
	View.ZoomAt(0.0f, 0.0f, 0.0f);
	View.Pan(std::numeric_limits<float>::infinity(), 0.0f);
	EXPECT_FLOAT_EQ(View.Scale(), 0.5f);
	EXPECT_FLOAT_EQ(View.ImageRect().m_X, 0.0f);
}
