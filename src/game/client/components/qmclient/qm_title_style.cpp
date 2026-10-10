// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qm_title_style.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/components/qmclient/qm_title_render.h>
#include <game/client/qm_title_effect.h>

#include <algorithm>
#include <cmath>

namespace
{
	// Calamity Mod 2.2.2 的稀有度色标，0xRRGGBB。
	// 每个数组对应一个 style，出处见 docs/qmclient/title_style_spec.md 的表 A / 表 B / 表 C。
	const unsigned int gs_aTurquoise[] = {0x00FFC8};
	const unsigned int gs_aPureGreen[] = {0x00FF00};
	// CosmicPurple 的显示色是 TextClr(#67428A) 逐通道 ×2 后饱和钳位的结果。
	const unsigned int gs_aCosmicPurple[] = {0xCE84FF};
	// BurnishedAuric 的显示色是 TextClr(#9D6E0B) 逐通道 ×2 后饱和钳位的结果。
	const unsigned int gs_aBurnishedAuric[] = {0xFFDC16};
	const unsigned int gs_aHotPink[] = {0xFF00FF};
	// CalamityRed 的显示色是 TextClr(#F21B1B) 逐通道 ×2 后饱和钳位的结果。
	const unsigned int gs_aCalamityRed[] = {0xFF3636};
	// ExoticRainbow：Ares → Thanatos → Apollo（源码中 Artemis 被注释掉，未启用）。
	const unsigned int gs_aExoticRainbow[] = {0xFF6B6B, 0x7DC4E1, 0xD3EB6C};
	// ExoticRainbow 在 Item.expert 时改用的六色循环，同时速度与位置系数翻倍。
	const unsigned int gs_aExoticRainbowExpert[] = {0xFF4646, 0xFF46FF, 0x4646FF, 0x46FFFF, 0x46FF5A, 0xFFFF46};
	const unsigned int gs_aDarkOrange[] = {0xCC4723};
	const unsigned int gs_aAngelicAlliance[] = {0xFFC437, 0xFFE76B, 0xFFFEF3};
	const unsigned int gs_aContagion[] = {0xCF1175};
	const unsigned int gs_aCrystylCrusher[] = {0x811D95};
	const unsigned int gs_aDemonshade[] = {0xFF8416, 0xDD5507};
	const unsigned int gs_aDraconicDestruction[] = {0xFF4500, 0x8B0000};
	const unsigned int gs_aEarth[] = {0xFF4500, 0x48D1CC, 0x32CD32};
	const unsigned int gs_aEndogenesis[] = {0x83EFFF, 0x2437E6};
	// Eternity：白 / 紫 / 蜂蜜橙 / 粉 / 天蓝 / 亮黄 / 淡紫。
	const unsigned int gs_aEternity[] = {0xBCC0C1, 0x9D64B7, 0xF9A64D, 0xFF69EA, 0x43CCDB, 0xF9F563, 0xECA8F7};
	const unsigned int gs_aFlamsteedRing[] = {0x59E5FF, 0xFFFFFF};
	const unsigned int gs_aIllustriousKnives[] = {0x9AFF97, 0xE497FF};
	const unsigned int gs_aProfanedSoulCrystal[] = {0xFFA600, 0x19FA19};
	const unsigned int gs_aRedSun[] = {0xCC5650, 0xED458D};
	const unsigned int gs_aScarletDevil[] = {0xBF2D47, 0xB9BBFD};
	const unsigned int gs_aShatteredCommunity[] = {0x803E80, 0xF569F5};
	const unsigned int gs_aSomaPrime[] = {0xFFFFFF, 0xD1CC6F};
	const unsigned int gs_aStaffOfBlushie[] = {0x0000FF};
	const unsigned int gs_aSvantechnical[] = {0xDC143C};
	const unsigned int gs_aSylvestaff[] = {0xF9C5FF};
	const unsigned int gs_aTemporalUmbrella[] = {0xD200FF, 0xFFF818};
	const unsigned int gs_aTriactisHammer[] = {0xE3E2B4};
	const unsigned int gs_aDonatorItem[] = {0xFF799C};

	// 风格总表，顺序即设置页列表顺序。
	const SQmTitleStyle gs_aTitleStyles[] = {
		{"turquoise", "Turquoise", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aTurquoise, 1, 0.0f, 1.0f, 0.0f},
		{"pure_green", "Pure Green", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aPureGreen, 1, 0.0f, 1.0f, 0.0f},
		{"cosmic_purple", "Cosmic Purple", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aCosmicPurple, 1, 0.0f, 1.0f, 0.0f},
		{"burnished_auric", "Burnished Auric", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aBurnishedAuric, 1, 0.0f, 1.0f, 0.0f},
		{"hot_pink", "Hot Pink", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aHotPink, 1, 0.0f, 1.0f, 0.0f},
		{"calamity_red", "Calamity Red", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aCalamityRed, 1, 0.0f, 1.0f, 0.0f},
		{"exotic_rainbow", "Exotic Rainbow", EQmTitleStyleMode::PhaseCycle, EQmTitleInterpolation::Hard, gs_aExoticRainbow, 3, 2.0f, 1.0f, 0.005f},
		{"exotic_rainbow_expert", "Exotic Rainbow (Expert)", EQmTitleStyleMode::PhaseCycle, EQmTitleInterpolation::Hard, gs_aExoticRainbowExpert, 6, 2.0f, 2.0f, 0.01f},
		{"dark_orange", "Draedon's Arsenal", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aDarkOrange, 1, 0.0f, 1.0f, 0.0f},
		{"angelic_alliance", "Angelic Alliance", EQmTitleStyleMode::MultiLerp, EQmTitleInterpolation::Smooth, gs_aAngelicAlliance, 3, 2.0f, 1.0f, 0.0f},
		{"contagion", "Contagion", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aContagion, 1, 0.0f, 1.0f, 0.0f},
		{"crystyl_crusher", "Crystyl Crusher", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aCrystylCrusher, 1, 0.0f, 1.0f, 0.0f},
		{"demonshade", "Demonshade", EQmTitleStyleMode::SinSwap, EQmTitleInterpolation::Smooth, gs_aDemonshade, 2, 4.0f, 1.0f, 0.0f},
		{"draconic_destruction", "Draconic Destruction", EQmTitleStyleMode::SinSwap, EQmTitleInterpolation::Smooth, gs_aDraconicDestruction, 2, 4.0f, 1.0f, 0.0f},
		{"earth", "Earth", EQmTitleStyleMode::PhaseCycle, EQmTitleInterpolation::Smooth, gs_aEarth, 3, 2.0f, 1.0f, 0.0f},
		{"endogenesis", "Endogenesis", EQmTitleStyleMode::SinSwap, EQmTitleInterpolation::Smooth, gs_aEndogenesis, 2, 4.0f, 1.0f, 0.0f},
		{"eternity", "Eternity", EQmTitleStyleMode::PhaseCycle, EQmTitleInterpolation::Smooth, gs_aEternity, 7, 2.0f, 1.0f, 0.0f},
		{"flamsteed_ring", "Flamsteed Ring", EQmTitleStyleMode::Piecewise, EQmTitleInterpolation::Smooth, gs_aFlamsteedRing, 2, 1.0f, 1.0f, 0.0f},
		{"illustrious_knives", "Illustrious Knives", EQmTitleStyleMode::SinSwap, EQmTitleInterpolation::Smooth, gs_aIllustriousKnives, 2, 4.0f, 1.0f, 0.0f},
		{"profaned_soul_crystal", "Profaned Soul Crystal", EQmTitleStyleMode::SinSwap, EQmTitleInterpolation::Smooth, gs_aProfanedSoulCrystal, 2, 6.0f, 1.0f, 0.0f},
		{"red_sun", "Red Sun", EQmTitleStyleMode::SinSwap, EQmTitleInterpolation::Smooth, gs_aRedSun, 2, 4.0f, 1.0f, 0.0f},
		{"scarlet_devil", "Scarlet Devil", EQmTitleStyleMode::SinSwap, EQmTitleInterpolation::Smooth, gs_aScarletDevil, 2, 4.0f, 1.0f, 0.0f},
		{"shattered_community", "Shattered Community", EQmTitleStyleMode::SinSwap, EQmTitleInterpolation::Smooth, gs_aShatteredCommunity, 2, 3.0f, 1.0f, 0.0f},
		// Ozzathoth 在源码中直接复用 Shattered Community 的色定义。
		{"ozzathoth", "Ozzathoth", EQmTitleStyleMode::SinSwap, EQmTitleInterpolation::Smooth, gs_aShatteredCommunity, 2, 3.0f, 1.0f, 0.0f},
		{"soma_prime", "Soma Prime", EQmTitleStyleMode::SinSwap, EQmTitleInterpolation::Smooth, gs_aSomaPrime, 2, 4.0f, 1.0f, 0.0f},
		{"staff_of_blushie", "Staff of Blushie", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aStaffOfBlushie, 1, 0.0f, 1.0f, 0.0f},
		{"svantechnical", "Svantechnical", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aSvantechnical, 1, 0.0f, 1.0f, 0.0f},
		{"sylvestaff", "Sylvestaff", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aSylvestaff, 1, 0.0f, 1.0f, 0.0f},
		{"temporal_umbrella", "Temporal Umbrella", EQmTitleStyleMode::SinSwap, EQmTitleInterpolation::Smooth, gs_aTemporalUmbrella, 2, 4.0f, 1.0f, 0.0f},
		{"triactis_hammer", "Triactis' Hammer", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aTriactisHammer, 1, 0.0f, 1.0f, 0.0f},
		{"donator_item", "Donator", EQmTitleStyleMode::Static, EQmTitleInterpolation::Smooth, gs_aDonatorItem, 1, 0.0f, 1.0f, 0.0f},
	};

	const int gs_TitleStyleCount = (int)(sizeof(gs_aTitleStyles) / sizeof(gs_aTitleStyles[0]));

	ColorRGBA LerpRgb(const ColorRGBA &From, const ColorRGBA &To, const float Amount)
	{
		const float Clamped = Amount < 0.0f ? 0.0f : (Amount > 1.0f ? 1.0f : Amount);
		return ColorRGBA(
			From.r + (To.r - From.r) * Clamped,
			From.g + (To.g - From.g) * Clamped,
			From.b + (To.b - From.b) * Clamped,
			From.a + (To.a - From.a) * Clamped);
	}

	// 源码的取模只作用于非负时间；这里让负值输入也落回 [0, Modulo)，避免索引越界。
	float PositiveFmod(const float Value, const float Modulo)
	{
		if(Modulo <= 0.0f)
			return 0.0f;
		const float Result = std::fmod(Value, Modulo);
		return Result < 0.0f ? Result + Modulo : Result;
	}

	int PositiveModulo(const int Value, const int Modulo)
	{
		if(Modulo <= 0)
			return 0;
		const int Result = Value % Modulo;
		return Result < 0 ? Result + Modulo : Result;
	}

	ColorRGBA SampleAt(const SQmTitleStyle &Style, const int Index)
	{
		return QmTitleStyleColorFromHex(Style.m_pColors[PositiveModulo(Index, Style.m_ColorCount)]);
	}
}

ColorRGBA QmTitleStyleColorFromHex(const unsigned int Hex)
{
	return ColorRGBA(
		(float)((Hex >> 16) & 0xFFu) / 255.0f,
		(float)((Hex >> 8) & 0xFFu) / 255.0f,
		(float)(Hex & 0xFFu) / 255.0f,
		1.0f);
}

const SQmTitleStyle *QmTitleStyleById(const char *pId)
{
	if(pId == nullptr || pId[0] == '\0')
		return nullptr;
	for(const SQmTitleStyle &Style : gs_aTitleStyles)
	{
		if(str_comp(Style.m_pId, pId) == 0)
			return &Style;
	}
	return nullptr;
}

const SQmTitleStyle *QmTitleStyleByIndex(const int Index)
{
	if(Index < 0 || Index >= gs_TitleStyleCount)
		return nullptr;
	return &gs_aTitleStyles[Index];
}

int QmTitleStyleCount()
{
	return gs_TitleStyleCount;
}

EQmTitleInterpolation QmTitleStyleSourceInterpolation(const SQmTitleStyle &Style)
{
	return Style.m_Interpolation;
}

float QmTitleStyleEffectivePhasePerPx(const SQmTitleStyle &Style, const float Override)
{
	return Override > 0.0f ? Override : Style.m_PhasePerPx;
}

double QmTitleAnimationTime(const double LocalTime, const double ServerTimeOffset, const bool OffsetValid)
{
	// 3600 是 1、2、3、4、6 的公倍数，正好覆盖全部风格的周期，取模处不会出现相位跳变。
	constexpr double WrapPeriod = 3600.0;
	const double Aligned = OffsetValid ? LocalTime + ServerTimeOffset : LocalTime;
	const double Wrapped = std::fmod(Aligned, WrapPeriod);
	return Wrapped < 0.0 ? Wrapped + WrapPeriod : Wrapped;
}

double QmTitleUpdateServerTimeOffset(const double Current, const bool CurrentValid, const double Measured)
{
	if(!CurrentValid)
		return Measured;
	constexpr double Alpha = 0.2;
	return Current + (Measured - Current) * Alpha;
}

float QmTitleStyleBobPadding(const SQmTitleBobStyle &Bob, const float PixelSize, const float FontSize)
{
	if(Bob.m_Amplitude == 0.0f || Bob.m_WaveLength <= 0.0f)
		return 0.0f;

	float Padding = std::fabs(Bob.m_Amplitude);
	// 字形围绕中心缩放；按完整字号的增长量预留，给描边与字形外延留出余量。
	if(Bob.m_Mode == EQmTitleMotionMode::POP)
		Padding *= maximum(FontSize, 0.0f) * 0.1f;
	else if(Bob.m_QuantizeStep > 0.0f)
		Padding = (float)(std::nearbyint((double)Padding / (double)Bob.m_QuantizeStep) * (double)Bob.m_QuantizeStep);
	return PixelSize > 0.0f ? std::ceil(Padding / PixelSize) * PixelSize : Padding;
}

float QmTitleStyleBobOffset(const SQmTitleBobStyle &Bob, const float TimeSec, const float PixelX)
{
	if(Bob.m_Mode == EQmTitleMotionMode::POP || Bob.m_Amplitude == 0.0f || Bob.m_WaveLength <= 0.0f)
		return 0.0f;

	// FNA 的 MathHelper.TwoPi 是 float 常量，与 Calamity 的取法保持一致。
	const double TwoPi = (double)6.2831855f;
	// 与 Calamity 的 Wavy 同构：dy = sin(time * freq + x * (TwoPi / wavelength)) * amplitude
	// m_Speed 即 freq（弧度/秒），PixelX 为字符左边缘相对行首的像素偏移。
	const double Phase = (double)TimeSec * (double)Bob.m_Speed + (double)PixelX * (TwoPi / (double)Bob.m_WaveLength);
	const double Offset = (double)Bob.m_Amplitude * std::sin(Phase);
	if(Bob.m_QuantizeStep <= 0.0f)
		return (float)Offset;
	// 吸附到整数像素：小幅度下波形会长时间停在同一像素再跳变，观感类似掉帧，
	// 因此默认不量化，由调用方按字体清晰度与平滑度的取舍决定。
	return (float)(std::nearbyint(Offset / (double)Bob.m_QuantizeStep) * (double)Bob.m_QuantizeStep);
}

float QmTitleStyleBobScale(const SQmTitleBobStyle &Bob, const float TimeSec, const float PixelX)
{
	if(Bob.m_Mode != EQmTitleMotionMode::POP || Bob.m_Amplitude <= 0.0f || Bob.m_WaveLength <= 0.0f)
		return 1.0f;

	const double TwoPi = (double)6.2831855f;
	const double Phase = (double)TimeSec * (double)Bob.m_Speed - (double)PixelX * (TwoPi / (double)Bob.m_WaveLength);
	// 正半波收窄成一道凸起，经过的字平滑放大再复原；每档最多增加 10%。
	const double Crest = maximum(std::cos(Phase), 0.0);
	return 1.0f + (float)((double)Bob.m_Amplitude * 0.1 * Crest * Crest * Crest * Crest);
}

ColorRGBA QmTitleStyleSample(const SQmTitleStyle &Style, const float TimeSec, const float PixelX)
{
	// 默认档位是平滑插值：源码里 ExoticRainbow 的插值因子被 MathF.Round 量化成硬切换，
	// 需要逐字节还原时才显式传 Hard（源码原值可由 QmTitleStyleSourceInterpolation 取得）。
	return QmTitleStyleSampleWithInterpolation(Style, TimeSec, PixelX, EQmTitleInterpolation::Smooth);
}

ColorRGBA QmTitleStyleSampleWithInterpolation(const SQmTitleStyle &Style, const float TimeSec, const float PixelX, const EQmTitleInterpolation Interpolation)
{
	if(Style.m_pColors == nullptr || Style.m_ColorCount <= 0)
		return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	// 周期非正会让相位变成 inf/NaN，最终写进顶点色通道是未定义行为；表项填错时退回首色。
	if(Style.m_Mode != EQmTitleStyleMode::Static && Style.m_PeriodSec <= 0.0f)
		return SampleAt(Style, 0);

	const int Count = Style.m_ColorCount;
	const float Time = TimeSec * Style.m_TimeScale;
	// 逐字符相位：Calamity 用字符左边缘相对行首的累计像素宽度参与相位计算。
	const float Rate = Time + PixelX * Style.m_PhasePerPx;

	switch(Style.m_Mode)
	{
	case EQmTitleStyleMode::Static:
		return SampleAt(Style, 0);

	case EQmTitleStyleMode::SinSwap:
	{
		// CalamityUtils.ColorSwap：amount = (sin(TwoPi / seconds * time) + 1) * 0.5
		// FNA 的 MathHelper.TwoPi 是 float 常量，C# 里先转 double 再运算，这里保持一致。
		// 用 Rate 而不是 Time：相位系数为 0 时两者相等，覆盖后才产生逐字符光带。
		const double TwoPi = (double)6.2831855f;
		const float Amount = (float)((std::sin(TwoPi / (double)Style.m_PeriodSec * (double)Rate) + 1.0) * 0.5);
		return LerpRgb(SampleAt(Style, 0), SampleAt(Style, 1), Amount);
	}

	case EQmTitleStyleMode::MultiLerp:
	{
		// CalamityUtils.MulticolorLerp：increment %= 0.999f，再按色标数取整与插值。
		const float Increment = PositiveFmod(Rate / Style.m_PeriodSec, 0.999f);
		const int Index = (int)(Increment * (float)Count);
		const float Amount = PositiveFmod(Increment * (float)Count, 1.0f);
		return LerpRgb(SampleAt(Style, Index), SampleAt(Style, Index + 1), Amount);
	}

	case EQmTitleStyleMode::PhaseCycle:
	{
		// ExoticRainbow / Eternity / Earth 模板：
		//   colorIndex = (int)(rate / period % n)
		//   amount = rate % period > period / 2 ? 1 : rate % (period / 2)
		// 源码中周期与阈值硬编码为 2 秒与 1 秒，此处按半个周期表达。
		const float HalfPeriod = Style.m_PeriodSec * 0.5f;
		const int Index = (int)PositiveFmod(Rate / Style.m_PeriodSec, (float)Count);
		float Amount;
		if(PositiveFmod(Rate, Style.m_PeriodSec) > HalfPeriod)
			Amount = 1.0f;
		else
		{
			const float HalfPhase = PositiveFmod(Rate, HalfPeriod);
			// 源码用 C# 的 MathF.Round，它是银行家舍入（.5 进到偶数）；
			// std::nearbyint 在默认舍入模式下语义一致，std::round 则会偏离。
			Amount = Interpolation == EQmTitleInterpolation::Hard ? std::nearbyint(HalfPhase) : HalfPhase;
		}
		return LerpRgb(SampleAt(Style, Index), SampleAt(Style, Index + 1), Amount);
	}

	case EQmTitleStyleMode::Piecewise:
	{
		// FlamsteedRing：0.0-0.6 纯色 → 0.6-0.8 渐到第二色 → 0.8-1.0 渐回第一色。
		const float Position = PositiveFmod(Rate, 1.0f);
		if(Position < 0.6f)
			return SampleAt(Style, 0);
		if(Position < 0.8f)
			return LerpRgb(SampleAt(Style, 0), SampleAt(Style, 1), (Position - 0.6f) / 0.2f);
		return LerpRgb(SampleAt(Style, 1), SampleAt(Style, 0), (Position - 0.8f) / 0.2f);
	}
	}

	return SampleAt(Style, 0);
}

// 掠光与配色正交：它只按「字符序号 / 字符总数」分配相位，因此和风格表、调色板都无关，
// 放在风格模块里与 QmTitleStyleSample 并列，测试也就能直接覆盖（不需要链上绘制实现）。
float QmTitleShimmerFactor(const SQmTitleShimmer &Shimmer, const int CharIndex, const int CharCount, const float TimeSec)
{
	if(!Shimmer.m_Enabled || CharCount <= 0 || Shimmer.m_Speed <= 0.0f || Shimmer.m_DutyCycle <= 0.0f || Shimmer.m_Amount <= 0.0f)
		return 0.0f;

	// 位置归一化到 [0, 1)：光带位置由「第几个字」决定，因此头衔长短不影响扫过速度。
	const float CharPhase = (float)CharIndex / (float)CharCount;
	const float TimePhase = TimeSec * Shimmer.m_Speed / 100.0f;
	const float Phase = std::fmod(CharPhase + TimePhase, 1.0f);
	// 相位回绕时高光窗留在负数侧，否则最后一个字符会突然整片变亮。
	// DutyCycle 表示高光占行宽比例，窗宽即 Duty，半宽为 Duty/2。
	const float Duty = std::clamp(Shimmer.m_DutyCycle, 0.01f, 0.99f);
	const float HalfDuty = Duty * 0.5f;
	const float Distance = Phase < 0.5f ? Phase : Phase - 1.0f;
	if(std::fabs(Distance) > HalfDuty)
		return 0.0f;

	// 余弦窗代替线性窗：起止平滑，观感是镜面掠过而不是一条硬边扫过。
	// 结果夹到 [0, Amount]，避免 cos(±π/2) 浮点噪声给出极小负值。
	const float Window = std::cos(Distance / HalfDuty * (float)pi * 0.5f) * Shimmer.m_Amount;
	return std::clamp(Window, 0.0f, Shimmer.m_Amount);
}

int QmTitleShimmerUtf8CharCount(const char *pText)
{
	int Count = 0;
	const char *pCurrent = pText;
	while(*pCurrent != '\0')
	{
		const char *pNext = pCurrent;
		if(str_utf8_decode(&pNext) <= 0)
			break;
		++Count;
		pCurrent = pNext;
	}
	return Count;
}

SQmTitleShimmer QmTitleShimmerFromConfig()
{
	SQmTitleShimmer Shimmer;
	// 掠光属于抛光档的一部分；速度设 0 也能单独关掉。
	Shimmer.m_Enabled = g_Config.m_QmTitleEffect == (int)EQmTitleEffect::QM_TITLE_EFFECT_POLISHED && g_Config.m_QmTitleShimmerSpeed > 0;
	Shimmer.m_Speed = (float)std::clamp(g_Config.m_QmTitleShimmerSpeed, 0, 400);
	return Shimmer;
}
