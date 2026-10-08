#ifndef GAME_CLIENT_QMUI_QMINPUTMOTION_H
#define GAME_CLIENT_QMUI_QMINPUTMOTION_H

#include "QmAnimationBackend.h"

namespace qm_input_motion
{
	// 快速响应后只保留一轮明显回弹；文字透明度不使用这些弹簧。
	inline constexpr SUiSpringConfig FOLLOW = {1.0f, 760.0f, 36.0f, 0.02f, 0.20f};
	inline constexpr SUiSpringConfig RESIZE = {1.0f, 640.0f, 32.0f, 0.02f, 0.20f};
	inline constexpr SUiSpringConfig SELECTED = {1.0f, 1000.0f, 38.0f, 0.01f, 0.15f};
	inline constexpr SUiSpringConfig CARET = {1.0f, 1100.0f, 44.0f, 0.015f, 0.15f};
	inline constexpr SUiSpringConfig GLYPH = {1.0f, 900.0f, 34.0f, 0.003f, 0.03f};

	inline float ResolvePresentationValue(CQmAnimationBackend &Runtime, uint64_t NodeKey, EUiAnimProperty Property,
		float Target, const SUiSpringConfig &Spring, int MotionLevel, int Priority)
	{
		if(qm_animation::NormalizeMotionLevel(MotionLevel) == 0)
		{
			// 关闭动效也结束正在回弹的轨道，不能只等待下次目标变化。
			Runtime.SetValue(NodeKey, Property, Target);
			return Target;
		}
		SUiAnimTransition Transition;
		Transition.m_Driver = EUiAnimDriver::SPRING;
		Transition.m_Spring = Spring;
		Transition.m_Priority = Priority;
		Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
		Transition.m_RespectMotionLevel = false;
		Transition = qm_animation::ApplyMotionLevel(Transition, MotionLevel);
		return Runtime.ResolveTargetValue(NodeKey, Property, Target, Transition);
	}
}

#endif
