#include <engine/gfx/viewport.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <array>
#include <limits>

namespace
{
	class CViewportGraphics : public CIconBenchmarkGraphics
	{
	public:
		int m_Maps = 0;
		std::array<float, 4> m_Screen{-10, -20, 10, 20};
		explicit CViewportGraphics(float GameAspect = 0)
		{
			m_ScreenWidth = 2000;
			m_ScreenHeight = 1000;
			m_GameScreenAspectOverride = GameAspect;
		}
		void MapScreen(float X0, float Y0, float X1, float Y1) override
		{
			++m_Maps;
			m_Screen = {X0, Y0, X1, Y1};
		}
	};
}

TEST(GraphicsViewport, OrdinaryWorldMappingKeepsExistingFloatValues)
{
	CViewportGraphics Graphics;
	std::array<float, 4> View;
	Graphics.MapScreenToWorld(100, 50, 100, 100, 100, 10, -20, 2, 1, View.data());
	EXPECT_EQ(View, (std::array<float, 4>{-640, -345, 860, 405}));
}

TEST(GraphicsViewport, RealWorldMappingRecoversZeroParallaxAtLargestZoom)
{
	CViewportGraphics Graphics;
	std::array<float, 4> View;
	Graphics.MapScreenToWorld(120, -80, 50, 25, 0, 10, 20, 2, std::numeric_limits<float>::max(), View.data());
	EXPECT_EQ(View, (std::array<float, 4>{-680, -375, 820, 375}));
}

TEST(GraphicsViewport, RealWorldMappingRecoversSmallParallaxAfterIntermediateOverflow)
{
	CViewportGraphics Graphics;
	std::array<float, 4> View;
	Graphics.MapScreenToWorld(0, 0, 100, 100, 1, 0, 0, 2, 1.0e36f, View.data());
	ASSERT_TRUE(GraphicsViewport::IsValidScreen(View));
	EXPECT_FLOAT_EQ(View[2] - View[0], 1.5e37f);
}

TEST(GraphicsViewport, RealWorldMappingRecoversTinyPositiveZoomAfterFloatCancellation)
{
	CViewportGraphics Graphics;
	std::array<float, 4> View;
	Graphics.MapScreenToWorld(0, 0, 100, 100, 100, 0, 0, 2, 1.0e-9f, View.data());
	ASSERT_TRUE(GraphicsViewport::IsValidScreen(View));
	EXPECT_FLOAT_EQ(View[0], -7.5e-7f);
	EXPECT_FLOAT_EQ(View[2], 7.5e-7f);
}

TEST(GraphicsViewport, UnrepresentableWorldOutputRemainsDetectablyInvalid)
{
	CViewportGraphics Graphics;
	std::array<float, 4> View;
	Graphics.MapScreenToWorld(0, 0, 100, 100, 100, 0, 0, 2, std::numeric_limits<float>::max(), View.data());
	EXPECT_FALSE(GraphicsViewport::IsValidScreen(View));
}

TEST(GraphicsViewport, PhysicalInterfaceRejectsBadProjectionAndPreservesOldScreen)
{
	CViewportGraphics Graphics;
	const auto Original = Graphics.m_Screen;
	Graphics.MapScreenToInterface(0, 0, std::numeric_limits<float>::max());
	EXPECT_EQ(Graphics.m_Maps, 0);
	EXPECT_EQ(Graphics.m_Screen, Original);
}

TEST(GraphicsViewport, GameInterfaceRejectsNonFiniteCenterAndPreservesOldScreen)
{
	CViewportGraphics Graphics;
	const auto Original = Graphics.m_Screen;
	Graphics.MapScreenToGameInterface(std::numeric_limits<float>::quiet_NaN(), 0, 1);
	EXPECT_EQ(Graphics.m_Maps, 0);
	EXPECT_EQ(Graphics.m_Screen, Original);
}

TEST(GraphicsViewport, FailedInterfaceMappingDoesNotBlockTheNextNormalCall)
{
	CViewportGraphics Graphics;
	Graphics.MapScreenToGameInterface(0, 0, std::numeric_limits<float>::max());
	Graphics.MapScreenToGameInterface(0, 0, 1);
	EXPECT_EQ(Graphics.m_Maps, 1);
	EXPECT_EQ(Graphics.m_Screen, (std::array<float, 4>{-750, -375, 750, 375}));
}

TEST(GraphicsViewport, NormalInterfaceWrappersKeepTheSamePhysicalAspectWithoutOverride)
{
	CViewportGraphics Graphics;
	Graphics.MapScreenToInterface(0, 0, 1);
	const auto Physical = Graphics.m_Screen;
	Graphics.MapScreenToGameInterface(0, 0, 1);
	EXPECT_EQ(Graphics.m_Screen, Physical);
	EXPECT_EQ(Graphics.m_Maps, 2);
}

TEST(GraphicsViewport, GameAspectOverrideDoesNotChangePhysicalInterfaceMapping)
{
	CViewportGraphics Graphics(1.0f);
	Graphics.MapScreenToGameInterface(0, 0, 1);
	EXPECT_EQ(Graphics.m_Screen, (std::array<float, 4>{-525, -525, 525, 525}));
	Graphics.MapScreenToInterface(0, 0, 1);
	EXPECT_EQ(Graphics.m_Screen, (std::array<float, 4>{-750, -375, 750, 375}));
	EXPECT_EQ(Graphics.GameScreenAspect(), 1.0f);
	EXPECT_EQ(Graphics.ScreenAspect(), 2.0f);
}
