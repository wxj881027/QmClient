#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PENDING_HAMMER_HITS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PENDING_HAMMER_HITS_H

#include <base/vmath.h>

#include <engine/shared/snapshot.h>

#include <array>
#include <cstddef>
#include <span>

// 每次只暂存当前快照的锤击事件，数量不会超过该快照的全部条目。
class CQmPendingHammerHits
{
public:
	struct SEvent
	{
		vec2 m_Pos;
		int m_SnapshotTick;
		int m_Connection;
		int m_EventOrdinal;
		bool m_RenderEffect;
	};

	void Reset() noexcept { m_Count = 0; }
	[[nodiscard]] bool TryAppend(const SEvent &Event) noexcept
	{
		if(m_Count == m_aEvents.size())
			return false;
		m_aEvents[m_Count] = Event;
		++m_Count;
		return true;
	}
	std::span<const SEvent> Events() const noexcept { return {m_aEvents.data(), m_Count}; }

private:
	std::array<SEvent, CSnapshot::MAX_ITEMS> m_aEvents{};
	std::size_t m_Count = 0;
};

#endif
