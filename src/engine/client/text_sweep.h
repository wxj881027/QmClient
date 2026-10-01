#ifndef ENGINE_CLIENT_TEXT_SWEEP_H
#define ENGINE_CLIENT_TEXT_SWEEP_H

#include <base/vmath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

struct STextSweepLineRange
{
	size_t m_Begin = 0;
	size_t m_End = 0;
};

class CTextSweepLayout
{
	std::vector<size_t> m_vLineStarts;
	int m_LastLayoutLine = -1;
	size_t m_QuadCount = 0;

public:
	void Clear()
	{
		m_vLineStarts.clear();
		m_LastLayoutLine = -1;
		m_QuadCount = 0;
	}

	void AddQuad(int LayoutLine, size_t QuadIndex)
	{
		// 使用排版行号，字体高度、字形偏移和消息前缀都不会影响分行。
		if(m_vLineStarts.empty() || LayoutLine != m_LastLayoutLine)
			m_vLineStarts.push_back(QuadIndex);
		m_LastLayoutLine = LayoutLine;
		m_QuadCount = QuadIndex + 1;
	}

	int LineCount() const { return static_cast<int>(m_vLineStarts.size()); }

	STextSweepLineRange Line(int Index) const
	{
		if(Index < 0 || Index >= LineCount())
			return {};
		const size_t End = Index + 1 < LineCount() ? m_vLineStarts[Index + 1] : m_QuadCount;
		return {m_vLineStarts[Index], End};
	}
};

struct STextSweepVertex
{
	vec2 m_Position;
	vec2 m_TexCoord;
	float m_Alpha;
};

struct STextSweepBand
{
	float m_Center;
	float m_HalfWidth;
	float m_Slant;

	float Project(vec2 Position) const { return Position.x + Position.y * m_Slant; }
};

inline float TextSweepCenter(float MinProjection, float MaxProjection, float HalfWidth, float Progress)
{
	return MinProjection - HalfWidth + (MaxProjection - MinProjection + 2.0f * HalfWidth) * Progress;
}

// 在缓存字形上裁出连续光带，插值图集坐标；不重新排版，也不改变文字尺寸。
template<typename TEmit>
void TextSweepClipQuad(const std::array<STextSweepVertex, 4> &aQuad, const STextSweepBand &Band, TEmit &&Emit)
{
	if(!std::isfinite(Band.m_Center) || !std::isfinite(Band.m_HalfWidth) || !std::isfinite(Band.m_Slant) || Band.m_HalfWidth <= 0.0f)
		return;
	float MinProjection = Band.Project(aQuad[0].m_Position);
	float MaxProjection = MinProjection;
	for(const auto &Vertex : aQuad)
	{
		const float Projection = Band.Project(Vertex.m_Position);
		MinProjection = std::min(MinProjection, Projection);
		MaxProjection = std::max(MaxProjection, Projection);
	}
	if(MaxProjection <= Band.m_Center - Band.m_HalfWidth || MinProjection >= Band.m_Center + Band.m_HalfWidth)
		return;

	const auto Clip = [&](const auto &aInput, int Count, auto &aOutput, float Edge, bool KeepGreater) {
		int OutputCount = 0;
		if(Count == 0)
			return OutputCount;
		STextSweepVertex Previous = aInput[Count - 1];
		float PreviousDistance = Band.Project(Previous.m_Position) - Edge;
		bool PreviousInside = KeepGreater ? PreviousDistance >= 0.0f : PreviousDistance <= 0.0f;
		for(int i = 0; i < Count; ++i)
		{
			const STextSweepVertex &Current = aInput[i];
			const float Distance = Band.Project(Current.m_Position) - Edge;
			const bool Inside = KeepGreater ? Distance >= 0.0f : Distance <= 0.0f;
			if(Inside != PreviousInside)
			{
				const float Amount = PreviousDistance / (PreviousDistance - Distance);
				aOutput[OutputCount++] = {
					Previous.m_Position + (Current.m_Position - Previous.m_Position) * Amount,
					Previous.m_TexCoord + (Current.m_TexCoord - Previous.m_TexCoord) * Amount,
					Previous.m_Alpha + (Current.m_Alpha - Previous.m_Alpha) * Amount};
			}
			if(Inside)
				aOutput[OutputCount++] = Current;
			Previous = Current;
			PreviousDistance = Distance;
			PreviousInside = Inside;
		}
		return OutputCount;
	};

	static constexpr std::array<float, 6> s_aStops = {-1.0f, -0.65f, -0.15f, 0.15f, 0.65f, 1.0f};
	static constexpr std::array<float, 6> s_aOpacities = {0.0f, 0.25f, 1.0f, 1.0f, 0.25f, 0.0f};
	for(size_t Segment = 0; Segment + 1 < s_aStops.size(); ++Segment)
	{
		const float Left = Band.m_Center + s_aStops[Segment] * Band.m_HalfWidth;
		const float Right = Band.m_Center + s_aStops[Segment + 1] * Band.m_HalfWidth;
		if(MaxProjection <= Left || MinProjection >= Right)
			continue;
		// 矩形经过两个平行半平面裁剪后最多有六个顶点。
		std::array<STextSweepVertex, 8> aPolygon;
		std::array<STextSweepVertex, 8> aClipped;
		std::copy(aQuad.begin(), aQuad.end(), aPolygon.begin());
		int Count = Clip(aPolygon, 4, aClipped, Left, true);
		Count = Clip(aClipped, Count, aPolygon, Right, false);
		for(int i = 0; i < Count; ++i)
		{
			const float Amount = std::clamp((Band.Project(aPolygon[i].m_Position) - Left) / (Right - Left), 0.0f, 1.0f);
			aPolygon[i].m_Alpha *= s_aOpacities[Segment] + (s_aOpacities[Segment + 1] - s_aOpacities[Segment]) * Amount;
		}
		for(int i = 1; i + 1 < Count; ++i)
		{
			// 退化的第四个顶点让现有文字四边形管线绘制三角形扇。
			Emit(std::array<STextSweepVertex, 4>{aPolygon[0], aPolygon[i], aPolygon[i + 1], aPolygon[i + 1]});
		}
	}
}

#endif
