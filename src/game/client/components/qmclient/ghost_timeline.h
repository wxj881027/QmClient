#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_TIMELINE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_TIMELINE_H

#include "ghost_path.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace qm_ghost_timeline
{
	inline bool FitsInt(int64_t Value) noexcept
	{
		return Value >= std::numeric_limits<int>::min() && Value <= std::numeric_limits<int>::max();
	}

	// 查看面板使用源录像的绝对tick，幽灵时钟使用相对tick；只发布可表示的完整区间。
	class CViewRange
	{
		int m_First = 0;
		int m_Duration = 0;

	public:
		bool Prepare(int First, int Duration) noexcept
		{
			First = std::max(0, First);
			if(Duration <= 0 || !FitsInt(static_cast<int64_t>(First) + Duration))
				return false;
			m_First = First;
			m_Duration = Duration;
			return true;
		}
		int First() const noexcept { return m_First; }
		int Last() const noexcept { return m_First + m_Duration; }
		int Duration() const noexcept { return m_Duration; }
		int ToRelative(int Tick) const noexcept
		{
			return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(Tick) - m_First, 0, m_Duration));
		}
		int ToAbsolute(int Tick) const noexcept { return m_First + std::clamp(Tick, 0, m_Duration); }
	};

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
