#include <game/client/QmUi/AntiPingPlayersSettings.h>

#include <gtest/gtest.h>

namespace
{
	using namespace QmAntiPingPlayersSettings;
}

// 3 在设置页与 2 同属 Interact 档；若把 3 当成独立档位，三档单选会没有激活项。
TEST(QmAntiPingPlayersSettings, FrozenValueDisplaysAsInteract)
{
	EXPECT_EQ(BaseChoice(INTERACT_FROZEN), INTERACT);
	EXPECT_TRUE(ShowsFrozenOption(INTERACT_FROZEN));
	EXPECT_TRUE(FrozenOptionEnabled(INTERACT_FROZEN));
	EXPECT_FALSE(FrozenOptionEnabled(INTERACT));
}

// 从 Interact+frozen 切到 All 必须落到 1，不能把折叠后的 3 原样写回。
TEST(QmAntiPingPlayersSettings, SwitchingFromFrozenToAllDropsFrozenFlag)
{
	EXPECT_EQ(ApplyBaseChoice(INTERACT_FROZEN, ALL), ALL);
	EXPECT_FALSE(ShowsFrozenOption(ALL));
}

// 从 All 切回 Interact 默认关闭冻结选项；已处于 3 时保留勾选状态。
TEST(QmAntiPingPlayersSettings, SwitchingBackToInteractRestoresFrozenChoice)
{
	EXPECT_EQ(ApplyBaseChoice(ALL, INTERACT), INTERACT);
	EXPECT_EQ(ApplyBaseChoice(INTERACT_FROZEN, INTERACT), INTERACT_FROZEN);
}

// 冻结开关只在 2/3 之间切换，不产生 4+，也不改动 Off / All 档。
TEST(QmAntiPingPlayersSettings, FrozenToggleStaysWithinInteractValues)
{
	EXPECT_EQ(ApplyFrozenOption(INTERACT, true), INTERACT_FROZEN);
	EXPECT_EQ(ApplyFrozenOption(INTERACT_FROZEN, false), INTERACT);
	EXPECT_EQ(ApplyFrozenOption(ALL, true), ALL);
	EXPECT_EQ(ApplyFrozenOption(OFF, true), OFF);
}

TEST(QmAntiPingPlayersSettings, InvalidValuesClampToValidChoices)
{
	EXPECT_EQ(BaseChoice(-1), OFF);
	EXPECT_EQ(BaseChoice(99), INTERACT);
	EXPECT_EQ(ApplyBaseChoice(-1, 99), OFF);
	EXPECT_EQ(ApplyFrozenOption(99, true), INTERACT_FROZEN);
}
