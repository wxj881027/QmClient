// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef ENGINE_SHARED_QM_BACKGROUND_REFRESH_H
#define ENGINE_SHARED_QM_BACKGROUND_REFRESH_H

#include <algorithm>

// 后台（窗口失焦）主循环节拍。跟随显示器与自定义值分开存：
// 自定义模式沿用 DDNet 的 cl_refresh_rate_inactive（0 = 不限）；
// 跟随显示器每帧读 gfx_screen_refresh_rate，切换显示器或刷新率后无需改配置。
enum class EQmBackgroundRefreshMode
{
	FOLLOW_DISPLAY = 0,
	CUSTOM = 1,
	UNLIMITED = 2,
};

inline constexpr int QM_BACKGROUND_REFRESH_MIN_RATE = 10;
inline constexpr int QM_BACKGROUND_REFRESH_MAX_RATE = 1000;
// 显示器刷新率缺失（0）或超出合理范围时，退回 DDNet 原先的 120 Hz 后台默认值。
inline constexpr int QM_BACKGROUND_REFRESH_FALLBACK_RATE = 120;

inline EQmBackgroundRefreshMode QmBackgroundRefreshModeFromConfig(int Value)
{
	if(Value == static_cast<int>(EQmBackgroundRefreshMode::CUSTOM))
		return EQmBackgroundRefreshMode::CUSTOM;
	if(Value == static_cast<int>(EQmBackgroundRefreshMode::UNLIMITED))
		return EQmBackgroundRefreshMode::UNLIMITED;
	return EQmBackgroundRefreshMode::FOLLOW_DISPLAY;
}

// 返回后台节拍（Hz），0 表示不限。
// 后台只允许降低节拍：前台 cl_refresh_rate 更小时沿用前台值，避免失焦后反而跑得更快。
inline int QmResolveBackgroundRefreshRate(EQmBackgroundRefreshMode Mode, int CustomRate, int DisplayRate, int ActiveRate)
{
	int Rate;
	switch(Mode)
	{
	case EQmBackgroundRefreshMode::UNLIMITED:
		Rate = 0;
		break;
	case EQmBackgroundRefreshMode::CUSTOM:
		Rate = std::clamp(CustomRate, 0, 10000);
		break;
	case EQmBackgroundRefreshMode::FOLLOW_DISPLAY:
	default:
		Rate = DisplayRate >= QM_BACKGROUND_REFRESH_MIN_RATE && DisplayRate <= QM_BACKGROUND_REFRESH_MAX_RATE ? DisplayRate : QM_BACKGROUND_REFRESH_FALLBACK_RATE;
		break;
	}
	if(Rate > 0 && ActiveRate > 0)
		Rate = std::min(Rate, ActiveRate);
	return Rate;
}

#endif // ENGINE_SHARED_QM_BACKGROUND_REFRESH_H
