#include "tee_trail_style_common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// 焰灵：余烬之灵。视觉参考是 4 层加色火焰光带 + 头部火球 + 上升余烬 + 热浪丝。
// 实现用两层普通混合（深红剪影 + 橙红压底）把火焰压在地图上（浅色地图也看得清），
// 再用 4 层加法混合按 HTML 的宽度/色温比例叠出暖色光焰。
//
// 形状只由弧长、年龄和种子推导：
//  - 低频空间湍流（m_Head）负责火舌起伏，频率低于 112 个截面的奈奎斯特频率，
//    避免每帧看到锯齿状的宽度跳变；
//  - 闪烁相位取头部 m_Age，整条火焰按同一节拍呼吸，不会因采样密度不同而重播种；
//  - 每层再叠一个低频"舔舐"项，让同一条拖尾的亮度有长短不一的火舌节律。
namespace
{
	using namespace qm_tee_trail;
	using namespace qm_tee_trail::style_detail;

	// 火焰带截面上限。6 层 × 3 条带 × 111 段 = 1998 个四边形；
	// 头部径向光、余烬、热浪丝与扰动线的最坏情况仍在 2500 预算内。
	constexpr size_t IGNIS_FLAME_POINTS = 112;
	constexpr size_t IGNIS_EMBER_ANCHORS = 80;
	constexpr int IGNIS_EMBER_LIMIT = 128;
	constexpr int IGNIS_WISP_ANCHORS = 18;
	constexpr size_t IGNIS_HEAT_ANCHORS = 20;

	constexpr ColorRGBA s_Black(0x000000u);
	constexpr ColorRGBA s_White(0xffffffu);
	// 预设色板与 HTML 参考一致：深红、橙红、橙黄、亮黄、白黄。
	constexpr ColorRGBA s_PresetDeep(0x3c0804u);
	constexpr ColorRGBA s_PresetEmber(0xb4320au);
	constexpr ColorRGBA s_PresetFlame(0xff8c14u);
	constexpr ColorRGBA s_PresetGold(0xffd24au);
	constexpr ColorRGBA s_PresetWhite(0xfff6c8u);

	struct SFlameColors
	{
		ColorRGBA m_Deep, m_Ember, m_Flame, m_Gold, m_White;
	};

	// 预设色板是固定的暖色温度阶梯；自定义色从玩家色相出发，只做明度分层。
	SFlameColors IgnisColors(ColorRGBA TintColor, bool Preset)
	{
		if(Preset)
			return {s_PresetDeep, s_PresetEmber, s_PresetFlame, s_PresetGold, s_PresetWhite};
		return {
			Tint(TintColor, s_Black, 0.70f),
			Tint(TintColor, s_Black, 0.35f),
			Tint(TintColor, s_White, 0.30f),
			Tint(TintColor, s_White, 0.62f),
			Tint(TintColor, s_White, 0.88f),
		};
	}

	// DDNet 世界坐标以 Y 轴向下，火焰与余烬的"上升"是负 Y。
	inline vec2 IgnisUp()
	{
		return vec2(0.0f, -1.0f);
	}

	inline float IgnisPhase(unsigned Seed, unsigned Salt)
	{
		return Random(Seed + Salt) * 2.0f * pi;
	}

	// 菱形光斑：四顶点沿两个正交轴展开，作为火星与光刺的最小单元。
	inline void IgnisDiamond(std::vector<SQuad> &vOut, vec2 Center, vec2 AxisA, vec2 AxisB, ColorRGBA Color, bool Additive)
	{
		Quad(vOut, Center + AxisA, Center + AxisB, Center - AxisA, Center - AxisB, Color, Color, Color, Color, Additive);
	}

	// 径向光：同心环四边形拼出平滑衰减的光斑，最外环透明度收到零，
	// 环的多边形边界因此完全看不见，头部不会再出现方块/多边形的硬轮廓。
	void IgnisBuildRadialGlow(std::vector<SQuad> &vOut, vec2 Center, vec2 AxisA, vec2 AxisB, float Radius,
		const ColorRGBA *pColors, const float *pRadii, int RingCount, int Segments)
	{
		for(int Ring = 0; Ring < RingCount; ++Ring)
		{
			const float Inner = Radius * pRadii[Ring];
			const float Outer = Radius * pRadii[Ring + 1];
			const ColorRGBA InnerColor = pColors[Ring];
			const ColorRGBA OuterColor = pColors[Ring + 1];
			for(int Segment = 0; Segment < Segments; ++Segment)
			{
				const float Angle0 = 2.0f * pi * float(Segment) / float(Segments);
				const float Angle1 = 2.0f * pi * float(Segment + 1) / float(Segments);
				const vec2 Dir0 = AxisA * std::cos(Angle0) + AxisB * std::sin(Angle0);
				const vec2 Dir1 = AxisA * std::cos(Angle1) + AxisB * std::sin(Angle1);
				Quad(vOut, Center + Dir0 * Inner, Center + Dir1 * Inner, Center + Dir1 * Outer, Center + Dir0 * Outer,
					InnerColor, InnerColor, OuterColor, OuterColor, true);
			}
		}
	}

	inline bool IgnisSampleFinite(const SStyleSample &S)
	{
		return std::isfinite(S.m_Pos.x) && std::isfinite(S.m_Pos.y) &&
		       std::isfinite(S.m_Normal.x) && std::isfinite(S.m_Normal.y) &&
		       std::isfinite(S.m_Width) && std::isfinite(S.m_Alpha) && std::isfinite(S.m_Head) &&
		       std::isfinite(S.m_Age) && std::isfinite(S.m_Energy) && std::isfinite(S.m_Remaining) &&
		       std::isfinite(S.m_Distance) && std::isfinite(S.m_Tint.r) && std::isfinite(S.m_Tint.g) &&
		       std::isfinite(S.m_Tint.b) && std::isfinite(S.m_Tint.a);
	}

	// 头部火球与各层使用同一条"呼吸"曲线，保证整条火焰按同一节拍闪烁。
	float IgnisHeadPulse(const SStyleSample &Head, unsigned Seed)
	{
		const float Age = std::max(Head.m_Age, 0.0f);
		return 0.86f + 0.14f * std::sin(Age * 6.3f + IgnisPhase(Seed, 0x2b7u)) +
		       0.06f * std::sin(Age * 2.1f + IgnisPhase(Seed, 0x2c1u));
	}

	struct SFlameLayer
	{
		float m_WidthScale;
		float m_AlphaScale;
		float m_BaseGain;
		float m_EnergyGain;
		float m_Wobble;
		float m_Phase;
		float m_Softness;
		int m_Color;
		bool m_Additive;
	};

	// 两层普通混合负责浅色地图上的火焰剪影与底色；四层加法混合按 HTML 的
	// 2.2/1.4/0.8/0.3 比例叠光，最外层羽化到零，越往内层越亮，白黄核心只在高速段出现。
	// 颜色索引：0=深红，1=橙红，2=橙黄，3=亮黄，4=白黄。
	// 两层普通混合负责浅色地图上的火焰剪影与底色；四层加法混合按 HTML 的
	// 2.2/1.4/0.8/0.3 比例叠光，越往内层越亮；柔边只吃掉一部分宽度，
	// 避免整层被羽化到一半亮度，火焰主体才能呈现"亮芯 + 色带"而不是一根细线。
	// 颜色索引：0=深红，1=橙红，2=橙黄，3=亮黄，4=白黄。
	const SFlameLayer s_aIgnisLayers[] = {
		{1.85f, 0.16f, 0.85f, 0.20f, 0.30f, 0.00f, 0.45f, 0, false},
		{1.12f, 0.50f, 0.86f, 0.22f, 0.34f, 1.30f, 0.30f, 1, false},
		{1.45f, 0.350f, 0.60f, 0.50f, 0.95f, 0.00f, 0.85f, 0, true},
		{1.10f, 0.700f, 0.55f, 0.55f, 1.00f, 1.05f, 0.75f, 1, true},
		{0.80f, 1.100f, 0.45f, 0.70f, 0.90f, 2.10f, 0.62f, 2, true},
		{0.34f, 0.650f, 0.30f, 0.95f, 0.62f, 3.15f, 0.52f, 4, true},
	};
	constexpr int IGNIS_NORMAL_LAYERS = 2;
	constexpr int IGNIS_LAYER_COUNT = int(sizeof(s_aIgnisLayers) / sizeof(s_aIgnisLayers[0]));

	// 上游采样可能很密（最多 384 个），光带只保留按弧长均匀取出的截面：
	// 形状与层数因此只由轨迹长度决定，采样率变化不会重新播种。
	struct SFlameField
	{
		std::array<SStyleSample, IGNIS_FLAME_POINTS> m_aPoints;
		size_t m_Count = 0;
	};

	size_t IgnisBuildFlameField(const SStyleSample *pSamples, size_t Count, SFlameField &Field)
	{
		Field.m_Count = 0;
		const float Total = pSamples[Count - 1].m_Head;
		if(!(Total > 0.005f))
			return 0;
		const size_t PointCount = std::min(IGNIS_FLAME_POINTS, Count);
		if(PointCount < 2)
			return 0;
		const float Spacing = Total / float(PointCount - 1);
		size_t Segment = 0;
		for(size_t i = 0; i < PointCount; ++i)
		{
			SStyleSample &Out = Field.m_aPoints[i];
			if(i == 0)
			{
				Out = pSamples[0];
				Out.m_Head = 0.0f;
			}
			else if(i + 1 == PointCount)
			{
				Out = pSamples[Count - 1];
				Out.m_Head = Total;
			}
			else
			{
				const float Target = Spacing * float(i);
				while(Segment + 2 < Count && pSamples[Segment + 1].m_Head < Target)
					++Segment;
				const SStyleSample &A = pSamples[Segment], &B = pSamples[Segment + 1];
				const float Span = A.m_Head - B.m_Head;
				Out = Interpolate(A, B, Span > 0.0001f ? std::clamp((A.m_Head - Target) / Span, 0.0f, 1.0f) : 0.0f);
				Out.m_Head = Target;
			}
		}
		Field.m_Count = PointCount;
		return PointCount;
	}

	// 火焰截面：低频湍流把带宽与中心线一起推弯，左右不对称形成被风舔动的火舌。
	void IgnisBuildBand(std::vector<SQuad> &vOut, const SFlameField &Field, const SFlameLayer &Layer, bool Preset, float PixelSize, float HeadPulse, unsigned Seed)
	{
		if(Field.m_Count < 2)
			return;
		std::array<SBandPoint, IGNIS_FLAME_POINTS> aBand;
		const float WavePhase = IgnisPhase(Seed, 0x1f3u);
		const float LickPhase = IgnisPhase(Seed, 0x27fu);
		for(size_t i = 0; i < Field.m_Count; ++i)
		{
			const SStyleSample &S = Field.m_aPoints[i];
			const float Alive = std::clamp(S.m_Remaining, 0.0f, 1.0f);
			const float Energy = std::clamp(S.m_Energy, 0.0f, 1.0f);
			// 空间频率 0.038 rad/单位 → 周期约 165 世界单位；每段相位差很小，
			// 火舌是平滑弯折而不是逐段跳变；年龄项只做缓慢的整体流动。
			const float SlowWave = std::sin(S.m_Head * 0.038f + S.m_Age * 0.45f + Layer.m_Phase + WavePhase);
			const float FastWave = std::sin(S.m_Head * 0.095f - S.m_Age * 0.55f + Layer.m_Phase * 2.3f + WavePhase * 1.4f);
			const float Wave = SlowWave * 0.70f + FastWave * 0.30f;
			const float Wobble = Wave * Alive * Layer.m_Wobble;
			const float Half = std::max(S.m_Width * Layer.m_WidthScale * (0.86f + 0.28f * Energy), 0.0f);
			// 左右不对称：一侧向外舔、一侧收到火芯里；再叠一点很小的中心偏移。
			const float Left = std::max(Half * (1.0f + 0.78f * Wobble), Half * 0.16f);
			const float Right = std::max(Half * (1.0f - 0.52f * Wobble + 0.16f * std::sin(S.m_Head * 0.055f + Layer.m_Phase * 1.7f + WavePhase)), Half * 0.16f);
			const vec2 Center = S.m_Pos + S.m_Normal * (Half * 0.28f * Wobble) + IgnisUp() * (Half * 0.14f * Wobble);
			// 头部收窄：端帽的平直截面藏在火球光斑里，火焰从火球中自然长出而不是被切断。
			const float HeadFade = Smooth(std::clamp(S.m_Head / std::max(1.5f, Half * 1.10f), 0.0f, 1.0f));
			// 低频"舔舐"亮度：同一条拖尾上长短不一的火舌节律，相位只取弧长与年龄。
			const float Lick = 0.58f + 0.42f * std::sin(S.m_Head * 0.026f + S.m_Age * 0.60f + Layer.m_Phase * 1.9f + LickPhase);
			const float Grow = Layer.m_BaseGain + Layer.m_EnergyGain * Energy;
			const SFlameColors Colors = IgnisColors(S.m_Tint, Preset);
			ColorRGBA Base = Colors.m_Deep;
			switch(Layer.m_Color)
			{
			case 0: Base = Colors.m_Deep; break;
			case 1:
				// 压底层沿拖尾从鲜橙红（新）过渡到深红（旧），单层就覆盖 HTML 的色温跨度。
				Base = Layer.m_Additive ? Colors.m_Ember : Tint(Colors.m_Deep, Colors.m_Ember, 0.20f + 0.70f * Alive);
				break;
			case 2: Base = Colors.m_Flame; break;
			case 3: Base = Tint(Colors.m_Gold, Colors.m_White, 0.35f + 0.45f * Energy); break;
			default: Base = Tint(Colors.m_Gold, Colors.m_White, 0.55f + 0.40f * Energy); break;
			}
			const float Alpha = S.m_Alpha * Layer.m_AlphaScale * Lick * Grow * HeadPulse * HeadFade;
			aBand[i] = {Center, Left, Right, Base.WithAlpha(Alpha)};
		}
		Band(vOut, aBand.data(), Field.m_Count, Layer.m_Softness, PixelSize, Layer.m_Additive);
	}

	// 头部火球：HTML 的三层径向光在这里用同心环四边形铺成真正的径向衰减光斑，
	// 外环透明度收到零，因此没有方块/多边形的硬轮廓；只留一点沿运动方向的柔光尾。
	void IgnisBuildHeadFireball(std::vector<SQuad> &vOut, const SStyleSample &Head, bool Preset, float HeadPulse, unsigned Seed)
	{
		const float Alpha = Head.m_Alpha;
		const float Width = std::max(Head.m_Width, 0.0f);
		if(Alpha < 0.001f || Width < 0.001f)
			return;
		const float Energy = std::clamp(Head.m_Energy, 0.0f, 1.0f);
		const float Age = std::max(Head.m_Age, 0.0f);
		const float Pulse = HeadPulse * (1.0f + 0.09f * std::sin(Age * 9.7f + IgnisPhase(Seed, 0x3a1u)));
		const float Radius = Width * (0.95f + 0.55f * Energy) * Pulse;
		const float Gain = Alpha * (0.55f + 0.45f * Energy);
		const vec2 N = Head.m_Normal;
		const vec2 T = Tangent(Head);
		const vec2 Center = Head.m_Pos;
		const SFlameColors Colors = IgnisColors(Head.m_Tint, Preset);
		const float Angle = 0.10f + (Random(Seed + 0x44du) - 0.5f) * 0.25f;
		const vec2 RotA = T * std::cos(Angle) + N * std::sin(Angle);
		const vec2 RotB = vec2(-RotA.y, RotA.x);
		// 沿运动方向略拉长的椭圆径向光：五档色温从白黄核心衰减到深红，最后一档透明度为零。
		// 16 段把等亮度线的多边形误差压到 2% 以下，叠加混合时看不出硬轮廓。
		const vec2 GlowA = RotA * (Radius * 1.15f);
		const vec2 GlowB = RotB * (Radius * 0.92f);
		const ColorRGBA aColors[] = {
			Tint(Colors.m_White, Colors.m_Gold, 0.26f).WithAlpha(Gain * 0.86f),
			Colors.m_Gold.WithAlpha(Gain * 0.66f),
			Colors.m_Flame.WithAlpha(Gain * 0.42f),
			Colors.m_Ember.WithAlpha(Gain * 0.21f),
			Colors.m_Deep.WithAlpha(Gain * 0.085f),
			Colors.m_Deep.WithAlpha(0.0f),
		};
		const float aRadii[] = {0.00f, 0.30f, 0.66f, 1.10f, 1.65f, 2.25f};
		IgnisBuildRadialGlow(vOut, Center, GlowA, GlowB, 1.0f, aColors, aRadii, 5, 16);
	}

	// 余烬：轨迹锚点上按哈希撒 0~2 颗火星，位置沿法向散开、随年龄上飘，
	// 闪烁相位与锚点 Id 绑定。偏移量与 m_Width 成正比，不会漂到轨迹之外。
	void IgnisBuildEmbers(std::vector<SQuad> &vOut, const SStyleSample *pSamples, size_t Count, bool Preset, unsigned Seed)
	{
		const float Spacing = 20.0f + Random(Seed + 0x51u) * 9.0f;
		const float Offset = Random(Seed + 0x57u) * Spacing;
		int Emitted = 0;
		Anchors(pSamples, Count, Spacing, Offset, IGNIS_EMBER_ANCHORS, [&](const SStyleSample &S, unsigned Id) {
			if(Emitted >= IGNIS_EMBER_LIMIT)
				return;
			const float Remaining = std::clamp(S.m_Remaining, 0.0f, 1.0f);
			// 新火星更密，越老越稀疏；密度与颜色模式无关。
			if(Random(Id * 3u + Seed + 0x5du) > 0.20f + 0.62f * Remaining)
				return;
			const int EmberCount = Random(Id * 7u + Seed + 0x63u) > 0.58f ? 2 : 1;
			const vec2 N = S.m_Normal;
			const vec2 T = Tangent(S);
			const SFlameColors Colors = IgnisColors(S.m_Tint, Preset);
			const float Width = std::max(S.m_Width, 0.0f);
			if(Width < 0.001f)
				return;
			const float Age = std::max(S.m_Age, 0.0f);
			for(int Ember = 0; Ember < EmberCount && Emitted < IGNIS_EMBER_LIMIT; ++Ember)
			{
				++Emitted;
				const unsigned Base = Id * 0x85ebca6bu + Seed + unsigned(Ember) * 0x27d4eb2fu;
				const float R0 = Random(Base + 1u), R1 = Random(Base + 2u), R2 = Random(Base + 3u);
				const float R3 = Random(Base + 4u), R4 = Random(Base + 5u), R5 = Random(Base + 6u);
				// 余烬随年龄上升（负 Y），偏移上限约 0.9×火焰半宽；中心偏移让火星从火舌里钻出来。
				const float Rise = Width * (0.10f + 0.85f * Smooth(std::min(Age / 3.0f, 1.0f))) * (0.55f + 0.45f * R2);
				const float Lateral = (R0 - 0.5f) * Width * 1.35f + std::sin(Age * 2.1f + R1 * 6.2831853f + float(Id)) * Width * 0.16f;
				const float Along = (R1 - 0.5f) * Width * 0.8f;
				const vec2 Center = S.m_Pos + N * Lateral + T * Along + IgnisUp() * Rise;
				const float Flicker = 0.5f + 0.5f * std::sin(Age * 8.5f + float(Id) * 1.7f + R3 * 6.2831853f);
				const float Size = Width * (0.060f + 0.140f * R4 * R4) * (0.70f + 0.30f * Remaining);
				if(Size < 0.0005f)
					continue;
				const float Alpha = S.m_Alpha * (0.42f + 0.38f * R5) * (0.40f + 0.60f * Flicker) * (0.45f + 0.55f * Remaining);
				const ColorRGBA Core = Tint(Colors.m_Flame, Colors.m_White, 0.40f + 0.45f * R2).WithAlpha(Alpha);
				const ColorRGBA Halo = Tint(Colors.m_Ember, Colors.m_Flame, 0.55f).WithAlpha(Alpha * 0.50f);
				// 外圈沿上升方向拉长，像被上升气流带出的小火苗；内芯是亮黄白点。
				IgnisDiamond(vOut, Center, IgnisUp() * (Size * 2.1f), T * (Size * 1.3f), Halo, true);
				IgnisDiamond(vOut, Center, N * (Size * 0.85f), T * (Size * 0.85f), Core, true);
			}
		});
	}

	// 热浪丝：从火焰边缘升起、朝尾向飘散的折线，长度随速度能量增加。
	void IgnisBuildWisps(std::vector<SQuad> &vOut, const SStyleSample *pSamples, size_t Count, bool Preset, float PixelSize, unsigned Seed)
	{
		const float Spacing = 58.0f + Random(Seed + 0x6du) * 26.0f;
		const float Offset = Random(Seed + 0x71u) * Spacing;
		int Emitted = 0;
		Anchors(pSamples, Count, Spacing, Offset, size_t(IGNIS_WISP_ANCHORS), [&](const SStyleSample &S, unsigned Id) {
			if(S.m_Head < 10.0f || Emitted >= IGNIS_WISP_ANCHORS)
				return;
			// 约四分之一的锚点不画热浪丝：疏密不均才像热扰动，而不是均匀花纹。
			if((Hash(Id + Seed + 0x77u) & 3u) == 0)
				return;
			++Emitted;
			const float Width = std::max(S.m_Width, 0.001f);
			const float Energy = std::clamp(S.m_Energy, 0.0f, 1.0f);
			const float Remaining = std::clamp(S.m_Remaining, 0.0f, 1.0f);
			const SFlameColors Colors = IgnisColors(S.m_Tint, Preset);
			const vec2 Base = Unit(Tangent(S) * 0.50f + IgnisUp() * 0.86f);
			const float Length = Width * (0.34f + 0.66f * Energy) * (0.45f + 0.55f * Remaining) * (0.60f + 0.80f * Random(Id + Seed + 0x7bu));
			const float Thin = std::max(PixelSize * 0.80f, Width * 0.020f);
			const vec2 Start = S.m_Pos + S.m_Normal * ((Random(Id + Seed + 0x7fu) - 0.5f) * Width * 1.25f) + IgnisUp() * (Width * 0.15f);
			const vec2 Dir1 = Unit(Base + direction(Random(Id + Seed + 0x83u) * 2.0f * pi) * 0.60f);
			const vec2 Dir2 = Unit(Base + direction(Random(Id + Seed + 0x89u) * 2.0f * pi) * 0.80f);
			const vec2 Elbow = Start + Dir1 * (Length * 0.52f);
			const vec2 Tip = Elbow + Dir2 * (Length * 0.52f) + IgnisUp() * (Length * 0.30f);
			const float Alpha = S.m_Alpha * 0.34f * Remaining * (0.60f + 0.40f * Random(Id + Seed + 0x8fu));
			const ColorRGBA Color = Tint(Colors.m_Flame, Colors.m_Ember, 0.30f).WithAlpha(Alpha);
			Stroke(vOut, Start, Elbow, Thin, Thin * 0.72f, Color, PixelSize, true);
			Stroke(vOut, Elbow, Tip, Thin * 0.72f, 0.0f, Color.WithAlpha(Alpha * 0.8f), PixelSize, true);
		});
	}

	// 沿轨迹的热扰动短线：跨过火芯的细短线随热浪上下波动，模拟 HTML 末尾的扰动线。
	void IgnisBuildHeatStreaks(std::vector<SQuad> &vOut, const SStyleSample *pSamples, size_t Count, bool Preset, float PixelSize, unsigned Seed)
	{
		const float Spacing = 38.0f + Random(Seed + 0x97u) * 22.0f;
		const float Offset = Random(Seed + 0x9bu) * Spacing;
		const float Phase = IgnisPhase(Seed, 0xa1u);
		Anchors(pSamples, Count, Spacing, Offset, IGNIS_HEAT_ANCHORS, [&](const SStyleSample &S, unsigned Id) {
			if(S.m_Head < 8.0f)
				return;
			const float Width = std::max(S.m_Width, 0.001f);
			const float Remaining = std::clamp(S.m_Remaining, 0.0f, 1.0f);
			const SFlameColors Colors = IgnisColors(S.m_Tint, Preset);
			const float Wave = std::sin(S.m_Head * 0.070f + S.m_Age * 1.5f + Phase + float(Id) * 0.37f);
			const vec2 Center = S.m_Pos + S.m_Normal * (Width * 0.60f * Wave);
			const vec2 Dir = Unit(Tangent(S) + S.m_Normal * (Wave * 0.45f));
			const float Along = Width * (0.24f + 0.42f * std::clamp(S.m_Energy, 0.0f, 1.0f)) * (0.45f + 0.55f * Remaining);
			const float Thin = std::max(0.28f, PixelSize * 0.55f);
			const float Alpha = S.m_Alpha * 0.22f * Remaining * (0.35f + 0.65f * Random(Id + Seed + 0xa5u));
			Stroke(vOut, Center - Dir * Along, Center + Dir * Along, Thin * 0.7f, Thin * 0.7f, Tint(Colors.m_Flame, Colors.m_White, 0.2f).WithAlpha(Alpha), PixelSize, true);
		});
	}
}

void qm_tee_trail::style_detail::BuildIgnis(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut)
{
	if(pSamples == nullptr || Count < 2 || Count > MAX_RENDER_POINTS)
		return;
	if(!std::isfinite(Width) || !std::isfinite(PixelSize))
		return;
	PixelSize = std::clamp(PixelSize, 0.025f, 8.0f);
	for(size_t i = 0; i < Count; ++i)
		if(!IgnisSampleFinite(pSamples[i]))
			return;
	const float Total = pSamples[Count - 1].m_Head;
	if(!std::isfinite(Total) || !(Total > 0.005f))
		return;

	SFlameField Field;
	if(IgnisBuildFlameField(pSamples, Count, Field) < 2)
		return;
	const float HeadPulse = IgnisHeadPulse(pSamples[0], Seed);

	// 1. 普通混合压底：必须全部先于加法发光层入队。
	for(int Layer = 0; Layer < IGNIS_NORMAL_LAYERS; ++Layer)
		IgnisBuildBand(vOut, Field, s_aIgnisLayers[Layer], Preset, PixelSize, HeadPulse, Seed);
	// 2. 加法混合：四层火焰光带。
	for(int Layer = IGNIS_NORMAL_LAYERS; Layer < IGNIS_LAYER_COUNT; ++Layer)
		IgnisBuildBand(vOut, Field, s_aIgnisLayers[Layer], Preset, PixelSize, HeadPulse, Seed);
	// 3. 头球、余烬、热浪丝与热扰动短线，全部为加法混合。
	IgnisBuildHeadFireball(vOut, pSamples[0], Preset, HeadPulse, Seed);
	IgnisBuildEmbers(vOut, pSamples, Count, Preset, Seed);
	IgnisBuildWisps(vOut, pSamples, Count, Preset, PixelSize, Seed);
	IgnisBuildHeatStreaks(vOut, pSamples, Count, Preset, PixelSize, Seed);
	(void)Width;
}
