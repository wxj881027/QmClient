#include <game/client/QmUi/SettingsPageLayout.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <unordered_set>

TEST(SettingsCardViewKey, ChangesWhenAnySettingsSubTabChanges)
{
	const uint64_t Base = ResolveSettingsCardDisplayViewKey(0, 0, 0, 0, 0);
	EXPECT_NE(Base, ResolveSettingsCardDisplayViewKey(1, 0, 0, 0, 0));
	EXPECT_NE(Base, ResolveSettingsCardDisplayViewKey(0, 1, 0, 0, 0));
	EXPECT_NE(Base, ResolveSettingsCardDisplayViewKey(0, 0, 1, 0, 0));
	EXPECT_NE(Base, ResolveSettingsCardDisplayViewKey(0, 0, 0, 1, 0));
	EXPECT_NE(Base, ResolveSettingsCardDisplayViewKey(0, 0, 0, 0, 1));
	EXPECT_EQ(Base, ResolveSettingsCardDisplayViewKey(0, 0, 0, 0, 0));
}

TEST(SettingsCardViewKey, BoundaryIdentifiersRemainDistinctForEachViewField)
{
	const std::array<int, 5> Values{std::numeric_limits<int>::min(), -1, 0, 1, std::numeric_limits<int>::max()};
	for(size_t Field = 0; Field < 6; ++Field)
	{
		SCOPED_TRACE(Field);
		std::unordered_set<uint64_t> Keys;
		for(int Value : Values)
		{
			SCOPED_TRACE(Value);
			std::array<int, 6> View{};
			View[Field] = Value;
			EXPECT_TRUE(Keys.insert(ResolveSettingsCardDisplayViewKey(View[0], View[1], View[2], View[3], View[4], View[5])).second);
		}
	}
}

TEST(SettingsCardDefinitionsRevision, OrdinaryWidthsRetainHundredthBuckets)
{
	const uint64_t Base = ResolveSettingsCardDefinitionsRevision(4, 7, 800.0f, 3);
	EXPECT_EQ(ResolveSettingsCardDefinitionsRevision(4, 7, 800.001f, 3), Base);
	EXPECT_NE(ResolveSettingsCardDefinitionsRevision(4, 7, 800.01f, 3), Base);
}

TEST(SettingsCardDefinitionsRevision, InvalidAndOverflowingWidthsHaveStableKeys)
{
	const uint64_t Empty = ResolveSettingsCardDefinitionsRevision(4, 7, 0.0f, 3);
	EXPECT_EQ(ResolveSettingsCardDefinitionsRevision(4, 7, -1.0f, 3), Empty);
	EXPECT_EQ(ResolveSettingsCardDefinitionsRevision(4, 7, std::numeric_limits<float>::quiet_NaN(), 3), Empty);
	EXPECT_EQ(ResolveSettingsCardDefinitionsRevision(4, 7, std::numeric_limits<float>::infinity(), 3), Empty);
	EXPECT_EQ(ResolveSettingsCardDefinitionsRevision(4, 7, std::numeric_limits<float>::max(), 3), ResolveSettingsCardDefinitionsRevision(4, 7, 1.0e10f, 3));
	EXPECT_NE(ResolveSettingsCardDefinitionsRevision(4, 7, 1.0e10f, 3), Empty);
}
