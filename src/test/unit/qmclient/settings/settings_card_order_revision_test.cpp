#include <game/client/QmUi/QmCardOrderModel.h>

#include <gtest/gtest.h>

TEST(SettingsCardOrder, StateIndexRevisionChangesWhenSameSizedModelIsRebuilt)
{
	qm_card_order::CModel Model;
	Model.SetEntries({
		{"card-a", "settings", 1, 0},
		{"card-b", "settings", 2, 0},
	});
	const uint64_t InitialRevision = Model.StateIndexRevision();
	EXPECT_EQ(Model.StateIndexForStableId("card-a"), 0);
	EXPECT_EQ(Model.StateIndexForStableId("card-b"), 1);

	Model.SetEntries({
		{"card-b", "settings", 2, 0},
		{"card-a", "settings", 1, 0},
	});

	EXPECT_GT(Model.StateIndexRevision(), InitialRevision);
	EXPECT_EQ(Model.Count(), 2);
	EXPECT_EQ(Model.StateIndexForStableId("card-b"), 0);
	EXPECT_EQ(Model.StateIndexForStableId("card-a"), 1);
}
