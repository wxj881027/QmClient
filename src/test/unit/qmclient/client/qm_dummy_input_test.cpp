#include <game/client/components/qmclient/dummy_input.h>
#include <game/client/components/qmclient/input_state.h>

#include <gtest/gtest.h>

#include <vector>

// 分身输入闭环：按键事件 -> 开火计数器、切分身 -> 输入交接、
// 自动上锤 -> 每 tick 的发送决定。这里按一次真实按键序列和一段
// 连续 SnapInput 回放生产判定，断言服务器最终看到的开火边沿。

namespace
{
	// 一次 SnapInput 实际发给服务器的内容；NONE 表示本 tick 不发分身输入。
	struct SDummyHammerSnap
	{
		int m_Index = 0;
		EDummyHammerSnapAction m_Action = EDummyHammerSnapAction::NONE;
		int m_FireSent = -1;
	};

	std::vector<SDummyHammerSnap> SimulateDummyHammerSnaps(int SnapCount)
	{
		std::vector<SDummyHammerSnap> vSnaps;
		unsigned int DummyFire = 0;
		int DummyHammerResends = 0;
		int HammerFire = 0;
		for(int i = 0; i < SnapCount; i++)
		{
			const SDummyHammerSnapDecision Decision = QmResolveDummyHammerSnap(DummyFire, DummyHammerResends);
			DummyFire = Decision.m_DummyFire;
			DummyHammerResends = Decision.m_DummyHammerResends;

			SDummyHammerSnap Snap;
			Snap.m_Index = i;
			Snap.m_Action = Decision.m_Action;
			if(Decision.m_Action == EDummyHammerSnapAction::REFRESH)
			{
				HammerFire = QmPressedFireState(HammerFire);
				Snap.m_FireSent = HammerFire;
			}
			else if(Decision.m_Action == EDummyHammerSnapAction::RESEND)
			{
				Snap.m_FireSent = HammerFire;
			}
			vSnaps.push_back(Snap);
		}
		return vSnaps;
	}
}

TEST(QmInputCounter, PressAndReleaseEventsToggleTheCounterOnce)
{
	// 一次按下、一次松开：计数器 0 -> 1 -> 2，服务器看到一次按下边沿。
	int Fire = 0;
	Fire = QmInputCounterAfterEvent(Fire, 1);
	EXPECT_EQ(Fire, 1);
	Fire = QmInputCounterAfterEvent(Fire, 0);
	EXPECT_EQ(Fire, 2);
}

TEST(QmInputCounter, HeldKeyRepeatEventsDoNotToggleAgain)
{
	// 按住键产生的重复按下事件不能再次加一，否则服务器会把一次按住
	// 识别成连续点击。
	int Fire = 1;
	EXPECT_EQ(QmInputCounterAfterEvent(Fire, 1), 1);
	EXPECT_EQ(QmInputCounterAfterEvent(1, 1), 1);
	EXPECT_EQ(QmInputCounterAfterEvent(2, 0), 2);
}

TEST(QmInputCounter, RepeatingTheSameBindProducesOnePulsePerRelease)
{
	// 「按 e -> 按 r -> 再按一次 r」这类连续两次绑定触发：
	// 两次触发之间必须有一次松开，计数器才会产生第二个按下边沿。
	int Fire = 0;
	Fire = QmInputCounterAfterEvent(Fire, 1); // 第一次按下
	EXPECT_EQ(Fire, 1);
	Fire = QmInputCounterAfterEvent(Fire, 0); // 松开
	Fire = QmInputCounterAfterEvent(Fire, 1); // 第二次按下
	EXPECT_EQ(Fire, 3);
	EXPECT_EQ(Fire & 1, 1);

	// 没有松开就再次按下，服务器仍认为按键一直按住。
	EXPECT_EQ(QmInputCounterAfterEvent(3, 1), 3);
}

TEST(QmInputCounter, CounterWrapsInsideTheInputStateMask)
{
	EXPECT_EQ(QmInputCounterAfterEvent(63, 0), 0);
	EXPECT_EQ(QmInputCounterAfterEvent(62, 1), 63);
	EXPECT_EQ(QmInputCounterAfterEvent(63, 1), 63);
	for(int State = 0; State <= INPUT_STATE_MASK; State++)
	{
		const int Next = QmInputCounterAfterEvent(State, 1);
		EXPECT_EQ(Next & 1, 1) << "state " << State;
		EXPECT_GE(Next, 0);
		EXPECT_LE(Next, INPUT_STATE_MASK);
	}
}

TEST(QmInputCounter, ConsoleArgumentComparisonKeepsOriginalSemantics)
{
	// 控制台参数不是 0/1 时沿用原实现的直接比较：与最低位不同就递增。
	EXPECT_EQ(QmInputCounterAfterEvent(2, 2), 3);
	EXPECT_EQ(QmInputCounterAfterEvent(3, 2), 4);
}

TEST(QmFireState, PressedStateIsAlwaysOddAndChanges)
{
	for(int Fire = 0; Fire <= INPUT_STATE_MASK; Fire++)
	{
		const int Pressed = QmPressedFireState(Fire);
		EXPECT_EQ(Pressed & 1, 1) << "fire " << Fire;
		EXPECT_NE(Pressed, Fire) << "fire " << Fire;
	}
}

TEST(QmFireState, ReleasedStateIsEvenAndOnlyChangesWhileHeld)
{
	for(int Fire = 0; Fire <= INPUT_STATE_MASK; Fire++)
	{
		const int Released = QmReleasedFireState(Fire);
		EXPECT_EQ(Released & 1, 0) << "fire " << Fire;
		if((Fire & 1) == 0)
			EXPECT_EQ(Released, Fire) << "fire " << Fire;
		else
			EXPECT_EQ(Released, Fire + 1) << "fire " << Fire;
	}
}

TEST(QmDummySwap, ResetSlotFollowsTheConfiguredMode)
{
	EXPECT_EQ(QmDummyResetSlot(0, 0), -1);
	EXPECT_EQ(QmDummyResetSlot(0, 1), -1);
	// 模式 1 重置即将变为分身的槽位，模式 2 重置即将受控的槽位。
	EXPECT_EQ(QmDummyResetSlot(1, 0), 1);
	EXPECT_EQ(QmDummyResetSlot(1, 1), 0);
	EXPECT_EQ(QmDummyResetSlot(2, 0), 0);
	EXPECT_EQ(QmDummyResetSlot(2, 1), 1);
}

TEST(QmDummySwap, HeldFireMovesToTheNewlyControlledTee)
{
	// 旧分身正按住开火（计数器 5 为奇数）。
	CNetObj_PlayerInput DummyInput = {};
	DummyInput.m_Fire = 5;
	CNetObj_PlayerInput InactiveInput = {};
	InactiveInput.m_Direction = 1;
	InactiveInput.m_Jump = 1;
	InactiveInput.m_TargetX = 30;
	CNetObj_PlayerInput ActiveInput = {};
	ActiveInput.m_Fire = 0;

	QmApplyDummySwap(InactiveInput, DummyInput, ActiveInput);

	// 新受控连接沿用按下状态，切换瞬间不会松手；后续松开只产生一个边沿。
	EXPECT_EQ(ActiveInput.m_Fire, 5);
	EXPECT_EQ(QmInputCounterAfterEvent(ActiveInput.m_Fire, 0), 6);
	EXPECT_EQ(QmInputCounterAfterEvent(6, 0), 6);
	// 新分身沿用刚被切出去那条连接的历史输入。
	EXPECT_EQ(DummyInput.m_Direction, 1);
	EXPECT_EQ(DummyInput.m_Jump, 1);
	EXPECT_EQ(DummyInput.m_TargetX, 30);
}

TEST(QmDummySwap, ReleasedFireDoesNotTriggerOnSwitch)
{
	// 旧分身没有按住开火：切过去不能凭空产生一次按下。
	CNetObj_PlayerInput DummyInput = {};
	DummyInput.m_Fire = 4;
	CNetObj_PlayerInput InactiveInput = {};
	CNetObj_PlayerInput ActiveInput = {};
	ActiveInput.m_Fire = 3;

	QmApplyDummySwap(InactiveInput, DummyInput, ActiveInput);

	EXPECT_EQ(ActiveInput.m_Fire, 4);
	EXPECT_EQ(ActiveInput.m_Fire & 1, 0);
}

TEST(QmDummyHammer, RefreshAndResendCadenceCoversThirtySnaps)
{
	const std::vector<SDummyHammerSnap> vSnaps = SimulateDummyHammerSnaps(30);
	ASSERT_EQ(vSnaps.size(), 30u);

	for(const SDummyHammerSnap &Snap : vSnaps)
	{
		const bool Refresh = Snap.m_Index == 0 || Snap.m_Index == QM_DUMMY_HAMMER_INTERVAL;
		const bool Resend = Snap.m_Index == 1 || Snap.m_Index == 2 ||
				    Snap.m_Index == QM_DUMMY_HAMMER_INTERVAL + 1 ||
				    Snap.m_Index == QM_DUMMY_HAMMER_INTERVAL + 2;
		if(Refresh)
			EXPECT_EQ(Snap.m_Action, EDummyHammerSnapAction::REFRESH) << "snap " << Snap.m_Index;
		else if(Resend)
			EXPECT_EQ(Snap.m_Action, EDummyHammerSnapAction::RESEND) << "snap " << Snap.m_Index;
		else
			EXPECT_EQ(Snap.m_Action, EDummyHammerSnapAction::NONE) << "snap " << Snap.m_Index;
	}
}

TEST(QmDummyHammer, EveryRefreshSendsANewHeldFireState)
{
	const std::vector<SDummyHammerSnap> vSnaps = SimulateDummyHammerSnaps(26);
	const SDummyHammerSnap &First = vSnaps[0];
	const SDummyHammerSnap &Second = vSnaps[QM_DUMMY_HAMMER_INTERVAL];
	ASSERT_EQ(First.m_Action, EDummyHammerSnapAction::REFRESH);
	ASSERT_EQ(Second.m_Action, EDummyHammerSnapAction::REFRESH);
	EXPECT_EQ(First.m_FireSent & 1, 1);
	EXPECT_EQ(Second.m_FireSent & 1, 1);
	EXPECT_NE(First.m_FireSent, Second.m_FireSent);
}

TEST(QmDummyHammer, ResendsRepeatTheLastRefreshWithoutToggling)
{
	const std::vector<SDummyHammerSnap> vSnaps = SimulateDummyHammerSnaps(26);
	ASSERT_GE(vSnaps.size(), 3u);
	const int RefreshFire = vSnaps[0].m_FireSent;
	ASSERT_EQ(vSnaps[1].m_Action, EDummyHammerSnapAction::RESEND);
	ASSERT_EQ(vSnaps[2].m_Action, EDummyHammerSnapAction::RESEND);
	EXPECT_EQ(vSnaps[1].m_FireSent, RefreshFire);
	EXPECT_EQ(vSnaps[2].m_FireSent, RefreshFire);
	for(int i = 3; i < QM_DUMMY_HAMMER_INTERVAL; i++)
		EXPECT_EQ(vSnaps[i].m_FireSent, -1) << "snap " << i;
}

TEST(QmDummyHammer, DisablingHammerReleasesTheLastFireState)
{
	// 关闭自动上锤后的第一包把分身开火改写为松开，避免退出 HDF 时多挥一锤。
	EXPECT_EQ(QmReleasedFireState(5), 6);
	EXPECT_EQ(QmReleasedFireState(6), 6);
	EXPECT_EQ(QmReleasedFireState(0), 0);
}
