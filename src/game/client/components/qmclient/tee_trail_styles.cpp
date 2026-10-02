#include "tee_trail_styles.h"

#include "trail_band_geometry.h"
#include "trail_band_section.h"

#include <base/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

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

	constexpr size_t MAX_PIXEL_CELLS = 4096;
	constexpr size_t PIXEL_HASH_SIZE = MAX_PIXEL_CELLS * 2;
	constexpr size_t MAX_PIXEL_VISITS = MAX_RENDER_POINTS * 512;

	struct SPixelCell
	{
		int m_X, m_Y;
		ColorRGBA m_Color;
		float m_Strength, m_Radius, m_Integrity, m_Heat, m_Glow;
	};

	unsigned PixelHash(int X, int Y, unsigned Seed)
	{
		return Hash(unsigned(X) * 0x9e3779b9u ^ unsigned(Y) * 0x85ebca6bu ^ Seed);
	}

	// 固定容量的稀疏网格只扫描轨迹附近；折返的像素共享一份能量，不重复叠亮。
	struct SPixelGrid
	{
		std::array<SPixelCell, MAX_PIXEL_CELLS> m_aCells;
		std::array<uint16_t, PIXEL_HASH_SIZE> m_aSlots;
		size_t m_Count;

		void Reset()
		{
			m_aSlots.fill(0);
			m_Count = 0;
		}

		SPixelCell *FindOrAdd(int X, int Y)
		{
			size_t Slot = PixelHash(X, Y, 0) & (PIXEL_HASH_SIZE - 1);
			while(m_aSlots[Slot] != 0)
			{
				SPixelCell &Cell = m_aCells[m_aSlots[Slot] - 1];
				if(Cell.m_X == X && Cell.m_Y == Y)
					return &Cell;
				Slot = (Slot + 1) & (PIXEL_HASH_SIZE - 1);
			}
			if(m_Count == m_aCells.size())
				return nullptr;
			SPixelCell &Cell = m_aCells[m_Count++];
			Cell = {};
			Cell.m_X = X;
			Cell.m_Y = Y;
			m_aSlots[Slot] = uint16_t(m_Count);
			return &Cell;
		}
	};

	float PixelClusterNoise(double X, double Y, unsigned Seed)
	{
		const int Ix = int(std::floor(X)), Iy = int(std::floor(Y));
		const float Tx = Smooth(float(X - Ix)), Ty = Smooth(float(Y - Iy));
		return mix(mix(Random(PixelHash(Ix, Iy, Seed)), Random(PixelHash(Ix + 1, Iy, Seed)), Tx),
			mix(Random(PixelHash(Ix, Iy + 1, Seed)), Random(PixelHash(Ix + 1, Iy + 1, Seed)), Tx), Ty);
	}

	bool RasterizePixels(SPixelGrid &Grid, const SStyleSample *pSamples, size_t Count, float CellSize)
	{
		Grid.Reset();
		size_t Visits = 0;
		const float Total = std::max(pSamples[Count - 1].m_Head, 0.01f);
		const auto Position = [&](const SStyleSample &S) {
			const float Tail = Smooth((S.m_Head / Total - 0.5f) * 2.0f);
			return S.m_Pos + Tangent(S) * (CellSize * 0.7f * Tail * (1.0f - S.m_Remaining));
		};
		for(size_t i = 0; i + 1 < Count; ++i)
		{
			const auto &A = pSamples[i], &B = pSamples[i + 1];
			if(std::max(A.m_Alpha, B.m_Alpha) < 0.001f)
				continue;
			const vec2 Start = Position(A), End = Position(B);
			const vec2 Delta = End - Start;
			const float LengthSquared = dot(Delta, Delta);
			if(LengthSquared < 0.00001f)
				continue;
			const float Radius = std::max({A.m_Width, B.m_Width, CellSize * 0.6f}) * 1.12f;
			const double MinX = std::floor((double(std::min(Start.x, End.x)) - Radius) / CellSize);
			const double MaxX = std::floor((double(std::max(Start.x, End.x)) + Radius) / CellSize);
			const double MinY = std::floor((double(std::min(Start.y, End.y)) - Radius) / CellSize);
			const double MaxY = std::floor((double(std::max(Start.y, End.y)) + Radius) / CellSize);
			const double Limit = std::numeric_limits<int>::max() - 2.0;
			const double Area = (MaxX - MinX + 1.0) * (MaxY - MinY + 1.0);
			if(!std::isfinite(Area) || MinX < -Limit || MinY < -Limit || MaxX > Limit || MaxY > Limit || Area > MAX_PIXEL_VISITS - Visits)
				return false;
			Visits += size_t(Area);
			for(int Y = int(MinY); Y <= int(MaxY); ++Y)
				for(int X = int(MinX); X <= int(MaxX); ++X)
				{
					const vec2 Center((float(X) + 0.5f) * CellSize, (float(Y) + 0.5f) * CellSize);
					const float T = std::clamp(dot(Center - Start, Delta) / LengthSquared, 0.0f, 1.0f);
					const float Width = mix(A.m_Width, B.m_Width, T);
					const float Alpha = mix(A.m_Alpha, B.m_Alpha, T);
					if(Width <= 0.001f || Alpha < 0.001f)
						continue;
					const float Radial = distance(Center, Start + Delta * T) / std::max(Width, CellSize * 0.6f);
					if(Radial > 1.12f)
						continue;
					const ColorRGBA Source = Tint(A.m_Tint, B.m_Tint, T);
					const float Luminance = std::clamp(Source.r * 0.2126f + Source.g * 0.7152f + Source.b * 0.0722f, 0.0f, 1.0f);
					const float Energy = mix(A.m_Energy, B.m_Energy, T);
					const float Heat = (0.45f + 0.55f * Energy) * (0.68f + 0.32f * Luminance) * mix(A.m_Remaining, B.m_Remaining, T);
					const float Strength = Alpha * (0.4f + 0.6f * Heat) * (1.0f - Radial * 0.65f);
					SPixelCell *pCell = Grid.FindOrAdd(X, Y);
					if(!pCell)
						return false;
					if(Strength <= pCell->m_Strength)
						continue;
					const float Opacity = std::clamp(Alpha / std::max(mix(A.m_Tint.a, B.m_Tint.a, T), 0.001f), 0.0f, 1.0f);
					const float Tail = 1.0f - 0.78f * Smooth((mix(A.m_Head, B.m_Head, T) / Total - 0.52f) / 0.48f);
					pCell->m_Color = Source.WithAlpha(Alpha);
					pCell->m_Strength = Strength;
					pCell->m_Radius = Radial;
					pCell->m_Integrity = (0.58f + 0.42f * Energy) * mix(A.m_Remaining, B.m_Remaining, T) * (0.6f + 0.4f * Opacity) * Tail;
					pCell->m_Heat = Heat;
				}
		}
		return true;
	}

	void ShadePixels(SPixelGrid &Grid, bool Preset, unsigned Seed)
	{
		for(size_t i = 0; i < Grid.m_Count; ++i)
		{
			auto &Cell = Grid.m_aCells[i];
			// 旋转噪声坐标并混合两种簇尺度，实际像素仍保持世界轴对齐。
			const double U = Cell.m_X * 0.8 + Cell.m_Y * 0.6, V = Cell.m_Y * 0.8 - Cell.m_X * 0.6;
			const float Cluster = mix(PixelClusterNoise(U / 3.0, V / 3.0, Seed), PixelClusterNoise(U / 7.0, V / 7.0, Seed + 101u), 0.35f);
			const float Grain = Random(PixelHash(Cell.m_X, Cell.m_Y, Seed + 43u));
			const float Boundary = 0.94f + Cluster * 0.16f;
			const float Threshold = (0.08f + Cluster * 0.70f + Grain * 0.22f) * (0.65f + 0.35f * std::min(Cell.m_Radius, 1.0f));
			if(Cell.m_Radius > Boundary || Cell.m_Integrity <= Threshold)
			{
				Cell.m_Color.a = 0.0f;
				continue;
			}
			// 同一簇的生存阈值不随时间重播种；临近消失只改变整格透明度，不柔化边缘。
			const float Alpha = Cell.m_Color.a * std::min(1.0f, (Cell.m_Integrity - Threshold) * 20.0f);
			const float Light = Cell.m_Radius + (1.0f - Cell.m_Heat) * 0.20f + (Cluster - 0.5f) * 0.16f;
			const int Tier = Light < 0.48f ? 4 : Light < 0.72f ? 3 :
						     Light < 0.90f         ? 2 :
						     Light < 1.06f         ? 1 :
									     0;
			const ColorRGBA Base = Preset ? ColorRGBA(0xffad32u) : Cell.m_Color;
			const float Peak = std::max({Base.r, Base.g, Base.b});
			const ColorRGBA White(Peak, Peak, Peak, 1.0f);
			const ColorRGBA Palette[] = {
				ColorRGBA(Base.r * 0.44f, Base.g * 0.44f, Base.b * 0.44f, 1.0f),
				ColorRGBA(Base.r * 0.66f, Base.g * 0.66f, Base.b * 0.66f, 1.0f),
				Base, Tint(Base, White, 0.48f), Tint(Base, White, 0.92f)};
			Cell.m_Color = Palette[Tier].WithAlpha(Alpha);
			Cell.m_Glow = Tier >= 3 ? Cell.m_Heat * (Tier == 4 ? 0.32f : 0.10f) : 0.0f;
		}
		std::sort(Grid.m_aCells.begin(), Grid.m_aCells.begin() + Grid.m_Count, [](const SPixelCell &A, const SPixelCell &B) {
			return A.m_Y != B.m_Y ? A.m_Y < B.m_Y : A.m_X < B.m_X;
		});
	}

	void PixelPass(const SPixelGrid &Grid, float CellSize, bool Additive, size_t &QuadCount, std::vector<SQuad> &vOut)
	{
		for(size_t i = 0; i < Grid.m_Count;)
		{
			const auto &First = Grid.m_aCells[i++];
			const ColorRGBA Color = First.m_Color.WithMultipliedAlpha(Additive ? First.m_Glow : 1.0f);
			if(Color.a < 0.001f)
				continue;
			int EndX = First.m_X + 1;
			while(i < Grid.m_Count)
			{
				const auto &Next = Grid.m_aCells[i];
				if(Next.m_Y != First.m_Y || Next.m_X != EndX || Next.m_Color.WithMultipliedAlpha(Additive ? Next.m_Glow : 1.0f) != Color)
					break;
				++EndX;
				++i;
			}
			++QuadCount;
			const vec2 A(float(First.m_X) * CellSize, float(First.m_Y) * CellSize);
			const vec2 B(float(EndX) * CellSize, A.y + CellSize);
			Quad(vOut, A, vec2(B.x, A.y), B, vec2(A.x, B.y), Color, Color, Color, Color, Additive);
		}
	}

	void Pixels(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, unsigned Seed, std::vector<SQuad> &vOut)
	{
		SPixelGrid Grid;
		const size_t FirstQuad = vOut.size();
		// 世界网格仅由设置宽度确定；普通移动、缩放和老化不会改变像素尺度。
		float CellSize = std::max(2.0f, std::ceil(std::sqrt(Width)));
		for(int Attempt = 0; Attempt < 8; ++Attempt, CellSize *= 2.0f)
		{
			if(!RasterizePixels(Grid, pSamples, Count, CellSize))
				continue;
			ShadePixels(Grid, Preset, Seed);
			size_t QuadCount = FirstQuad;
			PixelPass(Grid, CellSize, false, QuadCount, vOut);
			PixelPass(Grid, CellSize, true, QuadCount, vOut);
			if(QuadCount <= MAX_QUADS)
				return;
			// 异常长的输入按整条轨迹降低分辨率，不能耗尽预算后直接截掉尾部。
			vOut.resize(FirstQuad);
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
