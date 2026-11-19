#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_MANUAL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_MANUAL_H

#include "ghost_timeline.h"

#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

namespace qm_ghost_manual
{
	// 选择、时长和主轨采样先作为候选准备；分配失败不改动已发布的播放计划。
	template<size_t Capacity, typename TAllocator = std::allocator<int>>
	class CPlan
	{
	public:
		std::array<bool, Capacity> m_aSlots{};
		int m_PrimarySlot = -1;
		qm_ghost_timeline::CViewRange m_Range;
		std::vector<int, TAllocator> m_vSampleTicks;

		explicit CPlan(const TAllocator &Allocator = TAllocator()) :
			m_vSampleTicks(Allocator) {}

		template<typename TGetTrack>
		bool Prepare(const std::vector<int> &vSlots, TGetTrack &&GetTrack, int FirstTick = 0)
		{
			try
			{
				CPlan Candidate(m_vSampleTicks.get_allocator());
				Candidate.m_PrimarySlot = vSlots.empty() ? -1 : vSlots.front();
				int EndTick = 0;
				for(int Slot : vSlots)
				{
					if(Slot < 0 || static_cast<size_t>(Slot) >= Capacity)
						continue;
					Candidate.m_aSlots[Slot] = true;
					const auto *pTrack = GetTrack(Slot);
					if(!pTrack || pTrack->m_Path.Size() == 0)
						continue;
					const int64_t End = static_cast<int64_t>(pTrack->m_Path.Get(pTrack->m_Path.Size() - 1)->m_Tick) - pTrack->m_StartTick;
					if(!qm_ghost_timeline::FitsInt(End))
						return false;
					EndTick = std::max(EndTick, static_cast<int>(End));
				}
				// 保留最长成员轨迹，不因主轨提前结束而截断团队回放。
				if(!Candidate.m_Range.Prepare(FirstTick, EndTick))
					return false;
				for(int Slot : vSlots)
				{
					if(Slot < 0 || static_cast<size_t>(Slot) >= Capacity || Slot != Candidate.m_PrimarySlot)
						continue;
					const auto *pTrack = GetTrack(Slot);
					if(!pTrack)
						continue;
					Candidate.m_vSampleTicks.reserve(pTrack->m_Path.Size());
					for(int Index = 0; Index < pTrack->m_Path.Size(); ++Index)
					{
						const int64_t Tick = static_cast<int64_t>(pTrack->m_Path.Get(Index)->m_Tick) - pTrack->m_StartTick;
						if(!qm_ghost_timeline::FitsInt(Tick))
							return false;
						Candidate.m_vSampleTicks.push_back(static_cast<int>(Tick));
					}
				}
				m_vSampleTicks.swap(Candidate.m_vSampleTicks);
				m_aSlots = Candidate.m_aSlots;
				m_PrimarySlot = Candidate.m_PrimarySlot;
				m_Range = Candidate.m_Range;
				return true;
			}
			catch(const std::bad_alloc &)
			{
				return false;
			}
			catch(const std::length_error &)
			{
				return false;
			}
		}
	};
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_MANUAL_H
