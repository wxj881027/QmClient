#include <game/client/components/hud_editor.h>
#include <game/client/components/qmclient/hud_notifications/hud_notifications.h>

#include <gtest/gtest.h>

TEST(QmHudNotifications, HudEdgeMarginHelperOffsetsAnchoredRects)
{
	const CUIRect Rect{10.0f, 20.0f, 100.0f, 40.0f};
	const QmHudEditor::SEdgeMargin Margin = QmHudEditor::SEdgeMargin::Uniform(8.0f);

	const CUIRect LeftTop = QmHudEditor::ApplyEdgeMargin(Rect, Margin, true, false, true, false);
	EXPECT_FLOAT_EQ(LeftTop.x, 18.0f);
	EXPECT_FLOAT_EQ(LeftTop.y, 28.0f);
	EXPECT_FLOAT_EQ(LeftTop.w, Rect.w);
	EXPECT_FLOAT_EQ(LeftTop.h, Rect.h);

	const CUIRect RightBottom = QmHudEditor::ApplyEdgeMargin(Rect, Margin, false, true, false, true);
	EXPECT_FLOAT_EQ(RightBottom.x, 2.0f);
	EXPECT_FLOAT_EQ(RightBottom.y, 12.0f);
	EXPECT_FLOAT_EQ(RightBottom.w, Rect.w);
	EXPECT_FLOAT_EQ(RightBottom.h, Rect.h);
}

TEST(QmHudNotifications, ChatEdgeBaseRectPreservesLegacyDefaultAndSupportsRightEdge)
{
	const CUIRect Legacy = QmHudEditor::ChatEdgeBaseRect(600.0f, 200.0f, 0.0f, false);
	EXPECT_FLOAT_EQ(Legacy.x, 0.0f);
	EXPECT_FLOAT_EQ(Legacy.y, 50.0f);
	EXPECT_FLOAT_EQ(Legacy.w, 232.0f);
	EXPECT_FLOAT_EQ(Legacy.h, 250.0f);

	const CUIRect Right = QmHudEditor::ChatEdgeBaseRect(600.0f, 200.0f, 8.0f, true);
	EXPECT_FLOAT_EQ(Right.x, 600.0f - 232.0f - 8.0f);
	EXPECT_FLOAT_EQ(Right.y, 50.0f);
	EXPECT_FLOAT_EQ(Right.w, 232.0f);
	EXPECT_FLOAT_EQ(Right.h, 250.0f);
}

TEST(QmHudNotificationsGeometry, ResolvesHorizontalFlowFromAnchorsAndScreenSide)
{
	EXPECT_EQ(QmHudNotifications::ResolveHorizontalFlow(true, false, 240.0f, 200.0f), QmHudNotifications::EHorizontalFlow::LeftToRight);
	EXPECT_EQ(QmHudNotifications::ResolveHorizontalFlow(false, true, 120.0f, 200.0f), QmHudNotifications::EHorizontalFlow::RightToLeft);
	EXPECT_EQ(QmHudNotifications::ResolveHorizontalFlow(false, false, 120.0f, 200.0f), QmHudNotifications::EHorizontalFlow::LeftToRight);
	EXPECT_EQ(QmHudNotifications::ResolveHorizontalFlow(false, false, 280.0f, 200.0f), QmHudNotifications::EHorizontalFlow::RightToLeft);
}

TEST(QmHudNotificationsGeometry, ComputesVisibleRectFromRealContentWidth)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};

	const CUIRect LeftVisible = QmHudNotifications::NotificationVisibleRect(BaseRect, 96.0f, 34.0f, QmHudNotifications::EHorizontalFlow::LeftToRight);
	EXPECT_FLOAT_EQ(LeftVisible.x, 100.0f);
	EXPECT_FLOAT_EQ(LeftVisible.y, 40.0f);
	EXPECT_FLOAT_EQ(LeftVisible.w, 96.0f);
	EXPECT_FLOAT_EQ(LeftVisible.h, 34.0f);

	const CUIRect RightVisible = QmHudNotifications::NotificationVisibleRect(BaseRect, 96.0f, 34.0f, QmHudNotifications::EHorizontalFlow::RightToLeft);
	EXPECT_FLOAT_EQ(RightVisible.x, 176.0f);
	EXPECT_FLOAT_EQ(RightVisible.y, 40.0f);
	EXPECT_FLOAT_EQ(RightVisible.w, 96.0f);
	EXPECT_FLOAT_EQ(RightVisible.h, 34.0f);
}

TEST(QmHudNotificationsGeometry, PlacesBoxesDifferentlyForLeftAndRightFlow)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};

	EXPECT_FLOAT_EQ(QmHudNotifications::NotificationBoxX(BaseRect, 60.0f, QmHudNotifications::EHorizontalFlow::LeftToRight, 0.0f), 100.0f);
	EXPECT_FLOAT_EQ(QmHudNotifications::NotificationBoxX(BaseRect, 60.0f, QmHudNotifications::EHorizontalFlow::RightToLeft, 0.0f), 212.0f);
	EXPECT_FLOAT_EQ(QmHudNotifications::NotificationBoxX(BaseRect, 60.0f, QmHudNotifications::EHorizontalFlow::LeftToRight, 12.0f), 88.0f);
	EXPECT_FLOAT_EQ(QmHudNotifications::NotificationBoxX(BaseRect, 60.0f, QmHudNotifications::EHorizontalFlow::RightToLeft, 12.0f), 224.0f);
}

TEST(QmHudNotificationsGeometry, ExpandsVisibleRectForSlideAnimation)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};

	const CUIRect LeftVisible = QmHudNotifications::NotificationVisibleRect(BaseRect, 96.0f, 34.0f, QmHudNotifications::EHorizontalFlow::LeftToRight, 32.0f);
	EXPECT_FLOAT_EQ(LeftVisible.x, 68.0f);
	EXPECT_FLOAT_EQ(LeftVisible.w, 128.0f);

	const CUIRect RightVisible = QmHudNotifications::NotificationVisibleRect(BaseRect, 96.0f, 34.0f, QmHudNotifications::EHorizontalFlow::RightToLeft, 32.0f);
	EXPECT_FLOAT_EQ(RightVisible.x, 176.0f);
	EXPECT_FLOAT_EQ(RightVisible.w, 128.0f);
}

TEST(QmHudNotificationsGeometry, EditorPreviewHeightUsesMaxVisibleStack)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};
	const float BoxHeight = 20.0f;
	const float Gap = 4.0f;

	const CUIRect PreviewRect = QmHudNotifications::EditorPreviewVisibleRect(
		BaseRect, 120.0f, BoxHeight, Gap, 4, QmHudNotifications::EHorizontalFlow::LeftToRight);

	EXPECT_FLOAT_EQ(PreviewRect.x, 100.0f);
	EXPECT_FLOAT_EQ(PreviewRect.w, 120.0f);
	EXPECT_FLOAT_EQ(PreviewRect.h, 92.0f);
}

TEST(QmHudNotificationsGeometry, EditorPreviewWidthStaysStableAcrossHorizontalFlows)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};

	const CUIRect LeftPreview = QmHudNotifications::EditorPreviewVisibleRect(
		BaseRect, 128.0f, 20.0f, 4.0f, 3, QmHudNotifications::EHorizontalFlow::LeftToRight);
	const CUIRect RightPreview = QmHudNotifications::EditorPreviewVisibleRect(
		BaseRect, 128.0f, 20.0f, 4.0f, 3, QmHudNotifications::EHorizontalFlow::RightToLeft);

	EXPECT_FLOAT_EQ(LeftPreview.w, RightPreview.w);
	EXPECT_FLOAT_EQ(LeftPreview.h, RightPreview.h);
	EXPECT_FLOAT_EQ(LeftPreview.x, 100.0f);
	EXPECT_FLOAT_EQ(RightPreview.x, 144.0f);
}

TEST(QmHudNotificationsGeometry, EditorPreviewDragRectDoesNotShiftAcrossHorizontalFlows)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};

	const CUIRect LeftPreview = QmHudNotifications::EditorPreviewDragRect(BaseRect, 128.0f, 20.0f, 4.0f, 3);
	const CUIRect RightPreview = QmHudNotifications::EditorPreviewDragRect(BaseRect, 128.0f, 20.0f, 4.0f, 3);

	EXPECT_FLOAT_EQ(LeftPreview.x, RightPreview.x);
	EXPECT_FLOAT_EQ(LeftPreview.x + LeftPreview.w * 0.5f, RightPreview.x + RightPreview.w * 0.5f);
}

TEST(QmHudNotificationsGeometry, EditorPreviewRenderKeepsNaturalWidthAnchoredToStableRightEdge)
{
	const CUIRect EditorRect = {100.0f, 40.0f, 128.0f, 92.0f};
	const CUIRect RenderBaseRect = QmHudNotifications::EditorPreviewRenderBaseRect(
		EditorRect, 172.0f, QmHudNotifications::EHorizontalFlow::RightToLeft);

	const float EditorBoxWidth = QmHudNotifications::NotificationBoxWidth(RenderBaseRect, 160.0f);
	const float BoxX = QmHudNotifications::NotificationBoxX(RenderBaseRect, EditorBoxWidth, QmHudNotifications::EHorizontalFlow::RightToLeft, 0.0f);

	EXPECT_FLOAT_EQ(EditorBoxWidth, 160.0f);
	EXPECT_FLOAT_EQ(BoxX + EditorBoxWidth, EditorRect.x + EditorRect.w);
}

TEST(QmHudNotificationsGeometry, RuntimeVisibleRectMayExpandWithoutChangingStableAnchor)
{
	const CUIRect AnchorRect = {100.0f, 40.0f, 128.0f, 92.0f};
	const CUIRect RenderBaseRect = QmHudNotifications::EditorPreviewRenderBaseRect(
		AnchorRect, 172.0f, QmHudNotifications::EHorizontalFlow::RightToLeft);
	const CUIRect RuntimeVisibleRect = QmHudNotifications::NotificationVisibleRect(
		RenderBaseRect, 96.0f, 34.0f, QmHudNotifications::EHorizontalFlow::RightToLeft, 32.0f);

	EXPECT_FLOAT_EQ(RuntimeVisibleRect.x, AnchorRect.x + AnchorRect.w - 96.0f);
	EXPECT_FLOAT_EQ(RenderBaseRect.x + RenderBaseRect.w, AnchorRect.x + AnchorRect.w);
	EXPECT_GT(RuntimeVisibleRect.w, 96.0f);
}

TEST(QmHudNotificationsGeometry, EdgeMarginInsetsOnlyAnchoredPreviewEdges)
{
	const CUIRect AnchorRect = {0.0f, 0.0f, 128.0f, 92.0f};
	const CUIRect FreeRect = {100.0f, 40.0f, 128.0f, 92.0f};
	const QmHudEditor::SEdgeMargin Margin = QmHudEditor::SEdgeMargin::Uniform(8.0f);

	const CUIRect LeftInset = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, true, false, true, false);
	const CUIRect RightInset = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, false, true, false, true);
	const CUIRect FreeInset = QmHudEditor::ApplyEdgeMargin(FreeRect, Margin, false, false, false, false);

	EXPECT_FLOAT_EQ(LeftInset.x, 8.0f);
	EXPECT_FLOAT_EQ(LeftInset.y, 8.0f);
	EXPECT_FLOAT_EQ(LeftInset.w, 128.0f);
	EXPECT_FLOAT_EQ(RightInset.x, -8.0f);
	EXPECT_FLOAT_EQ(RightInset.y, -8.0f);
	EXPECT_FLOAT_EQ(RightInset.w, 128.0f);
	EXPECT_FLOAT_EQ(FreeInset.x, FreeRect.x);
	EXPECT_FLOAT_EQ(FreeInset.y, FreeRect.y);
}

TEST(QmHudEditorGeometry, ApplyEdgeMarginMatchesLegacyNotificationInsetAcrossAnchors)
{
	const CUIRect AnchorRect = {0.0f, 0.0f, 128.0f, 92.0f};
	const QmHudEditor::SEdgeMargin Margin = QmHudEditor::SEdgeMargin::Uniform(8.0f);

	const CUIRect LeftTop = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, true, false, true, false);
	const CUIRect RightBottom = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, false, true, false, true);

	const CUIRect LegacyLeftTop = QmHudNotifications::InsetAnchoredRect(AnchorRect, 8.0f, true, false, true, false);
	const CUIRect LegacyRightBottom = QmHudNotifications::InsetAnchoredRect(AnchorRect, 8.0f, false, true, false, true);

	EXPECT_FLOAT_EQ(LeftTop.x, LegacyLeftTop.x);
	EXPECT_FLOAT_EQ(LeftTop.y, LegacyLeftTop.y);
	EXPECT_FLOAT_EQ(LeftTop.w, LegacyLeftTop.w);
	EXPECT_FLOAT_EQ(LeftTop.h, LegacyLeftTop.h);
	EXPECT_FLOAT_EQ(RightBottom.x, LegacyRightBottom.x);
	EXPECT_FLOAT_EQ(RightBottom.y, LegacyRightBottom.y);
}

TEST(QmHudEditorGeometry, ApplyEdgeMarginHandlesNonUniformMargins)
{
	const CUIRect AnchorRect = {0.0f, 0.0f, 64.0f, 32.0f};
	const QmHudEditor::SEdgeMargin Margin{4.0f, 8.0f, 2.0f, 16.0f};

	const CUIRect LeftTop = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, true, false, true, false);
	EXPECT_FLOAT_EQ(LeftTop.x, 4.0f);
	EXPECT_FLOAT_EQ(LeftTop.y, 2.0f);

	const CUIRect RightBottom = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, false, true, false, true);
	EXPECT_FLOAT_EQ(RightBottom.x, -8.0f);
	EXPECT_FLOAT_EQ(RightBottom.y, -16.0f);
}

TEST(QmHudEditorGeometry, ApplyEdgeMarginZeroIsIdentity)
{
	const CUIRect AnchorRect = {12.0f, 34.0f, 56.0f, 78.0f};
	const QmHudEditor::SEdgeMargin Zero{};

	const CUIRect LeftTop = QmHudEditor::ApplyEdgeMargin(AnchorRect, Zero, true, false, true, false);
	const CUIRect RightBottom = QmHudEditor::ApplyEdgeMargin(AnchorRect, Zero, false, true, false, true);
	const CUIRect Free = QmHudEditor::ApplyEdgeMargin(AnchorRect, Zero, false, false, false, false);

	EXPECT_FLOAT_EQ(LeftTop.x, AnchorRect.x);
	EXPECT_FLOAT_EQ(LeftTop.y, AnchorRect.y);
	EXPECT_FLOAT_EQ(RightBottom.x, AnchorRect.x);
	EXPECT_FLOAT_EQ(RightBottom.y, AnchorRect.y);
	EXPECT_FLOAT_EQ(Free.x, AnchorRect.x);
	EXPECT_FLOAT_EQ(Free.y, AnchorRect.y);
}

TEST(QmHudEditorGeometry, ApplyEdgeMarginIsZeroPredicate)
{
	EXPECT_TRUE((QmHudEditor::SEdgeMargin{}).IsZero());
	EXPECT_FALSE(QmHudEditor::SEdgeMargin::Uniform(1.0f).IsZero());
	EXPECT_FALSE((QmHudEditor::SEdgeMargin{0.0f, 0.0f, 0.0f, 0.5f}).IsZero());
}

TEST(QmHudEditorGeometry, ResolveHorizontalFlowFromVisibleRect)
{
	const float ScreenStartX = 0.0f;
	const float ScreenWidth = 400.0f;
	const CUIRect AnchoredLeft = {0.0f, 0.0f, 120.0f, 80.0f};
	const CUIRect AnchoredRight = {280.0f, 0.0f, 120.0f, 80.0f};
	const CUIRect CenterTilt = {100.0f, 0.0f, 80.0f, 60.0f};
	const CUIRect FarRightTilt = {260.0f, 0.0f, 60.0f, 60.0f};

	EXPECT_EQ(QmHudEditor::ResolveHorizontalFlow(AnchoredLeft, ScreenStartX, ScreenWidth), QmHudEditor::EHorizontalFlow::LeftToRight);
	EXPECT_EQ(QmHudEditor::ResolveHorizontalFlow(AnchoredRight, ScreenStartX, ScreenWidth), QmHudEditor::EHorizontalFlow::RightToLeft);
	EXPECT_EQ(QmHudEditor::ResolveHorizontalFlow(CenterTilt, ScreenStartX, ScreenWidth), QmHudEditor::EHorizontalFlow::LeftToRight);
	EXPECT_EQ(QmHudEditor::ResolveHorizontalFlow(FarRightTilt, ScreenStartX, ScreenWidth), QmHudEditor::EHorizontalFlow::RightToLeft);
}

TEST(QmHudNotificationsGeometry, EditorPreviewRightFlowBoxMatchesStableRightEdge)
{
	const CUIRect EditorRect = {100.0f, 40.0f, 128.0f, 92.0f};
	const float BoxWidth = QmHudNotifications::NotificationBoxWidth(EditorRect, 180.0f);
	const float BoxX = QmHudNotifications::NotificationBoxX(EditorRect, BoxWidth, QmHudNotifications::EHorizontalFlow::RightToLeft, 0.0f);

	EXPECT_FLOAT_EQ(BoxX, EditorRect.x);
	EXPECT_FLOAT_EQ(BoxX + BoxWidth, EditorRect.x + EditorRect.w);
}

TEST(QmHudEditorGeometry, SnapsOnlyToScreenEdges)
{
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenEdges(101.0f, 40.0f, 0.0f, 300.0f), 101.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenEdges(4.0f, 40.0f, 0.0f, 300.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenEdges(257.0f, 40.0f, 0.0f, 300.0f), 260.0f);
}

TEST(QmHudEditorGeometry, SnapsToScreenCenterGuide)
{
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenGuides(101.0f, 40.0f, 0.0f, 300.0f), 101.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenGuides(4.0f, 40.0f, 0.0f, 300.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenGuides(257.0f, 40.0f, 0.0f, 300.0f), 260.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenGuides(127.0f, 40.0f, 0.0f, 300.0f), 130.0f);
}

TEST(QmHudEditorGeometry, SnapsToOtherModuleAlignmentGuides)
{
	const QmHudEditor::SAxisReference aReferences[] = {
		{40.0f, 60.0f},
	};

	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToGuides(44.0f, 30.0f, 0.0f, 300.0f, aReferences, 1), 40.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToGuides(57.0f, 30.0f, 0.0f, 300.0f, aReferences, 1), 55.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToGuides(68.0f, 30.0f, 0.0f, 300.0f, aReferences, 1), 70.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToGuides(120.0f, 30.0f, 0.0f, 300.0f, aReferences, 1), 120.0f);
}

TEST(QmHudEditorGeometry, ReportsVisibleSnapGuidePosition)
{
	const QmHudEditor::SAxisReference aReferences[] = {
		{40.0f, 60.0f},
	};

	const QmHudEditor::SSnapAxisResult ScreenCenter = QmHudEditor::SnapAxisToGuidesEx(127.0f, 40.0f, 0.0f, 300.0f, nullptr, 0);
	EXPECT_TRUE(ScreenCenter.m_HasGuide);
	EXPECT_FLOAT_EQ(ScreenCenter.m_Position, 130.0f);
	EXPECT_FLOAT_EQ(ScreenCenter.m_GuidePosition, 150.0f);
	EXPECT_EQ(ScreenCenter.m_GuideKind, QmHudEditor::ESnapGuideKind::ScreenCenter);

	const QmHudEditor::SSnapAxisResult ReferenceEnd = QmHudEditor::SnapAxisToGuidesEx(68.0f, 30.0f, 0.0f, 300.0f, aReferences, 1);
	EXPECT_TRUE(ReferenceEnd.m_HasGuide);
	EXPECT_FLOAT_EQ(ReferenceEnd.m_Position, 70.0f);
	EXPECT_FLOAT_EQ(ReferenceEnd.m_GuidePosition, 100.0f);
	EXPECT_EQ(ReferenceEnd.m_GuideKind, QmHudEditor::ESnapGuideKind::ReferenceEnd);

	const QmHudEditor::SSnapAxisResult Free = QmHudEditor::SnapAxisToGuidesEx(120.0f, 30.0f, 0.0f, 300.0f, aReferences, 1);
	EXPECT_FALSE(Free.m_HasGuide);
	EXPECT_FLOAT_EQ(Free.m_Position, 120.0f);
}

TEST(QmHudEditorGeometry, MediaIslandUsesWeakerScreenEdgeSnapOnly)
{
	const QmHudEditor::SAxisReference aReferences[] = {
		{40.0f, 60.0f},
	};

	const QmHudEditor::SSnapAxisResult FreeNearEdge = QmHudEditor::SnapAxisToGuidesEx(3.0f, 40.0f, 0.0f, 300.0f, nullptr, 0, QmHudEditor::MEDIA_ISLAND_EDGE_SNAP_DISTANCE);
	EXPECT_FALSE(FreeNearEdge.m_HasGuide);
	EXPECT_FLOAT_EQ(FreeNearEdge.m_Position, 3.0f);

	const QmHudEditor::SSnapAxisResult SnappedEdge = QmHudEditor::SnapAxisToGuidesEx(2.0f, 40.0f, 0.0f, 300.0f, nullptr, 0, QmHudEditor::MEDIA_ISLAND_EDGE_SNAP_DISTANCE);
	EXPECT_TRUE(SnappedEdge.m_HasGuide);
	EXPECT_FLOAT_EQ(SnappedEdge.m_Position, 0.0f);
	EXPECT_EQ(SnappedEdge.m_GuideKind, QmHudEditor::ESnapGuideKind::ScreenStart);

	const QmHudEditor::SSnapAxisResult ScreenCenter = QmHudEditor::SnapAxisToGuidesEx(127.0f, 40.0f, 0.0f, 300.0f, nullptr, 0, QmHudEditor::MEDIA_ISLAND_EDGE_SNAP_DISTANCE);
	EXPECT_FLOAT_EQ(ScreenCenter.m_Position, 130.0f);
	EXPECT_EQ(ScreenCenter.m_GuideKind, QmHudEditor::ESnapGuideKind::ScreenCenter);

	const QmHudEditor::SSnapAxisResult ReferenceStart = QmHudEditor::SnapAxisToGuidesEx(44.0f, 30.0f, 0.0f, 300.0f, aReferences, 1, QmHudEditor::MEDIA_ISLAND_EDGE_SNAP_DISTANCE);
	EXPECT_FLOAT_EQ(ReferenceStart.m_Position, 40.0f);
	EXPECT_EQ(ReferenceStart.m_GuideKind, QmHudEditor::ESnapGuideKind::ReferenceStart);
}

TEST(QmHudEditorGeometry, HudNotificationsUsesStableLayoutToken)
{
	const char *pToken = QmHudEditor::ElementToken(EHudEditorElement::HudNotifications);

	EXPECT_STREQ(pToken, "hud_notifications");
	EXPECT_EQ(QmHudEditor::ElementFromToken(pToken), static_cast<int>(EHudEditorElement::HudNotifications));
	EXPECT_EQ(QmHudEditor::ElementFromToken(""), -1);
}

TEST(QmHudNotificationsGeometry, EditorPreviewCanAnchorToScreenEdges)
{
	const CUIRect LeftBaseRect = {8.0f, 40.0f, 172.0f, 68.0f};
	const CUIRect RightBaseRect = {120.0f, 40.0f, 172.0f, 68.0f};

	const CUIRect LeftPreview = QmHudNotifications::EditorPreviewVisibleRect(
		LeftBaseRect, 128.0f, 20.0f, 4.0f, 3, QmHudNotifications::EHorizontalFlow::LeftToRight);
	const CUIRect RightPreview = QmHudNotifications::EditorPreviewVisibleRect(
		RightBaseRect, 128.0f, 20.0f, 4.0f, 3, QmHudNotifications::EHorizontalFlow::RightToLeft);

	EXPECT_FLOAT_EQ(LeftPreview.x, 8.0f);
	EXPECT_FLOAT_EQ(RightPreview.x + RightPreview.w, 292.0f);
}
