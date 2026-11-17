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
	// 关闭快速输入作用于他人时，他人最终 tick 落在循环范围之外。
	// 主循环已经发布过他人核心，外推帧不能再写他人。
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(104, 104, 6, false);

	EXPECT_EQ(Plan.m_FinalTickOthers, 104);
	EXPECT_FALSE(Plan.Covers(Plan.m_FinalTickOthers));
	for(int Tick = Plan.m_FirstTick; Tick <= Plan.m_FinalTickSelf; Tick++)
	{
		EXPECT_FALSE(Plan.PublishesOthersFinal(Tick)) << "tick " << Tick;
		EXPECT_EQ(Plan.PublishesSelfFinal(Tick), Tick == 110) << "tick " << Tick;
	}
}

TEST(QmPracticePredictPlan, CoversMatchesTheExecutedRange)
{
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(104, 104, 6, true);
	EXPECT_FALSE(Plan.Covers(104));
	EXPECT_TRUE(Plan.Covers(105));
	EXPECT_TRUE(Plan.Covers(110));
	EXPECT_FALSE(Plan.Covers(111));

	const SPracticePredictTickPlan Empty = QmBuildPracticePredictTickPlan(104, 100, 0, true);
	EXPECT_FALSE(Empty.Covers(100));
	EXPECT_FALSE(Empty.Covers(105));
}

TEST(QmPracticePredictPlan, PublishActionsOnlyFireInsideThePlannedTicks)
{
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(100, 104, 0, true);

	const SPracticePublishActions BeforeRange = QmResolvePracticePublishActions(Plan, 100);
	EXPECT_FALSE(BeforeRange.m_CopyPrevWorld);
	EXPECT_FALSE(BeforeRange.m_PublishPrevRole);
	EXPECT_FALSE(BeforeRange.m_PublishPrevOthers);
	EXPECT_FALSE(BeforeRange.m_PublishPredRole);
	EXPECT_FALSE(BeforeRange.m_PublishPredOthers);

	const SPracticePublishActions AfterRange = QmResolvePracticePublishActions(Plan, 105);
	EXPECT_FALSE(AfterRange.m_CopyPrevWorld);
	EXPECT_FALSE(AfterRange.m_PublishPredRole);
	EXPECT_FALSE(AfterRange.m_PublishPredOthers);
}

TEST(QmPracticePredictPlan, NewPredictedTickConsumesEachAdvancedTickOnce)
{
	int LastConsumedTick = 104;

	EXPECT_TRUE(QmPracticeConsumeNewPredictedTick(105, LastConsumedTick));
	EXPECT_EQ(LastConsumedTick, 105);
	// 同一 tick 在分身/本体两个槽位回放时不能重复触发音效。
	EXPECT_FALSE(QmPracticeConsumeNewPredictedTick(105, LastConsumedTick));
	EXPECT_EQ(LastConsumedTick, 105);

	EXPECT_TRUE(QmPracticeConsumeNewPredictedTick(106, LastConsumedTick));
	EXPECT_EQ(LastConsumedTick, 106);
}

TEST(QmPracticePredictPlan, NewPredictedTickStartsAtZeroAndRejectsRegression)
{
	// 进入对局后的首批预测 tick 从 0 开始，初始哨兵为 -1。
	int LastConsumedTick = -1;
	EXPECT_TRUE(QmPracticeConsumeNewPredictedTick(0, LastConsumedTick));
	EXPECT_EQ(LastConsumedTick, 0);
	EXPECT_FALSE(QmPracticeConsumeNewPredictedTick(0, LastConsumedTick));

	// 乱序或回退的 tick 不消费，也不改记录。
	EXPECT_FALSE(QmPracticeConsumeNewPredictedTick(-1, LastConsumedTick));
	EXPECT_EQ(LastConsumedTick, 0);
	EXPECT_TRUE(QmPracticeConsumeNewPredictedTick(1, LastConsumedTick));
	EXPECT_FALSE(QmPracticeConsumeNewPredictedTick(0, LastConsumedTick));
	EXPECT_EQ(LastConsumedTick, 1);
}

// 分支时钟只允许前进。切换 cl_dummy 后分身连接的预测 tick 仍是 0，
// 若直接对齐，下一帧会从 0 一路补算到当前时间。
TEST(QmPracticePredictPlan, ClockTargetKeepsBranchTickWhenPredTickUninitialized)
{
	EXPECT_EQ(QmPracticeClockTarget(0, 60000), 60000);
	EXPECT_EQ(QmPracticeClockTarget(60010, 60000), 60010);
	EXPECT_EQ(QmPracticeClockTarget(60000, 60000), 60000);
	EXPECT_EQ(QmPracticeClockTarget(-1, 0), 0);
}

// 两秒以内的缺口逐 tick 补算；更大缺口必须走平移，避免主线程被数万 tick 卡死。
TEST(QmPracticePredictPlan, CatchUpShiftOnlyForGapsBeyondTwoSeconds)
{
	EXPECT_FALSE(QmPracticeCatchUpNeedsShift(1, 50));
	EXPECT_FALSE(QmPracticeCatchUpNeedsShift(100, 50));
	EXPECT_TRUE(QmPracticeCatchUpNeedsShift(101, 50));
	EXPECT_TRUE(QmPracticeCatchUpNeedsShift(60000, 50));
}
