#include <game/client/QmUi/QmLyricsHud.h>
#include <game/client/components/hud_editor.h>
#include <game/client/components/hud_media_island_logic.h>

#include <gtest/gtest.h>

namespace
{
	struct SIslandFrame
	{
		CUIRect m_Timer;
		CUIRect m_Main;
		CUIRect m_Visible;
	};

	SIslandFrame IslandFrame(float LeftWidth, float RightWidth, float LeftSatellite = 0.0f, float RightSatellite = 0.0f, float TimerWidth = 60.0f)
	{
		const auto Anchor = QmHudMediaIslandAnchorTimer(300.0f, TimerWidth, LeftWidth, 2.0f);
		const CUIRect Timer{Anchor.m_TimerX, 5.0f, TimerWidth, 11.2f};
		const CUIRect Main{Anchor.m_MainX, 5.0f, LeftWidth + 2.0f + TimerWidth + RightWidth, 11.2f};
		return {Timer, Main, {Main.x - LeftSatellite, Main.y, Main.w + LeftSatellite + RightSatellite, Main.h}};
	}

	SIslandFrame PlaceFrame(const SIslandFrame &Frame, bool HasCustom, float StoredPosition, float Scale = 1.0f, float ScreenStart = 0.0f, float ScreenWidth = 600.0f)
	{
		const auto Transform = QmHudMediaIslandEditorTransformRect(Frame.m_Timer);
		const auto Horizontal = QmHudEditor::ResolveHorizontalPlacement(QmHudEditor::EHorizontalPlacement::TransformOrigin,
			HasCustom, StoredPosition, Transform.x, Frame.m_Visible.w * Scale, (Frame.m_Visible.x - Transform.x) * Scale, ScreenStart, ScreenWidth);
		const CUIRect Target{Horizontal.m_Position, Transform.y, Transform.w * Scale, Transform.h * Scale};
		return {QmLyricsHudAnchor(Frame.m_Timer, Transform, Target), QmLyricsHudAnchor(Frame.m_Main, Transform, Target), QmLyricsHudAnchor(Frame.m_Visible, Transform, Target)};
	}

	float SaveDraggedTimer(float Position, float ScreenStart = 0.0f, float ScreenWidth = 600.0f)
	{
		const auto Frame = IslandFrame(40.0f, 20.0f);
		const auto Transform = QmHudMediaIslandEditorTransformRect(Frame.m_Timer);
		const auto Horizontal = QmHudEditor::ResolveHorizontalPlacement(QmHudEditor::EHorizontalPlacement::TransformOrigin,
			false, 0.0f, Transform.x, Frame.m_Visible.w, Frame.m_Visible.x - Transform.x, ScreenStart, ScreenWidth);
		const auto Snap = QmHudEditor::ResolveAxisSnapEx(Position, Horizontal.m_Width, ScreenStart, ScreenWidth, nullptr, 0, Horizontal.m_Offset);
		return round_to_int(QmHudEditor::StoreAxisAnchor(Snap.m_Position, Horizontal.m_Width, ScreenStart, ScreenWidth, Horizontal.m_Offset) * 10000.0f) / 10000.0f;
	}
}

TEST(QmHudMediaIslandEditorPlacement, MusicVisibilityKeepsTheSavedTimerCenter)
{
	for(const float ScreenWidth : {400.0f, 533.3333f, 700.0f})
	{
		SCOPED_TRACE(ScreenWidth);
		const float Center = 13.0f + ScreenWidth * 0.5f;
		const float Stored = SaveDraggedTimer(Center + 2.0f, 13.0f, ScreenWidth);
		for(const float LeftWidth : {0.0f, 40.0f, 90.0f, 40.0f, 0.0f})
		{
			SCOPED_TRACE(LeftWidth);
			const auto Placed = PlaceFrame(IslandFrame(LeftWidth, 20.0f), true, Stored, 1.0f, 13.0f, ScreenWidth);
			EXPECT_NEAR(Placed.m_Timer.x + Placed.m_Timer.w * 0.5f, Center, ScreenWidth / 10000.0f);
		}
	}
}

TEST(QmHudMediaIslandEditorPlacement, RecordingStatusExpandsRightWithoutMovingTheTimer)
{
	const float Stored = SaveDraggedTimer(210.0f);
	const auto Before = PlaceFrame(IslandFrame(40.0f, 20.0f), true, Stored);
	for(const float RightWidth : {20.0f, 80.0f, 150.0f, 20.0f})
	{
		SCOPED_TRACE(RightWidth);
		const auto Placed = PlaceFrame(IslandFrame(40.0f, RightWidth), true, Stored);
		EXPECT_FLOAT_EQ(Placed.m_Timer.x, Before.m_Timer.x);
		EXPECT_FLOAT_EQ(Placed.m_Main.x, Before.m_Main.x);
		EXPECT_FLOAT_EQ(Placed.m_Main.w - Before.m_Main.w, RightWidth - 20.0f);
	}
}

TEST(QmHudMediaIslandEditorPlacement, EyeAndCountdownSatellitesDoNotTranslateTheTimerOrMainCapsule)
{
	const float Stored = SaveDraggedTimer(210.0f);
	const auto Before = PlaceFrame(IslandFrame(40.0f, 20.0f), true, Stored);
	for(const float LeftSatellite : {0.0f, 40.0f, 100.0f})
	{
		SCOPED_TRACE(LeftSatellite);
		for(const float RightSatellite : {0.0f, 35.0f, 80.0f})
		{
			SCOPED_TRACE(RightSatellite);
			const auto Placed = PlaceFrame(IslandFrame(40.0f, 20.0f, LeftSatellite, RightSatellite), true, Stored);
			EXPECT_FLOAT_EQ(Placed.m_Timer.x, Before.m_Timer.x);
			EXPECT_FLOAT_EQ(Placed.m_Main.x, Before.m_Main.x);
			EXPECT_FLOAT_EQ(Placed.m_Main.w, Before.m_Main.w);
			EXPECT_FLOAT_EQ(Placed.m_Visible.w - Before.m_Visible.w, LeftSatellite + RightSatellite);
		}
	}
}

TEST(QmHudMediaIslandEditorPlacement, TimerTextWidthAndHudScaleKeepTheSameCenter)
{
	const float Stored = SaveDraggedTimer(210.0f);
	for(const int ScalePercent : {25, 75, 100, 150, 400})
	{
		SCOPED_TRACE(ScalePercent);
		for(const float TimerWidth : {40.0f, 60.0f, 80.0f})
		{
			SCOPED_TRACE(TimerWidth);
			const auto Placed = PlaceFrame(IslandFrame(90.0f, 100.0f, 40.0f, 35.0f, TimerWidth), true, Stored, QmHudEditor::ResolveScale(ScalePercent));
			EXPECT_NEAR(Placed.m_Timer.x + Placed.m_Timer.w * 0.5f, 210.0f, 0.001f);
		}
	}
}

TEST(QmHudMediaIslandEditorPlacement, ExpandingAcrossScreenEdgesDoesNotPushTheTimer)
{
	for(const float Stored : {0.0f, 0.05f, 0.5f, 0.95f, 1.0f})
	{
		SCOPED_TRACE(Stored);
		for(const float ScreenWidth : {80.0f, 400.0f, 600.0f})
		{
			SCOPED_TRACE(ScreenWidth);
			for(const auto Frame : {IslandFrame(0.0f, 20.0f), IslandFrame(190.0f, 200.0f, 100.0f, 100.0f)})
			{
				SCOPED_TRACE(Frame.m_Visible.w);
				const auto Placed = PlaceFrame(Frame, true, Stored, 1.5f, 13.0f, ScreenWidth);
				EXPECT_NEAR(Placed.m_Timer.x + Placed.m_Timer.w * 0.5f, 13.0f + Stored * ScreenWidth, 0.001f);
			}
		}
	}
}

TEST(QmHudMediaIslandEditorPlacement, DefaultPlacementKeepsTheSourceTimerAndMainCapsule)
{
	for(const auto Frame : {IslandFrame(0.0f, 20.0f), IslandFrame(90.0f, 100.0f, 40.0f, 35.0f)})
	{
		SCOPED_TRACE(Frame.m_Visible.w);
		const auto Placed = PlaceFrame(Frame, false, 0.0f);
		EXPECT_FLOAT_EQ(Placed.m_Timer.x, Frame.m_Timer.x);
		EXPECT_FLOAT_EQ(Placed.m_Main.x, Frame.m_Main.x);
		EXPECT_FLOAT_EQ(Placed.m_Main.w, Frame.m_Main.w);
	}
}

TEST(QmHudMediaIslandEditorPlacement, HidingTimerUsesMainCapsuleCenterWithoutSatellites)
{
	const CUIRect Main{260.0f, 5.0f, 50.0f, 22.4f};
	const auto Transform = QmHudMediaIslandEditorTransformRect(Main);
	const auto Horizontal = QmHudEditor::ResolveHorizontalPlacement(QmHudEditor::EHorizontalPlacement::TransformOrigin,
		true, 0.7f, Transform.x, 180.0f, -90.0f, 0.0f, 600.0f);
	const auto Placed = QmLyricsHudAnchor(Main, Transform, {Horizontal.m_Position, Transform.y, Transform.w, Transform.h});
	EXPECT_NEAR(Placed.x + Placed.w * 0.5f, 420.0f, 0.001f);
}

TEST(QmHudMediaIslandEditorPlacement, FollowingLyricsIgnoreSavedHorizontalPositionAndScaleAfterMainIslandChanges)
{
	for(const auto Frame : {IslandFrame(40.0f, 20.0f), IslandFrame(90.0f, 100.0f, 40.0f, 35.0f), IslandFrame(0.0f, 20.0f)})
	{
		SCOPED_TRACE(Frame.m_Visible.w);
		const auto Main = PlaceFrame(Frame, true, 0.35f, 1.5f).m_Main;
		const auto Lyrics = QmLyricsIslandLayout(Main, 600.0f, 300.0f, 2, true);
		for(const float OldX : {0.0f, 0.2f, 0.7f, 1.0f})
		{
			SCOPED_TRACE(OldX);
			const float Scale = QmHudEditor::ResolveScale(175, false);
			const auto Horizontal = QmHudEditor::ResolveHorizontalPlacement(QmHudEditor::EHorizontalPlacement::DefaultPosition,
				true, OldX, Lyrics.x, Lyrics.w * Scale, 0.0f, 0.0f, 600.0f);
			const float Y = QmHudEditor::RestoreAxisAnchor(0.4f, Lyrics.h, 0.0f, 300.0f, 0.0f);
			const auto Placed = QmLyricsHudAnchor(Lyrics, Lyrics, {Horizontal.m_Position, Y, Lyrics.w * Scale, Lyrics.h * Scale});
			EXPECT_FLOAT_EQ(Placed.x, Main.x);
			EXPECT_FLOAT_EQ(Placed.x + Placed.w, Main.x + Main.w);
			EXPECT_FLOAT_EQ(Placed.y, 120.0f);
		}
	}
}
