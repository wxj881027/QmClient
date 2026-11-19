#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_GLOBAL_SEARCH_CACHE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_GLOBAL_SEARCH_CACHE_H

#include "search_answers.h"

#include <game/client/QmUi/QmCardRegistry.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

struct SSettingsGlobalSearchResults
{
	std::vector<qm_card_registry::SCardSearchResult> m_vAllVisibleCards;
	qm_search_answers::SResult m_Answers;

	SSettingsGlobalSearchResults() = default;
	SSettingsGlobalSearchResults(const SSettingsGlobalSearchResults &) = delete;
	SSettingsGlobalSearchResults &operator=(const SSettingsGlobalSearchResults &) = delete;

private:
	// 导航位置来自可重建的全局模型，缓存不能跨帧借用该模型的字符串。
	std::deque<std::string> m_vNavigationTabs;
	friend class CSettingsGlobalSearchCache;
};

// 完整准备查询键、答案、卡片及两份页内布局后才发布；资源不足保留旧快照并重试。
class CSettingsGlobalSearchCache
{
	struct SSnapshot;
	std::unique_ptr<SSnapshot> m_pSnapshot;
	uint64_t m_Revision = 0;

public:
	CSettingsGlobalSearchCache() noexcept;
	~CSettingsGlobalSearchCache();
	CSettingsGlobalSearchCache(const CSettingsGlobalSearchCache &) = delete;
	CSettingsGlobalSearchCache &operator=(const CSettingsGlobalSearchCache &) = delete;

	bool TryUpdate(const char *pQuery, const char *pLanguage, bool Sixup, uint64_t TextGeneration, const qm_card_order::CModel &SourceModel);
	bool HasSnapshot() const noexcept { return m_pSnapshot != nullptr; }
	uint64_t Revision() const noexcept { return m_Revision; }
	const SSettingsGlobalSearchResults &Results() const;
	qm_card_order::CModel &OrderModel(bool MeasurementOnly);
};

#endif
