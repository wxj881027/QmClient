#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_FREEZE_BAR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_FREEZE_BAR_H

#include <algorithm>
#include <vector>

// 查看模式虚影的冻结进度条策略。
//
// 幽灵文件没有冻结起止字段。CGhostCharacter::m_VelY 在所有转换/录制路径里都被
// 清零且不参与回放插值，因此用作显式冻结标记：高 16 位固定为 0x514D（"QM"），
// 低 16 位是剩余冻结 tick（0 = 明确未冻结）。旧版文件没有标记位，仍回退到
// “武器为 WEAPON_NINJA 即冻结”的历史判定；该回退无法区分旧文件里的真实忍者帧，
// 但新文件不会再落入这条路径。播放开始时按连续冻结帧还原成相对 tick 区间，
// 播放头落在区间内就画一根与实机相同语义的冻结条（剩余比例 = 剩余 tick /
// 总冻结 tick）。这里只放纯计算，渲染由 CFreezeBars::RenderBarAt 负责。

inline constexpr int QM_GHOST_FREEZE_MARKER_MASK = 0xFFFF0000;
inline constexpr int QM_GHOST_FREEZE_MARKER = 0x514D0000;

// FreezeTicks <= 0 表示明确未冻结；正值写入剩余 tick（截到 16 位）。
inline int QmEncodeGhostFreezeMarker(int FreezeTicks)
{
	if(FreezeTicks <= 0)
		return QM_GHOST_FREEZE_MARKER;
	return QM_GHOST_FREEZE_MARKER | std::min(FreezeTicks, 0xFFFF);
}

// -1 = 旧文件帧（无显式标记），0 = 明确未冻结，>0 = 冻结且剩余 tick。
inline int QmDecodeGhostFreezeMarker(int VelY)
{
	if((VelY & QM_GHOST_FREEZE_MARKER_MASK) != QM_GHOST_FREEZE_MARKER)
		return -1;
	return VelY & 0xFFFF;
}

// 由 DDNetCharacter 的冻结结束 tick 换算标记值：< 0 表示深度冻结（没有结束
// tick），0 表示未冻结；只有结束 tick 晚于当前 tick 才计入剩余时长。
inline int QmGhostFreezeTicksFromEnd(int FreezeEnd, int Tick)
{
	if(FreezeEnd < 0)
		return 1;
	if(FreezeEnd > Tick)
		return FreezeEnd - Tick;
	return 0;
}

// 冻结判定：显式标记优先，旧文件才回退到武器标记，避免真实忍者帧被误判。
inline bool QmGhostFrameFrozen(int VelY, bool LegacyNinjaMarker)
{
	const int FreezeTicks = QmDecodeGhostFreezeMarker(VelY);
	if(FreezeTicks >= 0)
		return FreezeTicks > 0;
	return LegacyNinjaMarker;
}

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
