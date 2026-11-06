// 通过生产条目、归并入口和真实容器分配观察发布事务，不启动图形设备。
#include <game/client/components/qmclient/settings_asset_list_merge.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/tracked_allocation_resource.h>

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace
{
	using TMergeList = std::vector<CMenus::SCustomItem, CBorrowedAllocator<CMenus::SCustomItem>>;

	class CAssetMergeHandleFactory : public CIconBenchmarkGraphics
	{
	public:
		CTextureHandle Handle(int Id, uint32_t Generation) { return CreateTextureHandle(Id, Generation); }
	};

	class CAssetMergeJob : public IJob
	{
		void Run() override {}
	};

	class CObservedMergeAllocationResource : public CTrackedAllocationResource
	{
	public:
		const void *m_pObservedBlock = nullptr;
		bool m_ObservedBlockReleased = false;
		void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override
		{
			if(pData == m_pObservedBlock)
				m_ObservedBlockReleased = true;
			CTrackedAllocationResource::Deallocate(pData, Bytes, Alignment);
		}
	};

	// 限制真实 vector 的容量上界，不需要构造超大扫描结果。
	template<typename T>
	class CTwoItemAllocator
	{
	public:
		using value_type = T;
		using is_always_equal = std::true_type;
		CTwoItemAllocator() = default;
		template<typename U>
		CTwoItemAllocator(const CTwoItemAllocator<U> &) noexcept
		{
		}
		T *allocate(size_t Count) { return std::allocator<T>{}.allocate(Count); }
		void deallocate(T *pData, size_t Count) noexcept { std::allocator<T>{}.deallocate(pData, Count); }
		size_t max_size() const noexcept { return 2; }
		template<typename U>
		bool operator==(const CTwoItemAllocator<U> &) const noexcept
		{
			return true;
		}
	};

	std::vector<CMenus::SSettingsAssetMergeEntry> MergeEntries(std::initializer_list<const char *> Names)
	{
		std::vector<CMenus::SSettingsAssetMergeEntry> vEntries;
		for(const char *pName : Names)
		{
			CMenus::SSettingsAssetMergeEntry Entry;
			str_copy(Entry.m_aName, pName);
			vEntries.push_back(Entry);
		}
		return vEntries;
	}

	template<typename TItem, typename TAllocator>
	TItem *FindItem(std::vector<TItem, TAllocator> &vList, const char *pName)
	{
		for(TItem &Item : vList)
			if(str_comp(Item.m_aName, pName) == 0)
				return &Item;
		return nullptr;
	}

	template<typename TItem, typename TAllocator>
	void ExpectNames(const std::vector<TItem, TAllocator> &vList, std::initializer_list<const char *> Names)
	{
		ASSERT_EQ(vList.size(), Names.size());
		size_t Index = 0;
		for(const char *pName : Names)
			EXPECT_STREQ(vList[Index++].m_aName, pName);
	}

	void PrepareNothing(CMenus::SCustomItem &, const CMenus::SSettingsAssetMergeEntry &) {}

	class SettingsAssetListMerge : public ::testing::Test
	{
	protected:
		CObservedMergeAllocationResource m_Resource;
		TMergeList m_vList{CBorrowedAllocator<CMenus::SCustomItem>(&m_Resource)};
		CAssetMergeHandleFactory m_Graphics;
		std::shared_ptr<IJob> m_pOldJob;
		uint8_t *m_pOldPixels = nullptr;

		void SetUp() override
		{
			m_vList.emplace_back();
			auto &Item = m_vList.front();
			str_copy(Item.m_aName, "middle");
			str_copy(Item.m_aDisplayName, "old display");
			str_copy(Item.m_aAuthor, "old author");
			Item.m_RenderTexture = m_Graphics.Handle(41, 17);
			Item.m_PreviewImage.m_Width = 2;
			Item.m_PreviewImage.m_Height = 1;
			Item.m_PreviewImage.m_Format = CImageInfo::FORMAT_RGBA;
			Item.m_PreviewImage.m_pData = static_cast<uint8_t *>(calloc(8, 1));
			ASSERT_NE(Item.m_PreviewImage.m_pData, nullptr);
			m_pOldPixels = Item.m_PreviewImage.m_pData;
			m_pOldPixels[0] = 47;
			m_pOldJob = std::make_shared<CAssetMergeJob>();
			Item.m_pDecodeJob = m_pOldJob;
			Item.m_PreviewState = CMenus::SCustomItem::PREVIEW_STATE_READY;
			Item.m_PreviewEpoch = 13;
			Item.m_PreviewListIndex = 6;
			Item.m_PreviewBytes = 8;
			Item.m_PreviewRequestedTextureSize = 128;
			Item.m_PreviewResidentBytes = 77;
			Item.m_PreviewResized = true;
			Item.m_PreviewHighPriority = true;
		}
		void TearDown() override
		{
			{
				TMergeList vEmpty(m_vList.get_allocator());
				m_vList.swap(vEmpty);
			}
			EXPECT_EQ(m_Resource.m_Live, 0);
		}
		void ExpectOldItem(const CMenus::SCustomItem &Item)
		{
			EXPECT_STREQ(Item.m_aName, "middle");
			EXPECT_STREQ(Item.m_aDisplayName, "old display");
			EXPECT_STREQ(Item.m_aAuthor, "old author");
			EXPECT_EQ(Item.m_RenderTexture.Id(), 41);
			EXPECT_EQ(Item.m_RenderTexture.Generation(), 17U);
			EXPECT_EQ(Item.m_PreviewImage.m_pData, m_pOldPixels);
			ASSERT_NE(Item.m_PreviewImage.m_pData, nullptr);
			EXPECT_EQ(Item.m_PreviewImage.m_pData[0], 47);
			EXPECT_EQ(Item.m_PreviewImage.m_Width, 2U);
			EXPECT_EQ(Item.m_PreviewImage.m_Height, 1U);
			EXPECT_EQ(Item.m_PreviewImage.m_Format, CImageInfo::FORMAT_RGBA);
			EXPECT_EQ(Item.m_pDecodeJob, m_pOldJob);
			EXPECT_EQ(Item.m_pDecodeJob.use_count(), 2);
			EXPECT_EQ(Item.m_PreviewState, CMenus::SCustomItem::PREVIEW_STATE_READY);
			EXPECT_EQ(Item.m_PreviewEpoch, 13U);
			EXPECT_EQ(Item.m_PreviewListIndex, 6U);
			EXPECT_EQ(Item.m_PreviewBytes, 8U);
			EXPECT_EQ(Item.m_PreviewRequestedTextureSize, 128);
			EXPECT_EQ(Item.m_PreviewResidentBytes, 77U);
			EXPECT_TRUE(Item.m_PreviewResized);
			EXPECT_TRUE(Item.m_PreviewHighPriority);
		}
		void ExpectUnchanged(CMenus::SCustomItem *pData, size_t Capacity)
		{
			ASSERT_EQ(m_vList.size(), 1U);
			EXPECT_EQ(m_vList.data(), pData);
			EXPECT_EQ(m_vList.capacity(), Capacity);
			ExpectOldItem(m_vList.front());
			EXPECT_EQ(m_Resource.m_Live, 1);
		}
		void ExpectMergedOldItem()
		{
			const auto *pOld = FindItem(m_vList, "middle");
			ASSERT_NE(pOld, nullptr);
			ExpectOldItem(*pOld);
			EXPECT_EQ(m_Resource.m_Live, 1);
		}
	};
}

TEST_F(SettingsAssetListMerge, EmptyBatchDoesNotAllocateOrPrepare)
{
	const auto vEntries = MergeEntries({});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	const int Allocations = m_Resource.m_Allocations;
	m_Resource.m_FailAt = Allocations + 1;
	int Prepared = 0;
	EXPECT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, [&](auto &, const auto &) { ++Prepared; }));
	EXPECT_EQ(Prepared, 0);
	EXPECT_EQ(m_Resource.m_Allocations, Allocations);
	ExpectUnchanged(pData, Capacity);
}

TEST_F(SettingsAssetListMerge, ExistingNamesDoNotAllocateOrReplaceTheOldDisplay)
{
	const auto vEntries = MergeEntries({"middle", "middle"});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	const int Allocations = m_Resource.m_Allocations;
	m_Resource.m_FailAt = Allocations + 1;
	int Prepared = 0;
	EXPECT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, [&](auto &, const auto &) { ++Prepared; }));
	EXPECT_EQ(Prepared, 0);
	EXPECT_EQ(m_Resource.m_Allocations, Allocations);
	ExpectUnchanged(pData, Capacity);
}

TEST_F(SettingsAssetListMerge, UnorderedNewNamesUseCanonicalResourceOrder)
{
	const auto vEntries = MergeEntries({"zeta", "blank", "alpha", "default"});
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, [](auto &Item, const auto &Entry) {
		str_copy(Item.m_aAuthor, Entry.m_aName);
	}));
	ExpectNames(m_vList, {"default", "blank", "alpha", "middle", "zeta"});
	const auto *pAlpha = FindItem(m_vList, "alpha");
	ASSERT_NE(pAlpha, nullptr);
	EXPECT_STREQ(pAlpha->m_aAuthor, "alpha");
	EXPECT_FALSE(pAlpha->m_RenderTexture.IsValid());
	EXPECT_EQ(pAlpha->m_PreviewState, CMenus::SCustomItem::PREVIEW_STATE_UNLOADED);
	EXPECT_EQ(pAlpha->m_PreviewImage.m_pData, nullptr);
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetListMerge, DuplicateScanNamesPrepareOneNewItemAndKeepExistingOwners)
{
	const auto vEntries = MergeEntries({"middle", "alpha", "alpha", "middle"});
	int Prepared = 0;
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, [&](auto &Item, const auto &Entry) {
		++Prepared;
		EXPECT_STREQ(Item.m_aName, Entry.m_aName);
		str_copy(Item.m_aDisplayName, "prepared once");
	}));
	EXPECT_EQ(Prepared, 1);
	ExpectNames(m_vList, {"alpha", "middle"});
	EXPECT_STREQ(m_vList.front().m_aDisplayName, "prepared once");
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetListMerge, PreparationAllocationFailureKeepsOldStorageAndCanRetry)
{
	const auto vEntries = MergeEntries({"alpha", "zeta"});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	int Prepared = 0;
	EXPECT_FALSE(TryMergeSettingsAssetEntries(m_vList, vEntries, [&](auto &, const auto &) {
		if(++Prepared == 2)
			throw std::bad_alloc();
	}));
	EXPECT_EQ(Prepared, 2);
	ExpectUnchanged(pData, Capacity);
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing));
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetListMerge, PreparationLengthFailureKeepsOldStorageAndCanRetry)
{
	const auto vEntries = MergeEntries({"alpha"});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	EXPECT_FALSE(TryMergeSettingsAssetEntries(m_vList, vEntries, [](auto &, const auto &) {
		throw std::length_error("asset metadata is too large");
	}));
	ExpectUnchanged(pData, Capacity);
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing));
	ExpectNames(m_vList, {"alpha", "middle"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetListMerge, NewItemsAllocationFailureDoesNotStartPreparationAndCanRetry)
{
	const auto vEntries = MergeEntries({"alpha", "zeta"});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	m_Resource.m_FailAt = m_Resource.m_Allocations + 1;
	int Prepared = 0;
	EXPECT_FALSE(TryMergeSettingsAssetEntries(m_vList, vEntries, [&](auto &, const auto &) { ++Prepared; }));
	EXPECT_EQ(Prepared, 0);
	ExpectUnchanged(pData, Capacity);
	m_Resource.m_FailAt = -1;
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing));
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetListMerge, FinalStorageAllocationFailureDoesNotAppendAndCanRetry)
{
	const auto vEntries = MergeEntries({"zeta", "alpha"});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	m_Resource.m_FailAt = m_Resource.m_Allocations + 2;
	int Prepared = 0;
	EXPECT_FALSE(TryMergeSettingsAssetEntries(m_vList, vEntries, [&](auto &, const auto &) { ++Prepared; }));
	EXPECT_EQ(Prepared, 2);
	ExpectUnchanged(pData, Capacity);
	m_Resource.m_FailAt = -1;
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing));
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetListMerge, NullAllocationResultPreservesTheOldDisplayAndCanRetry)
{
	const auto vEntries = MergeEntries({"alpha"});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	m_Resource.m_ReturnNull = true;
	m_Resource.m_FailAt = m_Resource.m_Allocations + 2;
	EXPECT_FALSE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing));
	ExpectUnchanged(pData, Capacity);
	m_Resource.m_FailAt = -1;
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing));
	ExpectNames(m_vList, {"alpha", "middle"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetListMerge, CommitNeedsNoFurtherBorrowedAllocationAndPreservesPixelIdentity)
{
	const auto vEntries = MergeEntries({"zeta", "alpha"});
	const int Allocations = m_Resource.m_Allocations;
	m_Resource.m_FailAt = Allocations + 3;
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing));
	EXPECT_EQ(m_Resource.m_Allocations, Allocations + 2);
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetListMerge, RepeatingSuccessfulBatchDoesNotAllocateOrPrepare)
{
	const auto vEntries = MergeEntries({"alpha", "zeta"});
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing));
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	const int Allocations = m_Resource.m_Allocations;
	m_Resource.m_FailAt = Allocations + 1;
	int Prepared = 0;
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, [&](auto &, const auto &) { ++Prepared; }));
	EXPECT_EQ(Prepared, 0);
	EXPECT_EQ(m_Resource.m_Allocations, Allocations);
	EXPECT_EQ(m_vList.data(), pData);
	EXPECT_EQ(m_vList.capacity(), Capacity);
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetListMerge, PublicationInvalidatesBorrowedViewOnceBeforeOldStorageIsReleased)
{
	const auto vEntries = MergeEntries({"alpha", "zeta"});
	std::vector<CMenus::SCustomItem *> vpBorrowed{&m_vList.front()};
	m_Resource.m_pObservedBlock = m_vList.data();
	int Notifications = 0;
	bool Dirty = false;
	bool HadBorrowedView = false;
	bool SawNewOwner = false;
	bool OldStorageWasAlive = false;
	auto OnPublish = [&]() noexcept {
		++Notifications;
		HadBorrowedView = vpBorrowed.size() == 1;
		SawNewOwner = m_vList.size() == 3 && str_comp(m_vList.front().m_aName, "alpha") == 0;
		OldStorageWasAlive = !m_Resource.m_ObservedBlockReleased;
		vpBorrowed.clear();
		Dirty = true;
	};
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing, OnPublish));
	EXPECT_EQ(Notifications, 1);
	EXPECT_TRUE(HadBorrowedView);
	EXPECT_TRUE(SawNewOwner);
	EXPECT_TRUE(OldStorageWasAlive);
	EXPECT_TRUE(m_Resource.m_ObservedBlockReleased);
	EXPECT_TRUE(vpBorrowed.empty());
	EXPECT_TRUE(Dirty);
	ExpectMergedOldItem();
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing, OnPublish));
	EXPECT_EQ(Notifications, 1);
}

TEST_F(SettingsAssetListMerge, EmptyBatchLeavesTheBorrowedViewValidWithoutNotification)
{
	const auto vEntries = MergeEntries({});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	std::vector<CMenus::SCustomItem *> vpBorrowed{pData};
	int Notifications = 0;
	EXPECT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing, [&]() noexcept {
		++Notifications;
		vpBorrowed.clear();
	}));
	EXPECT_EQ(Notifications, 0);
	ASSERT_EQ(vpBorrowed.size(), 1U);
	EXPECT_EQ(vpBorrowed.front(), pData);
	ExpectUnchanged(pData, Capacity);
}

TEST_F(SettingsAssetListMerge, ExistingNamesKeepTheBorrowedViewValidWithoutNotification)
{
	const auto vEntries = MergeEntries({"middle", "middle"});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	std::vector<CMenus::SCustomItem *> vpBorrowed{pData};
	m_Resource.m_FailAt = m_Resource.m_Allocations + 1;
	int Notifications = 0;
	EXPECT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing, [&]() noexcept {
		++Notifications;
		vpBorrowed.clear();
	}));
	EXPECT_EQ(Notifications, 0);
	ASSERT_EQ(vpBorrowed.size(), 1U);
	EXPECT_EQ(vpBorrowed.front(), pData);
	ExpectUnchanged(pData, Capacity);
}

TEST_F(SettingsAssetListMerge, AllocationFailuresPreserveBorrowedViewUntilRetryPublishes)
{
	const auto vEntries = MergeEntries({"zeta", "alpha"});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	std::vector<CMenus::SCustomItem *> vpBorrowed{pData};
	int Notifications = 0;
	bool Dirty = false;
	auto OnPublish = [&]() noexcept {
		++Notifications;
		vpBorrowed.clear();
		Dirty = true;
	};
	for(const int AllocationStep : {1, 2})
	{
		SCOPED_TRACE(AllocationStep);
		m_Resource.m_FailAt = m_Resource.m_Allocations + AllocationStep;
		EXPECT_FALSE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing, OnPublish));
		EXPECT_EQ(Notifications, 0);
		EXPECT_FALSE(Dirty);
		ASSERT_EQ(vpBorrowed.size(), 1U);
		EXPECT_EQ(vpBorrowed.front(), pData);
		ExpectUnchanged(pData, Capacity);
	}
	m_Resource.m_FailAt = -1;
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing, OnPublish));
	EXPECT_EQ(Notifications, 1);
	EXPECT_TRUE(Dirty);
	EXPECT_TRUE(vpBorrowed.empty());
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetListMerge, AuthorAllocationFailureKeepsBorrowedViewUntilRetryPublishes)
{
	const auto vEntries = MergeEntries({"alpha"});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	std::vector<CMenus::SCustomItem *> vpBorrowed{pData};
	int Notifications = 0;
	auto OnPublish = [&]() noexcept {
		++Notifications;
		vpBorrowed.clear();
	};
	auto PrepareAuthor = [](auto &Item, const auto &) {
		str_copy(Item.m_aAuthor, "candidate author");
		throw std::bad_alloc();
	};
	EXPECT_FALSE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareAuthor, OnPublish));
	EXPECT_EQ(Notifications, 0);
	ASSERT_EQ(vpBorrowed.size(), 1U);
	EXPECT_EQ(vpBorrowed.front(), pData);
	ExpectUnchanged(pData, Capacity);
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing, OnPublish));
	EXPECT_EQ(Notifications, 1);
	EXPECT_TRUE(vpBorrowed.empty());
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetListMerge, AuthorLengthFailureKeepsBorrowedViewUntilRetryPublishes)
{
	const auto vEntries = MergeEntries({"alpha"});
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	std::vector<CMenus::SCustomItem *> vpBorrowed{pData};
	int Notifications = 0;
	auto OnPublish = [&]() noexcept {
		++Notifications;
		vpBorrowed.clear();
	};
	auto PrepareAuthor = [](auto &, const auto &) { throw std::length_error("author metadata exceeds capacity"); };
	EXPECT_FALSE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareAuthor, OnPublish));
	EXPECT_EQ(Notifications, 0);
	ASSERT_EQ(vpBorrowed.size(), 1U);
	EXPECT_EQ(vpBorrowed.front(), pData);
	ExpectUnchanged(pData, Capacity);
	ASSERT_TRUE(TryMergeSettingsAssetEntries(m_vList, vEntries, PrepareNothing, OnPublish));
	EXPECT_EQ(Notifications, 1);
	EXPECT_TRUE(vpBorrowed.empty());
	ExpectMergedOldItem();
}

TEST(SettingsAssetListMergeTypes, EmptyOldListAcceptsUnorderedEntries)
{
	std::vector<CMenus::SCustomItem> vList;
	const auto vEntries = MergeEntries({"zeta", "alpha"});
	ASSERT_TRUE(TryMergeSettingsAssetEntries(vList, vEntries, PrepareNothing));
	ExpectNames(vList, {"alpha", "zeta"});
}

TEST(SettingsAssetListMergeTypes, VectorCapacityLimitDoesNotChangeOldEntries)
{
	std::vector<CMenus::SCustomItem, CTwoItemAllocator<CMenus::SCustomItem>> vList;
	vList.reserve(2);
	vList.emplace_back();
	str_copy(vList.back().m_aName, "middle");
	vList.emplace_back();
	str_copy(vList.back().m_aName, "zeta");
	auto *pData = vList.data();
	const auto vEntries = MergeEntries({"alpha"});
	EXPECT_FALSE(TryMergeSettingsAssetEntries(vList, vEntries, PrepareNothing));
	EXPECT_EQ(vList.data(), pData);
	ExpectNames(vList, {"middle", "zeta"});
}

TEST(SettingsAssetListMergeTypes, NewStorageLengthErrorDoesNotPrepareOrChangeOldEntries)
{
	std::vector<CMenus::SCustomItem, CTwoItemAllocator<CMenus::SCustomItem>> vList;
	vList.emplace_back();
	str_copy(vList.front().m_aName, "middle");
	auto *pData = vList.data();
	const auto vEntries = MergeEntries({"alpha", "omega", "zeta"});
	int Prepared = 0;
	EXPECT_FALSE(TryMergeSettingsAssetEntries(vList, vEntries, [&](auto &, const auto &) { ++Prepared; }));
	EXPECT_EQ(Prepared, 0);
	EXPECT_EQ(vList.data(), pData);
	ExpectNames(vList, {"middle"});
}

TEST(SettingsAssetListMergeTypes, DefaultAllocatorReserveMovesExistingPixels)
{
	std::vector<CMenus::SCustomItem> vList;
	vList.emplace_back();
	str_copy(vList.front().m_aName, "middle");
	vList.front().m_PreviewImage.m_Width = 2;
	vList.front().m_PreviewImage.m_Height = 1;
	vList.front().m_PreviewImage.m_Format = CImageInfo::FORMAT_RGBA;
	vList.front().m_PreviewImage.m_pData = static_cast<uint8_t *>(calloc(8, 1));
	auto *pPixels = vList.front().m_PreviewImage.m_pData;
	ASSERT_NE(pPixels, nullptr);
	pPixels[0] = 53;
	vList.front().m_PreviewState = CMenus::SCustomItem::PREVIEW_STATE_READY;
	vList.reserve(vList.capacity() + 1);
	EXPECT_EQ(vList.front().m_PreviewImage.m_pData, pPixels);
	ASSERT_NE(vList.front().m_PreviewImage.m_pData, nullptr);
	EXPECT_EQ(vList.front().m_PreviewImage.m_pData[0], 53);
	EXPECT_EQ(vList.front().m_PreviewState, CMenus::SCustomItem::PREVIEW_STATE_READY);
}

TEST(SettingsAssetListMergeTypes, EntityMergeRetainsRenderAndArrayTextureGenerations)
{
	std::vector<CMenus::SCustomEntities> vList;
	vList.emplace_back();
	auto &Item = vList.front();
	str_copy(Item.m_aName, "middle");
	CAssetMergeHandleFactory Graphics;
	Item.m_RenderTexture = Graphics.Handle(7, 1);
	Item.m_aImages[0].m_Texture = Graphics.Handle(8, 5);
	Item.m_aImages[1].m_Texture = Graphics.Handle(8, 6);
	const auto vEntries = MergeEntries({"zeta", "alpha"});
	ASSERT_TRUE(TryMergeSettingsAssetEntries(vList, vEntries, PrepareNothing));
	ExpectNames(vList, {"alpha", "middle", "zeta"});
	const auto *pOld = FindItem(vList, "middle");
	ASSERT_NE(pOld, nullptr);
	EXPECT_EQ(pOld->m_RenderTexture.Id(), 7);
	EXPECT_EQ(pOld->m_RenderTexture.Generation(), 1U);
	EXPECT_EQ(pOld->m_aImages[0].m_Texture.Id(), 8);
	EXPECT_EQ(pOld->m_aImages[0].m_Texture.Generation(), 5U);
	EXPECT_EQ(pOld->m_aImages[1].m_Texture.Id(), 8);
	EXPECT_EQ(pOld->m_aImages[1].m_Texture.Generation(), 6U);
}

TEST(SettingsAssetListMergeTypes, EntityBackgroundMergeRetainsOldKindAndPreparesNewKinds)
{
	std::vector<CMenus::SCustomEntityBg> vList;
	vList.emplace_back();
	str_copy(vList.front().m_aName, "middle");
	vList.front().m_IsDirectory = true;
	auto vEntries = MergeEntries({"zeta", "alpha"});
	vEntries.front().m_IsDir = true;
	ASSERT_TRUE(TryMergeSettingsAssetEntries(vList, vEntries, [](auto &Item, const auto &Entry) {
		Item.m_IsDirectory = Entry.m_IsDir;
	}));
	ExpectNames(vList, {"alpha", "middle", "zeta"});
	EXPECT_FALSE(vList.front().m_IsDirectory);
	EXPECT_TRUE(vList[1].m_IsDirectory);
	EXPECT_TRUE(vList.back().m_IsDirectory);
}

namespace
{
	class SettingsAssetMergeBatch : public SettingsAssetListMerge
	{
	protected:
		size_t m_Cursor = 0;
		SSettingsResourceMergeBudget m_Budget{2, 7, ESettingsWarmupStopReason::TEXT_BUDGET, false};
		SSettingsWarmupFrameBudget m_FrameBudget{5, 6, 2, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET};

		void ExpectBudgets(const SSettingsResourceMergeBudget &Expected, const SSettingsWarmupFrameBudget &ExpectedFrame)
		{
			EXPECT_EQ(m_Budget.m_MaxListEntries, Expected.m_MaxListEntries);
			EXPECT_EQ(m_Budget.m_MaxGpuUploads, Expected.m_MaxGpuUploads);
			EXPECT_EQ(m_Budget.m_StopReason, Expected.m_StopReason);
			EXPECT_EQ(m_Budget.m_FrameMergeBudgetConsumed, Expected.m_FrameMergeBudgetConsumed);
			EXPECT_EQ(m_FrameBudget.m_MaxTextContainers, ExpectedFrame.m_MaxTextContainers);
			EXPECT_EQ(m_FrameBudget.m_MaxGpuUploads, ExpectedFrame.m_MaxGpuUploads);
			EXPECT_EQ(m_FrameBudget.m_MaxJobResultMerges, ExpectedFrame.m_MaxJobResultMerges);
			EXPECT_EQ(m_FrameBudget.m_StopReason, ExpectedFrame.m_StopReason);
		}
		bool Publish(const std::vector<CMenus::SSettingsAssetMergeEntry> &vBatch)
		{
			return TryMergeSettingsAssetEntries(m_vList, vBatch, PrepareNothing);
		}
	};
}

TEST_F(SettingsAssetMergeBatch, RejectedPublicationKeepsCursorAndBothBudgets)
{
	const auto vEntries = MergeEntries({"alpha", "zeta"});
	const auto OldBudget = m_Budget;
	const auto OldFrame = m_FrameBudget;
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	int Publications = 0;
	EXPECT_FALSE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &vBatch) {
		++Publications;
		EXPECT_EQ(vBatch.size(), 2U);
		return false;
	}));
	EXPECT_EQ(Publications, 1);
	EXPECT_EQ(m_Cursor, 0U);
	ExpectBudgets(OldBudget, OldFrame);
	ExpectUnchanged(pData, Capacity);
	ASSERT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &vBatch) { return Publish(vBatch); }));
	EXPECT_EQ(m_Cursor, 2U);
	EXPECT_EQ(m_FrameBudget.m_MaxJobResultMerges, 1);
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetMergeBatch, AllocationExceptionFromPublicationCanRetryTheSameBatch)
{
	const auto vEntries = MergeEntries({"alpha", "zeta"});
	const auto OldBudget = m_Budget;
	const auto OldFrame = m_FrameBudget;
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	EXPECT_FALSE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [](const auto &) -> bool {
		throw std::bad_alloc();
	}));
	EXPECT_EQ(m_Cursor, 0U);
	ExpectBudgets(OldBudget, OldFrame);
	ExpectUnchanged(pData, Capacity);
	ASSERT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &vBatch) { return Publish(vBatch); }));
	EXPECT_EQ(m_Cursor, vEntries.size());
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetMergeBatch, LengthExceptionFromPublicationKeepsCursorAndBudgets)
{
	const auto vEntries = MergeEntries({"alpha"});
	const auto OldBudget = m_Budget;
	const auto OldFrame = m_FrameBudget;
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	EXPECT_FALSE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [](const auto &) -> bool {
		throw std::length_error("asset publication exceeds capacity");
	}));
	EXPECT_EQ(m_Cursor, 0U);
	ExpectBudgets(OldBudget, OldFrame);
	ExpectUnchanged(pData, Capacity);
}

TEST_F(SettingsAssetMergeBatch, RealMergeAllocationFailureKeepsCursorAndCanRetry)
{
	const auto vEntries = MergeEntries({"zeta", "alpha"});
	const auto OldBudget = m_Budget;
	const auto OldFrame = m_FrameBudget;
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	m_Resource.m_FailAt = m_Resource.m_Allocations + 2;
	EXPECT_FALSE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &vBatch) { return Publish(vBatch); }));
	EXPECT_EQ(m_Cursor, 0U);
	ExpectBudgets(OldBudget, OldFrame);
	ExpectUnchanged(pData, Capacity);
	m_Resource.m_FailAt = -1;
	ASSERT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &vBatch) { return Publish(vBatch); }));
	EXPECT_EQ(m_Cursor, 2U);
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetMergeBatch, SuccessfulPublicationCommitsCursorAndConsumesOneFrameMerge)
{
	const auto vEntries = MergeEntries({"alpha"});
	const auto OldBudget = m_Budget;
	const auto OldFrame = m_FrameBudget;
	int Publications = 0;
	ASSERT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &vBatch) {
		++Publications;
		EXPECT_EQ(m_Cursor, 0U);
		ExpectBudgets(OldBudget, OldFrame);
		return Publish(vBatch);
	}));
	EXPECT_EQ(Publications, 1);
	EXPECT_EQ(m_Cursor, 1U);
	auto Expected = OldBudget;
	--Expected.m_MaxListEntries;
	Expected.m_FrameMergeBudgetConsumed = true;
	auto ExpectedFrame = OldFrame;
	--ExpectedFrame.m_MaxJobResultMerges;
	ExpectBudgets(Expected, ExpectedFrame);
	ExpectNames(m_vList, {"alpha", "middle"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetMergeBatch, PerBatchLimitPublishesOnlyAdmittedEntriesAndDefersTheRest)
{
	const auto vEntries = MergeEntries({"alpha", "zeta", "omega"});
	ASSERT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &vBatch) {
		EXPECT_EQ(vBatch.size(), 2U);
		return Publish(vBatch);
	}));
	EXPECT_EQ(m_Cursor, 2U);
	EXPECT_EQ(m_Budget.m_MaxListEntries, 0);
	EXPECT_EQ(m_Budget.m_StopReason, ESettingsWarmupStopReason::MERGE_BUDGET);
	EXPECT_TRUE(m_Budget.m_FrameMergeBudgetConsumed);
	EXPECT_EQ(m_FrameBudget.m_MaxJobResultMerges, 1);
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	m_Budget = {};
	ASSERT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &vBatch) {
		EXPECT_EQ(vBatch.size(), 1U);
		EXPECT_STREQ(vBatch.front().m_aName, "omega");
		return Publish(vBatch);
	}));
	EXPECT_EQ(m_Cursor, 3U);
	EXPECT_EQ(m_FrameBudget.m_MaxJobResultMerges, 0);
	ExpectNames(m_vList, {"alpha", "middle", "omega", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetMergeBatch, ListBudgetBlockDoesNotPublishOrConsumeAFrameMerge)
{
	const auto vEntries = MergeEntries({"alpha"});
	const auto OldFrame = m_FrameBudget;
	auto *pData = m_vList.data();
	const size_t Capacity = m_vList.capacity();
	for(const int Limit : {0, -1})
	{
		SCOPED_TRACE(Limit);
		m_Budget.m_MaxListEntries = Limit;
		auto Expected = m_Budget;
		Expected.m_StopReason = ESettingsWarmupStopReason::MERGE_BUDGET;
		int Publications = 0;
		EXPECT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &) {
			++Publications;
			return true;
		}));
		EXPECT_EQ(Publications, 0);
		EXPECT_EQ(m_Cursor, 0U);
		ExpectBudgets(Expected, OldFrame);
		ExpectUnchanged(pData, Capacity);
	}
}

TEST_F(SettingsAssetMergeBatch, SharedFrameBudgetBlockPublishesNothingAndReportsTheStopReason)
{
	const auto vEntries = MergeEntries({"alpha"});
	m_FrameBudget.m_MaxJobResultMerges = 0;
	auto Expected = m_Budget;
	Expected.m_StopReason = ESettingsWarmupStopReason::MERGE_BUDGET;
	auto ExpectedFrame = m_FrameBudget;
	ExpectedFrame.m_StopReason = ESettingsWarmupStopReason::MERGE_BUDGET;
	int Publications = 0;
	EXPECT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &) {
		++Publications;
		return true;
	}));
	EXPECT_EQ(Publications, 0);
	EXPECT_EQ(m_Cursor, 0U);
	ExpectBudgets(Expected, ExpectedFrame);
	ExpectOldItem(m_vList.front());
	m_FrameBudget.m_MaxJobResultMerges = 1;
	ASSERT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &vBatch) { return Publish(vBatch); }));
	EXPECT_EQ(m_Cursor, 1U);
	EXPECT_EQ(m_FrameBudget.m_MaxJobResultMerges, 0);
	ExpectNames(m_vList, {"alpha", "middle"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetMergeBatch, PreviouslyConsumedFrameMergeIsNotChargedTwice)
{
	const auto vEntries = MergeEntries({"alpha", "zeta"});
	m_Budget.m_FrameMergeBudgetConsumed = true;
	m_FrameBudget.m_MaxJobResultMerges = 0;
	const auto OldFrame = m_FrameBudget;
	auto Expected = m_Budget;
	Expected.m_MaxListEntries = 0;
	ASSERT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &vBatch) { return Publish(vBatch); }));
	EXPECT_EQ(m_Cursor, 2U);
	ExpectBudgets(Expected, OldFrame);
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetMergeBatch, NullFrameBudgetStillRespectsTheListLimit)
{
	const auto vEntries = MergeEntries({"alpha", "zeta", "omega"});
	ASSERT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, nullptr, [&](const auto &vBatch) { return Publish(vBatch); }));
	EXPECT_EQ(m_Cursor, 2U);
	EXPECT_EQ(m_Budget.m_MaxListEntries, 0);
	EXPECT_FALSE(m_Budget.m_FrameMergeBudgetConsumed);
	EXPECT_EQ(m_Budget.m_StopReason, ESettingsWarmupStopReason::MERGE_BUDGET);
	ExpectNames(m_vList, {"alpha", "middle", "zeta"});
	ExpectMergedOldItem();
}

TEST_F(SettingsAssetMergeBatch, EmptyInputDoesNotPublishOrChangeBudgets)
{
	const auto vEntries = MergeEntries({});
	const auto OldBudget = m_Budget;
	const auto OldFrame = m_FrameBudget;
	int Publications = 0;
	EXPECT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &) {
		++Publications;
		return true;
	}));
	EXPECT_EQ(Publications, 0);
	EXPECT_EQ(m_Cursor, 0U);
	ExpectBudgets(OldBudget, OldFrame);
	ExpectOldItem(m_vList.front());
}

TEST_F(SettingsAssetMergeBatch, CompletedCursorDoesNotRepublishOrChangeBudgets)
{
	const auto vEntries = MergeEntries({"alpha"});
	m_Cursor = vEntries.size();
	const auto OldBudget = m_Budget;
	const auto OldFrame = m_FrameBudget;
	int Publications = 0;
	EXPECT_TRUE(TryPublishSettingsAssetMergeBatch(vEntries, m_Cursor, m_Budget, &m_FrameBudget, [&](const auto &) {
		++Publications;
		return true;
	}));
	EXPECT_EQ(Publications, 0);
	EXPECT_EQ(m_Cursor, 1U);
	ExpectBudgets(OldBudget, OldFrame);
	ExpectOldItem(m_vList.front());
}
