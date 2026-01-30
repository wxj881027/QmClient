#include "ui_effects.h"

#include <engine/shared/config.h>
#include <game/client/gameclient.h>

#include <cmath>
#include <algorithm>

constexpr float PI = 3.14159265358979323846f;
constexpr float SMOOTH_VALUE_EPSILON = 0.01f; // Threshold for smooth value convergence
constexpr float SMOOTH_VALUE_STEP_MULTIPLIER = 10.0f; // Controls interpolation speed

CUiEffects::CUiEffects()
{
	m_Time = 0.0f;
}

void CUiEffects::OnReset()
{
	m_vSmoothValues.clear();
	m_Time = 0.0f;
}

void CUiEffects::OnRender()
{
	if(!g_Config.m_ClHudAnimations)
		return;

	const float TimePassed = Client()->RenderFrameTime();
	m_Time += TimePassed;

	// Update all smooth values
	const float SpeedMultiplier = g_Config.m_ClHudAnimationSpeed / 100.0f;
	for(auto &Value : m_vSmoothValues)
	{
		if(std::abs(Value.m_Current - Value.m_Target) < SMOOTH_VALUE_EPSILON)
		{
			Value.m_Current = Value.m_Target;
			continue;
		}

		const float Distance = Value.m_Target - Value.m_Current;
		const float Step = Distance * Value.m_Speed * SpeedMultiplier * TimePassed * SMOOTH_VALUE_STEP_MULTIPLIER;
		Value.m_Current += Step;
	}
}

float CUiEffects::ApplyTransition(float t, ETransitionType Type)
{
	t = std::clamp(t, 0.0f, 1.0f);
	
	switch(Type)
	{
	case TRANSITION_LINEAR:
		return t;
	case TRANSITION_EASE_IN:
		return t * t;
	case TRANSITION_EASE_OUT:
		return t * (2.0f - t);
	case TRANSITION_EASE_IN_OUT:
		return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
	case TRANSITION_BOUNCE:
		{
			const float n1 = 7.5625f;
			const float d1 = 2.75f;
			if(t < 1.0f / d1)
				return n1 * t * t;
			else if(t < 2.0f / d1)
			{
				t -= 1.5f / d1;
				return n1 * t * t + 0.75f;
			}
			else if(t < 2.5f / d1)
			{
				t -= 2.25f / d1;
				return n1 * t * t + 0.9375f;
			}
			else
			{
				t -= 2.625f / d1;
				return n1 * t * t + 0.984375f;
			}
		}
	default:
		return t;
	}
}

int CUiEffects::CreateSmoothValue(float Initial, float Speed, ETransitionType Type)
{
	CSmoothValue Value;
	Value.m_Current = Initial;
	Value.m_Target = Initial;
	Value.m_Speed = Speed;
	Value.m_Type = Type;
	
	m_vSmoothValues.push_back(Value);
	return m_vSmoothValues.size() - 1;
}

void CUiEffects::SetSmoothValue(int Index, float Target)
{
	if(Index < 0 || Index >= (int)m_vSmoothValues.size())
		return;
	
	m_vSmoothValues[Index].m_Target = Target;
}

float CUiEffects::GetSmoothValue(int Index)
{
	if(Index < 0 || Index >= (int)m_vSmoothValues.size())
		return 0.0f;
	
	return m_vSmoothValues[Index].m_Current;
}

void CUiEffects::UpdateSmoothValue(int Index, float Speed)
{
	if(Index < 0 || Index >= (int)m_vSmoothValues.size())
		return;
	
	m_vSmoothValues[Index].m_Speed = Speed;
}

ColorRGBA CUiEffects::LerpColor(const ColorRGBA &From, const ColorRGBA &To, float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return ColorRGBA(
		From.r + (To.r - From.r) * t,
		From.g + (To.g - From.g) * t,
		From.b + (To.b - From.b) * t,
		From.a + (To.a - From.a) * t
	);
}

ColorRGBA CUiEffects::PulseColor(const ColorRGBA &Color, float Time, float Speed)
{
	const float Pulse = (std::sin(Time * Speed * PI) + 1.0f) * 0.5f;
	return ColorRGBA(
		Color.r * (0.5f + Pulse * 0.5f),
		Color.g * (0.5f + Pulse * 0.5f),
		Color.b * (0.5f + Pulse * 0.5f),
		Color.a
	);
}

float CUiEffects::GetPulse(float Speed)
{
	return (std::sin(m_Time * Speed * PI) + 1.0f) * 0.5f;
}

float CUiEffects::GetWave(float Speed, float Offset)
{
	return std::sin((m_Time * Speed + Offset) * PI);
}

float CUiEffects::GetBounce(float Speed)
{
	const float t = std::fmod(m_Time * Speed, 1.0f);
	return ApplyTransition(t, TRANSITION_BOUNCE);
}
