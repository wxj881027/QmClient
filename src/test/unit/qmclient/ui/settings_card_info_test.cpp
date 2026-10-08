#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsCardInfo.h>
#include <game/client/QmUi/SettingsCardWidth.h>
#include <game/client/QmUi/SettingsIconFeedback.h>

#include <engine/shared/config.h>

#include <gtest/gtest.h>

class CSettingsHeaderIconFeedbackTest : public ::testing::Test
{
protected:
	int m_PreviousMotionLevel;
	void SetUp() override
	{
		m_PreviousMotionLevel = g_Config.m_QmUiMotionLevel;
		g_Config.m_QmUiMotionLevel = 2;
	}
	void TearDown() override { g_Config.m_QmUiMotionLevel = m_PreviousMotionLevel; }
};

TEST_F(CSettingsHeaderIconFeedbackTest, SustainedHoverFinishesOneBounce)
{
	CQmAnimationBackend Anim;
	EXPECT_GT(ResolveSettingsIconScale(Anim, 1, true, false, true), 1.0f);
	for(int Frame = 0; Frame < 20; ++Frame)
	{
		Anim.Advance(1.0f / 60.0f);
		ResolveSettingsIconScale(Anim, 1, true, false, true);
	}
	EXPECT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, true, false, true), 1.0f);
	EXPECT_FALSE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
	EXPECT_LT(ResolveSettingsIconScale(Anim, 1, true, true, true), 1.0f);
}

TEST_F(CSettingsHeaderIconFeedbackTest, DisabledButtonCancelsBounceAndDoesNotReact)
{
	CQmAnimationBackend Anim;
	ResolveSettingsIconScale(Anim, 1, true, false, true);
	EXPECT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, true, true, false), 1.0f);
	EXPECT_FALSE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
	EXPECT_GT(ResolveSettingsIconScale(Anim, 1, true, false, true), 1.0f);
}

TEST_F(CSettingsHeaderIconFeedbackTest, MotionOffKeepsIconAtItsOriginalSize)
{
	g_Config.m_QmUiMotionLevel = 0;
	CQmAnimationBackend Anim;
	EXPECT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, true, false, true), 1.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsIconScale(Anim, 1, true, true, true), 1.0f);
	EXPECT_FALSE(Anim.HasActiveAnimation(1, EUiAnimProperty::SCALE));
}

TEST(SettingsCardInfoLayout, LeadingActionStaysLeftOfInfoWidthAndCollapse)
{
	for(float Scale : {1.0f, 1.5f, 2.0f})
	{
		SCOPED_TRACE(Scale);
		const SSettingsCardFrame Frame = ResolveSettingsCardHeaderActions(
			BuildSettingsCardFrame({10, 20, 320 * Scale, 200}, {"search", "Long title", "Description", "Help"}, 80, Scale),
			60 * Scale, true);
		const CUIRect Info = ResolveSettingsCardInfoRect(Frame);
		const CUIRect Width = SettingsCardWidthButtonRect(Frame);
		const CUIRect Locate = Frame.m_LeadingHeaderActionRect;
		EXPECT_LE(Frame.m_TitleRect.x + Frame.m_TitleRect.w, Locate.x);
		EXPECT_LE(Locate.x + Locate.w, Info.x);
		EXPECT_LE(Info.x + Info.w, Width.x);
		EXPECT_LE(Width.x + Width.w, Frame.m_HandleRect.x);
		EXPECT_FLOAT_EQ(Locate.y, Frame.m_HandleRect.y);
		EXPECT_FLOAT_EQ(Locate.h, Frame.m_HandleRect.h);
		EXPECT_FLOAT_EQ(Frame.m_SubtitleRect.w, Frame.m_TitleRect.w);
	}
}

TEST(SettingsCardInfoLayout, VeryNarrowHeaderNeverPlacesActionsOutsideItsLeftEdge)
{
	const SSettingsCardFrame Frame = ResolveSettingsCardHeaderActions(
		BuildSettingsCardFrame({10, 20, 40, 100}, {"search", "Title", nullptr, "Help"}, 0, 1), 200, true);
	for(const CUIRect &Rect : {Frame.m_LeadingHeaderActionRect, ResolveSettingsCardInfoRect(Frame), SettingsCardWidthButtonRect(Frame)})
	{
		EXPECT_GE(Rect.x, Frame.m_HeaderRect.x);
		EXPECT_GE(Rect.w, 0.0f);
		EXPECT_GE(Rect.h, 0.0f);
		EXPECT_LE(Rect.x + Rect.w, Frame.m_HandleRect.x);
	}
	EXPECT_GE(Frame.m_TitleRect.w, 0.0f);
}

TEST(SettingsCardInfoLayout, NarrowCardUsesIconWidthAndRestoresTextWidthWhenExpanded)
{
	const SSettingsCardSpec Spec{"search", "Title", "Description", "Help"};
	const auto Narrow = ResolveSettingsCardHeaderActions(BuildSettingsCardFrame({0, 0, 170, 100}, Spec, 0, 1), 70, true);
	const auto Wide = ResolveSettingsCardHeaderActions(BuildSettingsCardFrame({0, 0, 400, 100}, Spec, 0, 1), 70, true);
	EXPECT_FLOAT_EQ(Narrow.m_LeadingHeaderActionRect.w, Narrow.m_HandleRect.w);
	EXPECT_GT(Narrow.m_TitleRect.w, 0.0f);
	EXPECT_FLOAT_EQ(Wide.m_LeadingHeaderActionRect.w, 70);
}

TEST(SettingsCardInfoLayout, LeadingActionMovesWithAnimatedCardGeometry)
{
	const SSettingsCardFrame Frame = ResolveSettingsCardHeaderActions(
		BuildSettingsCardFrame({10, 20, 400, 200}, {"search", "Title", nullptr}, 80, 1), 60, false);
	const SSettingsCardFrame Moved = ResolveSettingsCardDrawFrame(Frame, 12.5f, -7.0f);
	EXPECT_FLOAT_EQ(Moved.m_LeadingHeaderActionRect.x, Frame.m_LeadingHeaderActionRect.x + 12.5f);
	EXPECT_FLOAT_EQ(Moved.m_LeadingHeaderActionRect.y, Frame.m_LeadingHeaderActionRect.y - 7.0f);
	EXPECT_FLOAT_EQ(Moved.m_LeadingHeaderActionRect.w, Frame.m_LeadingHeaderActionRect.w);
	EXPECT_LE(Moved.m_LeadingHeaderActionRect.x + Moved.m_LeadingHeaderActionRect.w, SettingsCardWidthButtonRect(Moved).x);
}

TEST(SettingsCardInfoLayout, SharesSizeAndBaselineWithWidthAndCollapseActions)
{
	const SSettingsCardFrame Frame = BuildSettingsCardFrame({10, 20, 400, 200}, {"font", "Font", "Description"}, 80, 1.25f);
	const CUIRect Info = ResolveSettingsCardInfoRect(Frame);
	const CUIRect Width = SettingsCardWidthButtonRect(Frame);
	EXPECT_FLOAT_EQ(Info.w, Frame.m_HandleRect.w);
	EXPECT_FLOAT_EQ(Info.h, Frame.m_HandleRect.h);
	EXPECT_FLOAT_EQ(Info.y, Width.y);
	EXPECT_FLOAT_EQ(Info.y, Frame.m_HandleRect.y);
	EXPECT_LE(Info.x + Info.w, Width.x);
	EXPECT_GE(Info.x, Frame.m_HeaderRect.x);
}

TEST(SettingsCardInfoLayout, NarrowHeaderKeepsInformationInsideHeaderWithoutNegativeSize)
{
	const SSettingsCardFrame Frame = BuildSettingsCardFrame({10, 20, 65, 100}, {"font", "Font", nullptr}, 0, 1.0f);
	const CUIRect Info = ResolveSettingsCardInfoRect(Frame);
	EXPECT_GE(Info.w, 0.0f);
	EXPECT_GE(Info.h, 0.0f);
	EXPECT_GE(Info.x, Frame.m_HeaderRect.x);
	EXPECT_LE(Info.x + Info.w, Frame.m_HandleRect.x);
}

TEST(SettingsCardInfoLayout, ExplanationWidthRemainsReadableOnWideScreenAndFitsNarrowViewport)
{
	EXPECT_FLOAT_EQ(ResolveSettingsCardInfoWidth(1920, 1), 300);
	EXPECT_FLOAT_EQ(ResolveSettingsCardInfoWidth(1920, 1.5f), 450);
	EXPECT_LT(ResolveSettingsCardInfoWidth(200, 1.5f), 200);
	EXPECT_GT(ResolveSettingsCardInfoWidth(0, 0), 0);
}

namespace
{
	struct SCardIconTextObserver
	{
		ColorRGBA m_Color{0.2f, 0.3f, 0.4f, 0.5f};
		ColorRGBA m_Outline{0.1f, 0.2f, 0.3f, 0.4f};
		ColorRGBA m_Selection{0.6f, 0.7f, 0.8f, 0.9f};
		EFontPreset m_Preset = EFontPreset::DEFAULT_FONT;
		unsigned m_Flags = TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT;
		ColorRGBA GetTextColor() const { return m_Color; }
		ColorRGBA GetTextOutlineColor() const { return m_Outline; }
		ColorRGBA GetTextSelectionColor() const { return m_Selection; }
		EFontPreset GetFontPreset() const { return m_Preset; }
		unsigned GetRenderFlags() const { return m_Flags; }
		void TextColor(ColorRGBA Value) { m_Color = Value; }
		void TextOutlineColor(ColorRGBA Value) { m_Outline = Value; }
		void TextSelectionColor(ColorRGBA Value) { m_Selection = Value; }
		void SetFontPreset(EFontPreset Value) { m_Preset = Value; }
		void SetRenderFlags(unsigned Value) { m_Flags = Value; }
	};
}

TEST(SettingsCardHeaderIcon, UsesBundledIconPresetAndRestoresAllCallerStateAfterDrawing)
{
	SCardIconTextObserver Render;
	const SCardIconTextObserver Before = Render;
	const ColorRGBA IconColor(0.9f, 0.5f, 0.1f, 0.75f);
	int Draws = 0;
	ExecuteSettingsCardHeaderIcon(Render, IconColor, [&]() {
		++Draws;
		EXPECT_EQ(Render.m_Preset, EFontPreset::ICON_FONT_BOLD);
		EXPECT_EQ(Render.m_Color, IconColor);
		EXPECT_NE(Render.m_Flags & TEXT_RENDER_FLAG_NO_OVERSIZE, 0u);
		Render.TextOutlineColor(ColorRGBA(1, 1, 1, 1));
		Render.TextSelectionColor(ColorRGBA(0, 0, 0, 0));
	});
	EXPECT_EQ(Draws, 1);
	EXPECT_EQ(Render.m_Color, Before.m_Color);
	EXPECT_EQ(Render.m_Outline, Before.m_Outline);
	EXPECT_EQ(Render.m_Selection, Before.m_Selection);
	EXPECT_EQ(Render.m_Preset, Before.m_Preset);
	EXPECT_EQ(Render.m_Flags, Before.m_Flags);
}

TEST(SettingsCardHeaderIcon, NestedDrawingRestoresOuterIconBeforeReturningToBodyText)
{
	SCardIconTextObserver Render;
	const SCardIconTextObserver Before = Render;
	const ColorRGBA Outer(1, 0, 0, 0.7f), Inner(0, 1, 0, 0.4f);
	ExecuteSettingsCardHeaderIcon(Render, Outer, [&]() {
		ExecuteSettingsCardHeaderIcon(Render, Inner, [&]() { EXPECT_EQ(Render.m_Color, Inner); });
		EXPECT_EQ(Render.m_Color, Outer);
		EXPECT_EQ(Render.m_Preset, EFontPreset::ICON_FONT_BOLD);
	});
	EXPECT_EQ(Render.m_Color, Before.m_Color);
	EXPECT_EQ(Render.m_Preset, Before.m_Preset);
}

TEST(SettingsCardInfoLayout, TitleOnlyCardKeepsSameDimensionsAsOtherHeaderActions)
{
	const SSettingsCardFrame Frame = BuildSettingsCardFrame({0, 0, 400, 150}, {"example", "Title", nullptr}, 0, 1.5f);
	const CUIRect Info = ResolveSettingsCardInfoRect(Frame);
	EXPECT_FLOAT_EQ(Info.w, Frame.m_HandleRect.w);
	EXPECT_FLOAT_EQ(Info.h, Frame.m_HandleRect.h);
	EXPECT_FLOAT_EQ(Info.y, Frame.m_HandleRect.y);
}

TEST(SettingsCardHeaderLabel, ZeroWidthOrHeightDoesNotSubmitTextAndVisibleAreaRecovers)
{
	int Draws = 0;
	for(const CUIRect Rect : {CUIRect{0, 0, 0, 20}, CUIRect{0, 0, -1, 20}, CUIRect{0, 0, 20, 0}})
		ExecuteSettingsCardLabel(Rect, [&]() { ++Draws; });
	EXPECT_EQ(Draws, 0);
	ExecuteSettingsCardLabel(CUIRect{0, 0, 20, 20}, [&]() { ++Draws; });
	EXPECT_EQ(Draws, 1);
}
