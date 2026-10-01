#include "tee_trail_styles.h"

#include "trail_band_geometry.h"
#include "trail_band_section.h"

#include <base/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace
{
	using namespace qm_tee_trail;

	struct SBandPoint
	{
		vec2 m_Pos;
		float m_Left, m_Right;
		ColorRGBA m_Color;
	};

	float Smooth(float X)
	{
		X = std::clamp(X, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}

	unsigned Hash(unsigned X)
	{
		X ^= X >> 16;
		X *= 0x7feb352du;
		X ^= X >> 15;
		X *= 0x846ca68bu;
		return X ^ (X >> 16);
	}

	float Random(unsigned X)
	{
		return (Hash(X) & 0xffffffu) / float(0xffffffu);
	}

	unsigned CellId(double Distance)
	{
		return static_cast<unsigned>(static_cast<int64_t>(std::floor(Distance)));
	}

	float Noise(double Distance, unsigned Seed)
	{
		const double Cell = std::floor(Distance);
		const unsigned Id = CellId(Cell);
		return mix(Random(Id + Seed), Random(Id + 1u + Seed), float(Distance - Cell)) * 2.0f - 1.0f;
	}

	vec2 Unit(vec2 V, vec2 Fallback)
	{
		const float Length = length(V);
		return Length > 0.0001f ? V / Length : Fallback;
	}

	vec2 Tangent(const SStyleSample &S)
	{
		return vec2(S.m_Normal.y, -S.m_Normal.x);
	}

	ColorRGBA Tint(ColorRGBA A, ColorRGBA B, float T)
	{
		return ColorRGBA(mix(A.r, B.r, T), mix(A.g, B.g, T), mix(A.b, B.b, T), 1.0f);
	}

	void Quad(std::vector<SQuad> &vOut, vec2 A, vec2 B, vec2 C, vec2 D, ColorRGBA Ca, ColorRGBA Cb, ColorRGBA Cc, ColorRGBA Cd, bool Additive)
	{
		if(vOut.size() < MAX_QUADS && std::max({Ca.a, Cb.a, Cc.a, Cd.a}) >= 0.001f)
			vOut.push_back({{A, B, C, D}, {Ca, Cb, Cc, Cd}, Additive});
	}

	void BandSegment(std::vector<SQuad> &vOut, const SQmTrailBandSection &A, const SQmTrailBandSection &B, bool Additive)
	{
		for(int Strip = 0; Strip < 3; ++Strip)
			Quad(vOut, A.m_aPos[Strip], B.m_aPos[Strip], B.m_aPos[Strip + 1], A.m_aPos[Strip + 1],
				A.m_aColor[Strip], B.m_aColor[Strip], B.m_aColor[Strip + 1], A.m_aColor[Strip + 1], Additive);
	}

	void Band(std::vector<SQuad> &vOut, SBandPoint *pBand, size_t Count, float Softness, float PixelSize, bool Additive)
	{
		std::array<vec2, MAX_RENDER_POINTS> aNormals;
		QmPrepareTrailBandJoins(pBand, Count, aNormals.data());
		const auto Section = [&](size_t Index) {
			const SBandPoint &P = pBand[Index];
			return QmPrepareTrailBandSection(P.m_Pos, P.m_Left, P.m_Right, P.m_Color, aNormals[Index], Softness, PixelSize);
		};
		SQmTrailBandSection Previous = Section(0);
		for(size_t i = 1; i < Count; ++i)
		{
			const SQmTrailBandSection Current = Section(i);
			BandSegment(vOut, Previous, Current, Additive);
			Previous = Current;
		}
	}

	void Stroke(std::vector<SQuad> &vOut, vec2 A, vec2 B, float StartWidth, float EndWidth, ColorRGBA Color, float PixelSize, bool Additive)
	{
		const vec2 Delta = B - A;
		if(length(Delta) < 0.001f)
			return;
		const vec2 Normal = Unit(vec2(-Delta.y, Delta.x), vec2(0, 1));
		const auto Start = QmPrepareTrailBandSection(A, StartWidth, StartWidth, Color, Normal, 0.08f, PixelSize);
		const auto End = QmPrepareTrailBandSection(B, EndWidth, EndWidth, Color, Normal, 0.08f, PixelSize);
		BandSegment(vOut, Start, End, Additive);
	}

	SStyleSample Interpolate(const SStyleSample &A, const SStyleSample &B, float T)
	{
		SStyleSample S;
		S.m_Pos = mix(A.m_Pos, B.m_Pos, T);
		S.m_Normal = Unit(mix(A.m_Normal, B.m_Normal, T), A.m_Normal);
		S.m_Tint = Tint(A.m_Tint, B.m_Tint, T).WithAlpha(mix(A.m_Tint.a, B.m_Tint.a, T));
		S.m_Distance = mix(A.m_Distance, B.m_Distance, double(T));
		S.m_Age = mix(A.m_Age, B.m_Age, T);
		S.m_Width = mix(A.m_Width, B.m_Width, T);
		S.m_Alpha = mix(A.m_Alpha, B.m_Alpha, T);
		S.m_Energy = mix(A.m_Energy, B.m_Energy, T);
		S.m_Head = mix(A.m_Head, B.m_Head, T);
		S.m_Remaining = mix(A.m_Remaining, B.m_Remaining, T);
		return S;
	}

	// 装饰锚点由累计距离决定，头尾推进及屏幕细分不会重新播种整条拖尾。
	template<typename F>
	void Anchors(const SStyleSample *pSamples, size_t Count, float Spacing, float Offset, size_t Limit, F Emit)
	{
		const double First = std::floor((pSamples[0].m_Distance - Offset) / Spacing) * Spacing + Offset;
		size_t Segment = 0;
		for(size_t Index = 0; Index < Limit; ++Index)
		{
			const double Distance = First - double(Index) * Spacing;
			if(Distance < pSamples[Count - 1].m_Distance)
				break;
			while(Segment + 1 < Count && pSamples[Segment + 1].m_Distance > Distance)
				++Segment;
			if(Segment + 1 == Count)
				break;
			const SStyleSample &A = pSamples[Segment], &B = pSamples[Segment + 1];
			const double Span = A.m_Distance - B.m_Distance;
			if(Span < 0.00001)
				continue;
			SStyleSample S = Interpolate(A, B, std::clamp(float((A.m_Distance - Distance) / Span), 0.0f, 1.0f));
			S.m_Distance = Distance;
			if(S.m_Alpha >= 0.001f && S.m_Width > 0.001f)
				Emit(S, CellId((Distance - Offset) / Spacing));
		}
	}

	float BrushEnvelope(double Distance, unsigned Seed)
	{
		const double Phase = Distance / 54.0 + Random(Seed);
		const float T = float(Phase - std::floor(Phase));
		// 真正收笔到零宽，留下可透出地图的空隙，而不是用黑色覆盖模拟干刷。
		return Smooth(T / 0.12f) * (1.0f - Smooth((T - 0.66f) / 0.14f));
	}

	void Manga(const SStyleSample *pSamples, size_t Count, bool Preset, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut)
	{
		std::array<SBandPoint, MAX_RENDER_POINTS> aBand;
		for(int Layer = 0; Layer < 2; ++Layer)
		{
			for(size_t i = 0; i < Count; ++i)
			{
				const auto &S = pSamples[i];
				const float Envelope = BrushEnvelope(S.m_Distance, Seed);
				const float Pressure = 0.46f + 0.48f * std::abs(Noise(S.m_Distance / 18.0, Seed + 11u));
				const float Width = S.m_Width * 0.72f * Envelope * Pressure;
				const float Left = Width * (1.0f + 0.32f * Noise(S.m_Distance / 7.0, Seed + 29u));
				const float Right = Width * (0.85f + 0.38f * Noise(S.m_Distance / 9.0, Seed + 47u));
				const vec2 Center = S.m_Pos + S.m_Normal * (S.m_Width * 0.12f * Noise(S.m_Distance / 15.0, Seed));
				const ColorRGBA Base = Preset ? ColorRGBA(Layer == 0 ? 0xf4efe6u : 0x1c1c1cu) : Tint(S.m_Tint, ColorRGBA(Layer == 0 ? 0xffffffu : 0x000000u), Layer == 0 ? 0.6f : 0.90f);
				const float Edge = Layer == 0 ? 1.08f : 1.0f;
				const float EdgeBreak = Layer == 0 ? Smooth((Noise(S.m_Distance / 31.0, Seed + 61u) + 0.2f) * 2.0f) : 1.0f;
				const float Alpha = S.m_Alpha * Smooth(S.m_Head / 8.0f) * Envelope * EdgeBreak * (Layer == 0 ? 0.3f : 0.95f);
				aBand[i] = {Center, Left * Edge, Right * Edge, Base.WithAlpha(Alpha)};
			}
			Band(vOut, aBand.data(), Count, 0.035f, PixelSize, false);
		}

		Anchors(pSamples, Count, 24.0f, Random(Seed + 83u) * 24.0f, 32, [&](const SStyleSample &S, unsigned Id) {
			const float Sign = (Hash(Id + Seed) & 1u) ? 1.0f : -1.0f;
			const vec2 Root = S.m_Pos + S.m_Normal * (Sign * S.m_Width * 0.28f);
			const vec2 Tip = Root + Tangent(S) * (S.m_Width * 0.9f) + S.m_Normal * (Sign * S.m_Width * 0.48f);
			const ColorRGBA Ink = Preset ? ColorRGBA(0x1c1c1cu) : Tint(S.m_Tint, ColorRGBA(0x000000u), 0.9f);
			Stroke(vOut, Root, Tip, S.m_Width * 0.085f, 0.0f, Ink.WithAlpha(S.m_Alpha * 0.6f * BrushEnvelope(S.m_Distance, Seed)), PixelSize, false);
		});

		// 发光只落在窄裂纹中，全部排在普通墨迹之后，避免逐笔切换混合状态。
		for(size_t i = 0; i < Count; ++i)
		{
			const auto &S = pSamples[i];
			const float Envelope = BrushEnvelope(S.m_Distance, Seed);
			const float Fracture = Smooth((Noise(S.m_Distance / 23.0, Seed + 97u) + 0.1f) * 3.0f);
			const float Width = S.m_Width * 0.045f * Envelope;
			const vec2 Center = S.m_Pos + S.m_Normal * (S.m_Width * 0.34f * Noise(S.m_Distance / 8.0, Seed + 53u));
			const ColorRGBA Color = Preset ? ColorRGBA(0xbf1a26u) : S.m_Tint;
			aBand[i] = {Center, Width, Width, Color.WithAlpha(S.m_Alpha * Envelope * Fracture * Smooth(S.m_Head / 12.0f) * 0.82f)};
		}
		Band(vOut, aBand.data(), Count, 0.05f, PixelSize, true);
		int Cracks = 0;
		Anchors(pSamples, Count, 54.0f, (0.35f - Random(Seed)) * 54.0f, 24, [&](const SStyleSample &S, unsigned Id) {
			if(Cracks == 3 || S.m_Head < 14.0f)
				return;
			++Cracks;
			const float Sign = (Hash(Id + Seed + 7u) & 1u) ? 1.0f : -1.0f;
			const vec2 Root = S.m_Pos;
			const vec2 Elbow = Root + Tangent(S) * (S.m_Width * 0.18f) + S.m_Normal * (Sign * S.m_Width * 0.42f);
			const vec2 Tip = Root + Tangent(S) * (S.m_Width * 0.72f) + S.m_Normal * (Sign * S.m_Width * 0.28f);
			const ColorRGBA Color = (Preset ? ColorRGBA(0xbf1a26u) : S.m_Tint).WithAlpha(S.m_Alpha * 0.65f);
			Stroke(vOut, Root, Elbow, S.m_Width * 0.04f, S.m_Width * 0.025f, Color, PixelSize, true);
			Stroke(vOut, Elbow, Tip, S.m_Width * 0.025f, 0.0f, Color, PixelSize, true);
		});
	}

	void Magic(const SStyleSample *pSamples, size_t Count, bool Preset, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut)
	{
		std::array<SBandPoint, MAX_RENDER_POINTS> aBand;
		for(int Layer = 0; Layer < 3; ++Layer)
		{
			for(size_t i = 0; i < Count; ++i)
			{
				const auto &S = pSamples[i];
				const double Phase = std::fmod(S.m_Distance, 110.0) / 110.0 * 2.0 * pi + Random(Seed) * 2.0 * pi + S.m_Age * 0.025;
				const float Root = Smooth(S.m_Head / 14.0f) * Smooth((pSamples[Count - 1].m_Head - S.m_Head) / 18.0f);
				const float Wave = float(std::sin(Phase + (Layer == 1 ? 1.45 : 0.0)));
				const float Offset = Layer == 1 ? 0.32f + Wave * 0.25f : Wave * 0.16f;
				const float Pressure = 0.72f + 0.28f * float(std::sin(Phase - 0.6));
				const float Width = S.m_Width * (Layer == 0 ? 0.23f : Layer == 1 ? 0.072f :
												   0.038f) *
						    Pressure;
				const ColorRGBA Color = Preset ? ColorRGBA(Layer == 0 ? 0x00c0b0u : Layer == 1 ? 0xe09820u :
														 0xfffadeu) :
								 Tint(S.m_Tint, ColorRGBA(0xffffffu), Layer == 0 ? 0.0f : Layer == 1 ? 0.35f :
																       0.78f);
				aBand[i] = {S.m_Pos + S.m_Normal * (S.m_Width * Offset * Root), Width, Width, Color.WithAlpha(S.m_Alpha * Root * (Layer == 0 ? 0.78f : Layer == 1 ? 0.62f :
																						    0.6f))};
			}
			Band(vOut, aBand.data(), Count, Layer == 2 ? 0.3f : 0.14f, PixelSize, Layer == 2);
		}

		int Marks = 0;
		Anchors(pSamples, Count, 66.0f, Random(Seed + 137u) * 66.0f, 24, [&](const SStyleSample &S, unsigned Id) {
			if(Marks == 5 || S.m_Head < 20.0f)
				return;
			++Marks;
			const float Sign = (Hash(Id + Seed) & 1u) ? 1.0f : -1.0f;
			const vec2 Center = S.m_Pos + S.m_Normal * (Sign * S.m_Width * 0.38f);
			const float Radius = S.m_Width * 0.30f;
			const float Gain = Smooth(S.m_Age / 3.0f) * S.m_Remaining;
			const ColorRGBA Color = (Preset ? ColorRGBA(0xfffadeu) : Tint(S.m_Tint, ColorRGBA(0xffffffu), 0.65f)).WithAlpha(S.m_Alpha * Gain * 0.72f);
			if((Hash(Id + Seed + 19u) & 1u) != 0)
			{
				const float Angle = Random(Id + Seed) * pi;
				for(int Step = 0; Step < 8; ++Step)
				{
					if(Step == 3 || Step == 7)
						continue;
					const vec2 A = Center + direction(Angle + Step * pi / 4) * Radius;
					const vec2 B = Center + direction(Angle + (Step + 1) * pi / 4) * Radius;
					Stroke(vOut, A, B, S.m_Width * 0.025f, S.m_Width * 0.025f, Color.WithMultipliedAlpha(0.65f), PixelSize, true);
				}
			}
			const vec2 Axis = direction((Random(Id + Seed + 31u) - 0.5f) * 0.3f);
			const vec2 Along = Axis * Radius;
			const vec2 Across = vec2(-Axis.y, Axis.x) * (Radius * 0.72f);
			Stroke(vOut, Center - Along, Center, 0.0f, S.m_Width * 0.06f, Color, PixelSize, true);
			Stroke(vOut, Center, Center + Along, S.m_Width * 0.06f, 0.0f, Color, PixelSize, true);
			Stroke(vOut, Center - Across, Center, 0.0f, S.m_Width * 0.04f, Color, PixelSize, true);
			Stroke(vOut, Center, Center + Across, S.m_Width * 0.04f, 0.0f, Color, PixelSize, true);
		});
	}

	void PixelBlock(std::vector<SQuad> &vOut, vec2 Position, float Size, float Cell, ColorRGBA Color)
	{
		const vec2 Corner(std::floor((Position.x - Size * 0.5f) / Cell) * Cell, std::floor((Position.y - Size * 0.5f) / Cell) * Cell);
		Quad(vOut, Corner, Corner + vec2(Size, 0), Corner + vec2(Size, Size), Corner + vec2(0, Size), Color, Color, Color, Color, false);
	}

	void Pixels(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, unsigned Seed, std::vector<SQuad> &vOut)
	{
		const float Cell = std::clamp(std::round(Width * 0.16f), 1.0f, 3.0f);
		const float Spacing = std::max(8.0f, Cell * 4.0f);
		const float Offset = Random(Seed) * Spacing;
		const ColorRGBA aColors[] = {ColorRGBA(0xf0cc10u), ColorRGBA(0x2090e0u), ColorRGBA(0xee3040u)};
		const float aAlpha[] = {0.30f, 0.65f, 0.95f};
		for(int Layer = 0; Layer < 3; ++Layer)
		{
			Anchors(pSamples, Count, Spacing, Offset, MAX_POINTS, [&](const SStyleSample &S, unsigned Id) {
				if(Layer < 2 && S.m_Head < 16.0f)
					return;
				const float Root = Smooth(S.m_Head / 12.0f);
				const float Age = std::floor(S.m_Age / 3.0f) * 3.0f;
				const float Drift = Age / (Age + 22.0f);
				const float Depth = float(1 - Layer);
				const float Bias = 0.8f + 0.2f * Random(Id + Seed + 17u);
				const vec2 Displacement = (S.m_Normal * (Depth * 0.24f) + Tangent(S) * ((Layer == 0 ? 0.30f : Layer == 1 ? 0.13f :
																	   -0.10f) *
													       Drift)) *
							  (S.m_Width * Root * Bias);
				const vec2 Center = S.m_Pos + Displacement;
				const float WidthRatio = S.m_Width / std::max(Width, 0.01f);
				const int Cells = std::clamp(int(std::round(WidthRatio * (2.0f + Layer))), 1, 4);
				const float Size = Cell * Cells;
				const float DepthFade = Layer == 0 ? S.m_Remaining * S.m_Remaining : Layer == 1 ? S.m_Remaining :
														  1.0f;
				const ColorRGBA Base = Preset ? aColors[Layer] : Tint(S.m_Tint, ColorRGBA(Layer == 0 ? 0x000000u : 0xffffffu), Layer == 0 ? 0.35f : Layer == 1 ? 0.0f :
																						 0.22f);
				const ColorRGBA Color = Base.WithAlpha(S.m_Alpha * aAlpha[Layer] * Root * DepthFade);
				PixelBlock(vOut, Center, Size, Cell, Color);
				// 相邻小方块组成完整阶梯簇；位置由同一锚点导出，不抛洒独立粒子。
				const vec2 Step = Tangent(S);
				const vec2 Shoulder = std::abs(Step.x) >= std::abs(Step.y) ? vec2(Step.x < 0 ? -Size : Size, 0) : vec2(0, Step.y < 0 ? -Size : Size);
				PixelBlock(vOut, Center + Shoulder * 0.65f, Cell * std::max(1, Cells - 1), Cell, Color.WithMultipliedAlpha(0.72f));
				if(Layer == 2 && Cells >= 2)
				{
					const vec2 Corner(std::floor((Center.x - Size * 0.5f) / Cell) * Cell, std::floor((Center.y - Size * 0.5f) / Cell) * Cell);
					const ColorRGBA Highlight = Tint(Base, ColorRGBA(0xfffadeu), 0.48f).WithAlpha(Color.a * 0.55f);
					PixelBlock(vOut, Corner + vec2(Cell * 0.5f, Cell * 0.5f), Cell, Cell, Highlight);
				}
			});
		}
	}
}

void qm_tee_trail::BuildStyledEffect(const SStyleSample *pSamples, size_t Count, int Style, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut)
{
	if(Count < 2 || Count > MAX_RENDER_POINTS)
		return;
	if(Style == STYLE_MANGA)
		Manga(pSamples, Count, Preset, PixelSize, Seed, vOut);
	else if(Style == STYLE_MAGIC)
		Magic(pSamples, Count, Preset, PixelSize, Seed, vOut);
	else if(Style == STYLE_PIXEL)
		Pixels(pSamples, Count, Preset, Width, Seed, vOut);
}
