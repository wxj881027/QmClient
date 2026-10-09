#ifndef GAME_CLIENT_QMUI_QMCOLORGRADIENT_H
#define GAME_CLIENT_QMUI_QMCOLORGRADIENT_H

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>
#include <base/vmath.h>

#include <game/client/components/message_gradient.h>

#include <algorithm>
#include <array>
#include <cmath>

enum class EQmGradientType
{
	LINEAR,
	RADIAL,
	ANGULAR,
	REFLECTED,
	DIAMOND,
};

// 候选栏与预览使用相同的归一化坐标；颜色格式沿用发言部分的七色调色板。
struct SQmColorGradient
{
	std::array<ColorRGBA, CMessageGradient::MAX_COLORS> m_aColors{};
	int m_NumColors = 1;
	EQmGradientType m_Type = EQmGradientType::LINEAR;
	vec2 m_Center = vec2(0.5f, 0.5f);
	vec2 m_Direction = vec2(1.0f, 0.0f);
	float m_Range = 1.0f;
	bool m_Reverse = false;

	static SQmColorGradient FromConfig(const char *pColors, ColorRGBA Fallback, int Type, int Angle, int CenterX, int CenterY, int Range, bool Reverse)
	{
		SQmColorGradient Result;
		Result.m_aColors[0] = Fallback;
		unsigned aPackedColors[CMessageGradient::MAX_COLORS];
		const int Count = CMessageGradient::Unpack(pColors, aPackedColors, CMessageGradient::MAX_COLORS);
		if(Count > 0)
		{
			Result.m_NumColors = Count;
			for(int i = 0; i < Count; ++i)
			{
				Result.m_aColors[i] = color_cast<ColorRGBA>(ColorHSLA(aPackedColors[i]));
				Result.m_aColors[i].a = Fallback.a;
			}
		}
		Result.m_Type = static_cast<EQmGradientType>(std::clamp(Type, 0, 4));
		const float Radians = std::clamp(Angle, 0, 360) * pi / 180.0f;
		Result.m_Direction = vec2(std::cos(Radians), std::sin(Radians));
		if(std::abs(Result.m_Direction.x) < 0.000001f)
			Result.m_Direction.x = 0.0f;
		if(std::abs(Result.m_Direction.y) < 0.000001f)
			Result.m_Direction.y = 0.0f;
		Result.m_Center = vec2(std::clamp(CenterX, 0, 100), std::clamp(CenterY, 0, 100)) / 100.0f;
		Result.m_Range = std::clamp(Range, 10, 200) / 100.0f;
		Result.m_Reverse = Reverse;
		return Result;
	}

	float Position(vec2 Point) const
	{
		const vec2 Offset = Point - m_Center;
		const float Along = dot(Offset, m_Direction);
		const float Across = dot(Offset, vec2(-m_Direction.y, m_Direction.x));
		const float Extent = std::abs(m_Direction.x) + std::abs(m_Direction.y);
		float Amount = 0.0f;
		switch(m_Type)
		{
		case EQmGradientType::LINEAR: Amount = 0.5f + Along / (m_Range * Extent); break;
		case EQmGradientType::RADIAL: Amount = length(Offset) * 2.0f / m_Range; break;
		case EQmGradientType::ANGULAR:
			if(length(Offset) > 0.00001f)
			{
				Amount = std::atan2(Across, Along) / (2.0f * pi);
				Amount -= std::floor(Amount);
				Amount /= m_Range;
			}
			break;
		case EQmGradientType::REFLECTED: Amount = std::abs(Along) * 2.0f / (m_Range * Extent); break;
		case EQmGradientType::DIAMOND: Amount = (std::abs(Along) + std::abs(Across)) * 2.0f / m_Range; break;
		}
		Amount = std::clamp(Amount, 0.0f, 1.0f);
		return m_Reverse ? 1.0f - Amount : Amount;
	}

	ColorRGBA Sample(vec2 Point) const
	{
		const int Count = std::clamp(m_NumColors, 1, CMessageGradient::MAX_COLORS);
		if(Count == 1)
			return m_aColors[0];
		const float Scaled = Position(Point) * (Count - 1);
		const int Index = std::min(static_cast<int>(Scaled), Count - 2);
		const float T = Scaled - Index;
		const ColorRGBA &From = m_aColors[Index];
		const ColorRGBA &To = m_aColors[Index + 1];
		return ColorRGBA(From.r + (To.r - From.r) * T, From.g + (To.g - From.g) * T,
			From.b + (To.b - From.b) * T, From.a + (To.a - From.a) * T);
	}
};

// 长消息的颜色变化跨越多个字形，按方向和覆盖范围减少不必要的细分；单字仍保留七色网格。
inline std::array<int, 2> QmGradientTextGrid(const SQmColorGradient &Gradient, vec2 Size, float FontSize)
{
	if(Gradient.m_Type == EQmGradientType::ANGULAR)
		return {8, 8};
	const bool Linear = Gradient.m_Type == EQmGradientType::LINEAR || Gradient.m_Type == EQmGradientType::REFLECTED;
	const float Extent = std::abs(Gradient.m_Direction.x) + std::abs(Gradient.m_Direction.y);
	const float Frequency = 2.0f * std::max(1, Gradient.m_NumColors - 1) / Gradient.m_Range;
	const float Factor = Gradient.m_Type == EQmGradientType::LINEAR ? 1.0f : 2.0f;
	const auto Count = [&](float Dimension, float Direction) {
		const float Weight = Linear ? std::abs(Direction) / Extent : 1.0f;
		const float Cells = Frequency * Factor * std::max(0.0f, FontSize) * Weight / std::max(0.0001f, Dimension);
		return static_cast<int>(std::clamp(std::ceil(Cells), Linear ? 1.0f : 2.0f, 8.0f));
	};
	return {Count(Size.x, Gradient.m_Direction.x), Count(Size.y, Gradient.m_Direction.y)};
}

struct SQmGradientTextPaint
{
	const SQmColorGradient *m_pGradient;
	vec2 m_Origin;
	vec2 m_Size;
	float m_Alpha;

	static ColorRGBA Sample(vec2 Position, const void *pContext)
	{
		const auto &Paint = *static_cast<const SQmGradientTextPaint *>(pContext);
		const vec2 Size(std::max(Paint.m_Size.x, 0.0001f), std::max(Paint.m_Size.y, 0.0001f));
		ColorRGBA Color = Paint.m_pGradient->Sample((Position - Paint.m_Origin) / Size);
		Color.a *= Paint.m_Alpha;
		return Color;
	}
};

#endif
