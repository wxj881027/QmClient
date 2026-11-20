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
public:
	// 只替换真实字符串和容器扩容边界，借用对象须覆盖列表使用期。
	class CPreparationOperations
	{
	public:
		virtual ~CPreparationOperations() = default;
		virtual void AssignSearch(std::string &Search, const char *pSearch) { Search = pSearch; }
		virtual void AppendOption(std::vector<std::string> &vOptions, const char *pDescription) { vOptions.emplace_back(pDescription); }
		virtual void AppendVisible(std::vector<int> &vVisible, int Index) { vVisible.push_back(Index); }
	};

private:
	std::vector<std::string> m_vOptions;
	std::vector<int> m_vVisible;
	std::string m_Search;
	uint64_t m_Revision = std::numeric_limits<uint64_t>::max();
	int m_Selected = -1;
	CPreparationOperations *m_pPreparationOperations = nullptr;

public:
	CQmServerVoteList() = default;
	explicit CQmServerVoteList(CPreparationOperations *pOperations) :
		m_pPreparationOperations(pOperations) {}

	bool NeedsUpdate(uint64_t Revision, const char *pSearch) const
	{
		return m_Revision != Revision || m_Search != pSearch;
	}

	bool Update(const CVoteOptionClient *pFirst, uint64_t Revision, const char *pSearch) noexcept
	{
		if(!NeedsUpdate(Revision, pSearch))
			return false;
		try
		{
			CPreparationOperations DefaultOperations;
			auto &Operations = m_pPreparationOperations ? *m_pPreparationOperations : DefaultOperations;
			std::string Search;
			Operations.AssignSearch(Search, pSearch);
			const bool Replace = m_Revision != Revision;
			std::vector<std::string> vOptions;
			if(Replace)
			{
				// 保留服务器原文与顺序；旧行与可见索引在候选完整前保持同一版本。
				for(const CVoteOptionClient *pOption = pFirst; pOption != nullptr; pOption = pOption->m_pNext)
					Operations.AppendOption(vOptions, pOption->m_aDescription);
			}
			const auto &Options = Replace ? vOptions : m_vOptions;
			std::vector<int> vVisible;
			for(size_t Index = 0; Index < Options.size(); ++Index)
			{
				if(Search.empty() || str_utf8_find_nocase(Options[Index].c_str(), Search.c_str()) != nullptr)
					Operations.AppendVisible(vVisible, (int)Index);
			}
			int Selected = Replace ? -1 : m_Selected;
			if(std::find(vVisible.begin(), vVisible.end(), Selected) == vVisible.end())
				Selected = -1;
			// 同一个索引在子菜单里可能已变成另一个动作，只有整份候选可以发布。
			if(Replace)
				m_vOptions.swap(vOptions);
			m_vVisible.swap(vVisible);
			m_Search.swap(Search);
			m_Revision = Revision;
			m_Selected = Selected;
			return true;
		}
		catch(...)
		{
			// 失败时保留旧文本、索引、选择和版本，下帧仍可按当前请求重试。
			return false;
		}
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
