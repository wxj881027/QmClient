#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_FREEZE_BAR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_FREEZE_BAR_H

#include <algorithm>
#include <vector>

// 查看模式虚影的冻结进度条策略。
//
// 幽灵文件没有冻结起止字段：转换/录制侧把冻结帧的武器写成 WEAPON_NINJA 作为标记。
// 播放开始时按连续冻结帧还原成相对 tick 区间，播放头落在区间内就画一根与实机
// 相同语义的冻结条（剩余比例 = 剩余 tick / 总冻结 tick）。这里只放纯计算，
// 渲染由 CFreezeBars::RenderBarAt 负责。

struct SGhostFreezeSpan
{
	int m_StartTick = 0;
	int m_EndTick = 0;
};

// vRelativeTicks 与 vFrozen 逐帧给出相对于影子起始 tick 的帧位置和冻结标记
// （0 = 未冻结）。连续冻结帧合成一个区间；单帧冻结也保留（渲染侧判定
// 时长不足时不画）。
inline void QmCollectGhostFreezeSpans(const std::vector<int> &vRelativeTicks, const std::vector<unsigned char> &vFrozen, std::vector<SGhostFreezeSpan> &vOut)
{
	vOut.clear();
	const int Count = std::min((int)vRelativeTicks.size(), (int)vFrozen.size());
	bool InSpan = false;
	for(int Index = 0; Index < Count; Index++)
	{
		const bool Frozen = vFrozen[Index] != 0;
		if(Frozen && !InSpan)
		{
			SGhostFreezeSpan Span;
			Span.m_StartTick = vRelativeTicks[Index];
			Span.m_EndTick = vRelativeTicks[Index];
			vOut.push_back(Span);
			InSpan = true;
		}
		else if(Frozen)
		{
			vOut.back().m_EndTick = vRelativeTicks[Index];
		}
		else
		{
			InSpan = false;
		}
	}
}

// 播放头在冻结区间内返回剩余比例（1 → 0）；不在冻结段、或区间没有可信时长
// （单帧冻结）时返回 -1，调用方据此跳过绘制。
inline float QmGhostFreezeProgress(const std::vector<SGhostFreezeSpan> &vSpans, const int Tick)
{
	for(const SGhostFreezeSpan &Span : vSpans)
	{
		if(Tick < Span.m_StartTick || Tick > Span.m_EndTick)
			continue;
		if(Span.m_EndTick <= Span.m_StartTick)
			return -1.0f;
		const float Progress = (float)(Span.m_EndTick - Tick) / (float)(Span.m_EndTick - Span.m_StartTick);
		return std::clamp(Progress, 0.0f, 1.0f);
	}
	return -1.0f;
}

#endif
