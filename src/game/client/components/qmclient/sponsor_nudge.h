// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SPONSOR_NUDGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SPONSOR_NUDGE_H

#include <base/math.h>

#include <engine/shared/config.h>

#include <algorithm>

// 启动赞助提醒：按「阈值推进」触发，而不是对启动次数取模。
// 取模的语义是「每 N 次启动提示一次」，但玩家连续开关客户端会在同一天里
// 反复吃提示；阈值推进保证跨过一个阈值只提示一次，玩家不看就不会被连着念。
namespace qm_sponsor_nudge
{
	// 提示间隔（次启动）。
	inline constexpr int STEP = 7;
	// 首次提示前的随机延迟上限（次启动）。
	// 固定的第 14 次启动会让人感觉被算计，加 0~7 的抖动后首次提示落在 14~21 之间。
	inline constexpr int FIRST_MAX_EXTRA = 7;

	// 返回本次启动要展示的启动序号；0 表示本次启动不提示。
	// FirstExtraOffset 由调用方传入随机值 [0, FIRST_MAX_EXTRA]，
	// 只有落在初始阈值 14 上时才用于首次抖动，之后间隔固定推进。
	inline int OnLaunch(int *pLaunchCount, int *pNudgeAt, int FirstExtraOffset)
	{
		if(!g_Config.m_QmSponsorNudge)
			return 0;

		const int LaunchCount = ++(*pLaunchCount);
		if(LaunchCount < *pNudgeAt)
			return 0;

		// 跨过阈值：语义上只补提示一次，即使由于手改配置导致落后多个阈值也不追债。
		const int NudgeIndex = LaunchCount;
		if(*pNudgeAt == g_Config.m_QmSponsorNudgeAt)
			*pNudgeAt += std::clamp(FirstExtraOffset, 0, FIRST_MAX_EXTRA);
		do
		{
			*pNudgeAt += STEP;
		} while(*pNudgeAt <= LaunchCount);
		return NudgeIndex;
	}

	// 首次抖动使用的随机值。
	inline int RandomFirstExtraOffset()
	{
		return (int)random_float(0.0f, (float)FIRST_MAX_EXTRA + 0.999f);
	}
} // namespace qm_sponsor_nudge

#endif
