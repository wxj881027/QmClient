#ifndef ENGINE_CLIENT_INPUT_RING_H
#define ENGINE_CLIENT_INPUT_RING_H

// 客户端输入环的取用规则。
//
// 环槽位按“发送序号”递增写入，和 tick 没有固定对应关系：客户端的发送频率
// 低于 tick 频率（有变化或 25 Hz 保活才发），所以 tick % 容量 与真实槽位错开。
// 取某个 tick 要用的输入必须扫描整个环，找“不晚于该 tick 的最新记录”；
// 按 tick 取模直接定位会几乎总是落空，把输入回退成上一次的旧值。
template<typename Record, int Capacity>
inline const Record *QmLatestInputRecord(const Record (&aRecords)[Capacity], const int Tick)
{
	const Record *pBest = nullptr;
	for(const Record &Entry : aRecords)
	{
		if(Entry.m_Tick == -1 || Entry.m_Tick > Tick)
			continue;
		if(pBest == nullptr || pBest->m_Tick < Entry.m_Tick)
			pBest = &Entry;
	}
	return pBest;
}

#endif
