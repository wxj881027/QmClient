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

TEST(QmButtonStyle, SelectedBorderInvertsHoverAndRestoresWhenPointerLeaves)
{
	SUiTheme Theme{};
	Theme.m_Selected = ColorRGBA(0.2f, 0.7f, 0.5f, 0.22f);
	for(const ColorRGBA Backdrop : {ColorRGBA(0, 0, 0, 1), ColorRGBA(1, 1, 1, 1)})
	{
		for(const auto Role : {EUiButtonRole::SECONDARY, EUiButtonRole::ICON})
		{
			const ColorRGBA Surface(0.1f, 0.2f, 0.3f, 0.4f);
			const auto Selected = ResolveUiButtonStyle(Role, Surface, Backdrop, Theme, {true, false, false, true});
			const auto Hovered = ResolveUiButtonStyle(Role, Surface, Backdrop, Theme, {true, true, false, true});
			const auto Pressed = ResolveUiButtonStyle(Role, Surface, Backdrop, Theme, {true, true, true, true});
			const auto Left = ResolveUiButtonStyle(Role, Surface, Backdrop, Theme, {true, false, true, true});
			EXPECT_GT(Selected.m_Border.a, Hovered.m_Border.a);
			EXPECT_EQ(Hovered.m_Border, ui_token::color::BORDER_SUBTLE);
			EXPECT_EQ(Pressed.m_Border, Hovered.m_Border);
			EXPECT_EQ(Left.m_Border, Selected.m_Border);
			const auto Cleared = ResolveUiButtonStyle(Role, Surface, Backdrop, Theme, {true, false, false, false});
			EXPECT_EQ(Cleared.m_Border, Hovered.m_Border);
		}
	}
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

TEST(QmButtonStyle, ListEntryKeepsNoBorderInSelectedHoveredAndPressedStates)
{
	SUiTheme Theme{};
	Theme.m_Selected = ColorRGBA(0.2f, 0.7f, 0.5f, 0.22f);
	const ColorRGBA Surface(0.1f, 0.2f, 0.3f, 0.4f);
	for(const ColorRGBA Backdrop : {ColorRGBA(0, 0, 0, 1), ColorRGBA(1, 1, 1, 1)})
	{
		const SUiButtonState aStates[] = {
			SUiButtonState{true, false, false, true}, // 选中
			SUiButtonState{true, true, false, false}, // 悬停
			SUiButtonState{true, true, true, false}, // 按下
			SUiButtonState{true, true, true, true}, // 按下的选中项
		};
		for(const SUiButtonState &State : aStates)
		{
			SCOPED_TRACE(::testing::Message() << "background=" << Backdrop.r << " hovered=" << State.m_Hovered << " pressed=" << State.m_Pressed << " selected=" << State.m_Selected);
			const auto Style = ResolveUiButtonStyle(EUiButtonRole::LIST_ENTRY, Surface, Backdrop, Theme, State);
			EXPECT_FLOAT_EQ(Style.m_Border.a, 0.0f);
		}
	}
}

TEST(QmButtonStyle, ListEntrySelectedFillIsTheThemeSelectedOverlayOnTheEntrySurface)
{
	SUiTheme Theme{};
	Theme.m_Selected = ColorRGBA(0.2f, 0.7f, 0.5f, 0.22f);
	const ColorRGBA Surface(0.1f, 0.2f, 0.3f, 0.4f), Backdrop(0, 0, 0, 1);
	const auto Idle = ResolveUiButtonStyle(EUiButtonRole::LIST_ENTRY, Surface, Backdrop, Theme, {});
	const auto Selected = ResolveUiButtonStyle(EUiButtonRole::LIST_ENTRY, Surface, Backdrop, Theme, {true, false, false, true});
	// 选中只叠加主题选中色，不给条目额外垫一层不透明底板。
	EXPECT_EQ(Selected.m_Fill, BlendUiButtonSurface(Surface, Theme.m_Selected));
	EXPECT_EQ(Idle.m_Fill, Surface);
	EXPECT_NE(Selected.m_Fill, Idle.m_Fill);
}

TEST(QmButtonStyle, OtherRolesKeepActiveBorderWhileListEntryDoesNot)
{
	SUiTheme Theme{};
	Theme.m_Accent = ColorRGBA(0.8f, 0.3f, 0.1f, 1.0f);
	Theme.m_Selected = ColorRGBA(0.2f, 0.7f, 0.5f, 0.22f);
	const ColorRGBA Surface(0.1f, 0.2f, 0.3f, 1.0f), Backdrop(0, 0, 0, 1);
	const SUiButtonState Hovered{true, true, false, false};
	const SUiButtonState Pressed{true, true, true, false};
	for(const EUiButtonRole Role : {EUiButtonRole::PRIMARY, EUiButtonRole::SECONDARY, EUiButtonRole::ICON})
	{
		SCOPED_TRACE(static_cast<int>(Role));
		EXPECT_GT(ResolveUiButtonStyle(Role, Surface, Backdrop, Theme, Hovered).m_Border.a, 0.0f);
		EXPECT_GT(ResolveUiButtonStyle(Role, Surface, Backdrop, Theme, Pressed).m_Border.a, 0.0f);
	}
	EXPECT_FLOAT_EQ(ResolveUiButtonStyle(EUiButtonRole::LIST_ENTRY, Surface, Backdrop, Theme, Hovered).m_Border.a, 0.0f);
	EXPECT_FLOAT_EQ(ResolveUiButtonStyle(EUiButtonRole::LIST_ENTRY, Surface, Backdrop, Theme, Pressed).m_Border.a, 0.0f);
}

TEST(QmControlSurface, ConfiguredButtonColorAndOpacityRemainIndependentOfInterfaceBackground)
{
	struct SRestoreConfig
	{
		CConfig m_Previous = g_Config;
		~SRestoreConfig() { g_Config = m_Previous; }
	} Restore;
	g_Config.m_QmUiControlColor = ColorHSLA(0, 1, 0.5f).Pack(false);
	g_Config.m_QmUiControlOpacity = 40;
	const ColorRGBA Button = ResolveConfiguredControlSurface();
	EXPECT_GT(Button.r, 0.99f);
	EXPECT_LT(Button.g, 0.01f);
	EXPECT_LT(Button.b, 0.01f);
	EXPECT_FLOAT_EQ(Button.a, 0.4f);
	for(const unsigned BackgroundColor : {0x000000u, 0x0000FFu})
	{
		g_Config.m_QmUiColor = BackgroundColor;
		for(const int BackgroundOpacity : {0, 20, 100})
		{
			SCOPED_TRACE(::testing::Message() << "background=" << BackgroundColor << " opacity=" << BackgroundOpacity);
			g_Config.m_QmUiOpacity = BackgroundOpacity;
			EXPECT_EQ(ResolveConfiguredControlSurface(), Button);
			const ColorRGBA Disabled = ResolveConfiguredControlSurface(false);
			EXPECT_FLOAT_EQ(Disabled.r, Button.r);
			EXPECT_FLOAT_EQ(Disabled.g, Button.g);
			EXPECT_FLOAT_EQ(Disabled.b, Button.b);
			EXPECT_FLOAT_EQ(Disabled.a, 0.26f);
			EXPECT_EQ(ResolveConfiguredIconButtonSurface(ColorRGBA(0.2f, 0.4f, 0.8f, 0.3f)), ColorRGBA(0.2f, 0.4f, 0.8f, 0.3f));
		}
	}
}

TEST(QmControlSurface, OwnOpacityIsClampedBeforeApplyingDisabledState)
{
	struct SCase
	{
		int m_Opacity;
		float m_EnabledAlpha;
		float m_DisabledAlpha;
	};
	for(const SCase Case : {SCase{-20, 0.0f, 0.0f}, SCase{0, 0.0f, 0.0f}, SCase{40, 0.4f, 0.26f}, SCase{100, 1.0f, 0.65f}, SCase{120, 1.0f, 0.65f}})
	{
		SCOPED_TRACE(Case.m_Opacity);
		const ColorRGBA Enabled = ResolveUiControlSurface(0x0000FF, Case.m_Opacity);
		const ColorRGBA Disabled = ResolveUiControlSurface(0x0000FF, Case.m_Opacity, false);
		EXPECT_EQ(Enabled.WithAlpha(1), ColorRGBA(1, 1, 1, 1));
		EXPECT_EQ(Disabled.WithAlpha(1), ColorRGBA(1, 1, 1, 1));
		EXPECT_FLOAT_EQ(Enabled.a, Case.m_EnabledAlpha);
		EXPECT_FLOAT_EQ(Disabled.a, Case.m_DisabledAlpha);
	}
}
