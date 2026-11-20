#include "settings_global_search_cache.h"

#include <base/str.h>
#include <base/system.h>

#include <game/client/QmUi/cards/QmCardCatalogSearch.h>
#include <game/client/QmUi/cards/QmSearchAnswerCard.h>

#include <new>
#include <stdexcept>
#include <utility>

struct CSettingsGlobalSearchCache::SSnapshot
{
	std::string m_Query;
	std::string m_Language;
	bool m_Sixup = false;
	uint64_t m_TextGeneration = 0;
	uint64_t m_LayoutRevision = 0;
	SSettingsGlobalSearchResults m_Results;
	qm_card_order::CModel m_Model;
	qm_card_order::CModel m_PrewarmModel;

	bool Matches(const char *pQuery, const char *pLanguage, bool Sixup, uint64_t TextGeneration, uint64_t LayoutRevision) const noexcept
	{
		return m_Query == pQuery && m_Language == pLanguage && m_Sixup == Sixup &&
		       m_TextGeneration == TextGeneration && m_LayoutRevision == LayoutRevision;
	}
};

CSettingsGlobalSearchCache::CSettingsGlobalSearchCache() noexcept = default;
CSettingsGlobalSearchCache::~CSettingsGlobalSearchCache() = default;

bool CSettingsGlobalSearchCache::TryUpdate(const char *pQuery, const char *pLanguage, bool Sixup, uint64_t TextGeneration, const qm_card_order::CModel &SourceModel)
{
	pQuery = pQuery != nullptr ? pQuery : "";
	pLanguage = pLanguage != nullptr ? pLanguage : "";
	const uint64_t LayoutRevision = SourceModel.LayoutRevision();
	if(m_pSnapshot != nullptr && m_pSnapshot->Matches(pQuery, pLanguage, Sixup, TextGeneration, LayoutRevision))
		return true;
	try
	{
		// 空壳不构造会分配的模型/双端队列，首次准备失败也能留到下一帧。
		auto Candidate = std::make_unique<SSnapshot>();
		Candidate->m_Query = pQuery;
		Candidate->m_Language = pLanguage;
		Candidate->m_Sixup = Sixup;
		Candidate->m_TextGeneration = TextGeneration;
		Candidate->m_LayoutRevision = LayoutRevision;
		auto &Results = Candidate->m_Results;
		if(!qm_search_answers::TrySearch(pQuery, Results.m_Answers))
			return false;
		if(pQuery[0] != '\0')
		{
			auto vCards = qm_card_registry::SearchCards(pQuery, SourceModel);
			qm_card_catalog::AddAnswerCards(Results.m_Answers, SourceModel, vCards);
			Results.m_vAllVisibleCards.reserve(vCards.size());
			for(auto &Card : vCards)
			{
				const char *pTab = Card.m_Target.m_pTab;
				if(pTab != nullptr && (str_comp(pTab, "global-search") == 0 ||
							      (Sixup && str_comp(pTab, "tee") == 0) || (!Sixup && str_comp(pTab, "tee7") == 0)))
					continue;
				if(pTab != nullptr)
				{
					Results.m_vNavigationTabs.emplace_back(pTab);
					Card.m_Target.m_pTab = Results.m_vNavigationTabs.back().c_str();
				}
				Results.m_vAllVisibleCards.push_back(std::move(Card));
			}
		}
		const auto &vCards = Results.m_vAllVisibleCards;
		std::vector<qm_card_order::SEntry> vEntries;
		vEntries.reserve(vCards.size() + 2);
		if(!Results.m_Answers.m_vCandidates.empty())
			vEntries.push_back({qm_card_catalog::SEARCH_ANSWER_CARD_ID, "global-search", 0, -2});
		vEntries.push_back({"deck:global-search-empty", "global-search", 0, -1});
		for(size_t Index = 0; Index < vCards.size(); ++Index)
			vEntries.push_back({vCards[Index].m_pStableId, "global-search", Index % 2 == 0 ? 1 : 2, (int)(Index / 2)});
		Candidate->m_Model.SetEntries(vEntries);
		Candidate->m_PrewarmModel.SetEntries(std::move(vEntries));
		if(m_pSnapshot == nullptr)
			m_pSnapshot = std::move(Candidate);
		else
		{
			// 提交点不分配，两个模型保持各自单调版本，旧数据在候选析构时退休。
			auto &Current = *m_pSnapshot;
			Current.m_Query.swap(Candidate->m_Query);
			Current.m_Language.swap(Candidate->m_Language);
			std::swap(Current.m_Sixup, Candidate->m_Sixup);
			std::swap(Current.m_TextGeneration, Candidate->m_TextGeneration);
			std::swap(Current.m_LayoutRevision, Candidate->m_LayoutRevision);
			Current.m_Results.m_vAllVisibleCards.swap(Results.m_vAllVisibleCards);
			Current.m_Results.m_vNavigationTabs.swap(Results.m_vNavigationTabs);
			Current.m_Results.m_Answers.m_vCandidates.swap(Results.m_Answers.m_vCandidates);
			std::swap(Current.m_Results.m_Answers.m_pAnswer, Results.m_Answers.m_pAnswer);
			Current.m_Model.SwapContent(Candidate->m_Model);
			Current.m_PrewarmModel.SwapContent(Candidate->m_PrewarmModel);
		}
		++m_Revision;
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
}

const SSettingsGlobalSearchResults &CSettingsGlobalSearchCache::Results() const
{
	dbg_assert(m_pSnapshot != nullptr, "global search cache has no published snapshot");
	return m_pSnapshot->m_Results;
}

qm_card_order::CModel &CSettingsGlobalSearchCache::OrderModel(bool MeasurementOnly)
{
	dbg_assert(m_pSnapshot != nullptr, "global search cache has no published snapshot");
	return MeasurementOnly ? m_pSnapshot->m_PrewarmModel : m_pSnapshot->m_Model;
}
