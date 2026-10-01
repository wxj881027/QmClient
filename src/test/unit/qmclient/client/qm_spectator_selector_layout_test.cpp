#include <game/client/components/qmclient/spectator_selector_layout.h>

#include <gtest/gtest.h>

namespace
{
	void ExpectContains(const CUIRect &Outer, const CUIRect &Inner)
	{
		EXPECT_GE(Inner.x, Outer.x);
		EXPECT_GE(Inner.y, Outer.y);
		EXPECT_LE(Inner.x + Inner.w, Outer.x + Outer.w);
		EXPECT_LE(Inner.y + Inner.h, Outer.y + Outer.h);
	}
}

TEST(QmSpectatorSelectorLayout, BackgroundContainsSearchRowAndEveryStatus)
{
	for(float HalfWidth : {300.0f, 600.0f})
		for(float Offset : {-10.0f, 0.0f})
		{
			SCOPED_TRACE(HalfWidth);
			SCOPED_TRACE(Offset);
			const auto Layout = qm_spectator_layout::Build(vec2(800.0f, 600.0f + Offset), HalfWidth, true);
			ExpectContains(Layout.m_Panel, Layout.m_SearchRow);
			ExpectContains(Layout.m_Panel, Layout.m_Status);
			ExpectContains(Layout.m_Mouse, Layout.m_SearchRow);
			ExpectContains(Layout.m_Mouse, Layout.m_Status);
			EXPECT_LT(Layout.m_SearchRow.y + Layout.m_SearchRow.h, Layout.m_Status.y);
			EXPECT_GT(Layout.m_Panel.y + Layout.m_Panel.h, Layout.m_Status.y + Layout.m_Status.h);
		}
}

TEST(QmSpectatorSelectorLayout, MouseCanReachSearchControlsWithoutLeavingPanel)
{
	const vec2 Center(800.0f, 600.0f);
	const auto Layout = qm_spectator_layout::Build(Center, 300.0f, true);
	const vec2 ButtonCenter(Layout.m_SearchRow.x + Layout.m_SearchRow.w - 10.0f, Layout.m_SearchRow.y + Layout.m_SearchRow.h * 0.5f);
	const vec2 Clamped = qm_spectator_layout::ClampMouse(Layout, Center, ButtonCenter - Center) + Center;
	EXPECT_FLOAT_EQ(Clamped.x, ButtonCenter.x);
	EXPECT_FLOAT_EQ(Clamped.y, ButtonCenter.y);
	EXPECT_TRUE(Layout.m_SearchRow.Inside(Clamped));
	EXPECT_TRUE(Layout.m_Panel.Inside(Clamped));
}

TEST(QmSpectatorSelectorLayout, OutOfBoundsMouseStopsInsidePaddedPanel)
{
	const vec2 Center(800.0f, 600.0f);
	const auto Layout = qm_spectator_layout::Build(Center, 300.0f, true);
	for(vec2 Mouse : {vec2(-10000.0f, -10000.0f), vec2(10000.0f, 10000.0f)})
	{
		const vec2 Clamped = qm_spectator_layout::ClampMouse(Layout, Center, Mouse) + Center;
		EXPECT_GE(Clamped.x, Layout.m_Mouse.x);
		EXPECT_LE(Clamped.x, Layout.m_Mouse.x + Layout.m_Mouse.w);
		EXPECT_GE(Clamped.y, Layout.m_Mouse.y);
		EXPECT_LE(Clamped.y, Layout.m_Mouse.y + Layout.m_Mouse.h);
		EXPECT_TRUE(Layout.m_Panel.Inside(Clamped));
	}
}

TEST(QmSpectatorSelectorLayout, ReplayWithoutSearchKeepsOriginalPlayerArea)
{
	const vec2 Center(800.0f, 600.0f);
	const auto Layout = qm_spectator_layout::Build(Center, 600.0f, false);
	EXPECT_FLOAT_EQ(Layout.m_Panel.h, 600.0f);
	EXPECT_FLOAT_EQ(Layout.m_SearchRow.w, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_Status.w, 0.0f);
	const vec2 Clamped = qm_spectator_layout::ClampMouse(Layout, Center, vec2(0.0f, 1000.0f));
	EXPECT_FLOAT_EQ(Clamped.y, 280.0f);
}
