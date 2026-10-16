// 信号柱图标家族的字号补偿策略：Phosphor cell-signal 的 bbox em 占比逐档
// 塌缩（HIGH 0.5625 … NONE 0.09375），同字号绘制时视觉尺寸远小于常规图标。
// 用户校准：整组统一放大，四档字号一致 → 柱线粗细一致，四档墨迹都在
// 占位方框内，垂直居中天然对称。
#include <game/client/qm_icon.h>

#include <gtest/gtest.h>

#include <algorithm>

TEST(QmSignalIconFontSize, UnifiesBarWidthAcrossTiers)
{
	const CUIRect Rect{0, 0, 21, 21};
	// 四档字号一致：柱线粗细完全一致（用户校准以整组统一放大为基准）。
	const float High = QmSignalIconFontSize(Rect, EQmIcon::CELL_SIGNAL_HIGH);
	EXPECT_FLOAT_EQ(High, QmSignalIconFontSize(Rect, EQmIcon::CELL_SIGNAL_MEDIUM));
	EXPECT_FLOAT_EQ(High, QmSignalIconFontSize(Rect, EQmIcon::CELL_SIGNAL_LOW));
	EXPECT_FLOAT_EQ(High, QmSignalIconFontSize(Rect, EQmIcon::CELL_SIGNAL_NONE));
	// 统一倍数 1.5×：用户反馈 1.15× 在灵动岛胶囊里偏小，放大后最高档
	// 墨迹高度 0.844 方框字号，仍完整在方框内。
	const float Base = QmIconFontSize(Rect);
	EXPECT_FLOAT_EQ(High, Base * 1.5f);
}

TEST(QmSignalIconFontSize, RestoresTierHeightLadder)
{
	// 视觉高度 = 统一字号 × bbox 占比：四格 > 三格 > 两格 > 空档，
	// 恢复 Phosphor 字形本来的阶梯设计（与系统状态栏信号一致）。
	const CUIRect Rect{0, 0, 21, 21};
	const float FontSize = QmSignalIconFontSize(Rect, EQmIcon::CELL_SIGNAL_HIGH);
	const float High = FontSize * 0.5625f;
	const float Medium = FontSize * 0.40625f;
	const float Low = FontSize * 0.25f;
	EXPECT_GT(High, Medium);
	EXPECT_GT(Medium, Low);
	// 最高档墨迹不超过占位方框（1.5×0.5625 = 0.844 方框字号 < 1.0 方框）。
	EXPECT_LE(High, 21.0f);
	// helper 与手工计算一致。
	EXPECT_FLOAT_EQ(QmSignalIconMaxVisualSize(21.0f), High);
}

TEST(QmSignalIconFontSize, HighTierFitsInsideSlot)
{
	// 最高档的视觉高度不能超过灵动岛状态区占位方框（21px），否则被
	// 裁剪或压到相邻内容。
	const CUIRect Rect{0, 0, 21, 21};
	const float Visual = QmSignalIconFontSize(Rect, EQmIcon::CELL_SIGNAL_HIGH) * 0.5625f;
	EXPECT_LE(Visual, 21.0f);
	// helper 与手工计算一致。
	EXPECT_FLOAT_EQ(QmSignalIconMaxVisualSize(21.0f), Visual);
}

TEST(QmSignalIconFontSize, NonSignalIconsUnaffected)
{
	const CUIRect Rect{0, 0, 21, 21};
	EXPECT_FLOAT_EQ(QmSignalIconFontSize(Rect, EQmIcon::USERS), QmIconFontSize(Rect));
	EXPECT_FALSE(QmSignalIconFamily(EQmIcon::USERS));
	EXPECT_TRUE(QmSignalIconFamily(EQmIcon::CELL_SIGNAL_LOW));
}
