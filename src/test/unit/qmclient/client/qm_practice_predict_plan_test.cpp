#include <game/client/components/qmclient/practice_predict_plan.h>

#include <gtest/gtest.h>

// 预测循环的 tick 范围与发布节拍。主循环负责追赶服务器预测 tick，
// 视觉循环负责快速输入外推；两条循环共用这里的判定。
TEST(QmPracticePredictPlan, MainLoopCatchesUpEveryMissingTick)
{
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(100, 104, 0, true);

	EXPECT_EQ(Plan.m_FirstTick, 101);
	EXPECT_EQ(Plan.m_FinalTickSelf, 104);
	EXPECT_EQ(Plan.m_FinalTickOthers, 104);
	EXPECT_EQ(Plan.StepCount(), 4);
	EXPECT_FALSE(Plan.Empty());
}

TEST(QmPracticePredictPlan, MainLoopWithNoMissingTicksRunsNothing)
{
	const SPracticePredictTickPlan SameTick = QmBuildPracticePredictTickPlan(100, 100, 0, true);
	EXPECT_TRUE(SameTick.Empty());
	EXPECT_EQ(SameTick.StepCount(), 0);

	// 服务器 tick 落后于分支世界时同样不跑：重复推进会把世界推到未来。
	const SPracticePredictTickPlan Behind = QmBuildPracticePredictTickPlan(104, 100, 0, true);
	EXPECT_TRUE(Behind.Empty());
	EXPECT_EQ(Behind.StepCount(), 0);
}

TEST(QmPracticePredictPlan, MainLoopPublishesFinalOnlyAtThePredictedTick)
{
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(100, 104, 0, true);

	for(int Tick = Plan.m_FirstTick; Tick <= Plan.m_FinalTickSelf; Tick++)
	{
		const bool Final = Tick == 104;
		EXPECT_EQ(Plan.PublishesSelfFinal(Tick), Final) << "tick " << Tick;
		EXPECT_EQ(Plan.PublishesOthersFinal(Tick), Final) << "tick " << Tick;
	}
}

TEST(QmPracticePredictPlan, VisualLoopExtendsPastTheRealFinalTick)
{
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(104, 104, 6, true);

	EXPECT_EQ(Plan.m_FirstTick, 105);
	EXPECT_EQ(Plan.m_FinalTickSelf, 110);
	EXPECT_EQ(Plan.m_FinalTickOthers, 110);
	EXPECT_EQ(Plan.StepCount(), 6);
	EXPECT_FALSE(Plan.PublishesSelfFinal(104));
	EXPECT_TRUE(Plan.PublishesSelfFinal(110));
}

TEST(QmPracticePredictPlan, VisualLoopKeepsOtherPlayersAtTheRealFinalTick)
{
	// 关闭快速输入作用于他人时，其他玩家的核心不能停在视觉外推帧上，
	// 否则画面会把别人画到未来位置。
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(104, 104, 6, false);

	EXPECT_EQ(Plan.m_FinalTickOthers, 104);
	EXPECT_TRUE(Plan.PublishesOthersFinal(104));
	EXPECT_FALSE(Plan.PublishesOthersFinal(110));
	EXPECT_TRUE(Plan.PublishesSelfFinal(110));

	// 真实末帧先发布他人，之后外推帧只更新本体。
	EXPECT_NE(Plan.PublishesOthersFinal(104), Plan.PublishesSelfFinal(104));
}

TEST(QmPracticePredictPlan, NegativeFastInputTicksDoNotShrinkTheRange)
{
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(100, 104, -3, false);

	EXPECT_EQ(Plan.m_FinalTickSelf, 104);
	EXPECT_EQ(Plan.m_FinalTickOthers, 104);
	EXPECT_EQ(Plan.StepCount(), 4);
}

TEST(QmPracticePredictPlan, NewPredictedTickConsumesEachAdvancedTickOnce)
{
	int LastConsumedTick = 104;

	EXPECT_TRUE(QmPracticeConsumeNewPredictedTick(105, 110, LastConsumedTick));
	EXPECT_EQ(LastConsumedTick, 105);
	// 同一 tick 在分身/本体两个槽位回放时不能重复触发音效。
	EXPECT_FALSE(QmPracticeConsumeNewPredictedTick(105, 110, LastConsumedTick));
	EXPECT_EQ(LastConsumedTick, 105);

	EXPECT_TRUE(QmPracticeConsumeNewPredictedTick(106, 110, LastConsumedTick));
	EXPECT_EQ(LastConsumedTick, 106);
}

TEST(QmPracticePredictPlan, VisualExtrapolatedTicksDoNotConsumeFeedback)
{
	int LastConsumedTick = 109;

	// 视觉外推帧（111）超过真实末帧 110，不播放跳跃/钩子反馈。
	EXPECT_FALSE(QmPracticeConsumeNewPredictedTick(111, 110, LastConsumedTick));
	EXPECT_EQ(LastConsumedTick, 109);

	// 真实末帧本身仍可消费。
	EXPECT_TRUE(QmPracticeConsumeNewPredictedTick(110, 110, LastConsumedTick));
	EXPECT_EQ(LastConsumedTick, 110);
}
