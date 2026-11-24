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

TEST(QmSpectatorSelectorLayout, TallPlayerListPushesSearchBelowThePlayers)
{
	const vec2 Center(800.0f, 600.0f);
	constexpr float PlayersBottom = 310.0f;
	const auto Layout = qm_spectator_layout::Build(Center, 300.0f, true, PlayersBottom);
	EXPECT_GE(Layout.m_SearchRow.y, Center.y + PlayersBottom + 20.0f);
	ExpectContains(Layout.m_Panel, Layout.m_SearchRow);
	ExpectContains(Layout.m_Mouse, Layout.m_SearchRow);
	ExpectContains(Layout.m_Mouse, Layout.m_Status);
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

TEST(QmSpectatorSelectorLayout, TouchStaysUnderFingerWhilePanelMoves)
{
	const vec2 ScreenSize(1920.0f, 1200.0f);
	const vec2 Position(0.25f, 0.75f);
	for(float Offset : {-10.0f, 0.0f, 30.0f})
	{
		const vec2 Center(960.0f, 600.0f + Offset);
		const vec2 Relative = qm_spectator_layout::TouchPosition(Position, ScreenSize, Center);
		EXPECT_EQ(Relative + Center, Position * ScreenSize);
	}
}

TEST(QmSpectatorSelectorLayout, TeamBandMiddleHasStraightSides)
{
	for(float Radius : {3.0f, 5.0f, 6.0f, 10.0f, 30.0f})
	{
		SCOPED_TRACE(Radius);
		const auto Surface = qm_spectator_layout::TeamRowSurface(Radius, false, false);
		const auto Plan = ResolveRoundedSurfacePlan(CUIRect{0.0f, 0.0f, 270.0f, 60.0f}, Surface, true);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.x, 0.0f);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.y, 0.0f);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.z, 0.0f);
		EXPECT_FLOAT_EQ(Plan.m_CornerRadii.w, 0.0f);
		EXPECT_FLOAT_EQ(Plan.m_Radius, 0.0f);
	}
}

TEST(QmSpectatorSelectorLayout, TeamBandRoundsOnlyExposedEnds)
{
	const CUIRect Row{0.0f, 0.0f, 270.0f, 60.0f};
	const auto First = ResolveRoundedSurfacePlan(Row, qm_spectator_layout::TeamRowSurface(30.0f, true, false), true);
	EXPECT_FLOAT_EQ(First.m_CornerRadii.x, 30.0f);
	EXPECT_FLOAT_EQ(First.m_CornerRadii.y, 30.0f);
	EXPECT_FLOAT_EQ(First.m_CornerRadii.z, 0.0f);
	EXPECT_FLOAT_EQ(First.m_CornerRadii.w, 0.0f);

	const auto Last = ResolveRoundedSurfacePlan(Row, qm_spectator_layout::TeamRowSurface(30.0f, false, true), true);
	EXPECT_FLOAT_EQ(Last.m_CornerRadii.x, 0.0f);
	EXPECT_FLOAT_EQ(Last.m_CornerRadii.y, 0.0f);
	EXPECT_FLOAT_EQ(Last.m_CornerRadii.z, 30.0f);
	EXPECT_FLOAT_EQ(Last.m_CornerRadii.w, 30.0f);
}

TEST(QmSpectatorSelectorLayout, SinglePlayerTeamKeepsAllRoundedCorners)
{
	const auto Surface = qm_spectator_layout::TeamRowSurface(30.0f, true, true);
	const auto Plan = ResolveRoundedSurfacePlan(CUIRect{0.0f, 0.0f, 270.0f, 60.0f}, Surface, true);
	EXPECT_FLOAT_EQ(Plan.m_CornerRadii.x, 30.0f);
	EXPECT_FLOAT_EQ(Plan.m_CornerRadii.y, 30.0f);
	EXPECT_FLOAT_EQ(Plan.m_CornerRadii.z, 30.0f);
	EXPECT_FLOAT_EQ(Plan.m_CornerRadii.w, 30.0f);
}

TEST(QmSpectatorSelectorLayout, GroupTitleSeparatesPlayersLabelAndDivider)
{
	const vec2 Position(100.0f, 200.0f);
	const auto Title = qm_spectator_layout::GroupTitle(Position, 270.0f, 15.0f, 1.0f);
	EXPECT_GT(Title.m_Label.y, Position.y);
	EXPECT_GT(Title.m_Divider.y, Title.m_Label.y + Title.m_Label.h);
	EXPECT_GT(Position.y + Title.m_Height, Title.m_Divider.y + Title.m_Divider.h);
	EXPECT_FLOAT_EQ(Title.m_Label.x, Position.x);
	EXPECT_FLOAT_EQ(Title.m_Divider.x, Position.x);
	EXPECT_FLOAT_EQ(Title.m_Divider.w, 270.0f);
}

TEST(QmSpectatorSelectorLayout, BackgroundAndMouseContainTallPlayersWithoutSearch)
{
	const vec2 Center(800.0f, 600.0f);
	constexpr float PlayersBottom = 370.0f;
	const auto Layout = qm_spectator_layout::Build(Center, 300.0f, false, PlayersBottom);
	const CUIRect LastPlayer{Center.x - 270.0f, Center.y + PlayersBottom - 60.0f, 270.0f, 60.0f};
	ExpectContains(Layout.m_Panel, LastPlayer);
	ExpectContains(Layout.m_Mouse, LastPlayer);
	EXPECT_FLOAT_EQ(Layout.m_SearchRow.h, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_Status.h, 0.0f);
}
