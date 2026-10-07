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
