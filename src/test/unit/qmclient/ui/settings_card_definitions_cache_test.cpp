#include <game/client/QmUi/SettingsCardDeck.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using EPrepareResult = CSettingsCardDefinitionsCache::EPrepareResult;

	void AddOwnedCard(std::vector<SSettingsCardDefinition> &vCards, const char *pStableId, const std::shared_ptr<bool> &Owner)
	{
		SSettingsCardDefinition Card;
		Card.m_Spec = {pStableId, "Title", "Description"};
		Card.m_IsVisible = [Owner] { return *Owner; };
		vCards.push_back(std::move(Card));
	}
}

TEST(SettingsCardDefinitionsCache, ExpectedBuilderFailuresPreserveOldCallbacksAndRetryTheUnpublishedKey)
{
	for(const bool LengthFailure : {false, true})
	{
		SCOPED_TRACE(LengthFailure);
		CSettingsCardDefinitionsCache Cache;
		auto Owner = std::make_shared<bool>(true);
		const std::weak_ptr<bool> OldOwner = Owner;
		ASSERT_EQ(Cache.TryPrepare("old", 1, ESettingsCardRenderPass::INTERACTIVE, [&](auto &vCards) { AddOwnedCard(vCards, "old-card", Owner); }), EPrepareResult::PREPARED);
		Owner.reset();
		const auto *pOldDefinitions = Cache.Definitions().data();
		int FailedBuilds = 0;
		EXPECT_EQ(Cache.TryPrepare("pending", 2, ESettingsCardRenderPass::BACKGROUND, [&](auto &vCards) {
			++FailedBuilds;
			AddOwnedCard(vCards, "partial-card", std::make_shared<bool>(false));
			if(LengthFailure)
				throw std::length_error("controlled definition limit");
			throw std::bad_alloc();
		}),
			EPrepareResult::DEFERRED);
		ASSERT_EQ(Cache.Definitions().size(), 1u);
		EXPECT_EQ(Cache.Definitions().data(), pOldDefinitions);
		EXPECT_STREQ(Cache.Definitions().front().m_Spec.m_pStableId, "old-card");
		EXPECT_TRUE(Cache.Definitions().front().m_IsVisible());
		EXPECT_FALSE(OldOwner.expired());
		EXPECT_EQ(Cache.TryPrepare("old", 1, ESettingsCardRenderPass::INTERACTIVE, [&](auto &) { ++FailedBuilds; }), EPrepareResult::UNCHANGED);
		EXPECT_EQ(FailedBuilds, 1);
		EXPECT_EQ(Cache.TryPrepare("pending", 2, ESettingsCardRenderPass::BACKGROUND, [&](auto &vCards) {
			++FailedBuilds;
			AddOwnedCard(vCards, "ready-card", std::make_shared<bool>(false));
		}),
			EPrepareResult::PREPARED);
		ASSERT_EQ(Cache.Definitions().size(), 1u);
		EXPECT_STREQ(Cache.Definitions().front().m_Spec.m_pStableId, "ready-card");
		EXPECT_FALSE(Cache.Definitions().front().m_IsVisible());
		EXPECT_TRUE(OldOwner.expired());
		EXPECT_EQ(FailedBuilds, 2);
	}
}

TEST(SettingsCardDefinitionsCache, UnknownBuilderExceptionPropagatesWithoutReplacingOldContentsOrKey)
{
	CSettingsCardDefinitionsCache Cache;
	const auto Owner = std::make_shared<bool>(true);
	ASSERT_EQ(Cache.TryPrepare("old", 1, ESettingsCardRenderPass::INTERACTIVE, [&](auto &vCards) { AddOwnedCard(vCards, "old-card", Owner); }), EPrepareResult::PREPARED);
	const auto *pOldDefinitions = Cache.Definitions().data();
	EXPECT_THROW(Cache.TryPrepare("pending", 2, ESettingsCardRenderPass::MEASUREMENT, [&](auto &vCards) {
		AddOwnedCard(vCards, "partial-card", std::make_shared<bool>(false));
		throw std::logic_error("unrelated definition failure");
	}),
		std::logic_error);
	ASSERT_EQ(Cache.Definitions().size(), 1u);
	EXPECT_EQ(Cache.Definitions().data(), pOldDefinitions);
	EXPECT_STREQ(Cache.Definitions().front().m_Spec.m_pStableId, "old-card");
	EXPECT_TRUE(Cache.Definitions().front().m_IsVisible());
	bool RebuiltOldKey = false;
	EXPECT_EQ(Cache.TryPrepare("old", 1, ESettingsCardRenderPass::INTERACTIVE, [&](auto &) { RebuiltOldKey = true; }), EPrepareResult::UNCHANGED);
	EXPECT_FALSE(RebuiltOldKey);
}

TEST(SettingsCardDefinitionsCache, OnlyTabRevisionOrRenderPassChangesRebuildDefinitions)
{
	CSettingsCardDefinitionsCache Cache;
	int Builds = 0;
	const auto Build = [&](auto &vCards) {
		++Builds;
		AddOwnedCard(vCards, "card", std::make_shared<bool>(true));
	};
	EXPECT_EQ(Cache.TryPrepare("settings", 7, ESettingsCardRenderPass::INTERACTIVE, Build), EPrepareResult::PREPARED);
	const std::string SameTab = "settings";
	EXPECT_EQ(Cache.TryPrepare(SameTab.c_str(), 7, ESettingsCardRenderPass::INTERACTIVE, Build), EPrepareResult::UNCHANGED);
	EXPECT_EQ(Builds, 1);
	EXPECT_EQ(Cache.TryPrepare("search", 7, ESettingsCardRenderPass::INTERACTIVE, Build), EPrepareResult::PREPARED);
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(Cache.TryPrepare("search", 8, ESettingsCardRenderPass::INTERACTIVE, Build), EPrepareResult::PREPARED);
	EXPECT_EQ(Builds, 3);
	EXPECT_EQ(Cache.TryPrepare("search", 8, ESettingsCardRenderPass::BACKGROUND, Build), EPrepareResult::PREPARED);
	EXPECT_EQ(Builds, 4);
	EXPECT_EQ(Cache.TryPrepare("search", 8, ESettingsCardRenderPass::MEASUREMENT, Build), EPrepareResult::PREPARED);
	EXPECT_EQ(Cache.TryPrepare("search", 8, ESettingsCardRenderPass::MEASUREMENT, Build), EPrepareResult::UNCHANGED);
	EXPECT_EQ(Builds, 5);
	EXPECT_EQ(Cache.TryPrepare("search", 8, ESettingsCardRenderPass::INTERACTIVE, Build), EPrepareResult::PREPARED);
	EXPECT_EQ(Builds, 6);
}

TEST(SettingsCardDefinitionsCache, NativeTabAllocationFailureDoesNotCallBuilderOrRetireOldCallbacks)
{
	CSettingsCardDefinitionsCache Cache;
	auto Owner = std::make_shared<bool>(true);
	const std::weak_ptr<bool> OldOwner = Owner;
	ASSERT_EQ(Cache.TryPrepare("old", 1, ESettingsCardRenderPass::INTERACTIVE, [&](auto &vCards) { AddOwnedCard(vCards, "old-card", Owner); }), EPrepareResult::PREPARED);
	Owner.reset();
	const auto *pOldDefinitions = Cache.Definitions().data();
	const std::string LongTab(512, 't');
	bool BuilderCalled = false;
	bool Triggered = false;
	EPrepareResult Prepared = EPrepareResult::UNCHANGED;
	{
		CPredictionAllocationFailure Failure;
		Prepared = Cache.TryPrepare(LongTab.c_str(), 2, ESettingsCardRenderPass::BACKGROUND, [&](auto &) { BuilderCalled = true; });
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_EQ(Prepared, EPrepareResult::DEFERRED);
	EXPECT_FALSE(BuilderCalled);
	ASSERT_EQ(Cache.Definitions().size(), 1u);
	EXPECT_EQ(Cache.Definitions().data(), pOldDefinitions);
	EXPECT_TRUE(Cache.Definitions().front().m_IsVisible());
	EXPECT_FALSE(OldOwner.expired());
	EXPECT_EQ(Cache.TryPrepare("old", 1, ESettingsCardRenderPass::INTERACTIVE, [&](auto &) { BuilderCalled = true; }), EPrepareResult::UNCHANGED);
	EXPECT_FALSE(BuilderCalled);
	EXPECT_EQ(Cache.TryPrepare(LongTab.c_str(), 2, ESettingsCardRenderPass::BACKGROUND, [&](auto &vCards) {
		BuilderCalled = true;
		AddOwnedCard(vCards, "ready-card", std::make_shared<bool>(false));
	}),
		EPrepareResult::PREPARED);
	EXPECT_TRUE(BuilderCalled);
	EXPECT_TRUE(OldOwner.expired());
	ASSERT_EQ(Cache.Definitions().size(), 1u);
	EXPECT_FALSE(Cache.Definitions().front().m_IsVisible());
}
