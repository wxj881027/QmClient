#include "tee_trail_style_common.h"

#include <base/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Aurora: Polar Veil（极光：极夜光幕）
// 视觉参考：桌面演示 01_极光_Aurora.html。演示里五条冷色丝带沿法向做低频正弦起伏形成帘幕，
// 头部叠一段近白的亮核光晕，沿轨迹散布闪烁的微光粒子。本实现把同样的结构拆成世界空间的四边形：
//   1) 普通混合批次 —— 一条很宽的冷色底幕 + 五条丝带的暗面。
//      加法发光层在白亮地图上会被背景抬到饱和而消失，所以必须先用普通混合的冷色把帘幕轮廓压出来；
//      深色地图上这一批只是很淡的冷色垫底，不会抢走发光层的亮度。
//   2) 加法混合批次 —— 一层宽而淡的极光空气光 + 五条发光丝带 + 头部亮核带 + 头部辉光 + 锚点微光。
// 五条丝带的宽度、波幅、频率、相位与时间频率各不相同，叠在一起才有"极光帘幕"的分层褶皱感，
// 而不是一根普通光带。所有相位只由 m_Head（空间）、m_Age（时间）与 Seed 推导：
// 没有时钟、没有全局状态、没有静态计数器，因此同输入同种子逐顶点一致。
namespace
{
	using namespace qm_tee_trail;
	using namespace qm_tee_trail::style_detail;

	// 预设冷色：对应演示里的 hue 160 / 180 / 130 / 200 / 100，保持"冷色极光"方向。
	constexpr unsigned PALETTE_MINT_TEAL = 0x3ee8c0u;
	constexpr unsigned PALETTE_ICE_BLUE = 0x2fc0ffu;
	constexpr unsigned PALETTE_AURORA_GREEN = 0x86e85au;
	constexpr unsigned PALETTE_VIOLET_BLUE = 0x7a68f0u;
	constexpr unsigned PALETTE_MINT_CYAN = 0x7fe8d8u;
	// 极夜里的底幕、空气光与亮核。底幕用中间调冷色：深色地图上它提亮背景，
	// 白亮地图上它压暗背景，两边都能读出帘幕，而不会变成一块黑斑。
	constexpr unsigned PALETTE_NIGHT_BLUE = 0x0d2a44u;
	constexpr unsigned PALETTE_AIR_BLUE = 0x2fa8e8u;
	constexpr unsigned PALETTE_CORE_WHITE = 0xd8fff0u;

	// 丝带层数：与演示一致，固定五条。
	constexpr size_t AURORA_LAYER_COUNT = 5;
	// 底幕、空气光与亮核的截面上限；丝带的截面上限写在每层参数里。
	constexpr size_t AURORA_VEIL_POINTS = 40;
	constexpr size_t AURORA_AIR_POINTS = 40;
	constexpr size_t AURORA_CORE_POINTS = 22;
	// 锚点微光：弧长间距、上限与每个粒子的四边形数。
	constexpr float AURORA_SPARKLE_SPACING = 30.0f;
	constexpr size_t AURORA_SPARKLE_LIMIT = 16;

	struct SAuroraLayer
	{
		float m_HalfWidth; // 丝带半宽（× 采样宽度）
		float m_Amplitude; // 法向波幅（× 采样宽度）
		float m_Bias; // 法向常驻偏置（× 采样宽度）：让五层像帘幕一样摊开而不是全部绕中心线
		float m_Frequency; // 空间频率（弧度 / 世界单位）
		float m_TimeFrequency; // 时间频率（弧度 / 游戏 tick）
		float m_Phase; // 层相位
		float m_Fold; // 折皱噪声频率（1 / 世界单位）
		float m_GlowAlpha; // 加法发光层透明度系数
		float m_BaseAlpha; // 普通暗面透明度系数
		float m_BaseWidth; // 暗面宽度相对发光层的倍数
		float m_Softness; // 发光层柔边比例（越靠外的帘幕越柔）
		unsigned m_GlowCap; // 发光层截面数量上限
		unsigned m_BaseCap; // 暗面截面数量上限
		unsigned m_Preset; // 预设色
		unsigned m_Cold; // 自定义模式下的冷色偏移目标
	};

	// 宽度、波幅与频率逐层放大，形成由内向外铺开的帘幕；中间一条最亮（演示里 alpha 0.35），
	// 其余较暗（0.18），靠叠层叠出亮度而不是单层过曝。
	constexpr SAuroraLayer s_aAuroraLayers[AURORA_LAYER_COUNT] = {
		{0.26f, 1.04f, -0.44f, 0.0070f, 0.024f, 0.00f, 1.0f / 210.0f, 0.78f, 0.28f, 1.12f, 0.46f, 88, 80, PALETTE_MINT_TEAL, 0x9fffd8u},
		{0.38f, 1.61f, -0.22f, 0.0082f, 0.020f, 1.30f, 1.0f / 250.0f, 0.70f, 0.26f, 1.12f, 0.50f, 88, 80, PALETTE_ICE_BLUE, 0xa8e0ffu},
		{0.52f, 2.19f, 0.00f, 0.0094f, 0.027f, 2.60f, 1.0f / 290.0f, 1.05f, 0.34f, 1.10f, 0.54f, 88, 80, PALETTE_AURORA_GREEN, 0xc8ffb0u},
		{0.70f, 2.88f, 0.22f, 0.0106f, 0.018f, 3.90f, 1.0f / 330.0f, 0.42f, 0.24f, 1.14f, 0.64f, 60, 52, PALETTE_VIOLET_BLUE, 0xd0b0ffu},
		{0.90f, 3.57f, 0.46f, 0.0118f, 0.022f, 5.20f, 1.0f / 370.0f, 0.30f, 0.22f, 1.18f, 0.74f, 60, 52, PALETTE_MINT_CYAN, 0xd8fff0u},
	};

	struct SAuroraVeil
	{
		float m_HalfWidth;
		float m_Amplitude;
		float m_Frequency;
		float m_TimeFrequency;
		float m_Phase;
		float m_Fold;
		float m_Alpha;
		float m_Softness;
		unsigned m_Preset;
	};

	// 宽底幕：整条帘幕外扩的冷色空气感，白亮地图上保证能看出形状，深色地图上压暗背景托出发光层。
	constexpr SAuroraVeil s_AuroraVeil = {1.18f, 0.95f, 0.0050f, 0.016f, 0.40f, 1.0f / 260.0f, 0.55f, 0.70f, PALETTE_NIGHT_BLUE};

	inline float AuroraClamp01(float X)
	{
		return std::clamp(X, 0.0f, 1.0f);
	}

	ColorRGBA AuroraScale(ColorRGBA Color, float Scale)
	{
		return ColorRGBA(Color.r * Scale, Color.g * Scale, Color.b * Scale, Color.a);
	}

	// 发光色：预设直接取色板；自定义模式把玩家颜色向该层冷色偏移，越靠外的层越冷。
	ColorRGBA AuroraGlowColor(const SAuroraLayer &Layer, const SStyleSample &S, bool Preset, size_t LayerIndex)
	{
		if(Preset)
			return ColorRGBA(Layer.m_Preset);
		return Tint(S.m_Tint, ColorRGBA(Layer.m_Cold), 0.38f + 0.10f * float(LayerIndex));
	}

	// 暗面：发光色的压暗版本。白亮地图靠五条暗面与宽底幕保住"帘幕"轮廓；
	// 颜色只由 Preset / Tint 决定，透明度与几何走另一条独立计算，所以 Preset 只换颜色。
	ColorRGBA AuroraBaseColor(const SAuroraLayer &Layer, const SStyleSample &S, bool Preset, size_t LayerIndex)
	{
		// 中间调：比深底亮、比白底暗，普通混合在两种背景上都能显出帘幕轮廓。
		if(Preset)
			return AuroraScale(ColorRGBA(Layer.m_Preset), 0.44f);
		return AuroraScale(Tint(S.m_Tint, ColorRGBA(Layer.m_Cold), 0.58f + 0.06f * float(LayerIndex)), 0.50f);
	}

	// 帘幕波形。空间项只由累计弧长决定，时间项只由样本年龄决定；
	// 主波叠加一层噪声调制的二次折皱，避免边缘像规则正弦那样机械。
	float AuroraWave(double Head, float Radians, float Fold, unsigned Seed)
	{
		const float Slow = Noise(Head * double(Fold), Seed);
		const float Fast = Noise(Head * double(Fold) * 2.7 + 11.0, Seed ^ 0x85ebca6bu);
		return (std::sin(Radians) + 0.34f * Fast * std::sin(Radians * 2.13f + Slow * 2.4f)) * (0.72f + 0.28f * Slow);
	}

	// 头部收拢：各层在冲头附近向中心线聚拢，离开头部后逐渐铺开，形成被提起的帘幕起笔。
	float AuroraHeadConverge(const SStyleSample &S)
	{
		return 0.30f + 0.70f * Smooth(S.m_Head / std::max(1.0f, S.m_Width * 2.6f));
	}

	// 一条丝带的截面：暗面（Shadow）与发光面共用同一条中心线与波形，只改变宽度、颜色与透明度，
	// 因此两批混合之间的几何严格对齐，Preset 开关也不影响任何顶点位置。
	void FillAuroraRibbon(const SStyleSample *pSamples, size_t Count, size_t Points, const SAuroraLayer &Layer,
		size_t LayerIndex, bool Preset, bool Shadow, unsigned Seed, SBandPoint *pBand)
	{
		for(size_t i = 0; i < Points; ++i)
		{
			const size_t Index = (i * (Count - 1)) / (Points - 1);
			const SStyleSample &S = pSamples[Index];
			const vec2 Normal = Unit(S.m_Normal, vec2(0.0f, 1.0f));
			const float LocalWidth = std::max(S.m_Width, 0.0f);
			const float Remaining = AuroraClamp01(S.m_Remaining);
			const float Energy = AuroraClamp01(S.m_Energy);
			const float Radians = float(S.m_Head) * Layer.m_Frequency + S.m_Age * Layer.m_TimeFrequency + Layer.m_Phase;
			const float Wave = AuroraWave(double(S.m_Head), Radians, Layer.m_Fold, Seed + unsigned(LayerIndex) * 101u);
			const float Converge = AuroraHeadConverge(S);
			const vec2 Center = S.m_Pos + Normal * (LocalWidth * (Layer.m_Bias + Layer.m_Amplitude * Wave) * Converge * (0.80f + 0.20f * Energy));
			if(Shadow)
			{
				const float Half = LocalWidth * (Layer.m_HalfWidth * Layer.m_BaseWidth + 0.08f) * (0.86f + 0.14f * Remaining);
				const ColorRGBA Color = AuroraBaseColor(Layer, S, Preset, LayerIndex);
				// 暗面在头部快速淡入：头部由亮核负责，避免起点留下深色断口。
				const float HeadFade = Smooth(S.m_Head / std::max(1.0f, LocalWidth * 0.9f));
				pBand[i] = {Center, Half, Half, Color.WithAlpha(S.m_Alpha * Layer.m_BaseAlpha * (0.82f + 0.18f * Remaining) * HeadFade)};
			}
			else
			{
				// 宽度的轻微呼吸与亮度闪烁都跟着同一个相位走，像帘幕被气流慢慢拧动。
				const float Shimmer = 0.5f + 0.5f * std::sin(Radians * 0.37f + float(LayerIndex) * 1.7f);
				// 沿弧长的明暗带：所有层共用同一条相位，形成同步的帘幕竖褶与极光射线；
				// 层之间只保留极小的错位，避免看起来像同一张塑料布。
				const float Curtain = 0.5f + 0.5f * Noise(S.m_Head / 300.0, Seed + 211u);
				const float Ray = 0.5f + 0.5f * Noise(S.m_Head / 105.0 + float(LayerIndex) * 0.09, Seed + 317u);
				const float Half = LocalWidth * Layer.m_HalfWidth * (0.86f + 0.14f * Ray) * (0.80f + 0.20f * std::sin(Radians * 0.61f + Wave * 1.7f)) *
						   (0.82f + 0.18f * Remaining);
				// 头部亮核：靠头部的一段把颜色推向近白，模拟演示里最亮的核心。
				const float HeadGlow = 1.0f - Smooth(S.m_Head / std::max(1.0f, LocalWidth * 2.4f));
				const ColorRGBA Color = Tint(AuroraGlowColor(Layer, S, Preset, LayerIndex), ColorRGBA(PALETTE_CORE_WHITE), 0.30f * HeadGlow);
				const float Alpha = S.m_Alpha * Layer.m_GlowAlpha * (0.58f + 0.42f * Curtain) * (0.68f + 0.32f * Ray) * (0.78f + 0.28f * Shimmer) *
						    (0.78f + 0.30f * Energy);
				pBand[i] = {Center, Half, Half, Color.WithAlpha(Alpha)};
			}
		}
	}

	// 宽底幕截面：只做慢速摆动，保持大块冷色，不参与丝带的细节褶皱。
	void FillAuroraVeil(const SStyleSample *pSamples, size_t Count, size_t Points, bool Preset, unsigned Seed, SBandPoint *pBand)
	{
		for(size_t i = 0; i < Points; ++i)
		{
			const size_t Index = (i * (Count - 1)) / (Points - 1);
			const SStyleSample &S = pSamples[Index];
			const vec2 Normal = Unit(S.m_Normal, vec2(0.0f, 1.0f));
			const float LocalWidth = std::max(S.m_Width, 0.0f);
			const float Remaining = AuroraClamp01(S.m_Remaining);
			const float Energy = AuroraClamp01(S.m_Energy);
			const float Radians = float(S.m_Head) * s_AuroraVeil.m_Frequency + S.m_Age * s_AuroraVeil.m_TimeFrequency + s_AuroraVeil.m_Phase;
			const float Wave = AuroraWave(double(S.m_Head), Radians, s_AuroraVeil.m_Fold, Seed ^ 0x9e3779b9u);
			const vec2 Center = S.m_Pos + Normal * (LocalWidth * s_AuroraVeil.m_Amplitude * Wave * (0.85f + 0.15f * Energy));
			const float Half = LocalWidth * s_AuroraVeil.m_HalfWidth * (0.84f + 0.16f * Remaining);
			const ColorRGBA Color = Preset ? ColorRGBA(s_AuroraVeil.m_Preset) :
							 AuroraScale(Tint(S.m_Tint, ColorRGBA(s_AuroraVeil.m_Preset), 0.66f), 0.74f);
			pBand[i] = {Center, Half, Half, Color.WithAlpha(S.m_Alpha * s_AuroraVeil.m_Alpha * (0.86f + 0.14f * Remaining))};
		}
	}

	// 极光空气光：比五条丝带更宽更淡的加法光雾，深色地图上给帘幕一层发光的空气感。
	void FillAuroraAir(const SStyleSample *pSamples, size_t Count, size_t Points, bool Preset, unsigned Seed, SBandPoint *pBand)
	{
		for(size_t i = 0; i < Points; ++i)
		{
			const size_t Index = (i * (Count - 1)) / (Points - 1);
			const SStyleSample &S = pSamples[Index];
			const vec2 Normal = Unit(S.m_Normal, vec2(0.0f, 1.0f));
			const float LocalWidth = std::max(S.m_Width, 0.0f);
			const float Remaining = AuroraClamp01(S.m_Remaining);
			const float Radians = float(S.m_Head) * 0.0042f + S.m_Age * 0.013f + 1.9f;
			const float Wave = AuroraWave(double(S.m_Head), Radians, 1.0f / 300.0f, Seed + 509u);
			const vec2 Center = S.m_Pos + Normal * (LocalWidth * 1.35f * Wave * 0.85f);
			const float Half = LocalWidth * 1.45f * (0.86f + 0.14f * Remaining);
			const ColorRGBA Color = Preset ? ColorRGBA(PALETTE_AIR_BLUE) : Tint(S.m_Tint, ColorRGBA(PALETTE_AIR_BLUE), 0.55f);
			pBand[i] = {Center, Half, Half, Color.WithAlpha(S.m_Alpha * 0.07f * (0.75f + 0.25f * Remaining))};
		}
	}

	// 无贴图柔光菱形：以 Axis 为长轴，两尖端收透明、腰部最亮。
	// 几条同向菱形叠起来就是头部辉光，不会出现贴图或锐利多边形。
	void AuroraDiamond(std::vector<SQuad> &vOut, vec2 Center, vec2 Axis, float Length, float HalfWidth, ColorRGBA Color, bool Additive)
	{
		if(!(Length > 0.002f) || !(HalfWidth > 0.002f))
			return;
		const vec2 Long = Axis * Length;
		const vec2 Side = vec2(-Axis.y, Axis.x) * HalfWidth;
		Quad(vOut, Center + Long, Center + Side, Center - Long, Center - Side,
			Color.WithMultipliedAlpha(0.10f), Color, Color.WithMultipliedAlpha(0.10f), Color, Additive);
	}
}

void qm_tee_trail::style_detail::BuildAurora(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut)
{
	if(pSamples == nullptr || Count < 2 || Count > MAX_RENDER_POINTS)
		return;
	// 上游已经做过大部分安全检查；样式自身仍要兜底，任何非有限值都不许进入顶点。
	if(!std::isfinite(Width) || !std::isfinite(PixelSize))
		return;
	PixelSize = std::max(PixelSize, 0.001f);
	if(Width <= 0.0f)
		Width = 4.0f;

	bool AnyAlpha = false;
	for(size_t i = 0; i < Count; ++i)
	{
		const SStyleSample &S = pSamples[i];
		if(!std::isfinite(S.m_Pos.x) || !std::isfinite(S.m_Pos.y) || !std::isfinite(S.m_Normal.x) || !std::isfinite(S.m_Normal.y) ||
			!std::isfinite(S.m_Width) || !std::isfinite(S.m_Alpha) || !std::isfinite(S.m_Head) || !std::isfinite(S.m_Age) ||
			!std::isfinite(S.m_Remaining) || !std::isfinite(S.m_Energy) || !std::isfinite(S.m_Distance))
			return;
		if(S.m_Alpha >= 0.001f)
			AnyAlpha = true;
	}
	if(!AnyAlpha)
		return;

	// 栈上缓冲逐层重填：五层丝带、两批混合批次、底幕、空气光与头部亮核共用同一块空间，不产生堆分配。
	std::array<SBandPoint, MAX_RENDER_POINTS> aBand;

	// ---- 第一批：普通混合。宽底幕 + 五条丝带的暗面，保证白亮地图上也能看清帘幕轮廓。 ----
	{
		const size_t Points = std::min(Count, AURORA_VEIL_POINTS);
		FillAuroraVeil(pSamples, Count, Points, Preset, Seed, aBand.data());
		Band(vOut, aBand.data(), Points, s_AuroraVeil.m_Softness, PixelSize, false);
	}
	for(size_t Layer = 0; Layer < AURORA_LAYER_COUNT; ++Layer)
	{
		const SAuroraLayer &Spec = s_aAuroraLayers[Layer];
		const size_t Points = std::min(Count, size_t(Spec.m_BaseCap));
		FillAuroraRibbon(pSamples, Count, Points, Spec, Layer, Preset, true, Seed, aBand.data());
		Band(vOut, aBand.data(), Points, std::min(0.88f, Spec.m_Softness + 0.22f), PixelSize, false);
	}

	// ---- 第二批：加法混合。空气光、发光丝带、头部亮核、头部辉光、锚点微光。 ----
	{
		const size_t Points = std::min(Count, AURORA_AIR_POINTS);
		FillAuroraAir(pSamples, Count, Points, Preset, Seed, aBand.data());
		Band(vOut, aBand.data(), Points, 0.85f, PixelSize, true);
	}
	for(size_t Layer = 0; Layer < AURORA_LAYER_COUNT; ++Layer)
	{
		const SAuroraLayer &Spec = s_aAuroraLayers[Layer];
		const size_t Points = std::min(Count, size_t(Spec.m_GlowCap));
		FillAuroraRibbon(pSamples, Count, Points, Spec, Layer, Preset, false, Seed, aBand.data());
		Band(vOut, aBand.data(), Points, Spec.m_Softness, PixelSize, true);
	}

	// 头部亮核：演示里最先消失的那段近白核心，贴着中心线快速淡出，避免拖尾变成一根粗亮条。
	const size_t CorePoints = std::min(Count, AURORA_CORE_POINTS);
	if(CorePoints >= 2)
	{
		for(size_t i = 0; i < CorePoints; ++i)
		{
			const SStyleSample &S = pSamples[i];
			const vec2 Normal = Unit(S.m_Normal, vec2(0.0f, 1.0f));
			const float LocalWidth = std::max(S.m_Width, 0.0f);
			const float HeadFade = 1.0f - Smooth(S.m_Head / std::max(1.0f, LocalWidth * 3.0f));
			const float Radians = float(S.m_Head) * 0.011f + S.m_Age * 0.030f + 0.7f;
			const vec2 Center = S.m_Pos + Normal * (LocalWidth * 0.22f * std::sin(Radians) * HeadFade);
			const float Half = LocalWidth * (0.10f + 0.14f * HeadFade) * (0.85f + 0.15f * AuroraClamp01(S.m_Remaining));
			const ColorRGBA Color = Preset ? ColorRGBA(PALETTE_CORE_WHITE) : Tint(S.m_Tint, ColorRGBA(PALETTE_CORE_WHITE), 0.58f);
			const float Alpha = S.m_Alpha * 0.34f * HeadFade * (0.76f + 0.32f * AuroraClamp01(S.m_Energy));
			aBand[i] = {Center, Half, Half, Color.WithAlpha(Alpha)};
		}
		Band(vOut, aBand.data(), CorePoints, 0.55f, PixelSize, true);
	}

	// 头部辉光：三层同轴菱形叠成柔光团，避免演示截断那种尖刺感。
	{
		const SStyleSample &Head = pSamples[0];
		const vec2 HeadNormal = Unit(Head.m_Normal, vec2(0.0f, 1.0f));
		const vec2 HeadTangent = vec2(HeadNormal.y, -HeadNormal.x);
		const float HeadWidth = std::max(Head.m_Width, 0.5f);
		// 年龄只参与慢速呼吸相位；不读时钟，所以冻结时也不会抖动。
		const float Flare = std::max(Head.m_Alpha, 0.0f) * (0.85f + 0.15f * std::sin(Head.m_Age * 0.09f + Random(Seed + 7u) * 2.0f * pi));
		const ColorRGBA FlareColor = Preset ? ColorRGBA(PALETTE_CORE_WHITE) : Tint(Head.m_Tint, ColorRGBA(PALETTE_CORE_WHITE), 0.62f);
		AuroraDiamond(vOut, Head.m_Pos, HeadTangent, HeadWidth * 1.05f, HeadWidth * 0.70f, FlareColor.WithAlpha(Flare * 0.10f), true);
		AuroraDiamond(vOut, Head.m_Pos, Unit(HeadTangent + HeadNormal), HeadWidth * 0.68f, HeadWidth * 0.46f, FlareColor.WithAlpha(Flare * 0.15f), true);
		AuroraDiamond(vOut, Head.m_Pos, HeadNormal, HeadWidth * 0.40f, HeadWidth * 0.30f, FlareColor.WithAlpha(Flare * 0.22f), true);
	}

	// 锚点微光：按累计弧长固定落在帘幕两侧，闪烁相位由锚点年龄与哈希给出。
	Anchors(pSamples, Count, AURORA_SPARKLE_SPACING, Random(Seed + 173u) * AURORA_SPARKLE_SPACING, AURORA_SPARKLE_LIMIT, [&](const SStyleSample &S, unsigned Id) {
		const unsigned H = Hash(Id * 0x9e3779b9u + Seed);
		const float Side = Random(H) * 2.0f - 1.0f;
		const float Along = Random(H + 1u) * 2.0f - 1.0f;
		const float Size = Random(H + 2u);
		const float TwinklePhase = Random(H + 3u) * 2.0f * pi;
		const float HueMix = Random(H + 4u);
		const vec2 Normal = Unit(S.m_Normal, vec2(0.0f, 1.0f));
		const vec2 Tangent = vec2(Normal.y, -Normal.x);
		const float LocalWidth = std::max(S.m_Width, 0.0f);
		const float Remaining = AuroraClamp01(S.m_Remaining);
		const float Energy = AuroraClamp01(S.m_Energy);
		const vec2 Center = S.m_Pos + Normal * (Side * LocalWidth * 1.10f) + Tangent * (Along * LocalWidth * 0.40f);
		const float Twinkle = 0.5f + 0.5f * std::sin(S.m_Age * 0.13f + TwinklePhase);
		const float Extent = LocalWidth * (0.05f + 0.11f * Size) * (0.60f + 0.40f * Remaining) * (0.78f + 0.42f * Twinkle);
		const ColorRGBA Color = Preset ? Tint(ColorRGBA(PALETTE_MINT_CYAN), ColorRGBA(PALETTE_ICE_BLUE), HueMix) :
						 Tint(S.m_Tint, ColorRGBA(PALETTE_MINT_CYAN), 0.55f);
		const float Alpha = S.m_Alpha * (0.36f + 0.50f * Twinkle) * (0.80f + 0.28f * Energy);
		AuroraDiamond(vOut, Center, Tangent, Extent * 1.85f, Extent * 0.34f, Color.WithAlpha(Alpha * 0.90f), true);
		AuroraDiamond(vOut, Center, Normal, Extent * 1.25f, Extent * 0.30f, Color.WithAlpha(Alpha * 0.72f), true);
	});
}
