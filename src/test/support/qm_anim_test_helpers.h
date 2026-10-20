#ifndef TEST_SUPPORT_QM_ANIM_TEST_HELPERS_H
#define TEST_SUPPORT_QM_ANIM_TEST_HELPERS_H

#pragma once

#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>

// 临时把动效等级提到 Full，作用域结束恢复基座值（CRealUiFixture 默认为 0）。
// 用于显式测动画语义的用例：真实客户端由 qm_ui_motion_level 全局裁决，
// 测试想观察动画就必须显式提升，与生产门控语义一致。
class CScopedQmFullMotion
{
	int m_Saved;

public:
	CScopedQmFullMotion() :
		m_Saved(g_Config.m_QmUiMotionLevel)
	{
		g_Config.m_QmUiMotionLevel = 2;
	}
	~CScopedQmFullMotion() { g_Config.m_QmUiMotionLevel = m_Saved; }
	CScopedQmFullMotion(const CScopedQmFullMotion &) = delete;
	CScopedQmFullMotion &operator=(const CScopedQmFullMotion &) = delete;
};

inline void AdvanceQmAnimFor(CUiV2AnimationRuntime &Runtime, float Seconds)
{
	g_Config.m_QmUiMotionLevel = 2;
	const float Dt = 1.0f / 60.0f;
	const int Steps = static_cast<int>(Seconds / Dt) + 1;
	for(int i = 0; i < Steps; ++i)
		Runtime.Advance(Dt);
}

inline SUiAnimRequest MakeQmAnimRequest(uint64_t NodeKey, EUiAnimProperty Property, float Target, float DurationSec, int Priority, EUiAnimInterruptPolicy Interrupt, uint32_t TrackId)
{
	g_Config.m_QmUiMotionLevel = 2;
	SUiAnimRequest Request;
	Request.m_NodeKey = NodeKey;
	Request.m_Property = Property;
	Request.m_Target = Target;
	Request.m_Transition.m_DurationSec = DurationSec;
	Request.m_Transition.m_Priority = Priority;
	Request.m_Transition.m_Interrupt = Interrupt;
	Request.m_Transition.m_Easing = EEasing::LINEAR;
	Request.m_TrackId = TrackId;
	return Request;
}

#endif
