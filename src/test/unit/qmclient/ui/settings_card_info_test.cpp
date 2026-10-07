#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsCardInfo.h>
#include <game/client/QmUi/SettingsCardWidth.h>

#include <gtest/gtest.h>

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
