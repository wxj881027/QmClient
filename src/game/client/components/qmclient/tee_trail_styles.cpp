#include "tee_trail_styles.h"

#include "tee_trail_style_common.h"
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
	using namespace qm_tee_trail::style_detail;

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

	void ShadePixels(SPixelGrid &Grid, bool Preset, unsigned Seed, CPixelNoiseCache *pPixelNoiseCache)
	{
		for(size_t i = 0; i < Grid.m_Count; ++i)
		{
			auto &Cell = Grid.m_aCells[i];
			const SPixelNoise Noise = pPixelNoiseCache ? pPixelNoiseCache->Get(Cell.m_X, Cell.m_Y, Seed) : SamplePixelNoise(Cell.m_X, Cell.m_Y, Seed);
			const float Cluster = Noise.m_Cluster, Grain = Noise.m_Grain;
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

	void Pixels(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, unsigned Seed, std::vector<SQuad> &vOut, CPixelNoiseCache *pPixelNoiseCache)
	{
		SPixelGrid Grid;
		const size_t FirstQuad = vOut.size();
		// 世界网格仅由设置宽度确定；普通移动、缩放和老化不会改变像素尺度。
		float CellSize = std::max(2.0f, std::ceil(std::sqrt(Width)));
		for(int Attempt = 0; Attempt < 8; ++Attempt, CellSize *= 2.0f)
		{
			if(!RasterizePixels(Grid, pSamples, Count, CellSize))
				continue;
			ShadePixels(Grid, Preset, Seed, pPixelNoiseCache);
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

qm_tee_trail::SPixelNoise qm_tee_trail::SamplePixelNoise(int X, int Y, unsigned Seed)
{
	// 保留原有坐标旋转、两级簇噪声及浮点运算顺序，缓存只省去重复计算。
	const double U = X * 0.8 + Y * 0.6, V = Y * 0.8 - X * 0.6;
	return {mix(PixelClusterNoise(U / 3.0, V / 3.0, Seed), PixelClusterNoise(U / 7.0, V / 7.0, Seed + 101u), 0.35f),
		Random(PixelHash(X, Y, Seed + 43u))};
}

const qm_tee_trail::SPixelNoise &qm_tee_trail::CPixelNoiseCache::Get(int X, int Y, unsigned Seed)
{
	SEntry &Entry = m_aEntries[PixelHash(X, Y, Seed) & (m_aEntries.size() - 1)];
	if(!Entry.m_Valid || Entry.m_X != X || Entry.m_Y != Y || Entry.m_Seed != Seed)
	{
		Entry.m_Noise = SamplePixelNoise(X, Y, Seed);
		Entry.m_X = X;
		Entry.m_Y = Y;
		Entry.m_Seed = Seed;
		Entry.m_Valid = true;
	}
	return Entry.m_Noise;
}

void qm_tee_trail::BuildStyledEffect(const SStyleSample *pSamples, size_t Count, int Style, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut, CPixelNoiseCache *pPixelNoiseCache)
{
	if(Count < 2 || Count > MAX_RENDER_POINTS)
		return;
	if(Style == STYLE_MANGA)
		Manga(pSamples, Count, Preset, PixelSize, Seed, vOut);
	else if(Style == STYLE_MAGIC)
		Magic(pSamples, Count, Preset, PixelSize, Seed, vOut);
	else if(Style == STYLE_PIXEL)
		Pixels(pSamples, Count, Preset, Width, Seed, vOut, pPixelNoiseCache);
	else if(Style == STYLE_AURORA)
		style_detail::BuildAurora(pSamples, Count, Preset, Width, PixelSize, Seed, vOut);
	else if(Style == STYLE_INK)
		style_detail::BuildInk(pSamples, Count, Preset, Width, PixelSize, Seed, vOut);
	else if(Style == STYLE_IGNIS)
		style_detail::BuildIgnis(pSamples, Count, Preset, Width, PixelSize, Seed, vOut);
	else if(Style == STYLE_STARDUST)
		style_detail::BuildStardust(pSamples, Count, Preset, Width, PixelSize, Seed, vOut);
	else if(Style == STYLE_BLACKFLASH)
		style_detail::BuildBlackFlash(pSamples, Count, Preset, Width, PixelSize, Seed, vOut);
}
