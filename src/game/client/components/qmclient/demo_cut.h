#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_CUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_CUT_H

#include <engine/demo.h>

#include <algorithm>
#include <cstdint>

namespace qm_demo_cut
{
	inline bool ResolveRange(int Begin, int End, int FirstTick, int LastTick, SDemoSliceSegment &Range)
	{
		Range = {-1, -1};
		if(FirstTick < 0 || FirstTick >= LastTick || (Begin == -1 && End == -1))
			return false;
		const int StartTick = std::clamp(Begin == -1 ? FirstTick : Begin, FirstTick, LastTick);
		const int EndTick = std::clamp(End == -1 ? LastTick : End, FirstTick, LastTick);
		if(StartTick >= EndTick)
			return false;
		Range = {StartTick, EndTick};
		return true;
	}

	inline int64_t ToCentiseconds(int64_t Ticks, int TickSpeed)
	{
		// 保留短于一秒的裁剪片段；调用方须提供有效的回放 tick 频率。
		return Ticks * 100 / TickSpeed;
	}

	class CPreview
	{
		int m_EndTick = -1;
		bool m_Finished = false;

	public:
		void Reset()
		{
			m_EndTick = -1;
			m_Finished = false;
		}
		bool Start(const SDemoSliceSegment &Range)
		{
			Reset();
			if(Range.m_StartTick < 0 || Range.m_StartTick >= Range.m_EndTick)
				return false;
			m_EndTick = Range.m_EndTick;
			return true;
		}
		bool IsActive() const { return m_EndTick >= 0 && !m_Finished; }
		bool IsFinished() const { return m_Finished; }
		int EndTick() const { return m_EndTick; }
		bool Update(int CurrentTick)
		{
			if(!IsActive() || CurrentTick < m_EndTick)
				return false;
			m_Finished = true;
			return true;
		}
	};
}

#endif
