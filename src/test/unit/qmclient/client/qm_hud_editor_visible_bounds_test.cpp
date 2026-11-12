#include <game/client/components/hud_editor.h>
#include <game/client/components/qmclient/player_state_hud_bounds.h>

#include <gtest/gtest.h>

TEST(QmHudEditorVisibleBounds, ReportedContentReplacesEstimatedEmptySpace)
{
	QmHudEditor::CVisibleBounds Bounds;
	const CUIRect Transform{0.0f, 50.0f, 232.0f, 250.0f};
	EXPECT_FLOAT_EQ(Bounds.Resolve(Transform, Transform).h, 250.0f);

	// 聊天预估整个历史区，实际只绘制底部两行；此处输入的是绘制后的 UI 范围。
	Bounds.Observe(Transform, Transform, {0.0f, 100.0f, 464.0f, 500.0f}, {8.0f, 500.0f, 440.0f, 80.0f});
	const CUIRect Visible = Bounds.Resolve(Transform, Transform);
	EXPECT_FLOAT_EQ(Visible.x, 4.0f);
	EXPECT_FLOAT_EQ(Visible.y, 250.0f);
	EXPECT_FLOAT_EQ(Visible.w, 220.0f);
	EXPECT_FLOAT_EQ(Visible.h, 40.0f);
}

TEST(QmHudEditorVisibleBounds, MovingAndScalingDoesNotChangeSourceBounds)
{
	QmHudEditor::CVisibleBounds Bounds;
	const CUIRect Transform{10.0f, 20.0f, 100.0f, 80.0f};
	Bounds.Observe(Transform, Transform, {200.0f, 300.0f, 50.0f, 40.0f}, {205.0f, 310.0f, 40.0f, 25.0f});
	const CUIRect First = Bounds.Resolve(Transform, Transform);
	EXPECT_FLOAT_EQ(First.x, 20.0f);
	EXPECT_FLOAT_EQ(First.y, 40.0f);
	EXPECT_FLOAT_EQ(First.w, 80.0f);
	EXPECT_FLOAT_EQ(First.h, 50.0f);

	Bounds.Observe(Transform, Transform, {50.0f, 70.0f, 200.0f, 160.0f}, {70.0f, 110.0f, 160.0f, 100.0f});
	const CUIRect Second = Bounds.Resolve(Transform, Transform);
	EXPECT_FLOAT_EQ(Second.x, First.x);
	EXPECT_FLOAT_EQ(Second.y, First.y);
	EXPECT_FLOAT_EQ(Second.w, First.w);
	EXPECT_FLOAT_EQ(Second.h, First.h);
}

TEST(QmHudEditorVisibleBounds, ChangingLayoutDiscardsStaleMeasurement)
{
	QmHudEditor::CVisibleBounds Bounds;
	const CUIRect Transform{0.0f, 50.0f, 232.0f, 250.0f};
	Bounds.Observe(Transform, Transform, Transform, {4.0f, 250.0f, 220.0f, 40.0f});
	const CUIRect NewTransform{0.0f, 40.0f, 300.0f, 260.0f};
	const CUIRect NewDeclared{5.0f, 80.0f, 280.0f, 180.0f};
	EXPECT_FLOAT_EQ(Bounds.Resolve(NewTransform, NewTransform).w, 300.0f);
	EXPECT_FLOAT_EQ(Bounds.Resolve(Transform, NewDeclared).y, 80.0f);
	EXPECT_FLOAT_EQ(Bounds.Resolve(Transform, NewDeclared).h, 180.0f);

	Bounds.Observe(NewTransform, NewDeclared, NewTransform, {8.0f, 240.0f, 270.0f, 45.0f});
	EXPECT_FLOAT_EQ(Bounds.Resolve(NewTransform, NewDeclared).h, 45.0f);
}

TEST(QmHudEditorVisibleBounds, EmptyReportDoesNotReplaceUsableMeasurement)
{
	QmHudEditor::CVisibleBounds Bounds;
	const CUIRect Transform{0.0f, 0.0f, 100.0f, 100.0f};
	Bounds.Observe(Transform, Transform, Transform, {10.0f, 20.0f, 80.0f, 60.0f});
	Bounds.Observe(Transform, Transform, Transform, {});
	Bounds.Observe(Transform, Transform, {}, Transform);
	EXPECT_FLOAT_EQ(Bounds.Resolve(Transform, Transform).y, 20.0f);
	EXPECT_FLOAT_EQ(Bounds.Resolve(Transform, Transform).h, 60.0f);
}

TEST(QmHudEditorVisibleBounds, ActualVisibleEdgesCanBeReattachedAfterMovingAway)
{
	QmHudEditor::CVisibleBounds Bounds;
	const CUIRect Transform{0.0f, 50.0f, 232.0f, 250.0f};
	Bounds.Observe(Transform, Transform, Transform, {4.0f, 250.0f, 220.0f, 40.0f});
	const CUIRect Visible = Bounds.Resolve(Transform, Transform);
	const QmHudEditor::SAxisReference aReferences[] = {{14.0f, 40.0f}, {1156.0f, 40.0f}};

	for(bool Horizontal : {true, false})
	{
		SCOPED_TRACE(Horizontal);
		const float Size = Horizontal ? Visible.w : Visible.h;
		const float Offset = Horizontal ? Visible.x - Transform.x : Visible.y - Transform.y;
		const float ScreenStart = Horizontal ? 10.0f : 20.0f;
		const float ScreenSize = Horizontal ? 1200.0f : 900.0f;
		const float Moved = QmHudEditor::StoreAxisAnchor(ScreenStart + 100.0f, Size, ScreenStart, ScreenSize, Offset);
		EXPECT_NEAR(QmHudEditor::RestoreAxisAnchor(Moved, Size, ScreenStart, ScreenSize, Offset), ScreenStart + 100.0f, 0.001f);

		// 从中间位置重新向两侧拖过边界，保存后仍以实际可见边贴边而不是预估历史区。
		const auto Start = QmHudEditor::ResolveAxisSnapEx(ScreenStart - Offset - 10.0f, Size, ScreenStart, ScreenSize, aReferences, 2, Offset);
		const auto End = QmHudEditor::ResolveAxisSnapEx(ScreenStart + ScreenSize, Size, ScreenStart, ScreenSize, aReferences, 2, Offset);
		EXPECT_EQ(Start.m_GuideKind, QmHudEditor::ESnapGuideKind::ScreenStart);
		EXPECT_EQ(End.m_GuideKind, QmHudEditor::ESnapGuideKind::ScreenEnd);
		const float SavedStart = QmHudEditor::StoreAxisAnchor(Start.m_Position, Size, ScreenStart, ScreenSize, Offset);
		const float SavedEnd = QmHudEditor::StoreAxisAnchor(End.m_Position, Size, ScreenStart, ScreenSize, Offset);
		EXPECT_FLOAT_EQ(SavedStart, 0.0f);
		EXPECT_FLOAT_EQ(SavedEnd, 1.0f);
		for(float Scale : {0.25f, 1.0f, 4.0f})
		{
			SCOPED_TRACE(Scale);
			EXPECT_NEAR(QmHudEditor::RestoreAxisAnchor(SavedStart, Size * Scale, ScreenStart, ScreenSize, Offset * Scale) + Offset * Scale, ScreenStart, 0.001f);
			EXPECT_NEAR(QmHudEditor::RestoreAxisAnchor(SavedEnd, Size * Scale, ScreenStart, ScreenSize, Offset * Scale) + Offset * Scale + Size * Scale, ScreenStart + ScreenSize, 0.001f);
		}
	}
}

TEST(QmHudEditorVisibleBounds, ReopeningLearnsBoundsBeforeRestoringSavedEdge)
{
	const CUIRect Transform{0.0f, 50.0f, 232.0f, 250.0f};
	QmHudEditor::CVisibleBounds ReopenedBounds;
	EXPECT_FLOAT_EQ(ReopenedBounds.Resolve(Transform, Transform).h, 250.0f);
	ReopenedBounds.Observe(Transform, Transform, Transform, {4.0f, 250.0f, 220.0f, 40.0f});
	const CUIRect Visible = ReopenedBounds.Resolve(Transform, Transform);
	const float Anchor = QmHudEditor::RestoreAxisAnchor(1.0f, Visible.h, 0.0f, 300.0f, Visible.y - Transform.y);
	EXPECT_FLOAT_EQ(Anchor, 60.0f);
	EXPECT_FLOAT_EQ(Anchor + 200.0f + Visible.h, 300.0f);
}

TEST(QmHudEditorVisibleBounds, SourceOriginMoveKeepsRelativeContentButNewDeclarationInvalidatesIt)
{
	QmHudEditor::CVisibleBounds Bounds;
	const CUIRect Transform{0.0f, 50.0f, 100.0f, 80.0f};
	Bounds.Observe(Transform, Transform, Transform, {4.0f, 70.0f, 60.0f, 30.0f});
	const CUIRect Moved{30.0f, 100.0f, 100.0f, 80.0f};
	const CUIRect Visible = Bounds.Resolve(Moved, Moved);
	EXPECT_FLOAT_EQ(Visible.x, 34.0f);
	EXPECT_FLOAT_EQ(Visible.y, 120.0f);
	EXPECT_FLOAT_EQ(Visible.w, 60.0f);
	EXPECT_FLOAT_EQ(Visible.h, 30.0f);
	const CUIRect NewDeclaration{35.0f, 120.0f, 80.0f, 40.0f};
	EXPECT_FLOAT_EQ(Bounds.Resolve(Moved, NewDeclaration).w, 80.0f);
}

TEST(QmPlayerStateHudBounds, CommonLoadoutKeepsOnlyContentAndSmallPadding)
{
	CQmPlayerStateHudBounds Bounds;
	Bounds.AddWeapon(14.0f, 17.0f, vec2(19.2f, 14.4f));
	Bounds.AddWeapon(30.0f, 17.0f, vec2(14.310835f, 7.1554175f));
	Bounds.AddRect({5.0f, 29.0f, 24.0f, 12.0f});
	Bounds.AddRect({5.0f, 41.0f, 12.0f, 12.0f});
	const CUIRect Rect = Bounds.Rect();
	EXPECT_GT(Rect.x, 0.0f);
	EXPECT_GT(Rect.y, 0.0f);
	EXPECT_LT(Rect.w, 42.0f);
	EXPECT_LT(Rect.h, 55.0f);
	EXPECT_FLOAT_EQ(Rect.y + Rect.h, 55.0f);
}

TEST(QmPlayerStateHudBounds, RotatedWeaponIncludesAllFourCorners)
{
	CQmPlayerStateHudBounds Bounds;
	Bounds.AddWeapon(30.0f, 20.0f, vec2(20.0f, 10.0f));
	const CUIRect Rect = Bounds.Rect();
	EXPECT_NEAR(Rect.x, 17.393398f, 0.0001f);
	EXPECT_NEAR(Rect.y, 7.393398f, 0.0001f);
	EXPECT_NEAR(Rect.w, 25.213204f, 0.0001f);
	EXPECT_NEAR(Rect.h, 25.213204f, 0.0001f);
}

TEST(QmPlayerStateHudBounds, NinjaBarExtendsBounds)
{
	CQmPlayerStateHudBounds Bounds;
	Bounds.AddRect({5.0f, 29.0f, 24.0f, 12.0f});
	Bounds.AddRect({78.0f, 5.0f, 6.0f, 24.0f});
	const CUIRect Rect = Bounds.Rect();
	EXPECT_FLOAT_EQ(Rect.x, 3.0f);
	EXPECT_FLOAT_EQ(Rect.y, 3.0f);
	EXPECT_FLOAT_EQ(Rect.x + Rect.w, 86.0f);
	EXPECT_FLOAT_EQ(Rect.y + Rect.h, 43.0f);
}

TEST(QmPlayerStateHudBounds, LastStatusIconExtendsRowBounds)
{
	CQmPlayerStateHudBounds Bounds;
	Bounds.AddRect({5.0f, 65.0f, 12.0f, 12.0f});
	Bounds.AddRect({17.0f, 65.0f, 12.0f, 12.0f});
	const CUIRect Rect = Bounds.Rect();
	EXPECT_FLOAT_EQ(Rect.x, 3.0f);
	EXPECT_FLOAT_EQ(Rect.y, 63.0f);
	EXPECT_FLOAT_EQ(Rect.x + Rect.w, 31.0f);
	EXPECT_FLOAT_EQ(Rect.y + Rect.h, 79.0f);
}

TEST(QmPlayerStateHudBounds, EmptyContentAndZeroJumpRowHaveNoBounds)
{
	CQmPlayerStateHudBounds Bounds;
	Bounds.AddRect({5.0f, 29.0f, 0.0f, 12.0f});
	EXPECT_FLOAT_EQ(Bounds.Rect().w, 0.0f);
	EXPECT_FLOAT_EQ(Bounds.Rect().h, 0.0f);
}
