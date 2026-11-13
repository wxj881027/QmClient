#include <game/client/components/qmclient/bg_draw_state.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <list>
#include <new>
#include <vector>

TEST(QmBgDrawPublication, NewItemAllocationFailurePreservesOldItemsBorrowsAndDirtyState)
{
	std::list<int> Items{10, 20};
	std::array<int *, 2> apActive{&Items.front(), &Items.back()};
	const auto apBefore = apActive;
	bool Dirty = false, Threw = false, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			QmBgDrawAddItem(Items, apActive, 2, Dirty, 30);
		}
		catch(const std::bad_alloc &)
		{
			Threw = true;
		}
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Threw);
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(Attempts, 1u);
	EXPECT_EQ((std::vector<int>(Items.begin(), Items.end())), (std::vector<int>{10, 20}));
	EXPECT_EQ(apActive, apBefore);
	EXPECT_FALSE(Dirty);
}

TEST(QmBgDrawPublication, NewItemIsConstructedBeforeTrimmingAndRetainedBorrowKeepsItsAddress)
{
	std::list<int> Items{10, 20};
	int *pRetained = &Items.back();
	std::array<int *, 2> apActive{&Items.front(), pRetained};
	bool Dirty = false;
	int *pNew = QmBgDrawAddItem(Items, apActive, 2, Dirty, 30);
	EXPECT_EQ((std::vector<int>(Items.begin(), Items.end())), (std::vector<int>{20, 30}));
	EXPECT_EQ(pNew, &Items.back());
	EXPECT_EQ(apActive[0], nullptr);
	EXPECT_EQ(apActive[1], pRetained);
	EXPECT_TRUE(Dirty);
}

TEST(QmBgDrawPublication, DisabledCapacityClearsBorrowsWithoutConstructingANewItem)
{
	struct SItem
	{
		explicit SItem(bool Fail)
		{
			if(Fail)
				throw std::bad_alloc();
		}
	};
	std::list<SItem> Items;
	Items.emplace_back(false);
	std::array<SItem *, 1> apActive{&Items.front()};
	bool Dirty = false;
	EXPECT_EQ(QmBgDrawAddItem(Items, apActive, 0, Dirty, true), nullptr);
	EXPECT_TRUE(Items.empty());
	EXPECT_EQ(apActive[0], nullptr);
	EXPECT_TRUE(Dirty);
}

TEST(QmBgDrawPublication, CandidateConstructionFailureKeepsTheCompleteOldDrawing)
{
	std::list<int> Items{10, 20};
	std::array<int *, 2> apActive{&Items.front(), &Items.back()};
	const auto apBefore = apActive;
	const std::array<int, 2> aInputs{30, 40};
	bool Dirty = false;
	EXPECT_THROW(Dirty |= QmBgDrawAppendLoaded(Items, apActive, 2, aInputs, [](auto &Candidate, int Value) {
		if(Value == 40)
			throw std::bad_alloc();
		Candidate.emplace_back(Value);
	}),
		std::bad_alloc);
	EXPECT_EQ((std::vector<int>(Items.begin(), Items.end())), (std::vector<int>{10, 20}));
	EXPECT_EQ(apActive, apBefore);
	EXPECT_FALSE(Dirty);
}

TEST(QmBgDrawPublication, CompleteLoadedBatchAppendsInOrderAndKeepsTheNewestOldItem)
{
	std::list<int> Items{10, 20};
	int *pRetained = &Items.back();
	std::array<int *, 2> apActive{&Items.front(), pRetained};
	const std::array<int, 2> aInputs{30, 40};
	EXPECT_TRUE(QmBgDrawAppendLoaded(Items, apActive, 3, aInputs, [](auto &Candidate, int Value) { Candidate.emplace_back(Value); }));
	EXPECT_EQ((std::vector<int>(Items.begin(), Items.end())), (std::vector<int>{20, 30, 40}));
	EXPECT_EQ(apActive[0], nullptr);
	EXPECT_EQ(apActive[1], pRetained);
}

TEST(QmBgDrawPublication, NativeCandidateAllocationFailuresNeverPublishAPartialBatch)
{
	bool ReachedSuccess = false;
	for(size_t BeforeFailure = 0; BeforeFailure < 16; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		std::list<int> Items{10, 20};
		std::array<int *, 2> apActive{&Items.front(), &Items.back()};
		const auto apBefore = apActive;
		const std::array<int, 2> aInputs{30, 40};
		bool Dirty = false, Threw = false, Triggered = false;
		size_t Attempts = 0;
		{
			CPredictionAllocationFailure Failure(BeforeFailure);
			try
			{
				Dirty |= QmBgDrawAppendLoaded(Items, apActive, 2, aInputs, [](auto &Candidate, int Value) { Candidate.emplace_back(Value); });
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Failure.Triggered();
			Attempts = Failure.Attempts();
		}
		if(!Triggered)
		{
			EXPECT_FALSE(Threw);
			EXPECT_TRUE(Dirty);
			EXPECT_EQ((std::vector<int>(Items.begin(), Items.end())), (std::vector<int>{30, 40}));
			EXPECT_EQ(apActive, (std::array<int *, 2>{nullptr, nullptr}));
			ReachedSuccess = true;
			break;
		}
		EXPECT_TRUE(Threw);
		EXPECT_EQ(Attempts, BeforeFailure + 1);
		EXPECT_FALSE(Dirty);
		EXPECT_EQ((std::vector<int>(Items.begin(), Items.end())), (std::vector<int>{10, 20}));
		EXPECT_EQ(apActive, apBefore);
	}
	EXPECT_TRUE(ReachedSuccess);
}

TEST(QmBgDrawPublication, OversizedPreparedBatchIsRejectedBeforeOldItemsAreRemoved)
{
	std::list<int> Items{10};
	std::array<int *, 1> apActive{&Items.front()};
	int *pBefore = apActive[0];
	const std::array<int, 2> aInputs{20, 30};
	EXPECT_THROW(QmBgDrawAppendLoaded(Items, apActive, 1, aInputs, [](auto &Candidate, int Value) { Candidate.emplace_back(Value); }), std::length_error);
	EXPECT_EQ((std::vector<int>(Items.begin(), Items.end())), (std::vector<int>{10}));
	EXPECT_EQ(apActive[0], pBefore);
}

TEST(QmBgDrawPublication, EmptyLoadedBatchPreservesTheExistingDrawingAndBorrow)
{
	std::list<int> Items{10};
	std::array<int *, 1> apActive{&Items.front()};
	int *pBefore = apActive[0];
	const std::array<int, 0> aInputs{};
	EXPECT_FALSE(QmBgDrawAppendLoaded(Items, apActive, 2, aInputs, [](auto &Candidate, int Value) { Candidate.emplace_back(Value); }));
	EXPECT_EQ((std::vector<int>(Items.begin(), Items.end())), (std::vector<int>{10}));
	EXPECT_EQ(apActive[0], pBefore);
}

TEST(QmBgDrawPublication, EmptyLoadedBatchAtDisabledCapacityReleasesOldBorrows)
{
	std::list<int> Items{10};
	std::array<int *, 1> apActive{&Items.front()};
	const std::array<int, 0> aInputs{};
	EXPECT_TRUE(QmBgDrawAppendLoaded(Items, apActive, 0, aInputs, [](auto &Candidate, int Value) { Candidate.emplace_back(Value); }));
	EXPECT_TRUE(Items.empty());
	EXPECT_EQ(apActive[0], nullptr);
}
