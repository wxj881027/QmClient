/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_SEARCH_INDEX_H
#define GAME_CLIENT_UI_CARD_SEARCH_INDEX_H

#include "card_registry.h"
#include "card_search_logic.h"

#include <string>
#include <utility>
#include <vector>

// presentation 提供本地化标题、说明、控件标签、动作文本和稳定别名；
// 控件与搜索索引必须消费同一份设置描述，不允许另写一套关键词。
class ICardSearchContentProvider
{
public:
	virtual ~ICardSearchContentProvider() = default;
	virtual void CollectCardContent(const SCardDescriptor &Card, std::vector<SCardSearchItem> &vOut) const = 0;
};

// 全局卡片搜索索引：折叠、未访问页面和功能关闭不影响检索；
// 本页隐藏只影响该页布局，搜索仍能找到并恢复卡片。
class CCardSearchIndex final
{
public:
	void Configure(const CCardRegistry *pRegistry, const ICardSearchContentProvider *pProvider);
	void Rebuild();
	std::vector<SCardSearchResult> Search(const std::string &Query) const;
	bool IsBuilt() const { return !m_vEntries.empty(); }
	unsigned Revision() const { return m_Revision; }

private:
	std::vector<SCardSearchItem> CollectItems(const SCardDescriptor &Card) const;

	const CCardRegistry *m_pRegistry = nullptr;
	const ICardSearchContentProvider *m_pProvider = nullptr;
	std::vector<std::pair<std::string, std::vector<SCardSearchItem>>> m_vEntries;
	unsigned m_Revision = 0;
};

#endif
