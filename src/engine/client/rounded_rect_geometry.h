/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_ROUNDED_RECT_GEOMETRY_H
#define ENGINE_CLIENT_ROUNDED_RECT_GEOMETRY_H

#include <algorithm>
#include <cmath>

#include <base/math.h>

struct SRoundedRectGeometry
{
	float m_X = 0.0f;
	float m_Y = 0.0f;
	float m_W = 0.0f;
	float m_H = 0.0f;
	float m_Rounding = 0.0f;
};

inline SRoundedRectGeometry ResolveRoundedRectGeometry(const float X, const float Y, const float W, const float H, const float Rounding, const float PixelSize)
{
	SRoundedRectGeometry Result{X, Y, W, H, 0.0f};
	if(W <= 0.0f || H <= 0.0f)
		return Result;

	const float ResolvedPixelSize = std::max(PixelSize, 0.0f);
	const auto AlignToPixel = [ResolvedPixelSize](const float Value) {
		return ResolvedPixelSize > 0.0f ? std::round(Value / ResolvedPixelSize) * ResolvedPixelSize : Value;
	};
	const float Right = AlignToPixel(X + W);
	const float Bottom = AlignToPixel(Y + H);
	Result.m_X = AlignToPixel(X);
	Result.m_Y = AlignToPixel(Y);
	Result.m_W = Right - Result.m_X;
	Result.m_H = Bottom - Result.m_Y;
	if(Result.m_W <= 0.0f || Result.m_H <= 0.0f)
	{
		Result.m_X = X;
		Result.m_Y = Y;
		Result.m_W = W;
		Result.m_H = H;
	}

	const float MaxRounding = std::max(0.0f, std::min(Result.m_W, Result.m_H) * 0.5f);
	Result.m_Rounding = std::clamp(AlignToPixel(Rounding), 0.0f, MaxRounding);
	return Result;
}

// 连续曲率圆角的采样器：和着色器里同一条曲线（超椭圆 / 圆锥曲线角），
// 只供几何光栅化路径使用（模糊掩码、无 SDF 时的多边形圆角）。
// Amount 为 0 时两条曲线都退化成圆弧，采样点就是原来的 cos/sin 表。
inline float RoundedCornerCurveInset(const float Radius, const float Amount)
{
	return Radius * (1.0f + 1.3f * std::clamp(Amount, 0.0f, 1.0f));
}

inline void RoundedCornerCurveDirection(const int Curve, const float Amount, const int Index, const int Count, float &OutX, float &OutY)
{
	const float T = Count > 0 ? std::clamp((float)Index / (float)Count, 0.0f, 1.0f) : 0.0f;
	if(Curve != 0)
	{
		// 圆锥曲线角（有理二次鲍尔），权重与着色器一致。
		const float Weight = 0.70710678f + (1.2f - 0.70710678f) * std::clamp(Amount, 0.0f, 1.0f);
		const float B0 = (1.0f - T) * (1.0f - T);
		const float B1 = 2.0f * Weight * T * (1.0f - T);
		const float B2 = T * T;
		const float Den = std::max(B0 + B1 + B2, 0.0001f);
		OutX = (B0 + B1) / Den;
		OutY = (B1 + B2) / Den;
		return;
	}
	// 超椭圆（p 范数）角：指数随强度从 2 升到 4，直边处曲率为 0。
	const float Exponent = 2.0f + 2.0f * std::clamp(Amount, 0.0f, 1.0f);
	const float Power = 2.0f / Exponent;
	const float Angle = T * (float)pi * 0.5f;
	OutX = std::pow(std::cos(Angle), Power);
	OutY = std::pow(std::sin(Angle), Power);
}

#endif
