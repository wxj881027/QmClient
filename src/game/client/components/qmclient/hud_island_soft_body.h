#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_ISLAND_SOFT_BODY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_ISLAND_SOFT_BODY_H

#include <base/vmath.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

// 外壳独立响应已有动画，布局和文字不参与二次形变。
class CHudIslandSoftBody
{
	vec2 m_PreviousSize{};
	vec2 m_Displacement{};
	vec2 m_Velocity{};
	float m_RippleStrength = 0.0f;
	float m_RipplePhase = 0.0f;
	bool m_Initialized = false;

public:
	struct SPose
	{
		float m_Stretch = 0.0f;
		float m_Pull = 0.0f;
		float m_RippleStrength = 0.0f;
		float m_RipplePhase = 0.0f;

		CUIRect ShellRect(const CUIRect &Rect) const
		{
			const float Unit = std::min(Rect.w, Rect.h);
			const float WidthChange = Unit * 0.06f * m_Stretch;
			const float HeightChange = -Unit * 0.025f * m_Stretch;
			// 保持顶部锚点，避免顶部贴屏时向屏幕外挤压。
			return {Rect.x - WidthChange * 0.5f + Unit * 0.035f * m_Pull,
				Rect.y, Rect.w + WidthChange, Rect.h + HeightChange};
		}

		float RippleAmplitude(const CUIRect &Rect, float ScreenPixelSize) const
		{
			return std::min(std::min(Rect.w, Rect.h) * 0.012f, ScreenPixelSize * 0.7f) * m_RippleStrength;
		}
	};

	void Reset() { *this = {}; }

	SPose Pose() const
	{
		return {std::clamp(m_Displacement.x, -1.0f, 1.0f), std::clamp(m_Displacement.y, -1.0f, 1.0f), m_RippleStrength, m_RipplePhase};
	}

	SPose Update(float DeltaSeconds, vec2 Size, float LeftVelocity, float RightVelocity, int MotionLevel)
	{
		// 隐藏后的长间隔、时钟回退和关闭动效都建立新基线，不重播旧反馈。
		if(!m_Initialized || MotionLevel <= 0 || DeltaSeconds < 0.0f || DeltaSeconds > 0.25f || Size.x <= 0.0f || Size.y <= 0.0f)
		{
			Reset();
			m_PreviousSize = Size;
			m_Initialized = true;
			return Pose();
		}
		if(MotionLevel < 2)
		{
			m_RippleStrength = 0.0f;
			m_RipplePhase = 0.0f;
		}
		// 同一时刻重复绘制不能消耗尺寸变化，也不能推进外壳。
		if(DeltaSeconds == 0.0f)
			return Pose();

		const float Unit = std::max(1.0f, Size.y);
		const vec2 SizeVelocity = (Size - m_PreviousSize) / (DeltaSeconds * Unit);
		m_PreviousSize = Size;
		const float Left = std::clamp(LeftVelocity / 4.6f, -1.0f, 1.0f);
		const float Right = std::clamp(RightVelocity / 4.6f, -1.0f, 1.0f);
		const float Strength = MotionLevel >= 2 ? 1.0f : 0.25f;
		const vec2 Target(
			std::clamp(SizeVelocity.x * 0.10f - SizeVelocity.y * 0.16f + (Left + Right) * 0.35f, -1.0f, 1.0f) * Strength,
			std::clamp(Right - Left, -1.0f, 1.0f) * Strength);

		// 二阶欠阻尼解析积分；反向时保留当前位移与速度。
		constexpr float Frequency = 18.0f;
		constexpr float DecayRate = Frequency * 0.55f;
		const float DampedFrequency = std::sqrt(Frequency * Frequency - DecayRate * DecayRate);
		const float Decay = std::exp(-DecayRate * DeltaSeconds);
		const float CosPhase = std::cos(DampedFrequency * DeltaSeconds);
		const float SinPhase = std::sin(DampedFrequency * DeltaSeconds);
		const vec2 Offset = m_Displacement - Target;
		const vec2 Velocity = m_Velocity;
		m_Displacement = Target + (Offset * CosPhase + (Velocity + Offset * DecayRate) / DampedFrequency * SinPhase) * Decay;
		m_Velocity = (Velocity * CosPhase - (Velocity * DecayRate + Offset * (Frequency * Frequency)) / DampedFrequency * SinPhase) * Decay;
		if(length(Target) == 0.0f && length(m_Displacement) < 0.0001f && length(m_Velocity) < 0.001f)
		{
			m_Displacement = {};
			m_Velocity = {};
		}

		if(MotionLevel >= 2)
		{
			const float Excitation = std::clamp(std::abs(SizeVelocity.x) * 0.08f + std::abs(SizeVelocity.y) * 0.12f + (std::abs(Left) + std::abs(Right)) * 0.5f, 0.0f, 1.0f);
			const float Rate = Excitation > m_RippleStrength ? 18.0f : 7.0f;
			m_RippleStrength = Excitation + (m_RippleStrength - Excitation) * std::exp(-Rate * DeltaSeconds);
			if(m_RippleStrength < 0.0001f && Excitation == 0.0f)
			{
				m_RippleStrength = 0.0f;
				m_RipplePhase = 0.0f;
			}
			else
				m_RipplePhase = std::fmod(m_RipplePhase + DeltaSeconds * 12.0f, 6.28318530718f);
		}
		return Pose();
	}
};

#endif
