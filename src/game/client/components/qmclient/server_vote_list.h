#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SERVER_VOTE_LIST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SERVER_VOTE_LIST_H

#include <base/str.h>

#include <game/voting.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

class CQmServerVoteList
{
	std::vector<std::string> m_vOptions;
	std::vector<int> m_vVisible;
	std::string m_Search;
	uint64_t m_Revision = std::numeric_limits<uint64_t>::max();
	int m_Selected = -1;

public:
	bool NeedsUpdate(uint64_t Revision, const char *pSearch) const
	{
		return m_Revision != Revision || m_Search != pSearch;
	}

	bool Update(const CVoteOptionClient *pFirst, uint64_t Revision, const char *pSearch)
	{
		if(!NeedsUpdate(Revision, pSearch))
			return false;
		if(m_Revision != Revision)
		{
			// 保留服务器原文与顺序，不解析地图名、排序或合并重复选项。
			m_vOptions.clear();
			for(const CVoteOptionClient *pOption = pFirst; pOption != nullptr; pOption = pOption->m_pNext)
				m_vOptions.emplace_back(pOption->m_aDescription);
			m_Revision = Revision;
			// 同一个索引在子菜单里可能已经变成另一个动作。
			m_Selected = -1;
		}
		m_Search = pSearch;
		m_vVisible.clear();
		for(size_t Index = 0; Index < m_vOptions.size(); ++Index)
		{
			if(m_Search.empty() || str_utf8_find_nocase(m_vOptions[Index].c_str(), m_Search.c_str()) != nullptr)
				m_vVisible.push_back((int)Index);
		}
		if(SelectedRow() < 0)
			m_Selected = -1;
		return true;
	}

	const std::vector<std::string> &Options() const { return m_vOptions; }
	const std::vector<int> &Visible() const { return m_vVisible; }

	void Select(int Row)
	{
		m_Selected = Row >= 0 && Row < (int)m_vVisible.size() ? m_vVisible[Row] : -1;
	}

	int SelectedRow() const
	{
		const auto It = std::find(m_vVisible.begin(), m_vVisible.end(), m_Selected);
		return It != m_vVisible.end() ? (int)(It - m_vVisible.begin()) : -1;
	}

	int SelectedOption(uint64_t Revision) const
	{
		return Revision == m_Revision ? m_Selected : -1;
	}
};

#endif
