// 使用真实菜单条目和资源 registry；只替换来源同步、作者查询与 GPU 设备边界。
#include <engine/shared/jobs.h>

#include <game/client/components/menus.h>
#include <game/client/components/qmclient/settings_asset_hierarchy.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/prediction_allocation_failure.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	using TItem = CMenus::SCustomEntityBg;
	using TCandidate = SSettingsAssetHierarchyCandidate<TItem>;
	using TSourceKinds = std::unordered_map<std::string, EEntityBgHierarchyEntrySource>;
	constexpr char CURRENT_MAP[] = "z-current-preview-with-a-long-name.map";
	constexpr char NESTED_MAP[] = "folder-one/a-persistent-preview-with-a-long-name.map";
	constexpr char SYNCED_MAP[] = "entity_bg/synchronized-preview-with-a-long-name.map";

	class CHierarchyHandleGraphics : public CIconBenchmarkGraphics
	{
	public:
		CTextureHandle Handle(int Id, uint32_t Generation) { return CreateTextureHandle(Id, Generation); }
	};

	class CHierarchyPreviewJob : public IJob
	{
		void Run() override {}
	};

	void KeepSources(std::vector<std::string> &, TSourceKinds &) {}

	void PopulateAuthor(TItem &Item)
	{
		const std::string Author = std::string("Creator of ") + Item.m_aName;
		str_copy(Item.m_aAuthor, Author.c_str());
	}

	std::vector<std::string> ItemNames(const std::vector<TItem> &vItems)
	{
		std::vector<std::string> vNames;
		for(const TItem &Item : vItems)
			vNames.emplace_back(Item.m_aName);
		return vNames;
	}

	TItem *FindItem(std::vector<TItem> &vItems, const char *pName)
	{
		for(TItem &Item : vItems)
			if(str_comp(Item.m_aName, pName) == 0)
				return &Item;
		return nullptr;
	}

	struct SPublishedHierarchy
	{
		CSettingsAssetMemoryBudget m_MemoryBudget{64};
		std::vector<std::string> m_vSourceNames = {CURRENT_MAP, NESTED_MAP};
		TSourceKinds m_vSourceKinds = {{CURRENT_MAP, EEntityBgHierarchyEntrySource::LOCAL}};
		std::vector<TItem> m_vItems;

		SPublishedHierarchy()
		{
			CHierarchyHandleGraphics Graphics;
			TItem Item;
			str_copy(Item.m_aName, CURRENT_MAP);
			str_copy(Item.m_aDisplayName, "old display");
			str_copy(Item.m_aAuthor, "old author");
			Item.m_RenderTexture = Graphics.Handle(37, 9);
			Item.m_PreviewImage.m_Width = 2;
			Item.m_PreviewImage.m_Height = 1;
			Item.m_PreviewImage.m_Format = CImageInfo::FORMAT_RGBA;
			Item.m_PreviewImage.m_pData = static_cast<uint8_t *>(calloc(8, 1));
			if(Item.m_PreviewImage.m_pData)
			{
				if(!SettingsAssetMemoryTryAcquireImage(Item.m_PreviewImage, Item.m_MemoryLease, &m_MemoryBudget))
					throw std::bad_alloc();
				Item.m_PreviewImage.m_pData[0] = 73;
			}
			Item.m_PreviewState = TItem::PREVIEW_STATE_READY;
			Item.m_PreviewEpoch = 41;
			Item.m_PreviewListIndex = 23;
			Item.m_PreviewBytes = 8;
			Item.m_PreviewRequestedTextureSize = 256;
			Item.m_PreviewResidentBytes = 4096;
			Item.m_PreviewResized = true;
			Item.m_PreviewHighPriority = true;
			m_vItems.push_back(std::move(Item));
		}
	};

	std::optional<TCandidate> PrepareSynced(const SPublishedHierarchy &Published, bool Filtered)
	{
		return TryPrepareSettingsAssetHierarchy<TItem>(Published.m_vSourceNames, Published.m_vSourceKinds, "", true, false, Filtered ? "creator" : "", [](auto &vNames, auto &vKinds) {
				vNames.emplace_back(SYNCED_MAP);
				vKinds.emplace(SYNCED_MAP, EEntityBgHierarchyEntrySource::WORKSHOP); }, PopulateAuthor);
	}

	void ExpectPrepared(const TCandidate &Candidate, bool Filtered)
	{
		EXPECT_EQ(Candidate.m_vSourceNames, (std::vector<std::string>{"default", NESTED_MAP, CURRENT_MAP, SYNCED_MAP}));
		EXPECT_EQ(Candidate.m_vSourceKinds.at("default"), EEntityBgHierarchyEntrySource::LOCAL);
		EXPECT_EQ(Candidate.m_vSourceKinds.at(SYNCED_MAP), EEntityBgHierarchyEntrySource::WORKSHOP);
		if(Filtered)
			EXPECT_EQ(ItemNames(Candidate.m_vItems), (std::vector<std::string>{"default", NESTED_MAP, SYNCED_MAP, CURRENT_MAP}));
		else
			EXPECT_EQ(ItemNames(Candidate.m_vItems), (std::vector<std::string>{"//entity_bg", "/folder-one", "default", CURRENT_MAP}));
		for(const TItem &Item : Candidate.m_vItems)
		{
			EXPECT_EQ(Item.m_aAuthor[0] != '\0', !Item.m_IsDirectory);
			EXPECT_FALSE(Item.m_RenderTexture.IsValid());
			EXPECT_EQ(Item.m_pDecodeJob, nullptr);
			EXPECT_EQ(Item.m_PreviewImage.m_pData, nullptr);
			EXPECT_FALSE(Item.m_MemoryLease.HasReservation());
		}
	}

	void ExpectEveryNativeFailureAndRetry(bool Filtered)
	{
		size_t Allocations = 0;
		std::optional<TCandidate> Complete;
		{
			SPublishedHierarchy Published;
			CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
			Complete = PrepareSynced(Published, Filtered);
			Allocations = Count.Attempts();
		}
		ASSERT_TRUE(Complete);
		ASSERT_GT(Allocations, 0u);
		ExpectPrepared(*Complete, Filtered);
		size_t Rejected = 0;
		for(size_t BeforeFailure = 0; BeforeFailure < Allocations; ++BeforeFailure)
		{
			SCOPED_TRACE(BeforeFailure);
			SPublishedHierarchy Published;
			ASSERT_NE(Published.m_vItems[0].m_PreviewImage.m_pData, nullptr);
			const auto vNamesBefore = Published.m_vSourceNames;
			const auto vKindsBefore = Published.m_vSourceKinds;
			const auto *pItemsBefore = Published.m_vItems.data();
			const auto *pPixelsBefore = Published.m_vItems[0].m_PreviewImage.m_pData;
			const auto pJobBefore = Published.m_vItems[0].m_pDecodeJob;
			std::optional<TCandidate> Candidate;
			bool Triggered = false;
			{
				CPredictionAllocationFailure Failure(BeforeFailure);
				Candidate = PrepareSynced(Published, Filtered);
				Triggered = Failure.Triggered();
			}
			EXPECT_TRUE(Triggered);
			EXPECT_EQ(Published.m_vSourceNames, vNamesBefore);
			EXPECT_EQ(Published.m_vSourceKinds, vKindsBefore);
			EXPECT_EQ(Published.m_vItems.data(), pItemsBefore);
			ASSERT_EQ(Published.m_vItems.size(), 1u);
			EXPECT_EQ(Published.m_vItems[0].m_PreviewImage.m_pData, pPixelsBefore);
			EXPECT_EQ(Published.m_vItems[0].m_MemoryLease.Bytes(), 16u);
			EXPECT_EQ(Published.m_MemoryBudget.Used(), 16u);
			EXPECT_EQ(Published.m_vItems[0].m_pDecodeJob, pJobBefore);
			EXPECT_EQ(Published.m_vItems[0].m_RenderTexture.Id(), 37);
			EXPECT_EQ(Published.m_vItems[0].m_RenderTexture.Generation(), 9u);
			EXPECT_EQ(Published.m_vItems[0].m_PreviewState, TItem::PREVIEW_STATE_READY);
			EXPECT_STREQ(Published.m_vItems[0].m_aAuthor, "old author");
			// stable_sort 的可选临时缓冲可能退回原地排序；成功时也必须得到完整候选。
			if(Candidate)
				ExpectPrepared(*Candidate, Filtered);
			else
				++Rejected;
			auto Retry = PrepareSynced(Published, Filtered);
			ASSERT_TRUE(Retry);
			ExpectPrepared(*Retry, Filtered);
		}
		EXPECT_GT(Rejected, 0u);
	}
}

TEST(SettingsAssetHierarchyTransaction, HierarchyPreparesDefaultSourcesFoldersAndAuthorsBeforePublishing)
{
	const std::vector<std::string> vNames = {"zeta.map", "folder/child.map", "default", "default"};
	const TSourceKinds vKinds = {{"default", EEntityBgHierarchyEntrySource::WORKSHOP}};
	bool SyncSawDefault = false;
	int AuthorCalls = 0;
	auto Candidate = TryPrepareSettingsAssetHierarchy<TItem>(vNames, vKinds, "", true, false, "", [&](auto &vCandidateNames, auto &vCandidateKinds) {
			SyncSawDefault = std::count(vCandidateNames.begin(), vCandidateNames.end(), "default") == 1 &&
				vCandidateKinds.at("default") == EEntityBgHierarchyEntrySource::LOCAL;
			vCandidateNames.emplace_back("entity_bg/installed.map");
			vCandidateKinds.emplace("entity_bg/installed.map", EEntityBgHierarchyEntrySource::WORKSHOP); }, [&](TItem &Item) { ++AuthorCalls; str_copy(Item.m_aAuthor, "prepared author"); });
	ASSERT_TRUE(Candidate);
	EXPECT_TRUE(SyncSawDefault);
	EXPECT_EQ(ItemNames(Candidate->m_vItems), (std::vector<std::string>{"//entity_bg", "/folder", "default", "zeta.map"}));
	EXPECT_EQ(AuthorCalls, 2);
	EXPECT_TRUE(Candidate->m_vItems[0].m_IsDirectory);
	EXPECT_TRUE(Candidate->m_vItems[1].m_IsDirectory);
	EXPECT_STREQ(Candidate->m_vItems[0].m_aAuthor, "");
	EXPECT_STREQ(Candidate->m_vItems[2].m_aAuthor, "prepared author");
	EXPECT_EQ(vNames, (std::vector<std::string>{"zeta.map", "folder/child.map", "default", "default"}));
	EXPECT_EQ(vKinds.at("default"), EEntityBgHierarchyEntrySource::WORKSHOP);
}

TEST(SettingsAssetHierarchyTransaction, HiddenWorkshopResetsItsChildFolderOnlyInTheCandidate)
{
	const std::vector<std::string> vNames = {"local.map", "entity_bg/installed.map"};
	const TSourceKinds vKinds = {{"entity_bg/installed.map", EEntityBgHierarchyEntrySource::WORKSHOP}};
	for(const char *pFolder : {"//entity_bg/child", "entity_bg/child"})
	{
		SCOPED_TRACE(pFolder);
		const std::string Folder(pFolder);
		auto Candidate = TryPrepareSettingsAssetHierarchy<TItem>(vNames, vKinds, Folder.c_str(), false, true, "", KeepSources, PopulateAuthor);
		ASSERT_TRUE(Candidate);
		EXPECT_STREQ(Candidate->m_aCurrentFolder, "");
		EXPECT_EQ(Folder, pFolder);
		EXPECT_EQ(ItemNames(Candidate->m_vItems), (std::vector<std::string>{"default", "local.map"}));
	}
}

TEST(SettingsAssetHierarchyTransaction, HiddenWorkshopDoesNotResetLocalEntityBgNavigationTokens)
{
	const std::vector<std::string> vNames = {"entity_bg/entity_bg/sub/local", "entity_bg/workshop"};
	const TSourceKinds Kinds = {
		{vNames[0], EEntityBgHierarchyEntrySource::LOCAL},
		{vNames[1], EEntityBgHierarchyEntrySource::WORKSHOP},
	};
	auto Candidate = TryPrepareSettingsAssetHierarchy<TItem>(vNames, Kinds, "/entity_bg/sub", false, false, "", KeepSources, PopulateAuthor);
	ASSERT_TRUE(Candidate);
	EXPECT_STREQ(Candidate->m_aCurrentFolder, "/entity_bg/sub");
	EXPECT_EQ(ItemNames(Candidate->m_vItems), (std::vector<std::string>{"/", vNames[0]}));
	EXPECT_TRUE(Candidate->m_vItems[0].m_IsDirectory);
	EXPECT_FALSE(Candidate->m_vItems[1].m_IsDirectory);
	EXPECT_STREQ(Candidate->m_vItems[1].m_aAuthor, "Creator of entity_bg/entity_bg/sub/local");
	EXPECT_EQ(Candidate->m_vSourceKinds.at(vNames[0]), EEntityBgHierarchyEntrySource::LOCAL);
	EXPECT_EQ(Candidate->m_vSourceKinds.find("/entity_bg/sub"), Candidate->m_vSourceKinds.end());
}

TEST(SettingsAssetHierarchyTransaction, PendingWorkshopContentCanKeepTheWorkshopFolderVisible)
{
	auto Candidate = TryPrepareSettingsAssetHierarchy<TItem>({}, {}, "", true, true, "", KeepSources, PopulateAuthor);
	ASSERT_TRUE(Candidate);
	EXPECT_EQ(ItemNames(Candidate->m_vItems), (std::vector<std::string>{"//entity_bg", "default"}));
	EXPECT_TRUE(Candidate->m_vItems[0].m_IsDirectory);
}

TEST(SettingsAssetHierarchyTransaction, FlatFilterUsesNamesAndAuthorsAndKeepsStableBasenameOrder)
{
	const std::vector<std::string> vNames = {"Zulu/needle.map", "Alpha/needle.map", "name-needle.map", "other/hidden.map", "作者/山.map"};
	const TSourceKinds vKinds = {{"other/hidden.map", EEntityBgHierarchyEntrySource::WORKSHOP}};
	auto Candidate = TryPrepareSettingsAssetHierarchy<TItem>(vNames, vKinds, "unrelated-folder", false, false, "NEEDLE", KeepSources,
		[](TItem &Item) { str_copy(Item.m_aAuthor, str_comp(Item.m_aName, "default") == 0 ? "builtin" : "Needle 工匠"); });
	ASSERT_TRUE(Candidate);
	EXPECT_EQ(ItemNames(Candidate->m_vItems), (std::vector<std::string>{"name-needle.map", "Alpha/needle.map", "Zulu/needle.map", "作者/山.map"}));
	for(const TItem &Item : Candidate->m_vItems)
	{
		EXPECT_FALSE(Item.m_IsDirectory);
		EXPECT_STREQ(Item.m_aAuthor, "Needle 工匠");
	}
	EXPECT_STREQ(Candidate->m_vItems[1].m_aDisplayName, "needle.map");
}

TEST(SettingsAssetHierarchyTransaction, FlatFilterMatchesTheFullPathWhenItsBasenameDoesNotMatch)
{
	auto Candidate = TryPrepareSettingsAssetHierarchy<TItem>({"the-folder/child.map", "unrelated.map"}, {}, "", true, false,
		"THE-FOLDER", KeepSources, [](TItem &) {});
	ASSERT_TRUE(Candidate);
	EXPECT_EQ(ItemNames(Candidate->m_vItems), (std::vector<std::string>{"the-folder/child.map"}));
}

TEST(SettingsAssetHierarchyTransaction, FlatFilterMatchesUtf8AuthorText)
{
	auto Candidate = TryPrepareSettingsAssetHierarchy<TItem>({"plain.map", "other.map"}, {}, "", true, false, "工匠", KeepSources,
		[](TItem &Item) { str_copy(Item.m_aAuthor, str_comp(Item.m_aName, "plain.map") == 0 ? "工匠小明" : "other"); });
	ASSERT_TRUE(Candidate);
	EXPECT_EQ(ItemNames(Candidate->m_vItems), (std::vector<std::string>{"plain.map"}));
}

TEST(SettingsAssetHierarchyTransaction, NativeHierarchyAllocationFailuresKeepOldPublicationAndAllowRetry)
{
	ExpectEveryNativeFailureAndRetry(false);
}

TEST(SettingsAssetHierarchyTransaction, NativeFilteredAllocationFailuresKeepOldPublicationAndAllowRetry)
{
	ExpectEveryNativeFailureAndRetry(true);
}

TEST(SettingsAssetHierarchyTransaction, SyncLengthFailureDoesNotPublishPartialSources)
{
	SPublishedHierarchy Published;
	const auto vNamesBefore = Published.m_vSourceNames;
	const auto vKindsBefore = Published.m_vSourceKinds;
	auto Candidate = TryPrepareSettingsAssetHierarchy<TItem>(Published.m_vSourceNames, Published.m_vSourceKinds, "entity_bg/child", false, false, "", [](auto &vNames, auto &vKinds) {
			vNames.emplace_back(SYNCED_MAP);
			vKinds.emplace(SYNCED_MAP, EEntityBgHierarchyEntrySource::WORKSHOP);
			throw std::length_error("source capacity"); }, PopulateAuthor);
	EXPECT_FALSE(Candidate);
	EXPECT_EQ(Published.m_vSourceNames, vNamesBefore);
	EXPECT_EQ(Published.m_vSourceKinds, vKindsBefore);
	EXPECT_STREQ(Published.m_vItems[0].m_aDisplayName, "old display");
	EXPECT_TRUE(PrepareSynced(Published, false));
}

TEST(SettingsAssetHierarchyTransaction, LateAuthorLengthFailureKeepsOldItemsAndPreview)
{
	SPublishedHierarchy Published;
	const auto *pPixelsBefore = Published.m_vItems[0].m_PreviewImage.m_pData;
	const auto pJobBefore = Published.m_vItems[0].m_pDecodeJob;
	int AuthorCalls = 0;
	auto Candidate = TryPrepareSettingsAssetHierarchy<TItem>(Published.m_vSourceNames, Published.m_vSourceKinds, "", true, false, "", KeepSources,
		[&](TItem &Item) {
			++AuthorCalls;
			if(str_comp(Item.m_aName, CURRENT_MAP) == 0)
				throw std::length_error("author capacity");
			PopulateAuthor(Item);
		});
	EXPECT_FALSE(Candidate);
	EXPECT_EQ(AuthorCalls, 2);
	EXPECT_EQ(Published.m_vItems[0].m_PreviewImage.m_pData, pPixelsBefore);
	EXPECT_EQ(Published.m_vItems[0].m_pDecodeJob, pJobBefore);
	EXPECT_EQ(Published.m_vItems[0].m_RenderTexture.Id(), 37);
	EXPECT_TRUE(PrepareSynced(Published, false));
}

TEST(SettingsAssetHierarchyTransaction, CommitMovesReadyPixelsAndTextureWithoutAllocatingOrReplacingNewMetadata)
{
	SPublishedHierarchy Published;
	ASSERT_NE(Published.m_vItems[0].m_PreviewImage.m_pData, nullptr);
	const auto *pPixels = Published.m_vItems[0].m_PreviewImage.m_pData;
	const auto pJob = Published.m_vItems[0].m_pDecodeJob;
	auto Candidate = PrepareSynced(Published, true);
	ASSERT_TRUE(Candidate);
	bool Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		CommitSettingsAssetHierarchy(*Candidate, Published.m_vSourceNames, Published.m_vSourceKinds, Published.m_vItems);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	TItem *pCurrent = FindItem(Published.m_vItems, CURRENT_MAP);
	ASSERT_NE(pCurrent, nullptr);
	EXPECT_STREQ(pCurrent->m_aDisplayName, CURRENT_MAP);
	EXPECT_STREQ(pCurrent->m_aAuthor, (std::string("Creator of ") + CURRENT_MAP).c_str());
	EXPECT_EQ(pCurrent->m_RenderTexture.Id(), 37);
	EXPECT_EQ(pCurrent->m_RenderTexture.Generation(), 9u);
	EXPECT_EQ(pCurrent->m_pDecodeJob, pJob);
	EXPECT_EQ(pCurrent->m_PreviewImage.m_pData, pPixels);
	EXPECT_EQ(pCurrent->m_PreviewImage.m_pData[0], 73);
	EXPECT_EQ(pCurrent->m_PreviewImage.m_Width, 2u);
	EXPECT_EQ(pCurrent->m_PreviewImage.m_Height, 1u);
	EXPECT_EQ(pCurrent->m_PreviewImage.m_Format, CImageInfo::FORMAT_RGBA);
	EXPECT_EQ(pCurrent->m_MemoryLease.Bytes(), 16u);
	EXPECT_EQ(Published.m_MemoryBudget.Used(), 16u);
	EXPECT_EQ(pCurrent->m_PreviewState, TItem::PREVIEW_STATE_READY);
	EXPECT_EQ(pCurrent->m_PreviewEpoch, 41u);
	EXPECT_EQ(pCurrent->m_PreviewListIndex, 23u);
	EXPECT_EQ(pCurrent->m_PreviewBytes, 8u);
	EXPECT_EQ(pCurrent->m_PreviewRequestedTextureSize, 256);
	EXPECT_EQ(pCurrent->m_PreviewResidentBytes, 4096u);
	EXPECT_TRUE(pCurrent->m_PreviewResized);
	EXPECT_TRUE(pCurrent->m_PreviewHighPriority);
	ASSERT_EQ(Candidate->m_vItems.size(), 1u);
	EXPECT_FALSE(Candidate->m_vItems[0].m_RenderTexture.IsValid());
	EXPECT_EQ(Candidate->m_vItems[0].m_pDecodeJob, nullptr);
	EXPECT_EQ(Candidate->m_vItems[0].m_PreviewImage.m_pData, nullptr);
	EXPECT_EQ(Candidate->m_vItems[0].m_PreviewImage.m_Width, 0u);
	EXPECT_FALSE(Candidate->m_vItems[0].m_MemoryLease.HasReservation());
}

TEST(SettingsAssetHierarchyTransaction, CommitKeepsTheInFlightJobOwnedByItsNewSameNameItem)
{
	SPublishedHierarchy Published;
	Published.m_vItems[0].FreePreviewCpuImage();
	Published.m_vItems[0].m_PreviewState = TItem::PREVIEW_STATE_LOADING;
	Published.m_vItems[0].m_pDecodeJob = std::make_shared<CHierarchyPreviewJob>();
	const auto pJob = Published.m_vItems[0].m_pDecodeJob;
	auto Candidate = PrepareSynced(Published, false);
	ASSERT_TRUE(Candidate);
	CommitSettingsAssetHierarchy(*Candidate, Published.m_vSourceNames, Published.m_vSourceKinds, Published.m_vItems);
	TItem *pCurrent = FindItem(Published.m_vItems, CURRENT_MAP);
	ASSERT_NE(pCurrent, nullptr);
	EXPECT_EQ(pCurrent->m_pDecodeJob, pJob);
	EXPECT_EQ(pCurrent->m_PreviewState, TItem::PREVIEW_STATE_LOADING);
	EXPECT_EQ(pCurrent->m_PreviewImage.m_pData, nullptr);
	EXPECT_EQ(pCurrent->m_RenderTexture.Id(), 37);
	ASSERT_EQ(Candidate->m_vItems.size(), 1u);
	EXPECT_EQ(Candidate->m_vItems[0].m_pDecodeJob, nullptr);
	EXPECT_FALSE(Candidate->m_vItems[0].m_RenderTexture.IsValid());
	EXPECT_EQ(pJob.use_count(), 2);
}

TEST(SettingsAssetHierarchyTransaction, NativeCopyContextFailureKeepsThePublishedPixelsAndCanRetry)
{
	SPublishedHierarchy Published;
	const auto *pPixels = Published.m_vItems.front().m_PreviewImage.m_pData;
	ASSERT_NE(pPixels, nullptr);
	bool Triggered = false;
	bool Rejected = false;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			TItem Copy(Published.m_vItems.front());
		}
		catch(const std::bad_alloc &)
		{
			Rejected = true;
		}
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_TRUE(Rejected);
	EXPECT_EQ(Published.m_vItems.front().m_PreviewImage.m_pData, pPixels);
	EXPECT_EQ(pPixels[0], 73);
	EXPECT_EQ(Published.m_MemoryBudget.Used(), 16u);
	TItem Retry(Published.m_vItems.front());
	ASSERT_NE(Retry.m_PreviewImage.m_pData, nullptr);
	EXPECT_NE(Retry.m_PreviewImage.m_pData, pPixels);
	EXPECT_EQ(Retry.m_PreviewImage.m_pData[0], 73);
	EXPECT_EQ(Published.m_MemoryBudget.Used(), 32u);
}

TEST(SettingsAssetHierarchyTransaction, NativeCopyAssignmentContextFailurePreservesTheOldOwnerUntilRetry)
{
	SPublishedHierarchy Source;
	SPublishedHierarchy Destination;
	str_copy(Destination.m_vItems.front().m_aName, "old-destination.map");
	const auto *pOldPixels = Destination.m_vItems.front().m_PreviewImage.m_pData;
	const auto *pSourcePixels = Source.m_vItems.front().m_PreviewImage.m_pData;
	ASSERT_NE(pOldPixels, nullptr);
	ASSERT_NE(pSourcePixels, nullptr);
	bool Triggered = false;
	bool Rejected = false;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			Destination.m_vItems.front() = Source.m_vItems.front();
		}
		catch(const std::bad_alloc &)
		{
			Rejected = true;
		}
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_TRUE(Rejected);
	EXPECT_STREQ(Destination.m_vItems.front().m_aName, "old-destination.map");
	EXPECT_EQ(Destination.m_vItems.front().m_PreviewImage.m_pData, pOldPixels);
	EXPECT_EQ(Destination.m_vItems.front().m_RenderTexture.Id(), 37);
	EXPECT_EQ(Destination.m_MemoryBudget.Used(), 16u);
	EXPECT_EQ(Source.m_MemoryBudget.Used(), 16u);
	Destination.m_vItems.front() = Source.m_vItems.front();
	EXPECT_STREQ(Destination.m_vItems.front().m_aName, CURRENT_MAP);
	EXPECT_NE(Destination.m_vItems.front().m_PreviewImage.m_pData, pSourcePixels);
	EXPECT_EQ(Destination.m_MemoryBudget.Used(), 0u);
	EXPECT_EQ(Source.m_MemoryBudget.Used(), 32u);
}

TEST(SettingsAssetHierarchyTransaction, RemovedPreviewRemainsOwnedByTheReturnedOldItems)
{
	SPublishedHierarchy Published;
	CHierarchyHandleGraphics Graphics;
	TItem Removed;
	str_copy(Removed.m_aName, "removed.map");
	Removed.m_RenderTexture = Graphics.Handle(81, 12);
	Removed.m_pDecodeJob = std::make_shared<CHierarchyPreviewJob>();
	Removed.m_PreviewState = TItem::PREVIEW_STATE_LOADING;
	const auto pRemovedJob = Removed.m_pDecodeJob;
	Published.m_vItems.push_back(std::move(Removed));
	auto Candidate = PrepareSynced(Published, false);
	ASSERT_TRUE(Candidate);
	CommitSettingsAssetHierarchy(*Candidate, Published.m_vSourceNames, Published.m_vSourceKinds, Published.m_vItems);
	EXPECT_EQ(FindItem(Published.m_vItems, "removed.map"), nullptr);
	TItem *pOld = FindItem(Candidate->m_vItems, "removed.map");
	ASSERT_NE(pOld, nullptr);
	EXPECT_EQ(pOld->m_RenderTexture.Id(), 81);
	EXPECT_EQ(pOld->m_RenderTexture.Generation(), 12u);
	EXPECT_EQ(pOld->m_pDecodeJob, pRemovedJob);
}

TEST(SettingsAssetHierarchyTransaction, DirectoryNavigationDoesNotBorrowItsSameStemLeafPreview)
{
	SPublishedHierarchy Published;
	Published.m_vSourceNames = {"foo", "foo/child"};
	Published.m_vSourceKinds = {{"foo", EEntityBgHierarchyEntrySource::LOCAL}, {"foo/child", EEntityBgHierarchyEntrySource::LOCAL}};
	str_copy(Published.m_vItems[0].m_aName, "foo");
	const auto *pPixels = Published.m_vItems[0].m_PreviewImage.m_pData;
	ASSERT_NE(pPixels, nullptr);
	auto Root = TryPrepareSettingsAssetHierarchy<TItem>(Published.m_vSourceNames, Published.m_vSourceKinds, "", true, false, "", KeepSources, PopulateAuthor);
	ASSERT_TRUE(Root);
	EXPECT_EQ(ItemNames(Root->m_vItems), (std::vector<std::string>{"/foo", "default", "foo"}));
	CommitSettingsAssetHierarchy(*Root, Published.m_vSourceNames, Published.m_vSourceKinds, Published.m_vItems);
	const auto *pDirectory = FindItem(Published.m_vItems, "/foo");
	ASSERT_NE(pDirectory, nullptr);
	EXPECT_TRUE(pDirectory->m_IsDirectory);
	EXPECT_FALSE(pDirectory->m_RenderTexture.IsValid());
	EXPECT_EQ(pDirectory->m_PreviewImage.m_pData, nullptr);
	EXPECT_EQ(pDirectory->m_pDecodeJob, nullptr);
	EXPECT_STREQ(pDirectory->m_aAuthor, "");
	const auto *pLeaf = FindItem(Published.m_vItems, "foo");
	ASSERT_NE(pLeaf, nullptr);
	EXPECT_FALSE(pLeaf->m_IsDirectory);
	EXPECT_EQ(pLeaf->m_RenderTexture.Id(), 37);
	EXPECT_EQ(pLeaf->m_RenderTexture.Generation(), 9U);
	EXPECT_EQ(pLeaf->m_PreviewImage.m_pData, pPixels);
	EXPECT_STREQ(pLeaf->m_aAuthor, "Creator of foo");
	EXPECT_EQ(Published.m_vSourceKinds.find("/foo"), Published.m_vSourceKinds.end());

	auto Child = TryPrepareSettingsAssetHierarchy<TItem>(Published.m_vSourceNames, Published.m_vSourceKinds, pDirectory->m_aName, true, false, "", KeepSources, PopulateAuthor);
	ASSERT_TRUE(Child);
	EXPECT_EQ(ItemNames(Child->m_vItems), (std::vector<std::string>{"/", "foo/child"}));
	CommitSettingsAssetHierarchy(*Child, Published.m_vSourceNames, Published.m_vSourceKinds, Published.m_vItems);
	const auto *pChildLeaf = FindItem(Published.m_vItems, "foo/child");
	ASSERT_NE(pChildLeaf, nullptr);
	EXPECT_FALSE(pChildLeaf->m_RenderTexture.IsValid());
	EXPECT_EQ(pChildLeaf->m_PreviewImage.m_pData, nullptr);
	const auto *pRetiredLeaf = FindItem(Child->m_vItems, "foo");
	ASSERT_NE(pRetiredLeaf, nullptr);
	EXPECT_FALSE(pRetiredLeaf->m_IsDirectory);
	EXPECT_EQ(pRetiredLeaf->m_RenderTexture.Id(), 37);
	EXPECT_EQ(pRetiredLeaf->m_RenderTexture.Generation(), 9U);
	EXPECT_EQ(pRetiredLeaf->m_PreviewImage.m_pData, pPixels);
	const auto *pRetiredDirectory = FindItem(Child->m_vItems, "/foo");
	ASSERT_NE(pRetiredDirectory, nullptr);
	EXPECT_TRUE(pRetiredDirectory->m_IsDirectory);
	EXPECT_FALSE(pRetiredDirectory->m_RenderTexture.IsValid());
	EXPECT_EQ(pRetiredDirectory->m_PreviewImage.m_pData, nullptr);
}

TEST(SettingsAssetHierarchyTransaction, WorkshopNavigationAndItsSameNamedLeafKeepSeparatePreviewOwners)
{
	SPublishedHierarchy Published;
	Published.m_vSourceNames = {"entity_bg", "entity_bg/installed"};
	Published.m_vSourceKinds = {{"entity_bg", EEntityBgHierarchyEntrySource::LOCAL}, {"entity_bg/installed", EEntityBgHierarchyEntrySource::WORKSHOP}};
	str_copy(Published.m_vItems[0].m_aName, "entity_bg");
	const auto *pPixels = Published.m_vItems[0].m_PreviewImage.m_pData;
	ASSERT_NE(pPixels, nullptr);
	auto Root = TryPrepareSettingsAssetHierarchy<TItem>(Published.m_vSourceNames, Published.m_vSourceKinds, "", true, false, "", KeepSources, PopulateAuthor);
	ASSERT_TRUE(Root);
	EXPECT_EQ(ItemNames(Root->m_vItems), (std::vector<std::string>{"//entity_bg", "default", "entity_bg"}));
	CommitSettingsAssetHierarchy(*Root, Published.m_vSourceNames, Published.m_vSourceKinds, Published.m_vItems);
	const auto *pDirectory = FindItem(Published.m_vItems, "//entity_bg");
	ASSERT_NE(pDirectory, nullptr);
	EXPECT_TRUE(pDirectory->m_IsDirectory);
	EXPECT_FALSE(pDirectory->m_RenderTexture.IsValid());
	EXPECT_EQ(pDirectory->m_PreviewImage.m_pData, nullptr);
	EXPECT_EQ(pDirectory->m_pDecodeJob, nullptr);
	const auto *pLeaf = FindItem(Published.m_vItems, "entity_bg");
	ASSERT_NE(pLeaf, nullptr);
	EXPECT_FALSE(pLeaf->m_IsDirectory);
	EXPECT_EQ(pLeaf->m_RenderTexture.Id(), 37);
	EXPECT_EQ(pLeaf->m_PreviewImage.m_pData, pPixels);
	EXPECT_NE(Published.m_vSourceKinds.find("entity_bg"), Published.m_vSourceKinds.end());
	EXPECT_EQ(Published.m_vSourceKinds.find("//entity_bg"), Published.m_vSourceKinds.end());
}

TEST(SettingsAssetHierarchyTransaction, ReturningFromFlatFilterRetiresTheWorkshopLeafJobInsteadOfGivingItToNavigation)
{
	SPublishedHierarchy Published;
	Published.m_vSourceNames = {"entity_bg/installed"};
	Published.m_vSourceKinds = {{"entity_bg/installed", EEntityBgHierarchyEntrySource::WORKSHOP}};
	str_copy(Published.m_vItems[0].m_aName, "entity_bg/installed");
	Published.m_vItems[0].FreePreviewCpuImage();
	Published.m_vItems[0].m_PreviewBytes = 0;
	Published.m_vItems[0].m_PreviewState = TItem::PREVIEW_STATE_LOADING;
	Published.m_vItems[0].m_pDecodeJob = std::make_shared<CHierarchyPreviewJob>();
	const auto pJob = Published.m_vItems[0].m_pDecodeJob;
	auto Flat = TryPrepareSettingsAssetHierarchy<TItem>(Published.m_vSourceNames, Published.m_vSourceKinds, "", true, false, "Creator", KeepSources, PopulateAuthor);
	ASSERT_TRUE(Flat);
	CommitSettingsAssetHierarchy(*Flat, Published.m_vSourceNames, Published.m_vSourceKinds, Published.m_vItems);
	ASSERT_NE(FindItem(Published.m_vItems, "entity_bg/installed"), nullptr);
	auto Root = TryPrepareSettingsAssetHierarchy<TItem>(Published.m_vSourceNames, Published.m_vSourceKinds, "", true, false, "", KeepSources, PopulateAuthor);
	ASSERT_TRUE(Root);
	CommitSettingsAssetHierarchy(*Root, Published.m_vSourceNames, Published.m_vSourceKinds, Published.m_vItems);
	const auto *pDirectory = FindItem(Published.m_vItems, "//entity_bg");
	ASSERT_NE(pDirectory, nullptr);
	EXPECT_TRUE(pDirectory->m_IsDirectory);
	EXPECT_FALSE(pDirectory->m_RenderTexture.IsValid());
	EXPECT_EQ(pDirectory->m_pDecodeJob, nullptr);
	EXPECT_EQ(FindItem(Published.m_vItems, "entity_bg/installed"), nullptr);
	const auto *pRetired = FindItem(Root->m_vItems, "entity_bg/installed");
	ASSERT_NE(pRetired, nullptr);
	EXPECT_FALSE(pRetired->m_IsDirectory);
	EXPECT_EQ(pRetired->m_RenderTexture.Id(), 37);
	EXPECT_EQ(pRetired->m_pDecodeJob, pJob);
	EXPECT_EQ(pJob.use_count(), 2);
	EXPECT_EQ(Published.m_vSourceKinds.find("//entity_bg"), Published.m_vSourceKinds.end());
}

TEST(SettingsAssetHierarchyTransaction, ParentNavigationDoesNotBorrowTheRealDotDotMapLeafPreview)
{
	SPublishedHierarchy Published;
	// maps/...map 的真实资源名是 ..，仍作为 leaf 保留，不能拿它作父目录身份。
	Published.m_vSourceNames = {"..", "foo/child"};
	Published.m_vSourceKinds = {{"..", EEntityBgHierarchyEntrySource::LOCAL}, {"foo/child", EEntityBgHierarchyEntrySource::LOCAL}};
	str_copy(Published.m_vItems[0].m_aName, "..");
	const auto *pPixels = Published.m_vItems[0].m_PreviewImage.m_pData;
	ASSERT_NE(pPixels, nullptr);
	auto Root = TryPrepareSettingsAssetHierarchy<TItem>(Published.m_vSourceNames, Published.m_vSourceKinds, "", true, false, "", KeepSources, PopulateAuthor);
	ASSERT_TRUE(Root);
	CommitSettingsAssetHierarchy(*Root, Published.m_vSourceNames, Published.m_vSourceKinds, Published.m_vItems);
	const auto *pLeaf = FindItem(Published.m_vItems, "..");
	ASSERT_NE(pLeaf, nullptr);
	EXPECT_FALSE(pLeaf->m_IsDirectory);
	EXPECT_EQ(pLeaf->m_PreviewImage.m_pData, pPixels);
	auto Child = TryPrepareSettingsAssetHierarchy<TItem>(Published.m_vSourceNames, Published.m_vSourceKinds, "/foo", true, false, "", KeepSources, PopulateAuthor);
	ASSERT_TRUE(Child);
	CommitSettingsAssetHierarchy(*Child, Published.m_vSourceNames, Published.m_vSourceKinds, Published.m_vItems);
	const auto *pParent = FindItem(Published.m_vItems, "/");
	ASSERT_NE(pParent, nullptr);
	EXPECT_TRUE(pParent->m_IsDirectory);
	EXPECT_STREQ(pParent->m_aDisplayName, "..");
	EXPECT_FALSE(pParent->m_RenderTexture.IsValid());
	EXPECT_EQ(pParent->m_PreviewImage.m_pData, nullptr);
	EXPECT_EQ(pParent->m_pDecodeJob, nullptr);
	const auto *pRetired = FindItem(Child->m_vItems, "..");
	ASSERT_NE(pRetired, nullptr);
	EXPECT_EQ(pRetired->m_RenderTexture.Id(), 37);
	EXPECT_EQ(pRetired->m_PreviewImage.m_pData, pPixels);
	EXPECT_EQ(Published.m_vSourceKinds.find("/"), Published.m_vSourceKinds.end());
}

TEST(SettingsAssetHierarchyTransaction, SameNameWithAChangedEntryKindRetainsTheOldPreviewOwner)
{
	for(const bool WasDirectory : {false, true})
	{
		SCOPED_TRACE(WasDirectory);
		SPublishedHierarchy Published;
		str_copy(Published.m_vItems[0].m_aName, "same-name");
		Published.m_vItems[0].m_IsDirectory = WasDirectory;
		const auto *pPixels = Published.m_vItems[0].m_PreviewImage.m_pData;
		TCandidate Candidate;
		TItem Next;
		str_copy(Next.m_aName, "same-name");
		Next.m_IsDirectory = !WasDirectory;
		Candidate.m_vItems.push_back(std::move(Next));
		CommitSettingsAssetHierarchy(Candidate, Published.m_vSourceNames, Published.m_vSourceKinds, Published.m_vItems);
		ASSERT_EQ(Published.m_vItems.size(), 1u);
		EXPECT_EQ(Published.m_vItems[0].m_IsDirectory, !WasDirectory);
		EXPECT_FALSE(Published.m_vItems[0].m_RenderTexture.IsValid());
		EXPECT_EQ(Published.m_vItems[0].m_PreviewImage.m_pData, nullptr);
		ASSERT_EQ(Candidate.m_vItems.size(), 1u);
		EXPECT_EQ(Candidate.m_vItems[0].m_RenderTexture.Id(), 37);
		EXPECT_EQ(Candidate.m_vItems[0].m_PreviewImage.m_pData, pPixels);
	}
}
