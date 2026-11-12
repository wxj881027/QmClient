#include "QmSeasonalSidebar.h"

#include <base/time.h>

#include <engine/graphics.h>

#include <game/client/ui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	void Triangle(IGraphics *pGraphics, vec2 A, vec2 B, vec2 C)
	{
		const IGraphics::CFreeformItem Item(A, B, C, C);
		pGraphics->QuadsDrawFreeform(&Item, 1);
	}

	void Stroke(IGraphics *pGraphics, vec2 A, vec2 B, float Width)
	{
		const vec2 D = B - A;
		const float Length = length(D);
		if(Length <= 0.0f)
			return;
		const vec2 N(-D.y / Length * Width * 0.5f, D.x / Length * Width * 0.5f);
		const IGraphics::CFreeformItem Item(A + N, B + N, A - N, B - N);
		pGraphics->QuadsDrawFreeform(&Item, 1);
	}

	void Ellipse(IGraphics *pGraphics, vec2 Center, vec2 Radius)
	{
		for(int Index = 0; Index < 24; ++Index)
		{
			const float A = Index * 2.0f * pi / 24.0f;
			const float B = (Index + 1) * 2.0f * pi / 24.0f;
			Triangle(pGraphics, Center, Center + vec2(std::cos(A), std::sin(A)) * Radius, Center + vec2(std::cos(B), std::sin(B)) * Radius);
		}
	}

	void Star(IGraphics *pGraphics, vec2 Center, float Radius)
	{
		for(int Index = 0; Index < 10; ++Index)
		{
			const float A = Index * pi / 5.0f - pi * 0.5f;
			const float B = (Index + 1) * pi / 5.0f - pi * 0.5f;
			Triangle(pGraphics, Center, Center + direction(A) * Radius * (Index % 2 == 0 ? 1.0f : 0.43f),
				Center + direction(B) * Radius * (Index % 2 == 0 ? 0.43f : 1.0f));
		}
	}

	ColorRGBA Accent(EQmSidebarHoliday Holiday, float Alpha)
	{
		switch(Holiday)
		{
		case EQmSidebarHoliday::SPRING_FESTIVAL:
		case EQmSidebarHoliday::NATIONAL_DAY: return ColorRGBA(1.0f, 0.76f, 0.29f, Alpha);
		case EQmSidebarHoliday::QINGMING: return ColorRGBA(0.48f, 0.79f, 0.65f, Alpha);
		case EQmSidebarHoliday::LABOUR_DAY: return ColorRGBA(0.96f, 0.70f, 0.35f, Alpha);
		case EQmSidebarHoliday::DRAGON_BOAT: return ColorRGBA(0.41f, 0.85f, 0.61f, Alpha);
		case EQmSidebarHoliday::MID_AUTUMN: return ColorRGBA(1.0f, 0.87f, 0.53f, Alpha);
		case EQmSidebarHoliday::HALLOWEEN: return ColorRGBA(1.0f, 0.55f, 0.16f, Alpha);
		default: return ColorRGBA(0.83f, 0.96f, 1.0f, Alpha);
		}
	}

	void Emblem(IGraphics *pGraphics, EQmSidebarHoliday Holiday, vec2 P, float R, double TimeSec)
	{
		const float Sway = std::sin(static_cast<float>(std::fmod(TimeSec, 6.0) * pi / 3.0)) * R * 0.10f;
		P.x += Sway;
		pGraphics->SetColor(Accent(Holiday, 0.58f));
		switch(Holiday)
		{
		case EQmSidebarHoliday::SPRING_FESTIVAL:
			Stroke(pGraphics, P - vec2(0, R * 1.4f), P + vec2(0, R * 1.5f), R * 0.10f);
			pGraphics->SetColor(ColorRGBA(0.91f, 0.20f, 0.22f, 0.68f));
			Ellipse(pGraphics, P, vec2(R * 0.72f, R));
			pGraphics->SetColor(Accent(Holiday, 0.65f));
			Stroke(pGraphics, P - vec2(R * 0.4f, R * 0.8f), P + vec2(R * 0.4f, -R * 0.8f), R * 0.13f);
			Stroke(pGraphics, P - vec2(R * 0.4f, -R * 0.8f), P + vec2(R * 0.4f, R * 0.8f), R * 0.13f);
			break;
		case EQmSidebarHoliday::QINGMING:
			for(int Branch = -1; Branch <= 1; ++Branch)
			{
				const vec2 A = P + vec2(Branch * R * 0.55f, -R);
				const vec2 B = A + vec2(Sway + Branch * R * 0.25f, R * 2.2f);
				Stroke(pGraphics, A, B, R * 0.07f);
				for(int Leaf = 0; Leaf < 4; ++Leaf)
				{
					const vec2 C = mix(A, B, 0.25f + Leaf * 0.2f);
					Triangle(pGraphics, C, C + vec2(-R * 0.4f, R * 0.4f), C + vec2(-R * 0.05f, R * 0.45f));
				}
			}
			break;
		case EQmSidebarHoliday::LABOUR_DAY:
			Stroke(pGraphics, P - vec2(0, R), P + vec2(0, R), R * 0.09f);
			for(int Index = 0; Index < 4; ++Index)
				for(int Side : {-1, 1})
				{
					const vec2 C = P + vec2(0, R * (0.65f - Index * 0.43f));
					Triangle(pGraphics, C, C + vec2(Side * R * 0.75f, -R * 0.45f), C + vec2(Side * R * 0.35f, -R * 0.65f));
				}
			break;
		case EQmSidebarHoliday::DRAGON_BOAT:
			Triangle(pGraphics, P - vec2(0, R), P + vec2(-R, R * 0.7f), P + vec2(R, R * 0.7f));
			pGraphics->SetColor(ColorRGBA(1.0f, 0.85f, 0.48f, 0.72f));
			Stroke(pGraphics, P + vec2(-R * 0.5f, 0), P + vec2(R * 0.7f, R * 0.45f), R * 0.12f);
			Stroke(pGraphics, P + vec2(R * 0.5f, 0), P + vec2(-R * 0.7f, R * 0.45f), R * 0.12f);
			break;
		case EQmSidebarHoliday::MID_AUTUMN:
			for(int Index = 0; Index < 24; ++Index)
			{
				const float A = pi * 0.5f + Index * pi / 24.0f;
				const float B = pi * 0.5f + (Index + 1) * pi / 24.0f;
				const IGraphics::CFreeformItem Item(P + direction(A) * R, P + direction(B) * R,
					P + vec2(std::cos(A) * R * 0.25f, std::sin(A) * R), P + vec2(std::cos(B) * R * 0.25f, std::sin(B) * R));
				pGraphics->QuadsDrawFreeform(&Item, 1);
			}
			break;
		case EQmSidebarHoliday::NATIONAL_DAY:
			pGraphics->SetColor(ColorRGBA(0.95f, 0.22f, 0.25f, 0.48f));
			Triangle(pGraphics, P + vec2(-R, -R), P + vec2(R, -R), P + vec2(-R, R));
			pGraphics->SetColor(Accent(Holiday, 0.75f));
			Star(pGraphics, P + vec2(-R * 0.25f, -R * 0.25f), R * 0.48f);
			break;
		case EQmSidebarHoliday::HALLOWEEN:
			Ellipse(pGraphics, P, vec2(R, R * 0.77f));
			pGraphics->SetColor(ColorRGBA(0.40f, 0.70f, 0.38f, 0.72f));
			Stroke(pGraphics, P - vec2(0, R * 0.6f), P - vec2(-R * 0.12f, R), R * 0.22f);
			pGraphics->SetColor(ColorRGBA(0.17f, 0.10f, 0.23f, 0.90f));
			for(int Side : {-1, 1})
			{
				const vec2 Eye = P + vec2(Side * R * 0.4f, -R * 0.13f);
				Triangle(pGraphics, Eye + vec2(0, -R * 0.18f), Eye + vec2(-R * 0.16f, R * 0.12f), Eye + vec2(R * 0.16f, R * 0.12f));
			}
			Stroke(pGraphics, P + vec2(-R * 0.32f, R * 0.3f), P + vec2(0, R * 0.44f), R * 0.10f);
			Stroke(pGraphics, P + vec2(0, R * 0.44f), P + vec2(R * 0.32f, R * 0.3f), R * 0.10f);
			break;
		case EQmSidebarHoliday::CHRISTMAS:
			pGraphics->SetColor(ColorRGBA(0.34f, 0.80f, 0.56f, 0.66f));
			for(int Tier = 0; Tier < 3; ++Tier)
			{
				const float Y = P.y - R + Tier * R * 0.5f;
				const float W = R * (0.55f + Tier * 0.22f);
				Triangle(pGraphics, vec2(P.x, Y), vec2(P.x - W, Y + R), vec2(P.x + W, Y + R));
			}
			pGraphics->SetColor(ColorRGBA(1.0f, 0.84f, 0.39f, 0.85f));
			Star(pGraphics, P - vec2(0, R), R * 0.28f);
			break;
		default: break;
		}
	}
}

void CQmSeasonalSidebar::Render(CUi *pUi, IGraphics *pGraphics, const CUIRect &Rect, double TimeSec)
{
	if(pUi->DrawingSuppressed() || Rect.w < 24.0f || Rect.h < 40.0f)
		return;
	const int64_t Minute = time_timestamp() / 60;
	if(Minute != m_LastMinute)
	{
		m_LastMinute = Minute;
		char aDate[32];
		str_timestamp_format(aDate, sizeof(aDate), "%Y-%m-%d");
		int Year, Month, Day;
		m_Holiday = std::sscanf(aDate, "%d-%d-%d", &Year, &Month, &Day) == 3 ? QmSidebarHoliday(Year, Month, Day) : EQmSidebarHoliday::NONE;
	}
	if(m_Holiday == EQmSidebarHoliday::NONE)
		return;
	if(!std::isfinite(TimeSec))
		TimeSec = 0.0;
	CUIRect Clip;
	Rect.Margin(3.0f, &Clip);
	pUi->ClipEnable(&Clip);
	pUi->FlushQuadBatch();
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->QuadsSetRotation(0.0f);
	const float R = std::clamp(Rect.w * 0.075f, 4.0f, 8.0f);
	// 顶部角饰和两侧窄轨道留出文字及按钮中心，不创建任何输入控件。
	for(int Side : {-1, 1})
		Emblem(pGraphics, m_Holiday, vec2(Side < 0 ? Rect.x + R * 1.9f : Rect.x + Rect.w - R * 1.9f, Rect.y + R * 2.1f), R, TimeSec);
	for(int Index = 0; Index < QM_SIDEBAR_HOLIDAY_PARTICLES; ++Index)
	{
		const auto Particle = QmSidebarHolidayParticle(m_Holiday, Index, TimeSec);
		const vec2 P = vec2(Rect.x, Rect.y) + Particle.m_Position * vec2(Rect.w, Rect.h);
		pGraphics->SetColor(Accent(m_Holiday, Particle.m_Alpha));
		if(m_Holiday == EQmSidebarHoliday::CHRISTMAS)
		{
			for(int Axis = 0; Axis < 3; ++Axis)
			{
				const vec2 D = direction(Axis * pi / 3.0f) * Particle.m_Size;
				Stroke(pGraphics, P - D, P + D, 0.5f);
			}
		}
		else if(m_Holiday == EQmSidebarHoliday::QINGMING)
			Stroke(pGraphics, P, P + vec2(-Particle.m_Size, Particle.m_Size * 2.0f), 0.6f);
		else if(m_Holiday == EQmSidebarHoliday::HALLOWEEN)
		{
			const float S = Particle.m_Size;
			Triangle(pGraphics, P, P + vec2(-S * 1.7f, -S), P + vec2(-S * 0.7f, S * 0.3f));
			Triangle(pGraphics, P, P + vec2(S * 1.7f, -S), P + vec2(S * 0.7f, S * 0.3f));
		}
		else
			Star(pGraphics, P, Particle.m_Size);
	}
	pGraphics->QuadsEnd();
	pGraphics->SetColor(1, 1, 1, 1);
	pUi->ClipDisable();
}
