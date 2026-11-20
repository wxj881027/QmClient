#include <game/client/QmUi/SettingsCardDeckLogic.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>

namespace
{
	class CSettingsCardDropAllocationTest : public testing::Test
	{
	protected:
		void SetUp() override
		{
			ResetLayout();
			m_vActiveIndices = {
				m_Model.FindByStableId("left-a"),
				m_Model.FindByStableId("left-b"),
				m_Model.FindByStableId("right-a"),
				m_Model.FindByStableId("right-b"),
			};
			ASSERT_TRUE(m_Model.Serialize(m_aOriginal, sizeof(m_aOriginal)));
		}

		template<typename FCommit>
		void CheckDrop(const FCommit &Commit)
		{
			size_t AllocationCount;
			bool Changed;
			{
				CPredictionAllocationFailure Trace(std::numeric_limits<size_t>::max());
				Changed = Commit();
				AllocationCount = Trace.Attempts();
			}
			ASSERT_TRUE(Changed);
			ASSERT_GT(AllocationCount, 0u);
			char aExpected[512];
			ASSERT_TRUE(m_Model.Serialize(aExpected, sizeof(aExpected)));
			ASSERT_STRNE(aExpected, m_aOriginal);

			bool RejectedDrop = false;
			for(size_t Index = 0; Index < AllocationCount; ++Index)
			{
				SCOPED_TRACE(Index);
				ResetLayout();
				const auto LayoutRevision = m_Model.LayoutRevision();
				const auto StateIndexRevision = m_Model.StateIndexRevision();
				bool Triggered;
				{
					CPredictionAllocationFailure Failure(Index);
					Changed = Commit();
					Triggered = Failure.Triggered();
				}
				ASSERT_TRUE(Triggered);
				if(!Changed)
				{
					RejectedDrop = true;
					char aAfterFailure[512];
					ASSERT_TRUE(m_Model.Serialize(aAfterFailure, sizeof(aAfterFailure)));
					EXPECT_STREQ(aAfterFailure, m_aOriginal);
					EXPECT_EQ(m_Model.LayoutRevision(), LayoutRevision);
					EXPECT_FALSE(m_Model.IsDirty());
					ASSERT_TRUE(Commit());
				}
				char aAfterDrop[512];
				ASSERT_TRUE(m_Model.Serialize(aAfterDrop, sizeof(aAfterDrop)));
				EXPECT_STREQ(aAfterDrop, aExpected);
				EXPECT_GT(m_Model.LayoutRevision(), LayoutRevision);
				EXPECT_EQ(m_Model.StateIndexRevision(), StateIndexRevision);
				EXPECT_TRUE(m_Model.IsDirty());
			}
			EXPECT_TRUE(RejectedDrop);
		}

		qm_card_order::CModel m_Model;
		std::vector<int> m_vActiveIndices;

	private:
		void ResetLayout()
		{
			m_Model.SetEntries({
				{"left-a", "sound", 1, 0},
				{"left-hidden", "sound", 1, 1},
				{"left-b", "sound", 1, 2},
				{"right-hidden", "sound", 2, 0},
				{"right-a", "sound", 2, 1},
				{"right-b", "sound", 2, 2},
				{"other-tab", "other", 2, 0},
			});
			m_Model.ClearDirty();
		}

		char m_aOriginal[512];
	};
}

TEST(SettingsCardDropAllocation, SingleColumnDragPreviewReordersWithoutAllocation)
{
	std::array<std::vector<int>, 3> aColumns{
		std::vector<int>{0},
		std::vector<int>{1},
		std::vector<int>{2, 3, 4},
	};
	bool AllocationAttempted;
	{
		CPredictionAllocationFailure Failure;
		ApplySettingsCardDeckSingleColumnDragPlacement(aColumns, 4, 0);
		AllocationAttempted = Failure.Triggered();
	}
	EXPECT_FALSE(AllocationAttempted);
	EXPECT_EQ(aColumns[0], (std::vector<int>{0}));
	EXPECT_EQ(aColumns[1], (std::vector<int>{4}));
	EXPECT_EQ(aColumns[2], (std::vector<int>{1, 2, 3}));
}

TEST_F(CSettingsCardDropAllocationTest, SingleColumnDropCanRetryWithoutPublishingPartOfTheOrder)
{
	CheckDrop([&] {
		return CommitSettingsCardDeckSingleColumnDrop(m_Model, "sound", "right-b", 1, m_vActiveIndices);
	});
}

TEST_F(CSettingsCardDropAllocationTest, CrossColumnDropCanRetryWithoutChangingEitherColumn)
{
	CheckDrop([&] {
		return CommitSettingsCardDeckDrop(m_Model, "sound", "right-b", 1, 1, &m_vActiveIndices);
	});
}
