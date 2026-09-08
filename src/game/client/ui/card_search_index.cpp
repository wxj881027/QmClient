/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "card_search_index.h"

#include <algorithm>

void CCardSearchIndex::Configure(const CCardRegistry *pRegistry, const ICardSearchContentProvider *pProvider)
{
	if(m_pRegistry == pRegistry && m_pProvider == pProvider)
		return;
	m_pRegistry = pRegistry;
	m_pProvider = pProvider;
	m_vEntries.clear();
}

std::vector<SCardSearchItem> CCardSearchIndex::CollectItems(const SCardDescriptor &Card) const
{
	// descriptor 字段是内容下限；本地化文本与内部设置项由 provider 补齐。
	// 索引与渲染共用文本源，不索引密码、聊天等任意用户数据。
	std::vector<SCardSearchItem> vItems;
	vItems.push_back({Card.m_Id, ECardSearchField::ALIAS});
	if(!Card.m_TitleKey.empty())
		vItems.push_back({Card.m_TitleKey, ECardSearchField::TITLE});
	if(!Card.m_DescriptionKey.empty())
		vItems.push_back({Card.m_DescriptionKey, ECardSearchField::DESCRIPTION});
	for(const std::string &Keyword : Card.m_SearchKeywords)
		if(!Keyword.empty())
			vItems.push_back({Keyword, ECardSearchField::ALIAS});
	if(m_pProvider)
		m_pProvider->CollectCardContent(Card, vItems);
	return vItems;
}

void CCardSearchIndex::Rebuild()
{
	m_vEntries.clear();
	if(!m_pRegistry)
		return;
	for(const SCardDescriptor *pCard : m_pRegistry->CardsByInputPriority())
		m_vEntries.emplace_back(pCard->m_Id, CollectItems(*pCard));
	std::sort(m_vEntries.begin(), m_vEntries.end(), [](const auto &Left, const auto &Right) { return Left.first < Right.first; });
	++m_Revision;
}

std::vector<SCardSearchResult> CCardSearchIndex::Search(const std::string &Query) const
{
	return ResolveCardSearchMatches(m_vEntries, TokenizeSearchQuery(Query));
}
