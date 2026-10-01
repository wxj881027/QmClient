#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsCardDeckLogic.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

TEST(SettingsCard, ContentMeasureProbeMatchesConsumedLayout)
{
	const float Measured = ResolveSettingsCardContentHeight([](CUIRect &Content) {
		Content.HSplitTop(18.0f, nullptr, &Content);
		Content.HSplitTop(6.0f, nullptr, &Content);
	},
		320.0f);
	EXPECT_FLOAT_EQ(24.0f, Measured);

	EXPECT_FLOAT_EQ(0.0f, ResolveSettingsCardContentHeight(FSettingsCardRenderMeasured{}, 320.0f));
}

TEST(SettingsCard, ContentMeasureProbeClampsInvalidWidthAndOverconsumption)
{
	EXPECT_FLOAT_EQ(0.0f, ResolveSettingsCardContentHeight([](CUIRect &Content) {
		Content.HSplitTop(Content.w, nullptr, &Content);
	},
				      -10.0f));

	const float Measured = ResolveSettingsCardContentHeight([](CUIRect &Content) {
		Content.HSplitTop(SETTINGS_CARD_CONTENT_MEASURE_PROBE_HEIGHT + 100.0f, nullptr, &Content);
	},
		320.0f);
	EXPECT_FLOAT_EQ(SETTINGS_CARD_CONTENT_MEASURE_PROBE_HEIGHT, Measured);
}

TEST(SettingsCard, ContentClipAllowsFocusRingButNeverEscapesTheCard)
{
	const CUIRect Card{10.0f, 20.0f, 200.0f, 100.0f};
	const CUIRect Content{20.0f, 45.0f, 180.0f, 65.0f};
	const CUIRect Clip = ResolveSettingsCardContentClipRect(Content, Card, 1.0f);

	EXPECT_GE(Clip.x, Card.x);
	EXPECT_GE(Clip.y, Card.y);
	EXPECT_LE(Clip.x + Clip.w, Card.x + Card.w);
	EXPECT_LE(Clip.y + Clip.h, Card.y + Card.h);
	EXPECT_LT(Clip.y, Content.y);
	EXPECT_GT(Clip.y + Clip.h, Content.y + Content.h - 0.001f);
}

TEST(SettingsCardDeck, RestingCardsDoNotDrawASecondRoundedBorder)
{
	SSettingsCardVisualState State;
	EXPECT_FALSE(SettingsCardInteractionBorderVisible(State));
	State.m_Hovered = true;
	EXPECT_FALSE(SettingsCardInteractionBorderVisible(State));
	State.m_Hovered = false;
	State.m_Focused = true;
	EXPECT_TRUE(SettingsCardInteractionBorderVisible(State));
	State.m_Focused = false;
	State.m_Dragged = true;
	EXPECT_TRUE(SettingsCardInteractionBorderVisible(State));
	State.m_Dragged = false;
	State.m_DropFeedback = true;
	EXPECT_TRUE(SettingsCardInteractionBorderVisible(State));
}

TEST(SettingsCardDeck, RenderOnlyAndVisiblePassPlanChromeExactlyOnce)
{
	int SurfaceDrawCount = 0;
	int BorderedSurfaceDrawCount = 0;
	const auto DrawSurface = [&] { ++SurfaceDrawCount; };
	const auto DrawBorderedSurface = [&] { ++BorderedSurfaceDrawCount; };

	ExecuteSettingsCardChromeDraw(SettingsCardShouldDrawChrome(true), false, DrawSurface, DrawBorderedSurface);
	EXPECT_EQ(SurfaceDrawCount, 0);
	EXPECT_EQ(BorderedSurfaceDrawCount, 0);

	ExecuteSettingsCardChromeDraw(SettingsCardShouldDrawChrome(false), false, DrawSurface, DrawBorderedSurface);
	EXPECT_EQ(SurfaceDrawCount, 1);
	EXPECT_EQ(BorderedSurfaceDrawCount, 0);

	ExecuteSettingsCardChromeDraw(SettingsCardShouldDrawChrome(false), true, DrawSurface, DrawBorderedSurface);
	EXPECT_EQ(SurfaceDrawCount, 1);
	EXPECT_EQ(BorderedSurfaceDrawCount, 1);
}

TEST(SettingsCardDeck, InteractionBorderStaysInsideSurfaceEdge)
{
	const CUIRect Surface{10.0f, 20.0f, 200.0f, 100.0f};
	const CUIRect Border = ResolveSettingsCardInteractionBorderRect(Surface, 2.0f);
	EXPECT_GT(Border.x, Surface.x);
	EXPECT_GT(Border.y, Surface.y);
	EXPECT_LT(Border.x + Border.w, Surface.x + Surface.w);
	EXPECT_LT(Border.y + Border.h, Surface.y + Surface.h);
}

TEST(SettingsCardDeck, CardSurfaceColorIgnoresBorderInteractionState)
{
	const ColorRGBA BaseSurface(0.12f, 0.24f, 0.36f, 0.48f);
	SSettingsCardVisualState Resting;
	Resting.m_DrawAlpha = 0.75f;
	SSettingsCardVisualState Interactive = Resting;
	Interactive.m_Hovered = true;
	Interactive.m_Focused = true;
	Interactive.m_DropFeedback = true;

	const ColorRGBA RestingSurface = ResolveSettingsCardSurfaceColor(BaseSurface, Resting);
	const ColorRGBA InteractiveSurface = ResolveSettingsCardSurfaceColor(BaseSurface, Interactive);
	EXPECT_FLOAT_EQ(RestingSurface.r, InteractiveSurface.r);
	EXPECT_FLOAT_EQ(RestingSurface.g, InteractiveSurface.g);
	EXPECT_FLOAT_EQ(RestingSurface.b, InteractiveSurface.b);
	EXPECT_FLOAT_EQ(RestingSurface.a, InteractiveSurface.a);
	EXPECT_FLOAT_EQ(RestingSurface.a, BaseSurface.a * Resting.m_DrawAlpha);
}

TEST(SettingsCardDeck, BorderWidthDoesNotDependOnFocus)
{
	// Focus 状态不参与宽度解析，交互反馈只能改变边框颜色。
	EXPECT_FLOAT_EQ(ResolveSettingsCardBorderWidth(1.0f), 2.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsCardBorderWidth(0.5f), 2.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsCardBorderWidth(2.0f), 4.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsCardBorderWidth(1.3f, 0.5f), 2.5f);
}

TEST(SettingsCardDeck, ChromeGeometryAlignsToThePhysicalPixelGrid)
{
	const CUIRect Rect{10.2f, 20.3f, 99.6f, 49.4f};
	const CUIRect Aligned = ResolveSettingsCardChromeRect(Rect, 0.5f);
	EXPECT_FLOAT_EQ(Aligned.x, 10.0f);
	EXPECT_FLOAT_EQ(Aligned.y, 20.5f);
	EXPECT_FLOAT_EQ(Aligned.w, 100.0f);
	EXPECT_FLOAT_EQ(Aligned.h, 49.0f);
	EXPECT_FLOAT_EQ(AlignSettingsCardValueToPixels(12.2f, 0.5f), 12.0f);
	EXPECT_FLOAT_EQ(AlignSettingsCardValueToPixels(12.2f, 0.0f), 12.2f);
}

TEST(SettingsCardDeck, InnerSurfaceCompensatesBorderWithoutTintingCardBackground)
{
	const ColorRGBA Surface(0.24f, 0.28f, 0.32f, 0.70f);
	ColorRGBA Border(0.90f, 0.15f, 0.10f, 0.20f);
	const ColorRGBA Inner = ResolveSettingsCardInnerSurfaceColor(Surface, Border);
	Border.a = std::min(Border.a, Surface.a - 0.001f);
	const float CombinedAlpha = Inner.a + Border.a * (1.0f - Inner.a);
	const auto CombinedChannel = [&](const float InnerChannel, const float BorderChannel) {
		return InnerChannel * Inner.a + BorderChannel * Border.a * (1.0f - Inner.a);
	};
	EXPECT_NEAR(CombinedAlpha, Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.r, Border.r), Surface.r * Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.g, Border.g), Surface.g * Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.b, Border.b), Surface.b * Surface.a, 0.001f);
}

TEST(SettingsCardDeck, EffectiveBorderAlphaCannotPolluteATranslucentSurface)
{
	const ColorRGBA Surface(0.24f, 0.28f, 0.32f, 0.20f);
	const ColorRGBA Border(0.90f, 0.15f, 0.10f, 1.0f);
	const ColorRGBA Effective = ResolveSettingsCardEffectiveBorderColor(Border, Surface);
	const ColorRGBA Inner = ResolveSettingsCardInnerSurfaceColor(Surface, Effective);
	const float CombinedAlpha = Inner.a + Effective.a * (1.0f - Inner.a);
	const auto CombinedChannel = [&](const float InnerChannel, const float BorderChannel) {
		return InnerChannel * Inner.a + BorderChannel * Effective.a * (1.0f - Inner.a);
	};

	EXPECT_LT(Effective.a, Surface.a);
	EXPECT_NEAR(CombinedAlpha, Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.r, Effective.r), Surface.r * Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.g, Effective.g), Surface.g * Surface.a, 0.001f);
	EXPECT_NEAR(CombinedChannel(Inner.b, Effective.b), Surface.b * Surface.a, 0.001f);
}

TEST(SettingsCardDeck, ConfiguredBorderColorDoesNotTintSurface)
{
	const ColorRGBA Surface(0.24f, 0.28f, 0.32f, 0.70f);
	SSettingsCardVisualState State;
	const ColorRGBA Resolved = ResolveSettingsCardSurfaceColor(Surface, State);
	EXPECT_FLOAT_EQ(Resolved.r, Surface.r);
	EXPECT_FLOAT_EQ(Resolved.g, Surface.g);
	EXPECT_FLOAT_EQ(Resolved.b, Surface.b);
	EXPECT_FLOAT_EQ(Resolved.a, Surface.a);
}
