#include <game/client/components/qmclient/browser_status_layout.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
	constexpr float kCaption = 22.0f;
	constexpr float kAspect = 0.60f;
}

// 卡片尺寸的契约：行高只由列宽决定，与面板高度、内容行数、可见行数**全都无关**。
// 面板高度只决定这一屏放得下几行（末行可以只露一截），卡片本身永远保持同一形状。
TEST(QmGalleryLayout, RowHeightFollowsColumnWidthOnly)
{
	// 190 宽列 → 预览 114 + 说明行 22 = 136。
	EXPECT_FLOAT_EQ(QmResolveGalleryRowHeight(190.0f, kCaption, kAspect), 136.0f);
}

TEST(QmGalleryLayout, RowHeightIsIndependentOfViewportHeight)
{
	// 同一个列宽下，无论面板多高、内容有几行，行高都是同一个值——卡片不会被拉伸。
	// 旧实现按可用高度均分，面板矮时也会把卡片拉大，一屏反而只放两行（实机反馈）。
	const float Small = QmResolveGalleryRowHeight(240.0f, kCaption, kAspect);
	const float Large = QmResolveGalleryRowHeight(240.0f, kCaption, kAspect);
	EXPECT_FLOAT_EQ(Small, Large);
	EXPECT_FLOAT_EQ(Small, 240.0f * kAspect + kCaption);
}

TEST(QmGalleryLayout, WiderColumnGrowsCardProportionally)
{
	// 窗口变宽 → 列宽变大 → 卡片等比变大：预览部分严格按比例放大，说明行是常量。
	const float Narrow = QmResolveGalleryRowHeight(150.0f, kCaption, kAspect);
	const float Wide = QmResolveGalleryRowHeight(300.0f, kCaption, kAspect);
	EXPECT_FLOAT_EQ(Wide - Narrow, 150.0f * kAspect);
}

TEST(QmGalleryLayout, RowHeightStaysBelowOneCardForShortViewport)
{
	// 面板比一张卡片还矮：行高不能被压小（否则卡片变形），只是这一屏显示不满一行。
	const float RowHeight = QmResolveGalleryRowHeight(200.0f, kCaption, kAspect);
	EXPECT_FLOAT_EQ(RowHeight, 200.0f * kAspect + kCaption);
}

TEST(QmGalleryLayout, DegenerateInputsStaySane)
{
	// 列宽为 0 / 负数 / NaN 时退化成「只有说明行」，不产生负高度、不产生 NaN。
	EXPECT_FLOAT_EQ(QmResolveGalleryRowHeight(0.0f, kCaption, kAspect), kCaption);
	EXPECT_FLOAT_EQ(QmResolveGalleryRowHeight(-50.0f, kCaption, kAspect), kCaption);
	EXPECT_FLOAT_EQ(QmResolveGalleryRowHeight(std::nanf(""), kCaption, kAspect), kCaption);
	// 比例越界（0 / 负 / 极大）被夹到合理区间，行高不会变成 0 或天文数字。
	EXPECT_FLOAT_EQ(QmResolveGalleryRowHeight(100.0f, kCaption, -1.0f), 100.0f * 0.05f + kCaption);
	EXPECT_FLOAT_EQ(QmResolveGalleryRowHeight(100.0f, kCaption, 99.0f), 100.0f * 4.0f + kCaption);
}
