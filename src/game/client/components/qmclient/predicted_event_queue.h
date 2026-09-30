#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PREDICTED_EVENT_QUEUE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PREDICTED_EVENT_QUEUE_H

#include <game/client/prediction/gameworld.h>

#include <cstdint>
#include <vector>

inline bool QmPredictedEventExpired(const CGameWorld::CPredictedEvent &Event, int Tick, int TickSpeed)
{
	return static_cast<int64_t>(Tick) - Event.m_Tick > 3LL * TickSpeed;
}

inline void QmPruneHandledPredictedEvents(std::vector<CGameWorld::CPredictedEvent> &vEvents, int Tick, int TickSpeed)
{
	std::erase_if(vEvents, [Tick, TickSpeed](const CGameWorld::CPredictedEvent &Event) {
		return Event.m_Handled && QmPredictedEventExpired(Event, Tick, TickSpeed);
	});
}

template<typename F>
void QmProcessPredictedEvents(std::vector<CGameWorld::CPredictedEvent> &vEvents, int Tick, int TickSpeed, F &&HandleEvent)
{
	// 回调返回 false 时丢弃事件；其余事件按原顺序压缩，避免逐项 erase 搬移整个尾部。
	auto Write = vEvents.begin();
	for(auto Read = vEvents.begin(); Read != vEvents.end(); ++Read)
	{
		if(!Read->m_Handled && Read->m_Tick <= Tick)
		{
			if(!HandleEvent(*Read))
				continue;
			Read->m_Handled = true;
		}
		else if(QmPredictedEventExpired(*Read, Tick, TickSpeed))
			continue;

		if(Write != Read)
			*Write = *Read;
		++Write;
	}
	vEvents.erase(Write, vEvents.end());
}

#endif
