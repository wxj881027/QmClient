#ifndef GAME_CLIENT_QMUI_QMCHATBUBBLE_H
#define GAME_CLIENT_QMUI_QMCHATBUBBLE_H

#include <base/color.h>
#include <base/vmath.h>

#include <algorithm>
#include <array>
#include <cmath>

class IGraphics;

namespace qm_chat_bubble
{

	constexpr float MAX_TEXT_WIDTH = 230.0f;
	constexpr int CORNER_SEGMENTS = 8;
	constexpr int CONTOUR_POINTS = 4 * (CORNER_SEGMENTS + 1) + 3;
	using TContour = std::array<vec2, CONTOUR_POINTS>;

	struct SLayout
	{
		vec2 m_Position;
		vec2 m_Size;
		vec2 m_TextPosition;
		float m_Radius;
		float m_Stroke;
		float m_TailHeight;
		float m_TailHalfWidth;
		float m_TailLean;
		float m_Scale;
	};

	// 锚点位于尾尖；缩放时尾巴和正文一同收放，尾尖与名牌的间距保持稳定。
	inline SLayout Layout(vec2 Anchor, vec2 TextSize, float FontSize, float Scale)
	{
		const float PaddingX = std::clamp(FontSize * 0.8f, 12.0f, 20.0f);
		const float PaddingY = std::clamp(FontSize * 0.5f, 8.0f, 14.0f);
		const float Height = std::max(TextSize.y, FontSize) + PaddingY * 2.0f;
		const float Radius = std::min(18.0f, Height * 0.4f);
		// 短消息也要为倾斜尾巴留出直边，避免尾根进入两侧圆角。
		const vec2 Size(std::max(std::max(TextSize.x, FontSize) + PaddingX * 2.0f, Radius * 2.0f + 24.0f), Height);
		const float TailHeight = 9.0f * Scale;
		const vec2 Position(Anchor.x - Size.x * Scale * 0.5f, Anchor.y - TailHeight - Size.y * Scale);
		return {
			Position,
			Size * Scale,
			Position + (Size - TextSize) * (Scale * 0.5f),
			Radius * Scale,
			2.0f * Scale,
			TailHeight,
			8.0f * Scale,
			3.0f * Scale,
			Scale};
	}

	// 主体和尾巴共用闭合轮廓，半透明填充不会在接缝处叠出深色三角形。
	inline TContour Contour(const SLayout &Layout, float Inset = 0.0f)
	{
		const vec2 TopLeft = Layout.m_Position + vec2(Inset, Inset);
		const vec2 BottomRight = Layout.m_Position + Layout.m_Size - vec2(Inset, Inset);
		const float Radius = Layout.m_Radius - Inset;
		const float MiddleX = Layout.m_Position.x + Layout.m_Size.x * 0.5f;
		const float BottomY = Layout.m_Position.y + Layout.m_Size.y;
		const std::array<vec2, 4> aCenters = {{{BottomRight.x - Radius, TopLeft.y + Radius},
			{BottomRight.x - Radius, BottomRight.y - Radius},
			{TopLeft.x + Radius, BottomRight.y - Radius},
			{TopLeft.x + Radius, TopLeft.y + Radius}}};
		// 单位圆采样只初始化一次，屏幕位置与动画缩放无需逐帧计算三角函数。
		static const auto s_aDirections = [] {
			std::array<vec2, 4 * (CORNER_SEGMENTS + 1)> aDirections;
			for(int Corner = 0; Corner < 4; ++Corner)
				for(int Segment = 0; Segment <= CORNER_SEGMENTS; ++Segment)
				{
					const float Angle = (Corner - 1 + static_cast<float>(Segment) / CORNER_SEGMENTS) * pi * 0.5f;
					aDirections[Corner * (CORNER_SEGMENTS + 1) + Segment] = vec2(std::cos(Angle), std::sin(Angle));
				}
			return aDirections;
		}();

		TContour aPoints;
		int Index = 0;
		for(int Corner = 0; Corner < 4; ++Corner)
		{
			for(int Segment = 0; Segment <= CORNER_SEGMENTS; ++Segment)
				aPoints[Index++] = aCenters[Corner] + s_aDirections[Corner * (CORNER_SEGMENTS + 1) + Segment] * Radius;
			if(Corner == 1)
			{
				const float HalfWidth = Layout.m_TailHalfWidth - Inset;
				aPoints[Index++] = vec2(MiddleX + Layout.m_TailLean + HalfWidth, BottomRight.y);
				aPoints[Index++] = vec2(MiddleX, BottomY + Layout.m_TailHeight - Inset * 2.0f);
				aPoints[Index++] = vec2(MiddleX + Layout.m_TailLean - HalfWidth, BottomRight.y);
			}
		}
		return aPoints;
	}

	struct SPalette
	{
		ColorRGBA m_Fill;
		ColorRGBA m_Outline;
		ColorRGBA m_Shadow;
	};

	inline SPalette Palette(ColorRGBA Background, float Opacity)
	{
		Opacity = std::clamp(Opacity, 0.0f, 1.0f);
		const float BackgroundAlpha = std::clamp(Background.a, 0.0f, 1.0f);
		const float Luminance = Background.r * 0.2126f + Background.g * 0.7152f + Background.b * 0.0722f;
		// 深底使用浅色墨线，浅底使用深色墨线；保留玩家选择的底色与透明背景。
		ColorRGBA Outline = Luminance < 0.45f ? ColorRGBA(0.91f, 0.86f, 0.98f) : ColorRGBA(0.19f, 0.14f, 0.25f);
		Outline.a = std::sqrt(BackgroundAlpha) * Opacity * 0.9f;
		return {Background.WithAlpha(BackgroundAlpha * Opacity), Outline,
			ColorRGBA(0.08f, 0.06f, 0.12f, BackgroundAlpha * Opacity * 0.35f)};
	}

	void Render(IGraphics *pGraphics, const SLayout &Layout, ColorRGBA Background, float Opacity);

}

#endif
