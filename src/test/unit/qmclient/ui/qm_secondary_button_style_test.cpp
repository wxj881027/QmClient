#include <game/client/QmUi/UiButtonStyle.h>

#include <gtest/gtest.h>

TEST(QmSecondaryButtonStyle, HoverChangesVisibleFillAndBorderOnDarkAndLightBackgrounds)
{
	for(const ColorRGBA Backdrop : {ColorRGBA(0, 0, 0, 1), ColorRGBA(1, 1, 1, 1)})
	{
		for(const float Opacity : {0.0f, 0.2f, 1.0f})
		{
			SCOPED_TRACE(::testing::Message() << "background=" << Backdrop.r << " opacity=" << Opacity);
			const ColorRGBA Surface = Backdrop.WithAlpha(Opacity);
			const auto Idle = ResolveUiSecondaryButtonStyle(Surface, Backdrop, true, false, false);
			const auto Hover = ResolveUiSecondaryButtonStyle(Surface, Backdrop, true, true, false);
			EXPECT_EQ(Idle.m_Fill, Surface);
			EXPECT_GT(std::abs(CompositeUiSurface(Hover.m_Fill, Backdrop).r - CompositeUiSurface(Idle.m_Fill, Backdrop).r), 0.1f);
			EXPECT_NE(Hover.m_Border, Idle.m_Border);
			EXPECT_GT(Hover.m_Border.a, Idle.m_Border.a);
		}
	}
}

TEST(QmSecondaryButtonStyle, PressIsStrongerAndDraggingOutsideRestoresIdle)
{
	const ColorRGBA Backdrop(0, 0, 0, 1), Surface(0.1f, 0.2f, 0.3f, 0.4f);
	const auto Idle = ResolveUiSecondaryButtonStyle(Surface, Backdrop, true, false, false);
	const auto Hover = ResolveUiSecondaryButtonStyle(Surface, Backdrop, true, true, false);
	const auto Press = ResolveUiSecondaryButtonStyle(Surface, Backdrop, true, true, true);
	const auto DragOutside = ResolveUiSecondaryButtonStyle(Surface, Backdrop, true, false, true);
	EXPECT_GT(Press.m_Fill.a, Hover.m_Fill.a);
	EXPECT_GT(CompositeUiSurface(Press.m_Fill, Backdrop).r, CompositeUiSurface(Hover.m_Fill, Backdrop).r);
	EXPECT_EQ(DragOutside.m_Fill, Idle.m_Fill);
	EXPECT_EQ(DragOutside.m_Border, Idle.m_Border);
}

TEST(QmSecondaryButtonStyle, DisabledIgnoresHoverAndPress)
{
	const ColorRGBA Surface(0.2f, 0.3f, 0.4f, 0.2f), Backdrop(1, 1, 1, 1);
	const auto Idle = ResolveUiSecondaryButtonStyle(Surface, Backdrop, false, false, false);
	const auto DisabledPress = ResolveUiSecondaryButtonStyle(Surface, Backdrop, false, true, true);
	EXPECT_EQ(DisabledPress.m_Fill, Idle.m_Fill);
	EXPECT_EQ(DisabledPress.m_Border, Idle.m_Border);
}

TEST(QmSecondaryButtonStyle, ReleaseAndLeavingRecoverConfiguredSurfaceAcrossRepeatedInteractions)
{
	const ColorRGBA Surface(0.3f, 0.5f, 0.7f, 0.1f), Backdrop(0, 0, 0, 1);
	const auto Hover = ResolveUiSecondaryButtonStyle(Surface, Backdrop, true, true, false);
	for(int Interaction = 0; Interaction < 3; ++Interaction)
	{
		ResolveUiSecondaryButtonStyle(Surface, Backdrop, true, true, true);
		const auto Released = ResolveUiSecondaryButtonStyle(Surface, Backdrop, true, true, false);
		EXPECT_EQ(Released.m_Fill, Hover.m_Fill);
		const auto Left = ResolveUiSecondaryButtonStyle(Surface, Backdrop, true, false, false);
		EXPECT_EQ(Left.m_Fill, Surface);
		EXPECT_EQ(Left.m_Border, ui_token::color::BORDER_SUBTLE);
	}
}

TEST(QmButtonStyle, OrdinaryAndIconButtonsShareFeedbackAcrossLightDarkAndTransparentSurfaces)
{
	SUiTheme Theme{};
	Theme.m_Accent = ColorRGBA(0.2f, 0.6f, 0.8f, 1.0f);
	Theme.m_Selected = Theme.m_Accent.WithAlpha(0.22f);
	for(const ColorRGBA Backdrop : {ColorRGBA(0, 0, 0, 1), ColorRGBA(1, 1, 1, 1)})
	{
		for(float Opacity : {0.0f, 0.2f, 1.0f})
		{
			SCOPED_TRACE(::testing::Message() << "background=" << Backdrop.r << " opacity=" << Opacity);
			for(const SUiButtonState State : {SUiButtonState{}, SUiButtonState{true, true, false, false}, SUiButtonState{true, true, true, false}})
			{
				const auto Ordinary = ResolveUiButtonStyle(EUiButtonRole::SECONDARY, Backdrop.WithAlpha(Opacity), Backdrop, Theme, State);
				const auto Icon = ResolveUiButtonStyle(EUiButtonRole::ICON, Backdrop.WithAlpha(Opacity), Backdrop, Theme, State);
				EXPECT_EQ(Ordinary.m_Fill, Icon.m_Fill);
				EXPECT_EQ(Ordinary.m_Border, Icon.m_Border);
			}
		}
	}
}

TEST(QmButtonStyle, SelectionRemainsVisibleAfterPointerLeaves)
{
	SUiTheme Theme{};
	Theme.m_Selected = ColorRGBA(0.2f, 0.7f, 0.5f, 0.22f);
	const ColorRGBA Surface(0, 0, 0, 0), Backdrop(0, 0, 0, 1);
	const auto Idle = ResolveUiButtonStyle(EUiButtonRole::SECONDARY, Surface, Backdrop, Theme, {});
	const auto Selected = ResolveUiButtonStyle(EUiButtonRole::SECONDARY, Surface, Backdrop, Theme, {true, false, false, true});
	const auto DraggedOutside = ResolveUiButtonStyle(EUiButtonRole::SECONDARY, Surface, Backdrop, Theme, {true, false, true, true});
	EXPECT_NE(Selected.m_Fill, Idle.m_Fill);
	EXPECT_GT(Selected.m_Fill.a, 0.0f);
	EXPECT_EQ(DraggedOutside.m_Fill, Selected.m_Fill);
	EXPECT_EQ(DraggedOutside.m_Border, Selected.m_Border);
}

TEST(QmButtonStyle, DisabledSelectionIgnoresPointerFeedback)
{
	SUiTheme Theme{};
	Theme.m_Selected = ColorRGBA(0.2f, 0.7f, 0.5f, 0.22f);
	const ColorRGBA Surface(0.1f, 0.2f, 0.3f, 0.5f), Backdrop(0, 0, 0, 1);
	const auto Enabled = ResolveUiButtonStyle(EUiButtonRole::SECONDARY, Surface, Backdrop, Theme, {true, false, false, true});
	const auto Disabled = ResolveUiButtonStyle(EUiButtonRole::SECONDARY, Surface, Backdrop, Theme, {false, false, false, true});
	const auto DisabledPress = ResolveUiButtonStyle(EUiButtonRole::SECONDARY, Surface, Backdrop, Theme, {false, true, true, true});
	EXPECT_LT(Disabled.m_Fill.a, Enabled.m_Fill.a);
	EXPECT_GT(Disabled.m_Fill.a, 0.0f);
	EXPECT_EQ(DisabledPress.m_Fill, Disabled.m_Fill);
	EXPECT_EQ(DisabledPress.m_Border, Disabled.m_Border);
}

TEST(QmButtonStyle, PrimaryButtonUsesThemeAccentAndRecoversAfterHover)
{
	SUiTheme Theme{};
	Theme.m_Accent = ColorRGBA(0.8f, 0.3f, 0.1f, 1.0f);
	const ColorRGBA Backdrop(0, 0, 0, 1);
	const auto Idle = ResolveUiButtonStyle(EUiButtonRole::PRIMARY, ColorRGBA(), Backdrop, Theme, {});
	const auto Hover = ResolveUiButtonStyle(EUiButtonRole::PRIMARY, ColorRGBA(), Backdrop, Theme, {true, true, false, false});
	const auto Disabled = ResolveUiButtonStyle(EUiButtonRole::PRIMARY, ColorRGBA(), Backdrop, Theme, {false, true, true, false});
	const auto Recovered = ResolveUiButtonStyle(EUiButtonRole::PRIMARY, ColorRGBA(), Backdrop, Theme, {});
	EXPECT_FLOAT_EQ(Idle.m_Fill.r, Theme.m_Accent.r);
	EXPECT_FLOAT_EQ(Idle.m_Fill.g, Theme.m_Accent.g);
	EXPECT_FLOAT_EQ(Idle.m_Fill.b, Theme.m_Accent.b);
	EXPECT_GT(Hover.m_Fill.a, Idle.m_Fill.a);
	EXPECT_LT(Disabled.m_Fill.a, Idle.m_Fill.a);
	EXPECT_EQ(Recovered.m_Fill, Idle.m_Fill);
}

TEST(QmButtonStyle, ExplicitSemanticSurfaceIsPreservedWithoutPointerFeedback)
{
	const ColorRGBA Semantic(0.8f, 0.1f, 0.2f, 0.3f);
	const auto Style = ResolveUiButtonStyle(EUiButtonRole::SECONDARY, Semantic, ColorRGBA(0, 0, 0, 1), SUiTheme{}, {});
	EXPECT_EQ(Style.m_Fill, Semantic);
}

TEST(QmButtonStyle, InvisibleSurfacesKeepFiniteColorChannels)
{
	const ColorRGBA Surface(0.3f, 0.5f, 0.7f, 0.0f);
	const auto Blended = BlendUiButtonSurface(Surface, ColorRGBA(1, 1, 1, 0));
	EXPECT_FLOAT_EQ(Blended.a, 0.0f);
	EXPECT_TRUE(std::isfinite(Blended.r));
	EXPECT_TRUE(std::isfinite(Blended.g));
	EXPECT_TRUE(std::isfinite(Blended.b));
}

TEST(QmButtonStyle, DisabledListEntryIgnoresHoverAndPress)
{
	const SUiTheme Theme{};
	const ColorRGBA Backdrop(0, 0, 0, 1);
	const auto Disabled = ResolveUiButtonStyle(EUiButtonRole::LIST_ENTRY, ColorRGBA(), Backdrop, Theme, {false, false, false, false});
	const auto DisabledPress = ResolveUiButtonStyle(EUiButtonRole::LIST_ENTRY, ColorRGBA(), Backdrop, Theme, {false, true, true, false});
	EXPECT_EQ(DisabledPress.m_Fill, Disabled.m_Fill);
	EXPECT_EQ(DisabledPress.m_Border, Disabled.m_Border);
}
