#include <engine/client/qm_font_category.h>

#include <gtest/gtest.h>

// QmClient: 分类字形的码点分类纯函数单测。分类决定字形解析时优先尝试
// 哪个字体面（中文/图标符号），错分会导致中文字形取错字体或符号缺字。

TEST(QmFontCategory, CjkCodepoints)
{
	// CJK 标点、汉字、假名、谚文、全角形式。
	EXPECT_TRUE(QmIsCjkCodepoint(0x3001)); // 、
	EXPECT_TRUE(QmIsCjkCodepoint(0x300C)); // 「
	EXPECT_TRUE(QmIsCjkCodepoint(0x4E2D)); // 中
	EXPECT_TRUE(QmIsCjkCodepoint(0x9FA5));
	EXPECT_TRUE(QmIsCjkCodepoint(0x3042)); // あ
	EXPECT_TRUE(QmIsCjkCodepoint(0xAC00)); // 가
	EXPECT_TRUE(QmIsCjkCodepoint(0xFF0C)); // ，
	EXPECT_TRUE(QmIsCjkCodepoint(0xFF21)); // Ａ
	EXPECT_TRUE(QmIsCjkCodepoint(0x20000)); // 扩展 B
	EXPECT_FALSE(QmIsCjkCodepoint('A'));
	EXPECT_FALSE(QmIsCjkCodepoint(0x00E9)); // é
	EXPECT_FALSE(QmIsCjkCodepoint(0x2605)); // ★（属符号分类）
	EXPECT_FALSE(QmIsCjkCodepoint(0x1F600)); // emoji
}

TEST(QmFontCategory, IconSymbolCodepoints)
{
	// 星号、心形、对勾、几何形状、箭头、表情与私用区。
	EXPECT_TRUE(QmIsIconSymbolCodepoint(0x2605)); // ★
	EXPECT_TRUE(QmIsIconSymbolCodepoint(0x2606)); // ☆
	EXPECT_TRUE(QmIsIconSymbolCodepoint(0x2665)); // ♥
	EXPECT_TRUE(QmIsIconSymbolCodepoint(0x2713)); // ✓
	EXPECT_TRUE(QmIsIconSymbolCodepoint(0x25CF)); // ●
	EXPECT_TRUE(QmIsIconSymbolCodepoint(0x2192)); // →
	EXPECT_TRUE(QmIsIconSymbolCodepoint(0x2B50)); // ⭐
	EXPECT_TRUE(QmIsIconSymbolCodepoint(0x1F600)); // 😀
	EXPECT_TRUE(QmIsIconSymbolCodepoint(0xE0B2)); // 私用区（图标字体）
	EXPECT_FALSE(QmIsIconSymbolCodepoint(0x2500)); // ─ 制表符：保留给等宽字体
	EXPECT_FALSE(QmIsIconSymbolCodepoint(0x2588)); // █ 方块元素：保留给等宽字体
	EXPECT_FALSE(QmIsIconSymbolCodepoint('x'));
	EXPECT_FALSE(QmIsIconSymbolCodepoint(0x4E2D)); // 中（属 CJK 分类）
}
