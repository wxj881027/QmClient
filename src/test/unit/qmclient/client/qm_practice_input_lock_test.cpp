#include <game/client/components/qmclient/practice_input_lock.h>

#include <gtest/gtest.h>

namespace
{
	CNetObj_PlayerInput MakeInput()
	{
		CNetObj_PlayerInput Input = {};
		Input.m_Direction = -1;
		Input.m_Jump = 1;
		Input.m_Hook = 1;
		Input.m_Fire = 1;
		Input.m_WantedWeapon = 3;
		Input.m_TargetX = 10;
		Input.m_TargetY = 20;
		return Input;
	}
}

TEST(QmPracticeInputLock, CapturedServerInputKeepsAimAndHookButReleasesMovement)
{
	CPracticeInputLock Lock;
	ASSERT_TRUE(Lock.Capture(0, MakeInput()));

	CNetObj_PlayerInput Sent = MakeInput();
	Lock.PrepareForSend(Sent, 0, true);
	EXPECT_EQ(Sent.m_TargetX, 10);
	EXPECT_EQ(Sent.m_TargetY, 20);
	EXPECT_EQ(Sent.m_Hook, 1);
	EXPECT_EQ(Sent.m_Direction, 0);
	EXPECT_EQ(Sent.m_Jump, 0);
	EXPECT_EQ(Sent.m_Fire, 2); // 按下位(bit0)被改写为释放
	EXPECT_EQ(Sent.m_WantedWeapon, 0);
}

TEST(QmPracticeInputLock, UnlockedSlotSendsNeutralInput)
{
	CPracticeInputLock Lock;
	CNetObj_PlayerInput Sent = MakeInput();
	Sent.m_TargetX = 0;
	Sent.m_TargetY = 0;
	Lock.PrepareForSend(Sent, 1, true);
	EXPECT_EQ(Sent.m_Direction, 0);
	EXPECT_EQ(Sent.m_Jump, 0);
	EXPECT_EQ(Sent.m_Hook, 0);
	EXPECT_EQ(Sent.m_Fire, 2);
	EXPECT_EQ(Sent.m_TargetX, 1);
	EXPECT_EQ(Sent.m_TargetY, 0);
}

TEST(QmPracticeInputLock, PostPracticeSuppressionCountsDownPerSlot)
{
	CPracticeInputLock Lock;
	Lock.SetReleasedFireState(0, 1);
	Lock.SetReleasedFireState(1, 1);
	Lock.BeginPostPracticeSuppression(2);

	CNetObj_PlayerInput Sent = MakeInput();
	Lock.PrepareForSend(Sent, 0, false);
	EXPECT_EQ(Sent.m_Fire, 2);
	EXPECT_EQ(Sent.m_WantedWeapon, 0);

	Sent = MakeInput();
	Lock.PrepareForSend(Sent, 0, false);
	EXPECT_EQ(Sent.m_Fire, 2);

	// 槽位 0 的压制窗口已经用完，输入必须原样放行。
	Sent = MakeInput();
	Lock.PrepareForSend(Sent, 0, false);
	EXPECT_EQ(Sent.m_Fire, 1);
	EXPECT_EQ(Sent.m_WantedWeapon, 3);

	// 另一个槽位仍在自己的窗口内。
	Sent = MakeInput();
	Lock.PrepareForSend(Sent, 1, false);
	EXPECT_EQ(Sent.m_Fire, 2);
}

TEST(QmPracticeInputLock, ResetClearsLocksAndCountdownButKeepsReleasedFireState)
{
	CPracticeInputLock Lock;
	ASSERT_TRUE(Lock.Capture(0, MakeInput()));
	Lock.SetReleasedFireState(1, 3);
	Lock.Reset();

	EXPECT_FALSE(Lock.HasLockedInput(0));
	CNetObj_PlayerInput Sent = MakeInput();
	Lock.PrepareForSend(Sent, 0, true);
	// 锁已清空，走中性输入：移动/钩子被清掉，瞄准方向保留。
	EXPECT_EQ(Sent.m_Direction, 0);
	EXPECT_EQ(Sent.m_Hook, 0);
	EXPECT_EQ(Sent.m_TargetX, 10);

	// 释放后的开火位图在 Reset 后仍可用于退出压制（Disable 的调用顺序依赖它）。
	Lock.BeginPostPracticeSuppression(1);
	Sent = MakeInput();
	Lock.PrepareForSend(Sent, 1, false);
	EXPECT_EQ(Sent.m_Fire, 4);
}

TEST(QmPracticeInputLock, InvalidSlotIsIgnored)
{
	CPracticeInputLock Lock;
	EXPECT_FALSE(Lock.Capture(NUM_DUMMIES, MakeInput()));
	CNetObj_PlayerInput Sent = MakeInput();
	Lock.PrepareForSend(Sent, -1, true);
	EXPECT_EQ(Sent.m_Direction, -1);
}
