#include <engine/shared/config.h>

#include <game/client/QmUi/UiTag.h>

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <tuple>

// 标签组件的契约：几何固定、颜色只有三种来源、不描边、文字跟随「文本颜色模式」，
// 且「随机固定」必须由 key 完全决定（同一词跨会话同色，不能每次启动随机一次）。

namespace
{
	class CTextModeScope
	{
	public:
		explicit CTextModeScope(int Mode) :
			m_Previous(g_Config.m_QmUiTextColorMode)
		{
			g_Config.m_QmUiTextColorMode = Mode;
		}
		~CTextModeScope() { g_Config.m_QmUiTextColorMode = m_Previous; }

	private:
		int m_Previous;
	};
} // namespace

TEST(QmUiTag, GeometryIsFixedRegardlessOfCaller)
{
	EXPECT_FLOAT_EQ(ui_tag::HEIGHT, 16.0f);
	EXPECT_FLOAT_EQ(ui_tag::PADDING_X, 6.0f);
	EXPECT_FLOAT_EQ(ui_tag::RADIUS, 8.0f);
	EXPECT_FLOAT_EQ(ui_tag::RADIUS * 2.0f, ui_tag::HEIGHT);
}

TEST(QmUiTag, SeedIsStableAndDistinct)
{
	EXPECT_EQ(ui_tag::SeedFromKey("hj"), ui_tag::SeedFromKey("hj"));
	EXPECT_NE(ui_tag::SeedFromKey("hj"), ui_tag::SeedFromKey("fly"));
	EXPECT_NE(ui_tag::SeedFromKey(""), ui_tag::SeedFromKey("a"));
	// 大小写敏感：不同词就该不同种子（不做归一化，避免两个不同标签撞同一色）。
	EXPECT_NE(ui_tag::SeedFromKey("Hook"), ui_tag::SeedFromKey("hook"));
}

TEST(QmUiTag, RandomFixedIsStableAndWellSpread)
{
	const ui_tag::SColors First = ui_tag::Resolve(ui_tag::EColorMode::RANDOM_FIXED, ColorRGBA(), "hj");
	const ui_tag::SColors Second = ui_tag::Resolve(ui_tag::EColorMode::RANDOM_FIXED, ColorRGBA(), "hj");
	EXPECT_FLOAT_EQ(First.m_Fill.r, Second.m_Fill.r);
	EXPECT_FLOAT_EQ(First.m_Fill.g, Second.m_Fill.g);
	EXPECT_FLOAT_EQ(First.m_Fill.b, Second.m_Fill.b);
	EXPECT_FLOAT_EQ(First.m_Fill.a, Second.m_Fill.a);

	static const char *const aWords[] = {"hj", "fly", "Gores", "Hook", "block", "speedrun", "novice", "solo"};
	std::set<unsigned> Seeds;
	for(const char *pWord : aWords)
		Seeds.insert(ui_tag::SeedFromKey(pWord) % 360u);
	// 8 个常用标签里色相至少散在 5 档以上，不会一眼看成同一个色。
	EXPECT_GE(Seeds.size(), 5u);
}

TEST(QmUiTag, RandomFixedUsesMonetPalette)
{
	// 色板本身：12 色相 × 4 变体 = 48 色、全部不透明、亮度压在同一档（白字够读）。
	EXPECT_EQ(ui_token::tag::MONET_HUE_COUNT, 12);
	EXPECT_EQ(ui_token::tag::MONET_VARIANTS_PER_HUE, 4);
	EXPECT_EQ(ui_token::tag::MONET_PALETTE_SIZE, 48);
	for(int Index = 0; Index < ui_token::tag::MONET_PALETTE_SIZE; ++Index)
	{
		const ColorRGBA &Color = ui_token::tag::MONET_PALETTE[Index];
		EXPECT_FLOAT_EQ(Color.a, 1.0f);
		EXPECT_GT(ui_tag::RelativeLuminance(Color), 0.40f);
		EXPECT_LT(ui_tag::RelativeLuminance(Color), 0.52f);
	}
	// 同一个词必须落在色板的某一格上（不是再拿 HSL 现算），且可重复。
	const ui_tag::SColors Colors = ui_tag::Resolve(ui_tag::EColorMode::RANDOM_FIXED, ColorRGBA(), "hj");
	bool Matched = false;
	for(int Index = 0; Index < ui_token::tag::MONET_PALETTE_SIZE; ++Index)
	{
		const ColorRGBA &Color = ui_token::tag::MONET_PALETTE[Index];
		Matched = Matched || (std::fabs(Colors.m_Fill.r - Color.r) < 0.001f && std::fabs(Colors.m_Fill.g - Color.g) < 0.001f && std::fabs(Colors.m_Fill.b - Color.b) < 0.001f);
	}
	EXPECT_TRUE(Matched);
	// 词不同就该在色板上分散：8 个常用词几乎不该撞色（48 色下实测全不同）。
	// 同时验证取色是「变体在外、色相在内」：相邻种子不会落到同一色相的深浅变体。
	static const char *const aWords[] = {"hj", "fly", "Gores", "Hook", "block", "speedrun", "novice", "solo"};
	std::set<std::tuple<float, float, float>> Used;
	for(const char *pWord : aWords)
	{
		const ui_tag::SColors WordColors = ui_tag::Resolve(ui_tag::EColorMode::RANDOM_FIXED, ColorRGBA(), pWord);
		Used.insert({WordColors.m_Fill.r, WordColors.m_Fill.g, WordColors.m_Fill.b});
	}
	EXPECT_GE(Used.size(), 7u);
}

TEST(QmUiTag, FixedKeepsCallerColor)
{
	const CTextModeScope Mode(0);
	const ColorRGBA Green(0.18f, 0.62f, 0.32f, 0.88f);
	const ui_tag::SColors Colors = ui_tag::Resolve(ui_tag::EColorMode::FIXED, Green, nullptr);
	EXPECT_NEAR(Colors.m_Fill.r, 0.18f, 0.001f);
	EXPECT_NEAR(Colors.m_Fill.g, 0.62f, 0.001f);
	EXPECT_NEAR(Colors.m_Fill.a, 0.88f, 0.001f);
	// 白色模式：文字纯白，不按填充亮度自动翻色。
	EXPECT_GT(Colors.m_Text.r, 0.9f);
	EXPECT_GT(Colors.m_Text.g, 0.9f);
	EXPECT_GT(Colors.m_Text.b, 0.9f);
}

TEST(QmUiTag, DerivedIsADarkerShadeOfBase)
{
	const ColorRGBA Base(0.90f, 0.50f, 0.40f, 1.0f);
	const ui_tag::SColors Colors = ui_tag::Resolve(ui_tag::EColorMode::DERIVED, Base, nullptr);
	EXPECT_LT(Colors.m_Fill.r, Base.r);
	EXPECT_LT(Colors.m_Fill.g, Base.g);
	EXPECT_LT(Colors.m_Fill.b, Base.b);
	// 公式固定：基色 × 0.72。
	EXPECT_NEAR(Colors.m_Fill.r, Base.r * 0.72f, 0.001f);
	EXPECT_NEAR(Colors.m_Fill.g, Base.g * 0.72f, 0.001f);
}

TEST(QmUiTag, AlphaScalesFillAndText)
{
	const ui_tag::SColors Full = ui_tag::Resolve(ui_tag::EColorMode::RANDOM_FIXED, ColorRGBA(), "hj", 1.0f);
	const ui_tag::SColors Half = ui_tag::Resolve(ui_tag::EColorMode::RANDOM_FIXED, ColorRGBA(), "hj", 0.5f);
	EXPECT_NEAR(Half.m_Fill.a, Full.m_Fill.a * 0.5f, 0.001f);
	EXPECT_NEAR(Half.m_Text.a, Full.m_Text.a * 0.5f, 0.001f);
}

TEST(QmUiTag, TextFollowsConfiguredTextColorMode)
{
	const ColorRGBA Base(0.18f, 0.62f, 0.32f, 1.0f);
	{
		const CTextModeScope Mode(0);
		EXPECT_GT(ui_tag::Resolve(ui_tag::EColorMode::FIXED, Base, nullptr).m_Text.r, 0.9f);
	}
	{
		const CTextModeScope Mode(1);
		const ui_tag::SColors Colors = ui_tag::Resolve(ui_tag::EColorMode::FIXED, Base, nullptr);
		// 黑色档：文字压到深色，填充仍保持调用点的绿色（二者互不影响）。
		EXPECT_LT(Colors.m_Text.r, 0.2f);
		EXPECT_NEAR(Colors.m_Fill.g, 0.62f, 0.001f);
	}
}

TEST(QmUiTag, FillNeverCarriesABorder)
{
	// 组件不再有描边字段：胶囊只有填充 + 文字，调用点不可能再画出「小卡片外框」。
	const ui_tag::SColors Colors = ui_tag::Resolve(ui_tag::EColorMode::RANDOM_FIXED, ColorRGBA(), "hj");
	EXPECT_FLOAT_EQ(Colors.m_Fill.a, 0.92f);
}
