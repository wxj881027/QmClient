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
		EXPECT_EQ(Render.GetRenderFlags(), unsigned(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT));
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

TEST(QmCardLabelHints, MovesAllGroupsIncludingAcronymsAndWarningsIntoHint)
{
	const auto Text = QmSplitCardLabel("全屏抗锯齿（FSAA）采样倍数 (可能会产生延迟)");
	EXPECT_EQ(Text.m_Label, "全屏抗锯齿采样倍数");
	EXPECT_EQ(Text.m_Hint, "FSAA\n可能会产生延迟");
}

TEST(QmCardLabelHints, UnitsAndOptionDistinctionsMoveWithoutFiltering)
{
	const auto Units = QmSplitCardLabel("Threshold (ms)");
	EXPECT_EQ(Units.m_Label, "Threshold");
	EXPECT_EQ(Units.m_Hint, "ms");
	const auto Option = QmSplitCardLabel("Show others (own team only)");
	EXPECT_EQ(Option.m_Label, "Show others");
	EXPECT_EQ(Option.m_Hint, "own team only");
}

TEST(QmCardLabelHints, NestedParenthesesStayTogetherInTheHint)
{
	const auto Text = QmSplitCardLabel("Mode （details (experimental) here） enabled");
	EXPECT_EQ(Text.m_Label, "Mode enabled");
	EXPECT_EQ(Text.m_Hint, "details (experimental) here");
}

TEST(QmCardLabelHints, IncompleteGroupsStayVisibleAndPlainWhitespaceIsUnchanged)
{
	const auto Broken = QmSplitCardLabel("Mode (unfinished");
	EXPECT_EQ(Broken.m_Label, "Mode (unfinished");
	EXPECT_TRUE(Broken.m_Hint.empty());
	const auto Plain = QmSplitCardLabel("  Plain  label  ");
	EXPECT_EQ(Plain.m_Label, "  Plain  label  ");
	EXPECT_TRUE(Plain.m_Hint.empty());
}

TEST(QmCardLabelHints, ParentheticalOnlyChoiceKeepsAVisibleHoverTarget)
{
	const auto Text = QmSplitCardLabel("(Follow English font)");
	EXPECT_EQ(Text.m_Label, "…");
	EXPECT_EQ(Text.m_Hint, "Follow English font");
}

TEST(QmCardLabelHints, CacheOwnsCallerTextAndKeepsTranslatedEntriesStable)
{
	CQmCardLabelHintCache Cache;
	std::string Caller = "V-Sync (may cause delay)";
	const auto &English = Cache.Get(Caller);
	Caller = "changed";
	const auto &Chinese = Cache.Get("垂直同步（可能会产生延迟）");
	EXPECT_EQ(English.m_Label, "V-Sync");
	EXPECT_EQ(Chinese.m_Label, "垂直同步");
	EXPECT_EQ(&English, &Cache.Get("V-Sync (may cause delay)"));
	Cache.Clear();
	EXPECT_EQ(Cache.Get("Mode (new)").m_Hint, "new");
}

namespace
{
	struct SCardLabelUi
	{
		bool m_Enabled = false;
		bool CardLabelHintsEnabled() const { return m_Enabled; }
		void SetCardLabelHintsEnabled(bool Enabled) { m_Enabled = Enabled; }
	};
}

TEST(QmCardLabelHints, NestedCardAndInputScopesRestoreTheCallersState)
{
	SCardLabelUi Ui;
	{
		CQmCardLabelHintScope Card(&Ui);
		EXPECT_TRUE(Ui.m_Enabled);
		{
			CQmCardLabelHintScope Input(&Ui, false);
			EXPECT_FALSE(Ui.m_Enabled);
		}
		EXPECT_TRUE(Ui.m_Enabled);
	}
	EXPECT_FALSE(Ui.m_Enabled);
}

TEST(QmTooltips, ShortGapKeepsVisibleBubbleAndNextTargetSkipsEntrance)
{
	CTooltip First, Next;
	CQmTooltipHoverState Hover;
	Hover.Update(First, 10.0);
	Hover.Update(First, 11.0);
	EXPECT_TRUE(Hover.Retain(11.1));
	EXPECT_TRUE(Hover.Retain(11.2));
	EXPECT_GE(Hover.Update(Next, 11.2), CQmTooltipHoverState::FADE_IN_SECONDS);
}

TEST(QmTooltips, GapTimeoutRestoresTheNextTargetsDelay)
{
	CTooltip First, Next;
	CQmTooltipHoverState Hover;
	Hover.Update(First, 10.0);
	Hover.Update(First, 11.0);
	EXPECT_FALSE(Hover.Retain(11.3));
	EXPECT_LT(Hover.Update(Next, 11.31), 0.0f);
}

TEST(QmTooltips, GapBeforeAnyVisibleBubbleDoesNotRetainAContainer)
{
	CTooltip Tooltip;
	CQmTooltipHoverState Hover;
	Hover.Update(Tooltip, 10.0);
	EXPECT_FALSE(Hover.Retain(10.1));
	EXPECT_LT(Hover.Update(Tooltip, 10.5), 0.0f);
}

TEST(QmTooltips, TargetChangeAfterLongFrameDoesNotReuseExpiredVisibility)
{
	CTooltip First, Next;
	CQmTooltipHoverState Hover;
	Hover.Update(First, 10.0);
	Hover.Update(First, 11.0);
	EXPECT_LT(Hover.Update(Next, 11.5), 0.0f);
}

TEST(QmTooltips, MotionStartsAtCurrentPositionAndSettlesOnTheNextTarget)
{
	CQmTooltipMotionState Motion;
	const CUIRect First{10, 20, 100, 30}, Next{110, 120, 200, 50};
	EXPECT_FLOAT_EQ(Motion.Update(First, 0, true).x, First.x);
	EXPECT_FLOAT_EQ(Motion.Update(Next, 1, true).x, First.x);
	const auto Middle = Motion.Update(Next, 1.08, true);
	EXPECT_GT(Middle.x, First.x);
	EXPECT_LT(Middle.x, Next.x);
	EXPECT_GT(Middle.w, First.w);
	EXPECT_LT(Middle.w, Next.w);
	const auto Settled = Motion.Update(Next, 1.2, true);
	EXPECT_FLOAT_EQ(Settled.x, Next.x);
	EXPECT_FLOAT_EQ(Settled.y, Next.y);
	EXPECT_FLOAT_EQ(Settled.w, Next.w);
}

TEST(QmTooltips, RapidRetargetingContinuesFromTheVisibleRectangle)
{
	CQmTooltipMotionState Motion;
	const CUIRect First{0, 0, 100, 30}, Second{100, 50, 100, 30}, Third{200, 150, 100, 30};
	Motion.Update(First, 0, true);
	Motion.Update(Second, 1, true);
	const auto Before = Motion.Update(Second, 1.08, true);
	const auto After = Motion.Update(Third, 1.08, true);
	EXPECT_FLOAT_EQ(After.x, Before.x);
	EXPECT_FLOAT_EQ(After.y, Before.y);
	EXPECT_FLOAT_EQ(Motion.Update(Third, 1.3, true).x, Third.x);
}

TEST(QmTooltips, DisabledMotionAndPageResetPlaceTheBubbleImmediately)
{
	CQmTooltipMotionState Motion;
	Motion.Update({0, 0, 100, 30}, 0, true);
	EXPECT_FLOAT_EQ(Motion.Update({100, 100, 100, 30}, 1, false).x, 100);
	Motion.Clear();
	EXPECT_FLOAT_EQ(Motion.Update({200, 100, 100, 30}, 1.1, true).x, 200);
}

namespace
{
	struct STooltipContainerRenderer
	{
		int m_Created = 0;
		int m_Updated = 0;
		int m_Deleted = 0;
		std::string m_Text;
		void RecreateTextContainerSoft(STextContainerIndex &Index, CTextCursor *pCursor, const char *pText)
		{
			if(!Index.Valid())
				Index.m_Index = ++m_Created;
			++m_Updated;
			m_Text = pText;
			EXPECT_FLOAT_EQ(pCursor->m_StartX, 0);
			EXPECT_FLOAT_EQ(pCursor->m_StartY, 0);
		}
		void DeleteTextContainer(STextContainerIndex &Index)
		{
			if(Index.m_Index >= 0)
				++m_Deleted;
			Index.Reset();
		}
	};
}

TEST(QmTooltipTextCache, MovingAndChangingContentKeepTheSameContainer)
{
	CQmTooltipTextCache Cache;
	STooltipContainerRenderer Render;
	Cache.Update(Render, QmTooltipTextCursor({10, 20, 100, 40}, 10, 100, 0), "first");
	const int Index = Cache.Index().m_Index;
	Cache.Update(Render, QmTooltipTextCursor({200, 300, 100, 40}, 10, 100, 0), "first");
	EXPECT_EQ(Render.m_Updated, 1);
	Cache.Update(Render, QmTooltipTextCursor({200, 300, 100, 40}, 10, 100, 0), "next");
	EXPECT_EQ(Cache.Index().m_Index, Index);
	EXPECT_EQ(Render.m_Text, "next");
	EXPECT_EQ(Render.m_Created, 1);
	EXPECT_EQ(Render.m_Updated, 2);
	EXPECT_EQ(Render.m_Deleted, 0);
	Cache.Clear(Render);
	EXPECT_EQ(Render.m_Deleted, 1);
	EXPECT_FALSE(Cache.Index().Valid());
}

TEST(QmTooltipTextCache, ScaleWrapAndLineLimitChangesUpdateLayoutWithoutReleasingTheContainer)
{
	CQmTooltipTextCache Cache;
	STooltipContainerRenderer Render;
	Cache.Update(Render, QmTooltipTextCursor({}, 10, 100, 0), "text");
	Cache.Update(Render, QmTooltipTextCursor({}, 12, 100, 0), "text");
	Cache.Update(Render, QmTooltipTextCursor({}, 12, 80, 0), "text");
	Cache.Update(Render, QmTooltipTextCursor({}, 12, 80, 1), "text");
	EXPECT_EQ(Render.m_Created, 1);
	EXPECT_EQ(Render.m_Updated, 4);
	EXPECT_EQ(Render.m_Deleted, 0);
}

TEST(QmTooltipTextCache, ClosingAndReopeningCreatesAFreshContainer)
{
	CQmTooltipTextCache Cache;
	STooltipContainerRenderer Render;
	Cache.Update(Render, QmTooltipTextCursor({}, 10, 100, 0), "text");
	Cache.Clear(Render);
	Cache.Update(Render, QmTooltipTextCursor({}, 10, 100, 0), "text");
	EXPECT_TRUE(Cache.Index().Valid());
	EXPECT_EQ(Render.m_Created, 2);
	EXPECT_EQ(Render.m_Deleted, 1);
}

TEST(QmTooltips, UniformTextScaleFitsBothDimensionsAndCentersTheTargetLayout)
{
	const CUIRect Screen{100, 50, 400, 200};
	const CUIRect Layout{200, 100, 100, 80};
	for(const CUIRect Bubble : std::array<CUIRect, 2>{{{120, 60, 200, 20}, {200, 70, 20, 120}}})
	{
		SCOPED_TRACE(Bubble.w);
		const auto Projection = QmTooltipTextProjection(Screen, Layout, Bubble);
		const auto Project = [&](vec2 Point) {
			return vec2(Screen.x + (Point.x - Projection.x) * Screen.w / Projection.w,
				Screen.y + (Point.y - Projection.y) * Screen.h / Projection.h);
		};
		const vec2 Center = Project(Layout.Center());
		EXPECT_NEAR(Center.x, Bubble.Center().x, 0.001f);
		EXPECT_NEAR(Center.y, Bubble.Center().y, 0.001f);
		const vec2 TopLeft = Project(Layout.TopLeft());
		const vec2 BottomRight = Project(Layout.TopLeft() + vec2(Layout.w, Layout.h));
		EXPECT_GE(TopLeft.x, Bubble.x - 0.001f);
		EXPECT_GE(TopLeft.y, Bubble.y - 0.001f);
		EXPECT_LE(BottomRight.x, Bubble.x + Bubble.w + 0.001f);
		EXPECT_LE(BottomRight.y, Bubble.y + Bubble.h + 0.001f);
		EXPECT_NEAR((BottomRight.x - TopLeft.x) / Layout.w, (BottomRight.y - TopLeft.y) / Layout.h, 0.001f);
	}
}

TEST(QmTooltips, DisabledMotionRestoresTargetSizeAndUnmodifiedTextProjection)
{
	const CUIRect Screen{100, 50, 400, 200};
	const CUIRect Target{200, 100, 120, 60};
	CQmTooltipMotionState Motion;
	Motion.Update({120, 70, 200, 20}, 0, true);
	const auto Bubble = QmTooltipAnimatedRect(Motion.Update(Target, 1, false), Screen, QmTooltipScale(0, false));
	EXPECT_FLOAT_EQ(Bubble.x, Target.x);
	EXPECT_FLOAT_EQ(Bubble.y, Target.y);
	EXPECT_FLOAT_EQ(Bubble.w, Target.w);
	EXPECT_FLOAT_EQ(Bubble.h, Target.h);
	EXPECT_FLOAT_EQ(QmTooltipTextScale(Target, Bubble), 1);
	const auto Projection = QmTooltipTextProjection(Screen, Target, Bubble);
	EXPECT_FLOAT_EQ(Projection.x, Screen.x);
	EXPECT_FLOAT_EQ(Projection.y, Screen.y);
	EXPECT_FLOAT_EQ(Projection.w, Screen.w);
	EXPECT_FLOAT_EQ(Projection.h, Screen.h);
}

TEST(QmTooltips, EmptyLayoutOrBubbleDoesNotCreateAnInvalidProjection)
{
	const CUIRect Screen{100, 50, 400, 200};
	for(const CUIRect Empty : std::array<CUIRect, 2>{{{0, 0, 0, 30}, {0, 0, 100, 0}}})
	{
		EXPECT_FLOAT_EQ(QmTooltipTextScale(Empty, Screen), 0);
		EXPECT_FLOAT_EQ(QmTooltipTextScale(Screen, Empty), 0);
		const auto Projection = QmTooltipTextProjection(Screen, Empty, Screen);
		EXPECT_FLOAT_EQ(Projection.x, Screen.x);
		EXPECT_FLOAT_EQ(Projection.y, Screen.y);
		EXPECT_FLOAT_EQ(Projection.w, Screen.w);
		EXPECT_FLOAT_EQ(Projection.h, Screen.h);
	}
}

TEST(QmTooltips, RegisteredClippedSourceClearsGraceAndRestoresDelayAfterRecovery)
{
	CTooltip Tooltip;
	CQmTooltipHoverState Hover;
	Hover.Update(Tooltip, 10.0);
	ASSERT_GE(Hover.Update(Tooltip, 11.0), 0);
	const CUIRect Clip{0, 0, 100, 100};
	QmTooltipRecordSource(Tooltip, 42, {0, 110, 80, 20}, &Clip, vec2(20, 120));
	EXPECT_FALSE(Hover.Retain(11.05, &Tooltip, 42));
	QmTooltipRecordSource(Tooltip, 43, {0, 0, 80, 20}, &Clip, vec2(20, 10));
	EXPECT_TRUE(Tooltip.m_SourceAvailable);
	EXPECT_LT(Hover.Update(Tooltip, 11.1), 0);
	EXPECT_GE(Hover.Update(Tooltip, 12.0), 0);
}

TEST(QmTooltips, PointerOverClippedPartDoesNotRetainAPartiallyVisibleSource)
{
	CTooltip Tooltip;
	Tooltip.m_Rect = {0, 0, 80, 40};
	Tooltip.m_HoverByRect = true;
	Tooltip.m_OnScreen = true;
	Tooltip.m_RegisteredFrame = 42;
	const CUIRect Clip{0, 0, 100, 20};
	QmTooltipRecordSource(Tooltip, 42, Tooltip.m_Rect, &Clip, vec2(10, 30));
	STooltipPointer Pointer;
	Pointer.m_Position = vec2(10, 30);
	EXPECT_FALSE(QmTooltipActive(Tooltip, 42, Pointer));
	CQmTooltipHoverState Hover;
	Hover.Update(Tooltip, 10.0);
	Hover.Update(Tooltip, 11.0);
	EXPECT_FALSE(Hover.Retain(11.05, &Tooltip, 42));
}

TEST(QmTooltips, ExplicitEmptyTextClearsVisibleGrace)
{
	CTooltip Tooltip;
	CQmTooltipHoverState Hover;
	Hover.Update(Tooltip, 10.0);
	Hover.Update(Tooltip, 11.0);
	QmTooltipRecordSource(Tooltip, 42, {0, 0, 80, 20}, nullptr, vec2(10, 10), false);
	EXPECT_FALSE(Hover.Retain(11.05, &Tooltip, 42));
}

TEST(QmTooltips, CollapsedSourceRectClearsVisibleGrace)
{
	CTooltip Tooltip;
	CQmTooltipHoverState Hover;
	Hover.Update(Tooltip, 10.0);
	Hover.Update(Tooltip, 11.0);
	QmTooltipRecordSource(Tooltip, 42, {0, 0, 80, 0}, nullptr, vec2(10, 10));
	EXPECT_FALSE(Hover.Retain(11.05, &Tooltip, 42));
}

TEST(QmTooltips, VisibleRegisteredSourceAllowsPointerGapAndImmediateSwitch)
{
	CTooltip First, Next;
	CQmTooltipHoverState Hover;
	Hover.Update(First, 10.0);
	Hover.Update(First, 11.0);
	const CUIRect Clip{0, 0, 100, 100};
	QmTooltipRecordSource(First, 42, {0, 0, 80, 20}, &Clip, vec2(90, 30));
	EXPECT_TRUE(Hover.Retain(11.05, &First, 42));
	EXPECT_GE(Hover.Update(Next, 11.1), CQmTooltipHoverState::FADE_IN_SECONDS);
}

TEST(QmTooltips, MissingRegistrationKeepsOnlyBoundedGraceWithoutGuessingSourceLifetime)
{
	CTooltip Tooltip;
	CQmTooltipHoverState Hover;
	Hover.Update(Tooltip, 10.0);
	Hover.Update(Tooltip, 11.0);
	QmTooltipRecordSource(Tooltip, 41, {0, 0, 80, 0}, nullptr, vec2(10, 10));
	// 旧帧无效证据不能推断本帧来源已卸载；仍保留远程跨间隙用途。
	EXPECT_TRUE(Hover.Retain(11.05, &Tooltip, 42));
	EXPECT_TRUE(Hover.Retain(11.2, nullptr, 42));
	EXPECT_FALSE(Hover.Retain(11.3, &Tooltip, 42));
	EXPECT_LT(Hover.Update(Tooltip, 11.31), 0);
}
