#include <game/client/components/hud_editor.h>

#include <gtest/gtest.h>

TEST(QmHudVisibleFrame, RegistrationOwnsTheElementGeometry)
{
	QmHudEditor::SVisibleElement Element;
	Element.m_Element = EHudEditorElement::LegacyMediaInfo;
	Element.m_Rect = {1, 2, 30, 40};
	Element.m_TransformRect = {5, 6, 70, 80};
	Element.m_DeclaredVisibleRect = {9, 10, 11, 12};
	Element.m_TargetUiRect = {13, 14, 15, 16};
	Element.m_BaseWidth = 17;
	Element.m_BaseHeight = 18;
	Element.m_StateOffsetX = 19;
	Element.m_StateOffsetY = 20;
	Element.m_HorizontalPlacement = QmHudEditor::EHorizontalPlacement::TransformOrigin;
	Element.m_Scalable = false;
	QmHudEditor::CVisibleFrame Frame;
	ASSERT_TRUE(Frame.Register(Element));
	Element.m_Rect.x = 999;
	const auto &Registered = Frame.Elements()[0];
	EXPECT_EQ(Registered.m_Element, EHudEditorElement::LegacyMediaInfo);
	EXPECT_FLOAT_EQ(Registered.m_Rect.x, 1);
	EXPECT_FLOAT_EQ(Registered.m_Rect.h, 40);
	EXPECT_FLOAT_EQ(Registered.m_TransformRect.y, 6);
	EXPECT_FLOAT_EQ(Registered.m_TransformRect.w, 70);
	EXPECT_FLOAT_EQ(Registered.m_DeclaredVisibleRect.x, 9);
	EXPECT_FLOAT_EQ(Registered.m_DeclaredVisibleRect.h, 12);
	EXPECT_FLOAT_EQ(Registered.m_TargetUiRect.y, 14);
	EXPECT_FLOAT_EQ(Registered.m_TargetUiRect.w, 15);
	EXPECT_FLOAT_EQ(Registered.m_BaseWidth, 17);
	EXPECT_FLOAT_EQ(Registered.m_BaseHeight, 18);
	EXPECT_FLOAT_EQ(Registered.m_StateOffsetX, 19);
	EXPECT_FLOAT_EQ(Registered.m_StateOffsetY, 20);
	EXPECT_EQ(Registered.m_HorizontalPlacement, QmHudEditor::EHorizontalPlacement::TransformOrigin);
	EXPECT_FALSE(Registered.m_Scalable);
	EXPECT_TRUE(Frame.Complete());
}

TEST(QmHudVisibleFrame, RepeatedElementsKeepTheirOriginalRegistrationOrder)
{
	QmHudEditor::CVisibleFrame Frame;
	QmHudEditor::SVisibleElement Element;
	Element.m_Rect.x = 1;
	ASSERT_TRUE(Frame.Register(Element));
	Element.m_Rect.x = 2;
	ASSERT_TRUE(Frame.Register(Element));
	ASSERT_EQ(Frame.Elements().size(), 2u);
	EXPECT_FLOAT_EQ(Frame.Elements()[0].m_Rect.x, 1);
	EXPECT_FLOAT_EQ(Frame.Elements()[1].m_Rect.x, 2);
}

TEST(QmHudVisibleFrame, MutableReportsRemainVisibleThroughTheReadOnlyFrameView)
{
	QmHudEditor::CVisibleFrame Frame;
	ASSERT_TRUE(Frame.Register({}));
	Frame.Elements()[0].m_Rect = {10, 20, 100, 30};
	Frame.Elements()[0].m_StateOffsetY = 7;
	const auto &ReadOnly = Frame;
	EXPECT_FLOAT_EQ(ReadOnly.Elements()[0].m_Rect.y, 20);
	EXPECT_FLOAT_EQ(ReadOnly.Elements()[0].m_Rect.h, 30);
	EXPECT_FLOAT_EQ(ReadOnly.Elements()[0].m_StateOffsetY, 7);
}

TEST(QmHudVisibleFrame, NextFrameDiscardsPreviousInteractionElements)
{
	QmHudEditor::CVisibleFrame Frame;
	ASSERT_TRUE(Frame.Register({}));
	Frame.BeginFrame();
	EXPECT_TRUE(Frame.Complete());
	EXPECT_TRUE(Frame.Elements().empty());
	QmHudEditor::SVisibleElement NewElement;
	NewElement.m_Element = EHudEditorElement::Lyrics;
	ASSERT_TRUE(Frame.Register(NewElement));
	ASSERT_EQ(Frame.Elements().size(), 1u);
	EXPECT_EQ(Frame.Elements()[0].m_Element, EHudEditorElement::Lyrics);
}
