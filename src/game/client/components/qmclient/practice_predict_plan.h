#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_PREDICT_PLAN_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_PREDICT_PLAN_H

// 快速练习两条预测循环共用的 tick 计划与发布节拍。
//
// 主循环把分支世界从 BaseTick 追赶到 PredTick；视觉循环再从 PredTick 外推
// FastInputTicks 帧，给快速输入提供预览。两条循环维护同一组预测状态
// （m_PrevPredictedWorld、m_PredictedChar、m_aClients[].m_Predicted、
// m_aPredPos），过去各自写一份 tick 判断，出过「末帧核心写错 tick」
// 和「新预测 tick 反馈重复播放」的问题。这里只放不依赖 CGameClient 的
// 纯判定，循环本身按判定结果发布，行为测试直接覆盖这些判定。

struct SPracticePredictTickPlan
{
	// 循环覆盖 [m_FirstTick, m_FinalTickSelf]，两端都执行。
	int m_FirstTick = 0;
	int m_FinalTickSelf = 0;
	// 其他玩家核心最终发布的 tick。主循环等于 m_FinalTickSelf；
	// 视觉循环在关闭「快速输入作用于他人」时停在真实末帧。
	int m_FinalTickOthers = 0;

	bool Empty() const { return m_FirstTick > m_FinalTickSelf; }
	bool Covers(int Tick) const { return Tick >= m_FirstTick && Tick <= m_FinalTickSelf; }
	int StepCount() const { return Empty() ? 0 : m_FinalTickSelf - m_FirstTick + 1; }
	bool PublishesSelfFinal(int Tick) const { return Tick == m_FinalTickSelf; }
	bool PublishesOthersFinal(int Tick) const { return Tick == m_FinalTickOthers; }
};

// FastInputTicks = 0 生成主循环计划：BaseTick 是分支世界当前 tick，
// FinalTickRegular 是服务器请求的预测 tick，循环补齐中间缺失的 tick。
// FastInputTicks > 0 生成视觉外推计划：BaseTick 传入 FinalTickRegular，
// 调用方在此之前已保证 FastInputTicks 为正。
// PredictOthers 只影响其他玩家的最终发布时机，主循环固定覆盖所有人。
inline SPracticePredictTickPlan QmBuildPracticePredictTickPlan(int BaseTick, int FinalTickRegular, int FastInputTicks, bool PredictOthers)
{
	SPracticePredictTickPlan Plan;
	Plan.m_FirstTick = BaseTick + 1;
	Plan.m_FinalTickSelf = FinalTickRegular + FastInputTicks;
	Plan.m_FinalTickOthers = PredictOthers ? Plan.m_FinalTickSelf : FinalTickRegular;
	return Plan;
}

// 新预测 tick 反馈（跳跃特效、跳跃与钩子音效）每个输入槽位只消费一次。
// 调用方是主循环，tick 天然不超过真实末帧；视觉循环不播放反馈。
inline bool QmPracticeConsumeNewPredictedTick(int Tick, int &LastConsumedTick)
{
	if(Tick <= LastConsumedTick)
		return false;
	LastConsumedTick = Tick;
	return true;
}

// 分支时钟的对齐目标。cl_dummy 刚切到分身连接时，这条连接的预测 tick 还停在 0，
// 直接对齐会让分支世界倒退回零点；取较大值保证时钟只前进、不倒退。
inline int QmPracticeClockTarget(int PredTick, int BranchTick)
{
	return PredTick > BranchTick ? PredTick : BranchTick;
}

// 主循环逐 tick 补算的步数上限（两秒）。更大的缺口只能来自时钟断层
// （切换分身连接、预测时钟重置），逐 tick 仿真会把主线程卡死；
// 调用方应改为平移时钟，与暂停恢复的语义一致。
inline bool QmPracticeCatchUpNeedsShift(int StepCount, int GameTickSpeed)
{
	return StepCount > GameTickSpeed * 2;
}

// 单 tick 的预测状态发布动作。步进前写「上一帧」，步进后写「当前帧」；
// 本体角色与其他人可以停在不同 tick（视觉外推只延长本体时）。
// 两条循环都按这份判定发布，避免某一处漏写或写错 tick。
struct SPracticePublishActions
{
	bool m_CopyPrevWorld = false;
	bool m_PublishPrevRole = false;
	bool m_PublishPrevOthers = false;
	bool m_PublishPredRole = false;
	bool m_PublishPredOthers = false;
};

inline SPracticePublishActions QmResolvePracticePublishActions(const SPracticePredictTickPlan &Plan, int Tick)
{
	SPracticePublishActions Actions;
	// 步进前的世界就是上一帧：镜头插值与预测差分都从这里取。
	if(Plan.PublishesSelfFinal(Tick))
	{
		Actions.m_CopyPrevWorld = true;
		Actions.m_PublishPrevRole = true;
		Actions.m_PublishPredRole = true;
	}
	Actions.m_PublishPrevOthers = Plan.PublishesOthersFinal(Tick);
	Actions.m_PublishPredOthers = Plan.PublishesOthersFinal(Tick);
	return Actions;
}

#endif
