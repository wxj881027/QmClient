#include <game/map/map_renderer.h>
#include <game/map/render_layer.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <array>
#include <limits>

namespace
{
	class CWorldGraphics : public CIconBenchmarkGraphics
	{
	public:
		std::array<float, 4> m_Screen{-10, -20, 10, 20};
		int m_Maps = 0, m_Enables = 0, m_Disables = 0;
		bool m_Clipping = true;
		CWorldGraphics()
		{
			m_ScreenWidth = 2000;
			m_ScreenHeight = 1000;
		}
		void MapScreen(float X0, float Y0, float X1, float Y1) override
		{
			++m_Maps;
			m_Screen = {X0, Y0, X1, Y1};
		}
		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1) const override
		{
			*pX0 = m_Screen[0];
			*pY0 = m_Screen[1];
			*pX1 = m_Screen[2];
			*pY1 = m_Screen[3];
		}
		void ClipDisable() override
		{
			++m_Disables;
			m_Clipping = false;
		}
		void ClipEnable(int X, int Y, int Width, int Height) override
		{
			++m_Enables;
			m_Clipping = true;
		}
	};

	CMapItemGroup Item()
	{
		CMapItemGroup Result{};
		Result.m_Version = 2;
		Result.m_ParallaxX = Result.m_ParallaxY = 100;
		return Result;
	}

	CRenderLayerParams Input()
	{
		CRenderLayerParams Result{};
		// 不修改全局 noclip，使用既有强制处理裁剪状态的渲染类型。
		Result.m_RenderType = ERenderType::RENDERTYPE_FULL_DESIGN;
		Result.m_Zoom = 1;
		Result.m_Center = vec2(0, 0);
		return Result;
	}
}

TEST(MapWorldScreenIntegration, UnclippedGroupKeepsNormalProjection)
{
	CWorldGraphics Graphics;
	auto Data = Item();
	CRenderLayerGroup Group(0, &Data);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	ASSERT_TRUE(Group.DoRender(Input()));
	Group.Render(Input());
	EXPECT_EQ(Graphics.m_Screen, (std::array<float, 4>{-750, -375, 750, 375}));
	EXPECT_EQ(Graphics.m_Maps, 1);
}

TEST(MapWorldScreenIntegration, UnclippedGroupRejectsOverflowingProjectionBeforePublishing)
{
	CWorldGraphics Graphics;
	const auto Original = Graphics.m_Screen;
	auto Data = Item();
	CRenderLayerGroup Group(0, &Data);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	auto Params = Input();
	Params.m_Zoom = std::numeric_limits<float>::max();
	EXPECT_FALSE(Group.DoRender(Params));
	EXPECT_EQ(Graphics.m_Maps, 0);
	EXPECT_EQ(Graphics.m_Screen, Original);
	EXPECT_FALSE(Graphics.m_Clipping);
}

TEST(MapWorldScreenIntegration, FiniteHugeCenterCannotPublishCollapsedProjection)
{
	CWorldGraphics Graphics;
	auto Data = Item();
	CRenderLayerGroup Group(0, &Data);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	auto Params = Input();
	Params.m_Center = vec2(1.0e30f, 0);
	EXPECT_FALSE(Group.DoRender(Params));
	EXPECT_EQ(Graphics.m_Maps, 0);
}

TEST(MapWorldScreenIntegration, FiniteParallaxInputsCanStillOverflowTheActualGroupCenter)
{
	CWorldGraphics Graphics;
	auto Data = Item();
	Data.m_ParallaxX = std::numeric_limits<int>::max();
	CRenderLayerGroup Group(0, &Data);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	auto Params = Input();
	Params.m_Center.x = 1.0e32f;
	Params.m_Zoom = 1.0e30f;
	EXPECT_FALSE(Group.DoRender(Params));
	EXPECT_EQ(Graphics.m_Maps, 0);
}

TEST(MapWorldScreenIntegration, RejectedGroupDoesNotPreventNextValidGroup)
{
	CWorldGraphics Graphics;
	auto Data = Item();
	CRenderLayerGroup Group(0, &Data);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	auto Bad = Input();
	Bad.m_Zoom = std::numeric_limits<float>::max();
	ASSERT_FALSE(Group.DoRender(Bad));
	CRenderLayerGroup Next(1, &Data);
	Next.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	ASSERT_TRUE(Next.DoRender(Input()));
	Next.Render(Input());
	EXPECT_EQ(Graphics.m_Screen, (std::array<float, 4>{-750, -375, 750, 375}));
	EXPECT_EQ(Graphics.m_Maps, 1);
}

TEST(MapWorldScreenIntegration, DirectGroupRenderAlsoRejectsBadProjection)
{
	CWorldGraphics Graphics;
	const auto Original = Graphics.m_Screen;
	auto Data = Item();
	CRenderLayerGroup Group(0, &Data);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	auto Params = Input();
	Params.m_Zoom = std::numeric_limits<float>::max();
	Group.Render(Params);
	EXPECT_EQ(Graphics.m_Maps, 0);
	EXPECT_EQ(Graphics.m_Screen, Original);
	EXPECT_FALSE(Graphics.m_Clipping);
}

TEST(MapWorldScreenIntegration, InvalidActualProjectionCannotEnableAnOtherwiseValidInterfaceClip)
{
	CWorldGraphics Graphics;
	auto Data = Item();
	Data.m_ParallaxX = std::numeric_limits<int>::max();
	Data.m_UseClipping = 1;
	Data.m_ClipW = Data.m_ClipH = 100;
	CRenderLayerGroup Group(0, &Data);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	auto Params = Input();
	Params.m_Center.x = 1.0e32f;
	Params.m_Zoom = 1.0e30f;
	EXPECT_FALSE(Group.DoRender(Params));
	EXPECT_EQ(Graphics.m_Maps, 0);
	EXPECT_EQ(Graphics.m_Enables, 0);
}

TEST(MapWorldScreenIntegration, ZeroParallaxKeepsExistingFiniteLargeZoomProjectionAndOffsets)
{
	CWorldGraphics Graphics;
	auto Data = Item();
	Data.m_ParallaxX = Data.m_ParallaxY = 0;
	Data.m_OffsetX = 10;
	Data.m_OffsetY = 20;
	CRenderLayerGroup Group(0, &Data);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	auto Params = Input();
	Params.m_Zoom = 1000000.0f;
	ASSERT_TRUE(Group.DoRender(Params));
	Group.Render(Params);
	EXPECT_FLOAT_EQ(Graphics.m_Screen[0], -740);
	EXPECT_FLOAT_EQ(Graphics.m_Screen[1], -355);
	EXPECT_FLOAT_EQ(Graphics.m_Screen[2], 760);
	EXPECT_FLOAT_EQ(Graphics.m_Screen[3], 395);
}

TEST(MapWorldScreenIntegration, EmptyForegroundRendererRestoresIncomingScreen)
{
	CWorldGraphics Graphics;
	const auto Original = Graphics.m_Screen;
	CMapRenderer Renderer;
	Renderer.OnInit(&Graphics, nullptr, nullptr);
	Renderer.Render(Input());
	EXPECT_EQ(Graphics.m_Screen, Original);
	EXPECT_EQ(Graphics.m_Disables, 1);
}

TEST(MapWorldScreenIntegration, EmptyBackgroundRendererKeepsNormalInterfaceProjection)
{
	CWorldGraphics Graphics;
	CMapRenderer Renderer;
	Renderer.OnInit(&Graphics, nullptr, nullptr);
	auto Params = Input();
	Params.m_RenderType = ERenderType::RENDERTYPE_BACKGROUND;
	Renderer.Render(Params);
	EXPECT_EQ(Graphics.m_Screen, (std::array<float, 4>{-750, -375, 750, 375}));
	EXPECT_FALSE(Graphics.m_Clipping);
}

TEST(MapWorldScreenIntegration, InvalidBackgroundInterfaceFallsBackToIncomingValidScreen)
{
	CWorldGraphics Graphics;
	const auto Original = Graphics.m_Screen;
	CMapRenderer Renderer;
	Renderer.OnInit(&Graphics, nullptr, nullptr);
	auto Params = Input();
	Params.m_RenderType = ERenderType::RENDERTYPE_BACKGROUND_FORCE;
	Params.m_Zoom = std::numeric_limits<float>::max();
	Renderer.Render(Params);
	EXPECT_EQ(Graphics.m_Screen, Original);
	EXPECT_EQ(Graphics.m_Maps, 1);
	EXPECT_FALSE(Graphics.m_Clipping);
}

TEST(MapWorldScreenIntegration, BackgroundFallbackDoesNotBlockNextNormalInterface)
{
	CWorldGraphics Graphics;
	CMapRenderer Renderer;
	Renderer.OnInit(&Graphics, nullptr, nullptr);
	auto Params = Input();
	Params.m_RenderType = ERenderType::RENDERTYPE_BACKGROUND;
	Params.m_Zoom = std::numeric_limits<float>::max();
	Renderer.Render(Params);
	Params.m_Zoom = 1;
	Renderer.Render(Params);
	EXPECT_EQ(Graphics.m_Screen, (std::array<float, 4>{-750, -375, 750, 375}));
	EXPECT_EQ(Graphics.m_Maps, 2);
	EXPECT_FALSE(Graphics.m_Clipping);
}
