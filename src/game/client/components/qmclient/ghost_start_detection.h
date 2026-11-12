#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_START_DETECTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_START_DETECTION_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <cstdint>

namespace qm_ghost_start
{
	struct SSample
	{
		int m_CharacterTick;
		int m_SnapshotTick;
		vec2 m_Pos;
	};

	// 与角色快照的三秒推演窗口一致，禁止跨长时间断档逐 tick 扫描。
	inline constexpr int MAX_SCAN_TICKS = 3 * SERVER_TICK_SPEED;

	template<typename F>
	int FindStartTick(const SSample &Prev, const SSample &Cur, int LastDeathTick, bool AllowRestart, F &&IsStart)
	{
		// 角色 tick 为 0 表示直接使用当前状态，其时间归属对应快照，并非服务器第 0 tick。
		const int PrevTick = Prev.m_CharacterTick == 0 ? Prev.m_SnapshotTick : Prev.m_CharacterTick;
		const int CurTick = Cur.m_CharacterTick == 0 ? Cur.m_SnapshotTick : Cur.m_CharacterTick;
		const int64_t TickDiff = static_cast<int64_t>(CurTick) - PrevTick;
		if(PrevTick < 0 || CurTick < 0 || LastDeathTick >= PrevTick || TickDiff <= 0 || TickDiff > MAX_SCAN_TICKS)
			return -1;

		int RecordTick = -1;
		const int Steps = static_cast<int>(TickDiff);
		for(int i = 0; i < Steps; ++i)
		{
			if(IsStart(mix(Prev.m_Pos, Cur.m_Pos, (float)i / Steps), mix(Prev.m_Pos, Cur.m_Pos, (float)(i + 1) / Steps)))
			{
				RecordTick = PrevTick + i + 1;
				if(!AllowRestart)
					break;
			}
		}
		return RecordTick;
	}
}

#endif
