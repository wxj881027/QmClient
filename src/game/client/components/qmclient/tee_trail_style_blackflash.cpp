#include "tee_trail_style_common.h"

#include <base/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

// Black Flash: Void Rift（黑闪：虚空裂隙）。
// 所有形状都落在世界空间的四边形里，不做全屏闪白或负片：
//   普通混合批：深红外扩场 → 深红轮廓 → 纯黑内核 → 更黑的虚空心 → 碎片暗面 → 爆发暗斑 → 头部黑洞暗核；
//   加法混合批：暗红薄雾 → 两侧绯红脉络 → 空间裂纹闪电（三遍描边 + 分叉）→ 轨迹内联闪电
//               → 碎片绯红描边与反光 → 头部三圈吸积环与热点 → 周期性黑闪爆发的红边。
// 普通混合层全部先于加法混合层输出；每个顶点透明度都乘入采样透明度；
// 随机量只来自 Seed、累计弧长与样本字段（m_Distance/m_Age/m_Head/m_Remaining/m_Energy），
// 因此同一输入与 Seed 逐顶点一致，不读取时钟、全局状态或静态计数器。
namespace
{
	using namespace qm_tee_trail;
	using namespace qm_tee_trail::style_detail;

	// 固定上限的栈缓冲；装饰数量与轨迹采样密度无关，最坏情况仍在 MAX_QUADS 预算内。
	constexpr size_t MAX_BOLT_POINTS = 14;
	constexpr size_t MAX_CRACKS = 7;
	constexpr size_t MAX_INLINE_BOLTS = 6;
	constexpr size_t MAX_SHARDS = 12;
	constexpr size_t MAX_BURSTS = 4;

	// 各层宽带抽稀上限：宽而柔的层不需要每个采样一个截面。
	constexpr size_t SHELL_POINTS = 72;
	constexpr size_t RIM_POINTS = 72;
	constexpr size_t CORE_POINTS = 120;
	constexpr size_t VOID_POINTS = 64;
	constexpr size_t HAZE_POINTS = 56;
	constexpr size_t VEIN_POINTS = 88;
	constexpr size_t HAIR_POINTS = 72;

	struct SPalette
	{
		ColorRGBA m_Core; // 近黑内核
		ColorRGBA m_Void; // 更纯的黑，用于虚空心、碎片暗面与黑洞暗核
		ColorRGBA m_Shell; // 深红外扩场
		ColorRGBA m_Rim; // 深红轮廓
		ColorRGBA m_Ember; // 闪电最外层暗红
		ColorRGBA m_Crimson; // 绯红脉络
		ColorRGBA m_Bright; // 亮绯
		ColorRGBA m_Spark; // 粉白高光
	};

	// Preset 只替换颜色，不参与任何几何、数量或透明度计算。
	SPalette Palette(const SStyleSample &S, bool Preset)
	{
		SPalette P;
		if(Preset)
		{
			P.m_Core = ColorRGBA(0x000000u);
			P.m_Void = ColorRGBA(0x000000u);
			P.m_Shell = ColorRGBA(0x28050fu);
			P.m_Rim = ColorRGBA(0x3a0814u);
			P.m_Ember = ColorRGBA(0x500014u);
			P.m_Crimson = ColorRGBA(0xb41428u);
			P.m_Bright = ColorRGBA(0xff3c50u);
			P.m_Spark = ColorRGBA(0xffdce6u);
		}
		else
		{
			// 自定义颜色：黑核保持近黑，绯红系从玩家颜色派生，保留黑闪的明暗结构。
			P.m_Core = Tint(S.m_Tint, ColorRGBA(0x000000u), 0.90f);
			P.m_Void = Tint(S.m_Tint, ColorRGBA(0x000000u), 0.97f);
			P.m_Shell = Tint(S.m_Tint, ColorRGBA(0x28050fu), 0.60f);
			P.m_Rim = Tint(S.m_Tint, ColorRGBA(0x3a0814u), 0.60f);
			P.m_Ember = Tint(S.m_Tint, ColorRGBA(0x500014u), 0.65f);
			P.m_Crimson = Tint(S.m_Tint, ColorRGBA(0xb41428u), 0.50f);
			P.m_Bright = Tint(S.m_Tint, ColorRGBA(0xff3c50u), 0.55f);
			P.m_Spark = Tint(S.m_Tint, ColorRGBA(0xffffffu), 0.80f);
		}
		return P;
	}

	inline ColorRGBA AlphaOf(const ColorRGBA &Color, float Alpha)
	{
		return Color.WithAlpha(std::clamp(Alpha, 0.0f, 1.0f));
	}

	// 只由样本字段与 Seed 推导的周期相位，代替 HTML 里的墙钟时间。
	inline float Wave(float Phase, unsigned Seed, float Offset = 0.0f)
	{
		return std::sin(Phase + Offset + Random(Seed) * 2.0f * pi);
	}

	// 按 Count 比例抽稀采样索引，宽带的四边形数量不随轨迹细分增长。
	inline size_t BandIndex(size_t Index, size_t Count, size_t Total)
	{
		return Count > 1 ? (Index * (Total - 1)) / (Count - 1) : 0;
	}

	// 装饰的世界尺度参考：随配置宽度放大，但用平方根抑制过厚拖尾下的巨型裂纹。
	inline float DecorScale(float SampleWidth, float Reference)
	{
		return std::sqrt(std::max(SampleWidth, 0.01f) * Reference);
	}

	// 闪电折线：沿 A→B 生成锯齿中点，抖动幅度向末端衰减，首尾严格落在端点上。
	size_t Bolt(std::array<vec2, MAX_BOLT_POINTS> &aPoints, vec2 A, vec2 B, int Segments, float Jitter, unsigned Seed)
	{
		Segments = std::clamp(Segments, 2, int(MAX_BOLT_POINTS) - 2);
		const vec2 Delta = B - A;
		if(!std::isfinite(Delta.x) || !std::isfinite(Delta.y))
			return 0;
		const vec2 Dir = Unit(Delta, vec2(1.0f, 0.0f));
		const vec2 Perp = vec2(-Dir.y, Dir.x);
		aPoints[0] = A;
		for(int i = 1; i < Segments; ++i)
		{
			const float T = float(i) / float(Segments);
			const float Fade = 1.0f - 0.35f * T;
			const float Offset = (Random(Seed + unsigned(i) * 73u) - 0.5f) * Jitter * Fade;
			aPoints[i] = mix(A, B, T) + Perp * Offset;
		}
		aPoints[Segments] = B;
		return size_t(Segments) + 1;
	}

	// 一遍描边：宽度沿折线由 StartWidth 收敛到 EndWidth，像素感由 PixelSize 换算。
	void BoltPass(std::vector<SQuad> &vOut, const std::array<vec2, MAX_BOLT_POINTS> &aPoints, size_t Count,
		float StartWidth, float EndWidth, ColorRGBA Color, float PixelSize)
	{
		if(Count < 2)
			return;
		for(size_t i = 0; i + 1 < Count; ++i)
		{
			const float T0 = float(i) / float(Count - 1);
			const float T1 = float(i + 1) / float(Count - 1);
			Stroke(vOut, aPoints[i], aPoints[i + 1], mix(StartWidth, EndWidth, T0), mix(StartWidth, EndWidth, T1), Color, PixelSize, true);
		}
	}

	// 圆盘用中心扇形拼成四边形：中心顶点不透明度更高，边缘更低，近似 HTML 的径向渐变。
	void Disc(std::vector<SQuad> &vOut, vec2 Center, float Radius, ColorRGBA CenterColor, ColorRGBA EdgeColor, int Segments, bool Additive)
	{
		for(int k = 0; k < Segments; ++k)
		{
			const vec2 P0 = Center + direction(2.0f * pi * float(k) / float(Segments)) * Radius;
			const vec2 P1 = Center + direction(2.0f * pi * float(k + 1) / float(Segments)) * Radius;
			Quad(vOut, Center, P0, P1, P1, CenterColor, EdgeColor, EdgeColor, EdgeColor, Additive);
		}
	}

	// 圆环：内外半径之间的细长四边形，外缘更淡，用来拼吸积环与爆发红边。
	void Ring(std::vector<SQuad> &vOut, vec2 Center, float Inner, float Outer, ColorRGBA InnerColor, ColorRGBA OuterColor, int Segments, bool Additive)
	{
		for(int k = 0; k < Segments; ++k)
		{
			const vec2 D0 = direction(2.0f * pi * float(k) / float(Segments));
			const vec2 D1 = direction(2.0f * pi * float(k + 1) / float(Segments));
			Quad(vOut, Center + D0 * Inner, Center + D1 * Inner, Center + D1 * Outer, Center + D0 * Outer,
				InnerColor, InnerColor, OuterColor, OuterColor, Additive);
		}
	}

	// 普通批收集的空间碎片，加法批再复用同一份顶点描边，避免二次哈希与层序交错。
	struct SShard
	{
		vec2 m_Center;
		std::array<vec2, 5> m_aVertex;
		int m_Count;
		ColorRGBA m_Void, m_Edge, m_Bright, m_Spark;
		float m_Alpha;
		float m_SparkAlpha;
	};

	// 普通批收集的周期性黑闪爆发，加法批再补红边与四芒星。
	struct SBurst
	{
		vec2 m_Center;
		float m_Radius;
		float m_Alpha;
		float m_Angle;
		ColorRGBA m_Void, m_Crimson, m_Bright, m_Spark;
	};
}

void qm_tee_trail::style_detail::BuildBlackFlash(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut)
{
	if(Count < 2 || Count > MAX_RENDER_POINTS || pSamples == nullptr)
		return;
	if(!std::isfinite(Width) || !std::isfinite(PixelSize))
		return;
	// 生产路径已经校验过轨迹；这里再拒绝非有限输入，避免把 NaN/Inf 写进顶点缓冲。
	for(size_t i = 0; i < Count; ++i)
	{
		const SStyleSample &S = pSamples[i];
		if(!std::isfinite(S.m_Pos.x) || !std::isfinite(S.m_Pos.y) ||
			!std::isfinite(S.m_Normal.x) || !std::isfinite(S.m_Normal.y) ||
			!std::isfinite(S.m_Distance) || !std::isfinite(S.m_Age) ||
			!std::isfinite(S.m_Head) || !std::isfinite(S.m_Width) ||
			!std::isfinite(S.m_Alpha) || !std::isfinite(S.m_Energy) ||
			!std::isfinite(S.m_Remaining))
			return;
	}
	// 世界尺度参考：配置宽度决定装饰的最小尺度，极端配置下仍保持可读比例。
	const float Reference = std::clamp(Width, 0.001f, 64.0f);
	const float Px = std::clamp(PixelSize, 0.001f, 8.0f);

	// ===== 普通混合批次：全部会遮挡地图的"虚空体" =====

	// N1 深红外扩场：最宽的一层暗红，先把黑体周围压暗，浅色地图上形成剪影。
	{
		std::array<SBandPoint, MAX_RENDER_POINTS> aBand;
		const size_t N = std::min(Count, SHELL_POINTS);
		for(size_t j = 0; j < N; ++j)
		{
			const SStyleSample &S = pSamples[BandIndex(j, N, Count)];
			const SPalette P = Palette(S, Preset);
			const float Pulse = 0.90f + 0.12f * Wave(float(S.m_Distance) * 0.035f + S.m_Age * 1.6f, Seed + 17u);
			const float Half = S.m_Width * 1.38f * Pulse;
			const float Distort = S.m_Width * 0.12f * Noise(S.m_Distance / 26.0, Seed + 23u);
			aBand[j] = {S.m_Pos + S.m_Normal * Distort, Half, Half, AlphaOf(P.m_Shell, S.m_Alpha * 0.24f)};
		}
		Band(vOut, aBand.data(), N, 0.16f, Px, false);
	}

	// N2 深红轮廓：略窄且更实的暗红，让黑色内核在深色地图外缘有明确轮廓。
	{
		std::array<SBandPoint, MAX_RENDER_POINTS> aBand;
		const size_t N = std::min(Count, RIM_POINTS);
		for(size_t j = 0; j < N; ++j)
		{
			const SStyleSample &S = pSamples[BandIndex(j, N, Count)];
			const SPalette P = Palette(S, Preset);
			const float Pulse = 0.94f + 0.08f * Wave(float(S.m_Distance) * 0.05f + S.m_Age * 2.1f, Seed + 31u);
			const float Half = S.m_Width * 0.92f * Pulse;
			const float Distort = S.m_Width * 0.06f * Noise(S.m_Distance / 18.0, Seed + 41u);
			aBand[j] = {S.m_Pos + S.m_Normal * Distort, Half, Half, AlphaOf(P.m_Rim, S.m_Alpha * 0.44f)};
		}
		Band(vOut, aBand.data(), N, 0.10f, Px, false);
	}

	// N3 纯黑内核：黑闪的主体。普通混合会挡住地图，这正是"虚空"的可见方式。
	{
		std::array<SBandPoint, MAX_RENDER_POINTS> aBand;
		const size_t N = std::min(Count, CORE_POINTS);
		for(size_t j = 0; j < N; ++j)
		{
			const SStyleSample &S = pSamples[BandIndex(j, N, Count)];
			const SPalette P = Palette(S, Preset);
			const float Pulse = 0.96f + 0.07f * Wave(float(S.m_Distance) * 0.07f + S.m_Age * 2.7f, Seed + 53u);
			const float Ragged = 0.94f + 0.10f * Noise(S.m_Distance / 13.0, Seed + 61u);
			const float Half = S.m_Width * (0.52f + 0.07f * S.m_Energy) * Pulse * Ragged;
			const float Distort = S.m_Width * 0.05f * Noise(S.m_Distance / 21.0, Seed + 67u);
			aBand[j] = {S.m_Pos + S.m_Normal * Distort, Half, Half, AlphaOf(P.m_Core, S.m_Alpha * 0.90f)};
		}
		Band(vOut, aBand.data(), N, 0.05f, Px, false);
	}

	// N4 虚空心：更黑更窄的一段，给内核加一点吸收光的纵深。
	{
		std::array<SBandPoint, MAX_RENDER_POINTS> aBand;
		const size_t N = std::min(Count, VOID_POINTS);
		for(size_t j = 0; j < N; ++j)
		{
			const SStyleSample &S = pSamples[BandIndex(j, N, Count)];
			const SPalette P = Palette(S, Preset);
			const float Half = S.m_Width * (0.27f + 0.05f * S.m_Energy);
			aBand[j] = {S.m_Pos, Half, Half, AlphaOf(P.m_Void, S.m_Alpha * 0.96f)};
		}
		Band(vOut, aBand.data(), N, 0.025f, Px, false);
	}

	// N5 空间碎片暗面：深色多边形先在其所属的普通批次里画完，加法批只补描边与反光。
	std::array<SShard, MAX_SHARDS> aShards;
	size_t ShardCount = 0;
	Anchors(pSamples, Count, 68.0f, Random(Seed + 401u) * 68.0f, 8, [&](const SStyleSample &S, unsigned Id) {
		if(ShardCount + 2 > MAX_SHARDS || S.m_Head < 6.0f || S.m_Remaining < 0.18f || S.m_Width < 0.05f)
			return;
		const unsigned H0 = Hash(Id + Seed + 0x3du);
		if(Random(H0) > 0.72f)
			return;
		const SPalette P = Palette(S, Preset);
		const float Scale = DecorScale(S.m_Width, Reference);
		const int Pieces = 2;
		for(int Piece = 0; Piece < Pieces && ShardCount < MAX_SHARDS; ++Piece)
		{
			const unsigned H = Hash(H0 + unsigned(Piece) * 0x9e3779b9u + 0x51u);
			const float Angle = Random(H) * 2.0f * pi;
			const vec2 Dir = direction(Angle);
			// 碎片从拖尾侧向抛出后沿径向漂移；漂移量由锚点年龄推导，稳定且不重播种。
			const float Side = (Hash(H + 1u) & 1u) ? 1.0f : -1.0f;
			const float Dist = Scale * (0.55f + Random(Hash(H + 2u)) * 1.8f) + std::min(S.m_Age, 6.0f) * (0.25f + Random(Hash(H + 3u)) * 0.8f);
			const vec2 Center = S.m_Pos + S.m_Normal * (Side * (S.m_Width * 0.75f + Scale * 0.35f)) + Dir * Dist +
					    Tangent(S) * ((Random(Hash(H + 4u)) - 0.5f) * Scale * 0.9f);
			const float Radius = Scale * (0.42f + Random(Hash(H + 5u)) * 1.35f);
			const int VertexCount = 3 + int(Hash(H + 6u) % 3u);
			const float Spin = Random(Hash(H + 7u)) * 2.0f * pi + std::min(S.m_Age, 40.0f) * (Random(Hash(H + 8u)) - 0.5f) * 2.4f;
			const float Fade = S.m_Alpha * Smooth((S.m_Remaining - 0.08f) / 0.35f) * (0.45f + 0.55f * S.m_Remaining);
			if(Fade < 0.004f)
				continue;
			SShard Shard;
			Shard.m_Center = Center;
			Shard.m_Count = VertexCount;
			Shard.m_Void = P.m_Void;
			Shard.m_Edge = P.m_Crimson;
			Shard.m_Bright = P.m_Bright;
			Shard.m_Spark = P.m_Spark;
			Shard.m_Alpha = Fade;
			const float Shimmer = 0.5f + 0.5f * Wave(S.m_Age * 7.5f + float(Piece) * 2.1f, H + 13u);
			// 极低 alpha 的粉色反光面，只负责在深色地图上提示玻璃质感。
			Shard.m_SparkAlpha = Fade * (0.06f + 0.12f * std::max(Shimmer - 0.5f, 0.0f));
			for(int Vertex = 0; Vertex < VertexCount; ++Vertex)
			{
				const float VertexAngle = Spin + 2.0f * pi * float(Vertex) / float(VertexCount);
				const float VertexRadius = Radius * (0.42f + 0.58f * Random(Hash(H + 17u + unsigned(Vertex) * 29u)));
				Shard.m_aVertex[Vertex] = Center + direction(VertexAngle) * VertexRadius;
			}
			// 中心扇形填充：深色暗面，中心比边缘更实。
			for(int Triangle = 0; Triangle + 1 < VertexCount; ++Triangle)
				Quad(vOut, Center, Shard.m_aVertex[Triangle], Shard.m_aVertex[Triangle + 1], Shard.m_aVertex[Triangle + 1],
					AlphaOf(P.m_Void, Fade * 0.85f), AlphaOf(P.m_Void, Fade * 0.55f), AlphaOf(P.m_Void, Fade * 0.55f), AlphaOf(P.m_Void, Fade * 0.55f), false);
			aShards[ShardCount++] = Shard;
		}
	});

	// N6 周期性黑闪爆发：暗斑与暗红环用普通混合压出"空间塌陷"，红边留到加法批。
	std::array<SBurst, MAX_BURSTS> aBursts;
	size_t BurstCount = 0;
	Anchors(pSamples, Count, 92.0f, Random(Seed + 503u) * 92.0f, 8, [&](const SStyleSample &S, unsigned Id) {
		if(BurstCount >= MAX_BURSTS || S.m_Head < 12.0f || S.m_Remaining < 0.16f || S.m_Width < 0.05f)
			return;
		const unsigned H = Hash(Id + Seed + 0x71u);
		if(Random(H) > 0.70f)
			return;
		// 相位推进只依赖锚点年龄：同一处爆发会按固定周期亮起再熄灭。
		const float Pulse = std::sin(S.m_Age * 3.4f + Random(Hash(H + 1u)) * 2.0f * pi);
		if(Pulse < 0.10f)
			return;
		const float Scale = DecorScale(S.m_Width, Reference);
		SBurst Burst;
		Burst.m_Center = S.m_Pos;
		Burst.m_Radius = Scale * (1.0f + Random(Hash(H + 2u)) * 0.9f);
		Burst.m_Angle = Random(Hash(H + 3u)) * 2.0f * pi + S.m_Age * 0.8f;
		Burst.m_Alpha = S.m_Alpha * Smooth((Pulse - 0.10f) / 0.90f) * S.m_Remaining;
		const SPalette P = Palette(S, Preset);
		Burst.m_Void = P.m_Void;
		Burst.m_Crimson = P.m_Crimson;
		Burst.m_Bright = P.m_Bright;
		Burst.m_Spark = P.m_Spark;
		if(Burst.m_Alpha < 0.01f)
			return;
		aBursts[BurstCount++] = Burst;
		// 暗斑：两层暗核 + 一圈暗红塌陷环，浅色地图上也能看出"空间被吞掉"。
		Disc(vOut, Burst.m_Center, Burst.m_Radius, AlphaOf(P.m_Void, Burst.m_Alpha * 0.62f), AlphaOf(P.m_Void, 0.0f), 8, false);
		Disc(vOut, Burst.m_Center, Burst.m_Radius * 0.52f, AlphaOf(P.m_Void, Burst.m_Alpha * 0.74f), AlphaOf(P.m_Void, Burst.m_Alpha * 0.22f), 8, false);
		Ring(vOut, Burst.m_Center, Burst.m_Radius * 1.0f, Burst.m_Radius * 1.24f,
			AlphaOf(P.m_Rim, Burst.m_Alpha * 0.48f), AlphaOf(P.m_Rim, 0.0f), 10, false);
	});

	// N7 头部黑洞暗核：普通混合的最后一部分，压在地图与其他虚空体之上。
	const SStyleSample &Head = pSamples[0];
	const float HeadAlpha = Head.m_Alpha;
	const float HeadPulse = 0.82f + 0.18f * Wave(Head.m_Age * 4.2f, Seed + 601u);
	const float HeadRadius = Head.m_Width * (0.72f + 0.55f * Head.m_Energy);
	if(HeadAlpha > 0.001f && Head.m_Width > 0.05f && HeadRadius > 0.001f)
	{
		const SPalette P = Palette(Head, Preset);
		// 暗红引力晕：把黑洞周围的地图压暗，轮廓因此在深浅地图都成立。
		Ring(vOut, Head.m_Pos, HeadRadius * 1.02f, HeadRadius * 1.44f,
			AlphaOf(P.m_Rim, HeadAlpha * 0.44f), AlphaOf(P.m_Rim, 0.0f), 16, false);
		Disc(vOut, Head.m_Pos, HeadRadius, AlphaOf(P.m_Void, HeadAlpha * 0.78f), AlphaOf(P.m_Void, 0.0f), 14, false);
		Disc(vOut, Head.m_Pos, HeadRadius * 0.80f, AlphaOf(P.m_Void, HeadAlpha * 0.86f), AlphaOf(P.m_Void, HeadAlpha * 0.14f), 14, false);
		Disc(vOut, Head.m_Pos, HeadRadius * 0.56f, AlphaOf(P.m_Void, HeadAlpha * 0.94f), AlphaOf(P.m_Void, HeadAlpha * 0.30f), 14, false);
		Disc(vOut, Head.m_Pos, HeadRadius * 0.34f, AlphaOf(P.m_Core, HeadAlpha * 0.98f), AlphaOf(P.m_Core, HeadAlpha * 0.62f), 14, false);
	}

	// ===== 加法混合批次：全部发光层一次性排在普通层之后 =====

	// 头部黑洞半径内让位：绯红脉络与闪电不横穿黑洞中心，只从洞缘外开始发光。
	const auto HeadMask = [&](const SStyleSample &S) {
		const float Span = std::max(Head.m_Width * 1.4f, 0.001f);
		return Smooth((S.m_Head - Head.m_Width * 2.2f) / Span);
	};

	// A0 暗红薄雾：比外壳更宽的极淡红场，深色地图上让虚空边缘透气。
	{
		std::array<SBandPoint, MAX_RENDER_POINTS> aBand;
		const size_t N = std::min(Count, HAZE_POINTS);
		for(size_t j = 0; j < N; ++j)
		{
			const SStyleSample &S = pSamples[BandIndex(j, N, Count)];
			const SPalette P = Palette(S, Preset);
			const float Pulse = 0.86f + 0.14f * Wave(float(S.m_Distance) * 0.03f + S.m_Age * 1.2f, Seed + 71u);
			const float Half = S.m_Width * 1.90f * Pulse;
			const float Distort = S.m_Width * 0.16f * Noise(S.m_Distance / 30.0, Seed + 79u);
			aBand[j] = {S.m_Pos + S.m_Normal * Distort, Half, Half, AlphaOf(P.m_Ember, S.m_Alpha * 0.10f * HeadMask(S))};
		}
		Band(vOut, aBand.data(), N, 0.35f, Px, true);
	}

	// A1 绯红脉络（宽）：中心线沿法向脉动偏移，压在内核与外壳的交界上形成绯红边缘。
	{
		std::array<SBandPoint, MAX_RENDER_POINTS> aBand;
		const size_t N = std::min(Count, VEIN_POINTS);
		for(size_t j = 0; j < N; ++j)
		{
			const SStyleSample &S = pSamples[BandIndex(j, N, Count)];
			const SPalette P = Palette(S, Preset);
			const float Throb = 0.72f + 0.38f * Wave(float(S.m_Head) * 0.07f + S.m_Age * 2.3f, Seed + 107u);
			const float Fracture = 0.55f + 0.45f * Smooth((Noise(S.m_Distance / 9.0, Seed + 113u) + 0.5f) * 0.9f);
			const float Half = S.m_Width * 0.34f * Throb;
			const float Offset = S.m_Width * (0.58f + 0.12f * Wave(float(S.m_Head) * 0.045f + S.m_Age * 1.7f, Seed + 127u, 1.3f) +
								 0.07f * Noise(S.m_Distance / 11.0, Seed + 137u));
			aBand[j] = {S.m_Pos + S.m_Normal * Offset, Half, Half, AlphaOf(P.m_Crimson, S.m_Alpha * 0.30f * Throb * Fracture * HeadMask(S))};
		}
		Band(vOut, aBand.data(), N, 0.16f, Px, true);
	}

	// A2 亮绯细脉络：另一侧更细更亮，沿里程断裂成电气段，形成不对称的裂纹感。
	{
		std::array<SBandPoint, MAX_RENDER_POINTS> aBand;
		const size_t N = std::min(Count, HAIR_POINTS);
		for(size_t j = 0; j < N; ++j)
		{
			const SStyleSample &S = pSamples[BandIndex(j, N, Count)];
			const SPalette P = Palette(S, Preset);
			const float Throb = 0.68f + 0.42f * Wave(float(S.m_Head) * 0.09f + S.m_Age * 2.9f, Seed + 131u, -0.7f);
			const float Fracture = 0.40f + 0.60f * Smooth((Noise(S.m_Distance / 7.0, Seed + 139u) + 0.45f) * 1.1f);
			const float Half = S.m_Width * 0.13f * Throb;
			const float Offset = -S.m_Width * (0.66f + 0.12f * Wave(float(S.m_Head) * 0.06f + S.m_Age * 2.2f, Seed + 149u, 0.4f) +
								  0.06f * Noise(S.m_Distance / 10.0, Seed + 151u));
			aBand[j] = {S.m_Pos + S.m_Normal * Offset, Half, Half, AlphaOf(P.m_Bright, S.m_Alpha * 0.40f * Throb * Fracture * HeadMask(S))};
		}
		Band(vOut, aBand.data(), N, 0.22f, Px, true);
	}

	// A3 空间裂纹：从锚点沿法向左右交替外伸，主链两折分叉，全部由 Seed 与锚点 Id 决定。
	int CrackCount = 0;
	Anchors(pSamples, Count, 46.0f, Random(Seed + 211u) * 46.0f, 16, [&](const SStyleSample &S, unsigned Id) {
		if(CrackCount >= int(MAX_CRACKS) || S.m_Head < Head.m_Width * 3.4f || S.m_Remaining < 0.26f || S.m_Width < 0.05f)
			return;
		const unsigned H = Hash(Id + Seed + 0x1fu);
		if(Random(H) > 0.62f)
			return;
		++CrackCount;
		const SPalette P = Palette(S, Preset);
		const float Scale = DecorScale(S.m_Width, Reference);
		const float Sign = (Hash(H + 1u) & 1u) ? 1.0f : -1.0f;
		vec2 Outward = S.m_Normal * Sign;
		// 裂纹永远背离头部黑洞，避免闪电横穿洞心。
		const vec2 ToHead = Head.m_Pos - S.m_Pos;
		if(dot(Outward, ToHead) > 0.0f)
			Outward = -Outward;
		const float Len = Scale * (3.2f + Random(Hash(H + 2u)) * 4.8f) * (0.55f + 0.45f * S.m_Remaining);
		const float Fade = S.m_Alpha * (0.62f + 0.38f * Wave(S.m_Age * 5.5f, H + 3u));
		if(Fade < 0.01f || Len < 0.01f)
			return;
		const vec2 Root = S.m_Pos + Outward * (S.m_Width * 0.30f);
		const vec2 Tip = Root + Outward * Len + Tangent(S) * ((Random(Hash(H + 4u)) - 0.5f) * Len * 0.5f);
		if(distance(Tip, Head.m_Pos) < HeadRadius * 1.5f || distance(Root, Head.m_Pos) < HeadRadius * 1.5f)
			return;
		std::array<vec2, MAX_BOLT_POINTS> aPoints;
		const size_t PointCount = Bolt(aPoints, Root, Tip, 8 + int(Hash(H + 5u) % 4u), Len * 0.18f, H + 6u);
		if(PointCount < 2)
			return;
		// 三遍描边：暗红外晕、绯红主体、粉白电芯，逐层收窄；最外层刻意加宽，避免预览里变发丝。
		BoltPass(vOut, aPoints, PointCount, 7.0f * Px, 3.4f * Px, AlphaOf(P.m_Ember, Fade * 0.60f), Px);
		BoltPass(vOut, aPoints, PointCount, 3.0f * Px, 1.5f * Px, AlphaOf(P.m_Crimson, Fade * 0.90f), Px);
		BoltPass(vOut, aPoints, PointCount, 1.0f * Px, 0.4f * Px, AlphaOf(P.m_Spark, Fade * 1.0f), Px);
		// 每条主链再抽 2~4 条分叉，方向与长度由同一组哈希决定。
		const int BranchCount = 2 + int(Hash(H + 7u) % 3u);
		for(int Branch = 0; Branch < BranchCount; ++Branch)
		{
			const unsigned HB = Hash(H + 11u + unsigned(Branch) * 0x27d4eb2du);
			const float RootT = 0.25f + Random(HB) * 0.55f;
			const size_t RootIndex = std::min(PointCount - 1, size_t(RootT * float(PointCount - 1)));
			const vec2 BranchRoot = aPoints[RootIndex];
			const vec2 MainDir = Unit(Tip - Root, vec2(0.0f, 1.0f));
			const vec2 BranchDir = rotate(MainDir, (Random(Hash(HB + 1u)) - 0.5f) * 2.4f);
			const float BranchLen = Len * (0.20f + Random(Hash(HB + 2u)) * 0.34f) * (0.6f + 0.4f * S.m_Remaining);
			const vec2 BranchTip = BranchRoot + BranchDir * BranchLen + Tangent(S) * ((Random(Hash(HB + 3u)) - 0.5f) * BranchLen * 0.5f);
			std::array<vec2, MAX_BOLT_POINTS> aBranch;
			const size_t BranchPoints = Bolt(aBranch, BranchRoot, BranchTip, 4, BranchLen * 0.28f, HB + 4u);
			BoltPass(vOut, aBranch, BranchPoints, 1.9f * Px, 0.8f * Px, AlphaOf(P.m_Crimson, Fade * 0.60f), Px);
			BoltPass(vOut, aBranch, BranchPoints, 0.65f * Px, 0.28f * Px, AlphaOf(P.m_Spark, Fade * 0.75f), Px);
		}
	});

	// A4 轨迹内联闪电：贴在内核边缘、每段双遍描边的短促放电。
	int InlineCount = 0;
	Anchors(pSamples, Count, 26.0f, Random(Seed + 307u) * 26.0f, 16, [&](const SStyleSample &S, unsigned Id) {
		if(InlineCount >= int(MAX_INLINE_BOLTS) || S.m_Head < Head.m_Width * 3.2f || S.m_Remaining < 0.10f || S.m_Width < 0.05f)
			return;
		const unsigned H = Hash(Id + Seed + 0x2bu);
		if(Random(H) > 0.50f)
			return;
		++InlineCount;
		const SPalette P = Palette(S, Preset);
		const float Scale = DecorScale(S.m_Width, Reference);
		const float Sign = (Hash(H + 1u) & 1u) ? 1.0f : -1.0f;
		vec2 InlineOut = S.m_Normal * Sign;
		if(dot(InlineOut, Head.m_Pos - S.m_Pos) > 0.0f)
			InlineOut = -InlineOut;
		const float Fade = S.m_Alpha * Smooth(S.m_Head / 16.0f) * (0.55f + 0.45f * Wave(S.m_Age * 6.7f, H + 2u));
		if(Fade < 0.01f)
			return;
		const vec2 Start = S.m_Pos + InlineOut * (S.m_Width * 0.50f);
		const float Len = Scale * (2.5f + Random(Hash(H + 3u)) * 4.5f) * (0.55f + 0.45f * S.m_Energy);
		const vec2 End = Start + InlineOut * Len + Tangent(S) * ((Random(Hash(H + 4u)) - 0.5f) * Len * 0.5f);
		if(distance(End, Head.m_Pos) < HeadRadius * 1.2f)
			return;
		std::array<vec2, MAX_BOLT_POINTS> aPoints;
		const size_t PointCount = Bolt(aPoints, Start, End, 5 + int(Hash(H + 5u) % 3u), Len * 0.30f, H + 6u);
		BoltPass(vOut, aPoints, PointCount, 2.0f * Px, 0.9f * Px, AlphaOf(P.m_Crimson, Fade * 0.65f), Px);
		BoltPass(vOut, aPoints, PointCount, 0.9f * Px, 0.35f * Px, AlphaOf(P.m_Spark, Fade * 1.0f), Px);
	});

	// A5 碎片描边与反光：复用普通批收集的顶点，按同一序位追加加法层。
	for(size_t ShardIndex = 0; ShardIndex < ShardCount; ++ShardIndex)
	{
		const SShard &Shard = aShards[ShardIndex];
		const ColorRGBA Edge = AlphaOf(Shard.m_Edge, Shard.m_Alpha * 0.85f);
		for(int Vertex = 0; Vertex < Shard.m_Count; ++Vertex)
		{
			const vec2 A = Shard.m_aVertex[Vertex];
			const vec2 B = Shard.m_aVertex[(Vertex + 1) % Shard.m_Count];
			Stroke(vOut, A, B, 1.25f * Px, 1.0f * Px, Edge, Px, true);
		}
		// 至少一条更亮的绯红棱线，避免碎片在浅色地图上退化成灰色纸屑。
		Stroke(vOut, Shard.m_aVertex[0], Shard.m_aVertex[1], 1.8f * Px, 1.3f * Px,
			AlphaOf(Shard.m_Bright, Shard.m_Alpha * 0.45f), Px, true);
		if(Shard.m_SparkAlpha > 0.008f)
		{
			const ColorRGBA Spark = AlphaOf(Shard.m_Spark, Shard.m_SparkAlpha);
			const ColorRGBA SparkEdge = AlphaOf(Shard.m_Spark, Shard.m_SparkAlpha * 0.15f);
			const vec2 A = mix(Shard.m_Center, Shard.m_aVertex[0], 0.30f);
			const vec2 B = mix(Shard.m_Center, Shard.m_aVertex[1], 0.52f);
			const vec2 D = mix(Shard.m_Center, Shard.m_aVertex[2 % Shard.m_Count], 0.38f);
			Quad(vOut, A, B, D, D, Spark, Spark, SparkEdge, SparkEdge, true);
		}
	}

	// A6 头部黑洞：暗红引力晕内的三圈吸积环 + 环上热点，尺寸随 m_Energy 增大。
	if(HeadAlpha > 0.001f && Head.m_Width > 0.05f && HeadRadius > 0.001f)
	{
		const SPalette P = Palette(Head, Preset);
		const float In = HeadRadius * (0.88f + 0.05f * HeadPulse);
		const float Spin = Head.m_Age * 2.2f + Random(Seed + 607u) * 2.0f * pi;
		Disc(vOut, Head.m_Pos, HeadRadius * 1.35f, AlphaOf(P.m_Crimson, HeadAlpha * 0.10f), AlphaOf(P.m_Crimson, 0.0f), 14, true);
		Ring(vOut, Head.m_Pos, In, In + 5.0f * Px, AlphaOf(P.m_Crimson, HeadAlpha * 0.46f * HeadPulse), AlphaOf(P.m_Crimson, HeadAlpha * 0.12f), 16, true);
		Ring(vOut, Head.m_Pos, In * 1.06f, In * 1.06f + 1.5f * Px, AlphaOf(P.m_Spark, HeadAlpha * 0.55f * HeadPulse), AlphaOf(P.m_Spark, HeadAlpha * 0.18f), 16, true);
		Ring(vOut, Head.m_Pos, In * 1.14f, In * 1.14f + 3.4f * Px, AlphaOf(P.m_Bright, HeadAlpha * 0.34f * HeadPulse), AlphaOf(P.m_Bright, HeadAlpha * 0.08f), 16, true);
		Ring(vOut, Head.m_Pos, In * 1.28f, In * 1.28f + 2.2f * Px, AlphaOf(P.m_Spark, HeadAlpha * 0.20f * HeadPulse), AlphaOf(P.m_Spark, HeadAlpha * 0.05f), 16, true);
		for(int Hot = 0; Hot < 6; ++Hot)
		{
			const float Angle = Spin + float(Hot) * pi / 3.0f;
			const vec2 Radial = direction(Angle);
			const vec2 Side = vec2(-Radial.y, Radial.x);
			const vec2 Center = Head.m_Pos + Radial * (In * 1.06f);
			const float HotAlpha = HeadAlpha * (0.40f + 0.30f * Head.m_Energy) * (0.6f + 0.4f * Wave(Head.m_Age * 8.0f + float(Hot), Seed + 613u));
			const ColorRGBA HotColor = AlphaOf(P.m_Spark, HotAlpha);
			Stroke(vOut, Center - Radial * (3.6f * Px), Center + Radial * (3.6f * Px), 1.6f * Px, 1.6f * Px, HotColor, Px, true);
			Stroke(vOut, Center - Side * (3.6f * Px), Center + Side * (3.6f * Px), 1.6f * Px, 1.6f * Px, HotColor, Px, true);
		}
	}

	// A7 爆发红边：暗斑之外再补绯红圆环与四芒星脉冲。
	for(size_t BurstIndex = 0; BurstIndex < BurstCount; ++BurstIndex)
	{
		const SBurst &Burst = aBursts[BurstIndex];
		const float Radius = Burst.m_Radius;
		Disc(vOut, Burst.m_Center, Radius * 1.55f, AlphaOf(Burst.m_Crimson, Burst.m_Alpha * 0.14f), AlphaOf(Burst.m_Crimson, 0.0f), 12, true);
		// 不规则的冲击波环：逐段抖动半径与亮度，避免出现规整的红色圆圈。
		const int Segments = 14;
		for(int k = 0; k < Segments; ++k)
		{
			const float A0 = 2.0f * pi * float(k) / float(Segments);
			const float A1 = 2.0f * pi * float(k + 1) / float(Segments);
			const unsigned HK = Hash(unsigned(k) * 41u + Seed + unsigned(BurstIndex) * 0x9e3779b9u + 0x51u);
			const float Rough = 0.86f + 0.24f * Random(HK);
			const float Alpha = Burst.m_Alpha * 0.44f * (0.35f + 0.65f * Random(HK + 7u));
			const float Inner = Radius * (1.0f + 0.10f * Rough);
			const float Outer = Inner + Radius * (0.10f + 0.10f * Rough);
			const vec2 D0 = direction(A0), D1 = direction(A1);
			Quad(vOut, Burst.m_Center + D0 * Inner, Burst.m_Center + D1 * Inner, Burst.m_Center + D1 * Outer, Burst.m_Center + D0 * Outer,
				AlphaOf(Burst.m_Crimson, Alpha), AlphaOf(Burst.m_Crimson, Alpha), AlphaOf(Burst.m_Crimson, Alpha * 0.2f), AlphaOf(Burst.m_Crimson, Alpha * 0.2f), true);
		}
		Disc(vOut, Burst.m_Center, Radius * 0.75f, AlphaOf(Burst.m_Crimson, Burst.m_Alpha * 0.18f), AlphaOf(Burst.m_Crimson, 0.0f), 10, true);
		for(int Spike = 0; Spike < 4; ++Spike)
		{
			const vec2 Dir = direction(Burst.m_Angle + float(Spike) * pi * 0.5f);
			const vec2 Mid = Burst.m_Center + Dir * (Radius * 0.55f);
			const vec2 Tip = Burst.m_Center + Dir * (Radius * 2.05f);
			Stroke(vOut, Burst.m_Center, Mid, 2.2f * Px, 1.2f * Px, AlphaOf(Burst.m_Bright, Burst.m_Alpha * 0.40f), Px, true);
			Stroke(vOut, Mid, Tip, 1.2f * Px, 0.10f * Px, AlphaOf(Burst.m_Spark, Burst.m_Alpha * 0.55f), Px, true);
		}
	}
}
