#include <game/client/QmUi/QmCardOrderModel.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <string>

TEST(QmCardOrderModelSwap, PreparedContentTransfersEntriesIndexAndDirtyStateTogether)
{
	qm_card_order::CModel Published;
	qm_card_order::CModel Prepared;
	Published.SetEntries({{"old-first", "old-tab", 1, 0}, {"old-second", "old-tab", 2, 0}});
	Prepared.SetEntries({{"new-first", "new-tab", 2, 0}, {"new-second", "new-tab", 2, 1}});
	Prepared.ClearDirty();
	Published.SwapContent(Prepared);
	ASSERT_EQ(Published.Count(), 2);
	EXPECT_EQ(Published.StateIndexForStableId("new-first"), 0);
	EXPECT_EQ(Published.FindByStableId("new-second"), 1);
	EXPECT_EQ(Published.FindByStableId("old-first"), -1);
	EXPECT_STREQ(Published.Entry(0).m_pStableId, "new-first");
	EXPECT_STREQ(Published.Entry(0).m_pDefaultTab, "new-tab");
	EXPECT_EQ(Published.Entry(0).m_Column, 2);
	EXPECT_EQ(Published.Entry(1).m_OrderInColumn, 1);
	EXPECT_FALSE(Published.IsDirty());
	ASSERT_EQ(Prepared.Count(), 2);
	EXPECT_EQ(Prepared.StateIndexForStableId("old-first"), 0);
	EXPECT_EQ(Prepared.FindByStableId("old-second"), 1);
	EXPECT_EQ(Prepared.FindByStableId("new-first"), -1);
	EXPECT_STREQ(Prepared.Entry(0).m_pDefaultTab, "old-tab");
	EXPECT_TRUE(Prepared.IsDirty());
}

TEST(QmCardOrderModelSwap, EachReceiverAdvancesItsOwnVersionsRatherThanImportingOtherVersions)
{
	qm_card_order::CModel Published;
	qm_card_order::CModel Prepared;
	for(int Index = 0; Index < 3; ++Index)
		Published.SetEntries({{"old", "published", 1, 0}});
	Published.Move("old", 2, 0);
	Prepared.SetEntries({{"new", "prepared", 1, 0}});
	const auto PublishedLayout = Published.LayoutRevision();
	const auto PublishedIndex = Published.StateIndexRevision();
	const auto PreparedLayout = Prepared.LayoutRevision();
	const auto PreparedIndex = Prepared.StateIndexRevision();
	Published.SwapContent(Prepared);
	EXPECT_EQ(Published.LayoutRevision(), PublishedLayout + 1);
	EXPECT_EQ(Published.StateIndexRevision(), PublishedIndex + 1);
	EXPECT_EQ(Prepared.LayoutRevision(), PreparedLayout + 1);
	EXPECT_EQ(Prepared.StateIndexRevision(), PreparedIndex + 1);
	Prepared.SwapContent(Published);
	EXPECT_EQ(Published.LayoutRevision(), PublishedLayout + 2);
	EXPECT_EQ(Published.StateIndexRevision(), PublishedIndex + 2);
	EXPECT_EQ(Prepared.LayoutRevision(), PreparedLayout + 2);
	EXPECT_EQ(Prepared.StateIndexRevision(), PreparedIndex + 2);
	EXPECT_EQ(Published.FindByStableId("old"), 0);
	EXPECT_EQ(Prepared.FindByStableId("new"), 0);
}

TEST(QmCardOrderModelSwap, CommitOfPreparedModelsNeedsNoNewAllocation)
{
	const std::string OldTab(96, 'o');
	const std::string NewTab(192, 'n');
	qm_card_order::CModel Published;
	qm_card_order::CModel Prepared;
	Published.SetEntries({{"old", OldTab.c_str(), 1, 0}});
	Prepared.SetEntries({{"new", NewTab.c_str(), 2, 0}});
	bool Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Published.SwapContent(Prepared);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	ASSERT_EQ(Published.Count(), 1);
	ASSERT_EQ(Prepared.Count(), 1);
	EXPECT_STREQ(Published.Entry(0).m_pDefaultTab, NewTab.c_str());
	EXPECT_STREQ(Prepared.Entry(0).m_pDefaultTab, OldTab.c_str());
	EXPECT_EQ(Published.FindByStableId("new"), 0);
	EXPECT_EQ(Prepared.FindByStableId("old"), 0);
}

TEST(QmCardOrderModelSwap, OwnedShortAndLongTabsSurviveRetirementOfThePreparedModel)
{
	qm_card_order::CModel Published;
	Published.SetEntries({{"old", "old-tab", 1, 0}});
	const char *pShortTab = nullptr;
	const char *pLongTab = nullptr;
	{
		qm_card_order::CModel Prepared;
		std::string ShortTab = "hud";
		std::string LongTab(128, 'l');
		Prepared.SetEntries({{"short", ShortTab.c_str(), 1, 0}, {"long", LongTab.c_str(), 2, 0}});
		pShortTab = Prepared.Entry(0).m_pDefaultTab;
		pLongTab = Prepared.Entry(1).m_pDefaultTab;
		ShortTab = "changed";
		LongTab.assign(160, 'x');
		Published.SwapContent(Prepared);
		EXPECT_EQ(Published.Entry(0).m_pDefaultTab, pShortTab);
		EXPECT_EQ(Published.Entry(1).m_pDefaultTab, pLongTab);
		EXPECT_EQ(Prepared.FindByStableId("old"), 0);
	}
	ASSERT_EQ(Published.Count(), 2);
	EXPECT_EQ(Published.Entry(0).m_pDefaultTab, pShortTab);
	EXPECT_EQ(Published.Entry(1).m_pDefaultTab, pLongTab);
	EXPECT_STREQ(Published.Entry(0).m_pDefaultTab, "hud");
	EXPECT_EQ(std::string(Published.Entry(1).m_pDefaultTab), std::string(128, 'l'));
	EXPECT_EQ(Published.FindByStableId("short"), 0);
	EXPECT_EQ(Published.FindByStableId("long"), 1);
}

TEST(QmCardOrderModelSwap, EmptyPreparedContentRetiresAllEntriesAndBothModelsStayUsable)
{
	qm_card_order::CModel Published;
	qm_card_order::CModel Prepared;
	Published.SetEntries({{"old", "old-tab", 1, 0}});
	Published.SwapContent(Prepared);
	EXPECT_EQ(Published.Count(), 0);
	EXPECT_EQ(Published.FindByStableId("old"), -1);
	ASSERT_EQ(Prepared.Count(), 1);
	EXPECT_EQ(Prepared.FindByStableId("old"), 0);
	EXPECT_STREQ(Prepared.Entry(0).m_pDefaultTab, "old-tab");
	Published.SetEntries({{"replacement", "new-tab", 2, 0}});
	Prepared.MoveToTab("old", "retired-tab", 2, 0);
	EXPECT_EQ(Published.FindByStableId("replacement"), 0);
	EXPECT_EQ(Published.FindByStableId("old"), -1);
	EXPECT_STREQ(Prepared.Entry(0).m_pDefaultTab, "retired-tab");
}

TEST(QmCardOrderModelSwap, SelfSwapKeepsPointersVersionsAndDirtyStateWithoutAllocation)
{
	qm_card_order::CModel Model;
	Model.SetEntries({{"card", "stable-tab", 1, 0}});
	Model.ClearDirty();
	const auto *pEntry = &Model.Entry(0);
	const auto *pTab = Model.Entry(0).m_pDefaultTab;
	const auto LayoutRevision = Model.LayoutRevision();
	const auto StateIndexRevision = Model.StateIndexRevision();
	bool Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Model.SwapContent(Model);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_EQ(&Model.Entry(0), pEntry);
	EXPECT_EQ(Model.Entry(0).m_pDefaultTab, pTab);
	EXPECT_EQ(Model.LayoutRevision(), LayoutRevision);
	EXPECT_EQ(Model.StateIndexRevision(), StateIndexRevision);
	EXPECT_EQ(Model.FindByStableId("card"), 0);
	EXPECT_FALSE(Model.IsDirty());
}
