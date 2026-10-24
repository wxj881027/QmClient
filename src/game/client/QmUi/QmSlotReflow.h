/* 请抬头享受阳光｜日子很好 我很我---------致咩子 */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMSLOTREFLOW_H
#define GAME_CLIENT_QMUI_QMSLOTREFLOW_H

#include <base/math.h>
#include <base/vmath.h>

#include <string>
#include <unordered_map>

// 卡片换位的「槽位跟随」。
//
// 列表插入/删除后，每个条目的槽位序号会变。把序号差换算成像素偏移，
// 先让卡片停在原来的位置，再用欠阻尼弹簧滑到新槽位（带一点回弹）。
// 只依赖槽位序号，不依赖上一帧的屏幕矩形——切页、换列、筛选变化都不会
// 把卡片算到裁剪区外。
class CQmSlotReflow
{
public:
	// 从 PrevSlot 换到 Slot 时，卡片要停在旧位置的偏移量（像素）。
	static vec2 SlotDeltaOffset(int PrevSlot, int Slot, int Columns, float StepX, float StepY);

	void BeginFrame(float DtSec, int Columns, bool Snap);
	// 返回本帧要叠加到布局矩形上的像素偏移。
	vec2 Track(const char *pKey, int Slot, float StepX, float StepY);
	void Reset();

private:
	struct SState
	{
		bool m_Seen = false;
		int m_Slot = 0;
		int m_LastFrame = 0;
		float m_OffsetX = 0.0f;
		float m_OffsetY = 0.0f;
		float m_VelocityX = 0.0f;
		float m_VelocityY = 0.0f;
	};

	void PruneIfNeeded();

	std::unordered_map<std::string, SState> m_States;
	int m_Columns = 0;
	int m_Frame = 0;
	float m_Dt = 0.0f;
	bool m_Snap = true;
};

#endif // GAME_CLIENT_QMUI_QMSLOTREFLOW_H
