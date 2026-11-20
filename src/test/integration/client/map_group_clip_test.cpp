#include <game/map/render_layer.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <array>
#include <limits>
#include <vector>

namespace
{
	class CGroupGraphics : public CIconBenchmarkGraphics
	{
	public:
		bool m_Clipping = true;
		int m_Maps = 0, m_Enables = 0, m_Disables = 0;
		std::array<float, 4> m_Screen{-1, -2, 3, 4};
		std::array<int, 4> m_Clip{};
		std::vector<char> m_vEvents;
		CGroupGraphics()
		{
			m_ScreenWidth = 2000;
			m_ScreenHeight = 1000;
		}
		void MapScreen(float X0, float Y0, float X1, float Y1) override
		{
			++m_Maps;
			m_vEvents.push_back('M');
			m_Screen = {X0, Y0, X1, Y1};
		}
		void ClipEnable(int X, int Y, int Width, int Height) override
		{
			++m_Enables;
			m_vEvents.push_back('E');
			m_Clipping = true;
			m_Clip = {X, Y, Width, Height};
		}
		void ClipDisable() override
		{
			++m_Disables;
			m_vEvents.push_back('D');
			m_Clipping = false;
		}
	};

	CMapItemGroup ClippedGroup()
	{
		CMapItemGroup Group{};
		Group.m_Version = 2;
		Group.m_UseClipping = 1;
		Group.m_ClipX = -150;
		Group.m_ClipY = -75;
		Group.m_ClipW = 300;
		Group.m_ClipH = 150;
		return Group;
	}

	CRenderLayerParams Params()
	{
		CRenderLayerParams Result{};
		// FULL_DESIGN 的既有分支始终应用裁剪，无需修改全局 noclip 设置。
		Result.m_RenderType = ERenderType::RENDERTYPE_FULL_DESIGN;
		Result.m_Center = vec2(0, 0);
		Result.m_Zoom = 1;
		return Result;
	}
}

TEST(MapGroupClipIntegration, RealGroupPreparesScreenBeforeEnablingTheExpectedPixelMask)
{
	CGroupGraphics Graphics;
	auto Item = ClippedGroup();
	CRenderLayerGroup Group(0, &Item);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	ASSERT_TRUE(Group.DoRender(Params()));
	EXPECT_EQ(Graphics.m_Clip, (std::array<int, 4>{800, 400, 400, 200}));
	EXPECT_EQ(Graphics.m_vEvents, (std::vector<char>{'D', 'M', 'E'}));
	EXPECT_TRUE(Graphics.m_Clipping);
}

TEST(MapGroupClipIntegration, OverflowedWorldViewPreservesPriorScreenAndCanRecoverNextFrame)
{
	CGroupGraphics Graphics;
	const auto OriginalScreen = Graphics.m_Screen;
	auto Item = ClippedGroup();
	CRenderLayerGroup Group(0, &Item);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	auto Input = Params();
	Input.m_Zoom = std::numeric_limits<float>::max();
	EXPECT_FALSE(Group.DoRender(Input));
	EXPECT_FALSE(Graphics.m_Clipping);
	EXPECT_EQ(Graphics.m_Screen, OriginalScreen);
	EXPECT_EQ(Graphics.m_Maps, 0);
	EXPECT_EQ(Graphics.m_Enables, 0);
	ASSERT_TRUE(Group.DoRender(Params()));
	EXPECT_TRUE(Graphics.m_Clipping);
	EXPECT_EQ(Graphics.m_Enables, 1);
}

TEST(MapGroupClipIntegration, FiniteOutOfIntPixelMaskIsNotPublished)
{
	CGroupGraphics Graphics;
	auto Item = ClippedGroup();
	Item.m_ClipX = -2000000000;
	Item.m_ClipW = std::numeric_limits<int>::max();
	CRenderLayerGroup Group(0, &Item);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	EXPECT_FALSE(Group.DoRender(Params()));
	EXPECT_FALSE(Graphics.m_Clipping);
	EXPECT_EQ(Graphics.m_Maps, 0);
	EXPECT_EQ(Graphics.m_Enables, 0);
}

TEST(MapGroupClipIntegration, OffscreenGroupClearsOldClipWithoutPublishingScreen)
{
	CGroupGraphics Graphics;
	auto Item = ClippedGroup();
	Item.m_ClipX = 1000000;
	CRenderLayerGroup Group(0, &Item);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	EXPECT_FALSE(Group.DoRender(Params()));
	EXPECT_EQ(Graphics.m_vEvents, (std::vector<char>{'D'}));
	EXPECT_FALSE(Graphics.m_Clipping);
}

TEST(MapGroupClipIntegration, NextUnclippedGroupClearsPreviousGroupsMask)
{
	CGroupGraphics Graphics;
	auto Item = ClippedGroup();
	CRenderLayerGroup Group(0, &Item);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	ASSERT_TRUE(Group.DoRender(Params()));
	ASSERT_TRUE(Graphics.m_Clipping);
	Item.m_UseClipping = 0;
	EXPECT_TRUE(Group.DoRender(Params()));
	EXPECT_FALSE(Graphics.m_Clipping);
	EXPECT_EQ(Graphics.m_Enables, 1);
	EXPECT_EQ(Graphics.m_Disables, 2);
}

TEST(MapGroupClipIntegration, OldGroupVersionStillDoesNotEnableClipping)
{
	CGroupGraphics Graphics;
	auto Item = ClippedGroup();
	Item.m_Version = 1;
	CRenderLayerGroup Group(0, &Item);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	EXPECT_TRUE(Group.DoRender(Params()));
	EXPECT_FALSE(Graphics.m_Clipping);
	EXPECT_EQ(Graphics.m_Maps, 0);
	EXPECT_EQ(Graphics.m_Enables, 0);
}

TEST(MapGroupClipIntegration, ZeroWidthMaskRetainsExistingGroupHidingBehavior)
{
	CGroupGraphics Graphics;
	auto Item = ClippedGroup();
	Item.m_ClipW = 0;
	CRenderLayerGroup Group(0, &Item);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	ASSERT_TRUE(Group.DoRender(Params()));
	EXPECT_EQ(Graphics.m_Clip[2], 0);
	EXPECT_TRUE(Graphics.m_Clipping);
}

TEST(MapGroupClipIntegration, RepeatedGroupPreparationDoesNotAccumulatePixelMaskOffsets)
{
	CGroupGraphics Graphics;
	auto Item = ClippedGroup();
	CRenderLayerGroup Group(0, &Item);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	ASSERT_TRUE(Group.DoRender(Params()));
	const auto FirstMask = Graphics.m_Clip;
	ASSERT_TRUE(Group.DoRender(Params()));
	EXPECT_EQ(Graphics.m_Clip, FirstMask);
	EXPECT_EQ(Graphics.m_Enables, 2);
	EXPECT_EQ(Graphics.m_Disables, 2);
}

TEST(MapGroupClipIntegration, WideWorldEndpointCanStillProduceAValidRealGroupMask)
{
	CGroupGraphics Graphics;
	auto Item = ClippedGroup();
	Item.m_ClipX = Item.m_ClipW = std::numeric_limits<int>::max();
	CRenderLayerGroup Group(0, &Item);
	Group.CRenderComponent::OnInit(&Graphics, nullptr, nullptr);
	auto Input = Params();
	Input.m_Zoom = 4000000.0f;
	ASSERT_TRUE(Group.DoRender(Input));
	EXPECT_GE(Graphics.m_Clip[0], 0);
	EXPECT_LT(Graphics.m_Clip[0], 2000);
	EXPECT_GT(Graphics.m_Clip[2], 0);
	EXPECT_EQ(Graphics.m_Enables, 1);
}
