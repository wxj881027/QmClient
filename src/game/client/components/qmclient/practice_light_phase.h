#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_LIGHT_PHASE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_LIGHT_PHASE_H

#include <base/math.h>

// 静态激光的旋转与伸缩相位只依赖地图初始值和已经经过的步数。
// 把这段纯计算独立出来，练习分支的逐 tick 推进和影子回放的任意跳转
// 才能共用同一套结果，也能直接被单元测试与基准测量。
namespace qm_practice_light
{
	struct SPhase
	{
		float m_Rotation = 0.0f;
		int m_CurveLength = 0;
		int m_LengthL = 0;
		int m_Speed = 0;
	};

	inline void Step(SPhase &Phase, int Length, float AngularSpeed, int TickInterval)
	{
		if(Phase.m_Speed != 0)
		{
			if((Phase.m_CurveLength >= Length && Phase.m_Speed > 0) || (Phase.m_CurveLength <= 0 && Phase.m_Speed < 0))
				Phase.m_Speed = -Phase.m_Speed;
			Phase.m_CurveLength += Phase.m_Speed * TickInterval + Phase.m_LengthL;
			Phase.m_LengthL = 0;
			if(Phase.m_CurveLength > Length)
			{
				Phase.m_LengthL = Phase.m_CurveLength - Length;
				Phase.m_CurveLength = Length;
			}
			else if(Phase.m_CurveLength < 0)
			{
				Phase.m_LengthL = Phase.m_CurveLength;
				Phase.m_CurveLength = 0;
			}
		}

		Phase.m_Rotation += AngularSpeed * TickInterval;
		if(Phase.m_Rotation > 2.0f * pi)
			Phase.m_Rotation -= 2.0f * pi;
		else if(Phase.m_Rotation < 0.0f)
			Phase.m_Rotation += 2.0f * pi;
	}

	// 从头重放 TargetTick 内的所有步。跳转后退时调用方也走这里，
	// 结果与一直向前推进相同，不依赖任何隐藏累积状态。
	inline SPhase After(float StartRotation, int StartCurveLength, int StartSpeed, int Length, float AngularSpeed,
		int TickInterval, int TargetTick)
	{
		SPhase Phase{StartRotation, StartCurveLength, 0, StartSpeed};
		if(TickInterval <= 0)
			return Phase;
		for(int Tick = TickInterval; Tick <= TargetTick; Tick += TickInterval)
			Step(Phase, Length, AngularSpeed, TickInterval);
		return Phase;
	}

	// 最后一次实际推进的 tick，用于激光脉冲动画的时间基准。
	inline int LastStepTick(int TickInterval, int TargetTick)
	{
		if(TickInterval <= 0 || TargetTick < TickInterval)
			return 0;
		return TargetTick / TickInterval * TickInterval;
	}
}

#endif
