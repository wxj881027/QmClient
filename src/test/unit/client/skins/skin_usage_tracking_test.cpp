#include <base/borrowed_allocator.h>

#include <game/client/components/qmclient/skin_usage_tracking.h>

#include <gtest/gtest.h>

#include <iterator>
#include <list>
#include <new>
#include <string>

namespace
{
	class CTrackingMemory : public IAllocationResource
	{
		void *Allocate(size_t Bytes, size_t Alignment) override
		{
			if(m_Fail)
				throw std::bad_alloc();
			++m_Allocations;
			return IAllocationResource::AllocateDefault(Bytes, Alignment);
		}
		void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override
		{
			++m_Deallocations;
			IAllocationResource::DeallocateDefault(pData, Bytes, Alignment);
		}

	public:
		bool m_Fail = false;
		int m_Allocations = 0, m_Deallocations = 0;
	};
	using TList = std::list<std::string, CBorrowedAllocator<std::string>>;
}

TEST(SkinUsageTracking, ExistingEntryMovesToFrontWithoutAllocationOrInvalidatingReferences)
{
	CTrackingMemory Memory;
	TList Usage(&Memory), Background(&Memory);
	Usage.emplace_back("other");
	Usage.emplace_back("skin");
	std::optional<TList::iterator> Entry = std::prev(Usage.end()), BackgroundEntry;
	const auto *pName = &*Entry.value();
	const int Allocations = Memory.m_Allocations, Deallocations = Memory.m_Deallocations;
	Memory.m_Fail = true;
	QmTouchSkinUsageEntry(Usage, Entry, Background, BackgroundEntry, "skin");
	EXPECT_EQ(Usage.front(), "skin");
	EXPECT_EQ(Usage.back(), "other");
	EXPECT_EQ(&*Entry.value(), pName);
	EXPECT_EQ(Memory.m_Allocations, Allocations);
	EXPECT_EQ(Memory.m_Deallocations, Deallocations);
}

TEST(SkinUsageTracking, RepeatedTouchOfFrontEntryKeepsOneStableNode)
{
	CTrackingMemory Memory;
	TList Usage(&Memory), Background(&Memory);
	Usage.emplace_back("skin");
	std::optional<TList::iterator> Entry = Usage.begin(), BackgroundEntry;
	const auto *pName = &*Entry.value();
	const int Allocations = Memory.m_Allocations, Deallocations = Memory.m_Deallocations;
	Memory.m_Fail = true;
	for(int Attempt = 0; Attempt < 3; ++Attempt)
		QmTouchSkinUsageEntry(Usage, Entry, Background, BackgroundEntry, "skin");
	ASSERT_EQ(Usage.size(), 1u);
	EXPECT_EQ(&Usage.front(), pName);
	EXPECT_EQ(Memory.m_Allocations, Allocations);
	EXPECT_EQ(Memory.m_Deallocations, Deallocations);
}

TEST(SkinUsageTracking, FailedPromotionPreservesBackgroundNodeAndOtherUsageEntries)
{
	CTrackingMemory Memory;
	TList Usage(&Memory), Background(&Memory);
	Usage.emplace_back("other");
	Background.emplace_back("skin");
	std::optional<TList::iterator> Entry, BackgroundEntry = Background.begin();
	const auto *pBackgroundName = &*BackgroundEntry.value();
	Memory.m_Fail = true;
	EXPECT_THROW(QmTouchSkinUsageEntry(Usage, Entry, Background, BackgroundEntry, "skin"), std::bad_alloc);
	EXPECT_FALSE(Entry.has_value());
	ASSERT_TRUE(BackgroundEntry.has_value());
	EXPECT_EQ(&*BackgroundEntry.value(), pBackgroundName);
	ASSERT_EQ(Background.size(), 1u);
	EXPECT_EQ(Background.front(), "skin");
	ASSERT_EQ(Usage.size(), 1u);
	EXPECT_EQ(Usage.front(), "other");
}

TEST(SkinUsageTracking, PromotionAfterFailurePublishesUsageThenRemovesBackgroundIdentity)
{
	CTrackingMemory Memory;
	TList Usage(&Memory), Background(&Memory);
	Background.emplace_back("skin");
	std::optional<TList::iterator> Entry, BackgroundEntry = Background.begin();
	Memory.m_Fail = true;
	EXPECT_THROW(QmTouchSkinUsageEntry(Usage, Entry, Background, BackgroundEntry, "skin"), std::bad_alloc);
	Memory.m_Fail = false;
	QmTouchSkinUsageEntry(Usage, Entry, Background, BackgroundEntry, "skin");
	ASSERT_TRUE(Entry.has_value());
	EXPECT_EQ(Entry.value(), Usage.begin());
	ASSERT_EQ(Usage.size(), 1u);
	EXPECT_EQ(Usage.front(), "skin");
	EXPECT_TRUE(Background.empty());
	EXPECT_FALSE(BackgroundEntry.has_value());
}
