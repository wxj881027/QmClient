#include <base/str.h>

#include <game/client/QmUi/UiConfigHint.h>
#include <game/client/components/tooltips.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

TEST(QmTooltips, OwnsCallerText)
{
	char aCallerText[] = "rabbit";
	CTooltip Tooltip{nullptr, CUIRect{}, aCallerText, -1.0f, false};
	aCallerText[0] = 'R';
	EXPECT_EQ(Tooltip.m_Text, "rabbit");
}

TEST(QmTooltips, TextProjectionScalesAroundTheSameCenterAndRestoresWithoutMotion)
{
	const CUIRect Screen{100, 50, 400, 200};
	const auto Scaled = QmTooltipTextProjection(Screen, vec2(200, 100), 2.0f);
	EXPECT_FLOAT_EQ(Scaled.x, 150);
	EXPECT_FLOAT_EQ(Scaled.y, 75);
	EXPECT_FLOAT_EQ(Scaled.w, 200);
	EXPECT_FLOAT_EQ(Scaled.h, 100);
	const auto Restored = QmTooltipTextProjection(Screen, vec2(200, 100), 1.0f);
	EXPECT_FLOAT_EQ(Restored.x, Screen.x);
	EXPECT_FLOAT_EQ(Restored.y, Screen.y);
	EXPECT_FLOAT_EQ(Restored.w, Screen.w);
	EXPECT_FLOAT_EQ(Restored.h, Screen.h);
}

TEST(QmTooltips, BubbleIsCenteredOnItsAnchorAndMovesWithTheControl)
{
	const CUIRect Screen{0, 0, 600, 400};
	const auto First = QmTooltipRect({200, 180, 100, 20}, Screen, vec2(120, 40), 5);
	EXPECT_FLOAT_EQ(First.x, 190);
	EXPECT_FLOAT_EQ(First.y, 135);
	const auto Moved = QmTooltipRect({240, 200, 100, 20}, Screen, vec2(120, 40), 5);
	EXPECT_FLOAT_EQ(Moved.x - First.x, 40);
	EXPECT_FLOAT_EQ(Moved.y - First.y, 20);
}

TEST(QmTooltips, BubbleAtTopEdgeOpensBelowAndRespectsNonzeroScreenOrigin)
{
	const CUIRect Screen{100, 50, 300, 200};
	const auto Rect = QmTooltipRect({380, 55, 20, 20}, Screen, vec2(150, 40), 5);
	EXPECT_FLOAT_EQ(Rect.x, 245);
	EXPECT_FLOAT_EQ(Rect.y, 80);
	EXPECT_LE(Rect.x + Rect.w, Screen.x + Screen.w - 5);
}

TEST(QmTooltips, OversizedBubbleRemainsInsideSmallViewport)
{
	const CUIRect Screen{20, 30, 90, 60};
	const auto Rect = QmTooltipRect({100, 50, 10, 20}, Screen, vec2(400, 300), 5);
	EXPECT_FLOAT_EQ(Rect.w, 80);
	EXPECT_FLOAT_EQ(Rect.h, 50);
	EXPECT_FLOAT_EQ(Rect.x, 25);
	EXPECT_FLOAT_EQ(Rect.y, 35);
}

TEST(QmTooltips, BounceSettlesQuicklyAndDisabledMotionIsImmediate)
{
	EXPECT_LT(QmTooltipScale(0.0f, true), 1.0f);
	EXPECT_GT(QmTooltipScale(0.12f, true), 1.0f);
	EXPECT_FLOAT_EQ(QmTooltipScale(0.18f, true), 1.0f);
	EXPECT_FLOAT_EQ(QmTooltipScale(1.0f, true), 1.0f);
	EXPECT_FLOAT_EQ(QmTooltipScale(0.0f, false), 1.0f);
	EXPECT_FLOAT_EQ(QmTooltipScale(0.12f, false), 1.0f);
}

namespace
{
	struct STooltipPointer
	{
		vec2 m_Position{10, 10};
		bool m_InputAvailable = true;
		const void *m_pHotItem = nullptr;
		bool MouseHovered(const CUIRect *pRect) const { return m_InputAvailable && pRect->Inside(m_Position); }
		const void *HotItem() const { return m_pHotItem; }
	};
}

TEST(QmTooltips, RectangleHoverExpiresWhenInputIsBlockedAndRecoversAfterPopupCloses)
{
	CTooltip Tooltip{nullptr, CUIRect{0, 0, 20, 20}, "info", 100, true};
	Tooltip.m_HoverByRect = true;
	STooltipPointer Pointer;
	EXPECT_TRUE(QmTooltipHovered(Tooltip, Pointer));
	Pointer.m_InputAvailable = false;
	EXPECT_FALSE(QmTooltipHovered(Tooltip, Pointer));
	Pointer.m_InputAvailable = true;
	EXPECT_TRUE(QmTooltipHovered(Tooltip, Pointer));
	Pointer.m_Position = vec2(30, 10);
	EXPECT_FALSE(QmTooltipHovered(Tooltip, Pointer));
}

TEST(QmConfigHint, ResolvesClientIntegerColorAndStringBindingsWithoutReadingValues)
{
	CConfig Config{};
	Config.m_QmScreenshotWatermark = 1;
	str_copy(Config.m_QmScreenshotWatermarkText, "private watermark text");
	EXPECT_STREQ(QmUiConfigCommand(Config, &Config.m_QmScreenshotWatermark), "qm_screenshot_watermark");
	EXPECT_STREQ(QmUiConfigCommand(Config, &Config.m_QmUiColor), "qm_ui_color");
	EXPECT_STREQ(QmUiConfigCommand(Config, Config.m_QmScreenshotWatermarkText), "qm_screenshot_watermark_text");
	EXPECT_STREQ(QmUiConfigCommand(Config, &Config.m_ClShowhud), "cl_showhud");
	EXPECT_STREQ(QmUiConfigCommand(Config, Config.m_QmCustomFont), "qm_custom_font");
	Config.m_QmScreenshotWatermark = 0;
	EXPECT_STREQ(QmUiConfigCommand(Config, &Config.m_QmScreenshotWatermark), "qm_screenshot_watermark");
}

TEST(QmConfigHint, RejectsServerVariablesInteriorPointersAndUnboundControls)
{
	CConfig Config{};
	int Unbound = 0;
	EXPECT_EQ(QmUiConfigCommand(Config, nullptr), nullptr);
	EXPECT_EQ(QmUiConfigCommand(Config, &Unbound), nullptr);
	EXPECT_EQ(QmUiConfigCommand(Config, &Config.m_SvName), nullptr);
	EXPECT_EQ(QmUiConfigCommand(Config, Config.m_QmScreenshotWatermarkText + 1), nullptr);
	EXPECT_EQ(QmUiConfigCommand(Config, reinterpret_cast<const char *>(&Config) + sizeof(Config)), nullptr);
}

TEST(QmConfigHintText, PreservesDescriptionAndAppendsOnlyCommandNames)
{
	CUiConfigHintText Hint;
	Hint.SetDescription("Automatically add a watermark");
	Hint.SetCommands("qm_screenshot_watermark");
	EXPECT_STREQ(Hint.Text(), "Automatically add a watermark\nqm_screenshot_watermark");
	Hint.SetCommands("qm_screenshot_watermark");
	Hint.SetDescription(nullptr);
	EXPECT_STREQ(Hint.Text(), "Automatically add a watermark\nqm_screenshot_watermark");
}

TEST(QmConfigHintText, OwnsDescriptionAndReflectsLanguageChanges)
{
	CUiConfigHintText Hint;
	Hint.SetCommands("qm_screenshot_watermark");
	char aDescription[] = "Watermark";
	Hint.SetDescription(aDescription);
	aDescription[0] = 'w';
	EXPECT_STREQ(Hint.Text(), "Watermark\nqm_screenshot_watermark");
	Hint.SetDescription("截图水印");
	EXPECT_STREQ(Hint.Text(), "截图水印\nqm_screenshot_watermark");
}

TEST(QmConfigHintText, GroupedSettingsShowBothCommandsAndDeduplicateIdenticalBindings)
{
	CUiConfigHintText Hint;
	Hint.SetCommands("qm_ui_color", "qm_ui_opacity");
	EXPECT_STREQ(Hint.Text(), "qm_ui_color\nqm_ui_opacity");
	Hint.SetCommands("qm_ui_color", "qm_ui_color");
	EXPECT_STREQ(Hint.Text(), "qm_ui_color");
	Hint.SetCommands(nullptr, "qm_ui_opacity");
	EXPECT_STREQ(Hint.Text(), "qm_ui_opacity");
}

TEST(QmConfigHintText, RebindingAndClearingDoNotKeepPreviousOptionHelp)
{
	CUiConfigHintText Hint;
	Hint.SetCommands("qm_ui_color");
	Hint.SetDescription("Button color");
	Hint.SetCommands("qm_ui_opacity");
	EXPECT_STREQ(Hint.Text(), "qm_ui_opacity");
	Hint.SetDescription("Button opacity");
	Hint.SetCommands(nullptr);
	EXPECT_STREQ(Hint.Text(), "");
}

TEST(QmConfigHintText, UsesCurrentCardOverviewUntilOptionDescriptionIsAvailable)
{
	CUiConfigHintText Hint;
	Hint.SetCommands("qm_screenshot_watermark");
	Hint.SetFallbackDescription("Screenshot settings");
	EXPECT_STREQ(Hint.Text(), "Screenshot settings\nqm_screenshot_watermark");
	Hint.SetFallbackDescription("截图设置");
	EXPECT_STREQ(Hint.Text(), "截图设置\nqm_screenshot_watermark");
	Hint.SetDescription("Automatically add a watermark");
	Hint.SetFallbackDescription("Other card overview");
	EXPECT_STREQ(Hint.Text(), "Automatically add a watermark\nqm_screenshot_watermark");
	Hint.SetFallbackDescription(nullptr);
	EXPECT_STREQ(Hint.Text(), "Automatically add a watermark\nqm_screenshot_watermark");
}

TEST(QmTooltips, SettingsHintsHaveNoHoverDelayOrBounce)
{
	CTooltip Tooltip;
	Tooltip.m_SmallInstant = true;
	EXPECT_FLOAT_EQ(QmTooltipDelay(Tooltip), 0.0f);
	EXPECT_FALSE(QmTooltipAnimate(Tooltip, true));
	EXPECT_FLOAT_EQ(QmTooltipScale(0.0f, QmTooltipAnimate(Tooltip, true)), 1.0f);
}

TEST(QmTooltips, OrdinaryBubblesKeepDelayAndRespectDisabledMotion)
{
	CTooltip Tooltip;
	EXPECT_FLOAT_EQ(QmTooltipDelay(Tooltip), 0.75f);
	EXPECT_TRUE(QmTooltipAnimate(Tooltip, true));
	EXPECT_FALSE(QmTooltipAnimate(Tooltip, false));
	Tooltip.m_Immediate = true;
	EXPECT_FLOAT_EQ(QmTooltipDelay(Tooltip), 0.0f);
}

TEST(QmTooltips, VisibleBubbleSwitchesTargetWithoutWaitingOrReplayingItsEntrance)
{
	CTooltip First, Next;
	Next.m_FadeTime = 2.0f;
	CQmTooltipHoverState Hover;
	EXPECT_LT(Hover.Update(First, 10.0), 0.0f);
	EXPECT_GT(Hover.Update(First, 11.0), 0.0f);
	EXPECT_GE(Hover.Update(Next, 11.1), CQmTooltipHoverState::FADE_IN_SECONDS);
}

TEST(QmTooltips, SwitchingBeforeFirstBubbleAppearsRestartsTheHoverDelay)
{
	CTooltip First, Next;
	CQmTooltipHoverState Hover;
	Hover.Update(First, 10.0);
	EXPECT_LT(Hover.Update(First, 10.5), 0.0f);
	EXPECT_LT(Hover.Update(Next, 10.5), 0.0f);
	EXPECT_LT(Hover.Update(Next, 11.0), 0.0f);
	EXPECT_GE(Hover.Update(Next, 11.3), 0.0f);
}

TEST(QmTooltips, LeavingTargetsOrClosingPageRestoresDelayOnNextHover)
{
	CTooltip Tooltip;
	CQmTooltipHoverState Hover;
	Hover.Update(Tooltip, 10.0);
	EXPECT_GE(Hover.Update(Tooltip, 11.0), 0.0f);
	Hover.Clear();
	EXPECT_LT(Hover.Update(Tooltip, 11.1), 0.0f);
	EXPECT_GE(Hover.Update(Tooltip, 12.0), 0.0f);
}

TEST(QmTooltips, SwitchingFromInstantSettingsHintKeepsOrdinaryBubbleVisible)
{
	CTooltip Settings, Ordinary;
	Settings.m_SmallInstant = true;
	CQmTooltipHoverState Hover;
	EXPECT_FLOAT_EQ(Hover.Update(Settings, 10.0), 0.0f);
	EXPECT_GE(Hover.Update(Ordinary, 10.1), CQmTooltipHoverState::FADE_IN_SECONDS);
}

namespace
{
	struct STooltipTextObserver
	{
		ColorRGBA m_Color{0, 0, 0, 0};
		EFontPreset m_Preset = EFontPreset::ICON_FONT;
		unsigned m_Flags = TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH;
		ColorRGBA GetTextColor() const { return m_Color; }
		EFontPreset GetFontPreset() const { return m_Preset; }
		unsigned GetRenderFlags() const { return m_Flags; }
		void TextColor(ColorRGBA Color) { m_Color = Color; }
		void SetFontPreset(EFontPreset Preset) { m_Preset = Preset; }
		void SetRenderFlags(unsigned Flags) { m_Flags = Flags; }
	};
}

TEST(QmTooltips, TextPreparationIgnoresTransparentCallerColorAndIconFontThenRestoresThem)
{
	STooltipTextObserver Render;
	const STooltipTextObserver Before = Render;
	{
		CQmTooltipTextScope Scope(Render);
		EXPECT_EQ(Render.GetTextColor(), ColorRGBA(1, 1, 1, 1));
		EXPECT_EQ(Render.GetFontPreset(), EFontPreset::DEFAULT_FONT);
		EXPECT_EQ(Render.GetRenderFlags(), unsigned(TEXT_RENDER_FLAG_ONE_TIME_USE | TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT));
	}
	EXPECT_EQ(Render.GetTextColor(), Before.GetTextColor());
	EXPECT_EQ(Render.GetFontPreset(), Before.GetFontPreset());
	EXPECT_EQ(Render.GetRenderFlags(), Before.GetRenderFlags());
}

TEST(QmTooltips, PopupMotionKeepsTheSameCenterThroughOvershoot)
{
	const CUIRect Screen{0, 0, 600, 400};
	const CUIRect Fixed = QmTooltipRect({200, 180, 100, 20}, Screen, vec2(120, 40), 5);
	const auto Entering = QmTooltipAnimatedRect(Fixed, Screen, QmTooltipScale(0.0f, true));
	const auto Overshoot = QmTooltipAnimatedRect(Fixed, Screen, QmTooltipScale(0.12f, true));
	EXPECT_FLOAT_EQ(Entering.Center().x, Fixed.Center().x);
	EXPECT_FLOAT_EQ(Entering.Center().y, Fixed.Center().y);
	EXPECT_FLOAT_EQ(Overshoot.Center().x, Fixed.Center().x);
	EXPECT_FLOAT_EQ(Overshoot.Center().y, Fixed.Center().y);
	EXPECT_GT(Overshoot.w, Fixed.w);
}

TEST(QmTooltips, OvershootAtViewportEdgeKeepsCenterAndFitsScreen)
{
	const CUIRect Screen{100, 50, 300, 200};
	const CUIRect Fixed{100, 50, 150, 40};
	const auto Rect = QmTooltipAnimatedRect(Fixed, Screen, QmTooltipScale(0.12f, true));
	EXPECT_FLOAT_EQ(Rect.Center().x, Fixed.Center().x);
	EXPECT_FLOAT_EQ(Rect.Center().y, Fixed.Center().y);
	EXPECT_GE(Rect.x, Screen.x);
	EXPECT_GE(Rect.y, Screen.y);
	EXPECT_LE(Rect.x + Rect.w, Screen.x + Screen.w);
	EXPECT_LE(Rect.y + Rect.h, Screen.y + Screen.h);
}

TEST(QmTooltips, DisappearingTargetExpiresAndCanRegisterAgain)
{
	CTooltip Tooltip;
	Tooltip.m_RegisteredFrame = 42;
	EXPECT_TRUE(QmTooltipRegistered(Tooltip, 42));
	EXPECT_FALSE(QmTooltipRegistered(Tooltip, 43));
	Tooltip.m_RegisteredFrame = 44;
	EXPECT_TRUE(QmTooltipRegistered(Tooltip, 44));
}

TEST(QmTooltips, SpecificHelpWinsOverAutomaticDescriptionInEitherRegistrationOrder)
{
	CTooltip Specific, Fallback;
	Specific.m_RegisteredFrame = Fallback.m_RegisteredFrame = 1;
	Fallback.m_Fallback = true;
	EXPECT_FALSE(QmTooltipMayReplace(Fallback, Specific, 1, true));
	EXPECT_TRUE(QmTooltipMayReplace(Specific, Fallback, 1, true));
}

TEST(QmTooltips, MovingFromSpecificHelpToAnotherOptionActivatesItsFallbackImmediately)
{
	CTooltip Previous, Next;
	Previous.m_RegisteredFrame = Next.m_RegisteredFrame = 1;
	Next.m_Fallback = true;
	EXPECT_TRUE(QmTooltipMayReplace(Next, Previous, 1, false));
	EXPECT_TRUE(QmTooltipMayReplace(Next, Previous, 2, true));
}

TEST(QmTooltips, RectangleHintDoesNotWaitForHotItemOrMoveItsAnchorWithThePointer)
{
	CTooltip Tooltip;
	Tooltip.m_Rect = {0, 0, 100, 20};
	Tooltip.m_Anchor = {0, 0, 40, 20};
	Tooltip.m_HoverByRect = true;
	STooltipPointer Pointer;
	Pointer.m_pHotItem = nullptr;
	const CUIRect Screen{0, 0, 600, 400};
	const auto Before = QmTooltipRect(Tooltip.m_Anchor, Screen, vec2(120, 40), 5);
	EXPECT_TRUE(QmTooltipHovered(Tooltip, Pointer));
	Pointer.m_Position = vec2(90, 10);
	EXPECT_TRUE(QmTooltipHovered(Tooltip, Pointer));
	const auto After = QmTooltipRect(Tooltip.m_Anchor, Screen, vec2(120, 40), 5);
	EXPECT_FLOAT_EQ(Before.x, After.x);
	EXPECT_FLOAT_EQ(Before.y, After.y);
}

TEST(QmTooltips, InnerControlFallbackKeepsTheRowsRegisteredAnchorAndHitArea)
{
	CTooltip Row;
	Row.m_RegisteredFrame = 1;
	Row.m_Fallback = true;
	EXPECT_FALSE(QmTooltipMayUpdate(Row, 1, true, true));
	EXPECT_TRUE(QmTooltipMayUpdate(Row, 1, false, true));
	EXPECT_TRUE(QmTooltipMayUpdate(Row, 1, true, false));
	EXPECT_TRUE(QmTooltipMayUpdate(Row, 2, true, true));
}

TEST(QmTooltips, TitleAnchorRefinesAutomaticHintAndSurvivesLaterInnerRegistration)
{
	CTooltip Automatic;
	Automatic.m_RegisteredFrame = 1;
	Automatic.m_Fallback = true;
	EXPECT_TRUE(QmTooltipMayUpdate(Automatic, 1, true, true, true));
	Automatic.m_HasTextAnchor = true;
	EXPECT_FALSE(QmTooltipMayUpdate(Automatic, 1, true, true));
	EXPECT_FALSE(QmTooltipMayUpdate(Automatic, 1, true, true, true));
}

TEST(QmTooltips, AutomaticTitleAnchorDoesNotReplaceSpecificHelpOnTheSameControl)
{
	CTooltip Specific;
	Specific.m_RegisteredFrame = 1;
	EXPECT_FALSE(QmTooltipMayUpdate(Specific, 1, true, true, true));
	EXPECT_TRUE(QmTooltipMayUpdate(Specific, 1, false, true, true));
}

TEST(QmTooltips, ClearingContentClipDoesNotReactivateAHiddenTarget)
{
	CTooltip Tooltip;
	Tooltip.m_Rect = {0, 0, 20, 20};
	Tooltip.m_HoverByRect = true;
	Tooltip.m_RegisteredFrame = 1;
	STooltipPointer Pointer;
	Pointer.m_InputAvailable = false;
	Tooltip.m_OnScreen = QmTooltipHovered(Tooltip, Pointer);
	Pointer.m_InputAvailable = true;
	EXPECT_FALSE(QmTooltipActive(Tooltip, 1, Pointer));

	Tooltip.m_RegisteredFrame = 2;
	Tooltip.m_OnScreen = QmTooltipHovered(Tooltip, Pointer);
	EXPECT_TRUE(QmTooltipActive(Tooltip, 2, Pointer));
	Pointer.m_InputAvailable = false;
	EXPECT_FALSE(QmTooltipActive(Tooltip, 2, Pointer));
	Pointer.m_InputAvailable = true;
	EXPECT_FALSE(QmTooltipActive(Tooltip, 3, Pointer));
}
