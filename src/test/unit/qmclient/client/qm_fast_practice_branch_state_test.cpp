#include <game/client/components/qmclient/practice_branch_state.h>
#include <game/client/components/qmclient/predicted_event_queue.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
	// 与 IClient::CONN_MAIN / CONN_DUMMY 的顺序一致；测试只关心连接槽位语义。
	constexpr int CONN_MAIN = 0;
	constexpr int CONN_DUMMY = 1;

	CNetObj_PlayerInput ServerInput(int Direction, int Jump, int Fire, int TargetX, int TargetY, int Hook)
	{
		CNetObj_PlayerInput Input = {};
		Input.m_Direction = Direction;
		Input.m_Jump = Jump;
		Input.m_Fire = Fire;
		Input.m_TargetX = TargetX;
		Input.m_TargetY = TargetY;
		Input.m_Hook = Hook;
		Input.m_WantedWeapon = 2;
		Input.m_NextWeapon = 1;
		Input.m_PrevWeapon = -1;
		return Input;
	}

	CGameWorld::CPredictedEvent Event(int Id, int Tick)
	{
		return CGameWorld::CPredictedEvent(NETEVENTTYPE_SOUNDWORLD, vec2(0.0f, 0.0f), Id, Tick, 0);
	}
}

TEST(QmFastPracticeServerInput, LockReleasesMovementJumpAndWeaponRequests)
{
	const CNetObj_PlayerInput Locked = QmBuildPracticeServerLockedInput(ServerInput(1, 1, 0, 30, -10, 0));
	EXPECT_EQ(Locked.m_Direction, 0);
	EXPECT_EQ(Locked.m_Jump, 0);
	EXPECT_EQ(Locked.m_WantedWeapon, 0);
	EXPECT_EQ(Locked.m_NextWeapon, 0);
	EXPECT_EQ(Locked.m_PrevWeapon, 0);
}

TEST(QmFastPracticeServerInput, LockKeepsAimAndHookSoTheRealTeeStaysAttached)
{
	// 挂墙的本体如果被松钩会直接掉下去：锁定输入必须保留瞄准与钩子。
	const CNetObj_PlayerInput Locked = QmBuildPracticeServerLockedInput(ServerInput(0, 0, 0, 30, -10, 1));
	EXPECT_EQ(Locked.m_TargetX, 30);
	EXPECT_EQ(Locked.m_TargetY, -10);
	EXPECT_EQ(Locked.m_Hook, 1);
}

TEST(QmFastPracticeServerInput, LockReleasesHeldFireWithoutChangingOtherStateBits)
{
	const CNetObj_PlayerInput Pressed = QmBuildPracticeServerLockedInput(ServerInput(0, 0, 1, 10, 0, 0));
	EXPECT_EQ(Pressed.m_Fire & 1, 0);

	const CNetObj_PlayerInput Held = QmBuildPracticeServerLockedInput(ServerInput(0, 0, 2, 10, 0, 0));
	EXPECT_EQ(Held.m_Fire, 2);
}

TEST(QmFastPracticeServerInput, LockGivesZeroAimAValidDirection)
{
	const CNetObj_PlayerInput Locked = QmBuildPracticeServerLockedInput(ServerInput(0, 0, 0, 0, 0, 0));
	EXPECT_EQ(Locked.m_TargetX, 1);
	EXPECT_EQ(Locked.m_TargetY, 0);
}

TEST(QmFastPracticeServerInput, ReleasedFireStateAlwaysClearsThePressEdge)
{
	for(int Fire = 0; Fire <= INPUT_STATE_MASK; Fire++)
	{
		const int Released = QmPracticeReleasedFireState(Fire);
		EXPECT_EQ(Released & 1, 0);
		if((Fire & 1) == 0)
			EXPECT_EQ(Released, Fire);
	}
}

TEST(QmFastPracticeServerInput, LateDummyAttachKeepsTheExistingMainLock)
{
	// 已锁定的本体槽位不能被分身接入时的重新采样覆盖。
	EXPECT_FALSE(QmPracticeShouldCaptureServerLockedInput(true, true, 5, 5));
	EXPECT_TRUE(QmPracticeShouldCaptureServerLockedInput(true, false, 5, 5));
	EXPECT_TRUE(QmPracticeShouldCaptureServerLockedInput(true, true, 3, 5));
	EXPECT_TRUE(QmPracticeShouldCaptureServerLockedInput(false, true, 5, 5));
}

TEST(QmFastPracticeRebind, UnchangedParticipantsDoNotRebind)
{
	const int aLocalIds[NUM_DUMMIES] = {5, 3};
	const SQmPracticeRebindDecision Decision = QmResolvePracticeRebind(aLocalIds, CONN_MAIN, 5, 3, true);
	EXPECT_FALSE(Decision.m_Changed);
	EXPECT_FALSE(Decision.m_MigrateLocalState);
	EXPECT_FALSE(Decision.m_MigrateDummyState);
}

TEST(QmFastPracticeRebind, RespawnWithNewLocalIdMigratesLocalStateOnly)
{
	// 服务器死亡重进后本体换成新 id；分身的 id 不变，检查点随本体迁移。
	const int aLocalIds[NUM_DUMMIES] = {9, 3};
	const SQmPracticeRebindDecision Decision = QmResolvePracticeRebind(aLocalIds, CONN_MAIN, 5, 3, true);
	EXPECT_TRUE(Decision.m_Changed);
	EXPECT_EQ(Decision.m_LocalClientId, 9);
	EXPECT_EQ(Decision.m_DummyClientId, 3);
	EXPECT_TRUE(Decision.m_MigrateLocalState);
	EXPECT_FALSE(Decision.m_MigrateDummyState);
}

TEST(QmFastPracticeRebind, DummyStillReconnectingStaysPending)
{
	// 分身重连期间允许暂时没有 id；下一次重绑会补上，不能把要求降级成单人。
	const int aLocalIds[NUM_DUMMIES] = {9, -1};
	const SQmPracticeRebindDecision Decision = QmResolvePracticeRebind(aLocalIds, CONN_MAIN, 5, 3, true);
	EXPECT_TRUE(Decision.m_Changed);
	EXPECT_EQ(Decision.m_LocalClientId, 9);
	EXPECT_EQ(Decision.m_DummyClientId, -1);
	EXPECT_TRUE(Decision.m_MigrateLocalState);
	EXPECT_FALSE(Decision.m_MigrateDummyState);
}

TEST(QmFastPracticeRebind, RebindFollowsTheEnabledConnectionNotTheActiveTee)
{
	// 启用练习时本体在分身连接上（id 3），主连接是分身（id 5）。之后切换 cl_dummy
	// 并重进，本体仍应按启用连接取到新 id 7，而不是按当前控制的角色互换身份。
	const int aLocalIds[NUM_DUMMIES] = {5, 7};
	const SQmPracticeRebindDecision Decision = QmResolvePracticeRebind(aLocalIds, CONN_DUMMY, 3, 5, true);
	EXPECT_TRUE(Decision.m_Changed);
	EXPECT_EQ(Decision.m_LocalClientId, 7);
	EXPECT_EQ(Decision.m_DummyClientId, 5);
	EXPECT_TRUE(Decision.m_MigrateLocalState);
	EXPECT_FALSE(Decision.m_MigrateDummyState);
}

TEST(QmFastPracticeRebind, SwappedIdsDoNotMigrateStateAcrossRoles)
{
	// 新本体 id 恰好是旧分身 id：迁移会覆盖对方状态，必须跳过。
	const int aLocalIds[NUM_DUMMIES] = {3, 5};
	const SQmPracticeRebindDecision Decision = QmResolvePracticeRebind(aLocalIds, CONN_MAIN, 5, 3, true);
	EXPECT_TRUE(Decision.m_Changed);
	EXPECT_EQ(Decision.m_LocalClientId, 3);
	EXPECT_EQ(Decision.m_DummyClientId, 5);
	EXPECT_FALSE(Decision.m_MigrateLocalState);
	EXPECT_FALSE(Decision.m_MigrateDummyState);
}

TEST(QmFastPracticeRebind, MissingLocalIdLeavesTheBindingUntouched)
{
	const int aLocalIds[NUM_DUMMIES] = {-1, 3};
	const SQmPracticeRebindDecision Decision = QmResolvePracticeRebind(aLocalIds, CONN_MAIN, 5, 3, true);
	EXPECT_FALSE(Decision.m_Changed);
	EXPECT_EQ(Decision.m_LocalClientId, -1);
	EXPECT_FALSE(Decision.m_MigrateLocalState);
	EXPECT_FALSE(Decision.m_MigrateDummyState);
}

TEST(QmFastPracticeRebind, OutOfRangeLocalIdLeavesTheBindingUntouched)
{
	// 服务器 id 越界时同样不能迁移检查点或改写旧绑定。
	const int aLocalIds[NUM_DUMMIES] = {MAX_CLIENTS, -1};
	const SQmPracticeRebindDecision Decision = QmResolvePracticeRebind(aLocalIds, CONN_MAIN, 5, 3, true);
	EXPECT_FALSE(Decision.m_Changed);
	EXPECT_EQ(Decision.m_LocalClientId, -1);
	EXPECT_FALSE(Decision.m_MigrateLocalState);
	EXPECT_FALSE(Decision.m_MigrateDummyState);
}

TEST(QmFastPracticeRebind, InvalidConnectionSlotKeepsThePreviousBinding)
{
	const int aLocalIds[NUM_DUMMIES] = {5, 3};

	const SQmPracticeRebindDecision Negative = QmResolvePracticeRebind(aLocalIds, -1, 5, 3, true);
	EXPECT_FALSE(Negative.m_Changed);
	EXPECT_EQ(Negative.m_LocalClientId, -1);

	const SQmPracticeRebindDecision TooHigh = QmResolvePracticeRebind(aLocalIds, NUM_DUMMIES, 5, 3, true);
	EXPECT_FALSE(TooHigh.m_Changed);
	EXPECT_EQ(TooHigh.m_LocalClientId, -1);
}

TEST(QmFastPracticeTransientFailure, HoldsForTheWholeGraceWindow)
{
	const int GraceTicks = QmPracticeTransientGraceTicks(50);
	for(int Failure = 1; Failure <= GraceTicks; Failure++)
		EXPECT_EQ(QmResolvePracticeTransientFailure(Failure, GraceTicks), EQmPracticeTransientFailureAction::HOLD);
}

TEST(QmFastPracticeTransientFailure, DisablesAfterTheGraceWindow)
{
	const int GraceTicks = QmPracticeTransientGraceTicks(50);
	EXPECT_EQ(QmResolvePracticeTransientFailure(GraceTicks + 1, GraceTicks), EQmPracticeTransientFailureAction::DISABLE);
}

TEST(QmFastPracticeTransientFailure, GraceIsTwoSecondsOfTicks)
{
	EXPECT_EQ(QmPracticeTransientGraceTicks(50), 100);
	EXPECT_EQ(QmPracticeTransientGraceTicks(1), 2);
	EXPECT_EQ(QmPracticeTransientGraceTicks(0), 2);
	EXPECT_EQ(QmPracticeTransientGraceTicks(-5), 2);
}

TEST(QmFastPracticePauseShift, ShiftedEventsDoNotBurstOnTheFirstResumedTick)
{
	std::vector<CGameWorld::CPredictedEvent> vEvents = {Event(1, 100), Event(2, 101)};
	QmShiftPracticePredictedEventTicks(vEvents, 40);
	ASSERT_EQ(vEvents.size(), 2u);
	EXPECT_EQ(vEvents[0].m_Tick, 140);
	EXPECT_EQ(vEvents[1].m_Tick, 141);

	std::vector<int> vPlayed;
	const auto Play = [&vPlayed](const CGameWorld::CPredictedEvent &Predicted) {
		vPlayed.push_back(Predicted.m_Id);
		return true;
	};
	// 平移后的世界已经走过旧 tick，恢复的第一帧不能把积压事件一次性补播。
	QmProcessPredictedEvents(vEvents, 139, SERVER_TICK_SPEED, Play);
	EXPECT_TRUE(vPlayed.empty());
	QmProcessPredictedEvents(vEvents, 140, SERVER_TICK_SPEED, Play);
	EXPECT_EQ(vPlayed, (std::vector<int>{1}));
}

TEST(QmFastPracticeEventBarrier, AllowsOnlyTicksAfterTheBarrier)
{
	EXPECT_TRUE(QmPracticeTickAllowsPredictedEvents(106, 105));
	EXPECT_FALSE(QmPracticeTickAllowsPredictedEvents(105, 105));
	EXPECT_FALSE(QmPracticeTickAllowsPredictedEvents(104, 105));
	EXPECT_TRUE(QmPracticeTickAllowsPredictedEvents(0, -1));
}

TEST(QmFastPracticeEventBarrier, ReplayedTicksNeverPlayTheSameShotTwice)
{
	// 分支世界在 tick 100/101 各开一枪并播放，随后重新 fork 从 tick 100 重算。
	// 重算区间已经播过，声学上必须是静默的，只有新 tick 才发一次。
	int BarrierTick = -1;
	std::vector<CGameWorld::CPredictedEvent> vQueue;
	std::vector<int> vPlayed;
	const auto Play = [&vPlayed](const CGameWorld::CPredictedEvent &Predicted) {
		vPlayed.push_back(Predicted.m_Tick);
		return true;
	};
	const auto StepTick = [&](int Tick) {
		if(QmPracticeTickAllowsPredictedEvents(Tick, BarrierTick))
			vQueue.push_back(Event(7, Tick));
		QmProcessPredictedEvents(vQueue, Tick, SERVER_TICK_SPEED, Play);
		BarrierTick = std::max(BarrierTick, Tick);
	};

	StepTick(100);
	StepTick(101);
	EXPECT_EQ(vPlayed, (std::vector<int>{100, 101}));

	// 重新 fork：新世界带着服务器世界的旧队列，重复事件去重没有依据。
	QmResetPracticePredictedEvents(vQueue);
	EXPECT_TRUE(vQueue.empty());
	BarrierTick = 101;
	StepTick(100);
	StepTick(101);
	StepTick(102);
	EXPECT_EQ(vPlayed, (std::vector<int>{100, 101, 102}));
}

TEST(QmFastPracticeEventBarrier, RestartDropsEventsOfTheDiscardedTimeline)
{
	// 重开练习世界带回快照初刻的队列，这些 tick 已经被屏障盖过。
	std::vector<CGameWorld::CPredictedEvent> vEvents = {Event(1, 100), Event(2, 104), Event(3, 105), Event(4, 106)};
	QmDropPracticePredictedEventsUntil(vEvents, 105);
	ASSERT_EQ(vEvents.size(), 1u);
	EXPECT_EQ(vEvents[0].m_Tick, 106);
}

TEST(QmFastPracticeRecordFilter, AcceptsVisualAndSoundEventsThatPlaybackWouldShow)
{
	// 特效类事件与音效开关无关，总是录制。
	EXPECT_TRUE(QmShouldRecordPracticeEvent(NETEVENTTYPE_HAMMERHIT, 0, false, false, false, false, false, false));
	EXPECT_TRUE(QmShouldRecordPracticeEvent(NETEVENTTYPE_EXPLOSION, 0, false, false, false, false, false, false));
	EXPECT_TRUE(QmShouldRecordPracticeEvent(NETEVENTTYPE_DAMAGEIND, 0, false, false, false, false, false, false));
	// 世界音效沿用播放侧门控：关掉游戏音效就不录。
	EXPECT_TRUE(QmShouldRecordPracticeEvent(NETEVENTTYPE_SOUNDWORLD, SOUND_GUN_FIRE, true, true, true, false, true, true));
	EXPECT_FALSE(QmShouldRecordPracticeEvent(NETEVENTTYPE_SOUNDWORLD, SOUND_GUN_FIRE, false, true, true, false, true, true));
}

TEST(QmFastPracticeRecordFilter, MapWorldSoundFollowsTheGameSoundSwitch)
{
	// 与 DispatchViewEvent 的 MAPSOUNDWORLD 分支一致：只看游戏音效总开关。
	EXPECT_TRUE(QmShouldRecordPracticeEvent(NETEVENTTYPE_MAPSOUNDWORLD, 3, true, false, false, false, false, false));
	EXPECT_FALSE(QmShouldRecordPracticeEvent(NETEVENTTYPE_MAPSOUNDWORLD, 3, false, false, false, false, false, false));
}

TEST(QmFastPracticeRecordFilter, DropsEventTypesTheTimelineReaderWouldReject)
{
	EXPECT_FALSE(QmShouldRecordPracticeEvent(NETEVENTTYPE_DEATH, 0, true, true, true, true, true, true));
	EXPECT_FALSE(QmShouldRecordPracticeEvent(NETEVENTTYPE_SPAWN, 0, true, true, true, true, true, true));
	EXPECT_FALSE(QmShouldRecordPracticeEvent(9999, 0, true, true, true, true, true, true));
}

TEST(QmFastPracticeControlledTee, MainControlResolvesTheMainParticipant)
{
	// 本体在 conn0、分身在 conn1；控制本体时命令与检查点落在本体上。
	const int aLocalIds[NUM_DUMMIES] = {3, 4};
	EXPECT_EQ(QmResolveControlledPracticeClientId(aLocalIds, false, 3, 4), 3);
}

TEST(QmFastPracticeControlledTee, DummyControlResolvesTheDummyParticipant)
{
	// 切到 cl_dummy 后按键属于分身连接：检查点与 /r 必须跟着换人。
	const int aLocalIds[NUM_DUMMIES] = {3, 4};
	EXPECT_EQ(QmResolveControlledPracticeClientId(aLocalIds, true, 3, 4), 4);
}

TEST(QmFastPracticeControlledTee, DummyControlWithoutADummyFallsBackToMain)
{
	// 分身没接入（id -1）时控制槽位不是参与者，退回另一条参与者连接，
	// 否则命令会静默失效。
	const int aLocalIds[NUM_DUMMIES] = {3, -1};
	EXPECT_EQ(QmResolveControlledPracticeClientId(aLocalIds, true, 3, -1), 3);
}

TEST(QmFastPracticeControlledTee, SpectatingResolvesToNoParticipant)
{
	// 旁观时两条本地连接都不是参与者：返回 -1，由调用方决定是否回退。
	const int aLocalIds[NUM_DUMMIES] = {9, 10};
	EXPECT_EQ(QmResolveControlledPracticeClientId(aLocalIds, false, 3, 4), -1);
}

TEST(QmFastPracticeControlledTee, OutOfRangeSlotsAreNeverParticipants)
{
	// 服务器 id 越界与 -1 一样不参与，不能把命令发往不存在的连接。
	const int aLocalIds[NUM_DUMMIES] = {MAX_CLIENTS, -1};
	EXPECT_EQ(QmResolveControlledPracticeClientId(aLocalIds, false, 3, 4), -1);
	EXPECT_FALSE(QmIsPracticeParticipant(-1, 3, 4));
	EXPECT_FALSE(QmIsPracticeParticipant(MAX_CLIENTS, 3, 4));
}

TEST(QmFastPracticeDeathTile, FreshDeathPlaysFeedbackAndReturnsToTheSavedPoint)
{
	// 与 /r 一致：踩到死亡 tile 的那一刻播一次死亡效果并回到保存点。
	const SQmPracticeDeathTileDecision Decision = QmEvaluatePracticeDeathTile(true, false, true);
	EXPECT_TRUE(Decision.m_PlayFeedback);
	EXPECT_TRUE(Decision.m_ReturnToSavedPoint);
}

TEST(QmFastPracticeDeathTile, StayingInDeathDoesNotRetrigger)
{
	// 停在死亡 tile 上时预测每 tick 都报告“在死亡区域”，不能每 tick 都传送。
	const SQmPracticeDeathTileDecision Decision = QmEvaluatePracticeDeathTile(false, false, true);
	EXPECT_FALSE(Decision.m_PlayFeedback);
	EXPECT_FALSE(Decision.m_ReturnToSavedPoint);
}

TEST(QmFastPracticeDeathTile, InvincibleOrSuperIgnoresDeathTilesLikeTheServer)
{
	// 服务端 HandleSkippableTiles 在 super / invincible 时完全不处理死亡 tile；
	// 客户端分支世界必须保持同一行为，否则会凭空传送。
	const SQmPracticeDeathTileDecision Decision = QmEvaluatePracticeDeathTile(true, true, true);
	EXPECT_FALSE(Decision.m_PlayFeedback);
	EXPECT_FALSE(Decision.m_ReturnToSavedPoint);
}

TEST(QmFastPracticeDeathTile, NoSavedPointStillPlaysFeedbackWithoutTeleporting)
{
	// 首次死亡且还没有救援点：只播反馈，不做位置突变，也不能假装回退成功。
	const SQmPracticeDeathTileDecision Decision = QmEvaluatePracticeDeathTile(true, false, false);
	EXPECT_TRUE(Decision.m_PlayFeedback);
	EXPECT_FALSE(Decision.m_ReturnToSavedPoint);
}
