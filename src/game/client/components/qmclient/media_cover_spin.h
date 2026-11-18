#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MEDIA_COVER_SPIN_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MEDIA_COVER_SPIN_H

#include <base/math.h>

#include <algorithm>
#include <cmath>

// 圆形封面的「唱片自转」：播放时匀速转，暂停就定格在当前角度（不回正、不跳变），
// 动效等级 0（关闭动画）不转。HUD 媒体岛与计分板媒体条共用这一份状态与速率，
// 两处封面才会以同一个转速转，不会一个快一个慢。
inline constexpr float QM_MEDIA_COVER_SPIN_PERIOD_SECONDS = 6.0f;

struct SQmMediaCoverSpin
{
	// 角度用弧度累计并取模，长时间播放也不会丢精度。
	float m_Angle = 0.0f;
	// 时间以秒为单位传入（调用方用 time_get()/time_freq() 折算）：测试不需要
	// 依赖平台计时频率，两条渲染路径也不必各自重复「tick 差值 / 频率」的换算。
	float m_LastTime = 0.0f;
	// 首帧没有「上一帧」，单独用标志而不是拿时间 0 当哨兵。
	bool m_HasLastTime = false;

	void Reset()
	{
		m_Angle = 0.0f;
		m_LastTime = 0.0f;
		m_HasLastTime = false;
	}

	// 单帧增量上限 0.1s：界面隐藏/断点后第一帧不要把停住的那段时间一次性补上
	// （那会表现为封面突然跳几十度）。
	float Advance(bool Playing, bool MotionEnabled, float NowSeconds)
	{
		float Delta = 0.0f;
		if(m_HasLastTime && NowSeconds > m_LastTime)
			Delta = std::min(NowSeconds - m_LastTime, 0.1f);
		m_LastTime = NowSeconds;
		m_HasLastTime = true;
		if(Playing && MotionEnabled)
		{
			constexpr float FullTurn = 2.0f * pi;
			m_Angle = std::fmod(m_Angle + Delta * (FullTurn / QM_MEDIA_COVER_SPIN_PERIOD_SECONDS), FullTurn);
			if(m_Angle < 0.0f)
				m_Angle += FullTurn;
		}
		return m_Angle;
	}
};

#endif
