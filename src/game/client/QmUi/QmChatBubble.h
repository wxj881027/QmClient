#ifndef GAME_CLIENT_QMUI_QMCHATBUBBLE_H
#define GAME_CLIENT_QMUI_QMCHATBUBBLE_H

#include <base/color.h>
#include <base/vmath.h>

#include <algorithm>
#include <array>
#include <cmath>

class IGraphics;
class ITextRender;
class CTextCursor;

namespace qm_chat_bubble
{

	enum class EStyle
	{
		COMIC,
		CANDY,
		CLOUD
	};
	constexpr int STYLE_COUNT = 3;
	constexpr float MAX_TEXT_WIDTH = 230.0f;
	constexpr int CORNER_SEGMENTS = 16;
	constexpr int MAX_CONTOUR_POINTS = 4 * (CORNER_SEGMENTS + 1) + 3;

	inline float AntialiasWidth(vec2 MappedSize, vec2 ScreenSize)
	{
		// 羽化始终覆盖实际像素，不随出现/收起动画缩成不足一个像素。
		return 1.25f * std::max(std::abs(MappedSize.x) / std::max(1.0f, ScreenSize.x), std::abs(MappedSize.y) / std::max(1.0f, ScreenSize.y));
	}

	inline EStyle StyleFromConfig(int Value)
	{
		return static_cast<EStyle>(std::clamp(Value, 0, STYLE_COUNT - 1));
	}

	struct SContour
	{
		std::array<vec2, MAX_CONTOUR_POINTS> m_aPoints;
		int m_Count = 0;
	};

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
		EStyle m_Style;
	};

	// 锚点位于尾尖；文字尺寸独立于镜头缩放，样式只改变留白和外壳。
	inline SLayout Layout(vec2 Anchor, vec2 TextSize, float FontSize, float Scale, EStyle Style = EStyle::COMIC)
	{
		const float PaddingX = std::clamp(FontSize * 0.8f, 12.0f, 20.0f);
		const float PaddingY = std::clamp(FontSize * 0.5f, 8.0f, 14.0f);
		float Height = std::max(TextSize.y, FontSize) + PaddingY * 2.0f;
		float Radius = std::min(26.0f, Height * 0.48f);
		float Width = std::max(std::max(TextSize.x, FontSize) + PaddingX * 2.0f, Radius * 2.0f + 26.0f);
		float Stroke = 2.2f;
		float TailHeight = 11.0f;
		float TailWidth = 9.0f;
		float TailLean = 3.0f;
		if(Style == EStyle::CANDY)
		{
			Radius = Height * 0.5f;
			Width = std::max(Width, Radius * 2.0f + 24.0f);
			Stroke = 1.2f;
			TailHeight = 7.0f;
			TailWidth = 6.0f;
			TailLean = -3.0f;
		}
		else if(Style == EStyle::CLOUD)
		{
			Width *= 1.18f;
			Height *= 1.45f;
			Radius = std::min(Width, Height) * 0.4f;
			Stroke = TailHeight = TailWidth = TailLean = 0.0f;
		}
		const vec2 Size(Width, Height);
		const vec2 Position(Anchor.x - Width * Scale * 0.5f, Anchor.y - (Height + TailHeight) * Scale);
		return {Position, Size * Scale, Position + (Size - TextSize) * (Scale * 0.5f), Radius * Scale,
			Stroke * Scale, TailHeight * Scale, TailWidth * Scale, TailLean * Scale, Scale, Style};
	}

	inline SContour Contour(const SLayout &Layout, float Inset = 0.0f)
	{
		SContour Result;
		const vec2 Center = Layout.m_Position + Layout.m_Size * 0.5f;
		const vec2 TopLeft = Layout.m_Position + vec2(Inset, Inset);
		const vec2 BottomRight = Layout.m_Position + Layout.m_Size - vec2(Inset, Inset);
		const float Radius = Layout.m_Radius - Inset;
		const float BottomY = Layout.m_Position.y + Layout.m_Size.y;
		const std::array<vec2, 4> aCenters = {{{BottomRight.x - Radius, TopLeft.y + Radius},
			{BottomRight.x - Radius, BottomRight.y - Radius}, {TopLeft.x + Radius, BottomRight.y - Radius}, {TopLeft.x + Radius, TopLeft.y + Radius}}};
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
		for(int Corner = 0; Corner < 4; ++Corner)
		{
			for(int Segment = 0; Segment <= CORNER_SEGMENTS; ++Segment)
				Result.m_aPoints[Result.m_Count++] = aCenters[Corner] + s_aDirections[Corner * (CORNER_SEGMENTS + 1) + Segment] * Radius;
			if(Corner == 1)
			{
				const float HalfWidth = Layout.m_TailHalfWidth - Inset;
				Result.m_aPoints[Result.m_Count++] = vec2(Center.x + Layout.m_TailLean + HalfWidth, BottomRight.y);
				Result.m_aPoints[Result.m_Count++] = vec2(Center.x, BottomY + Layout.m_TailHeight - Inset * 2.0f);
				Result.m_aPoints[Result.m_Count++] = vec2(Center.x + Layout.m_TailLean - HalfWidth, BottomRight.y);
			}
		}
		return Result;
	}

	struct SPalette
	{
		ColorRGBA m_Fill;
		ColorRGBA m_FillBottom;
		ColorRGBA m_Outline;
		ColorRGBA m_Shadow;
		ColorRGBA m_Text;
	};

	inline SPalette Palette(EStyle Style, bool CustomColors, ColorRGBA Background, ColorRGBA Text, float Opacity)
	{
		SPalette Result;
		switch(Style)
		{
		case EStyle::CLOUD: Result = {{1.0f, 1.0f, 1.0f}, {0.92f, 0.95f, 1.0f}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0.13f, 0.18f, 0.25f}}; break;
		case EStyle::CANDY: Result = {{0.66f, 0.94f, 1.0f}, {0.78f, 0.70f, 1.0f}, {0.96f, 0.98f, 1.0f, 0.9f}, {0.28f, 0.22f, 0.53f, 0.25f}, {0.15f, 0.17f, 0.35f}}; break;
		default: Result = {{1.0f, 0.99f, 0.94f}, {1.0f, 0.99f, 0.94f}, {0.16f, 0.13f, 0.17f}, {0.16f, 0.13f, 0.17f, 0.35f}, {0.16f, 0.13f, 0.17f}}; break;
		}
		if(CustomColors)
		{
			Background.a = std::clamp(Background.a, 0.0f, 1.0f);
			Result.m_Fill = Result.m_FillBottom = Background;
			const float Luminance = Background.r * 0.2126f + Background.g * 0.7152f + Background.b * 0.0722f;
			Result.m_Outline = Luminance < 0.45f ? ColorRGBA(0.91f, 0.86f, 0.98f) : ColorRGBA(0.19f, 0.14f, 0.25f);
			Result.m_Outline.a = std::sqrt(Background.a) * 0.9f;
			Result.m_Shadow.a *= Background.a;
			Result.m_Text = Text;
		}
		Opacity = std::clamp(Opacity, 0.0f, 1.0f);
		Result.m_Fill.a *= Opacity;
		Result.m_FillBottom.a *= Opacity;
		Result.m_Outline.a *= Opacity;
		Result.m_Shadow.a *= Opacity;
		Result.m_Text.a *= Opacity;
		return Result;
	}

	void Render(IGraphics *pGraphics, const SLayout &Layout, const SPalette &Colors);
	void RenderText(ITextRender *pTextRender, CTextCursor &Cursor, const char *pText, ColorRGBA Color, ColorRGBA Outline);

}
#endif
