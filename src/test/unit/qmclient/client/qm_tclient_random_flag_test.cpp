#include <game/client/components/tclient/tclient.h>

#include <gtest/gtest.h>

TEST(QmTClientRandomFlag, EmptyFlagListKeepsCurrentSelection)
{
	int Country = 156;
	QmApplyRandomFlag(Country, 0, [](size_t) {
		ADD_FAILURE() << "Tried to sample an empty flag list";
		return -1;
	});
	EXPECT_EQ(Country, 156);
}

TEST(QmTClientRandomFlag, InitializedFlagListAppliesSelectedCountry)
{
	int Country = 156;
	QmApplyRandomFlag(Country, 1, [](size_t Count) {
		EXPECT_EQ(Count, 1u);
		return -1;
	});
	EXPECT_EQ(Country, -1);
}
