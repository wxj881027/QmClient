#include <game/client/QmUi/cards/QmSearchAnswerCard.h>
#include <game/client/components/qmclient/settings_global_search_cache.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <vector>

namespace
{
	constexpr const char *OLD_QUERY = "游戏没有声音怎么办？";
	constexpr const char *NEW_QUERY = "同一语音房间走远后声音变小怎么办？";
	constexpr const char *LANGUAGE_KEY = "global-search-test-language";
	constexpr uint64_t TEXT_GENERATION = 17;

	using TCard = qm_card_registry::SCardSearchResult;
	using TModel = qm_card_order::CModel;

	struct SModelObservation
	{
		const TModel *m_pModel = nullptr;
		const qm_card_order::SEntry *m_pEntries = nullptr;
		std::vector<qm_card_order::SEntry> m_vEntries;
		std::vector<std::string> m_vTabs;
		std::vector<int> m_vStateIndices;
		uint64_t m_LayoutRevision = 0;
		uint64_t m_StateIndexRevision = 0;
		bool m_Dirty = false;
	};

	struct SCacheObservation
	{
		const SSettingsGlobalSearchResults *m_pResults = nullptr;
		const TCard *m_pCards = nullptr;
		size_t m_CardCapacity = 0;
		std::vector<TCard> m_vCards;
		std::vector<std::string> m_vNavigationTabs;
		const qm_search_answers::SAnswer *m_pAnswer = nullptr;
		const qm_search_answers::SAnswer *const *m_pCandidates = nullptr;
		size_t m_CandidateCapacity = 0;
		std::vector<const qm_search_answers::SAnswer *> m_vCandidates;
		std::array<SModelObservation, 2> m_aModels;
		uint64_t m_Revision = 0;
	};

	struct SUpdateAttempt
	{
		bool m_Success;
		bool m_Triggered;
		size_t m_Attempts;
	};

	SModelObservation ObserveModel(TModel &Model)
	{
		SModelObservation Observed;
		Observed.m_pModel = &Model;
		Observed.m_pEntries = Model.Count() != 0 ? &Model.Entry(0) : nullptr;
		Observed.m_LayoutRevision = Model.LayoutRevision();
		Observed.m_StateIndexRevision = Model.StateIndexRevision();
		Observed.m_Dirty = Model.IsDirty();
		for(int Index = 0; Index < Model.Count(); ++Index)
		{
			const auto &Entry = Model.Entry(Index);
			Observed.m_vEntries.push_back(Entry);
			Observed.m_vTabs.emplace_back(Entry.m_pDefaultTab != nullptr ? Entry.m_pDefaultTab : "");
			Observed.m_vStateIndices.push_back(Model.StateIndexForStableId(Entry.m_pStableId));
		}
		return Observed;
	}

	SCacheObservation ObserveCache(CSettingsGlobalSearchCache &Cache)
	{
		SCacheObservation Observed;
		const auto &Results = Cache.Results();
		Observed.m_pResults = &Results;
		Observed.m_pCards = Results.m_vAllVisibleCards.data();
		Observed.m_CardCapacity = Results.m_vAllVisibleCards.capacity();
		Observed.m_vCards = Results.m_vAllVisibleCards;
		for(const auto &Card : Results.m_vAllVisibleCards)
			Observed.m_vNavigationTabs.emplace_back(Card.m_Target.m_pTab != nullptr ? Card.m_Target.m_pTab : "");
		Observed.m_pAnswer = Results.m_Answers.m_pAnswer;
		Observed.m_pCandidates = Results.m_Answers.m_vCandidates.data();
		Observed.m_CandidateCapacity = Results.m_Answers.m_vCandidates.capacity();
		Observed.m_vCandidates = Results.m_Answers.m_vCandidates;
		Observed.m_aModels = {ObserveModel(Cache.OrderModel(false)), ObserveModel(Cache.OrderModel(true))};
		Observed.m_Revision = Cache.Revision();
		return Observed;
	}

	void ExpectResultValues(const SSettingsGlobalSearchResults &Results, const SCacheObservation &Expected)
	{
		EXPECT_EQ(Results.m_Answers.m_pAnswer, Expected.m_pAnswer);
		EXPECT_EQ(Results.m_Answers.m_vCandidates, Expected.m_vCandidates);
		ASSERT_EQ(Results.m_vAllVisibleCards.size(), Expected.m_vCards.size());
		for(size_t Index = 0; Index < Expected.m_vCards.size(); ++Index)
		{
			SCOPED_TRACE(Index);
			const auto &Card = Results.m_vAllVisibleCards[Index];
			const auto &Previous = Expected.m_vCards[Index];
			EXPECT_STREQ(Card.m_pStableId, Previous.m_pStableId);
			EXPECT_EQ(Card.m_Title, Previous.m_Title);
			EXPECT_EQ(Card.m_Description, Previous.m_Description);
			EXPECT_STREQ(Card.m_Target.m_pStableId, Previous.m_Target.m_pStableId);
			if(Previous.m_Target.m_pTab == nullptr)
				EXPECT_EQ(Card.m_Target.m_pTab, nullptr);
			else
				EXPECT_STREQ(Card.m_Target.m_pTab, Expected.m_vNavigationTabs[Index].c_str());
			EXPECT_EQ(Card.m_vMatches, Previous.m_vMatches);
			EXPECT_EQ(Card.m_vMatchKeys, Previous.m_vMatchKeys);
		}
	}

	void ExpectModelValues(const TModel &Model, const SModelObservation &Expected)
	{
		ASSERT_EQ(Model.Count(), static_cast<int>(Expected.m_vEntries.size()));
		for(int Index = 0; Index < Model.Count(); ++Index)
		{
			SCOPED_TRACE(Index);
			const auto &Entry = Model.Entry(Index);
			const auto &Previous = Expected.m_vEntries[Index];
			EXPECT_STREQ(Entry.m_pStableId, Previous.m_pStableId);
			if(Previous.m_pDefaultTab == nullptr)
				EXPECT_EQ(Entry.m_pDefaultTab, nullptr);
			else
				EXPECT_STREQ(Entry.m_pDefaultTab, Expected.m_vTabs[Index].c_str());
			EXPECT_EQ(Entry.m_Column, Previous.m_Column);
			EXPECT_EQ(Entry.m_OrderInColumn, Previous.m_OrderInColumn);
			EXPECT_EQ(Model.StateIndexForStableId(Previous.m_pStableId), Expected.m_vStateIndices[Index]);
		}
	}

	void ExpectCacheUnchanged(CSettingsGlobalSearchCache &Cache, const SCacheObservation &Expected)
	{
		ASSERT_TRUE(Cache.HasSnapshot());
		EXPECT_EQ(Cache.Revision(), Expected.m_Revision);
		const auto &Results = Cache.Results();
		EXPECT_EQ(&Results, Expected.m_pResults);
		EXPECT_EQ(Results.m_vAllVisibleCards.data(), Expected.m_pCards);
		EXPECT_EQ(Results.m_vAllVisibleCards.capacity(), Expected.m_CardCapacity);
		EXPECT_EQ(Results.m_Answers.m_vCandidates.data(), Expected.m_pCandidates);
		EXPECT_EQ(Results.m_Answers.m_vCandidates.capacity(), Expected.m_CandidateCapacity);
		ExpectResultValues(Results, Expected);
		ASSERT_EQ(Results.m_vAllVisibleCards.size(), Expected.m_vCards.size());
		for(size_t Index = 0; Index < Expected.m_vCards.size(); ++Index)
			EXPECT_EQ(Results.m_vAllVisibleCards[Index].m_Target.m_pTab, Expected.m_vCards[Index].m_Target.m_pTab);
		for(size_t Index = 0; Index < Expected.m_aModels.size(); ++Index)
		{
			SCOPED_TRACE(Index);
			const auto &Model = Cache.OrderModel(Index != 0);
			const auto &Previous = Expected.m_aModels[Index];
			EXPECT_EQ(&Model, Previous.m_pModel);
			EXPECT_EQ(Model.LayoutRevision(), Previous.m_LayoutRevision);
			EXPECT_EQ(Model.StateIndexRevision(), Previous.m_StateIndexRevision);
			EXPECT_EQ(Model.IsDirty(), Previous.m_Dirty);
			EXPECT_EQ(Model.Count() != 0 ? &Model.Entry(0) : nullptr, Previous.m_pEntries);
			ExpectModelValues(Model, Previous);
			ASSERT_EQ(Model.Count(), static_cast<int>(Previous.m_vEntries.size()));
			for(int EntryIndex = 0; EntryIndex < Model.Count(); ++EntryIndex)
				EXPECT_EQ(Model.Entry(EntryIndex).m_pDefaultTab, Previous.m_vEntries[EntryIndex].m_pDefaultTab);
		}
	}

	const TCard *FindCard(const SSettingsGlobalSearchResults &Results, const char *pStableId)
	{
		const auto &vCards = Results.m_vAllVisibleCards;
		const auto It = std::find_if(vCards.begin(), vCards.end(), [pStableId](const TCard &Card) {
			return Card.m_pStableId != nullptr && std::string(Card.m_pStableId) == pStableId;
		});
		return It != vCards.end() ? &*It : nullptr;
	}

	bool ContainsTab(const std::vector<TCard> &vCards, const char *pTab)
	{
		return std::any_of(vCards.begin(), vCards.end(), [pTab](const TCard &Card) {
			return Card.m_Target.m_pTab != nullptr && std::string(Card.m_Target.m_pTab) == pTab;
		});
	}

	void ExpectPublishedCardsInBothModels(CSettingsGlobalSearchCache &Cache)
	{
		const auto &Results = Cache.Results();
		for(bool MeasurementOnly : {false, true})
		{
			const auto &Model = Cache.OrderModel(MeasurementOnly);
			EXPECT_TRUE(Model.IsDirty());
			EXPECT_GE(Model.FindByStableId("deck:global-search-empty"), 0);
			const bool HasAnswers = !Results.m_Answers.m_vCandidates.empty();
			EXPECT_EQ(Model.FindByStableId(qm_card_catalog::SEARCH_ANSWER_CARD_ID) >= 0, HasAnswers);
			EXPECT_EQ(Model.Count(), static_cast<int>(Results.m_vAllVisibleCards.size()) + (HasAnswers ? 2 : 1));
			for(const auto &Card : Results.m_vAllVisibleCards)
			{
				const int Index = Model.FindByStableId(Card.m_pStableId);
				ASSERT_GE(Index, 0);
				EXPECT_STREQ(Model.Entry(Index).m_pDefaultTab, "global-search");
			}
		}
	}

	SUpdateAttempt UpdateWithFailure(size_t BeforeFailure, CSettingsGlobalSearchCache &Cache, const char *pQuery, const TModel &Source)
	{
		CPredictionAllocationFailure Failure(BeforeFailure);
		const bool Success = Cache.TryUpdate(pQuery, LANGUAGE_KEY, false, TEXT_GENERATION, Source);
		return {Success, Failure.Triggered(), Failure.Attempts()};
	}
}

TEST(SettingsGlobalSearchCache, FirstSnapshotFailureLeavesTheCacheEmptyAndCanRetry)
{
	TModel Source;
	Source.SetEntries(qm_card_registry::BuildDefaultEntries());
	CSettingsGlobalSearchCache Cache;
	EXPECT_FALSE(Cache.HasSnapshot());
	EXPECT_EQ(Cache.Revision(), 0u);
	const auto Attempt = UpdateWithFailure(0, Cache, NEW_QUERY, Source);
	ASSERT_TRUE(Attempt.m_Triggered);
	EXPECT_FALSE(Attempt.m_Success);
	EXPECT_FALSE(Cache.HasSnapshot());
	EXPECT_EQ(Cache.Revision(), 0u);
	ASSERT_TRUE(Cache.TryUpdate(NEW_QUERY, LANGUAGE_KEY, false, TEXT_GENERATION, Source));
	ASSERT_TRUE(Cache.HasSnapshot());
	EXPECT_EQ(Cache.Revision(), 1u);
	ASSERT_NE(Cache.Results().m_Answers.m_pAnswer, nullptr);
	EXPECT_STREQ(Cache.Results().m_Answers.m_pAnswer->m_pId, "voice-distance");
	ExpectPublishedCardsInBothModels(Cache);
}

TEST(SettingsGlobalSearchCache, CompletePublicationKeepsBothModelObjectsAndAdvancesTheirOwnVersions)
{
	TModel Source;
	Source.SetEntries(qm_card_registry::BuildDefaultEntries());
	CSettingsGlobalSearchCache Cache;
	ASSERT_TRUE(Cache.TryUpdate("我身上的十字架怎么关？", LANGUAGE_KEY, false, TEXT_GENERATION, Source));
	ASSERT_NE(Cache.Results().m_Answers.m_pAnswer, nullptr);
	EXPECT_STREQ(Cache.Results().m_Answers.m_pAnswer->m_pId, "hitbox");
	ASSERT_NE(FindCard(Cache.Results(), "qm:collision_hitbox"), nullptr);
	ExpectPublishedCardsInBothModels(Cache);
	auto &Rendered = Cache.OrderModel(false);
	auto &Measured = Cache.OrderModel(true);
	ASSERT_NE(&Rendered, &Measured);
	Rendered.Move("qm:collision_hitbox", 2, 0);
	Measured.BuildStateIndex();
	const auto Before = ObserveCache(Cache);
	ASSERT_TRUE(Cache.TryUpdate(NEW_QUERY, LANGUAGE_KEY, false, TEXT_GENERATION, Source));
	EXPECT_EQ(&Cache.Results(), Before.m_pResults);
	EXPECT_EQ(&Cache.OrderModel(false), &Rendered);
	EXPECT_EQ(&Cache.OrderModel(true), &Measured);
	EXPECT_EQ(Cache.Revision(), Before.m_Revision + 1);
	for(size_t Index = 0; Index < Before.m_aModels.size(); ++Index)
	{
		const auto &Model = Cache.OrderModel(Index != 0);
		EXPECT_EQ(Model.LayoutRevision(), Before.m_aModels[Index].m_LayoutRevision + 1);
		EXPECT_EQ(Model.StateIndexRevision(), Before.m_aModels[Index].m_StateIndexRevision + 1);
		EXPECT_EQ(Model.FindByStableId("qm:collision_hitbox"), -1);
	}
	ASSERT_NE(Cache.Results().m_Answers.m_pAnswer, nullptr);
	EXPECT_STREQ(Cache.Results().m_Answers.m_pAnswer->m_pId, "voice-distance");
	ASSERT_NE(FindCard(Cache.Results(), "qm:voice"), nullptr);
	ExpectPublishedCardsInBothModels(Cache);
}

TEST(SettingsGlobalSearchCache, NavigationOwnsShortAndLongTabsAcrossSourceRebuildAndDestruction)
{
	CSettingsGlobalSearchCache Cache;
	const std::string LongTab(160, 'l');
	const char *pPublishedShort = nullptr;
	const char *pPublishedLong = nullptr;
	{
		TModel Source;
		Source.SetEntries(qm_card_registry::BuildDefaultEntries());
		Source.MoveToTab("deck:sound-toggle", "hud", 1, 0);
		Source.MoveToTab("deck:sound-volume", LongTab.c_str(), 2, 0);
		ASSERT_TRUE(Cache.TryUpdate(OLD_QUERY, LANGUAGE_KEY, false, TEXT_GENERATION, Source));
		const auto *pShort = FindCard(Cache.Results(), "deck:sound-toggle");
		const auto *pLong = FindCard(Cache.Results(), "deck:sound-volume");
		ASSERT_NE(pShort, nullptr);
		ASSERT_NE(pLong, nullptr);
		const int ShortIndex = Source.FindByStableId("deck:sound-toggle");
		const int LongIndex = Source.FindByStableId("deck:sound-volume");
		ASSERT_GE(ShortIndex, 0);
		ASSERT_GE(LongIndex, 0);
		ASSERT_NE(pShort->m_Target.m_pTab, Source.Entry(ShortIndex).m_pDefaultTab);
		ASSERT_NE(pLong->m_Target.m_pTab, Source.Entry(LongIndex).m_pDefaultTab);
		pPublishedShort = pShort->m_Target.m_pTab;
		pPublishedLong = pLong->m_Target.m_pTab;
		EXPECT_STREQ(pPublishedShort, "hud");
		EXPECT_STREQ(pPublishedLong, LongTab.c_str());
		Source.SetEntries({{"qm:voice", "replacement", 1, 0}});
		EXPECT_STREQ(pPublishedShort, "hud");
		EXPECT_STREQ(pPublishedLong, LongTab.c_str());
	}
	EXPECT_STREQ(pPublishedShort, "hud");
	EXPECT_STREQ(pPublishedLong, LongTab.c_str());
	EXPECT_EQ(FindCard(Cache.Results(), "deck:sound-toggle")->m_Target.m_pTab, pPublishedShort);
	EXPECT_EQ(FindCard(Cache.Results(), "deck:sound-volume")->m_Target.m_pTab, pPublishedLong);
	ExpectPublishedCardsInBothModels(Cache);
}

TEST(SettingsGlobalSearchCache, EqualKeysReuseThePublishedSnapshotWithoutAllocating)
{
	TModel Source;
	Source.SetEntries(qm_card_registry::BuildDefaultEntries());
	CSettingsGlobalSearchCache Cache;
	ASSERT_TRUE(Cache.TryUpdate(NEW_QUERY, LANGUAGE_KEY, false, TEXT_GENERATION, Source));
	const auto Before = ObserveCache(Cache);
	const std::string SameQuery = NEW_QUERY;
	const std::string SameLanguage = LANGUAGE_KEY;
	bool Success = false;
	bool Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Success = Cache.TryUpdate(SameQuery.c_str(), SameLanguage.c_str(), false, TEXT_GENERATION, Source);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Success);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	ExpectCacheUnchanged(Cache, Before);
}

TEST(SettingsGlobalSearchCache, EmptyAndNullQueriesClearPublishedContentAndShareTheSameKey)
{
	TModel Source;
	Source.SetEntries(qm_card_registry::BuildDefaultEntries());
	CSettingsGlobalSearchCache Cache;
	ASSERT_TRUE(Cache.TryUpdate(NEW_QUERY, LANGUAGE_KEY, false, TEXT_GENERATION, Source));
	const auto Revision = Cache.Revision();
	ASSERT_TRUE(Cache.TryUpdate(nullptr, nullptr, false, TEXT_GENERATION, Source));
	EXPECT_EQ(Cache.Revision(), Revision + 1);
	EXPECT_EQ(Cache.Results().m_Answers.m_pAnswer, nullptr);
	EXPECT_TRUE(Cache.Results().m_Answers.m_vCandidates.empty());
	EXPECT_TRUE(Cache.Results().m_vAllVisibleCards.empty());
	ExpectPublishedCardsInBothModels(Cache);
	const auto Before = ObserveCache(Cache);
	bool Success = false;
	bool Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Success = Cache.TryUpdate("", "", false, TEXT_GENERATION, Source);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Success);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	ExpectCacheUnchanged(Cache, Before);
	ASSERT_TRUE(Cache.TryUpdate(NEW_QUERY, LANGUAGE_KEY, false, TEXT_GENERATION, Source));
	ASSERT_NE(Cache.Results().m_Answers.m_pAnswer, nullptr);
	EXPECT_STREQ(Cache.Results().m_Answers.m_pAnswer->m_pId, "voice-distance");
	ExpectPublishedCardsInBothModels(Cache);
}

TEST(SettingsGlobalSearchCache, SixupChangesVisibleSkinCardsAndTheSearchPageStaysHidden)
{
	TModel Source;
	Source.SetEntries(qm_card_registry::BuildDefaultEntries());
	const auto Unfiltered = qm_card_registry::SearchCards("skin", Source);
	ASSERT_TRUE(ContainsTab(Unfiltered, "tee"));
	ASSERT_TRUE(ContainsTab(Unfiltered, "tee7"));
	CSettingsGlobalSearchCache Cache;
	ASSERT_TRUE(Cache.TryUpdate("skin", LANGUAGE_KEY, false, TEXT_GENERATION, Source));
	EXPECT_TRUE(ContainsTab(Cache.Results().m_vAllVisibleCards, "tee"));
	EXPECT_FALSE(ContainsTab(Cache.Results().m_vAllVisibleCards, "tee7"));
	const auto Revision = Cache.Revision();
	ASSERT_TRUE(Cache.TryUpdate("skin", LANGUAGE_KEY, true, TEXT_GENERATION, Source));
	EXPECT_EQ(Cache.Revision(), Revision + 1);
	EXPECT_FALSE(ContainsTab(Cache.Results().m_vAllVisibleCards, "tee"));
	EXPECT_TRUE(ContainsTab(Cache.Results().m_vAllVisibleCards, "tee7"));
	ExpectPublishedCardsInBothModels(Cache);
	ASSERT_TRUE(ContainsTab(qm_card_registry::SearchCards("global", Source), "global-search"));
	ASSERT_TRUE(Cache.TryUpdate("global", LANGUAGE_KEY, true, TEXT_GENERATION, Source));
	EXPECT_FALSE(ContainsTab(Cache.Results().m_vAllVisibleCards, "global-search"));
	ExpectPublishedCardsInBothModels(Cache);
}

TEST(SettingsGlobalSearchCache, LanguageTextGenerationAndSourceLayoutEachInvalidateTheKey)
{
	TModel Source;
	Source.SetEntries(qm_card_registry::BuildDefaultEntries());
	CSettingsGlobalSearchCache Cache;
	ASSERT_TRUE(Cache.TryUpdate("截图", "english-key", false, 10, Source));
	const auto Before = ObserveCache(Cache);
	ASSERT_TRUE(Cache.TryUpdate("截图", "chinese-key", false, 10, Source));
	EXPECT_EQ(Cache.Revision(), Before.m_Revision + 1);
	ASSERT_TRUE(Cache.TryUpdate("截图", "chinese-key", false, 11, Source));
	EXPECT_EQ(Cache.Revision(), Before.m_Revision + 2);
	Source.MoveToTab("deck:general-recording", "hud", 2, 0);
	ASSERT_TRUE(Cache.TryUpdate("截图", "chinese-key", false, 11, Source));
	EXPECT_EQ(Cache.Revision(), Before.m_Revision + 3);
	const auto *pRecording = FindCard(Cache.Results(), "deck:general-recording");
	ASSERT_NE(pRecording, nullptr);
	EXPECT_STREQ(pRecording->m_Target.m_pTab, "hud");
	for(size_t Index = 0; Index < Before.m_aModels.size(); ++Index)
	{
		const auto &Model = Cache.OrderModel(Index != 0);
		EXPECT_EQ(&Model, Before.m_aModels[Index].m_pModel);
		EXPECT_EQ(Model.LayoutRevision(), Before.m_aModels[Index].m_LayoutRevision + 3);
		EXPECT_EQ(Model.StateIndexRevision(), Before.m_aModels[Index].m_StateIndexRevision + 3);
	}
	ExpectPublishedCardsInBothModels(Cache);
}

TEST(SettingsGlobalSearchCache, EveryRejectedReplacementPreservesTheWholeSnapshotAndTheSameRequestCanRetry)
{
	TModel Source;
	Source.SetEntries(qm_card_registry::BuildDefaultEntries());
	Source.MoveToTab("deck:sound-toggle", "hud", 1, 0);
	const std::string LongTab(160, 'l');
	Source.MoveToTab("deck:sound-volume", LongTab.c_str(), 2, 0);
	CSettingsGlobalSearchCache Trace;
	ASSERT_TRUE(Trace.TryUpdate(NEW_QUERY, LANGUAGE_KEY, false, TEXT_GENERATION, Source));
	ASSERT_TRUE(Trace.TryUpdate(OLD_QUERY, LANGUAGE_KEY, false, TEXT_GENERATION, Source));
	const auto Traced = UpdateWithFailure(std::numeric_limits<size_t>::max(), Trace, NEW_QUERY, Source);
	ASSERT_TRUE(Traced.m_Success);
	ASSERT_FALSE(Traced.m_Triggered);
	ASSERT_GT(Traced.m_Attempts, 0u);
	ASSERT_LE(Traced.m_Attempts, 8192u);
	const auto ExpectedNew = ObserveCache(Trace);
	size_t Rejected = 0;
	for(size_t Allocation = 0; Allocation < Traced.m_Attempts; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		CSettingsGlobalSearchCache Cache;
		ASSERT_TRUE(Cache.TryUpdate(OLD_QUERY, LANGUAGE_KEY, false, TEXT_GENERATION, Source));
		Cache.OrderModel(false).ClearDirty();
		Cache.OrderModel(true).BuildStateIndex();
		const auto Before = ObserveCache(Cache);
		const auto Attempt = UpdateWithFailure(Allocation, Cache, NEW_QUERY, Source);
		ASSERT_TRUE(Attempt.m_Triggered);
		if(!Attempt.m_Success)
		{
			++Rejected;
			ExpectCacheUnchanged(Cache, Before);
			EXPECT_EQ(Cache.OrderModel(false).FindByStableId("qm:voice"), -1);
			EXPECT_EQ(Cache.OrderModel(true).FindByStableId("qm:voice"), -1);
		}
		else
		{
			// stable_sort 可能拒收临时缓冲后原地成功，仍必须发布完整候选。
			ExpectResultValues(Cache.Results(), ExpectedNew);
			ExpectPublishedCardsInBothModels(Cache);
		}
		ASSERT_TRUE(Cache.TryUpdate(NEW_QUERY, LANGUAGE_KEY, false, TEXT_GENERATION, Source));
		EXPECT_EQ(Cache.Revision(), Before.m_Revision + 1);
		EXPECT_EQ(&Cache.Results(), Before.m_pResults);
		ExpectResultValues(Cache.Results(), ExpectedNew);
		for(size_t Index = 0; Index < Before.m_aModels.size(); ++Index)
		{
			const auto &Model = Cache.OrderModel(Index != 0);
			EXPECT_EQ(&Model, Before.m_aModels[Index].m_pModel);
			EXPECT_EQ(Model.LayoutRevision(), Before.m_aModels[Index].m_LayoutRevision + 1);
			EXPECT_EQ(Model.StateIndexRevision(), Before.m_aModels[Index].m_StateIndexRevision + 1);
			ExpectModelValues(Model, ExpectedNew.m_aModels[Index]);
		}
		ExpectPublishedCardsInBothModels(Cache);
	}
	EXPECT_GT(Rejected, 0u);
}
