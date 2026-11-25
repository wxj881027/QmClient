#include <game/client/components/qmclient/spectator_friend_priority.h>
#include <game/client/components/qmclient/spectator_selector_layout.h>

#include <gtest/gtest.h>

namespace
{
	void ExpectContains(const CUIRect &Outer, const CUIRect &Inner)
	{
		EXPECT_PRED_FORMAT2(::testing::FloatLE, Outer.x, Inner.x);
		EXPECT_PRED_FORMAT2(::testing::FloatLE, Outer.y, Inner.y);
		EXPECT_PRED_FORMAT2(::testing::FloatLE, Inner.x + Inner.w, Outer.x + Outer.w);
		EXPECT_PRED_FORMAT2(::testing::FloatLE, Inner.y + Inner.h, Outer.y + Outer.h);
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
			EXPECT_FLOAT_EQ(Layout.m_Panel.y + Layout.m_Panel.h * 0.5f, 600.0f + Offset);
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
	const vec2 ScreenSize(1600.0f, 1200.0f);
	const vec2 Touch = qm_spectator_layout::TouchPosition(ButtonCenter / ScreenSize, ScreenSize, Center);
	const vec2 Clamped = qm_spectator_layout::ClampMouse(Layout, Center, Touch) + Center;
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
	EXPECT_FLOAT_EQ(Layout.m_SearchRow.y - (Layout.m_ContentCenter.y + PlayersBottom), 20.0f);
	ExpectContains(Layout.m_Panel, Layout.m_SearchRow);
	ExpectContains(Layout.m_Mouse, Layout.m_SearchRow);
	ExpectContains(Layout.m_Mouse, Layout.m_Status);
}

TEST(QmSpectatorSelectorLayout, OutOfBoundsMouseStopsInsidePaddedPanel)
{
	const vec2 Center(800.0f, 600.0f);
	const auto Layout = qm_spectator_layout::Build(Center, 300.0f, true);
	EXPECT_EQ(qm_spectator_layout::ClampMouse(Layout, Center, vec2(0.0f, 0.0f)), vec2(0.0f, 0.0f));
	for(vec2 Mouse : {vec2(-10000.0f, -10000.0f), vec2(10000.0f, 10000.0f)})
	{
		const vec2 Clamped = qm_spectator_layout::ClampMouse(Layout, Center, Mouse) + Center;
		EXPECT_FLOAT_EQ(Clamped.x, Center.x + (Mouse.x < 0.0f ? -0.5f : 0.5f) * Layout.m_Mouse.w);
		EXPECT_FLOAT_EQ(Clamped.y, Center.y + (Mouse.y < 0.0f ? -0.5f : 0.5f) * Layout.m_Mouse.h);
		EXPECT_TRUE(Layout.m_Panel.Inside(Clamped));
	}
}

TEST(QmSpectatorSelectorLayout, ReplayWithoutSearchKeepsOriginalPlayerArea)
{
	const vec2 Center(800.0f, 600.0f);
	const auto Layout = qm_spectator_layout::Build(Center, 600.0f, false);
	EXPECT_FLOAT_EQ(Layout.m_Panel.h, 600.0f);
	EXPECT_FLOAT_EQ(Layout.m_Panel.y, 300.0f);
	EXPECT_EQ(Layout.m_ContentCenter, Center);
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

TEST(QmSpectatorSelectorLayout, MouseAndTouchHitEachShiftedViewTab)
{
	const vec2 ScreenSize(1600.0f, 1200.0f);
	const vec2 Center(800.0f, 590.0f);
	const auto Layout = qm_spectator_layout::Build(Center, 300.0f, true);
	const auto Unshifted = qm_spectator_layout::Build(Center, 300.0f, false);
	for(int Index = 0; Index < 3; ++Index)
	{
		SCOPED_TRACE(Index);
		const CUIRect Tab = qm_spectator_layout::ViewTab(Layout, Index, 1.0f);
		const vec2 Point(Tab.x + Tab.w * 0.5f, Tab.y + 3.0f);
		const vec2 Mouse = Center + qm_spectator_layout::ClampMouse(Layout, Center, Point - Center);
		const vec2 Touch = qm_spectator_layout::TouchPosition(Point / ScreenSize, ScreenSize, Center);
		const vec2 TouchMouse = Center + qm_spectator_layout::ClampMouse(Layout, Center, Touch);
		EXPECT_FLOAT_EQ(TouchMouse.x, Mouse.x);
		EXPECT_FLOAT_EQ(TouchMouse.y, Mouse.y);
		EXPECT_TRUE(Tab.Inside(Mouse));
		EXPECT_TRUE(Tab.Inside(TouchMouse));
		EXPECT_FALSE(qm_spectator_layout::ViewTab(Unshifted, Index, 1.0f).Inside(Mouse));
	}
}

TEST(QmSpectatorSelectorLayout, ATeamBandIncludesOnlyConsecutiveRowsOfTheSameTeam)
{
	const int aTeams[] = {2, 2, 2, 3, 2};
	const auto TeamAt = [&](int Row) { return aTeams[Row]; };
	EXPECT_EQ(qm_spectator_layout::TeamBandEnd(0, 5, TeamAt), 3);
	EXPECT_EQ(qm_spectator_layout::TeamBandEnd(3, 5, TeamAt), 4);
	EXPECT_EQ(qm_spectator_layout::TeamBandEnd(4, 5, TeamAt), 5);
}

TEST(QmSpectatorSelectorLayout, ATeamBandStopsAtTheEndOfItsGroup)
{
	const auto TeamAt = [](int Row) {
		EXPECT_LT(Row, 2);
		return 2;
	};
	EXPECT_EQ(qm_spectator_layout::TeamBandEnd(0, 2, TeamAt), 2);
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
	EXPECT_FLOAT_EQ(Layout.m_Panel.y + Layout.m_Panel.h * 0.5f, Center.y);
	const CUIRect LastPlayer{Center.x - 270.0f, Layout.m_ContentCenter.y + PlayersBottom - 60.0f, 270.0f, 60.0f};
	ExpectContains(Layout.m_Panel, LastPlayer);
	ExpectContains(Layout.m_Mouse, LastPlayer);
	EXPECT_FLOAT_EQ(Layout.m_SearchRow.h, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_Status.h, 0.0f);
}

TEST(QmSpectatorSelectorLayout, EveryDensityCentersAndContainsGroupedRows)
{
	const vec2 Center(1066.0f, 600.0f);
	for(float Scale : {1.0f, 1.35f})
		for(int Count = 1; Count <= 256; ++Count)
			for(bool HasTeleSearch : {false, true})
			{
				SCOPED_TRACE(Scale);
				SCOPED_TRACE(Count);
				SCOPED_TRACE(HasTeleSearch);
				const auto Density = qm_spectator_layout::Density(Count);
				const float RowHeight = Density.m_RowHeight * Scale;
				const float RowWidth = (270.0f - Density.m_BoxOffset) * Scale;
				const float TitleHeight = std::clamp(RowHeight * 0.5f, 12.0f * Scale, 15.0f * Scale);
				const float TitleBlock = qm_spectator_layout::GroupTitle({}, RowWidth, TitleHeight, Scale).m_Height;
				const float PlayersTop = qm_spectator_layout::PlayerListStart(Scale) + Density.m_BoxMove * Scale;
				const float ColumnHeight = qm_spectator_friends::MaxColumnHeight(Count, 1, Density.m_RowsPerColumn, RowHeight, TitleBlock);
				const auto Layout = qm_spectator_layout::Build(Center, Density.m_HalfWidth * Scale, HasTeleSearch, PlayersTop + ColumnHeight, 300.0f * Scale);
				const CUIRect Players{Layout.m_Panel.x + 25.0f * Scale, Layout.m_ContentCenter.y + PlayersTop, RowWidth, ColumnHeight};
				const float PanelBottom = Layout.m_Panel.y + Layout.m_Panel.h;
				EXPECT_FLOAT_EQ(Layout.m_Panel.y + Layout.m_Panel.h * 0.5f, Center.y);
				EXPECT_GE(Layout.m_Panel.y, 0.0f);
				EXPECT_LE(PanelBottom, 1200.0f);
				EXPECT_NEAR(Layout.m_Panel.y, 1200.0f - PanelBottom, 0.001f);
				EXPECT_LT(Density.m_FontSize, Density.m_RowHeight);
				ExpectContains(Layout.m_Panel, Players);
				ExpectContains(Layout.m_Mouse, Players);
				for(int Index = 0; Index < 3; ++Index)
				{
					const CUIRect Tab = qm_spectator_layout::ViewTab(Layout, Index, Scale);
					ExpectContains(Layout.m_Panel, Tab);
					ExpectContains(Layout.m_Mouse, Tab);
					EXPECT_LT(Tab.y + Tab.h, Players.y);
				}
				if(HasTeleSearch)
				{
					EXPECT_PRED_FORMAT2(::testing::FloatLE, Players.y + Players.h + 20.0f, Layout.m_SearchRow.y);
					ExpectContains(Layout.m_Panel, Layout.m_SearchRow);
					ExpectContains(Layout.m_Panel, Layout.m_Status);
					ExpectContains(Layout.m_Mouse, Layout.m_SearchRow);
					ExpectContains(Layout.m_Mouse, Layout.m_Status);
				}
			}
}

TEST(QmSpectatorSelectorLayout, TallSkinsStayInsideEveryDensityRow)
{
	struct SRestoreConfig
	{
		int m_Fat = g_Config.m_ClFatSkins;
		~SRestoreConfig() { g_Config.m_ClFatSkins = m_Fat; }
	} RestoreConfig;
	for(int Fat : {0, 1})
		for(int Count : {1, 16, 17, 32, 33, 64, 65, 96, 97, 128, 129, 256})
		{
			SCOPED_TRACE(Fat);
			SCOPED_TRACE(Count);
			g_Config.m_ClFatSkins = Fat;
			const auto Density = qm_spectator_layout::Density(Count);
			CTeeRenderInfo Info;
			Info.m_Size = 64.0f * Density.m_TeeSizeMod;
			auto &Body = Info.m_SkinMetrics.m_Body;
			Body.m_Width = Body.m_Height = Body.m_MaxWidth = Body.m_MaxHeight = 128;
			Body.m_OffsetX = Body.m_OffsetY = 0;
			auto &Feet = Info.m_SkinMetrics.m_Feet;
			Feet.m_Width = Feet.m_MaxWidth = 64;
			Feet.m_Height = Feet.m_MaxHeight = 32;
			Feet.m_OffsetX = Feet.m_OffsetY = 0;
			qm_spectator_layout::FitTeeToRow(Info, Density.m_RowHeight);
			const auto *pIdle = CAnimState::GetIdle();
			vec2 Offset, BodyOffset, FeetOffset;
			float BodyWidth, BodyHeight, FeetWidth, FeetHeight;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdle, &Info, Offset);
			CRenderTools::GetRenderTeeBodySize(pIdle, &Info, BodyOffset, BodyWidth, BodyHeight);
			CRenderTools::GetRenderTeeFeetSize(pIdle, &Info, FeetOffset, FeetWidth, FeetHeight);
			const float Scale = Info.m_Size / 64.0f;
			const float BodyTop = Offset.y + (pIdle->GetBody()->m_Y - 32.0f) * Scale + BodyOffset.y;
			EXPECT_GT(BodyTop, -Density.m_RowHeight / 2.0f);
			EXPECT_LT(BodyTop + BodyHeight, Density.m_RowHeight / 2.0f);
			for(const auto *pFoot : {pIdle->GetFrontFoot(), pIdle->GetBackFoot()})
			{
				const float Bottom = Offset.y + (pFoot->m_Y - 16.0f) * Scale + FeetOffset.y + FeetHeight;
				EXPECT_LT(Bottom, Density.m_RowHeight / 2.0f);
			}
		}
}
