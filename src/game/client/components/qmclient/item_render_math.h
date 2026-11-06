#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_ITEM_RENDER_MATH_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_ITEM_RENDER_MATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

inline float QmItemTickDelta(int Tick, int StartTick, int Offset = 0) noexcept
{
	return static_cast<float>(static_cast<int64_t>(Tick) + Offset - static_cast<int64_t>(StartTick));
}

inline float QmLaserFadeProgress(float Ticks, int TickSpeed, float Delay) noexcept
{
	// 零延迟的线段立即淡出，头部动画由原绘制路径单独处理。
	if(Delay == 0.0f)
		return 1.0f;
	const float Ms = Ticks * 1000.0f / TickSpeed;
	return std::clamp(Ms / Delay, 0.0f, 1.0f);
}

struct SQmLaserSplatFrame
{
	int m_Index;
	float m_Rotation;
};

inline SQmLaserSplatFrame QmLaserSplatFrame(float Tick) noexcept
{
	if(!std::isfinite(Tick))
		return {0, 0.0f};
	const float Rotation = std::trunc(Tick);
	int Index = static_cast<int>(std::fmod(Rotation, 3.0f));
	if(Index < 0)
		Index += 3;
	return {Index, Rotation};
}

struct SQmItemTrailRange
{
	int64_t m_First = 0;
	int64_t m_End = 0;
};

inline SQmItemTrailRange QmItemTrailRange(float From, float To, float Step) noexcept
{
	if(!std::isfinite(From) || !std::isfinite(To) || !std::isfinite(Step) || Step <= 0.0f)
		return {};
	// 保留原浮点商的取整和采样间距，仅拓宽计数，转换前检查64位边界。
	const float First = From / Step;
	const float End = To / Step;
	constexpr double LIMIT = 9223372036854775808.0; // 2^63，不能用舍入后的INT64_MAX作含等号上界。
	if(!std::isfinite(First) || !std::isfinite(End) ||
		static_cast<double>(First) < -LIMIT || static_cast<double>(First) >= LIMIT ||
		static_cast<double>(End) < -LIMIT || static_cast<double>(End) >= LIMIT)
		return {};
	const int64_t Begin = static_cast<int64_t>(First);
	if(Begin == std::numeric_limits<int64_t>::max())
		return {};
	return {Begin + 1, static_cast<int64_t>(End)};
}

template<typename FEmit>
void QmForEachItemTrailSample(SQmItemTrailRange Range, FEmit &&Emit)
{
	for(int64_t Index = Range.m_First; Index < Range.m_End; ++Index)
	{
		// 首个拒收即结束本次重建，避免继续为拒收的烟迹计算轨迹和随机参数。
		if(!Emit(Index))
			return;
	}
}

#endif
