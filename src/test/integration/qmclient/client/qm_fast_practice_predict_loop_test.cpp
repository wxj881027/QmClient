#include <generated/protocol.h>

#include <game/client/components/qmclient/practice_branch_state.h>
#include <game/client/components/qmclient/practice_predict_plan.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

// 预测循环的恢复路径：快速练习接管预测后，服务器重进、暂停恢复与分身
// 晚接入都要求 tick 计划、参与权重绑和事件平移保持同一份节拍。
// 这里组合三条生产策略的实际输出，防止某一环单独改动后恢复首帧错位。

namespace
{
	// 与 IClient::CONN_MAIN / CONN_DUMMY 的顺序一致。
	constexpr int CONN_MAIN = 0;
	constexpr int CONN_DUMMY = 1;

	CGameWorld::CPredictedEvent SoundEvent(int Id, int Tick)
	{
		return CGameWorld::CPredictedEvent(NETEVENTTYPE_SOUNDWORLD, vec2(0.0f, 0.0f), Id, Tick, 0);
	}

	int CountEventsBeforeTick(const std::vector<CGameWorld::CPredictedEvent> &vEvents, int Tick)
	{
		return (int)std::count_if(vEvents.begin(), vEvents.end(), [Tick](const CGameWorld::CPredictedEvent &Event) {
			return Event.m_Tick < Tick;
		});
	}
}

TEST(QmFastPracticePredictLoopRecovery, CatchUpPublishesOnlyAtTheFinalTick)
{
	// 服务器预测 tick 跳到 4804，分支世界停在 4800：循环补齐 4801..4804。
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(4800, 4804, 0, true);
	ASSERT_EQ(Plan.m_FirstTick, 4801);
	ASSERT_EQ(Plan.StepCount(), 4);

	const SPracticePublishActions First = QmResolvePracticePublishActions(Plan, 4801);
	EXPECT_FALSE(First.m_CopyPrevWorld);
	EXPECT_FALSE(First.m_PublishPrevRole);
	EXPECT_FALSE(First.m_PublishPrevOthers);
	EXPECT_FALSE(First.m_PublishPredRole);
	EXPECT_FALSE(First.m_PublishPredOthers);

	const SPracticePublishActions Middle = QmResolvePracticePublishActions(Plan, 4802);
	EXPECT_FALSE(Middle.m_PublishPredRole);
	EXPECT_FALSE(Middle.m_PublishPredOthers);

	// 末帧才发布上一世界与本体核心，其他玩家也在同一 tick 收口。
	const SPracticePublishActions Last = QmResolvePracticePublishActions(Plan, 4804);
	EXPECT_TRUE(Last.m_CopyPrevWorld);
	EXPECT_TRUE(Last.m_PublishPrevRole);
	EXPECT_TRUE(Last.m_PublishPrevOthers);
	EXPECT_TRUE(Last.m_PublishPredRole);
	EXPECT_TRUE(Last.m_PublishPredOthers);
}

TEST(QmFastPracticePredictLoopRecovery, RejoinHoldsTheFrozenBaseThenCatchesUpAfterRebind)
{
	const int GraceTicks = QmPracticeTransientGraceTicks(66);
	// 重进后的前两帧角色还解析不出：冻结分支世界，不推进 tick。
	EXPECT_EQ(QmResolvePracticeTransientFailure(1, GraceTicks), EQmPracticeTransientFailureAction::HOLD);
	EXPECT_EQ(QmResolvePracticeTransientFailure(2, GraceTicks), EQmPracticeTransientFailureAction::HOLD);

	// 下一次解析成功，本体换到新 id；分身 id 不变，检查点只随本体迁移。
	const int aLocalIds[NUM_DUMMIES] = {9, 3};
	const SQmPracticeRebindDecision Rebind = QmResolvePracticeRebind(aLocalIds, CONN_MAIN, 5, 3, true);
	ASSERT_TRUE(Rebind.m_Changed);
	EXPECT_EQ(Rebind.m_LocalClientId, 9);
	EXPECT_EQ(Rebind.m_DummyClientId, 3);
	EXPECT_TRUE(Rebind.m_MigrateLocalState);
	EXPECT_FALSE(Rebind.m_MigrateDummyState);

	// 冻结期间世界停在 4800；恢复后从 4801 补齐到服务器预测 tick。
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(4800, 4803, 0, true);
	EXPECT_EQ(Plan.m_FirstTick, 4801);
	EXPECT_EQ(Plan.StepCount(), 3);
	EXPECT_TRUE(QmResolvePracticePublishActions(Plan, 4803).m_PublishPredOthers);
}

TEST(QmFastPracticePredictLoopRecovery, PauseResumeShiftLeavesNoBacklogBeforeTheFirstTick)
{
	// 服务器暂停恢复后分支时钟整体前移 6 tick，事件队列随世界一起平移。
	std::vector<CGameWorld::CPredictedEvent> vEvents = {SoundEvent(1, 4801), SoundEvent(2, 4805)};
	QmShiftPracticePredictedEventTicks(vEvents, 6);

	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(4806, 4808, 0, true);
	ASSERT_EQ(Plan.m_FirstTick, 4807);

	// 恢复后的第一帧之前没有积压事件，不会集中播放暂停期间的音效。
	EXPECT_EQ(CountEventsBeforeTick(vEvents, Plan.m_FirstTick), 0);
	EXPECT_EQ(vEvents[0].m_Tick, 4807);
	EXPECT_EQ(vEvents[1].m_Tick, 4811);
}

TEST(QmFastPracticePredictLoopRecovery, LateDummyAttachAddsTheOtherPlayerToTheRealFinalTick)
{
	// 分身晚接入：旧 id -1（重连中），新 id 3，检查点需要迁移到新 id。
	const int aLocalIds[NUM_DUMMIES] = {9, 3};
	const SQmPracticeRebindDecision Rebind = QmResolvePracticeRebind(aLocalIds, CONN_MAIN, 9, 7, true);
	ASSERT_EQ(Rebind.m_DummyClientId, 3);
	EXPECT_TRUE(Rebind.m_MigrateDummyState);

	// 关闭快速输入作用于他人：视觉外推只延长本体，其他玩家停在真实末帧。
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(104, 104, 6, false);

	const SPracticePublishActions AtRealFinal = QmResolvePracticePublishActions(Plan, 104);
	EXPECT_TRUE(AtRealFinal.m_PublishPrevOthers);
	EXPECT_TRUE(AtRealFinal.m_PublishPredOthers);
	EXPECT_FALSE(AtRealFinal.m_PublishPrevRole);
	EXPECT_FALSE(AtRealFinal.m_PublishPredRole);

	const SPracticePublishActions AtVisualFinal = QmResolvePracticePublishActions(Plan, 110);
	EXPECT_FALSE(AtVisualFinal.m_PublishPrevOthers);
	EXPECT_FALSE(AtVisualFinal.m_PublishPredOthers);
	EXPECT_TRUE(AtVisualFinal.m_PublishPrevRole);
	EXPECT_TRUE(AtVisualFinal.m_PublishPredRole);
}

TEST(QmFastPracticePredictLoopRecovery, VisualOthersExtensionPublishesEveryoneAtTheVisualFinalTick)
{
	// 打开快速输入作用于他人时，其他玩家与本体的核心都在视觉末帧收口。
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(104, 104, 6, true);
	EXPECT_EQ(Plan.m_FinalTickOthers, 110);

	const SPracticePublishActions AtRealFinal = QmResolvePracticePublishActions(Plan, 104);
	EXPECT_FALSE(AtRealFinal.m_PublishPrevOthers);
	EXPECT_FALSE(AtRealFinal.m_PublishPredRole);

	const SPracticePublishActions AtVisualFinal = QmResolvePracticePublishActions(Plan, 110);
	EXPECT_TRUE(AtVisualFinal.m_PublishPrevOthers);
	EXPECT_TRUE(AtVisualFinal.m_PublishPredOthers);
	EXPECT_TRUE(AtVisualFinal.m_PublishPredRole);
}
