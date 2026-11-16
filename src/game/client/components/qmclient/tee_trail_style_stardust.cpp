#include "tee_trail_style_common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// 星链：星海连线。普通混合层用深蓝紫底面压住浅色地图，加法层依次叠加气态星云、
// 亮核细线、头部亮核与星芒，最后是按累计距离锚定的星点与星座连线。
// 所有随机量只来自 Seed 与样本字段；装饰锚点由 m_Distance 决定，因此头尾推进、
// 刷新率和屏幕细分都不会重新播种整条拖尾。
namespace
{
	using namespace qm_tee_trail;
	using namespace qm_tee_trail::style_detail;

	// ---- 几何预算 ----
	// Band 每段固定输出 3 个四边形。下列固定容量把最坏情况的四边形数锁在 2500 以内：
	// 带宽层约 1494、头部约 20、星点最多 576、连线最多 162。
	// 正常轨迹（Count 小于容量）使用全部采样；超长轨迹按固定步长抽稀，尾部不丢。
	constexpr size_t STARDUST_BASE_POINTS = 96;
	constexpr size_t STARDUST_INNER_POINTS = 72;
	constexpr size_t STARDUST_NEBULA_POINTS[3] = {88, 72, 56};
	constexpr size_t STARDUST_CORE_POINTS = 120;
	constexpr size_t STARDUST_MAX_ANCHORS = 48;
	constexpr size_t STARDUST_MAX_STARS = 64;
	constexpr size_t STARDUST_MAX_LINKS = 18;

	// ---- 预设色板：深蓝紫底 / 紫 / 群青 / 青蓝 / 冰蓝 / 白 ----
	constexpr ColorRGBA STARDUST_PRESET_BASE(0x1b2140u);
	constexpr ColorRGBA STARDUST_PRESET_PURPLE(0x7a5cffu);
	constexpr ColorRGBA STARDUST_PRESET_ULTRA(0x4a7bffu);
	constexpr ColorRGBA STARDUST_PRESET_CYAN(0x7fe3ffu);
	constexpr ColorRGBA STARDUST_PRESET_ICE(0xcfe4ffu);
	constexpr ColorRGBA STARDUST_PRESET_WHITE(0xffffffu);
	constexpr ColorRGBA STARDUST_PRESET_LINK(0xa9c4ffu);
	constexpr ColorRGBA STARDUST_CUSTOM_VIOLET(0x6a4dffu);
	constexpr ColorRGBA STARDUST_BLACK(0x000000u);

	// 固定容量抽稀：把 Count 个采样均匀映射到 Points 个带点，头尾两端都保留。
	inline size_t BandIndex(size_t Index, size_t Points, size_t Count)
	{
		if(Points <= 1)
			return 0;
		return (Index * (Count - 1) + (Points - 1) / 2) / (Points - 1);
	}

	struct SBandShape
	{
		vec2 m_Center;
		float m_HalfWidth;
		float m_Alpha;
	};

	// 气态带宽只沿法向做小幅偏移；抖动被限制在宽度比例内，弯道处不自交。
	inline SBandShape SampleBandShape(const SStyleSample &S, unsigned LayerSeed, float WidthScale, float CenterScale, float AlphaScale, double NoiseFrequency)
	{
		const float Wobble = Noise(S.m_Distance / NoiseFrequency, LayerSeed);
		const float Turbulence = Noise(S.m_Distance / (NoiseFrequency * 1.7), LayerSeed + 101u);
		const float HalfWidth = std::max(S.m_Width * WidthScale * (1.0f + 0.26f * Wobble), 0.0f);
		const vec2 Center = S.m_Pos + S.m_Normal * (S.m_Width * CenterScale * Turbulence);
		const float Alpha = std::max(S.m_Alpha, 0.0f) * AlphaScale * (0.80f + 0.20f * (0.5f + 0.5f * Wobble));
		return {Center, HalfWidth, Alpha};
	}

	// ---- 颜色只按 Preset 分支选择，透明度与几何完全不受颜色模式影响 ----
	inline ColorRGBA BaseColor(const SStyleSample &S, int Layer, bool Preset)
	{
		// 自定义色把玩家色彩压暗成深底；内层再压暗一档，叠出体积感。
		if(Preset)
			return Layer == 0 ? STARDUST_PRESET_BASE : Tint(STARDUST_PRESET_BASE, STARDUST_BLACK, 0.28f);
		return Tint(S.m_Tint, STARDUST_BLACK, Layer == 0 ? 0.70f : 0.78f);
	}

	inline ColorRGBA NebulaColor(const SStyleSample &S, int Layer, bool Preset, unsigned Seed)
	{
		// 色相沿累计距离在 220..300 之间缓慢漂移，同一玩家每次绘制保持一致。
		const float Drift = 0.5f + 0.5f * Noise(S.m_Distance / 130.0 + Random(Seed + 977u + unsigned(Layer)) * 20.0, Seed + 211u + unsigned(Layer) * 7u);
		if(Preset)
		{
			if(Layer == 0)
				return Tint(STARDUST_PRESET_PURPLE, STARDUST_PRESET_ULTRA, Drift);
			if(Layer == 1)
				return Tint(STARDUST_PRESET_ULTRA, STARDUST_PRESET_CYAN, 0.20f + 0.60f * Drift);
			return Tint(STARDUST_PRESET_CYAN, STARDUST_PRESET_ICE, 0.30f + 0.55f * Drift);
		}
		// 自定义色：提亮 m_Tint 后仍向蓝紫方向偏，避免星云变脏发灰。
		const float Bright = Layer == 0 ? 0.10f : Layer == 1 ? 0.22f :
								       0.36f;
		const float Violet = Layer == 0 ? 0.50f : Layer == 1 ? 0.44f :
								       0.32f;
		return Tint(Tint(S.m_Tint, STARDUST_PRESET_WHITE, Bright), STARDUST_CUSTOM_VIOLET, Violet);
	}

	inline ColorRGBA CoreColor(const SStyleSample &S, bool Preset)
	{
		return Preset ? Tint(STARDUST_PRESET_ICE, STARDUST_PRESET_WHITE, 0.42f) : Tint(S.m_Tint, STARDUST_PRESET_WHITE, 0.80f);
	}

	inline ColorRGBA HeadColor(const SStyleSample &S, int Layer, bool Preset)
	{
		if(Preset)
		{
			if(Layer == 0)
				return Tint(STARDUST_PRESET_PURPLE, STARDUST_PRESET_ICE, 0.42f);
			if(Layer == 1)
				return Tint(STARDUST_PRESET_ICE, STARDUST_PRESET_WHITE, 0.52f);
			return STARDUST_PRESET_WHITE;
		}
		return Tint(S.m_Tint, STARDUST_PRESET_WHITE, Layer == 2 ? 0.94f : 0.56f + 0.16f * float(Layer));
	}

	inline ColorRGBA StarColor(const SStyleSample &S, unsigned Key, bool Preset)
	{
		if(Preset)
		{
			const float Choose = Random(Key + 41u);
			if(Choose < 0.34f)
				return STARDUST_PRESET_WHITE;
			if(Choose < 0.64f)
				return STARDUST_PRESET_ICE;
			if(Choose < 0.82f)
				return STARDUST_PRESET_CYAN;
			return Tint(STARDUST_PRESET_PURPLE, STARDUST_PRESET_WHITE, 0.45f);
		}
		return Tint(Tint(S.m_Tint, STARDUST_PRESET_WHITE, 0.72f + 0.22f * Random(Key + 43u)), STARDUST_PRESET_ICE, 0.20f);
	}

	// 从中心向四个轴对称边缘拉三角形：中心亮、边缘透明，用四个四边形近似径向亮核。
	inline void DiamondFan(std::vector<SQuad> &vOut, vec2 Center, float Radius, ColorRGBA Color)
	{
		const vec2 aEdge[4] = {vec2(Radius, 0.0f), vec2(0.0f, Radius), vec2(-Radius, 0.0f), vec2(0.0f, -Radius)};
		const ColorRGBA Edge = Color.WithAlpha(0.0f);
		for(int i = 0; i < 4; ++i)
		{
			const vec2 A = Center + aEdge[i];
			const vec2 B = Center + aEdge[(i + 1) % 4];
			Quad(vOut, Center, A, B, B, Color, Edge, Edge, Edge, true);
		}
	}

	// 四角星芒：四根细长三角，根部有透明度、尖端归零，整体沿世界轴向略作旋转。
	inline void StarFlare(std::vector<SQuad> &vOut, vec2 Center, float Angle, float Length, float HalfWidth, ColorRGBA Color)
	{
		const ColorRGBA Tip = Color.WithAlpha(0.0f);
		for(int Arm = 0; Arm < 4; ++Arm)
		{
			const vec2 Dir = direction(Angle + float(Arm) * pi * 0.5f);
			const vec2 Perp = vec2(-Dir.y, Dir.x) * HalfWidth;
			const vec2 TipPos = Center + Dir * Length;
			Quad(vOut, Center + Perp, Center - Perp, TipPos, TipPos, Color, Color, Tip, Tip, true);
		}
	}

	// 星点数据先收集到栈上，连线需要看到整份星场再决定相邻连接。
	struct SStardustStar
	{
		vec2 m_Pos;
		ColorRGBA m_Color;
		float m_Size;
		float m_Alpha;
		float m_Age;
		float m_TwinkleRate;
		float m_Phase;
		float m_Remaining;
		bool m_Bright;
		unsigned m_Id;
	};
}

void qm_tee_trail::style_detail::BuildStardust(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut)
{
	if(pSamples == nullptr || Count < 2 || Count > MAX_RENDER_POINTS)
		return;

	// 非有限输入不进入几何：上游虽然已经过滤，这里再兜底一次防止 NaN 顶点。
	for(size_t i = 0; i < Count; ++i)
	{
		const SStyleSample &S = pSamples[i];
		if(!std::isfinite(S.m_Pos.x) || !std::isfinite(S.m_Pos.y) ||
			!std::isfinite(S.m_Normal.x) || !std::isfinite(S.m_Normal.y) ||
			!std::isfinite(S.m_Distance) || !std::isfinite(S.m_Age) ||
			!std::isfinite(S.m_Width) || !std::isfinite(S.m_Alpha) ||
			!std::isfinite(S.m_Energy) || !std::isfinite(S.m_Head) || !std::isfinite(S.m_Remaining))
			return;
	}

	// 整段拖尾的弧长只用于亮核的头尾包络；退化输入退回 1 避免除零。
	const float Total = std::max(pSamples[Count - 1].m_Head, 1.0f);
	std::array<SBandPoint, MAX_RENDER_POINTS> aBand;

	// ===== 普通混合层：深蓝紫底面，先把浅色地图压暗 =====
	// 外层宽而虚，内层窄而实；宽度全部来自已含锥形/能量宽度的 m_Width。
	for(int Layer = 0; Layer < 2; ++Layer)
	{
		const size_t Points = std::min(Count, Layer == 0 ? STARDUST_BASE_POINTS : STARDUST_INNER_POINTS);
		const unsigned LayerSeed = Layer == 0 ? Seed + 401u : Seed + 409u;
		for(size_t k = 0; k < Points; ++k)
		{
			const SStyleSample &S = pSamples[BandIndex(k, Points, Count)];
			const SBandShape Shape = SampleBandShape(S, LayerSeed, Layer == 0 ? 1.26f : 0.60f, Layer == 0 ? 0.06f : 0.03f,
				Layer == 0 ? 0.38f : 0.52f, Layer == 0 ? 46.0 : 39.0);
			aBand[k] = {Shape.m_Center, Shape.m_HalfWidth, Shape.m_HalfWidth, BaseColor(S, Layer, Preset).WithAlpha(Shape.m_Alpha)};
		}
		Band(vOut, aBand.data(), Points, 0.55f, PixelSize, false);
	}

	// ===== 加法混合层从这里开始，全部排在普通层之后 =====
	// 2) 气态星云：三层不同色相、不同宽窄的低透明度宽带，中心沿法向缓慢漂移。
	const float aNebulaWidth[3] = {2.55f, 1.45f, 0.90f};
	const float aNebulaAlpha[3] = {0.210f, 0.155f, 0.105f};
	const float aNebulaSoftness[3] = {0.66f, 0.58f, 0.50f};
	const float aNebulaCenter[3] = {0.12f, 0.18f, 0.24f};
	for(int Layer = 0; Layer < 3; ++Layer)
	{
		const size_t Points = std::min(Count, STARDUST_NEBULA_POINTS[Layer]);
		const unsigned LayerSeed = Seed + 911u + unsigned(Layer) * 32u;
		for(size_t k = 0; k < Points; ++k)
		{
			const SStyleSample &S = pSamples[BandIndex(k, Points, Count)];
			const SBandShape Shape = SampleBandShape(S, LayerSeed, aNebulaWidth[Layer], aNebulaCenter[Layer], aNebulaAlpha[Layer], 64.0 - double(Layer) * 12.0);
			aBand[k] = {Shape.m_Center, Shape.m_HalfWidth, Shape.m_HalfWidth, NebulaColor(S, Layer, Preset, Seed).WithAlpha(Shape.m_Alpha)};
		}
		Band(vOut, aBand.data(), Points, aNebulaSoftness[Layer], PixelSize, true);
	}

	// 3) 亮核细线：蓝白窄带，靠近头部更亮，尾部随 m_Head 包络收敛。
	{
		const size_t Points = std::min(Count, STARDUST_CORE_POINTS);
		for(size_t k = 0; k < Points; ++k)
		{
			const SStyleSample &S = pSamples[BandIndex(k, Points, Count)];
			const float Shimmer = 0.70f + 0.30f * (0.5f + 0.5f * Noise(S.m_Distance / 33.0, Seed + 1201u));
			const float HeadEnvelope = 0.32f + 0.68f * Smooth(1.0f - S.m_Head / Total);
			const float HalfWidth = std::max(S.m_Width * 0.065f * (0.82f + 0.36f * (0.5f + 0.5f * Noise(S.m_Distance / 21.0, Seed + 1207u))), 0.0f);
			const vec2 Center = S.m_Pos + S.m_Normal * (S.m_Width * 0.03f * Noise(S.m_Distance / 17.0, Seed + 1213u));
			const float Alpha = std::max(S.m_Alpha, 0.0f) * 0.48f * HeadEnvelope * Shimmer * (0.80f + 0.20f * std::clamp(S.m_Energy, 0.0f, 1.0f));
			aBand[k] = {Center, HalfWidth, HalfWidth, CoreColor(S, Preset).WithAlpha(Alpha)};
		}
		Band(vOut, aBand.data(), Points, 0.35f, PixelSize, true);
	}

	// 4) 头部：三层同心菱形亮核 + 随速度出现的四角星芒 + 中心白点。
	{
		const SStyleSample &Head = pSamples[0];
		if(Head.m_Alpha >= 0.002f && Head.m_Width > 0.001f)
		{
			const float Energy = std::clamp(Head.m_Energy, 0.0f, 1.0f);
			const float Pulse = 0.80f + 0.20f * float(std::sin(Head.m_Age * 0.22f + Random(Seed + 1301u) * 2.0f * pi));
			const vec2 Center = Head.m_Pos;
			const float Span = Head.m_Width;
			const float aGain[3] = {0.22f, 0.40f, 0.76f};
			const float aRadius[3] = {1.95f, 1.10f, 0.46f};
			for(int Layer = 0; Layer < 3; ++Layer)
			{
				const float SpeedGain = Layer == 2 ? 1.0f : 0.74f + 0.26f * Energy;
				const ColorRGBA Color = HeadColor(Head, Layer, Preset).WithAlpha(Head.m_Alpha * aGain[Layer] * Pulse * SpeedGain);
				DiamondFan(vOut, Center, Span * aRadius[Layer], Color);
			}
			// 星芒长度随速度增长，方向基本落在世界轴上，仅有很小的种子偏转。
			const float Flare = Smooth((Energy - 0.05f) / 0.75f);
			if(Flare > 0.02f)
			{
				const float Angle = (Random(Seed + 1319u) - 0.5f) * 0.45f + Head.m_Age * 0.05f;
				const float Length = Span * (0.80f + 1.45f * Energy) * Pulse;
				StarFlare(vOut, Center, Angle, Length, Span * 0.060f, HeadColor(Head, 2, Preset).WithAlpha(Head.m_Alpha * 0.46f * Flare * Pulse));
			}
			DiamondFan(vOut, Center, Span * 0.15f, STARDUST_PRESET_WHITE.WithAlpha(Head.m_Alpha * 0.84f * Pulse));
		}
	}

	// 5) 星点与星座连线：锚点由累计距离决定，同一位置在整条拖尾生命周期内稳定。
	std::array<SStardustStar, STARDUST_MAX_STARS> aStars;
	size_t StarCount = 0;
	const float AnchorSpacing = std::clamp(Width * 1.80f, 22.0f, 34.0f);
	// 单个锚点最多派生两颗星：主星保证星座密度，副星制造错落的星空感。
	const auto EmitStar = [&](const SStyleSample &S, unsigned Key, unsigned Slot) {
		if(StarCount == aStars.size())
			return;
		const unsigned Sub = Key + Slot * 0x9e3779b9u + 0x165667b1u;
		SStardustStar Star;
		// 星点散布在轨迹带内外：法向幅度大、切向幅度小，避免排成一条贴着中心线的珠链。
		const float Sign = Random(Sub + 5u) > 0.5f ? 1.0f : -1.0f;
		const float Cross = Sign * (0.30f + 0.95f * Random(Sub + 7u)) * S.m_Width * (Slot == 0 ? 1.0f : 1.18f);
		const float Along = (Random(Sub + 11u) * 2.0f - 1.0f) * S.m_Width * 0.48f;
		Star.m_Pos = S.m_Pos + S.m_Normal * Cross + Tangent(S) * Along;
		Star.m_Size = std::clamp(S.m_Width * (0.100f + 0.240f * Random(Sub + 13u)), 0.60f, 4.0f);
		Star.m_Alpha = std::max(S.m_Alpha, 0.0f) * (0.60f + 0.40f * Random(Sub + 17u));
		Star.m_Age = S.m_Age;
		Star.m_TwinkleRate = 0.045f + 0.115f * Random(Sub + 19u);
		Star.m_Phase = Random(Sub + 23u) * 2.0f * pi;
		Star.m_Remaining = S.m_Remaining;
		Star.m_Bright = Random(Sub + 29u) > (Slot == 0 ? 0.42f : 0.62f);
		Star.m_Color = StarColor(S, Sub, Preset);
		Star.m_Id = Hash(Sub + 31u);
		aStars[StarCount++] = Star;
	};
	Anchors(pSamples, Count, AnchorSpacing, Random(Seed + 83u) * AnchorSpacing, STARDUST_MAX_ANCHORS, [&](const SStyleSample &S, unsigned Id) {
		// 每个锚点按哈希决定是否有星；只由锚点编号与种子决定，不随寿命淡出而闪烁消失。
		const unsigned Key = Hash(Id * 0x9e3779b9u + Seed * 0x85ebca6bu + 0x27d4eb2du);
		if(Random(Key + 3u) < 0.80f)
			EmitStar(S, Key, 0);
		if(Random(Key + 37u) < 0.42f)
			EmitStar(S, Key, 1);
	});

	for(size_t i = 0; i < StarCount; ++i)
	{
		const SStardustStar &Star = aStars[i];
		// 闪烁相位来自样本年龄与锚点哈希，不用墙钟；尾端星点随剩余寿命淡出。
		const float Twinkle = 0.35f + 0.65f * (0.5f + 0.5f * float(std::sin(Star.m_Age * Star.m_TwinkleRate + Star.m_Phase)));
		const float Fade = 0.55f + 0.45f * std::clamp(Star.m_Remaining, 0.0f, 1.0f);
		const float Alpha = Star.m_Alpha * (0.55f + 0.45f * Twinkle) * Fade;
		if(Alpha < 0.001f)
			continue;
		// 外层光晕用一个四角渐变菱形近似径向渐变。
		DiamondFan(vOut, Star.m_Pos, Star.m_Size * (3.2f + 1.0f * Twinkle), Star.m_Color.WithAlpha(Alpha * 0.50f));
		// 星核用平色小菱形，足够亮以便在深色地图上点出"星星"。
		const float CoreRadius = Star.m_Size * 0.70f;
		const ColorRGBA Core = Star.m_Color.WithAlpha(Alpha);
		Quad(vOut, Star.m_Pos + vec2(CoreRadius, 0.0f), Star.m_Pos + vec2(0.0f, CoreRadius), Star.m_Pos - vec2(CoreRadius, 0.0f), Star.m_Pos - vec2(0.0f, CoreRadius), Core, Core, Core, Core, true);
		if(Star.m_Bright)
		{
			// 亮星带十字芒，长度跟随闪烁因子，看起来像真的在呼吸。
			const float ArmAngle = Random(Star.m_Id) * 0.35f;
			StarFlare(vOut, Star.m_Pos, ArmAngle, Star.m_Size * (2.80f + 1.40f * Twinkle), Star.m_Size * 0.20f, Star.m_Color.WithAlpha(Alpha * 0.55f));
		}
	}

	// 6) 星座连线：在星场里向后找第一颗距离合适的星连线，跳过过近/过远的邻居，
	//    透明度随两端剩余寿命淡出；连线本身也只沿累计距离变化，不逐帧重播种。
	size_t LinkCount = 0;
	for(size_t i = 0; i + 1 < StarCount && LinkCount < STARDUST_MAX_LINKS; ++i)
	{
		size_t Partner = StarCount;
		float Span = 0.0f;
		for(size_t j = i + 1; j < StarCount && j <= i + 3; ++j)
		{
			const float Candidate = length(aStars[j].m_Pos - aStars[i].m_Pos);
			if(Candidate >= 6.0f && Candidate <= 90.0f)
			{
				Partner = j;
				Span = Candidate;
				break;
			}
		}
		if(Partner == StarCount)
			continue;
		const SStardustStar &A = aStars[i], &B = aStars[Partner];
		++LinkCount;
		const vec2 Delta = B.m_Pos - A.m_Pos;
		const vec2 Dir = Delta / Span;
		const vec2 Perp = vec2(-Dir.y, Dir.x);
		const float Fade = std::min(std::clamp(A.m_Remaining, 0.0f, 1.0f), std::clamp(B.m_Remaining, 0.0f, 1.0f));
		const float Falloff = std::clamp((90.0f - Span) / 40.0f, 0.0f, 1.0f);
		// 每顶点 alpha 乘入两端源透明度；亮度上限保持连线纤细，尾段仍随寿命淡出。
		const float Alpha = std::min(std::min(std::max(A.m_Alpha, 0.0f), std::max(B.m_Alpha, 0.0f)) * 2.2f, 0.22f) * Falloff * (0.45f + 0.55f * Fade);
		if(Alpha < 0.001f)
			continue;
		const ColorRGBA Color = (Preset ? STARDUST_PRESET_LINK : Tint(A.m_Color, STARDUST_PRESET_WHITE, 0.35f)).WithAlpha(Alpha);
		const float Bend = (Random(Hash(A.m_Id * 7u + B.m_Id + Seed)) - 0.5f) * Span * 0.14f;
		const int Segments = Span > 52.0f ? 3 : 2;
		const float LineWidth = std::clamp(std::min(A.m_Size, B.m_Size) * 0.42f, 0.38f, 1.30f);
		vec2 Previous = A.m_Pos;
		for(int Segment = 1; Segment <= Segments; ++Segment)
		{
			const float T = float(Segment) / float(Segments);
			// 中间点沿法向轻微弯折，两端保持原位置；3 段时第二段往反方向折。
			const float Amplitude = T * (1.0f - T) * 4.0f;
			const float BendSign = Segments == 3 && Segment == 2 ? -0.65f : 1.0f;
			const vec2 Next = mix(A.m_Pos, B.m_Pos, T) + Perp * (Bend * Amplitude * BendSign);
			Stroke(vOut, Previous, Next, LineWidth, LineWidth, Color, PixelSize, true);
			Previous = Next;
		}
	}
}
