#include <game/client/components/hud_editor.h>
#include <game/client/components/hud_media_island_logic.h>

#include <gtest/gtest.h>

namespace
{
	constexpr CUIRect IdleIsland{240.0f, 0.0f, 80.0f, 11.2f};
	constexpr CUIRect PlayingIsland{180.0f, 0.0f, 140.0f, 22.4f};
	constexpr CUIRect ExpandedIsland{130.0f, 0.0f, 190.0f, 28.0f};
	constexpr float PositionScale = 10000.0f;

	float SaveSnappedPosition(const CUIRect &VisibleRect, float VisibleX, float Scale, float ScreenStart, float ScreenWidth)
	{
		const CUIRect TransformRect = QmHudMediaIslandEditorTransformRect(VisibleRect);
		const float Offset = (VisibleRect.x - TransformRect.x) * Scale;
		const float Width = VisibleRect.w * Scale;
		const auto Snap = QmHudEditor::ResolveAxisSnapEx(VisibleX - Offset, Width, ScreenStart, ScreenWidth, nullptr, 0, Offset);
		return round_to_int(QmHudEditor::StoreAxisAnchor(Snap.m_Position, Width, ScreenStart, ScreenWidth, Offset) * PositionScale) / PositionScale;
	}

	CUIRect RestorePosition(const CUIRect &VisibleRect, float StoredPosition, float Scale, float ScreenStart, float ScreenWidth)
	{
		const CUIRect TransformRect = QmHudMediaIslandEditorTransformRect(VisibleRect);
		const float Offset = (VisibleRect.x - TransformRect.x) * Scale;
		const float Width = VisibleRect.w * Scale;
		const float Anchor = QmHudEditor::RestoreAxisAnchor(StoredPosition, Width, ScreenStart, ScreenWidth, Offset);
		return {Anchor + Offset, VisibleRect.y, Width, VisibleRect.h * Scale};
	}
}

TEST(QmHudMediaIslandEditorPlacement, StartingMusicAfterCenteringKeepsWholeIslandCentered)
{
	constexpr float ScreenStart = 13.0f;
	for(const float ScreenWidth : {400.0f, 533.3333f, 700.0f})
	{
		SCOPED_TRACE(ScreenWidth);
		for(const float Scale : {0.75f, 1.0f, 1.5f})
		{
			SCOPED_TRACE(Scale);
			const float CenterX = ScreenStart + ScreenWidth * 0.5f;
			const float Stored = SaveSnappedPosition(IdleIsland, CenterX - IdleIsland.w * Scale * 0.5f + 2.0f, Scale, ScreenStart, ScreenWidth);
			for(const CUIRect &Visible : {IdleIsland, PlayingIsland, ExpandedIsland, PlayingIsland, IdleIsland})
			{
				SCOPED_TRACE(Visible.w);
				const CUIRect Restored = RestorePosition(Visible, Stored, Scale, ScreenStart, ScreenWidth);
				EXPECT_NEAR(Restored.x + Restored.w * 0.5f, CenterX, ScreenWidth / PositionScale);
			}
		}
	}
}

TEST(QmHudMediaIslandEditorPlacement, StoppingMusicAfterCenteringKeepsWholeIslandCentered)
{
	for(const float Scale : {0.75f, 1.0f, 1.5f})
	{
		SCOPED_TRACE(Scale);
		const float Stored = SaveSnappedPosition(PlayingIsland, 300.0f - PlayingIsland.w * Scale * 0.5f, Scale, 0.0f, 600.0f);
		for(const CUIRect &Visible : {ExpandedIsland, IdleIsland, PlayingIsland, IdleIsland})
		{
			SCOPED_TRACE(Visible.w);
			const CUIRect Restored = RestorePosition(Visible, Stored, Scale, 0.0f, 600.0f);
			EXPECT_NEAR(Restored.x + Restored.w * 0.5f, 300.0f, 0.06f);
		}
	}
}

TEST(QmHudMediaIslandEditorPlacement, MusicVisibilityKeepsCustomOffCenterPosition)
{
	constexpr float Scale = 1.5f;
	constexpr float CenterX = 210.0f;
	const float Stored = SaveSnappedPosition(PlayingIsland, CenterX - PlayingIsland.w * Scale * 0.5f, Scale, 0.0f, 600.0f);
	for(const CUIRect &Visible : {PlayingIsland, IdleIsland, ExpandedIsland, IdleIsland})
	{
		SCOPED_TRACE(Visible.w);
		const CUIRect Restored = RestorePosition(Visible, Stored, Scale, 0.0f, 600.0f);
		EXPECT_NEAR(Restored.x + Restored.w * 0.5f, CenterX, 0.06f);
	}
}

TEST(QmHudMediaIslandEditorPlacement, HidingTimerKeepsTheSameSavedCenter)
{
	const float Stored = SaveSnappedPosition(PlayingIsland, 230.0f, 1.0f, 0.0f, 600.0f);
	const CUIRect MediaWithoutTimer{260.0f, 0.0f, 50.0f, 22.4f};
	for(const CUIRect &Visible : {MediaWithoutTimer, ExpandedIsland, IdleIsland})
	{
		SCOPED_TRACE(Visible.w);
		const CUIRect Restored = RestorePosition(Visible, Stored, 1.0f, 0.0f, 600.0f);
		EXPECT_NEAR(Restored.x + Restored.w * 0.5f, 300.0f, 0.06f);
	}
}

TEST(QmHudMediaIslandEditorPlacement, ScreenEdgesStayAttachedWhenMusicVisibilityChanges)
{
	constexpr float ScreenStart = 13.0f;
	constexpr float ScreenWidth = 600.0f;
	for(const CUIRect &Initial : {IdleIsland, PlayingIsland})
	{
		SCOPED_TRACE(Initial.w);
		for(const float Scale : {0.75f, 1.0f, 1.5f})
		{
			SCOPED_TRACE(Scale);
			for(const bool RightEdge : {false, true})
			{
				SCOPED_TRACE(RightEdge);
				const float Stored = SaveSnappedPosition(Initial, RightEdge ? 800.0f : -100.0f, Scale, ScreenStart, ScreenWidth);
				EXPECT_FLOAT_EQ(Stored, RightEdge ? 1.0f : 0.0f);
				for(const CUIRect &Visible : {IdleIsland, PlayingIsland, ExpandedIsland})
				{
					SCOPED_TRACE(Visible.w);
					const CUIRect Restored = RestorePosition(Visible, Stored, Scale, ScreenStart, ScreenWidth);
					EXPECT_NEAR(RightEdge ? Restored.x + Restored.w : Restored.x, RightEdge ? ScreenStart + ScreenWidth : ScreenStart, 0.001f);
				}
			}
		}
	}
}

TEST(QmHudMediaIslandEditorPlacement, ScreenClampDoesNotOverwriteSavedCenter)
{
	const float Stored = SaveSnappedPosition(IdleIsland, 50.0f, 1.0f, 0.0f, 600.0f);
	const CUIRect Expanded = RestorePosition(ExpandedIsland, Stored, 1.0f, 0.0f, 600.0f);
	EXPECT_NEAR(Expanded.x, 0.0f, 0.001f);
	const CUIRect Collapsed = RestorePosition(IdleIsland, Stored, 1.0f, 0.0f, 600.0f);
	EXPECT_NEAR(Collapsed.x + Collapsed.w * 0.5f, 90.0f, 0.06f);
}

TEST(QmHudMediaIslandEditorPlacement, DefaultPlacementKeepsOriginalVisibleGeometry)
{
	for(const CUIRect &Visible : {IdleIsland, PlayingIsland, ExpandedIsland})
	{
		SCOPED_TRACE(Visible.w);
		const CUIRect Transform = QmHudMediaIslandEditorTransformRect(Visible);
		const float Offset = Visible.x - Transform.x;
		const float Anchor = QmHudEditor::SnapAxisToScreenEdgesEx(Transform.x, Visible.w, 0.0f, 600.0f, Offset);
		EXPECT_NEAR(Anchor + Offset, Visible.x, 0.001f);
		EXPECT_FLOAT_EQ(Transform.y, Visible.y);
	}
}
