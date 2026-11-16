#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_BADGE_MOTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_TITLE_BADGE_MOTION_H

#include "qm_title_appearance.h"

#include <engine/graphics.h>

#include <game/client/ui_rect.h>

namespace qm_title_badge_motion
{
	// 在高 24 的设计坐标内绘制。两条光带贴内壁运行，文字所在的中段只留暗底。
	class CCanvas
	{
		IGraphics &m_Graphics;
		vec2 m_Origin;
		float m_Scale;
		float m_Width;
		const SQmTitleBadgePalette &m_Palette;

		float TextShade(vec2 Point) const
		{
			// 字面落在 x=30 到右端留白之间。穿过字面的轨迹压暗，靠近上下边缘逐渐恢复原亮度。
			const float Clear = std::max({(30.0f - Point.x) / 4.0f, (Point.x - (m_Width - 9.0f)) / 3.0f, (std::abs(Point.y - 12.0f) - 4.0f) / 4.0f});
			return mix(0.36f, 1.0f, std::clamp(Clear, 0.0f, 1.0f));
		}

		void Quad(vec2 A, vec2 B, vec2 C, vec2 D, ColorRGBA Color, float OpacityA, float OpacityB, float OpacityC, float OpacityD)
		{
			const IGraphics::CColorVertex aColors[] = {
				{0, Color.WithMultipliedAlpha(OpacityA * TextShade(A))}, {1, Color.WithMultipliedAlpha(OpacityB * TextShade(B))},
				{2, Color.WithMultipliedAlpha(OpacityC * TextShade(C))}, {3, Color.WithMultipliedAlpha(OpacityD * TextShade(D))}};
			m_Graphics.SetColorVertex(aColors, 4);
			const IGraphics::CFreeformItem Item(m_Origin + A * m_Scale, m_Origin + B * m_Scale, m_Origin + C * m_Scale, m_Origin + D * m_Scale);
			m_Graphics.QuadsDrawFreeform(&Item, 1);
		}

		struct SBandPoint
		{
			vec2 m_Outside, m_Crest, m_Inside;
			float m_Light;
		};

		SBandPoint BandPoint(EQmTitleBadgeStyle Style, float U, float Phase, float Side) const
		{
			const float X = mix(2.0f, m_Width - 2.0f, U);
			const float CapX = X - std::clamp(X, 12.0f, m_Width - 12.0f);
			const float H = std::sqrt(std::max(0.0f, 10.5f * 10.5f - CapX * CapX));
			const float Along = Side > 0.0f ? U : 1.0f - U;
			const float Wave = 2.0f * pi * (Along - Phase);
			float Crest = 0.80f, Glow = 0.28f, Light = 0.0f;
			switch(Style)
			{
			case EQmTitleBadgeStyle::MOON:
				Light = 0.5f + 0.5f * std::cos(Wave);
				Crest += 0.11f * std::sin(Wave);
				break;
			case EQmTitleBadgeStyle::SUN:
			{
				const float Flare = 0.5f + 0.5f * std::cos(3.0f * Wave);
				Light = Flare * Flare;
				Crest -= 0.19f * Light;
				Glow += 0.10f * Light;
				break;
			}
			case EQmTitleBadgeStyle::EARTH:
				Light = 0.5f + 0.5f * std::sin(2.0f * Wave + Side);
				Crest += 0.14f * std::sin(2.0f * Wave + 0.6f * std::sin(Wave));
				Glow = 0.36f;
				break;
			case EQmTitleBadgeStyle::MORNING_STAR:
			{
				const float Sweep = 0.5f + 0.5f * std::cos(Wave);
				Light = Sweep * Sweep * Sweep * Sweep;
				Crest += 0.09f * std::sin(Wave);
				break;
			}
			case EQmTitleBadgeStyle::BLACK_HOLE:
				// 两股吸积流向同一侧收拢，头衔越长，拉出的轨迹越长。
				Light = 0.5f + 0.5f * std::cos(2.0f * pi * (1.6f * U - Phase) + Side);
				Crest = 0.91f - 0.31f * U + 0.05f * std::sin(Wave);
				break;
			}
			const float Offset = Side * H;
			const float FadeAtTips = std::min(1.0f, std::min(U, 1.0f - U) * 12.0f);
			return {{X, 12.0f + Offset}, {X, 12.0f + Offset * Crest}, {X, 12.0f + Offset * (Crest - Glow)},
				(0.18f + 0.72f * Light) * FadeAtTips};
		}

		void Bands(EQmTitleBadgeStyle Style, float Phase)
		{
			const int Steps = std::clamp(static_cast<int>(m_Width / 7.0f), 12, 30);
			for(float Side : {-1.0f, 1.0f})
			{
				auto Previous = BandPoint(Style, 0.0f, Phase, Side);
				for(int i = 1; i <= Steps; ++i)
				{
					const auto Next = BandPoint(Style, static_cast<float>(i) / Steps, Phase, Side);
					Quad(Previous.m_Outside, Next.m_Outside, Previous.m_Crest, Next.m_Crest, m_Palette.m_Icon, 0, 0, Previous.m_Light, Next.m_Light);
					Quad(Previous.m_Crest, Next.m_Crest, Previous.m_Inside, Next.m_Inside, m_Palette.m_Icon, Previous.m_Light, Next.m_Light, 0, 0);
					Previous = Next;
				}
			}
		}

		// 椭圆弧从内侧亮脊渐隐到外侧，起止角控制自转的明暗面。
		void Arc(vec2 Center, vec2 Radius, float Tilt, float Start, float Sweep, float Width, ColorRGBA Color, float Light)
		{
			const vec2 Axis(std::cos(Tilt), std::sin(Tilt));
			const vec2 Normal(-Axis.y, Axis.x);
			const int Steps = std::max(4, static_cast<int>(std::ceil(std::abs(Sweep) * 3.0f)));
			vec2 PrevInner{}, PrevOuter{};
			for(int i = 0; i <= Steps; ++i)
			{
				const float Angle = Start + Sweep * static_cast<float>(i) / Steps;
				const vec2 X = Axis * std::cos(Angle), Y = Normal * std::sin(Angle);
				const vec2 Inner = Center + X * Radius.x + Y * Radius.y;
				const vec2 Outer = Inner + (X + Y) * Width;
				if(i)
					Quad(PrevInner, Inner, PrevOuter, Outer, Color, Light, Light, 0, 0);
				PrevInner = Inner;
				PrevOuter = Outer;
			}
		}

		void Star(vec2 Center, float Radius, float Angle, float Light)
		{
			for(int i = 0; i < 4; ++i)
			{
				const float Ray = Angle + i * pi * 0.5f;
				const vec2 Axis(std::cos(Ray), std::sin(Ray));
				const vec2 Shoulder(-Axis.y * Radius * 0.20f, Axis.x * Radius * 0.20f);
				Quad(Center, Center + Shoulder, Center - Shoulder, Center + Axis * Radius, m_Palette.m_Text, Light, 0, 0, 0);
			}
		}

	public:
		CCanvas(IGraphics &Graphics, const CUIRect &Rect, const SQmTitleBadgePalette &Palette) :
			m_Graphics(Graphics), m_Origin(Rect.x, Rect.y), m_Scale(Rect.h / QM_TITLE_BADGE_HEIGHT), m_Width(Rect.w / m_Scale), m_Palette(Palette) {}

		void Draw(EQmTitleBadgeStyle Style, double Time)
		{
			if(m_Width < QM_TITLE_BADGE_HEIGHT || !std::isfinite(Time))
				return;
			// 周期整除名牌与聊天共用时钟的 3600 秒回绕周期。
			const double Period = Style == EQmTitleBadgeStyle::EARTH ? 8.0 : Style == EQmTitleBadgeStyle::MORNING_STAR ? 4.0 : 6.0;
			const float Phase = static_cast<float>(Time / Period - std::floor(Time / Period));
			const float Angle = 2.0f * pi * Phase;
			const float Pulse = 0.5f + 0.5f * std::cos(Angle);
			const vec2 Core(m_Width - 12.0f, 12.0f);

			m_Graphics.TextureClear();
			m_Graphics.QuadsBegin();
			Bands(Style, Phase);
			switch(Style)
			{
			case EQmTitleBadgeStyle::MOON:
				Arc(Core, {5.2f, 6.6f}, -0.42f, 0.16f * pi, 1.10f * pi, 2.1f, m_Palette.m_Text, 0.64f);
				Star(Core + vec2(7.2f * std::cos(Angle), 5.7f * std::sin(Angle)), 2.3f, 0.24f, 0.88f);
				break;
			case EQmTitleBadgeStyle::SUN:
				Arc(Core, {4.6f, 4.6f}, 0, 0, 2 * pi, 2.0f + Pulse, m_Palette.m_Icon, 0.86f);
				for(int i = 0; i < 6; ++i)
					Arc(Core, {6.2f, 6.2f}, 0, Angle + i * pi / 3, 0.54f, 1.6f + Pulse, m_Palette.m_Text, 0.76f);
				break;
			case EQmTitleBadgeStyle::EARTH:
				Arc(Core, {6.6f, 6.6f}, 0, -pi, 2 * pi, 1.4f, m_Palette.m_Icon, 0.72f);
				for(int i = 0; i < 3; ++i)
				{
					const float Longitude = Angle + i * 2.0f * pi / 3.0f;
					Arc(Core, {5.7f * std::sin(Longitude), 6.2f}, -0.30f, -pi * 0.5f, pi, 0.9f, m_Palette.m_Text, 0.38f + 0.28f * std::cos(Longitude));
				}
				break;
			case EQmTitleBadgeStyle::MORNING_STAR:
				Star(Core, 6.3f + 2.6f * Pulse, -0.15f + 0.17f * std::sin(Angle), 1.0f);
				Star(Core, 5.3f - 1.7f * Pulse, pi * 0.25f, 0.74f);
				for(int i = 0; i < 3; ++i)
				{
					const float U = (i + 0.5f) / 3.0f;
					const float Flash = 0.5f + 0.5f * std::cos(Angle - 2.0f * pi * U);
					Star({mix(12.0f, m_Width - 12.0f, U), i % 2 ? 5.1f : 18.9f}, 1.1f + 1.9f * Flash, 0.15f, Flash);
				}
				break;
			case EQmTitleBadgeStyle::BLACK_HOLE:
				m_Graphics.SetColor(m_Palette.m_Background);
				m_Graphics.DrawCircle(Core.x * m_Scale + m_Origin.x, Core.y * m_Scale + m_Origin.y, 4.7f * m_Scale, 16);
				Arc(Core, {4.9f, 5.0f}, -0.24f, Angle, 1.65f * pi, 1.15f, m_Palette.m_Icon, 0.93f);
				Arc(Core, {8.4f, 2.2f}, -0.29f, Angle, 1.42f * pi, 1.3f, m_Palette.m_Text, 0.90f);
				break;
			}
			m_Graphics.QuadsEnd();
		}
	};
}

#endif
