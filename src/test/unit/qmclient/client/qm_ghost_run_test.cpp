#include <game/client/components/ghost_run.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/ghost_path.h>

#include <gtest/gtest.h>

#include <memory>
#include <memory_resource>
#include <new>
#include <vector>

namespace
{
	struct SRun
	{
		std::shared_ptr<int> m_pManagedTeeRenderInfo;
		qm_ghost_path::CChunkedPath<int> m_Path;
		bool Empty() const { return m_Path.Size() == 0; }
	};

	using CRow = CMenus::CGhostItem;

	// 只控制真实菜单容器的分配器，插入、停用和刷新都走生产代码。
	class CRowMemory : public std::pmr::memory_resource
	{
		void *do_allocate(size_t Bytes, size_t Alignment) override
		{
			if(m_Fail)
				throw std::bad_alloc();
			return std::pmr::new_delete_resource()->allocate(Bytes, Alignment);
		}
		void do_deallocate(void *pData, size_t Bytes, size_t Alignment) override
		{
			std::pmr::new_delete_resource()->deallocate(pData, Bytes, Alignment);
		}
		bool do_is_equal(const std::pmr::memory_resource &Other) const noexcept override { return this == &Other; }

	public:
		bool m_Fail = false;
	};

	struct CRunList
	{
		CRowMemory m_Memory;
		std::pmr::vector<CRow> m_vRows{&m_Memory};
		ghost_run::CPersonalBest<SRun, CRow> m_Best;
		SRun m_aSlots[2];
		int m_RemovedFiles = 0;

		void Finish(SRun &Track, int Time, bool Saved = false)
		{
			CRow Item;
			str_copy(Item.m_aPlayer, "runner");
			if(Saved)
				str_format(Item.m_aFilename, sizeof(Item.m_aFilename), "run-%d.gho", Time);
			Item.m_Time = Time;
			Item.m_Date = 12345;
			Item.m_Failed = false;
			Item.m_Own = true;
			Item.m_InMemoryBest = !Saved;
			m_Best.Finish(Item, Track);
		}

		bool Publish(int Previous, int Free)
		{
			return m_Best.Publish(m_aSlots, Previous, Free, [this](const CRow &Item) {
				return ghost_run::PublishRow(m_vRows, Item, true, [](const CRow &A, const CRow &B) { return A < B; }, [this](const CRow &) { ++m_RemovedFiles; });
			}, [this](int Slot) { m_aSlots[Slot].m_Path.Reset(); });
		}
	};
}

TEST(QmGhostRun, SkinAllocationFailurePreservesTheOldTrackAndAllowsRetry)
{
	SRun Active, Candidate;
	ASSERT_TRUE(Active.m_Path.Add(11));
	ASSERT_TRUE(Candidate.m_Path.Add(22));
	Active.m_pManagedTeeRenderInfo = std::make_shared<int>(1);
	EXPECT_FALSE(ghost_run::Prepare(Active, Candidate, [](const SRun &) -> std::shared_ptr<int> { throw std::bad_alloc(); }));
	ASSERT_NE(Active.m_Path.Get(0), nullptr);
	EXPECT_EQ(*Active.m_Path.Get(0), 11);
	ASSERT_NE(Active.m_pManagedTeeRenderInfo, nullptr);
	EXPECT_EQ(*Active.m_pManagedTeeRenderInfo, 1);

	ASSERT_TRUE(ghost_run::Prepare(Active, Candidate, [](const SRun &) { return std::make_shared<int>(2); }));
	ASSERT_NE(Active.m_Path.Get(0), nullptr);
	EXPECT_EQ(*Active.m_Path.Get(0), 22);
	EXPECT_EQ(*Active.m_pManagedTeeRenderInfo, 2);
}

TEST(QmGhostRun, RecordedBestKeepsItsSamplesWhenThePlaybackSkinCannotBeAllocated)
{
	CRunList List;
	SRun Recorded;
	ASSERT_TRUE(Recorded.m_Path.Add(42));
	EXPECT_FALSE(ghost_run::PrepareSkin(Recorded, [](const SRun &) -> std::shared_ptr<int> { throw std::bad_alloc(); }));
	List.Finish(Recorded, 500);
	ASSERT_TRUE(List.Publish(-1, 0));
	EXPECT_EQ(List.m_aSlots[0].m_pManagedTeeRenderInfo, nullptr);
	ASSERT_NE(List.m_aSlots[0].m_Path.Get(0), nullptr);
	EXPECT_EQ(*List.m_aSlots[0].m_Path.Get(0), 42);
}

TEST(QmGhostRun, MissingSkinDoesNotOccupyAnEmptySlot)
{
	SRun Slot, Candidate;
	ASSERT_TRUE(Candidate.m_Path.Add(10));
	EXPECT_FALSE(ghost_run::Prepare(Slot, Candidate, [](const SRun &) { return std::shared_ptr<int>{}; }));
	EXPECT_TRUE(Slot.Empty());
}

TEST(QmGhostRun, NewBestReusesThePreviousBestSlotWhenAllSlotsAreOccupied)
{
	CRunList List;
	SRun Previous, Faster;
	ASSERT_TRUE(List.m_aSlots[0].m_Path.Add(1));
	ASSERT_TRUE(Previous.m_Path.Add(2));
	List.Finish(Previous, 500);
	ASSERT_TRUE(List.Publish(-1, 1));
	ASSERT_TRUE(Faster.m_Path.Add(3));
	List.Finish(Faster, 490);
	ASSERT_TRUE(List.Publish(1, -1));
	EXPECT_EQ(*List.m_aSlots[0].m_Path.Get(0), 1);
	ASSERT_NE(List.m_aSlots[1].m_Path.Get(0), nullptr);
	EXPECT_EQ(*List.m_aSlots[1].m_Path.Get(0), 3);
	ASSERT_EQ(List.m_vRows.size(), 1u);
	EXPECT_EQ(List.m_vRows[0].m_Slot, 1);
}

TEST(QmGhostRun, RankRefreshRestoresTheUnsavedRowAndTrackEvenWithNoFreeSlots)
{
	CRunList List;
	SRun NewRun;
	ASSERT_TRUE(List.m_aSlots[0].m_Path.Add(1));
	ASSERT_TRUE(List.m_aSlots[1].m_Path.Add(2));
	ASSERT_TRUE(NewRun.m_Path.Add(42));
	List.Finish(NewRun, 500);
	ASSERT_TRUE(List.Publish(-1, -1));
	List.m_vRows.clear();
	List.m_Best.RestoreRow(List.m_vRows);
	ASSERT_EQ(List.m_vRows.size(), 1u);
	const auto &Row = List.m_vRows[0];
	EXPECT_STREQ(Row.m_aPlayer, "runner");
	EXPECT_EQ(Row.m_Time, 500);
	EXPECT_EQ(Row.m_Date, 12345);
	EXPECT_FALSE(Row.HasFile());
	EXPECT_FALSE(Row.m_Failed);
	EXPECT_TRUE(Row.m_InMemoryBest);
	EXPECT_FALSE(Row.Active());
	ASSERT_NE(List.m_Best.UnsavedTrack(List.m_aSlots), nullptr);
	ASSERT_NE(List.m_Best.UnsavedTrack(List.m_aSlots)->m_Path.Get(0), nullptr);
	EXPECT_EQ(*List.m_Best.UnsavedTrack(List.m_aSlots)->m_Path.Get(0), 42);
	EXPECT_FALSE(List.m_Best.Activate(List.m_aSlots, 0));
	EXPECT_TRUE(List.m_Best.ShouldActivate());
	List.m_Best.Unload(0, List.m_aSlots[0]);
	List.m_aSlots[0].m_Path.Reset();
	ASSERT_TRUE(List.m_Best.Activate(List.m_aSlots, 0));
	EXPECT_EQ(*List.m_aSlots[0].m_Path.Get(0), 42);
	List.m_vRows.clear();
	List.m_Best.RestoreRow(List.m_vRows);
	ASSERT_EQ(List.m_vRows.size(), 1u);
	EXPECT_EQ(List.m_vRows[0].m_Slot, 0);
}

TEST(QmGhostRun, DeactivateAllKeepsTheBestInactiveUntilThePlayerActivatesIt)
{
	CRunList List;
	SRun NewRun;
	ASSERT_TRUE(NewRun.m_Path.Add(42));
	List.Finish(NewRun, 500);
	ASSERT_TRUE(List.Publish(-1, -1));
	ASSERT_TRUE(List.m_Best.ShouldActivate());
	List.m_Best.Suspend();
	List.m_Best.Unload(0, List.m_aSlots[0]);
	EXPECT_FALSE(List.m_Best.ShouldActivate());
	ASSERT_NE(List.m_Best.UnsavedItem(), nullptr);
	EXPECT_FALSE(List.m_Best.UnsavedItem()->Active());
	ASSERT_TRUE(List.m_Best.Activate(List.m_aSlots, 0));
	EXPECT_EQ(*List.m_aSlots[0].m_Path.Get(0), 42);
	List.m_Best.Suspend();
	List.m_Best.Unload(0, List.m_aSlots[0]);
	EXPECT_TRUE(List.m_aSlots[0].Empty());
	EXPECT_FALSE(List.m_Best.ShouldActivate());
	ASSERT_NE(List.m_Best.UnsavedTrack(List.m_aSlots)->m_Path.Get(0), nullptr);
	EXPECT_EQ(*List.m_Best.UnsavedTrack(List.m_aSlots)->m_Path.Get(0), 42);
}

TEST(QmGhostRun, MenuAllocationFailureKeepsTheOldRowSlotAndTheFasterTrackForRetry)
{
	CRunList List;
	SRun Old, Faster, Slower;
	ASSERT_TRUE(Old.m_Path.Add(1));
	ASSERT_TRUE(Faster.m_Path.Add(2));
	ASSERT_TRUE(Slower.m_Path.Add(3));
	List.Finish(Old, 500, true);
	ASSERT_TRUE(List.Publish(-1, 0));
	List.Finish(Faster, 480, true);
	List.m_Memory.m_Fail = true;
	ASSERT_FALSE(List.Publish(0, -1));
	ASSERT_EQ(List.m_vRows.size(), 1u);
	EXPECT_EQ(List.m_vRows[0].m_Time, 500);
	EXPECT_TRUE(List.m_vRows[0].m_Own);
	EXPECT_EQ(List.m_vRows[0].m_Slot, 0);
	EXPECT_EQ(*List.m_aSlots[0].m_Path.Get(0), 1);
	EXPECT_EQ(List.m_RemovedFiles, 0);
	List.Finish(Slower, 490);
	ASSERT_NE(List.m_Best.FinishedItem(), nullptr);
	EXPECT_EQ(List.m_Best.FinishedItem()->m_Time, 480);
	List.m_Memory.m_Fail = false;
	ASSERT_TRUE(List.Publish(0, -1));
	EXPECT_EQ(List.m_Best.FinishedItem(), nullptr);
	ASSERT_EQ(List.m_vRows.size(), 1u);
	EXPECT_EQ(List.m_vRows[0].m_Time, 480);
	EXPECT_EQ(List.m_vRows[0].m_Slot, 0);
	EXPECT_EQ(List.m_RemovedFiles, 1);
	EXPECT_EQ(*List.m_aSlots[0].m_Path.Get(0), 2);
}

TEST(QmGhostRun, FailedPublicationKeepsBothTheOldUnsavedBestAndTheNewCandidate)
{
	CRunList List;
	SRun Old, Faster;
	ASSERT_TRUE(Old.m_Path.Add(10));
	ASSERT_TRUE(Faster.m_Path.Add(20));
	List.Finish(Old, 500);
	ASSERT_TRUE(List.Publish(-1, -1));
	List.Finish(Faster, 480);
	List.m_Memory.m_Fail = true;
	ASSERT_FALSE(List.Publish(-1, -1));
	List.m_vRows.clear();
	List.m_Best.RestoreRow(List.m_vRows);
	ASSERT_EQ(List.m_vRows.size(), 1u);
	EXPECT_EQ(List.m_vRows.front().m_Time, 500);
	ASSERT_NE(List.m_Best.UnsavedTrack(List.m_aSlots)->m_Path.Get(0), nullptr);
	EXPECT_EQ(*List.m_Best.UnsavedTrack(List.m_aSlots)->m_Path.Get(0), 10);
	ASSERT_NE(List.m_Best.FinishedItem(), nullptr);
	EXPECT_EQ(List.m_Best.FinishedItem()->m_Time, 480);
	List.m_vRows.front().m_Own = true;
	List.m_Memory.m_Fail = false;
	ASSERT_TRUE(List.Publish(-1, -1));
	ASSERT_EQ(List.m_vRows.size(), 1u);
	EXPECT_EQ(List.m_vRows.front().m_Time, 480);
	ASSERT_NE(List.m_Best.UnsavedTrack(List.m_aSlots)->m_Path.Get(0), nullptr);
	EXPECT_EQ(*List.m_Best.UnsavedTrack(List.m_aSlots)->m_Path.Get(0), 20);
}

TEST(QmGhostRun, DeactivationWhileAnInsertionIsPendingAlsoAppliesToTheRetry)
{
	CRunList List;
	SRun NewRun;
	ASSERT_TRUE(NewRun.m_Path.Add(9));
	List.Finish(NewRun, 400);
	List.m_Memory.m_Fail = true;
	ASSERT_FALSE(List.Publish(-1, 0));
	List.m_Best.Suspend();
	List.m_Memory.m_Fail = false;
	ASSERT_TRUE(List.Publish(-1, 0));
	EXPECT_TRUE(List.m_aSlots[0].Empty());
	EXPECT_FALSE(List.m_Best.ShouldActivate());
	ASSERT_NE(List.m_Best.UnsavedItem(), nullptr);
	EXPECT_FALSE(List.m_Best.UnsavedItem()->Active());
	ASSERT_TRUE(List.m_Best.Activate(List.m_aSlots, 0));
	EXPECT_EQ(*List.m_aSlots[0].m_Path.Get(0), 9);
}

TEST(QmGhostRun, SavedOrDeletedTrackDoesNotReturnDuringTheNextRefresh)
{
	CRunList List;
	SRun Old, Faster;
	ASSERT_TRUE(Old.m_Path.Add(1));
	List.Finish(Old, 500);
	ASSERT_TRUE(List.Publish(-1, -1));
	List.m_Best.ForgetUnsaved();
	List.m_vRows.clear();
	List.m_Best.RestoreRow(List.m_vRows);
	EXPECT_TRUE(List.m_vRows.empty());
	ASSERT_TRUE(Faster.m_Path.Add(2));
	List.Finish(Faster, 490, true);
	ASSERT_TRUE(List.Publish(-1, -1));
	EXPECT_EQ(List.m_Best.UnsavedItem(), nullptr);
}
