#ifndef GAME_CLIENT_QMUI_SETTINGSCARDMOTION_H
#define GAME_CLIENT_QMUI_SETTINGSCARDMOTION_H

#include <base/system.h>

namespace qm_settings_motion
{
	// 设置卡片此刻是否还在播展开/折叠、入场或列重排动画。
	// 动画期间的帧本来就要重新声明行、重新测量并新建文本缓存，外部的一次性重活
	// （语音运行时冷启动、SDL 设备枚举、工作线程启动）不要挤进同几帧。
	// 状态由 SettingsCardDeck 每帧发布；页面不再渲染时发布停止，因此读取侧按时间戳
	// 判断有效窗口，避免「动画中途离页」把标记永久挂住。
	inline constexpr int64_t SETTINGS_CARD_MOTION_STALE_MS = 300;

	inline bool &PublishedAnimationActive()
	{
		static bool s_AnimationActive = false;
		return s_AnimationActive;
	}

	inline int64_t &LastPublishTime()
	{
		static int64_t s_LastPublishTime = 0;
		return s_LastPublishTime;
	}

	inline void PublishCardAnimationActive(const bool Active)
	{
		PublishedAnimationActive() = Active;
		LastPublishTime() = time_get();
	}

	inline bool IsCardAnimationActive()
	{
		if(!PublishedAnimationActive())
			return false;
		return time_get() - LastPublishTime() < (int64_t)time_freq() * SETTINGS_CARD_MOTION_STALE_MS / 1000;
	}
} // namespace qm_settings_motion

#endif
