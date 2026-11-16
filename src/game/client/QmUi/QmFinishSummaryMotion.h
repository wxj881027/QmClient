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
		CUIRect m_NeckRect{};
		float m_Radius = 0.0f;
		float m_Connection = 0.0f;
		float m_ContentAlpha = 0.0f;
		float m_Pinch = 0.0f;
		float m_TopShade = 0.0f;
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

	// 近端先抵达岛口，远端随后加速穿过同一位置，保留窗口被吸入的轮廓。
	inline SCardPose ClosingLayout(const CUIRect &Island, const CUIRect &Target, float Start, float Progress)
	{
		const bool Attached = Start <= 0.65f;
		SCardPose Pose = Layout(Island, Target, Attached ? Progress : Start);
		if(Start <= 0.0f)
			return Pose;
		const float T = std::clamp(1.0f - Progress / Start, 0.0f, 1.0f);
		const float Neck = std::min(T / 0.42f, 1.0f);
		const float NeckEase = Neck * Neck * (3.0f - 2.0f * Neck);
		Pose.m_TopShade = 0.96f * NeckEase;
		if(Attached)
			return Pose;

		const CUIRect From = Pose.m_Rect;
		const float Pull = T * T * (2.0f - T);
		const float Overlap = Island.h * 0.12f;
		const float MouthY = Island.y + Island.h - Overlap;
		const float Top = mix(From.y, MouthY, NeckEase);
		const float Bottom = mix(From.y + From.h, MouthY, Pull);
		const float Width = From.w * (1.0f - Pull);
		const float HostCenter = Island.x + Island.w * 0.5f;
		const float Center = mix(From.x + From.w * 0.5f, HostCenter, NeckEase);
		Pose.m_Rect = {Center - Width * 0.5f, Top, Width, std::max(0.0f, Bottom - Top)};
		Pose.m_Radius *= (1.0f - Pull);
		constexpr float MaxPinch = 0.82f;
		Pose.m_Pinch = MaxPinch * NeckEase;

		// 岛底与卡片上缘各压住一小段接颈；卡片靠屏幕边缘时只取两者重合的宽度。
		const float FlatHalf = std::max(0.0f, Width * 0.5f - Pose.m_Radius) * (1.0f - Pose.m_Pinch);
		const float SocketHalf = std::min(Island.w * 0.25f, From.w * (1.0f - MaxPinch) * 0.5f);
		const float Left = std::max(Center - FlatHalf, HostCenter - SocketHalf);
		const float Right = std::min(Center + FlatHalf, HostCenter + SocketHalf);
		const float NeckWidth = std::max(0.0f, Right - Left) * NeckEase;
		const float NeckBottom = Top + std::min(Overlap, Pose.m_Rect.h * 0.5f);
		Pose.m_NeckRect = {(Left + Right - NeckWidth) * 0.5f, MouthY, NeckWidth, std::max(0.0f, NeckBottom - MouthY)};
		return Pose;
	}

	class CCardMotion
	{
		uint64_t m_Id = 0;
		double m_LastTime = 0.0;
		vec2 m_Tilt{};
		float m_HostScale = 1.0f;

	public:
		void Update(uint64_t Id, double Now, bool Opening, float Progress, vec2 Velocity, int MotionLevel)
		{
			if(Id == 0 || m_Id != Id)
			{
				*this = {};
				m_Id = Id;
				m_LastTime = Now;
			}
			if(!Id)
				return;
			const float Dt = static_cast<float>(std::clamp(Now - m_LastTime, 0.0, 1.0));
			m_LastTime = Now;
			const bool Moving = dot(Velocity, Velocity) > 0.04f;
			const float Blend = -std::expm1(-Dt * (Moving ? 14.0f : 8.0f));
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

		float TiltX() const { return m_Tilt.x; }
		float TiltY() const { return m_Tilt.y; }
		float HostScale() const { return m_HostScale; }
	};
}

#endif
