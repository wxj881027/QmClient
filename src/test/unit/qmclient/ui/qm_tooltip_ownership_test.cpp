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

TEST(QmTooltips, VisibleLinesFitBubbleAndKeepMinimumForTinyViewport)
{
	EXPECT_EQ(QmTooltipVisibleLines(100, 14), 7);
	EXPECT_EQ(QmTooltipVisibleLines(28, 14), 2);
	EXPECT_EQ(QmTooltipVisibleLines(1, 14), 1);
	EXPECT_EQ(QmTooltipVisibleLines(-5, 0), 1);
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
		vec2 MousePos() const { return m_Position; }
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
