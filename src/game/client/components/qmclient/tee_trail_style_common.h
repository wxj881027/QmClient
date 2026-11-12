#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TEE_TRAIL_STYLE_COMMON_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TEE_TRAIL_STYLE_COMMON_H

#include <base/math.h>

#include <game/client/components/qmclient/tee_trail_styles.h>
#include <game/client/components/qmclient/trail_band_geometry.h>
#include <game/client/components/qmclient/trail_band_section.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// 艺术样式的共享几何契约。样式实现按翻译单元分开，但仍遵守同一套输出规则：
// 1. 只向 vOut 追加四边形，普通混合层必须全部排在加法混合层之前；
// 2. 顶点透明度乘入采样透明度，源透明度为零时不得产生任何四边形；
// 3. Preset 只替换颜色，形状、数量与透明度必须与自定义颜色完全一致；
// 4. 随机量只来自 Seed 与样本字段，不读取时钟、全局状态或静态计数器；
// 5. 形状由样本的 m_Pos、m_Normal、m_Width、m_Alpha、m_Distance、m_Age 推导，
//    屏幕像素尺寸只用于细分与柔边，不影响世界坐标中的整体形态。
namespace qm_tee_trail
{
	namespace style_detail
	{
		using SSample = SStyleSample;

		struct SBandPoint
		{
			vec2 m_Pos;
			float m_Left, m_Right;
			ColorRGBA m_Color;
		};

		inline float Smooth(float X)
		{
			X = std::clamp(X, 0.0f, 1.0f);
			return X * X * (3.0f - 2.0f * X);
		}

		inline unsigned Hash(unsigned X)
		{
			X ^= X >> 16;
			X *= 0x7feb352du;
			X ^= X >> 15;
			X *= 0x846ca68bu;
			return X ^ (X >> 16);
		}

		inline float Random(unsigned X)
		{
			return (Hash(X) & 0xffffffu) / float(0xffffffu);
		}

		// 一维值噪声，范围 -1..1；距离均匀推进时输出稳定，不随时间重播种。
		inline float Noise(double Distance, unsigned Seed)
		{
			const double Cell = std::floor(Distance);
			const auto Id = StyleCellId(Cell);
			if(!Id)
				return 0.0f;
			return mix(Random(*Id + Seed), Random(*Id + 1u + Seed), float(Distance - Cell)) * 2.0f - 1.0f;
		}

		inline vec2 Unit(vec2 V, vec2 Fallback = vec2(1.0f, 0.0f))
		{
			const float Length = length(V);
			return Length > 0.0001f ? V / Length : Fallback;
		}

		inline vec2 Tangent(const SStyleSample &S)
		{
			return vec2(S.m_Normal.y, -S.m_Normal.x);
		}

		// 只混合颜色，透明度由调用方通过 WithAlpha / WithMultipliedAlpha 指定。
		inline ColorRGBA Tint(ColorRGBA A, ColorRGBA B, float T)
		{
			return ColorRGBA(mix(A.r, B.r, T), mix(A.g, B.g, T), mix(A.b, B.b, T), 1.0f);
		}

		inline void Quad(std::vector<SQuad> &vOut, vec2 A, vec2 B, vec2 C, vec2 D, ColorRGBA Ca, ColorRGBA Cb, ColorRGBA Cc, ColorRGBA Cd, bool Additive)
		{
			if(vOut.size() < MAX_QUADS && std::max({Ca.a, Cb.a, Cc.a, Cd.a}) >= 0.001f)
				vOut.push_back({{A, B, C, D}, {Ca, Cb, Cc, Cd}, Additive});
		}

		inline void BandSegment(std::vector<SQuad> &vOut, const SQmTrailBandSection &A, const SQmTrailBandSection &B, bool Additive)
		{
			for(int Strip = 0; Strip < 3; ++Strip)
				Quad(vOut, A.m_aPos[Strip], B.m_aPos[Strip], B.m_aPos[Strip + 1], A.m_aPos[Strip + 1],
					A.m_aColor[Strip], B.m_aColor[Strip], B.m_aColor[Strip + 1], A.m_aColor[Strip + 1], Additive);
		}

		// 带宽截面三次细分：内部两边共用接缝，外侧两边做柔边。
		inline void Band(std::vector<SQuad> &vOut, SBandPoint *pBand, size_t Count, float Softness, float PixelSize, bool Additive)
		{
			if(Count < 2)
				return;
			std::array<vec2, MAX_RENDER_POINTS> aNormals;
			QmPrepareTrailBandHead(pBand, Count);
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

		inline void Stroke(std::vector<SQuad> &vOut, vec2 A, vec2 B, float StartWidth, float EndWidth, ColorRGBA Color, float PixelSize, bool Additive)
		{
			const vec2 Delta = B - A;
			if(length(Delta) < 0.001f)
				return;
			const vec2 Normal = Unit(vec2(-Delta.y, Delta.x), vec2(0, 1));
			const auto Start = QmPrepareTrailBandSection(A, StartWidth, StartWidth, Color, Normal, 0.08f, PixelSize);
			const auto End = QmPrepareTrailBandSection(B, EndWidth, EndWidth, Color, Normal, 0.08f, PixelSize);
			BandSegment(vOut, Start, End, Additive);
		}

		inline SStyleSample Interpolate(const SStyleSample &A, const SStyleSample &B, float T)
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
			if(!std::isfinite(First))
				return;
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
				{
					const auto Id = StyleCellId((Distance - Offset) / Spacing);
					if(Id)
						Emit(S, *Id);
				}
			}
		}

		inline float BrushEnvelope(double Distance, unsigned Seed)
		{
			const double Phase = Distance / 54.0 + Random(Seed);
			const float T = float(Phase - std::floor(Phase));
			// 真正收笔到零宽，留下可透出地图的空隙，而不是用黑色覆盖模拟干刷。
			return Smooth(T / 0.12f) * (1.0f - Smooth((T - 0.66f) / 0.14f));
		}

		// 六款程序化艺术样式各自一个翻译单元，形状由样本驱动，不含全局状态。
		void BuildAurora(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut);
		void BuildInk(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut);
		void BuildIgnis(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut);
		void BuildStardust(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut);
		void BuildBlackFlash(const SStyleSample *pSamples, size_t Count, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut);
	}
}

#endif
