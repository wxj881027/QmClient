#include <game/client/components/menus.h>

#include <gtest/gtest.h>

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
