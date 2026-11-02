// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/qmclient/self_kill_protection.h>

#include <gtest/gtest.h>
#include <test/test.h>

// 默认不拦截：没配置自动开启、玩家也没按键时，K 键必须照常发出去。
// 这条是"自杀保护莫名其妙自己开了"的回归线：默认状态只能是关闭。
TEST(QmSelfKillProtection, DefaultsToOffAndNeverAutoEnablesWithoutADelay)
{
	CQmSelfKillProtection Protection;
	Protection.Configure(false, 0);
	Protection.BeginRound();
	EXPECT_FALSE(Protection.BlocksKill());

	// 不给延迟，跑多久都不该自己切到拦截。
	for(int Step = 0; Step < 600; ++Step)
		EXPECT_FALSE(Protection.Update(1.0f));
	EXPECT_FALSE(Protection.BlocksKill());
	EXPECT_FLOAT_EQ(Protection.SecondsUntilAutoEnable(), 0.0f);
}

// 手动按键是唯一直接开启途径，而且不会被后续帧或自动计时改回来。
TEST(QmSelfKillProtection, ManualToggleSurvivesTheAutoEnableTimer)
{
	CQmSelfKillProtection Protection;
	Protection.Configure(false, 0);
	Protection.BeginRound();

	Protection.SetEnabled(true);
	EXPECT_TRUE(Protection.BlocksKill());

	// 玩家自己关掉之后，本局不该再被任何帧重新打开。
	Protection.SetEnabled(false);
	for(int Step = 0; Step < 600; ++Step)
		EXPECT_FALSE(Protection.Update(1.0f));
	EXPECT_FALSE(Protection.BlocksKill());
}

// 玩家显式配置了延迟之后，只有到点那一次才开启，并且只回显一次。
TEST(QmSelfKillProtection, AutoEnableFiresExactlyOnceAfterTheConfiguredDelay)
{
	CQmSelfKillProtection Protection;
	Protection.Configure(false, 120);
	Protection.BeginRound();
	EXPECT_FALSE(Protection.BlocksKill());

	// 到点前一直保持关闭：前 119 秒都不该触发，剩余秒数逐秒递减。
	for(int Second = 0; Second < 119; ++Second)
	{
		EXPECT_FLOAT_EQ(Protection.SecondsUntilAutoEnable(), 120.0f - static_cast<float>(Second));
		EXPECT_FALSE(Protection.Update(1.0f));
		EXPECT_FALSE(Protection.BlocksKill());
	}
	EXPECT_FLOAT_EQ(Protection.SecondsUntilAutoEnable(), 1.0f);
	// 满 120 秒那一刻触发。
	EXPECT_TRUE(Protection.Update(1.0f));
	EXPECT_TRUE(Protection.BlocksKill());
	EXPECT_FLOAT_EQ(Protection.SecondsUntilAutoEnable(), 0.0f);

	// 只触发一次：之后玩家手动关掉也不会被同一次延迟再打开。
	Protection.SetEnabled(false);
	for(int Step = 0; Step < 60; ++Step)
		EXPECT_FALSE(Protection.Update(1.0f));
	EXPECT_FALSE(Protection.BlocksKill());
}

// 换图 / 重连要重新计时，且开局状态回到配置默认值，
// 上一局自动开启的拦截不能漏到下一局的开局瞬间。
TEST(QmSelfKillProtection, BeginRoundRestartsTheTimerAndResetsTheState)
{
	CQmSelfKillProtection Protection;
	Protection.Configure(false, 60);
	Protection.BeginRound();
	for(int Second = 0; Second < 60; ++Second)
		Protection.Update(1.0f);
	EXPECT_TRUE(Protection.BlocksKill());

	Protection.BeginRound();
	EXPECT_FALSE(Protection.BlocksKill());
	EXPECT_FLOAT_EQ(Protection.SecondsUntilAutoEnable(), 60.0f);

	// 离开对局同样回到默认值：菜单里不会残留拦截状态。
	Protection.EndRound();
	EXPECT_FALSE(Protection.BlocksKill());
	EXPECT_FLOAT_EQ(Protection.SecondsUntilAutoEnable(), 0.0f);
	// 不在对局中不计时，回来之前不会自己走到开启。
	for(int Step = 0; Step < 600; ++Step)
		EXPECT_FALSE(Protection.Update(1.0f));
	EXPECT_FALSE(Protection.BlocksKill());
}

// 设置卡/控制台改配置要立即生效：改了默认值就当一次手动开关。
TEST(QmSelfKillProtection, ConfigChangeAppliesImmediatelyEvenMidRound)
{
	CQmSelfKillProtection Protection;
	Protection.Configure(false, 0);
	Protection.BeginRound();
	EXPECT_FALSE(Protection.BlocksKill());

	Protection.Configure(true, 0);
	EXPECT_TRUE(Protection.BlocksKill());

	Protection.Configure(false, 0);
	EXPECT_FALSE(Protection.BlocksKill());

	// 只是改延迟不该改动当前拦截状态。
	Protection.Configure(false, 30);
	EXPECT_FALSE(Protection.BlocksKill());
	EXPECT_FLOAT_EQ(Protection.SecondsUntilAutoEnable(), 30.0f);
}

// 已经开着的时候到点，不重复回显，但不影响它保持开启。
TEST(QmSelfKillProtection, AutoEnableKeepsAnAlreadyEnabledStateWithoutAnnouncingAgain)
{
	CQmSelfKillProtection Protection;
	Protection.Configure(false, 30);
	Protection.BeginRound();
	Protection.SetEnabled(true);
	for(int Second = 0; Second < 30; ++Second)
		EXPECT_FALSE(Protection.Update(1.0f));
	EXPECT_FALSE(Protection.Update(1.0f));
	EXPECT_TRUE(Protection.BlocksKill());
	// 触发之后计时不再冒出剩余秒数。
	EXPECT_FLOAT_EQ(Protection.SecondsUntilAutoEnable(), 0.0f);
}
