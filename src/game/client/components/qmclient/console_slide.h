#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SLIDE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SLIDE_H

#include <game/client/QmUi/QmAnimationBackend.h>

#include <algorithm>
#include <cmath>

// 控制台下拉/收回的高度比例轨道策略。
//
// 面板下沿会扫过聊天区和世界画面，所以高度比例必须始终落在 [0, 1] 且保持单调：
// 越过 1 会把刚露出的聊天文字再盖住一次，越过 0 会在完全收起后反向闪一帧。
// 手感档位只改变响应周期，不改变阻尼比；所有档位都使用临界阻尼，零过冲。
struct SQmConsoleSlideSpringSpec
{
	float m_Response = 0.28f;
	float m_DampingRatio = 1.0f;
};

inline SQmConsoleSlideSpringSpec QmConsoleSlideSpringSpec(bool Opening, int Feel)
{
	SQmConsoleSlideSpringSpec Spec;
	switch(std::clamp(Feel, 0, 2))
	{
	case 0: // 柔和
		Spec.m_Response = 0.32f;
		break;
	case 2: // 弹性：更短的响应周期，仍然零过冲
		Spec.m_Response = 0.26f;
		break;
	default: // 标准
		break;
	}
	if(!Opening)
	{
		// 收回比展开利落；临界阻尼保证不会缩到 0 以下再弹回来。
		Spec.m_Response *= 0.78f;
	}
	return Spec;
}

inline SUiSpringConfig QmConsoleSlideSpring(bool Opening, int Feel)
{
	const SQmConsoleSlideSpringSpec Spec = QmConsoleSlideSpringSpec(Opening, Feel);
	SUiSpringConfig Spring = SUiSpringConfig::FromResponse(Spec.m_Response, Spec.m_DampingRatio);
	Spring.m_RestEpsilon = 0.002f;
	Spring.m_RestVelocity = 0.02f;
	return Spring;
}

// 弹簧值就是高度比例，只允许 [0, 1]。非有限值按“完全收起”处理，避免坏值
// 把整块面板铺满屏幕并盖住聊天。
inline float QmConsoleSlideHeightScale(float Value)
{
	if(!std::isfinite(Value))
		return 0.0f;
	return std::clamp(Value, 0.0f, 1.0f);
}

#endif
