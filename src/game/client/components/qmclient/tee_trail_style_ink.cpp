#include "tee_trail_style_common.h"

#include <base/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Ink / Paper Bloom（水墨：宣纸漾墨）
// 由下至上一共四层普通混合，模拟湿墨落在宣纸上的洇染：
//   湿纸晕  —— 极宽的暖白洇痕，像水在宣纸上漾开的湿边，也保证深色地图上墨迹有浅色承托；
//   宣纸底  —— 略窄的暖白纸面，把墨体与地图隔开；
//   湿墨晕  —— 宽羽化、低透明度的洇染晕（对应 HTML 的 layer0），让墨体边缘"化"开；
//   松烟墨体 —— 近黑笔心（对应 HTML 的 layer1），笔压噪声形成粗细节律，干笔处退成细墨丝。
// 墨体之上再叠加：沿切向的浅色飞白细丝（笔毛分叉露出纸色）、按弧长锚点确定性生成的
// 高速墨点，以及笔锋处的朱红方印（普通混合印面 + 加法柔光）。
// 所有形状只由采样字段、Seed 与累计弧长推导；头尾推进不会重新播种，也不读取时钟。
namespace
{
	using namespace qm_tee_trail;
	using namespace qm_tee_trail::style_detail;

	// 宣纸暖白、松烟墨、笔毛飞白、朱砂印泥。
	constexpr unsigned PALETTE_PAPER = 0xf2e9d8u;
	constexpr unsigned PALETTE_INK = 0x1a1712u;
	constexpr unsigned PALETTE_DRY = 0x3a332au;
	constexpr unsigned PALETTE_SEAL = 0xb4271eu;

	// 普通混合层的参数：宽度倍率、干笔宽度/透明度收缩、基础透明度、柔边比例、
	// 左右边缘不对称、起笔透明度下限，以及整体宽度噪声与边缘噪声的世界尺度。
	struct SInkLayer
	{
		float m_WidthScale;
		float m_DryWidth;
		float m_DryAlpha;
		float m_Alpha;
		float m_Softness;
		float m_Asymmetry;
		float m_AlphaLift;
		float m_WaveScale;
		float m_EdgeScale;
	};

	constexpr SInkLayer s_aInkLayers[] = {
		{2.05f, 0.00f, 0.00f, 0.120f, 0.72f, 0.30f, 0.62f, 40.0f, 30.0f}, // 湿纸晕
		{1.55f, 0.06f, 0.08f, 0.300f, 0.52f, 0.24f, 0.58f, 27.0f, 18.0f}, // 宣纸底
		{1.38f, 0.14f, 0.20f, 0.340f, 0.56f, 0.20f, 0.52f, 19.0f, 11.0f}, // 湿墨晕
		{1.00f, 0.46f, 0.30f, 1.000f, 0.32f, 0.22f, 0.50f, 13.0f, 4.6f}, // 松烟墨体
	};

	// 每个采样点的水墨参数；与颜色、Preset 无关，两种配色共用同一份几何。
	struct SInkPoint
	{
		vec2 m_Center;
		float m_Body; // 墨体标称半宽（含速度与笔压节律）
		float m_Dry; // 干笔强度 0..1（左右平均，用于宽层与透明度）
		float m_DryL; // 左侧干笔强度：与右侧错相，断笔边缘不齐整
		float m_DryR;
		float m_Lift; // 笔尖起笔包络 0..1
		float m_Sine; // 沿弧长的正弦包络
		float m_Alpha; // 采样透明度
	};

	// 墨体标称半宽：快笔更细，两个频段的笔压噪声叠出 2~3 倍的粗细节律。
	float InkBodyWidth(const SStyleSample &S, unsigned Seed)
	{
		const float Pressure = 0.50f + 0.50f * std::abs(Noise(S.m_Distance / 11.0, Seed + 17u));
		const float Rhythm = 0.50f + 0.50f * std::abs(Noise(S.m_Distance / 37.0, Seed + 23u));
		const float Energy = std::clamp(S.m_Energy, 0.0f, 1.0f);
		return std::clamp(S.m_Width, 0.0f, 1024.0f) * mix(0.46f, 0.80f, Pressure) * mix(0.78f, 1.05f, Rhythm) * mix(1.0f, 0.84f, Energy);
	}

	// 干笔断笔：沿累计弧长周期性收笔，湿墨在此处退成细墨丝，透出纸底。
	float DryWave(double Distance, unsigned Seed)
	{
		const double Phase = Distance / 79.0 + double(Random(Seed + 131u));
		const float T = float(Phase - std::floor(Phase));
		return Smooth((T - 0.28f) / 0.09f) * (1.0f - Smooth((T - 0.44f) / 0.10f));
	}

	// 断笔强度随更长的弧长起伏：有的周期几乎不断，有的整整抽干，避免节拍器等距。
	float DryGain(double Distance, unsigned Seed)
	{
		return 0.40f + 0.60f * std::abs(Noise(Distance / 230.0, Seed + 203u));
	}

	// 笔尖起笔包络：头部收成窄锋，向后快速铺开，避免墨迹在 Tee 脚下突然截断。
	float HeadBloom(float Head, float Width, float Scale)
	{
		return Smooth(Head / std::max(0.8f, Width * Scale));
	}

	// 双倍频边缘噪声：低频大起伏 + 高频细毛边，让湿边像纸纤维而不是多边形。
	float EdgeNoise(double Distance, unsigned Seed)
	{
		return Noise(Distance / 9.0, Seed) * 0.62f + Noise(Distance / 3.2, Seed + 31u) * 0.38f;
	}

	// 以 Axis/Across 为轴的小四边形（墨点、印章面），顶点颜色一致。
	void InkDot(std::vector<SQuad> &vOut, vec2 Center, vec2 Axis, vec2 Across, float Radius, ColorRGBA Color, bool Additive)
	{
		if(!(Radius > 0.0001f))
			return;
		Quad(vOut, Center - (Axis + Across) * Radius, Center + (Axis - Across) * Radius,
			Center + (Axis + Across) * Radius, Center + (Across - Axis) * Radius, Color, Color, Color, Color, Additive);
	}

	// 纸张色：宣纸暖白；自定义颜色时向白色提亮，保证深色地图上仍是浅色承托。
	ColorRGBA PaperColor(const SStyleSample &S, bool Preset)
	{
		return Preset ? ColorRGBA(PALETTE_PAPER) : Tint(S.m_Tint, ColorRGBA(0xffffffu), 0.70f);
	}

	// 墨色：松烟近黑；自定义颜色时压向黑色，保持"墨是暗色"的前提。
	ColorRGBA InkColor(const SStyleSample &S, bool Preset)
	{
		return Preset ? ColorRGBA(PALETTE_INK) : Tint(S.m_Tint, ColorRGBA(0x000000u), 0.86f);
	}

	// 飞白色：干笔笔毛露出的纸色干痕，比墨体明显更浅。
	ColorRGBA DryColor(const SStyleSample &S, bool Preset)
	{
		return Preset ? Tint(ColorRGBA(PALETTE_PAPER), ColorRGBA(PALETTE_DRY), 0.42f) :
				Tint(PaperColor(S, false), Tint(S.m_Tint, ColorRGBA(0x000000u), 0.55f), 0.42f);
	}

	// 印泥色：朱红；自定义颜色时提亮 Tee 颜色。
	ColorRGBA SealColor(const SStyleSample &S, bool Preset)
	{
		return Preset ? ColorRGBA(PALETTE_SEAL) : Tint(S.m_Tint, ColorRGBA(0xffffffu), 0.28f);
	}
}

void qm_tee_trail::style_detail::BuildInk(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut)
{
	if(pSamples == nullptr || Count < 2)
		return;
	Count = std::min(Count, static_cast<size_t>(MAX_RENDER_POINTS));
	if(!std::isfinite(PixelSize))
		return;
	PixelSize = std::clamp(PixelSize, 0.025f, 8.0f);
	(void)Width; // 世界宽度已在上游乘入 m_Width，这里只做形态调制

	// 输入兜底：异常采样整段放弃，避免把 NaN/Inf 写进顶点。
	float MaxAlpha = 0.0f;
	for(size_t i = 0; i < Count; ++i)
	{
		const SStyleSample &S = pSamples[i];
		if(!std::isfinite(S.m_Pos.x) || !std::isfinite(S.m_Pos.y) ||
			!std::isfinite(S.m_Normal.x) || !std::isfinite(S.m_Normal.y) ||
			!std::isfinite(S.m_Width) || !std::isfinite(S.m_Alpha) ||
			!std::isfinite(S.m_Head) || !std::isfinite(S.m_Distance) ||
			!std::isfinite(S.m_Energy) || !std::isfinite(S.m_Remaining) ||
			!std::isfinite(S.m_Tint.r) || !std::isfinite(S.m_Tint.g) ||
			!std::isfinite(S.m_Tint.b) || !std::isfinite(S.m_Tint.a))
			return;
		MaxAlpha = std::max(MaxAlpha, S.m_Alpha);
	}
	// 源透明度趋近零时不产生任何四边形。
	if(MaxAlpha < 0.001f)
		return;

	const float Total = std::max(0.01f, pSamples[Count - 1].m_Head);
	std::array<SInkPoint, MAX_RENDER_POINTS> aInk;
	for(size_t i = 0; i < Count; ++i)
	{
		const SStyleSample &S = pSamples[i];
		const float Head = std::max(0.0f, S.m_Head);
		const float Progress = std::clamp(Head / Total, 0.0f, 1.0f);
		const float RawWidth = std::clamp(S.m_Width, 0.0f, 1024.0f);
		const float Body = InkBodyWidth(S, Seed);
		const float Lift = HeadBloom(Head, RawWidth, 0.72f);
		// 中线沿高频噪声轻微摆动，模拟笔锋在宣纸纤维上的行走。
		const vec2 Center = S.m_Pos + S.m_Normal * (Body * 0.16f * Noise(S.m_Distance / 15.0, Seed + 5u) * Lift);
		// 正弦包络：笔触中段最饱满，头尾自然收势（尾端另有上游 taper）。
		const float Sine = 0.75f + 0.25f * std::sin(Progress * pi);
		// 左右断笔错相：墨体两侧在不同位置收笔，断口被"撕"开而不是整齐截断。
		const float Gain = DryGain(S.m_Distance, Seed + 207u);
		const float Dry = DryWave(S.m_Distance, Seed + 41u) * Gain;
		const float DryL = DryWave(S.m_Distance + 13.0, Seed + 43u) * Gain;
		const float DryR = DryWave(S.m_Distance - 19.0, Seed + 47u) * Gain;
		aInk[i] = {Center, Body, Dry, DryL, DryR, Lift, Sine, std::clamp(S.m_Alpha, 0.0f, 1.0f)};
	}

	std::array<SBandPoint, MAX_RENDER_POINTS> aBand;
	// 三层宽而软的洇染层按弧长抽稀到约 112 点；墨体保留全分辨率以留住笔压与飞白细节。
	const size_t WideStride = std::max<size_t>(1, (Count + 103) / 104);

	// ------------------------------------------------------------------
	// 普通混合第一段：湿纸晕 → 宣纸底 → 湿墨晕 → 墨体
	// ------------------------------------------------------------------
	for(int Layer = 0; Layer < 4; ++Layer)
	{
		const SInkLayer &Config = s_aInkLayers[Layer];
		const bool Body = Layer == 3;
		const bool Paper = Layer < 2;
		const size_t Stride = Body ? 1 : WideStride;
		size_t Points = 0;
		const auto Append = [&](size_t Index) {
			const SStyleSample &S = pSamples[Index];
			const SInkPoint &P = aInk[Index];
			// 宽层用更低频的噪声控制整体宽度起伏，墨体用更高频的笔压。
			const float Wave = 0.90f + 0.20f * std::abs(Noise(S.m_Distance / Config.m_WaveScale, Seed + 61u + unsigned(Layer) * 37u));
			const float MulL = 1.0f - Config.m_DryWidth * (Body ? P.m_DryL : P.m_Dry);
			const float MulR = 1.0f - Config.m_DryWidth * (Body ? P.m_DryR : P.m_Dry);
			const float Shared = P.m_Body * Config.m_WidthScale * Wave * P.m_Sine * P.m_Lift;
			const float HalfL = Shared * MulL;
			const float HalfR = Shared * MulR;
			const float Left = std::max(0.0f, HalfL * (1.0f + Config.m_Asymmetry * EdgeNoise(S.m_Distance, Seed + 97u + unsigned(Layer) * 53u)));
			const float Right = std::max(0.0f, HalfR * (1.0f + Config.m_Asymmetry * EdgeNoise(S.m_Distance + 37.0, Seed + 149u + unsigned(Layer) * 53u)));
			const float AlphaNoise = 0.90f + 0.10f * std::abs(Noise(S.m_Distance / 17.0, Seed + 173u + unsigned(Layer) * 71u));
			float Alpha = P.m_Alpha * mix(Config.m_AlphaLift, 1.0f, P.m_Lift) * Config.m_Alpha * AlphaNoise;
			if(!Paper)
			{
				const float DryAvg = Body ? 0.5f * (P.m_DryL + P.m_DryR) : P.m_Dry;
				Alpha *= 1.0f - Config.m_DryAlpha * DryAvg;
			}
			const ColorRGBA Color = Paper ? PaperColor(S, Preset) : InkColor(S, Preset);
			aBand[Points++] = {P.m_Center, Left, Right, Color.WithAlpha(std::clamp(Alpha, 0.0f, 1.0f))};
		};
		for(size_t i = 0; i < Count; i += Stride)
			Append(i);
		// 抽稀后补上最尾端，保证墨迹在尾端完整收势。
		if((Count - 1) % Stride != 0)
			Append(Count - 1);
		if(Points < 2)
			continue;
		Band(vOut, aBand.data(), Points, Config.m_Softness, PixelSize, false);
	}

	// ------------------------------------------------------------------
	// 普通混合第二段：飞白细丝（干笔笔毛露出纸色的浅色干痕）
	// ------------------------------------------------------------------
	int FlyAnchors = 0;
	Anchors(pSamples, Count, 20.0f, Random(Seed + 151u) * 20.0f, 14, [&](const SStyleSample &S, unsigned Id) {
		if(FlyAnchors >= 14)
			return;
		++FlyAnchors;
		const float Dry = DryWave(S.m_Distance, Seed + 41u) * DryGain(S.m_Distance, Seed + 207u);
		const float Body = InkBodyWidth(S, Seed);
		const float Lift = HeadBloom(std::max(0.0f, S.m_Head), std::clamp(S.m_Width, 0.0f, 1024.0f), 0.5f);
		const vec2 Normal = Unit(S.m_Normal);
		const vec2 Along = Tangent(S);
		const int Stripes = 3 + int(Hash(Id * 5u + Seed + 191u) & 1u);
		const ColorRGBA Color = DryColor(S, Preset);
		for(int Stripe = 0; Stripe < Stripes; ++Stripe)
		{
			const unsigned H = Id * 17u + unsigned(Stripe) * 131u + Seed + 211u;
			const float Across = (((float(Stripe) + 0.5f) / float(Stripes)) - 0.5f + 0.22f * (Random(H + 6u) - 0.5f)) * 1.5f * Body;
			const float Length = Body * (0.7f + 2.1f * Random(H));
			const vec2 Start = S.m_Pos + Normal * Across - Along * (Length * 0.5f);
			const vec2 End = Start + Along * Length + Normal * (Body * 0.22f * (Random(H + 1u) - 0.5f));
			const float StrokeWidth = std::max(PixelSize * 0.40f, Body * 0.030f);
			const float Alpha = std::clamp(S.m_Alpha, 0.0f, 1.0f) * Lift * (0.18f + 0.32f * Dry) * (0.45f + 0.55f * Random(H + 2u));
			Stroke(vOut, Start, End, StrokeWidth, StrokeWidth * 0.55f, Color.WithAlpha(Alpha), PixelSize, false);
		}
	});

	// ------------------------------------------------------------------
	// 普通混合第三段：洇染触须（湿墨沿宣纸纤维向两侧渗出的细墨丝）
	// ------------------------------------------------------------------
	int TendrilAnchors = 0;
	Anchors(pSamples, Count, 46.0f, Random(Seed + 281u) * 46.0f, 7, [&](const SStyleSample &S, unsigned Id) {
		if(TendrilAnchors >= 7)
			return;
		++TendrilAnchors;
		const float Body = InkBodyWidth(S, Seed);
		const float Lift = HeadBloom(std::max(0.0f, S.m_Head), std::clamp(S.m_Width, 0.0f, 1024.0f), 0.6f);
		const vec2 Normal = Unit(S.m_Normal);
		const vec2 Along = Tangent(S);
		const ColorRGBA Ink = InkColor(S, Preset);
		const int Fibers = 1 + int(Hash(Id * 3u + Seed + 293u) & 1u);
		for(int Fiber = 0; Fiber < Fibers; ++Fiber)
		{
			const unsigned H = Id * 23u + unsigned(Fiber) * 419u + Seed + 307u;
			const float Side = Random(H) < 0.5f ? -1.0f : 1.0f;
			const vec2 Root = S.m_Pos + Normal * (Side * Body * (0.70f + 0.25f * Random(H + 1u)));
			const vec2 Tip = Root + Normal * (Side * Body * (0.45f + 1.05f * Random(H + 2u))) +
					 Along * (Body * (Random(H + 3u) - 0.5f) * 1.1f);
			const float Width = std::max(PixelSize * 0.32f, Body * 0.022f);
			const float Alpha = std::clamp(S.m_Alpha, 0.0f, 1.0f) * Lift * (0.30f + 0.30f * Random(H + 4u));
			Stroke(vOut, Root, Tip, Width, Width * 0.35f, Ink.WithAlpha(Alpha), PixelSize, false);
		}
	});

	// ------------------------------------------------------------------
	// 普通混合第四段：高速墨点（按弧长锚点生成，随采样寿命消散）
	// ------------------------------------------------------------------
	int SplatAnchors = 0;
	Anchors(pSamples, Count, 30.0f, Random(Seed + 233u) * 30.0f, 14, [&](const SStyleSample &S, unsigned Id) {
		if(SplatAnchors >= 14)
			return;
		++SplatAnchors;
		const int Drops = int(Hash(Id * 7u + Seed + 257u) % 4u);
		if(Drops == 0)
			return;
		const float Body = InkBodyWidth(S, Seed);
		const vec2 Normal = Unit(S.m_Normal);
		const vec2 Along = Tangent(S);
		const float Energy = std::clamp(S.m_Energy, 0.0f, 1.0f);
		const float Remaining = std::clamp(S.m_Remaining, 0.0f, 1.0f);
		const ColorRGBA PaperBase = PaperColor(S, Preset);
		const ColorRGBA InkBase = InkColor(S, Preset);
		for(int Drop = 0; Drop < Drops; ++Drop)
		{
			const unsigned H = Id * 13u + unsigned(Drop) * 617u + Seed + 269u;
			const float Side = Random(H) < 0.5f ? -1.0f : 1.0f;
			const float Spread = Body * (1.00f + 1.9f * Random(H + 1u));
			const vec2 Center = S.m_Pos + Normal * (Side * Spread) + Along * (Body * (Random(H + 2u) - 0.5f) * 2.0f);
			const float Radius = mix(0.7f, 2.3f, Random(H + 3u)) * (0.60f + 1.05f * Energy) * mix(0.75f, 1.0f, Remaining);
			const float Alpha = std::clamp(S.m_Alpha, 0.0f, 1.0f) * (0.24f + 0.76f * Energy) * mix(0.30f, 1.0f, Remaining) * (0.55f + 0.45f * Random(H + 4u));
			const float Angle = Random(H + 5u) * pi;
			const vec2 HaloAxis = direction(Angle);
			const vec2 HaloAcross = vec2(-HaloAxis.y, HaloAxis.x);
			// 高能段沿切向拉长的墨滴，像甩出的墨点；两片错相叠加，远看浑圆。
			const vec2 StretchAxis = Along * (1.0f + 0.65f * Energy);
			const vec2 StretchAcross = Normal * 0.85f;
			InkDot(vOut, Center, HaloAxis, HaloAcross, Radius * 1.30f, PaperBase.WithAlpha(Alpha * 0.09f), false);
			InkDot(vOut, Center, StretchAxis, StretchAcross, Radius, InkBase.WithAlpha(Alpha * 0.78f), false);
			InkDot(vOut, Center, direction(Angle + pi * 0.25f), direction(Angle + pi * 0.75f), Radius * 0.66f, InkBase.WithAlpha(Alpha * 0.46f), false);
		}
	});

	// ------------------------------------------------------------------
	// 普通混合第五段：笔锋朱红方印；随后是唯一的加法柔光层
	// ------------------------------------------------------------------
	const SStyleSample &HeadSample = pSamples[0];
	const float HeadEnergy = std::clamp(HeadSample.m_Energy, 0.0f, 1.0f);
	const float HeadAlpha = std::clamp(HeadSample.m_Alpha, 0.0f, 1.0f) * mix(0.45f, 1.0f, std::clamp(HeadEnergy * 2.5f, 0.0f, 1.0f));
	if(HeadAlpha > 0.02f)
	{
		const vec2 HeadNormal = Unit(HeadSample.m_Normal);
		const vec2 HeadAlong = Tangent(HeadSample);
		const float Radius = std::clamp(InkBodyWidth(HeadSample, Seed) * 0.72f, PixelSize * 2.2f, 11.0f);
		const vec2 Center = HeadSample.m_Pos + HeadAlong * (Radius * 1.15f) + HeadNormal * (Radius * 0.06f);
		// 印章随笔锋方向略微转动，避免每帧完全正交。
		const float Angle = (Random(Seed + 311u) - 0.5f) * 0.8f;
		const float Cos = std::cos(Angle), Sin = std::sin(Angle);
		const vec2 Axis = HeadAlong * Cos + HeadNormal * Sin;
		const vec2 Across = HeadNormal * Cos - HeadAlong * Sin;
		const ColorRGBA Seal = SealColor(HeadSample, Preset);
		const ColorRGBA PaperBase = PaperColor(HeadSample, Preset);
		// 印面（朱红填色方印）与印心留白（篆刻字口）。
		InkDot(vOut, Center, Axis, Across, Radius, Seal.WithAlpha(HeadAlpha * 0.80f), false);
		InkDot(vOut, Center, Axis, Across, Radius * 0.44f, PaperBase.WithAlpha(HeadAlpha * 0.66f), false);
		// 边框：与印面同四个角点，描出方印轮廓（角点 = Center ±(Axis±Across)·R）。
		const float Rim = std::max(Radius * 0.10f, PixelSize * 0.28f);
		const ColorRGBA RimColor = Tint(Seal, ColorRGBA(0x000000u), 0.30f).WithAlpha(HeadAlpha * 0.90f);
		const vec2 aCorner[4] = {Center - (Axis + Across) * Radius, Center + (Axis - Across) * Radius,
			Center + (Axis + Across) * Radius, Center + (Across - Axis) * Radius};
		for(int Edge = 0; Edge < 4; ++Edge)
			Stroke(vOut, aCorner[Edge], aCorner[(Edge + 1) & 3], Rim, Rim, RimColor, PixelSize, false);
		// 印泥加法柔光：两圈旋转 45° 的菱形叠成八边形，模拟径向衰减的微光，
		// 是唯一的加法混合内容，排在所有普通层之后。
		const vec2 GlowA = Axis * 1.30f, GlowB = Across * 1.30f;
		const vec2 GlowC = direction(Angle + pi * 0.25f) * 1.30f, GlowD = direction(Angle + pi * 0.75f) * 1.30f;
		const vec2 GlowE = Axis * 1.75f, GlowF = Across * 1.75f;
		const vec2 GlowG = direction(Angle + pi * 0.25f) * 1.75f, GlowH = direction(Angle + pi * 0.75f) * 1.75f;
		InkDot(vOut, Center, GlowA, GlowB, Radius, Seal.WithAlpha(HeadAlpha * 0.020f), true);
		InkDot(vOut, Center, GlowC, GlowD, Radius, Seal.WithAlpha(HeadAlpha * 0.020f), true);
		InkDot(vOut, Center, GlowE, GlowF, Radius, Seal.WithAlpha(HeadAlpha * 0.012f), true);
		InkDot(vOut, Center, GlowG, GlowH, Radius, Seal.WithAlpha(HeadAlpha * 0.012f), true);
	}
}
