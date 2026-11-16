#include "watermark_compositor.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float PI = 3.14159265358979323846f;
	struct SGeometry
	{
		float m_Width, m_Height, m_Cos, m_Sin, m_BoundsWidth, m_BoundsHeight;
	};
	SGeometry Geometry(int Width, int Height, int StampWidth, int StampHeight, const SQmWatermarkLayer &Layer)
	{
		const float Angle = std::clamp(Layer.m_Angle, -180, 180) * PI / 180.0f;
		SGeometry G{};
		G.m_Cos = std::cos(Angle);
		G.m_Sin = std::sin(Angle);
		G.m_Width = Width * std::clamp(Layer.m_Size, 3, 70) / 100.0f;
		G.m_Height = G.m_Width * StampHeight / std::max(1, StampWidth);
		G.m_BoundsWidth = std::abs(G.m_Cos) * G.m_Width + std::abs(G.m_Sin) * G.m_Height;
		G.m_BoundsHeight = std::abs(G.m_Sin) * G.m_Width + std::abs(G.m_Cos) * G.m_Height;
		const float Fit = std::min({1.0f, Width * 0.96f / std::max(1.0f, G.m_BoundsWidth), Height * 0.96f / std::max(1.0f, G.m_BoundsHeight)});
		G.m_Width *= Fit;
		G.m_Height *= Fit;
		G.m_BoundsWidth *= Fit;
		G.m_BoundsHeight *= Fit;
		return G;
	}

	// 预乘后插值，透明 Logo 边缘不会混入底色黑边。
	ColorRGBA Sample(const CImageInfo &Image, float X, float Y)
	{
		const int Left = (int)std::floor(X), Top = (int)std::floor(Y);
		const float Fx = X - Left, Fy = Y - Top;
		ColorRGBA Result(0, 0, 0, 0);
		for(int Dy = 0; Dy < 2; ++Dy)
			for(int Dx = 0; Dx < 2; ++Dx)
			{
				if(Left + Dx < 0 || Top + Dy < 0 || Left + Dx >= (int)Image.m_Width || Top + Dy >= (int)Image.m_Height)
					continue;
				const auto C = Image.PixelColor(Left + Dx, Top + Dy);
				const float Weight = (Dx ? Fx : 1 - Fx) * (Dy ? Fy : 1 - Fy) * C.a;
				Result.r += C.r * Weight;
				Result.g += C.g * Weight;
				Result.b += C.b * Weight;
				Result.a += Weight;
			}
		return Result;
	}
}

SQmWatermarkBounds QmWatermarkBounds(int Width, int Height, int LayerWidth, int LayerHeight, const SQmWatermarkLayer &Layer)
{
	const auto G = Geometry(Width, Height, LayerWidth, LayerHeight, Layer);
	const float Margin = std::min(Width, Height) * 0.02f;
	return {Margin + std::max(0.0f, Width - 2 * Margin - G.m_BoundsWidth) * std::clamp(Layer.m_X, 0, 1000) / 1000.0f,
		Margin + std::max(0.0f, Height - 2 * Margin - G.m_BoundsHeight) * std::clamp(Layer.m_Y, 0, 1000) / 1000.0f,
		G.m_BoundsWidth, G.m_BoundsHeight};
}

bool QmCompositeWatermark(CImageInfo &Image, const CImageInfo &Stamp, const SQmWatermarkLayer &Layer)
{
	if(!Image.m_pData || !Stamp.m_pData || Image.m_Format != CImageInfo::FORMAT_RGBA || Stamp.m_Format != CImageInfo::FORMAT_RGBA ||
		Image.m_Width == 0 || Image.m_Height == 0 || Stamp.m_Width == 0 || Stamp.m_Height == 0)
		return false;
	const auto G = Geometry(Image.m_Width, Image.m_Height, Stamp.m_Width, Stamp.m_Height, Layer);
	const auto B = QmWatermarkBounds(Image.m_Width, Image.m_Height, Stamp.m_Width, Stamp.m_Height, Layer);
	const float Opacity = std::clamp(Layer.m_Opacity, 0, 100) / 100.0f;
	const auto Draw = [&](float CenterX, float CenterY) {
		const int Left = std::max(0, (int)std::floor(CenterX - G.m_BoundsWidth / 2 - 1));
		const int Top = std::max(0, (int)std::floor(CenterY - G.m_BoundsHeight / 2 - 1));
		const int Right = std::min((int)Image.m_Width, (int)std::ceil(CenterX + G.m_BoundsWidth / 2 + 1));
		const int Bottom = std::min((int)Image.m_Height, (int)std::ceil(CenterY + G.m_BoundsHeight / 2 + 1));
		for(int Y = Top; Y < Bottom; ++Y)
			for(int X = Left; X < Right; ++X)
			{
				const float Dx = X + 0.5f - CenterX, Dy = Y + 0.5f - CenterY;
				const float U = (Dx * G.m_Cos + Dy * G.m_Sin) / G.m_Width + 0.5f;
				const float V = (-Dx * G.m_Sin + Dy * G.m_Cos) / G.m_Height + 0.5f;
				if(U < 0 || V < 0 || U > 1 || V > 1)
					continue;
				const auto Source = Sample(Stamp, U * Stamp.m_Width - 0.5f, V * Stamp.m_Height - 0.5f);
				const auto Base = Image.PixelColor(X, Y);
				const float A = Source.a * Opacity;
				const float OutA = A + Base.a * (1 - A);
				if(OutA > 0)
					Image.SetPixelColor(X, Y, ColorRGBA((Source.r * Opacity + Base.r * Base.a * (1 - A)) / OutA, (Source.g * Opacity + Base.g * Base.a * (1 - A)) / OutA, (Source.b * Opacity + Base.b * Base.a * (1 - A)) / OutA, OutA));
			}
	};
	if(Layer.m_Tile)
	{
		const float StepX = std::max(G.m_BoundsWidth * 1.5f, Image.m_Width * 0.08f);
		const float StepY = std::max(G.m_BoundsHeight * 1.5f, Image.m_Height * 0.08f);
		const float OffsetX = std::fmod(B.m_X + B.m_Width / 2, StepX);
		const float OffsetY = std::fmod(B.m_Y + B.m_Height / 2, StepY);
		for(float Y = OffsetY - StepY; Y < Image.m_Height + StepY; Y += StepY)
			for(float X = OffsetX - StepX; X < Image.m_Width + StepX; X += StepX)
				Draw(X, Y);
	}
	else
		Draw(B.m_X + B.m_Width / 2, B.m_Y + B.m_Height / 2);
	return true;
}
