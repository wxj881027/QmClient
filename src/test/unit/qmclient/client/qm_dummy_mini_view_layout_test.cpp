#include <engine/graphics.h>

#include <game/client/components/qmclient/dummy_mini_view_layout.h>

#include <gtest/gtest.h>

#include <utility>

TEST(QmDummyMiniViewLayout, MovingDownMovesViewportAndClipDown)
{
	const CUIRect Screen{0.0f, 0.0f, 600.0f, 300.0f};
	const auto Top = QmDummyMiniViewLayout::ResolveViewport({100.0f, 20.0f, 120.0f, 80.0f}, Screen, 1800, 900);
	const auto Bottom = QmDummyMiniViewLayout::ResolveViewport({100.0f, 200.0f, 120.0f, 80.0f}, Screen, 1800, 900);
	EXPECT_TRUE(Top.IsVisible());
	EXPECT_EQ(Top.m_X, 300);
	EXPECT_EQ(Top.m_Y, 60);
	EXPECT_EQ(Top.m_W, 360);
	EXPECT_EQ(Top.m_H, 240);
	EXPECT_EQ(Bottom.m_Y, 600);
	// 视口的 Y 只在 OpenGL 后端翻转一次。
	EXPECT_EQ(graphics_viewport::OpenGLViewportY(900, Top.m_Y, Top.m_H), 600);
	EXPECT_EQ(graphics_viewport::OpenGLViewportY(900, Bottom.m_Y, Bottom.m_H), 60);
}

TEST(QmDummyMiniViewLayout, FourCornersKeepSameContentExtent)
{
	const CUIRect Screen{0.0f, 0.0f, 600.0f, 300.0f};
	for(float X : {0.0f, 480.0f})
		for(float Y : {0.0f, 220.0f})
		{
			SCOPED_TRACE(X);
			SCOPED_TRACE(Y);
			const auto Viewport = QmDummyMiniViewLayout::ResolveViewport({X, Y, 120.0f, 80.0f}, Screen, 1800, 900);
			EXPECT_EQ(Viewport.m_X, X == 0.0f ? 0 : 1440);
			EXPECT_EQ(Viewport.m_Y, Y == 0.0f ? 0 : 660);
			EXPECT_EQ(Viewport.m_W, 360);
			EXPECT_EQ(Viewport.m_H, 240);
		}
}

TEST(QmDummyMiniViewLayout, NonzeroScreenOriginAndUnequalPixelScaleAreRespected)
{
	const auto Viewport = QmDummyMiniViewLayout::ResolveViewport({30.0f, 40.0f, 50.0f, 30.0f}, {10.0f, 20.0f, 400.0f, 200.0f}, 1200, 800);
	EXPECT_EQ(Viewport.m_X, 60);
	EXPECT_EQ(Viewport.m_Y, 80);
	EXPECT_EQ(Viewport.m_W, 150);
	EXPECT_EQ(Viewport.m_H, 120);
}

TEST(QmDummyMiniViewLayout, PartialOffscreenContentUsesVisibleIntersection)
{
	const CUIRect Screen{0.0f, 0.0f, 600.0f, 300.0f};
	const auto TopLeft = QmDummyMiniViewLayout::ResolveViewport({-10.0f, -20.0f, 120.0f, 80.0f}, Screen, 1800, 900);
	EXPECT_EQ(TopLeft.m_X, 0);
	EXPECT_EQ(TopLeft.m_Y, 0);
	EXPECT_EQ(TopLeft.m_W, 330);
	EXPECT_EQ(TopLeft.m_H, 180);
	const auto TopLeftClip = TopLeft.ResolveClip(QmDummyMiniViewLayout::EBackend::OPENGL, 1800, 900);
	ASSERT_TRUE(TopLeftClip.has_value());
	EXPECT_EQ(TopLeftClip->m_X, 0);
	EXPECT_EQ(TopLeftClip->m_Y, 720);
	EXPECT_EQ(TopLeftClip->m_W, 330);
	EXPECT_EQ(TopLeftClip->m_H, 180);
	const auto BottomRight = QmDummyMiniViewLayout::ResolveViewport({590.0f, 280.0f, 120.0f, 80.0f}, Screen, 1800, 900);
	EXPECT_EQ(BottomRight.m_X, 1770);
	EXPECT_EQ(BottomRight.m_Y, 840);
	EXPECT_EQ(BottomRight.m_W, 30);
	EXPECT_EQ(BottomRight.m_H, 60);
	const auto BottomRightClip = BottomRight.ResolveClip(QmDummyMiniViewLayout::EBackend::OPENGL, 1800, 900);
	ASSERT_TRUE(BottomRightClip.has_value());
	EXPECT_EQ(BottomRightClip->m_X, 0);
	EXPECT_EQ(BottomRightClip->m_Y, 840);
	EXPECT_EQ(BottomRightClip->m_W, 30);
	EXPECT_EQ(BottomRightClip->m_H, 60);
}

TEST(QmDummyMiniViewLayout, OutsideOrEmptyContentDoesNotCreateViewport)
{
	const CUIRect Screen{0.0f, 0.0f, 600.0f, 300.0f};
	for(const CUIRect Content : {CUIRect{-130.0f, 20.0f, 120.0f, 80.0f}, CUIRect{600.0f, 20.0f, 120.0f, 80.0f}, CUIRect{20.0f, 300.0f, 120.0f, 80.0f}, CUIRect{20.0f, 20.0f, 0.0f, 80.0f}})
		EXPECT_FALSE(QmDummyMiniViewLayout::ResolveViewport(Content, Screen, 1800, 900).IsVisible());
	EXPECT_FALSE(QmDummyMiniViewLayout::ResolveViewport(Screen, {}, 1800, 900).IsVisible());
	EXPECT_FALSE(QmDummyMiniViewLayout::ResolveViewport(Screen, Screen, 0, 900).IsVisible());
}

TEST(QmDummyMiniViewLayout, FractionalEdgesRoundConsistentlyAtHighDpi)
{
	const auto Viewport = QmDummyMiniViewLayout::ResolveViewport({10.2f, 20.3f, 30.5f, 40.4f}, {0.0f, 0.0f, 100.0f, 100.0f}, 250, 250);
	EXPECT_EQ(Viewport.m_X, 26);
	EXPECT_EQ(Viewport.m_Y, 51);
	EXPECT_EQ(Viewport.m_W, 76);
	EXPECT_EQ(Viewport.m_H, 101);
}

TEST(QmDummyMiniViewLayout, OpenGLClipIsRelativeToViewportRegardlessOfScreenPosition)
{
	const CUIRect Screen{0.0f, 0.0f, 600.0f, 300.0f};
	for(float X : {0.0f, 100.0f, 480.0f})
		for(float Y : {0.0f, 20.0f, 220.0f})
		{
			SCOPED_TRACE(X);
			SCOPED_TRACE(Y);
			const auto Viewport = QmDummyMiniViewLayout::ResolveViewport({X, Y, 120.0f, 80.0f}, Screen, 1800, 900);
			const auto Clip = Viewport.ResolveClip(QmDummyMiniViewLayout::EBackend::OPENGL, 1800, 900);
			ASSERT_TRUE(Clip.has_value());
			EXPECT_EQ(Clip->m_X, 0);
			EXPECT_EQ(Clip->m_Y, 660);
			EXPECT_EQ(Clip->m_W, 360);
			EXPECT_EQ(Clip->m_H, 240);
			// 转为左下原点后，裁剪的偏移必须为零；后端负责叠加一次实际视口原点。
			EXPECT_EQ(graphics_viewport::OpenGLViewportY(900, Clip->m_Y, Clip->m_H), 0);
		}
}

TEST(QmDummyMiniViewLayout, DetectedBackendNamesSelectClipSemantics)
{
	using QmDummyMiniViewLayout::EBackend;
	EXPECT_EQ(QmDummyMiniViewLayout::ResolveBackend("OpenGL"), EBackend::OPENGL);
	EXPECT_EQ(QmDummyMiniViewLayout::ResolveBackend("GLES"), EBackend::OPENGL);
	EXPECT_EQ(QmDummyMiniViewLayout::ResolveBackend("Vulkan"), EBackend::VULKAN);
	EXPECT_EQ(QmDummyMiniViewLayout::ResolveBackend(""), EBackend::UNKNOWN);
	EXPECT_EQ(QmDummyMiniViewLayout::ResolveBackend(nullptr), EBackend::UNKNOWN);
	EXPECT_EQ(QmDummyMiniViewLayout::ResolveBackend("Unsupported"), EBackend::UNKNOWN);
}

TEST(QmDummyMiniViewLayout, VulkanViewportLeavesScissorToPresentedExtent)
{
	for(const QmDummyMiniViewLayout::SViewport Viewport : {
		    QmDummyMiniViewLayout::SViewport{0, 0, 1, 1},
		    QmDummyMiniViewLayout::SViewport{1799, 899, 1, 1},
		    QmDummyMiniViewLayout::SViewport{300, 60, 360, 240},
		    QmDummyMiniViewLayout::SViewport{0, 0, 1800, 900}})
	{
		SCOPED_TRACE(Viewport.m_X);
		SCOPED_TRACE(Viewport.m_Y);
		SCOPED_TRACE(Viewport.m_W);
		// 不提交前端尺寸的裁剪框，让 Vulkan 使用真实 presented extent。
		EXPECT_FALSE(Viewport.ResolveClip(QmDummyMiniViewLayout::EBackend::VULKAN, 1800, 900).has_value());
	}
}

TEST(QmDummyMiniViewLayout, VulkanClipDoesNotAssumeCanvasEqualsPresentedExtent)
{
	const QmDummyMiniViewLayout::SViewport Viewport{300, 60, 360, 240};
	for(const auto Canvas : {std::pair{1800, 900}, std::pair{3840, 2160}, std::pair{1000, 1000}})
	{
		SCOPED_TRACE(Canvas.first);
		SCOPED_TRACE(Canvas.second);
		EXPECT_FALSE(Viewport.ResolveClip(QmDummyMiniViewLayout::EBackend::VULKAN, Canvas.first, Canvas.second).has_value());
	}
}

TEST(QmDummyMiniViewLayout, UnknownBackendOrEmptyCanvasDoesNotAssumeOpenGLClip)
{
	using QmDummyMiniViewLayout::EBackend;
	const QmDummyMiniViewLayout::SViewport Viewport{300, 60, 360, 240};
	EXPECT_FALSE(Viewport.ResolveClip(EBackend::UNKNOWN, 1800, 900).has_value());
	EXPECT_FALSE(Viewport.ResolveClip(EBackend::VULKAN, 0, 900).has_value());
	EXPECT_FALSE(Viewport.ResolveClip(EBackend::OPENGL, 1800, 0).has_value());
	EXPECT_FALSE(QmDummyMiniViewLayout::SViewport{}.ResolveClip(EBackend::VULKAN, 1800, 900).has_value());
}
