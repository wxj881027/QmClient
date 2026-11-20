#include <game/client/components/menus.h>

#include <gtest/gtest.h>

TEST(QmMenuSettingsCardLayoutMigration, FailedStepPreservesSavedOrderAndVersionForRetry)
{
	qm_card_order::CModel Model;
	Model.SetEntries({{"deck:general-game", "general", 2, 0}});
	ASSERT_TRUE(Model.IsDirty());
	int Version = 5;
	char aStoredOrder[] = "previous";

	EXPECT_FALSE(qmclient::settings_card_order::TryMigrateTClientMainCardsLayout(Model, aStoredOrder, Version));
	EXPECT_STREQ(aStoredOrder, "previous");
	EXPECT_EQ(Version, 5);
	EXPECT_TRUE(Model.IsDirty());

	char aRetryOrder[128] = "previous";
	EXPECT_TRUE(qmclient::settings_card_order::TryMigrateTClientMainCardsLayout(Model, aRetryOrder, Version));
	EXPECT_STREQ(aRetryOrder, "deck:general-game|general|right|0;");
	EXPECT_EQ(Version, 6);
	EXPECT_FALSE(Model.IsDirty());
}
