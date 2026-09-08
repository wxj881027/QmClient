/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "card_search_logic.h"

#include <base/str.h>

#include <algorithm>

std::vector<std::string> TokenizeSearchQuery(const std::string_view Query)
{
	std::vector<std::string> vTokens;
	size_t Index = 0;
	while(Index < Query.size())
	{
		while(Index < Query.size() && std::isspace(static_cast<unsigned char>(Query[Index])))
			++Index;
		const size_t Begin = Index;
		while(Index < Query.size() && !std::isspace(static_cast<unsigned char>(Query[Index])))
			++Index;
		if(Index > Begin)
			vTokens.emplace_back(Query.substr(Begin, Index - Begin));
	}
	return vTokens;
}

bool MatchSearchToken(const std::string_view Haystack, const std::string_view Token)
{
	if(Token.empty())
		return false;
	// str_utf8_find_nocase 按码点折叠大小写，对 CJK 等宽字符即连续子串匹配。
	return str_utf8_find_nocase(Haystack.empty() ? "" : std::string(Haystack).c_str(), std::string(Token).c_str()) != nullptr;
}

int SearchFieldPriority(const ECardSearchField Field)
{
	return static_cast<int>(Field);
}

std::vector<SCardSearchResult> ResolveCardSearchMatches(
	const std::vector<std::pair<std::string, std::vector<SCardSearchItem>>> &vEntries,
	const std::vector<std::string> &vTokens)
{
	std::vector<SCardSearchResult> vResults;
	if(vTokens.empty())
	{
		// 空查询显示可搜索全集；以稳定 ID 排序保证确定性。
		vResults.reserve(vEntries.size());
		for(const auto &Entry : vEntries)
			vResults.push_back({Entry.first, SearchFieldPriority(ECardSearchField::TITLE), {}});
		std::sort(vResults.begin(), vResults.end(), [](const SCardSearchResult &Left, const SCardSearchResult &Right) {
			return Left.m_Id < Right.m_Id;
		});
		return vResults;
	}

	for(const auto &Entry : vEntries)
	{
		int BestPriority = -1;
		std::string BestMatch;
		bool AllMatch = true;
		for(const std::string &Token : vTokens)
		{
			bool TokenMatched = false;
			for(const SCardSearchItem &Item : Entry.second)
			{
				if(!MatchSearchToken(Item.m_Text, Token))
					continue;
				TokenMatched = true;
				const int Priority = SearchFieldPriority(Item.m_Field);
				if(BestPriority < 0 || Priority < BestPriority)
				{
					BestPriority = Priority;
					BestMatch = Item.m_Text;
				}
			}
			if(!TokenMatched)
			{
				AllMatch = false;
				break;
			}
		}
		if(AllMatch && BestPriority >= 0)
			vResults.push_back({Entry.first, BestPriority, std::move(BestMatch)});
	}
	std::sort(vResults.begin(), vResults.end(), [](const SCardSearchResult &Left, const SCardSearchResult &Right) {
		if(Left.m_BestPriority != Right.m_BestPriority)
			return Left.m_BestPriority < Right.m_BestPriority;
		return Left.m_Id < Right.m_Id;
	});
	return vResults;
}
