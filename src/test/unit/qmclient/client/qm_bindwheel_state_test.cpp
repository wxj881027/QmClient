#include <game/client/components/qmclient/bind_menu_layout.h>
#include <game/client/components/qmclient/bind_menu_preview.h>
#include <game/client/components/qmclient/bindwheel_state.h>

#include <gtest/gtest.h>

namespace
{
	bool RectsOverlap(const CUIRect &A, const CUIRect &B)
	{
		return A.x < B.x + B.w && A.x + A.w > B.x && A.y < B.y + B.h && A.y + A.h > B.y;
	}
}

TEST(QmBindMenu, ParsesAllMenuTypesAndBuildsTriggerCommands)
{
	ASSERT_EQ(qm_bind_menu::NUM_MENUS, 4);
	for(int Index = 0; Index < qm_bind_menu::NUM_MENUS; ++Index)
	{
		const auto Menu = static_cast<qm_bind_menu::EMenu>(Index);
		const auto Parsed = qm_bind_menu::Parse(qm_bind_menu::Name(Menu));
		ASSERT_TRUE(Parsed.has_value());
		EXPECT_EQ(*Parsed, Menu);
		EXPECT_STREQ(qm_bind_menu::TriggerCommand(Menu), qm_bind_menu::TriggerCommand(*Parsed));
	}
	EXPECT_STREQ(qm_bind_menu::LocalizedName(qm_bind_menu::EMenu::ASSETS), "Assets");
}

TEST(QmBindMenu, IdentityRowsDoNotRequestAHoverBubble)
{
	EXPECT_FALSE(qm_bind_menu::HasHoverDetails(qm_bind_menu::EMenu::IDENTITY));
	EXPECT_TRUE(qm_bind_menu::HasHoverDetails(qm_bind_menu::EMenu::CHAT));
	EXPECT_TRUE(qm_bind_menu::HasHoverDetails(qm_bind_menu::EMenu::ASSETS));
	EXPECT_TRUE(qm_bind_menu::HasHoverDetails(qm_bind_menu::EMenu::CUSTOM));
}

TEST(QmBindWheelState, ReleaseCommitsSelectionOnce)
{
	CQmBindWheelState State;
	State.Open(qm_bind_menu::EMenu::IDENTITY);
	State.TriggerRelease(qm_bind_menu::EMenu::IDENTITY, 3);
	State.TriggerRelease(qm_bind_menu::EMenu::IDENTITY, 4);
	EXPECT_FALSE(State.IsActive());
	EXPECT_EQ(State.TakePendingBind(), 3);
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, GlobalReleaseCancelsSelection)
{
	CQmBindWheelState State;
	State.Open(qm_bind_menu::EMenu::CHAT);
	State.Cancel();
	State.TriggerRelease(qm_bind_menu::EMenu::CHAT, 2);
	EXPECT_FALSE(State.IsActive());
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, CancelDiscardsPendingCommand)
{
	CQmBindWheelState State;
	State.Open(qm_bind_menu::EMenu::CUSTOM);
	State.TriggerRelease(qm_bind_menu::EMenu::CUSTOM, 2);
	State.Cancel();
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, BindMutationDiscardsPendingIndex)
{
	CQmBindWheelState State;
	State.Open(qm_bind_menu::EMenu::IDENTITY);
	State.TriggerRelease(qm_bind_menu::EMenu::IDENTITY, 2);
	State.InvalidateSelection();
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, ReopeningDoesNotExecutePreviousSelection)
{
	CQmBindWheelState State;
	State.Open(qm_bind_menu::EMenu::IDENTITY);
	State.TriggerRelease(qm_bind_menu::EMenu::IDENTITY, 2);
	State.Open(qm_bind_menu::EMenu::IDENTITY);
	EXPECT_TRUE(State.IsActive());
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, ResolvesLatestCursorPositionAndEmptyCenter)
{
	EXPECT_EQ(CQmBindWheelState::SelectedWheel(vec2(0, 0), 4), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedWheel(vec2(110, 0), 4), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedWheel(vec2(170, 0), 0), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedWheel(vec2(170, 0), 4), 0);
	EXPECT_EQ(CQmBindWheelState::SelectedWheel(vec2(0, 170), 4), 1);
	EXPECT_EQ(CQmBindWheelState::SelectedWheel(vec2(-170, 0), 4), 2);
	EXPECT_EQ(CQmBindWheelState::SelectedWheel(vec2(0, -170), 4), 3);
}

TEST(QmBindWheelState, CenterReleaseStillRequestsMouseReset)
{
	CQmBindWheelState State;
	State.Open(qm_bind_menu::EMenu::CUSTOM);
	State.TriggerRelease(qm_bind_menu::EMenu::CUSTOM, -1);
	const auto Pending = State.TakePendingBind();
	ASSERT_TRUE(Pending.has_value());
	EXPECT_EQ(*Pending, -1);
}

TEST(QmBindWheelState, BothLayoutsReleaseOnTriggerKey)
{
	CQmBindWheelState State;
	State.Open(qm_bind_menu::EMenu::IDENTITY);
	State.TriggerRelease(qm_bind_menu::EMenu::IDENTITY, 2);
	EXPECT_FALSE(State.IsActive());
	EXPECT_EQ(State.TakePendingBind(), 2);
}

TEST(QmBindWheelState, ReleaseActiveClosesEvenWhenMenuArgumentIsMissing)
{
	CQmBindWheelState State;
	State.Open(qm_bind_menu::EMenu::CHAT);
	State.ReleaseActive(4);
	EXPECT_FALSE(State.IsActive());
	EXPECT_EQ(State.TakePendingBind(), 4);
	State.ReleaseActive(5);
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, ClickAppliesSelectionAndCloses)
{
	CQmBindWheelState State;
	State.Open(qm_bind_menu::EMenu::CHAT);
	State.ApplyClick(1);
	EXPECT_FALSE(State.IsActive());
	EXPECT_EQ(State.TakePendingBind(), 1);
	State.TriggerRelease(qm_bind_menu::EMenu::CHAT, 2);
	EXPECT_FALSE(State.TakePendingBind().has_value());
}

TEST(QmBindWheelState, RepeatedPressDoesNotResetSelection)
{
	CQmBindWheelState State;
	State.Open(qm_bind_menu::EMenu::CUSTOM);
	State.Open(qm_bind_menu::EMenu::CUSTOM);
	State.ApplyClick(1);
	EXPECT_EQ(State.TakePendingBind(), 1);
}

TEST(QmBindWheelState, PanelHitTestRespectsScrollAndBounds)
{
	EXPECT_EQ(CQmBindWheelState::SelectedPanel(vec2(110, 25), 100, 0, 200, 50, 4, 0), 0);
	EXPECT_EQ(CQmBindWheelState::SelectedPanel(vec2(110, 75), 100, 0, 200, 50, 4, 1), 2);
	EXPECT_EQ(CQmBindWheelState::SelectedPanel(vec2(90, 25), 100, 0, 200, 50, 4, 0), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedPanel(vec2(110, 225), 100, 0, 200, 50, 4, 0), -1);
}

TEST(QmBindWheelState, PanelHitTestSkipsRowSpacing)
{
	EXPECT_EQ(CQmBindWheelState::SelectedPanel(vec2(110, 20), 100, 0, 200, 40, 4, 0, 10), 0);
	EXPECT_EQ(CQmBindWheelState::SelectedPanel(vec2(110, 60), 100, 0, 200, 40, 4, 0, 10), 1);
	EXPECT_EQ(CQmBindWheelState::SelectedPanel(vec2(110, 45), 100, 0, 200, 40, 4, 0, 10), -1);
}

TEST(QmBindWheelState, IdentityColumnsKeepMainAndDummyClansSeparate)
{
	const auto Layout = qm_bind_menu_preview::BuildIdentityLayout(CUIRect{0.0f, 0.0f, 320.0f, 56.0f}, true, true, 1.0f);
	ASSERT_EQ(Layout.m_TeeCount, 2);
	EXPECT_FALSE(Layout.m_Compact);
	for(int Tee = 0; Tee < Layout.m_TeeCount; ++Tee)
	{
		EXPECT_GT(Layout.m_aRows[Tee].m_Avatar.w, 0.0f);
		EXPECT_GT(Layout.m_aRows[Tee].m_Name.w, 0.0f);
		EXPECT_GT(Layout.m_aRows[Tee].m_Name.h, 0.0f);
		EXPECT_GT(Layout.m_aRows[Tee].m_Clan.w, 0.0f);
		EXPECT_GT(Layout.m_aRows[Tee].m_Clan.h, 0.0f);
	}
	EXPECT_LE(Layout.m_aRows[0].m_Name.x + Layout.m_aRows[0].m_Name.w, Layout.m_aRows[1].m_Avatar.x);
}

TEST(QmBindWheelState, IdentityCompactLayoutReservesAvatarsAndLabelLines)
{
	const auto Layout = qm_bind_menu_preview::BuildIdentityLayout(CUIRect{0.0f, 0.0f, 84.0f, 40.0f}, true, true, 1.0f);
	ASSERT_EQ(Layout.m_TeeCount, 2);
	EXPECT_TRUE(Layout.m_Compact);
	for(int Tee = 0; Tee < Layout.m_TeeCount; ++Tee)
		EXPECT_GT(Layout.m_aRows[Tee].m_Avatar.w, 0.0f);
	EXPECT_GT(Layout.m_CompactNames.w, 0.0f);
	EXPECT_GT(Layout.m_CompactNames.h, 0.0f);
	EXPECT_GT(Layout.m_CompactClans.w, 0.0f);
	EXPECT_GT(Layout.m_CompactClans.h, 0.0f);
	EXPECT_LE(Layout.m_CompactNames.y + Layout.m_CompactNames.h, Layout.m_CompactClans.y);
}

TEST(QmBindWheelState, WheelCardsRemainSeparatedForTenEntries)
{
	const CUIRect Area{0.0f, 0.0f, 740.0f, 500.0f};
	const auto Layout = qm_bind_menu_preview::BuildLayout(Area, qm_bind_menu::ELayout::WHEEL, 10, 0, 1.0f, false);
	EXPECT_FALSE(Layout.m_WheelDoubleRing);
	EXPECT_GE(Layout.m_WheelCardWidth, 85.0f);
	EXPECT_GE(Layout.m_WheelCardHeight, 50.0f);
	for(int First = 0; First < 10; ++First)
	{
		const CUIRect FirstRect = qm_bind_menu_preview::WheelItemRect(Layout, First, 10);
		EXPECT_GE(FirstRect.x, Area.x);
		EXPECT_GE(FirstRect.y, Area.y);
		EXPECT_LE(FirstRect.x + FirstRect.w, Area.x + Area.w);
		EXPECT_LE(FirstRect.y + FirstRect.h, Area.y + Area.h);
		for(const vec2 Corner : {vec2(FirstRect.x, FirstRect.y), vec2(FirstRect.x + FirstRect.w, FirstRect.y),
			    vec2(FirstRect.x, FirstRect.y + FirstRect.h), vec2(FirstRect.x + FirstRect.w, FirstRect.y + FirstRect.h)})
			EXPECT_LE(length(Corner - Layout.m_Center), Layout.m_WheelBackgroundOuterRadius);
		for(int Second = First + 1; Second < 10; ++Second)
			EXPECT_FALSE(RectsOverlap(FirstRect, qm_bind_menu_preview::WheelItemRect(Layout, Second, 10)));
	}
}

TEST(QmBindWheelState, WheelUsesTwoOrbitsWhenSingleRingWouldCrowd)
{
	const CUIRect Area{0.0f, 0.0f, 740.0f, 500.0f};
	const auto Layout = qm_bind_menu_preview::BuildLayout(Area, qm_bind_menu::ELayout::WHEEL, 14, 0, 1.0f, false);
	EXPECT_TRUE(Layout.m_WheelDoubleRing);
	EXPECT_LT(Layout.m_WheelInnerRadius, Layout.m_WheelRadius);
	for(int First = 0; First < 14; ++First)
		for(int Second = First + 1; Second < 14; ++Second)
			EXPECT_FALSE(RectsOverlap(qm_bind_menu_preview::WheelItemRect(Layout, First, 14),
				qm_bind_menu_preview::WheelItemRect(Layout, Second, 14)));
}

TEST(QmBindWheelState, ScreenWheelUsesSmallerRadiusThanSettingsPreview)
{
	const CUIRect Area{0.0f, 0.0f, 1920.0f, 1080.0f};
	const auto Preview = qm_bind_menu_preview::BuildLayout(Area, qm_bind_menu::ELayout::WHEEL, 10, 0, 1.0f, false);
	const auto Overlay = qm_bind_menu_preview::BuildLayout(Area, qm_bind_menu::ELayout::WHEEL, 10, 0, 1.0f, true);
	EXPECT_LT(Overlay.m_WheelRadius, Preview.m_WheelRadius);
}

TEST(QmBindWheelState, RevealScaleStartsSmallAndSettlesAtFullSize)
{
	EXPECT_FLOAT_EQ(qm_bind_menu_preview::RevealScaleFor(qm_bind_menu::ELayout::WHEEL, 0.0f),
		ui_token::bind_menu::WHEEL_ENTER_SCALE);
	EXPECT_FLOAT_EQ(qm_bind_menu_preview::RevealScaleFor(qm_bind_menu::ELayout::PANEL, 0.0f),
		ui_token::bind_menu::PANEL_ENTER_SCALE);
	EXPECT_FLOAT_EQ(qm_bind_menu_preview::RevealScaleFor(qm_bind_menu::ELayout::WHEEL, 1.0f), 1.0f);
	EXPECT_FLOAT_EQ(qm_bind_menu_preview::RevealScaleFor(qm_bind_menu::ELayout::PANEL, 1.0f), 1.0f);
}

TEST(QmBindWheelState, PanelIsCenteredInScreenAndPreview)
{
	const CUIRect Area{100.0f, 50.0f, 1200.0f, 800.0f};
	for(const bool ScreenOverlay : {false, true})
	{
		const auto Layout = qm_bind_menu_preview::BuildLayout(Area, qm_bind_menu::ELayout::PANEL, 4, 0, 1.0f, ScreenOverlay);
		EXPECT_FLOAT_EQ(Layout.m_Panel.Center().x, Area.Center().x);
		EXPECT_FLOAT_EQ(Layout.m_Panel.Center().y, Area.Center().y);
		EXPECT_GE(Layout.m_Panel.x, Area.x);
		EXPECT_GE(Layout.m_Panel.y, Area.y);
		EXPECT_LE(Layout.m_Panel.x + Layout.m_Panel.w, Area.x + Area.w);
		EXPECT_LE(Layout.m_Panel.y + Layout.m_Panel.h, Area.y + Area.h);
		EXPECT_GE(Layout.m_Rows.y, Layout.m_Panel.y);
		EXPECT_LE(Layout.m_Rows.y + Layout.m_Rows.h, Layout.m_Panel.y + Layout.m_Panel.h);
	}
}

TEST(QmBindWheelState, PanelOpenCursorStartsOutsideRows)
{
	const CUIRect Area{0.0f, 0.0f, 1920.0f, 1080.0f};
	const auto Layout = qm_bind_menu_preview::BuildLayout(Area, qm_bind_menu::ELayout::PANEL, 4, 0, 1.0f, true);
	EXPECT_EQ(qm_bind_menu_preview::HitTest(Layout, Layout.m_Title.Center(), 4, 0), -1);
	const vec2 FirstRowCenter{Layout.m_Rows.x + Layout.m_Rows.w * 0.5f, Layout.m_Rows.y + Layout.m_RowHeight * 0.5f};
	EXPECT_GE(qm_bind_menu_preview::HitTest(Layout, FirstRowCenter, 4, 0), 0);
}

TEST(QmBindWheelState, AssetMenuReservesMorePreviewHeight)
{
	const CUIRect Area{0.0f, 0.0f, 1200.0f, 800.0f};
	const auto Panel = qm_bind_menu_preview::BuildLayout(Area, qm_bind_menu::ELayout::PANEL, 4, 0, 1.0f, true,
		qm_bind_menu::EMenu::CUSTOM);
	const auto AssetPanel = qm_bind_menu_preview::BuildLayout(Area, qm_bind_menu::ELayout::PANEL, 4, 0, 1.0f, true,
		qm_bind_menu::EMenu::ASSETS);
	EXPECT_GT(AssetPanel.m_RowHeight, Panel.m_RowHeight);

	const auto Wheel = qm_bind_menu_preview::BuildLayout(Area, qm_bind_menu::ELayout::WHEEL, 4, 0, 1.0f, true,
		qm_bind_menu::EMenu::CUSTOM);
	const auto AssetWheel = qm_bind_menu_preview::BuildLayout(Area, qm_bind_menu::ELayout::WHEEL, 4, 0, 1.0f, true,
		qm_bind_menu::EMenu::ASSETS);
	EXPECT_GT(AssetWheel.m_WheelCardHeight, Wheel.m_WheelCardHeight);
}
