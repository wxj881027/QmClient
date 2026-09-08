/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_SEARCH_LOGIC_H
#define GAME_CLIENT_UI_CARD_SEARCH_LOGIC_H

#include <string>
#include <string_view>
#include <vector>

// 卡片内部可搜索文本的字段类别；数值越小命中优先级越高。
enum class ECardSearchField
{
	TITLE = 0,
	CONTROL = 1,
	DESCRIPTION = 2,
	ACTION = 3,
	ALIAS = 4,
};

struct SCardSearchItem
{
	std::string m_Text;
	ECardSearchField m_Field = ECardSearchField::ALIAS;
};

struct SCardSearchResult
{
	std::string m_Id;
	int m_BestPriority = 0;
	// 最佳命中字段的原文（本地化后文本），供搜索结果展示命中依据；空表示全集。
	std::string m_BestMatch;
};

// 去首尾空白并按空白分词；空查询返回空列表（调用方据此返回可搜索全集）。
std::vector<std::string> TokenizeSearchQuery(std::string_view Query);
// Unicode 无关大小写的连续子串匹配；CJK 逐码点比较天然支持连续子串。
bool MatchSearchToken(std::string_view Haystack, std::string_view Token);
int SearchFieldPriority(ECardSearchField Field);

// 查询规则：全部分词都需命中（允许跨字段），结果按最佳命中字段优先、
// 稳定 card ID 打破同分。不承诺正则语法。
std::vector<SCardSearchResult> ResolveCardSearchMatches(
	const std::vector<std::pair<std::string, std::vector<SCardSearchItem>>> &vEntries,
	const std::vector<std::string> &vTokens);

#endif
