#include <game/client/components/hud_editor.h>

#include <gtest/gtest.h>

TEST(QmHudMeasuredGeometry, ReportsLocalContentOffsetAndSizeAfterRendering)
{
	QmHudEditor::CMeasuredVisibleRect Geometry;
	const CUIRect Transform{0.0f, 50.0f, 190.0f, 250.0f};
	// 绘制空间已经放大两倍并平移，可见内容仍须还原到原始外框的局部坐标。
	Geometry.Observe(Transform, {400.0f, 100.0f, 380.0f, 500.0f}, {410.0f, 520.0f, 370.0f, 70.0f});
	const CUIRect Visible = Geometry.Resolve(Transform, Transform);
	EXPECT_NEAR(Visible.x, 5.0f, 0.001f);
	EXPECT_NEAR(Visible.y, 260.0f, 0.001f);
	EXPECT_NEAR(Visible.w, 185.0f, 0.001f);
	EXPECT_NEAR(Visible.h, 35.0f, 0.001f);
}

TEST(QmHudMeasuredGeometry, KeepsMeasuredContentAtAllFourEdgesAfterMovingAway)
{
	const CUIRect Transform{0.0f, 0.0f, 200.0f, 240.0f};
	for(float Scale : {0.75f, 1.0f, 2.0f})
	{
		SCOPED_TRACE(Scale);
		QmHudEditor::CMeasuredVisibleRect Geometry;
		Geometry.Observe(Transform, {100.0f, 120.0f, 200.0f * Scale, 240.0f * Scale},
			{100.0f + 5.0f * Scale, 120.0f + 180.0f * Scale, 180.0f * Scale, 40.0f * Scale});
		const CUIRect Visible = Geometry.Resolve(Transform, Transform);
		const float aOffsets[] = {Visible.x * Scale, Visible.y * Scale};
		const float aSizes[] = {Visible.w * Scale, Visible.h * Scale};
		for(int Axis = 0; Axis < 2; ++Axis)
		{
			SCOPED_TRACE(Axis);
			const float Offset = aOffsets[Axis];
			const float Size = aSizes[Axis];
			const float Away = QmHudEditor::ResolveAxisSnapEx(250.0f, Size, 20.0f, 1000.0f, nullptr, 0, Offset).m_Position;
			EXPECT_GT(Away + Offset, 20.0f);
			EXPECT_LT(Away + Offset + Size, 1020.0f);
			for(float Position : {-500.0f, 1600.0f})
			{
				SCOPED_TRACE(Position);
				const auto Snapped = QmHudEditor::ResolveAxisSnapEx(Position, Size, 20.0f, 1000.0f, nullptr, 0, Offset);
				ASSERT_TRUE(Snapped.m_HasGuide);
				const float Saved = round_to_int(QmHudEditor::StoreAxisAnchor(Snapped.m_Position, Size, 20.0f, 1000.0f, Offset) * 10000.0f) / 10000.0f;
				const float Restored = QmHudEditor::RestoreAxisAnchor(Saved, Size, 20.0f, 1000.0f, Offset);
				if(Position < 0.0f)
					EXPECT_NEAR(Restored + Offset, 20.0f, 0.001f);
				else
					EXPECT_NEAR(Restored + Offset + Size, 1020.0f, 0.001f);
			}
		}
	}
}

TEST(QmHudMeasuredGeometry, SavesInteriorPositionWhenContentOffsetMovesAnchorOutsideScreen)
{
	for(const float VisibleTop : {100.0f, 210.0f, 210.01f})
	{
		SCOPED_TRACE(VisibleTop);
		const float Offset = 210.0f;
		const float Anchor = VisibleTop - Offset;
		const float Stored = QmHudEditor::StoreAxisAnchor(Anchor, 40.0f, 0.0f, 300.0f, Offset);
		const float Saved = round_to_int(Stored * 10000.0f) / 10000.0f;
		const float Reloaded = QmHudEditor::ClampStoredAxisPosition(Saved);
		const float Restored = QmHudEditor::RestoreAxisAnchor(Reloaded, 40.0f, 0.0f, 300.0f, Offset);
		EXPECT_NEAR(Restored + Offset, VisibleTop, 0.031f);
		EXPECT_NEAR(QmHudEditor::StoreAxisAnchor(Restored, 40.0f, 0.0f, 300.0f, Offset), Saved, 0.0001f);
		// 同一配置在投影到另一分辨率后仍恢复同一比例位置。
		EXPECT_NEAR(QmHudEditor::RestoreAxisAnchor(Reloaded, 80.0f, 20.0f, 600.0f, 420.0f) + 420.0f,
			20.0f + 2.0f * VisibleTop, 0.062f);
	}
}

TEST(QmHudMeasuredGeometry, SavesInteriorPositionWhenContentOffsetMovesAnchorBeyondEnd)
{
	const float Stored = QmHudEditor::StoreAxisAnchor(350.0f, 40.0f, 0.0f, 300.0f, -200.0f);
	const float Saved = round_to_int(Stored * 10000.0f) / 10000.0f;
	EXPECT_FLOAT_EQ(QmHudEditor::RestoreAxisAnchor(Saved, 40.0f, 0.0f, 300.0f, -200.0f), 350.0f);
}

TEST(QmHudMeasuredGeometry, PreservesLegacyPositiveLayoutCoordinates)
{
	EXPECT_FLOAT_EQ(QmHudEditor::RestoreAxisAnchor(0.25f, 40.0f, 20.0f, 300.0f, 10.0f), 95.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::StoreAxisAnchor(95.0f, 40.0f, 20.0f, 300.0f, 10.0f), 0.25f);
}

TEST(QmHudMeasuredGeometry, UsesFreshExplicitBoundsInsteadOfPreviousMeasurement)
{
	QmHudEditor::CMeasuredVisibleRect Geometry;
	const CUIRect Transform{0.0f, 0.0f, 100.0f, 80.0f};
	Geometry.Observe(Transform, Transform, {4.0f, 10.0f, 60.0f, 20.0f});
	const CUIRect Fresh{6.0f, 12.0f, 70.0f, 30.0f};
	const CUIRect Visible = Geometry.Resolve(Transform, Fresh);
	EXPECT_FLOAT_EQ(Visible.x, Fresh.x);
	EXPECT_FLOAT_EQ(Visible.y, Fresh.y);
	EXPECT_FLOAT_EQ(Visible.w, Fresh.w);
	EXPECT_FLOAT_EQ(Visible.h, Fresh.h);
}

TEST(QmHudMeasuredGeometry, FallsBackAfterTransformSizeChangesOrRuntimeReset)
{
	QmHudEditor::CMeasuredVisibleRect Geometry;
	const CUIRect Transform{0.0f, 0.0f, 100.0f, 80.0f};
	Geometry.Observe(Transform, Transform, {4.0f, 10.0f, 60.0f, 20.0f});
	const CUIRect Resized{0.0f, 0.0f, 200.0f, 80.0f};
	EXPECT_FLOAT_EQ(Geometry.Resolve(Resized, Resized).w, 200.0f);
	Geometry = {};
	const CUIRect Visible = Geometry.Resolve(Transform, Transform);
	EXPECT_FLOAT_EQ(Visible.x, Transform.x);
	EXPECT_FLOAT_EQ(Visible.y, Transform.y);
	EXPECT_FLOAT_EQ(Visible.w, Transform.w);
	EXPECT_FLOAT_EQ(Visible.h, Transform.h);
}

TEST(QmHudMeasuredGeometry, InvalidMeasurementCannotReplaceUsableBounds)
{
	QmHudEditor::CMeasuredVisibleRect Geometry;
	const CUIRect Transform{0.0f, 0.0f, 100.0f, 80.0f};
	Geometry.Observe(Transform, Transform, {4.0f, 10.0f, 60.0f, 20.0f});
	Geometry.Observe(Transform, {}, {0.0f, 0.0f, 50.0f, 20.0f});
	EXPECT_FLOAT_EQ(Geometry.Resolve(Transform, Transform).w, 60.0f);
}
