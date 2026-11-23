#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_LIST_CONTROLLER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_LIST_CONTROLLER_H

#include "rank_demo_manifest.h"
#include "rank_demo_sort.h"

#include <string>
#include <vector>

// Rank 1 列表每帧的交互状态：清单重排、搜索过滤、展示截断与选中项身份。
// 这些规则原先内联在菜单渲染函数里，无法脱离 CUi 验证；抽成纯模块后，
// “排序后双击哪一条”“过滤后选中项跑到哪里”都能直接对照断言。

namespace qmclient::rank_demo
{
	struct SListSelection
	{
		// 选中项的身份：每帧重建后按 demo 名重新定位，而不是记住行号。
		std::string m_Demo;
		int m_Index = 0;
	};

	struct SListSnapshot
	{
		std::vector<SEntry> m_vEntries;
		int m_SelectedIndex = 0;

		const SEntry *Selected() const
		{
			if(m_SelectedIndex < 0 || m_SelectedIndex >= static_cast<int>(m_vEntries.size()))
				return nullptr;
			return &m_vEntries[m_SelectedIndex];
		}
	};

	// 菜单列表的固定顺序：先排序、再按展示上限截断、最后按 demo 身份找回选中行。
	// DisplayLimit <= 0 表示不截断（非搜索模式展示全部条目）。
	inline SListSnapshot BuildRankListSnapshot(std::vector<SEntry> vEntries, const SSortState &Sort, int DisplayLimit, const SListSelection &Selection)
	{
		SortEntries(vEntries, Sort);
		if(DisplayLimit > 0 && static_cast<int>(vEntries.size()) > DisplayLimit)
			vEntries.resize(static_cast<size_t>(DisplayLimit));

		int SelectedIndex = Selection.m_Index;
		if(!Selection.m_Demo.empty())
		{
			for(int Index = 0; Index < static_cast<int>(vEntries.size()); Index++)
			{
				if(vEntries[Index].m_Demo == Selection.m_Demo)
				{
					SelectedIndex = Index;
					break;
				}
			}
		}
		if(SelectedIndex >= static_cast<int>(vEntries.size()))
			SelectedIndex = static_cast<int>(vEntries.size()) - 1;
		if(SelectedIndex < 0)
			SelectedIndex = 0;

		SListSnapshot Snapshot;
		Snapshot.m_vEntries = std::move(vEntries);
		Snapshot.m_SelectedIndex = SelectedIndex;
		return Snapshot;
	}

	// 列表框返回本帧的选中行后写回身份，供下一帧重排/过滤后重新定位。
	// 列表为空时清空身份，避免恢复出条目前把过期 demo 当成选中项。
	inline void CommitListSelection(SListSelection &Selection, const std::vector<SEntry> &vEntries, int SelectedIndex)
	{
		if(SelectedIndex < 0 || SelectedIndex >= static_cast<int>(vEntries.size()))
		{
			Selection.m_Demo.clear();
			Selection.m_Index = 0;
			return;
		}
		Selection.m_Demo = vEntries[SelectedIndex].m_Demo;
		Selection.m_Index = SelectedIndex;
	}

	enum class ERowActivation
	{
		NONE = 0,
		LOAD_GHOST,
		DOWNLOAD_REPLAY,
	};

	// 双击行与「加载影子 / 下载回放」按钮共用的动作判定：
	// 已缓存时双击等价于加载影子（已处于激活态则无动作）；未缓存时等价于下载，
	// 即使其它下载正在运行也允许入队，否则连续补档只能靠按钮。
	inline ERowActivation ResolveRowActivation(bool TaskBusy, bool GhostActive, bool DemoCached)
	{
		if(TaskBusy)
			return DemoCached ? ERowActivation::NONE : ERowActivation::DOWNLOAD_REPLAY;
		if(DemoCached)
			return GhostActive ? ERowActivation::NONE : ERowActivation::LOAD_GHOST;
		return ERowActivation::DOWNLOAD_REPLAY;
	}
} // namespace qmclient::rank_demo

#endif
