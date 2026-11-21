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

	inline int CompareValues(float Left, float Right)
	{
		if(Left == Right)
			return 0;
		return Left < Right ? -1 : 1;
	}

	inline int CompareDemoNames(const SEntry &Left, const SEntry &Right)
	{
		const int Compare = str_comp(Left.m_Demo.c_str(), Right.m_Demo.c_str());
		return Compare < 0 ? -1 : Compare > 0 ? 1 :
							0;
	}

	// 统一的列比较，返回 -1/0/1；主键相等时用 demo 名兜底，保证不同刷新顺序
	// 下结果稳定。TIME 列在 SortEntries 里使用预计算键，走同一套比较语义。
	inline int CompareForSort(const SEntry &Left, const SEntry &Right, ESortColumn Column)
	{
		int Compare = 0;
		switch(Column)
		{
		case ESortColumn::MODE:
			if(IsTeamEntry(Left) != IsTeamEntry(Right))
				Compare = IsTeamEntry(Left) ? 1 : -1; // solo 在前
			break;
		case ESortColumn::PLAYERS:
			Compare = str_comp_nocase(Left.m_Names.c_str(), Right.m_Names.c_str());
			break;
		case ESortColumn::TIME:
			Compare = CompareValues(str_tofloat(Left.m_Time.c_str()), str_tofloat(Right.m_Time.c_str()));
			break;
		case ESortColumn::DATE:
			Compare = Left.m_Ts == Right.m_Ts ? 0 : (Left.m_Ts < Right.m_Ts ? -1 : 1);
			break;
		case ESortColumn::NONE:
			return 0;
		}
		if(Compare != 0)
			return Compare < 0 ? -1 : 1;
		return CompareDemoNames(Left, Right);
	}

	inline void SortEntries(std::vector<SEntry> &vEntries, const SSortState &State)
	{
		if(State.m_Column == ESortColumn::NONE || vEntries.size() < 2)
			return;
		const bool Descending = State.m_Descending;

		if(State.m_Column == ESortColumn::TIME)
		{
			// 时间字符串每帧只解析一次；比较阶段用预计算键，避免每次比较都
			// 重新 str_tofloat，也不再为降序把 Less/Greater 各算一遍。
			struct STimeKey
			{
				float m_Time = 0.0f;
				size_t m_Index = 0;
			};
			std::vector<STimeKey> vKeys;
			vKeys.reserve(vEntries.size());
			for(size_t Index = 0; Index < vEntries.size(); Index++)
				vKeys.push_back({str_tofloat(vEntries[Index].m_Time.c_str()), Index});
			std::stable_sort(vKeys.begin(), vKeys.end(), [&](const STimeKey &Left, const STimeKey &Right) {
				int Compare = CompareValues(Left.m_Time, Right.m_Time);
				if(Compare == 0)
					Compare = CompareDemoNames(vEntries[Left.m_Index], vEntries[Right.m_Index]);
				return Descending ? Compare > 0 : Compare < 0;
			});
			std::vector<SEntry> vSorted;
			vSorted.reserve(vEntries.size());
			for(const STimeKey &Key : vKeys)
				vSorted.push_back(std::move(vEntries[Key.m_Index]));
			vEntries = std::move(vSorted);
			return;
		}

		std::stable_sort(vEntries.begin(), vEntries.end(), [&](const SEntry &Left, const SEntry &Right) {
			const int Compare = CompareForSort(Left, Right, State.m_Column);
			return Descending ? Compare > 0 : Compare < 0;
		});
	}
} // namespace qmclient::rank_demo

#endif
