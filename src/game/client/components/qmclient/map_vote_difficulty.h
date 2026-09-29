#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_VOTE_DIFFICULTY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_VOTE_DIFFICULTY_H

#include <base/math.h>
#include <base/str.h>

#include <game/voting.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace QmMapVotes
{
	inline int Stars(const char *pDescription)
	{
		if(pDescription == nullptr)
			return -1;
		const char *pSlash = str_find(pDescription, "/5");
		if(pSlash == nullptr || pSlash == pDescription)
			return -1;
		const char *pNumber = pSlash;
		while(pNumber > pDescription && pNumber[-1] >= '0' && pNumber[-1] <= '9')
			--pNumber;
		if(pNumber == pSlash)
			return -1;
		char aNumber[8];
		const int Length = minimum((int)(pSlash - pNumber), (int)sizeof(aNumber) - 1);
		str_copy(aNumber, pNumber, Length + 1);
		const int Result = str_toint(aNumber);
		return Result >= 0 && Result <= 5 ? Result : -1;
	}

	inline bool MapName(const char *pDescription, std::string &Out)
	{
		Out.clear();
		if(pDescription == nullptr || pDescription[0] == '\0')
			return false;
		if(str_startswith_nocase(pDescription, "Map:"))
		{
			const char *pStart = pDescription + 4;
			while(*pStart == ' ' || *pStart == '\t')
				++pStart;
			const char *pSuffix = str_find(pStart, " | ");
			Out.assign(pStart, pSuffix != nullptr ? pSuffix - pStart : str_length(pStart));
			while(!Out.empty() && (Out.back() == ' ' || Out.back() == '\t'))
				Out.pop_back();
			return !Out.empty() && str_find_nocase(Out.c_str(), "change_map") == nullptr;
		}
		const char *pBy = str_find_nocase(pDescription, " by ");
		if(pBy == nullptr || Stars(pDescription) < 0)
			return false;
		Out.assign(pDescription, pBy - pDescription);
		return !Out.empty();
	}

	inline bool ExtractQuoted(const char *pText, const char **ppAfter, std::string &Out)
	{
		Out.clear();
		if(pText == nullptr || *pText != '\"')
			return false;
		++pText;
		for(; *pText; ++pText)
		{
			if(*pText == '\"')
			{
				if(ppAfter)
					*ppAfter = pText + 1;
				return true;
			}
			if(*pText == '\\' && pText[1])
			{
				Out.push_back(pText[1]);
				++pText;
			}
			else
				Out.push_back(*pText);
		}
		return false;
	}

	inline bool MapVoteCommand(const char *pCommand, std::string &Out)
	{
		Out.clear();
		if(pCommand == nullptr)
			return false;
		const char *pChange = str_find_nocase(pCommand, "change_map");
		if(pChange == nullptr)
			return false;
		pChange += str_length("change_map");
		while(*pChange == ' ' || *pChange == '\t')
			++pChange;
		if(*pChange == '\"')
			return ExtractQuoted(pChange, nullptr, Out) && !Out.empty();
		const char *pEnd = pChange;
		while(*pEnd && *pEnd != ';' && *pEnd != ' ' && *pEnd != '\t')
			++pEnd;
		Out.assign(pChange, pEnd - pChange);
		return !Out.empty();
	}

	inline bool ParseDifficultyLine(const char *pLine, std::string &OutMap, int &OutStars)
	{
		OutMap.clear();
		OutStars = -1;
		if(pLine == nullptr)
			return false;
		while(*pLine == ' ' || *pLine == '\t')
			++pLine;
		if(!str_startswith_nocase(pLine, "difficulty"))
			return false;
		pLine += str_length("difficulty");
		if(*pLine != ' ' && *pLine != '\t')
			return false;
		while(*pLine == ' ' || *pLine == '\t')
			++pLine;
		if(*pLine == '\"')
		{
			const char *pAfter = nullptr;
			if(!ExtractQuoted(pLine, &pAfter, OutMap))
				return false;
			pLine = pAfter;
		}
		else
		{
			const char *pStart = pLine;
			while(*pLine && *pLine != ' ' && *pLine != '\t' && *pLine != '#')
				++pLine;
			OutMap.assign(pStart, pLine - pStart);
		}
		while(*pLine == ' ' || *pLine == '\t')
			++pLine;
		char *pEnd = nullptr;
		const long Value = std::strtol(pLine, &pEnd, 10);
		if(pEnd == pLine || Value < 0 || Value > 5)
			return false;
		while(*pEnd == ' ' || *pEnd == '\t')
			++pEnd;
		if(*pEnd == '\r')
			++pEnd;
		if(*pEnd != '\0' && *pEnd != '#')
			return false;
		OutStars = (int)Value;
		return !OutMap.empty();
	}

	inline bool MatchesFilter(int Stars, int NumPlayers, bool EmptyOnly, int StarMask, bool FavoritesOnly, bool IsFavorite)
	{
		// 「只看空服」是唯一的空服条件：EmptyOnly 关闭时空服照常显示，
		// 与引擎侧 CServerBrowser::Filter() 的 m_QmMapBrowserEmptyOnly 分支同义。
		if(EmptyOnly && NumPlayers != 0)
			return false;
		if(StarMask != 0 && (Stars < 0 || (StarMask & (1 << Stars)) == 0))
			return false;
		if(FavoritesOnly && !IsFavorite)
			return false;
		return true;
	}

	// 浏览器紧凑滑块的档位：0 不筛选，1 无人服务器，2-6 对应 1-5 星。
	constexpr int MAP_BROWSER_FILTER_LEVEL_NONE = 0;
	constexpr int MAP_BROWSER_FILTER_LEVEL_EMPTY = 1;
	constexpr int MAP_BROWSER_FILTER_LEVEL_FIRST_STAR = 2;
	constexpr int MAP_BROWSER_FILTER_LEVEL_LAST_STAR = 6;

	// 返回 -1 代表现有配置未表达单一档位（收藏、多选或星级与空服组合）。
	inline int MapBrowserFilterLevel(bool EmptyOnly, int StarMask)
	{
		if(StarMask == 0)
			return EmptyOnly ? MAP_BROWSER_FILTER_LEVEL_EMPTY : MAP_BROWSER_FILTER_LEVEL_NONE;
		if(EmptyOnly || StarMask <= 0 || (StarMask & (StarMask - 1)) != 0)
			return -1;
		for(int Star = 1; Star <= 5; ++Star)
		{
			if(StarMask == (1 << Star))
				return Star + MAP_BROWSER_FILTER_LEVEL_FIRST_STAR - 1;
		}
		return -1;
	}

	// 档位对应的星级；不筛选与无人服务器档返回 0。
	inline int MapBrowserFilterStars(int Level)
	{
		return Level >= MAP_BROWSER_FILTER_LEVEL_FIRST_STAR && Level <= MAP_BROWSER_FILTER_LEVEL_LAST_STAR ? Level - MAP_BROWSER_FILTER_LEVEL_FIRST_STAR + 1 : 0;
	}

	inline void ApplyMapBrowserFilterLevel(int Level, int &EmptyOnly, int &StarMask)
	{
		// 越界档位不表达任何筛选：保持现有配置，避免把「无法用单档表达」误写成为不筛选。
		if(Level < MAP_BROWSER_FILTER_LEVEL_NONE)
			return;
		EmptyOnly = Level == MAP_BROWSER_FILTER_LEVEL_EMPTY ? 1 : 0;
		const int Stars = MapBrowserFilterStars(Level);
		StarMask = Stars > 0 ? 1 << Stars : 0;
	}
}

class CQmMapVoteDifficulty
{
	struct SEntry
	{
		char m_aMapName[VOTE_DESC_LENGTH];
		int m_Stars;
		int m_Order;
	};
	std::vector<SEntry> m_vEntries;
	std::vector<SEntry> m_vFallbackEntries;
	uint64_t m_Revision = 0;
	bool m_Valid = false;

	static void Sort(std::vector<SEntry> &vEntries)
	{
		std::sort(vEntries.begin(), vEntries.end(), [](const SEntry &A, const SEntry &B) {
			const int Compare = str_comp_nocase(A.m_aMapName, B.m_aMapName);
			return Compare != 0 ? Compare < 0 : A.m_Order < B.m_Order;
		});
	}

	void Rebuild(uint64_t Revision, const CVoteOptionClient *pFirst)
	{
		m_vEntries.clear();
		int Order = 0;
		for(const CVoteOptionClient *pOption = pFirst; pOption; pOption = pOption->m_pNext, ++Order)
		{
			std::string MapName;
			if(!QmMapVotes::MapName(pOption->m_aDescription, MapName))
				continue;
			const int Stars = QmMapVotes::Stars(pOption->m_aDescription);
			if(Stars < 0)
				continue;
			SEntry Entry{};
			str_copy(Entry.m_aMapName, MapName.c_str());
			Entry.m_Stars = Stars;
			Entry.m_Order = Order;
			m_vEntries.push_back(Entry);
		}
		Sort(m_vEntries);
		m_Revision = Revision;
		m_Valid = true;
	}

	static int FindIn(const std::vector<SEntry> &vEntries, const char *pMapName)
	{
		const auto It = std::lower_bound(vEntries.begin(), vEntries.end(), pMapName, [](const SEntry &Entry, const char *pName) {
			return str_comp_nocase(Entry.m_aMapName, pName) < 0;
		});
		return It != vEntries.end() && str_comp_nocase(It->m_aMapName, pMapName) == 0 ? It->m_Stars : -1;
	}

public:
	bool AddFallbackLine(const char *pLine)
	{
		std::string MapName;
		int Stars = -1;
		if(QmMapVotes::ParseDifficultyLine(pLine, MapName, Stars))
		{
			SEntry Entry{};
			str_copy(Entry.m_aMapName, MapName.c_str());
			Entry.m_Stars = Stars;
			Entry.m_Order = (int)m_vFallbackEntries.size();
			m_vFallbackEntries.push_back(Entry);
			Sort(m_vFallbackEntries);
			return true;
		}
		if(pLine == nullptr || !str_startswith_nocase(pLine, "add_vote"))
			return false;
		const char *pQuote = pLine + str_length("add_vote");
		while(*pQuote == ' ' || *pQuote == '\t')
			++pQuote;
		std::string Label;
		if(!QmMapVotes::ExtractQuoted(pQuote, &pQuote, Label))
			return false;
		while(*pQuote == ' ' || *pQuote == '\t')
			++pQuote;
		std::string Command;
		if(!QmMapVotes::ExtractQuoted(pQuote, nullptr, Command))
			return false;
		Stars = QmMapVotes::Stars(Label.c_str());
		if(Stars < 0 || !QmMapVotes::MapVoteCommand(Command.c_str(), MapName))
			return false;
		SEntry Entry{};
		str_copy(Entry.m_aMapName, MapName.c_str());
		Entry.m_Stars = Stars;
		Entry.m_Order = (int)m_vFallbackEntries.size();
		m_vFallbackEntries.push_back(Entry);
		Sort(m_vFallbackEntries);
		return true;
	}

	void ClearFallback() { m_vFallbackEntries.clear(); }

	int Find(uint64_t Revision, const CVoteOptionClient *pFirst, const char *pMapName)
	{
		if(!pMapName || pMapName[0] == '\0')
			return -1;
		if(!m_Valid || m_Revision != Revision)
			Rebuild(Revision, pFirst);
		const int LiveStars = FindIn(m_vEntries, pMapName);
		return LiveStars >= 0 ? LiveStars : FindIn(m_vFallbackEntries, pMapName);
	}

	int FindFallback(const char *pMapName) const
	{
		return pMapName != nullptr && pMapName[0] != '\0' ? FindIn(m_vFallbackEntries, pMapName) : -1;
	}

	int FallbackCount() const { return (int)m_vFallbackEntries.size(); }
};

#endif
