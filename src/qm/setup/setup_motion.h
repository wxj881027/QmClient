#ifndef QM_SETUP_SETUP_MOTION_H
#define QM_SETUP_SETUP_MOTION_H

#include <cmath>

namespace QmSetup
{
	// 临界阻尼弹簧使用实际经过时间；中途改目标时保留位置与速度。
	class CTransition
	{
		float m_Value;
		float m_Target;
		float m_Velocity = 0.0f;
		float m_Duration = 0.24f;

	public:
		explicit CTransition(float Value = 0.0f) :
			m_Value(Value), m_Target(Value) {}

		float Value() const { return m_Value; }
		bool Active() const { return m_Value != m_Target || m_Velocity != 0.0f; }
		void Snap(float Value)
		{
			m_Value = m_Target = Value;
			m_Velocity = 0.0f;
		}
		void Finish() { Snap(m_Target); }
		void SetTarget(float Target, bool Animate, float Duration = 0.24f)
		{
			if(!Animate || Duration <= 0.0f)
			{
				Snap(Target);
				return;
			}
			if(Target == m_Target)
				return;
			m_Target = Target;
			m_Duration = Duration;
		}
		void Advance(float Seconds)
		{
			if(!Active() || Seconds <= 0.0f)
				return;
			const float Omega = 6.0f / m_Duration;
			const float Distance = m_Value - m_Target;
			const float Coefficient = m_Velocity + Omega * Distance;
			const float Decay = std::exp(-Omega * Seconds);
			m_Value = m_Target + (Distance + Coefficient * Seconds) * Decay;
			m_Velocity = (m_Velocity - Omega * Coefficient * Seconds) * Decay;
			if(std::abs(m_Value - m_Target) < 0.0005f && std::abs(m_Velocity) < 0.005f)
				Finish();
		}
	};
}

#endif // QM_SETUP_SETUP_MOTION_H
