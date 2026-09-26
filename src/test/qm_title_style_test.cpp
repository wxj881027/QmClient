#include <engine/textrender.h>

#include <game/client/components/qmclient/qm_title_render.h>
#include <game/client/components/qmclient/qm_title_style.h>
#include <game/client/components/qmclient/qmclient_utils.h>

#include <gtest/gtest.h>

#include <cmath>

// 称号风格引擎是纯函数模块：配色采样、逐字符相位、浮动偏移、掠光与时间对齐都在这里，
// 因此这一层全部用行为断言覆盖，不依赖绘制实现。
namespace
{
	const SQmTitleStyle &Style(const char *pId)
	{
		const SQmTitleStyle *pStyle = QmTitleStyleById(pId);
		EXPECT_NE(pStyle, nullptr) << "缺少风格: " << pId;
		return *pStyle;
	}

	// 比对 0xRRGGBB，允许逐通道误差（源码里有 float/double 混算）。
	void ExpectColorNear(ColorRGBA Color, unsigned int Hex, float Tolerance = 0.004f)
	{
		EXPECT_NEAR(Color.r, (float)((Hex >> 16) & 0xFFu) / 255.0f, Tolerance);
		EXPECT_NEAR(Color.g, (float)((Hex >> 8) & 0xFFu) / 255.0f, Tolerance);
		EXPECT_NEAR(Color.b, (float)(Hex & 0xFFu) / 255.0f, Tolerance);
	}
}

TEST(QmTitleStyle, ColorFromHexIsExact)
{
	ExpectColorNear(QmTitleStyleColorFromHex(0x00FFC8), 0x00FFC8, 0.0f);
	ExpectColorNear(QmTitleStyleColorFromHex(0x000000), 0x000000, 0.0f);
	ExpectColorNear(QmTitleStyleColorFromHex(0xFFFFFF), 0xFFFFFF, 0.0f);
	EXPECT_FLOAT_EQ(QmTitleStyleColorFromHex(0x123456).a, 1.0f);
}

TEST(QmTitleStyle, StyleTableIsWellFormed)
{
	ASSERT_EQ(QmTitleStyleCount(), 31);
	for(int Index = 0; Index < QmTitleStyleCount(); ++Index)
	{
		const SQmTitleStyle *pStyle = QmTitleStyleByIndex(Index);
		ASSERT_NE(pStyle, nullptr) << "Index=" << Index;
		EXPECT_NE(pStyle->m_pId, nullptr);
		EXPECT_NE(pStyle->m_pId[0], '\0');
		EXPECT_NE(pStyle->m_pLabel, nullptr);
		EXPECT_NE(pStyle->m_pColors, nullptr);
		EXPECT_GT(pStyle->m_ColorCount, 0) << "Index=" << Index;
		EXPECT_GT(pStyle->m_TimeScale, 0.0f) << "Index=" << Index;
		// 静态风格不带周期与相位；动态风格必须有正周期，否则采样会退化成首色。
		EXPECT_GE(pStyle->m_PeriodSec, 0.0f) << "Index=" << Index;
		// 按 id 反查必须回到同一项。
		EXPECT_EQ(QmTitleStyleById(pStyle->m_pId), pStyle);
	}
	// 越界与空指针都返回 nullptr，而不是拿表头兜底。
	EXPECT_EQ(QmTitleStyleByIndex(-1), nullptr);
	EXPECT_EQ(QmTitleStyleByIndex(QmTitleStyleCount()), nullptr);
	EXPECT_EQ(QmTitleStyleById(nullptr), nullptr);
	EXPECT_EQ(QmTitleStyleById(""), nullptr);
	EXPECT_EQ(QmTitleStyleById("no_such_style"), nullptr);
}

TEST(QmTitleStyle, StaticStyleIsConstant)
{
	const SQmTitleStyle &Turquoise = Style("turquoise");
	ExpectColorNear(QmTitleStyleSample(Turquoise, 0.0f, 0.0f), 0x00FFC8);
	// 静态风格不随时间和像素位置变化。
	ExpectColorNear(QmTitleStyleSample(Turquoise, 123.5f, 400.0f), 0x00FFC8);
	ExpectColorNear(QmTitleStyleSample(Style("donator_item"), 7.0f, 0.0f), 0xFF799C);
	ExpectColorNear(QmTitleStyleSample(Style("hot_pink"), 7.0f, 0.0f), 0xFF00FF);
}

TEST(QmTitleStyle, SinSwapReachesBothEnds)
{
	const SQmTitleStyle &ScarletDevil = Style("scarlet_devil");
	ASSERT_FLOAT_EQ(ScarletDevil.m_PeriodSec, 4.0f);
	// 正弦往返：半周期分别落在两端色标上。
	ExpectColorNear(QmTitleStyleSample(ScarletDevil, 1.0f, 0.0f), 0xB9BBFD);
	ExpectColorNear(QmTitleStyleSample(ScarletDevil, 3.0f, 0.0f), 0xBF2D47);
	// 中间时刻介于两端之间（不贴边）。
	const ColorRGBA Mid = QmTitleStyleSample(ScarletDevil, 0.0f, 0.0f);
	EXPECT_GT(Mid.r, 0.70f);
	EXPECT_LT(Mid.r, 0.76f);
}

TEST(QmTitleStyle, MulticolorLerpWalksColorRing)
{
	const SQmTitleStyle &Angelic = Style("angelic_alliance");
	ASSERT_EQ(Angelic.m_ColorCount, 3);
	ExpectColorNear(QmTitleStyleSample(Angelic, 0.0f, 0.0f), 0xFFC437);
	// 周期 2 秒走完三个色标：中途落在第二色，满一圈回到首色。
	ExpectColorNear(QmTitleStyleSample(Angelic, 2.0f / 3.0f, 0.0f), 0xFFE76B, 0.02f);
	ExpectColorNear(QmTitleStyleSample(Angelic, 2.0f, 0.0f), 0xFFC437, 0.02f);
}

TEST(QmTitleStyle, PhaseCycleMatchesExoticRainbowTemplate)
{
	const SQmTitleStyle &Rainbow = Style("exotic_rainbow");
	ASSERT_EQ(Rainbow.m_ColorCount, 3);
	EXPECT_EQ(Rainbow.m_Mode, EQmTitleStyleMode::PhaseCycle);
	// 每 2 秒推进一个色标；前半周期渐入，后半周期保持。
	ExpectColorNear(QmTitleStyleSample(Rainbow, 0.0f, 0.0f), 0xFF6B6B);
	ExpectColorNear(QmTitleStyleSample(Rainbow, 2.0f, 0.0f), 0x7DC4E1);
	ExpectColorNear(QmTitleStyleSample(Rainbow, 4.0f, 0.0f), 0xD3EB6C);
	ExpectColorNear(QmTitleStyleSample(Rainbow, 6.0f, 0.0f), 0xFF6B6B);
}

TEST(QmTitleStyle, PhaseCycleAddsPerPixelPhase)
{
	// 加了像素相位后，同一时刻不同 PixelX 取到不同进度——这正是光带扫过的来源。
	const SQmTitleStyle &Rainbow = Style("exotic_rainbow");
	const ColorRGBA Near = QmTitleStyleSample(Rainbow, 0.0f, 0.0f);
	const ColorRGBA Shifted = QmTitleStyleSample(Rainbow, 0.0f, 100.0f);
	EXPECT_GT(std::fabs(Near.r - Shifted.r) + std::fabs(Near.g - Shifted.g) + std::fabs(Near.b - Shifted.b), 0.001f);
	// 相位系数为 0 的风格对 PixelX 完全不敏感（整行同步）。
	const SQmTitleStyle &Eternity = Style("eternity");
	EXPECT_FLOAT_EQ(Eternity.m_PhasePerPx, 0.0f);
	const ColorRGBA A = QmTitleStyleSample(Eternity, 0.4f, 0.0f);
	const ColorRGBA B = QmTitleStyleSample(Eternity, 0.4f, 500.0f);
	ExpectColorNear(B, ((unsigned)(A.r * 255.0f + 0.5f) << 16) | ((unsigned)(A.g * 255.0f + 0.5f) << 8) | (unsigned)(A.b * 255.0f + 0.5f), 0.004f);
}

TEST(QmTitleStyle, PhaseCycleHardSwitchQuantizes)
{
	const SQmTitleStyle &Rainbow = Style("exotic_rainbow");
	// 平滑档在周期内连续变化；硬切档把插值因子量化到 0/1，退化成整段纯色。
	ExpectColorNear(QmTitleStyleSampleWithInterpolation(Rainbow, 0.3f, 0.0f, EQmTitleInterpolation::Hard), 0xFF6B6B);
	ExpectColorNear(QmTitleStyleSampleWithInterpolation(Rainbow, 0.7f, 0.0f, EQmTitleInterpolation::Hard), 0x7DC4E1);
	// 源码原始档位就是 Hard（ExoticRainbow 的 MathF.Round），默认渲染档位才是 Smooth。
	EXPECT_EQ(QmTitleStyleSourceInterpolation(Rainbow), EQmTitleInterpolation::Hard);
	EXPECT_EQ(QmTitleStyleSourceInterpolation(Style("eternity")), EQmTitleInterpolation::Smooth);
}

TEST(QmTitleStyle, PiecewiseMatchesFlamsteedRing)
{
	const SQmTitleStyle &Flamsteed = Style("flamsteed_ring");
	EXPECT_EQ(Flamsteed.m_Mode, EQmTitleStyleMode::Piecewise);
	// 0.0-0.6 纯色，0.6-0.8 渐到第二色，0.8-1.0 渐回首色。
	ExpectColorNear(QmTitleStyleSample(Flamsteed, 0.3f, 0.0f), 0x59E5FF);
	ExpectColorNear(QmTitleStyleSample(Flamsteed, 0.6f, 0.0f), 0x59E5FF);
	const ColorRGBA Fading = QmTitleStyleSample(Flamsteed, 0.7f, 0.0f);
	EXPECT_GT(Fading.r, 0.35f);
	EXPECT_LT(Fading.r, 1.0f);
	ExpectColorNear(QmTitleStyleSample(Flamsteed, 1.0f, 0.0f), 0x59E5FF);
}

TEST(QmTitleStyle, NegativeTimeStaysInRange)
{
	// 负时间不能产生越界索引或 NaN：所有风格都必须返回有限且在规定范围内的颜色。
	for(int Index = 0; Index < QmTitleStyleCount(); ++Index)
	{
		const SQmTitleStyle &Style = *QmTitleStyleByIndex(Index);
		const ColorRGBA Color = QmTitleStyleSample(Style, -3.5f, -120.0f);
		EXPECT_TRUE(std::isfinite(Color.r) && std::isfinite(Color.g) && std::isfinite(Color.b)) << Style.m_pId;
		EXPECT_GE(Color.r, 0.0f);
		EXPECT_LE(Color.r, 1.0f);
		EXPECT_GE(Color.g, 0.0f);
		EXPECT_LE(Color.g, 1.0f);
		EXPECT_GE(Color.b, 0.0f);
		EXPECT_LE(Color.b, 1.0f);
	}
}

TEST(QmTitleStyle, EffectivePhasePerPxOverride)
{
	const SQmTitleStyle &Style0 = Style("eternity");
	ASSERT_FLOAT_EQ(Style0.m_PhasePerPx, 0.0f);
	// Override > 0 时覆盖风格自带值；否则沿用风格值。
	EXPECT_FLOAT_EQ(QmTitleStyleEffectivePhasePerPx(Style0, 0.02f), 0.02f);
	EXPECT_FLOAT_EQ(QmTitleStyleEffectivePhasePerPx(Style0, 0.0f), 0.0f);
	const SQmTitleStyle &Rainbow = Style("exotic_rainbow");
	EXPECT_FLOAT_EQ(QmTitleStyleEffectivePhasePerPx(Rainbow, 0.0f), Rainbow.m_PhasePerPx);
}

TEST(QmTitleStyle, ZeroPeriodFallsBackToFirstColor)
{
	// 周期非正会让相位变成 inf/NaN；表项填错时必须退回首色而不是把 NaN 写进顶点色。
	SQmTitleStyle Broken = Style("scarlet_devil");
	Broken.m_PeriodSec = 0.0f;
	ExpectColorNear(QmTitleStyleSample(Broken, 1.0f, 0.0f), 0xBF2D47);
	ExpectColorNear(QmTitleStyleSample(Broken, 99.0f, 500.0f), 0xBF2D47);
	Broken.m_PeriodSec = -2.0f;
	ExpectColorNear(QmTitleStyleSample(Broken, 1.0f, 0.0f), 0xBF2D47);
}

TEST(QmTitleStyle, BobOffsetDisabled)
{
	SQmTitleBobStyle Bob{};
	EXPECT_FLOAT_EQ(QmTitleStyleBobOffset(Bob, 1.0f, 10.0f), 0.0f);
	// 波长为 0 同样视为关闭，避免除零。
	Bob.m_Amplitude = 4.0f;
	Bob.m_WaveLength = 0.0f;
	EXPECT_FLOAT_EQ(QmTitleStyleBobOffset(Bob, 1.0f, 10.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmTitleStyleBobPadding(Bob, 1.0f), 0.0f);
}

TEST(QmTitleStyle, BobOffsetVariesAlongPixelX)
{
	SQmTitleBobStyle Bob{};
	Bob.m_Amplitude = 4.0f;
	Bob.m_WaveLength = 320.0f;
	Bob.m_Speed = 1.5f;
	// 同一条横线上不同像素位置的偏移不同，才形成从左向右的波纹。
	EXPECT_NE(QmTitleStyleBobOffset(Bob, 1.0f, 0.0f), QmTitleStyleBobOffset(Bob, 1.0f, 160.0f));
	// 相距半个波长的两点相位相反。
	EXPECT_NEAR(QmTitleStyleBobOffset(Bob, 1.0f, 0.0f), -QmTitleStyleBobOffset(Bob, 1.0f, 160.0f), 1e-5f);
}

TEST(QmTitleStyle, BobOffsetAdvancesWithTime)
{
	SQmTitleBobStyle Bob{};
	Bob.m_Amplitude = 4.0f;
	Bob.m_WaveLength = 320.0f;
	Bob.m_Speed = 1.5f;
	EXPECT_NE(QmTitleStyleBobOffset(Bob, 0.0f, 0.0f), QmTitleStyleBobOffset(Bob, 1.0f, 0.0f));
}

TEST(QmTitleStyle, BobOffsetBoundedAndSnappedWhenRequested)
{
	SQmTitleBobStyle Bob{};
	Bob.m_Amplitude = 6.0f;
	Bob.m_WaveLength = 200.0f;
	Bob.m_Speed = 2.0f;
	for(int Step = 0; Step < 64; ++Step)
	{
		const float Time = Step * 0.05f;
		EXPECT_LE(std::fabs(QmTitleStyleBobOffset(Bob, Time, 37.0f)), 6.0f + 1e-5f);
	}
	// 量化步长为 1 时结果落在整数像素上（轮廓更锐利）。
	Bob.m_QuantizeStep = 1.0f;
	for(int Step = 0; Step < 64; ++Step)
	{
		const float Offset = QmTitleStyleBobOffset(Bob, Step * 0.05f, 37.0f);
		EXPECT_FLOAT_EQ(Offset, std::nearbyint(Offset));
		EXPECT_LE(std::fabs(Offset), 6.0f + 1e-5f);
	}
}

TEST(QmTitleStyle, BobOffsetIsSubPixelByDefault)
{
	// 默认不量化：偏移随帧连续变化，观感平滑（像素字体会略糊，取舍见配置说明）。
	SQmTitleBobStyle Bob{};
	Bob.m_Amplitude = 4.0f;
	Bob.m_WaveLength = 320.0f;
	Bob.m_Speed = 1.5f;
	ASSERT_FLOAT_EQ(Bob.m_QuantizeStep, 0.0f);
	bool SawFractional = false;
	for(int Step = 0; Step < 64; ++Step)
	{
		const float Offset = QmTitleStyleBobOffset(Bob, Step * 0.01f, 0.0f);
		if(std::fabs(Offset - std::nearbyint(Offset)) > 1e-3f)
			SawFractional = true;
	}
	EXPECT_TRUE(SawFractional);
}

TEST(QmTitleStyle, BobPaddingCoversAmplitudeAndRoundsUp)
{
	SQmTitleBobStyle Bob{};
	Bob.m_Amplitude = 4.0f;
	Bob.m_WaveLength = 320.0f;
	Bob.m_Speed = 1.5f;
	// 预留高度必须盖住整段振幅，否则波峰会被行距裁掉。
	EXPECT_GE(QmTitleStyleBobPadding(Bob, 1.5f), 4.0f);
	// 按屏幕像素向上取整：4 像素振幅、1.5 像素字号 → 5 像素（不是 4.5）。
	EXPECT_FLOAT_EQ(QmTitleStyleBobPadding(Bob, 1.5f), 4.5f);
	EXPECT_FLOAT_EQ(QmTitleStyleBobPadding(Bob, 2.0f), 4.0f);
	// 字号无效时退化成原始振幅。
	EXPECT_FLOAT_EQ(QmTitleStyleBobPadding(Bob, 0.0f), 4.0f);
}

TEST(QmTitleStyle, ShimmerDisabledPaths)
{
	SQmTitleShimmer Shimmer;
	EXPECT_FLOAT_EQ(QmTitleShimmerFactor(Shimmer, 0, 4, 0.0f), 0.0f);
	Shimmer.m_Enabled = true;
	EXPECT_FLOAT_EQ(QmTitleShimmerFactor(Shimmer, 0, 0, 0.0f), 0.0f) << "没有字符就没有掠光";
	Shimmer.m_Speed = 0.0f;
	EXPECT_FLOAT_EQ(QmTitleShimmerFactor(Shimmer, 0, 4, 0.0f), 0.0f);
	Shimmer.m_Speed = 60.0f;
	Shimmer.m_Amount = 0.0f;
	EXPECT_FLOAT_EQ(QmTitleShimmerFactor(Shimmer, 0, 4, 0.0f), 0.0f);
}

TEST(QmTitleStyle, ShimmerFactorStaysBounded)
{
	SQmTitleShimmer Shimmer;
	Shimmer.m_Enabled = true;
	Shimmer.m_Speed = 60.0f;
	for(int Index = 0; Index < 8; ++Index)
	{
		for(int Step = 0; Step < 40; ++Step)
		{
			const float Factor = QmTitleShimmerFactor(Shimmer, Index, 8, Step * 0.05f);
			EXPECT_GE(Factor, 0.0f);
			EXPECT_LE(Factor, Shimmer.m_Amount);
		}
	}
}

TEST(QmTitleStyle, ShimmerPhaseIsLengthIndependent)
{
	// 相位按「第几个字 / 总字数」分配，所以长短头衔的扫过速度一致。
	SQmTitleShimmer Shimmer;
	Shimmer.m_Enabled = true;
	Shimmer.m_Speed = 60.0f;
	EXPECT_FLOAT_EQ(QmTitleShimmerFactor(Shimmer, 0, 8, 0.0f), QmTitleShimmerFactor(Shimmer, 0, 16, 0.0f));
	EXPECT_FLOAT_EQ(QmTitleShimmerFactor(Shimmer, 4, 8, 0.0f), QmTitleShimmerFactor(Shimmer, 8, 16, 0.0f));
}

TEST(QmTitleStyle, ShimmerCountsUtf8Chars)
{
	EXPECT_EQ(QmTitleShimmerUtf8CharCount(""), 0);
	EXPECT_EQ(QmTitleShimmerUtf8CharCount("abc"), 3);
	// 多字节字符各算一个，而不是按字节数。
	EXPECT_EQ(QmTitleShimmerUtf8CharCount("中文"), 2);
	EXPECT_EQ(QmTitleShimmerUtf8CharCount("a中b"), 3);
}

TEST(QmTitleStyle, AnimationTimeAlignsToServerAndWraps)
{
	// 有服务端偏移时对齐到服务端时间，否则用本地时间。
	EXPECT_DOUBLE_EQ(QmTitleAnimationTime(100.0, 5.0, true), 105.0);
	EXPECT_DOUBLE_EQ(QmTitleAnimationTime(100.0, 5.0, false), 100.0);
	// 取模到 3600：所有风格周期（1/2/3/4/6 秒）都能整除它，边界不跳变。
	EXPECT_DOUBLE_EQ(QmTitleAnimationTime(3601.0, 0.0, true), 1.0);
	EXPECT_DOUBLE_EQ(QmTitleAnimationTime(7200.0, 0.0, false), 0.0);
	// 负值落回 [0, 3600)，不能返回负数相位。
	const double Negative = QmTitleAnimationTime(-1.0, 0.0, false);
	EXPECT_GE(Negative, 0.0);
	EXPECT_LT(Negative, 3600.0);
	EXPECT_DOUBLE_EQ(Negative, 3599.0);
}

TEST(QmTitleStyle, ServerTimeOffsetSmoothing)
{
	// 首次估算直接采纳，之后指数平滑，避免单次抖动让相位一跳一跳。
	EXPECT_DOUBLE_EQ(QmTitleUpdateServerTimeOffset(0.0, false, 0.4), 0.4);
	EXPECT_DOUBLE_EQ(QmTitleUpdateServerTimeOffset(0.4, true, 0.4), 0.4);
	const double Smoothed = QmTitleUpdateServerTimeOffset(0.4, true, 1.4);
	EXPECT_GT(Smoothed, 0.4);
	EXPECT_LT(Smoothed, 1.4);
	// 反复测量后收敛到测量值。
	double Current = 0.4;
	for(int Step = 0; Step < 200; ++Step)
		Current = QmTitleUpdateServerTimeOffset(Current, true, 1.4);
	EXPECT_NEAR(Current, 1.4, 1e-6);
}

// [] 内头衔的本地配色档：决定是否用本地颜色/透明度覆盖服务端下发的风格颜色。
TEST(QmTitleColorStyle, ResolvesEachMode)
{
	EXPECT_EQ(ResolveQmTitleColorStyle(0, 0xFF0000, 100, false).m_Mode, EQmTitleColorMode::FOLLOW_SERVER);
	EXPECT_EQ(ResolveQmTitleColorStyle(1, 0xFF0000, 100, false).m_Mode, EQmTitleColorMode::SINGLE);
	EXPECT_EQ(ResolveQmTitleColorStyle(2, 0xFF0000, 100, false).m_Mode, EQmTitleColorMode::RAINBOW);
	// 越界档位退回默认档，而不是留下未初始化状态。
	EXPECT_EQ(ResolveQmTitleColorStyle(-1, 0xFF0000, 100, false).m_Mode, EQmTitleColorMode::FOLLOW_SERVER);
	EXPECT_EQ(ResolveQmTitleColorStyle(99, 0xFF0000, 100, false).m_Mode, EQmTitleColorMode::FOLLOW_SERVER);
}

TEST(QmTitleColorStyle, FollowServerKeepsServerRainbowAndIgnoresLocalColor)
{
	// 跟随服务器档沿用调用方自己解析出的彩虹判定，且不读取本地单色/透明度。
	const SQmTitleColorStyle Rainbow = ResolveQmTitleColorStyle(0, 0xFF0000, 25, true);
	EXPECT_TRUE(Rainbow.m_Rainbow);
	EXPECT_FLOAT_EQ(Rainbow.m_Alpha, 1.0f);

	const SQmTitleColorStyle Plain = ResolveQmTitleColorStyle(0, 0xFF0000, 25, false);
	EXPECT_FALSE(Plain.m_Rainbow);
	EXPECT_FLOAT_EQ(Plain.m_Alpha, 1.0f);
}

TEST(QmTitleColorStyle, SingleModeCarriesColorAndAlpha)
{
	// 颜色配置位（MACRO_CONFIG_COL）内部按 HSLA 打包，因此这里也按同一表示传入。
	const unsigned Packed = ColorHSLA(0.0f, 1.0f, 0.5f, 1.0f).Pack();
	const SQmTitleColorStyle Style = ResolveQmTitleColorStyle(1, Packed, 50, false);
	EXPECT_EQ(Style.m_Mode, EQmTitleColorMode::SINGLE);
	EXPECT_FALSE(Style.m_Rainbow);
	EXPECT_TRUE(Style.m_Color.r > 0.9f) << "r=" << Style.m_Color.r;
	EXPECT_TRUE(Style.m_Color.g < 0.1f) << "g=" << Style.m_Color.g;
	EXPECT_TRUE(Style.m_Color.b < 0.1f) << "b=" << Style.m_Color.b;
	// 透明度并入颜色，调用方不必再乘一次。
	EXPECT_FLOAT_EQ(Style.m_Alpha, 0.5f);
	EXPECT_NEAR(Style.m_Color.a, 0.5f, 0.02f);
}

TEST(QmTitleColorStyle, RainbowModeForcesRainbowRegardlessOfServer)
{
	const SQmTitleColorStyle Style = ResolveQmTitleColorStyle(2, 0x00FF00, 100, false);
	EXPECT_TRUE(Style.m_Rainbow);

	// 透明度越界按 0-100 截断，不能产生 >1 或负数 alpha。
	EXPECT_FLOAT_EQ(ResolveQmTitleColorStyle(1, 0xFFFFFF, -10, false).m_Alpha, 0.0f);
	EXPECT_FLOAT_EQ(ResolveQmTitleColorStyle(1, 0xFFFFFF, 250, false).m_Alpha, 1.0f);
}

TEST(QmTitleRainbowColor, DistributesHueAlongTheLine)
{
	const ColorRGBA First = QmTitleRainbowColor(0, 8, 1.0f);
	const ColorRGBA Middle = QmTitleRainbowColor(4, 8, 1.0f);
	const ColorRGBA Last = QmTitleRainbowColor(7, 8, 1.0f);

	// 相邻字符不同色，整行只换色相。
	const float Diff = std::fabs(First.r - Middle.r) + std::fabs(First.g - Middle.g) + std::fabs(First.b - Middle.b);
	EXPECT_GT(Diff, 0.01f);
	const ColorRGBA aColors[] = {First, Middle, Last};
	for(const ColorRGBA &Color : aColors)
	{
		EXPECT_FLOAT_EQ(Color.a, 1.0f);
		EXPECT_GE(Color.r, 0.0f);
		EXPECT_LE(Color.r, 1.0f);
		EXPECT_GE(Color.g, 0.0f);
		EXPECT_LE(Color.g, 1.0f);
		EXPECT_GE(Color.b, 0.0f);
		EXPECT_LE(Color.b, 1.0f);
	}
}

TEST(QmTitleRainbowColor, HandlesEmptyLineAndClampsAlpha)
{
	// 空行不能产生 NaN（色相应为 0）。
	const ColorRGBA Empty = QmTitleRainbowColor(0, 0, 1.0f);
	EXPECT_TRUE(std::isfinite(Empty.r) && std::isfinite(Empty.g) && std::isfinite(Empty.b));
	EXPECT_FLOAT_EQ(QmTitleRainbowColor(1, 8, -3.0f).a, 0.0f);
	EXPECT_FLOAT_EQ(QmTitleRainbowColor(1, 8, 9.0f).a, 1.0f);
	EXPECT_FLOAT_EQ(QmTitleRainbowColor(1, 8, 0.4f).a, 0.4f);
}

// 引擎侧的默认保持：不传右边缘色时，字符内渐变必须退化成旧行为（左右同色）。
TEST(QmTitleColorStyle, ColorSplitEndColorDefaultsToStartColor)
{
	const ColorRGBA Start(0.25f, 0.5f, 0.75f, 1.0f);
	const STextColorSplit Legacy(3, 2, Start);
	EXPECT_FLOAT_EQ(Legacy.m_ColorEnd.r, Start.r);
	EXPECT_FLOAT_EQ(Legacy.m_ColorEnd.g, Start.g);
	EXPECT_FLOAT_EQ(Legacy.m_ColorEnd.b, Start.b);
	EXPECT_FLOAT_EQ(Legacy.m_ColorEnd.a, Start.a);
	EXPECT_EQ(Legacy.m_CharIndex, 3);
	EXPECT_EQ(Legacy.m_Length, 2);

	// 显式给出右边缘色时两者不同：这才是字符内横向渐变的来源。
	const ColorRGBA End(1.0f, 0.0f, 0.0f, 0.5f);
	const STextColorSplit Gradient(0, 1, Start, End);
	EXPECT_FLOAT_EQ(Gradient.m_Color.r, Start.r);
	EXPECT_FLOAT_EQ(Gradient.m_ColorEnd.r, End.r);
	EXPECT_FLOAT_EQ(Gradient.m_ColorEnd.a, End.a);

	// 逐字符顶点偏移按字符序号承载。
	const STextCharOffset Offset(5, 2.0f, -3.0f);
	EXPECT_EQ(Offset.m_CharIndex, 5);
	EXPECT_FLOAT_EQ(Offset.m_XOffset, 2.0f);
	EXPECT_FLOAT_EQ(Offset.m_YOffset, -3.0f);
}
