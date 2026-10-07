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
