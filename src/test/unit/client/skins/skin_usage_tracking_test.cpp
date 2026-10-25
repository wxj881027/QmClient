#include <game/client/components/qmclient/skin_usage_tracking.h>

#include <gtest/gtest.h>

#include <iterator>
#include <list>
#include <memory_resource>
#include <new>
#include <string>

namespace
{
	class CTrackingMemory : public std::pmr::memory_resource
	{
		void *do_allocate(size_t Bytes, size_t Alignment) override
		{
			if(m_Fail)
				throw std::bad_alloc();
			++m_Allocations;
			return std::pmr::new_delete_resource()->allocate(Bytes, Alignment);
		}
		void do_deallocate(void *pData, size_t Bytes, size_t Alignment) override
		{
			++m_Deallocations;
			std::pmr::new_delete_resource()->deallocate(pData, Bytes, Alignment);
		}
		bool do_is_equal(const std::pmr::memory_resource &Other) const noexcept override { return this == &Other; }

	public:
		bool m_Fail = false;
		int m_Allocations = 0, m_Deallocations = 0;
	};
	using TList = std::pmr::list<std::string>;
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
