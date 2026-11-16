#include "QmFinishSummaryCover.h"

#include <engine/client/rounded_rect_geometry.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

namespace qm_finish_summary
{
	std::array<SCoverRow, COVER_SEGMENTS + 1> CoverRows(float Width, float Height, float Radius,
		double NowSeconds, int MotionLevel, int CornerCurve, float Smoothing, float CapsuleSeam)
	{
		std::array<SCoverRow, COVER_SEGMENTS + 1> aRows{};
		if(Width <= 0.0f || Height <= 0.0f)
			return aRows;
		const auto Shape = ResolveRoundedCornerShape(Radius, Width, Height, Smoothing, CornerCurve, CapsuleSeam);
		const float R = Shape.m_Radius;
		const double Time = std::isfinite(NowSeconds) ? NowSeconds : 0.0;
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
			const float V = Row.m_Y / Height;
			// 两端固定，边界向两侧缓慢摆动，纹理坐标始终按原图位置计算。
			const float Offset = MotionLevel > 0 ? 0.02f * std::sin(pi * V) *
								       static_cast<float>(0.65 * std::sin(Time * 0.45 + V * 10.0) + 0.35 * std::sin(Time * 0.31 - V * 23.0)) :
							       0.0f;
			const float FadeWidth = Width / 3.0f;
			const float Center = Width * (0.65f - 0.35f * V + Offset);
			Row.m_FadeStart = std::clamp(Center - FadeWidth * 0.5f, Row.m_Left, std::max(Row.m_Left, Width - FadeWidth));
			Row.m_FadeEnd = std::min(Width, Row.m_FadeStart + FadeWidth);
		}
		return aRows;
	}

	vec2 CoverUV(vec2 Point, float Width, float Height)
	{
		if(Width <= 0.0f || Height <= 0.0f)
			return {};
		// 缓存为 2:1 图，按封面覆盖区域等比居中裁切。
		const float ImageWidth = std::max(Width * 0.84f, Height * 2.0f);
		return {(Point.x - Width * 0.42f) / ImageWidth + 0.5f, (Point.y - Height * 0.5f) / (ImageWidth * 0.5f) + 0.5f};
	}

	void RenderCover(IGraphics &Graphics, const CUIRect &Rect, IGraphics::CTextureHandle Texture,
		float Radius, double NowSeconds, float Alpha, int MotionLevel)
	{
		if(Alpha <= 0.001f || Rect.w <= 0.0f || Rect.h <= 0.0f || !Graphics.IsTextureHandleAllocated(Texture))
			return;
		const auto aRows = CoverRows(Rect.w, Rect.h, Radius, NowSeconds, MotionLevel, g_Config.m_QmUiCornerCurve,
			g_Config.m_QmUiCornerSmoothing / 100.0f, g_Config.m_QmUiCapsuleSeam / 100.0f);
		const float Opacity = std::clamp(Alpha, 0.0f, 1.0f);
		const float Feather = std::min(Rect.w * 0.005f, Graphics.GetScreen().Size().y / std::max(Graphics.ScreenHeight(), 1));
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
			const float TopInner = std::min(Top.m_Left + Feather, Top.m_FadeStart);
			const float BottomInner = std::min(Bottom.m_Left + Feather, Bottom.m_FadeStart);
			DrawStrip(Top.m_Left, TopInner, Bottom.m_Left, BottomInner, Top.m_Y, Bottom.m_Y, 0.0f, Opacity);
			DrawStrip(TopInner, Top.m_FadeStart, BottomInner, Bottom.m_FadeStart, Top.m_Y, Bottom.m_Y, Opacity, Opacity);
			// 分段平滑两端斜率，让实图、渐变带和底色平顺衔接。
			for(int Step = 0; Step < 4; ++Step)
			{
				const float A = Step / 4.0f;
				const float B = (Step + 1) / 4.0f;
				DrawStrip(mix(Top.m_FadeStart, Top.m_FadeEnd, A), mix(Top.m_FadeStart, Top.m_FadeEnd, B),
					mix(Bottom.m_FadeStart, Bottom.m_FadeEnd, A), mix(Bottom.m_FadeStart, Bottom.m_FadeEnd, B), Top.m_Y, Bottom.m_Y,
					Opacity * (1.0f - A * A * (3.0f - 2.0f * A)), Opacity * (1.0f - B * B * (3.0f - 2.0f * B)));
			}
		}
		Graphics.QuadsEnd();
		Graphics.QuadsSetSubset(0, 0, 1, 1);
		Graphics.TextureClear();
		Graphics.WrapNormal();
		Graphics.SetColor(1, 1, 1, 1);
	}
}
