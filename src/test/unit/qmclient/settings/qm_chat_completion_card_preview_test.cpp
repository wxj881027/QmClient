#include <game/client/QmUi/cards/QmCardCatalogInternal.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

TEST(QmChatCompletionCardPreview, AllocationFailureLeavesAnEmptyPreviewAndAllowsRetry)
{
	CQmChatCommandHud Preview;
	bool AllocationFailed;
	{
		CPredictionAllocationFailure Failure(0);
		Preview = qm_card_catalog::PrepareChatCompletionPreview("Weapon", "Info");
		AllocationFailed = Failure.Triggered();
	}

	EXPECT_TRUE(AllocationFailed);
	EXPECT_EQ(Preview.Count(), 0);
	Preview.SetLayout(0.0f, 200.0f, 200.0f, 1000.0f, 10.0f, 0.0f, 400.0f);
	EXPECT_EQ(Preview.Layout().m_VisibleRows, 0);

	Preview = qm_card_catalog::PrepareChatCompletionPreview("Weapon", "Info");
	ASSERT_EQ(Preview.Count(), 2);
	EXPECT_EQ(Preview.Selected(), 0);
	EXPECT_EQ(Preview.Candidate(0).m_Name, "/addweapon");
	EXPECT_EQ(Preview.Candidate(0).m_Detail, "Weapon");
	EXPECT_EQ(Preview.Candidate(1).m_Name, "/info");
	Preview.SetLayout(0.0f, 200.0f, 200.0f, 1000.0f, 10.0f, 0.0f, 400.0f);
	EXPECT_EQ(Preview.Layout().m_VisibleRows, 2);
}
