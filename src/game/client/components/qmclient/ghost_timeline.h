#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_TIMELINE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_TIMELINE_H

#include "ghost_path.h"

#include <cstdint>
#include <limits>

namespace qm_ghost_timeline
{
	inline bool FitsInt(int64_t Value) noexcept
	{
		return Value >= std::numeric_limits<int>::min() && Value <= std::numeric_limits<int>::max();
	}

	// 先验证全部候选时间，再提交旧格式重建；失败保留原记录和起点。
	template<typename T>
	bool Prepare(qm_ghost_path::CChunkedPath<T> &Path, bool HasTicks, int &StartTick) noexcept
	{
		const int Count = Path.Size();
		if(Count <= 0)
			return false;
		int64_t LegacyStart = 0;
		if(!HasTicks)
		{
			for(int i = 1; i < Count; ++i)
				if(Path.Get(i)->m_AttackTick != Path.Get(i - 1)->m_AttackTick)
					LegacyStart = static_cast<int64_t>(Path.Get(i)->m_AttackTick) - i;
			if(!FitsInt(LegacyStart) || !FitsInt(LegacyStart + Count - 1))
				return false;
		}
		const int First = HasTicks ? Path.Get(0)->m_Tick : static_cast<int>(LegacyStart);
		const int ResolvedStart = StartTick == -1 ? First : StartTick;
		int64_t Previous = First;
		for(int i = 0; i < Count; ++i)
		{
			const int64_t Tick = HasTicks ? Path.Get(i)->m_Tick : LegacyStart + i;
			if(Tick < Previous || !FitsInt(Tick - ResolvedStart))
				return false;
			Previous = Tick;
		}
		if(!HasTicks)
			for(int i = 0; i < Count; ++i)
				Path.Get(i)->m_Tick = static_cast<int>(LegacyStart + i);
		StartTick = ResolvedStart;
		return true;
	}

	// 三个原生tick先提升，避免先溢出再钳制。
	inline int64_t OffsetTick(int Base, int Tick, int Origin) noexcept
	{
		return static_cast<int64_t>(Base) + Tick - Origin;
	}

	// 玩家渲染器还会做PrevTick-AttackTick，两个结果都须可表示。
	inline bool MapAttack(int AttackTick, int64_t GhostTick, int CurrentTick, int PrevTick, int &Result) noexcept
	{
		if(!FitsInt(GhostTick))
			return false;
		const int64_t Mapped = static_cast<int64_t>(AttackTick) + CurrentTick - GhostTick;
		if(!FitsInt(Mapped) || !FitsInt(static_cast<int64_t>(PrevTick) - Mapped))
			return false;
		Result = static_cast<int>(Mapped);
		return true;
	}
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_TIMELINE_H
