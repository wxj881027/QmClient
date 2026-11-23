#include "QmFinishSummaryCover.h"

#include "UiSurface.h"
#include "UiTokens.h"

#include <engine/client/rounded_rect_geometry.h>
#include <engine/shared/config.h>

#include <algorithm>

namespace qm_finish_summary
{
	std::array<SCoverRow, COVER_SEGMENTS + 1> CoverRows(float Width, float Height, float Radius,
		int CornerCurve, float Smoothing, float CapsuleSeam)
	{
		std::array<SCoverRow, COVER_SEGMENTS + 1> aRows{};
		if(Width <= 0.0f || Height <= 0.0f)
			return aRows;
		const auto Shape = ResolveRoundedCornerShape(Radius, Width, Height, Smoothing, CornerCurve, CapsuleSeam);
		const float R = Shape.m_Radius;
		for(int i = 0; i <= COVER_SEGMENTS; ++i)
		{
			SCoverRow &Row = aRows[i];
			if(i <= 8 || i >= 16)
			{
				float X, Y;
				RoundedCornerCurveDirection(CornerCurve, Shape.m_Amount, i <= 8 ? i : i - 16, 8, X, Y);
				X = std::clamp(X, 0.0f, 1.0f);
				Y = std::clamp(Y, 0.0f, 1.0f);
				Row.m_Left = R * (1.0f - (i <= 8 ? Y : X));
				Row.m_Y = i <= 8 ? R * (1.0f - X) : Height - R + R * Y;
			}
			else
				Row.m_Y = R + (Height - 2.0f * R) * (i - 8) / 8.0f;
			Row.m_Right = Width - Row.m_Left;
		}
		return aRows;
	}

	vec2 CoverUV(vec2 Point, float Width, float Height)
	{
		if(Width <= 0.0f || Height <= 0.0f)
			return {};
		// 缓存为 2:1 图，按封面覆盖区域等比居中裁切。
		const float ImageWidth = std::max(Width, Height * 2.0f);
		return {(Point.x - Width * 0.5f) / ImageWidth + 0.5f, (Point.y - Height * 0.5f) / (ImageWidth * 0.5f) + 0.5f};
	}

	void RenderCover(IGraphics &Graphics, const CUIRect &Rect, IGraphics::CTextureHandle Texture,
		float Radius, float Alpha)
	{
		if(Alpha <= 0.001f || Rect.w <= 0.0f || Rect.h <= 0.0f || !Graphics.IsTextureHandleAllocated(Texture))
			return;
		const float Pixel = Graphics.GetScreen().Size().y / std::max(Graphics.ScreenHeight(), 1);
		SRoundedImageParams Image;
		Image.m_Texture = Texture;
		Image.m_UVOffset = CoverUV({}, Rect.w, Rect.h);
		const vec2 Extent = CoverUV(Rect.Size(), Rect.w, Rect.h) - Image.m_UVOffset;
		Image.m_UVScale = {Extent.x / Rect.w, Extent.y / Rect.h};
		Image.m_Alpha = Alpha;
		SRoundedSurfaceParams Surface;
		Surface.m_Radius = Radius;
		Surface.m_PixelSize = Pixel;
		Surface.m_AlignToPixel = false;
		if(DrawRoundedImage(&Graphics, Rect, Image, ui_token::color::COLOR_TRANSPARENT, Surface))
			return;
		const auto aRows = CoverRows(Rect.w, Rect.h, Radius, g_Config.m_QmUiCornerCurve,
			QmEffectiveCornerSmoothing(), QmEffectiveCapsuleSeam());
		const float Opacity = std::clamp(Alpha, 0.0f, 1.0f);
		const float Feather = std::min(Rect.w * 0.005f, Pixel);
		Graphics.WrapClamp();
		Graphics.TextureSet(Texture);
		Graphics.QuadsBegin();
		const auto DrawStrip = [&](float LeftTop, float RightTop, float LeftBottom, float RightBottom, float Top, float Bottom, float LeftAlpha, float RightAlpha) {
			const std::array<vec2, 4> aPoints = {vec2(LeftTop, Top), vec2(RightTop, Top), vec2(LeftBottom, Bottom), vec2(RightBottom, Bottom)};
			std::array<vec2, 4> aUV;
			for(size_t i = 0; i < aUV.size(); ++i)
				aUV[i] = CoverUV(aPoints[i], Rect.w, Rect.h);
			Graphics.QuadsSetSubsetFree(aUV[0].x, aUV[0].y, aUV[1].x, aUV[1].y, aUV[2].x, aUV[2].y, aUV[3].x, aUV[3].y);
			const IGraphics::CColorVertex aColors[] = {
				{0, 1, 1, 1, LeftAlpha}, {1, 1, 1, 1, RightAlpha},
				{2, 1, 1, 1, LeftAlpha}, {3, 1, 1, 1, RightAlpha}};
			Graphics.SetColorVertex(aColors, std::size(aColors));
			const IGraphics::CFreeformItem Quad(Rect.TopLeft() + aPoints[0], Rect.TopLeft() + aPoints[1], Rect.TopLeft() + aPoints[2], Rect.TopLeft() + aPoints[3]);
			Graphics.QuadsDrawFreeform(&Quad, 1);
		};
		for(int i = 0; i < COVER_SEGMENTS; ++i)
		{
			const auto &Top = aRows[i];
			const auto &Bottom = aRows[i + 1];
			if(Bottom.m_Y <= Top.m_Y)
				continue;
			const float TopEdge = std::min(Feather, (Top.m_Right - Top.m_Left) * 0.5f);
			const float BottomEdge = std::min(Feather, (Bottom.m_Right - Bottom.m_Left) * 0.5f);
			DrawStrip(Top.m_Left, Top.m_Left + TopEdge, Bottom.m_Left, Bottom.m_Left + BottomEdge, Top.m_Y, Bottom.m_Y, 0.0f, Opacity);
			DrawStrip(Top.m_Left + TopEdge, Top.m_Right - TopEdge, Bottom.m_Left + BottomEdge, Bottom.m_Right - BottomEdge, Top.m_Y, Bottom.m_Y, Opacity, Opacity);
			DrawStrip(Top.m_Right - TopEdge, Top.m_Right, Bottom.m_Right - BottomEdge, Bottom.m_Right, Top.m_Y, Bottom.m_Y, Opacity, 0.0f);
		}
		Graphics.QuadsEnd();
		Graphics.QuadsSetSubset(0, 0, 1, 1);
		Graphics.TextureClear();
		Graphics.WrapNormal();
		Graphics.SetColor(1, 1, 1, 1);
	}
}
