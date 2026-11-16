#ifndef GAME_CLIENT_QMUI_QMFINISHSUMMARYMOTION_H
#define GAME_CLIENT_QMUI_QMFINISHSUMMARYMOTION_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace qm_finish_summary
{
	struct SCardPose
	{
		CUIRect m_Rect{};
		float m_Radius = 0.0f;
		float m_Connection = 0.0f;
		float m_ContentAlpha = 0.0f;
	};

	inline SCardPose Layout(const CUIRect &Island, const CUIRect &Target, float Progress)
	{
		const float T = std::clamp(Progress, 0.0f, 1.0f);
		const float Grow = T * T * (3.0f - 2.0f * T);
		// 靠近岛的上缘先贴到宿主，下缘继续向上收拢。
		const float Travel = T * T * T;
		const float BudY = Island.y + Island.h * 0.75f;
		const float CenterX = mix(Island.x + Island.w * 0.5f, Target.x + Target.w * 0.5f, Grow);
		SCardPose Pose;
		const float Top = mix(BudY, Target.y, Travel);
		const float Bottom = mix(BudY, Target.y + Target.h, Grow);
		Pose.m_Rect = {CenterX - Target.w * Grow * 0.5f, Top, Target.w * Grow, Bottom - Top};
		Pose.m_Radius = Pose.m_Rect.h * mix(0.5f, 0.08f, Grow);
		// 芽尖离开主岛后，融合半径归零，独立表面从同一个轮廓接手。
		Pose.m_Connection = Island.h * 0.65f * std::max(1.0f - T / 0.65f, 0.0f);
		Pose.m_ContentAlpha = std::clamp((T - 0.35f) / 0.55f, 0.0f, 1.0f);
		return Pose;
	}

	class CCardMotion
	{
		uint64_t m_Id = 0;
		double m_LastTime = 0.0;
		double m_FadeAllowedAt = 0.0;
		float m_Alpha = 1.0f;
		vec2 m_Tilt{};
		float m_HostScale = 1.0f;

	public:
		void Update(uint64_t Id, double Now, bool Opening, float Progress, vec2 Velocity, int MotionLevel)
		{
			if(Id == 0 || m_Id != Id)
			{
				*this = {};
				m_Id = Id;
				m_FadeAllowedAt = Now + 5.0;
				m_LastTime = Now;
			}
			if(!Id)
				return;
			const float Dt = static_cast<float>(std::clamp(Now - m_LastTime, 0.0, 1.0));
			m_LastTime = Now;
			const bool Moving = dot(Velocity, Velocity) > 0.04f;
			const float Blend = -std::expm1(-Dt * (Moving ? 14.0f : 8.0f));
			const float FadeDt = std::min(Dt, static_cast<float>(std::max(0.0, Now - m_FadeAllowedAt)));
			m_Alpha = mix(m_Alpha, Moving && Opening ? 0.15f : 1.0f, -std::expm1(-FadeDt * (Moving && Opening ? 14.0f : 8.0f)));
			if(MotionLevel <= 0)
			{
				m_Tilt = {};
				m_HostScale = 1.0f;
				return;
			}
			const vec2 TargetTilt = Moving ? vec2(std::clamp(-Velocity.y * 0.008f, -0.12f, 0.12f),
								 std::clamp(Velocity.x * 0.012f, -0.16f, 0.16f)) :
							 vec2();
			m_Tilt = mix(m_Tilt, TargetTilt, Blend);
			const float Pulse = 4.0f * Progress * (1.0f - Progress);
			m_HostScale = mix(m_HostScale, 1.0f + (Opening ? -0.09f : 0.07f) * Pulse, -std::expm1(-20.0f * Dt));
		}

		float Alpha() const { return m_Alpha; }
		float TiltX() const { return m_Tilt.x; }
		float TiltY() const { return m_Tilt.y; }
		float HostScale() const { return m_HostScale; }
	};
}

#endif
