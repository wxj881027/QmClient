// 使用真实菜单条目与借用分配器观察候选发布，不启动私有菜单或图形设备。
#include <base/borrowed_allocator.h>
#include <base/system.h>

#include <game/client/components/menus.h>
#include <game/client/components/qmclient/settings_asset_search_view.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>

#include <cstdlib>
#include <deque>
#include <initializer_list>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	using TItem = CMenus::SCustomGame;
	using TOwned = std::vector<TItem, CBorrowedAllocator<TItem>>;
	using TView = std::vector<TItem *, CBorrowedAllocator<TItem *>>;
	using TReadyQueue = std::deque<SSettingsAssetPreviewHandle, CBorrowedAllocator<SSettingsAssetPreviewHandle>>;
	using TReadyMarkers = std::unordered_set<std::string, std::hash<std::string>, std::equal_to<std::string>, CBorrowedAllocator<std::string>>;

	void AddItems(TOwned &Items, std::initializer_list<const char *> Names)
	{
		Items.reserve(Items.size() + Names.size());
		for(const char *pName : Names)
		{
			Items.emplace_back();
			str_copy(Items.back().m_aName, pName);
			Items.back().m_PreviewResidentBytes = 77;
			Items.back().m_PreviewEpoch = 17;
		}
	}

	class SettingsAssetSearchView : public ::testing::Test
	{
	protected:
		CTrackedAllocationResource m_OwnedResource;
		CTrackedAllocationResource m_ViewResource;
		TOwned m_Owned{CBorrowedAllocator<TItem>(&m_OwnedResource)};
		TView m_View{CBorrowedAllocator<TItem *>(&m_ViewResource)};

		void SetUp() override
		{
			AddItems(m_Owned, {"first", "middle", "last"});
			m_View.push_back(&m_Owned[1]);
		}

		void TearDown() override
		{
			{
				TView Empty(m_View.get_allocator());
				m_View.swap(Empty);
			}
			{
				TOwned Empty(m_Owned.get_allocator());
				m_Owned.swap(Empty);
			}
			EXPECT_EQ(m_ViewResource.m_Live, 0);
			EXPECT_EQ(m_OwnedResource.m_Live, 0);
		}

		void ExpectOriginalSelection()
		{
			ASSERT_EQ(m_View.size(), 1u);
			EXPECT_EQ(m_View[0], &m_Owned[1]);
			EXPECT_STREQ(m_View[0]->m_aName, "middle");
			EXPECT_EQ(m_View.get_allocator().Resource(), &m_ViewResource);
			EXPECT_EQ(m_ViewResource.m_Live, 1);
		}

		SSettingsAssetPreviewHandle HandleFor(size_t Index)
		{
			return {ASSETS_TAB_GAME, m_Owned[Index].m_PreviewEpoch, Index, m_Owned[Index].m_aName};
		}
	};

	class SettingsAssetReadyPreview : public ::testing::Test
	{
	protected:
		CTrackedAllocationResource m_QueueResource;
		CTrackedAllocationResource m_MarkerResource;
		TReadyQueue m_Queue{CBorrowedAllocator<SSettingsAssetPreviewHandle>(&m_QueueResource)};
		TReadyMarkers m_Markers{0, std::hash<std::string>{}, std::equal_to<std::string>{}, CBorrowedAllocator<std::string>(&m_MarkerResource)};
		std::deque<TItem> m_Items;
		uint8_t *m_pCandidatePixels = nullptr;

		TItem &AddReadyItem(const char *pName, unsigned Epoch = 3, size_t Index = 99)
		{
			m_Items.emplace_back();
			TItem &Item = m_Items.back();
			str_copy(Item.m_aName, pName);
			Item.m_PreviewState = TItem::PREVIEW_STATE_READY;
			Item.m_PreviewEpoch = Epoch;
			Item.m_PreviewListIndex = Index;
			Item.m_PreviewBytes = 4;
			Item.m_PreviewImage.m_Width = 1;
			Item.m_PreviewImage.m_Height = 1;
			Item.m_PreviewImage.m_Format = CImageInfo::FORMAT_RGBA;
			Item.m_PreviewImage.m_pData = static_cast<uint8_t *>(std::calloc(4, 1));
			if(Item.m_PreviewImage.m_pData == nullptr)
				throw std::bad_alloc();
			Item.m_PreviewImage.m_pData[0] = 73;
			return Item;
		}

		void SetUp() override
		{
			TItem &Old = AddReadyItem("old", 5, 0);
			ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Old, ASSETS_TAB_GAME, 5, 0, false, m_Queue, m_Markers));
			m_pCandidatePixels = AddReadyItem("candidate").m_PreviewImage.m_pData;
		}

		TItem &Candidate() { return m_Items[1]; }

		void ExpectUnchangedCandidate()
		{
			EXPECT_EQ(Candidate().m_PreviewEpoch, 3u);
			EXPECT_EQ(Candidate().m_PreviewListIndex, 99u);
			EXPECT_FALSE(Candidate().m_PreviewHighPriority);
			EXPECT_EQ(Candidate().m_PreviewState, TItem::PREVIEW_STATE_READY);
			EXPECT_EQ(Candidate().m_PreviewBytes, 4u);
			EXPECT_EQ(Candidate().m_PreviewImage.m_pData, m_pCandidatePixels);
			EXPECT_EQ(Candidate().m_PreviewImage.m_pData[0], 73);
			ASSERT_EQ(m_Queue.size(), 1u);
			EXPECT_EQ(m_Queue.front().m_Name, "old");
			EXPECT_EQ(m_Markers.size(), 1u);
		}
	};
}

TEST_F(SettingsAssetSearchView, SelectsCurrentOwnedPointersInOwnerOrder)
{
	const int OwnedAllocations = m_OwnedResource.m_Allocations;
	const int ViewAllocations = m_ViewResource.m_Allocations;
	ASSERT_TRUE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &Item) {
		return str_comp(Item.m_aName, "middle") != 0;
	}));
	ASSERT_EQ(m_View.size(), 2u);
	EXPECT_EQ(m_View[0], &m_Owned[0]);
	EXPECT_EQ(m_View[1], &m_Owned[2]);
	EXPECT_EQ(m_View.get_allocator().Resource(), &m_ViewResource);
	EXPECT_EQ(m_ViewResource.m_Allocations, ViewAllocations + 1);
	EXPECT_EQ(m_ViewResource.m_Live, 1);
	EXPECT_EQ(m_OwnedResource.m_Allocations, OwnedAllocations);
	EXPECT_EQ(m_Owned[1].m_PreviewResidentBytes, 77u);
}

TEST_F(SettingsAssetSearchView, CandidateAllocationFailureKeepsSameOwnerViewAndCanRetry)
{
	auto **pPublishedStorage = m_View.data();
	const size_t PublishedCapacity = m_View.capacity();
	m_ViewResource.m_FailAt = m_ViewResource.m_Allocations + 1;
	int FilterCalls = 0;
	EXPECT_FALSE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [&](const TItem &) {
		++FilterCalls;
		return true;
	}));
	ExpectOriginalSelection();
	EXPECT_EQ(m_View.data(), pPublishedStorage);
	EXPECT_EQ(m_View.capacity(), PublishedCapacity);
	EXPECT_EQ(FilterCalls, 0);

	m_ViewResource.m_FailAt = -1;
	ASSERT_TRUE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &) { return true; }));
	ASSERT_EQ(m_View.size(), 3u);
	EXPECT_EQ(m_View[0], &m_Owned[0]);
	EXPECT_EQ(m_View[2], &m_Owned[2]);
	EXPECT_EQ(m_ViewResource.m_Live, 1);
}

TEST_F(SettingsAssetSearchView, NullAllocationFailureKeepsSameOwnerView)
{
	m_ViewResource.m_FailAt = m_ViewResource.m_Allocations + 1;
	m_ViewResource.m_ReturnNull = true;
	EXPECT_FALSE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &) { return true; }));
	ExpectOriginalSelection();
}

TEST_F(SettingsAssetSearchView, FilterBadAllocDoesNotPublishPartialSelectionAndCanRetry)
{
	int FilterCalls = 0;
	EXPECT_FALSE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [&](const TItem &) {
		if(++FilterCalls == 2)
			throw std::bad_alloc();
		return true;
	}));
	EXPECT_EQ(FilterCalls, 2);
	ExpectOriginalSelection();

	ASSERT_TRUE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &Item) {
		return str_comp(Item.m_aName, "last") == 0;
	}));
	ASSERT_EQ(m_View.size(), 1u);
	EXPECT_EQ(m_View[0], &m_Owned[2]);
	EXPECT_EQ(m_ViewResource.m_Live, 1);
}

TEST_F(SettingsAssetSearchView, FilterLengthErrorKeepsSameOwnerViewAndReleasesCandidate)
{
	int FilterCalls = 0;
	EXPECT_FALSE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [&](const TItem &) {
		if(++FilterCalls == 2)
			throw std::length_error("filter capacity exhausted");
		return true;
	}));
	EXPECT_EQ(FilterCalls, 2);
	ExpectOriginalSelection();
}

TEST_F(SettingsAssetSearchView, SuccessfulEmptySelectionClearsPreviousView)
{
	ASSERT_TRUE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &) { return false; }));
	EXPECT_TRUE(m_View.empty());
	EXPECT_EQ(m_ViewResource.m_Live, 1);
	ASSERT_EQ(m_Owned.size(), 3u);
	EXPECT_STREQ(m_Owned[1].m_aName, "middle");
}

TEST_F(SettingsAssetSearchView, EmptyOwnerNeedsNoAllocationOrFilter)
{
	// 调用方先撤回借用，再改变owner；传入接口时没有失效指针。
	m_View.clear();
	m_Owned.clear();
	const int ViewAllocations = m_ViewResource.m_Allocations;
	m_ViewResource.m_FailAt = ViewAllocations + 1;
	int FilterCalls = 0;
	ASSERT_TRUE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [&](const TItem &) {
		++FilterCalls;
		return true;
	}));
	EXPECT_TRUE(m_View.empty());
	EXPECT_EQ(m_ViewResource.m_Allocations, ViewAllocations);
	EXPECT_EQ(m_ViewResource.m_Live, 0);
	EXPECT_EQ(FilterCalls, 0);
}

TEST_F(SettingsAssetSearchView, RepeatedBuildReplacesViewWithoutAppendingDuplicates)
{
	for(int Repeat = 0; Repeat < 2; ++Repeat)
	{
		SCOPED_TRACE(Repeat);
		ASSERT_TRUE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &) { return true; }));
		ASSERT_EQ(m_View.size(), 3u);
		EXPECT_EQ(m_View[0], &m_Owned[0]);
		EXPECT_EQ(m_View[1], &m_Owned[1]);
		EXPECT_EQ(m_View[2], &m_Owned[2]);
		EXPECT_EQ(m_ViewResource.m_Live, 1);
	}
}

TEST_F(SettingsAssetSearchView, ClearedViewAfterOwnerSwapStaysSafeAcrossFailureAndRetry)
{
	{
		TOwned Replacement(m_Owned.get_allocator());
		AddItems(Replacement, {"replacement-first", "replacement-last"});
		m_View.clear();
		m_Owned.swap(Replacement);
	}
	// 旧条目及其存储已销毁，只将空视图和当前owner传入生产接口。
	m_ViewResource.m_FailAt = m_ViewResource.m_Allocations + 1;
	EXPECT_FALSE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &) { return true; }));
	EXPECT_TRUE(m_View.empty());

	m_ViewResource.m_FailAt = -1;
	ASSERT_TRUE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &) { return true; }));
	ASSERT_EQ(m_View.size(), 2u);
	EXPECT_EQ(m_View[0], &m_Owned[0]);
	EXPECT_EQ(m_View[1], &m_Owned[1]);
	EXPECT_STREQ(m_View[0]->m_aName, "replacement-first");
}

TEST_F(SettingsAssetSearchView, CallerClearAlsoProtectsInPlaceOwnerReplacement)
{
	const size_t PreviousCapacity = m_Owned.capacity();
	m_View.clear();
	m_Owned.clear();
	AddItems(m_Owned, {"new-at-reused-storage"});
	ASSERT_EQ(m_Owned.capacity(), PreviousCapacity);
	m_ViewResource.m_FailAt = m_ViewResource.m_Allocations + 1;
	EXPECT_FALSE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &) { return true; }));
	EXPECT_TRUE(m_View.empty());

	m_ViewResource.m_FailAt = -1;
	ASSERT_TRUE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &) { return true; }));
	ASSERT_EQ(m_View.size(), 1u);
	EXPECT_EQ(m_View[0], &m_Owned[0]);
	EXPECT_STREQ(m_View[0]->m_aName, "new-at-reused-storage");
}

TEST_F(SettingsAssetSearchView, OwnerLookupIgnoresSuccessfulEmptyFilteredView)
{
	const auto Handle = HandleFor(2);
	ASSERT_TRUE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &) { return false; }));
	ASSERT_TRUE(m_View.empty());
	EXPECT_EQ(FindSettingsAssetPreviewOwner(m_Owned, ASSETS_TAB_GAME, Handle), &m_Owned[2]);
	EXPECT_TRUE(m_View.empty());
}

TEST_F(SettingsAssetSearchView, OwnerLookupFindsItemExcludedFromPublishedView)
{
	const auto Handle = HandleFor(0);
	ExpectOriginalSelection();
	EXPECT_EQ(FindSettingsAssetPreviewOwner(m_Owned, ASSETS_TAB_GAME, Handle), &m_Owned[0]);
	ExpectOriginalSelection();
}

TEST_F(SettingsAssetSearchView, OwnerLookupStillWorksAfterViewCandidateAllocationFailure)
{
	const auto Handle = HandleFor(2);
	m_ViewResource.m_FailAt = m_ViewResource.m_Allocations + 1;
	ASSERT_FALSE(TryBuildSettingsAssetSearchView(m_View, m_Owned, [](const TItem &) { return true; }));
	ExpectOriginalSelection();
	EXPECT_EQ(FindSettingsAssetPreviewOwner(m_Owned, ASSETS_TAB_GAME, Handle), &m_Owned[2]);
}

TEST_F(SettingsAssetSearchView, OwnerLookupFallsBackToNameAndEpochAfterReordering)
{
	const auto Handle = HandleFor(1);
	m_View.clear();
	std::swap(m_Owned[0], m_Owned[1]);
	EXPECT_EQ(FindSettingsAssetPreviewOwner(m_Owned, ASSETS_TAB_GAME, Handle), &m_Owned[0]);
	EXPECT_STREQ(m_Owned[0].m_aName, "middle");
}

TEST_F(SettingsAssetSearchView, OwnerLookupFallsBackWhenOldIndexIsOutOfRange)
{
	auto Handle = HandleFor(2);
	Handle.m_Index = std::numeric_limits<size_t>::max();
	EXPECT_EQ(FindSettingsAssetPreviewOwner(m_Owned, ASSETS_TAB_GAME, Handle), &m_Owned[2]);
}

TEST_F(SettingsAssetSearchView, OwnerLookupRejectsMatchingNameFromAnotherEpoch)
{
	const auto Handle = HandleFor(1);
	++m_Owned[1].m_PreviewEpoch;
	EXPECT_EQ(FindSettingsAssetPreviewOwner(m_Owned, ASSETS_TAB_GAME, Handle), nullptr);
}

TEST_F(SettingsAssetSearchView, OwnerLookupRejectsAnotherNameAtTheHintedIndex)
{
	auto Handle = HandleFor(1);
	Handle.m_Name = "not-in-this-owner";
	EXPECT_EQ(FindSettingsAssetPreviewOwner(m_Owned, ASSETS_TAB_GAME, Handle), nullptr);
}

TEST_F(SettingsAssetSearchView, OwnerLookupRejectsAnotherTabWithMatchingNameAndEpoch)
{
	auto Handle = HandleFor(1);
	Handle.m_Tab = ASSETS_TAB_HUD;
	EXPECT_EQ(FindSettingsAssetPreviewOwner(m_Owned, ASSETS_TAB_GAME, Handle), nullptr);
}

TEST_F(SettingsAssetSearchView, OwnerLookupDoesNotAllocateWhenBorrowedResourcesReject)
{
	const auto Handle = HandleFor(1);
	auto MissingHandle = Handle;
	MissingHandle.m_Name = "missing";
	const int OwnedAllocations = m_OwnedResource.m_Allocations;
	const int ViewAllocations = m_ViewResource.m_Allocations;
	m_OwnedResource.m_FailAt = OwnedAllocations + 1;
	m_ViewResource.m_FailAt = ViewAllocations + 1;
	EXPECT_EQ(FindSettingsAssetPreviewOwner(m_Owned, ASSETS_TAB_GAME, Handle), &m_Owned[1]);
	EXPECT_EQ(FindSettingsAssetPreviewOwner(m_Owned, ASSETS_TAB_GAME, MissingHandle), nullptr);

	m_View.clear();
	m_Owned.clear();
	EXPECT_EQ(FindSettingsAssetPreviewOwner(m_Owned, ASSETS_TAB_GAME, Handle), nullptr);
	EXPECT_EQ(m_OwnedResource.m_Allocations, OwnedAllocations);
	EXPECT_EQ(m_ViewResource.m_Allocations, ViewAllocations);
}

TEST(SettingsAssetSearchViewDefault, EntityItemsWorkWithStandardOwnerAndViewAllocators)
{
	std::vector<CMenus::SCustomEntities> Owned(3);
	str_copy(Owned[0].m_aName, "alpha");
	str_copy(Owned[1].m_aName, "beta");
	str_copy(Owned[2].m_aName, "alpha-second");
	std::vector<CMenus::SCustomEntities *> View;
	ASSERT_TRUE(TryBuildSettingsAssetSearchView(View, Owned, [](const CMenus::SCustomEntities &Item) {
		return str_utf8_find_nocase(Item.m_aName, "ALPHA") != nullptr;
	}));
	ASSERT_EQ(View.size(), 2u);
	EXPECT_EQ(View[0], &Owned[0]);
	EXPECT_EQ(View[1], &Owned[2]);
}

TEST_F(SettingsAssetReadyPreview, CrossEpochAdmissionCommitsFieldsAndKeepsPixelOwner)
{
	ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 7, true, m_Queue, m_Markers));
	EXPECT_EQ(Candidate().m_PreviewEpoch, 11u);
	EXPECT_EQ(Candidate().m_PreviewListIndex, 7u);
	EXPECT_TRUE(Candidate().m_PreviewHighPriority);
	EXPECT_EQ(Candidate().m_PreviewImage.m_pData, m_pCandidatePixels);
	EXPECT_EQ(Candidate().m_PreviewImage.m_pData[0], 73);
	EXPECT_EQ(Candidate().m_PreviewState, TItem::PREVIEW_STATE_READY);
	EXPECT_EQ(Candidate().m_PreviewBytes, 4u);
	ASSERT_EQ(m_Queue.size(), 2u);
	EXPECT_EQ(m_Queue.front().m_Name, "candidate");
	EXPECT_EQ(m_Queue.front().m_Epoch, 11u);
	EXPECT_EQ(m_Queue.front().m_Index, 7u);
	EXPECT_EQ(m_Markers.count(SettingsAssetPreviewHandleKey(m_Queue.front())), 1u);
}

TEST_F(SettingsAssetReadyPreview, SameEpochViewIndexChangesDoNotDuplicateMarker)
{
	Candidate().m_PreviewEpoch = 11;
	Candidate().m_PreviewListIndex = 7;
	ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 42, false, m_Queue, m_Markers));
	ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 81, false, m_Queue, m_Markers));
	EXPECT_EQ(Candidate().m_PreviewListIndex, 7u);
	ASSERT_EQ(m_Queue.size(), 2u);
	EXPECT_EQ(m_Queue.back().m_Index, 7u);
	EXPECT_EQ(m_Markers.size(), 2u);
	EXPECT_EQ(Candidate().m_PreviewImage.m_pData, m_pCandidatePixels);
}

TEST_F(SettingsAssetReadyPreview, MarkerAllocationFailurePreservesFieldsAndPixelsForRetry)
{
	m_MarkerResource.m_FailAt = m_MarkerResource.m_Allocations + 1;
	EXPECT_FALSE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 7, true, m_Queue, m_Markers));
	ExpectUnchangedCandidate();

	m_MarkerResource.m_FailAt = -1;
	ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 7, true, m_Queue, m_Markers));
	EXPECT_EQ(Candidate().m_PreviewEpoch, 11u);
	EXPECT_EQ(Candidate().m_PreviewListIndex, 7u);
	EXPECT_TRUE(Candidate().m_PreviewHighPriority);
	EXPECT_EQ(Candidate().m_PreviewImage.m_pData, m_pCandidatePixels);
	EXPECT_EQ(m_Queue.size(), 2u);
	EXPECT_EQ(m_Markers.size(), 2u);
}

TEST_F(SettingsAssetReadyPreview, QueueNodeFailureRollsBackMarkerAndLeavesPixelOwnerRetryable)
{
	m_QueueResource.m_FailAt = m_QueueResource.m_Allocations + 1;
	bool Rejected = false;
	for(int Index = 0; Index < 1024 && !Rejected; ++Index)
	{
		char aName[32];
		str_format(aName, sizeof(aName), "queue-boundary-%d", Index);
		TItem &Item = AddReadyItem(aName);
		uint8_t *pPixels = Item.m_PreviewImage.m_pData;
		const size_t QueueSize = m_Queue.size();
		const size_t MarkerCount = m_Markers.size();
		const auto PreviousFront = m_Queue.front().m_Name;
		if(TryQueueSettingsAssetReadyPreview(Item, ASSETS_TAB_GAME, 11, Index + 1, true, m_Queue, m_Markers))
			continue;
		Rejected = true;
		EXPECT_EQ(m_QueueResource.m_Allocations, m_QueueResource.m_FailAt);
		EXPECT_EQ(m_Queue.size(), QueueSize);
		EXPECT_EQ(m_Markers.size(), MarkerCount);
		EXPECT_EQ(m_Queue.front().m_Name, PreviousFront);
		EXPECT_EQ(Item.m_PreviewEpoch, 3u);
		EXPECT_EQ(Item.m_PreviewListIndex, 99u);
		EXPECT_FALSE(Item.m_PreviewHighPriority);
		EXPECT_EQ(Item.m_PreviewImage.m_pData, pPixels);
		EXPECT_EQ(Item.m_PreviewImage.m_pData[0], 73);
		const SSettingsAssetPreviewHandle FailedHandle{ASSETS_TAB_GAME, 11, static_cast<size_t>(Index + 1), aName};
		EXPECT_EQ(m_Markers.count(SettingsAssetPreviewHandleKey(FailedHandle)), 0u);

		m_QueueResource.m_FailAt = -1;
		ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Item, ASSETS_TAB_GAME, 11, Index + 1, true, m_Queue, m_Markers));
		EXPECT_EQ(m_Queue.size(), QueueSize + 1);
		EXPECT_EQ(m_Markers.size(), MarkerCount + 1);
		EXPECT_EQ(Item.m_PreviewEpoch, 11u);
		EXPECT_TRUE(Item.m_PreviewHighPriority);
		EXPECT_EQ(Item.m_PreviewImage.m_pData, pPixels);
	}
	EXPECT_TRUE(Rejected);
}

TEST_F(SettingsAssetReadyPreview, ExistingReadyEntryCanGainPriorityWithoutDuplicating)
{
	ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 7, false, m_Queue, m_Markers));
	ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 42, true, m_Queue, m_Markers));
	ASSERT_EQ(m_Queue.size(), 2u);
	EXPECT_EQ(m_Queue.front().m_Name, "candidate");
	EXPECT_EQ(m_Queue.front().m_Index, 7u);
	EXPECT_TRUE(Candidate().m_PreviewHighPriority);
	EXPECT_EQ(m_Markers.size(), 2u);
}

TEST_F(SettingsAssetReadyPreview, CallerCanPreserveExistingHighPriorityOrder)
{
	m_Items[0].m_PreviewHighPriority = true;
	ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 7, false, m_Queue, m_Markers));
	ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 42, true, m_Queue, m_Markers, false));
	ASSERT_EQ(m_Queue.size(), 2u);
	EXPECT_EQ(m_Queue.front().m_Name, "old");
	EXPECT_EQ(m_Queue.back().m_Name, "candidate");
	EXPECT_TRUE(Candidate().m_PreviewHighPriority);
	EXPECT_EQ(m_Markers.size(), 2u);
}

TEST_F(SettingsAssetReadyPreview, ClearedTrackingCanBeRestoredWithoutMovingReadyPixels)
{
	ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 7, false, m_Queue, m_Markers));
	m_Queue.clear();
	m_Markers.clear();
	ASSERT_TRUE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 42, false, m_Queue, m_Markers));
	ASSERT_EQ(m_Queue.size(), 1u);
	EXPECT_EQ(m_Queue.front().m_Name, "candidate");
	EXPECT_EQ(m_Queue.front().m_Index, 7u);
	EXPECT_EQ(m_Markers.size(), 1u);
	EXPECT_EQ(Candidate().m_PreviewImage.m_pData, m_pCandidatePixels);
	EXPECT_EQ(Candidate().m_PreviewState, TItem::PREVIEW_STATE_READY);
}

TEST_F(SettingsAssetReadyPreview, NonReadyStatesDoNotPrepareTrackingOrChangePriority)
{
	const int QueueAllocations = m_QueueResource.m_Allocations;
	const int MarkerAllocations = m_MarkerResource.m_Allocations;
	for(auto State : {TItem::PREVIEW_STATE_UNLOADED, TItem::PREVIEW_STATE_LOADING, TItem::PREVIEW_STATE_LOADED, TItem::PREVIEW_STATE_FAILED})
	{
		SCOPED_TRACE(static_cast<int>(State));
		Candidate().m_PreviewState = State;
		EXPECT_FALSE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 7, true, m_Queue, m_Markers));
		EXPECT_EQ(Candidate().m_PreviewEpoch, 3u);
		EXPECT_EQ(Candidate().m_PreviewListIndex, 99u);
		EXPECT_FALSE(Candidate().m_PreviewHighPriority);
		EXPECT_EQ(Candidate().m_PreviewImage.m_pData, m_pCandidatePixels);
	}
	EXPECT_EQ(m_Queue.size(), 1u);
	EXPECT_EQ(m_Markers.size(), 1u);
	EXPECT_EQ(m_QueueResource.m_Allocations, QueueAllocations);
	EXPECT_EQ(m_MarkerResource.m_Allocations, MarkerAllocations);
}

TEST_F(SettingsAssetReadyPreview, ReadyStateWithoutPixelsDoesNotPrepareTracking)
{
	Candidate().m_PreviewImage.Free();
	EXPECT_FALSE(TryQueueSettingsAssetReadyPreview(Candidate(), ASSETS_TAB_GAME, 11, 7, true, m_Queue, m_Markers));
	EXPECT_EQ(Candidate().m_PreviewEpoch, 3u);
	EXPECT_EQ(Candidate().m_PreviewListIndex, 99u);
	EXPECT_FALSE(Candidate().m_PreviewHighPriority);
	EXPECT_EQ(Candidate().m_PreviewImage.m_pData, nullptr);
	EXPECT_EQ(m_Queue.size(), 1u);
	EXPECT_EQ(m_Markers.size(), 1u);
}
