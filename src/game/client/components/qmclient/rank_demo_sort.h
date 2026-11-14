#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_DEMO_SORT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_DEMO_SORT_H

#include "rank_demo_manifest.h"

#include <base/str.h>

#include <algorithm>
#include <vector>

// Rank 1 页面列表的列排序。清单收集默认按「单人优先、时间从新到旧」，
// 表头点击只改变展示顺序，不改变条目集合与缓存身份。
// 状态列依赖下载缓存与激活影子，属于外部状态，不参与排序。

namespace qmclient::rank_demo
{
	enum class ESortColumn
	{
		NONE = 0,
		MODE,
		PLAYERS,
		TIME,
		DATE,
	};

	struct SSortState
	{
		ESortColumn m_Column = ESortColumn::NONE;
		bool m_Descending = false;
	};

	// 点击同一列翻转方向；换列时从升序开始。NONE 列不响应点击。
	inline bool ToggleSort(SSortState &State, ESortColumn Column)
	{
		if(Column == ESortColumn::NONE)
			return false;
		if(State.m_Column == Column)
			State.m_Descending = !State.m_Descending;
		else
		{
			State.m_Column = Column;
			State.m_Descending = false;
		}
		return true;
	}

	inline bool LessForSort(const SEntry &Left, const SEntry &Right, ESortColumn Column)
	{
		switch(Column)
		{
		case ESortColumn::MODE:
		{
			const bool LeftTeam = IsTeamEntry(Left);
			const bool RightTeam = IsTeamEntry(Right);
			if(LeftTeam != RightTeam)
				return !LeftTeam;
			break;
		}
		case ESortColumn::PLAYERS:
		{
			const int Compare = str_comp_nocase(Left.m_Names.c_str(), Right.m_Names.c_str());
			if(Compare != 0)
				return Compare < 0;
			break;
		}
		case ESortColumn::TIME:
		{
			const float LeftTime = str_tofloat(Left.m_Time.c_str());
			const float RightTime = str_tofloat(Right.m_Time.c_str());
			if(LeftTime != RightTime)
				return LeftTime < RightTime;
			break;
		}
		case ESortColumn::DATE:
			if(Left.m_Ts != Right.m_Ts)
				return Left.m_Ts < Right.m_Ts;
			break;
		case ESortColumn::NONE:
			return false;
		}
		// 同列值相等时用 demo 名兜底，保证不同刷新顺序下结果稳定。
		return Left.m_Demo < Right.m_Demo;
	}

	inline void SortEntries(std::vector<SEntry> &vEntries, const SSortState &State)
	{
		if(State.m_Column == ESortColumn::NONE)
			return;
		std::stable_sort(vEntries.begin(), vEntries.end(), [&](const SEntry &Left, const SEntry &Right) {
			const bool Less = LessForSort(Left, Right, State.m_Column);
			const bool Greater = LessForSort(Right, Left, State.m_Column);
			if(Less == Greater)
				return false;
			return State.m_Descending ? Greater : Less;
		});
	}
} // namespace qmclient::rank_demo

#endif
