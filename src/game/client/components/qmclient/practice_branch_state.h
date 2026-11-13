#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_BRANCH_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_BRANCH_STATE_H

#include <engine/client/enums.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/client/prediction/gameworld.h>

#include <algorithm>
#include <vector>

// 快速练习分支世界的生命周期判定。这里只放不依赖 CUi / CGameClient 的纯策略，
// 让「重进换 id」「真实输入锁」「瞬态失败宽限」「暂停平移事件」这些出过实机
// 问题的路径可以被行为测试直接覆盖。

// 开火位图里 bit0 表示「本 tick 按下」。把按住状态改写成已释放，避免服务器把
// 练习期间一直按住的键当成一次新的按下（退出或重开落地时立刻挥锤、开枪）。
inline int QmPracticeReleasedFireState(int FireState)
{
	FireState &= INPUT_STATE_MASK;
	if((FireState & 1) != 0)
		FireState = (FireState + 1) & INPUT_STATE_MASK;
	return FireState;
}

// 压制动作请求：只改开火位图与换武器请求，不动瞄准、移动或钩子。
inline void QmPracticeSuppressActionInput(CNetObj_PlayerInput &Input, int FireState)
{
	Input.m_Fire = FireState & INPUT_STATE_MASK;
	Input.m_WantedWeapon = 0;
	Input.m_NextWeapon = 0;
	Input.m_PrevWeapon = 0;
}

// 练习期间发往真实服务器的输入：保留瞄准与钩子（挂墙本体不会松钩掉落），
// 释放移动、跳跃、开火与换武器请求。瞄准为零向量时补一个合法方向。
inline CNetObj_PlayerInput QmBuildPracticeServerLockedInput(const CNetObj_PlayerInput &Source)
{
	CNetObj_PlayerInput Input = Source;
	if(Input.m_TargetX == 0 && Input.m_TargetY == 0)
		Input.m_TargetX = 1;
	Input.m_Direction = 0;
	Input.m_Jump = 0;
	QmPracticeSuppressActionInput(Input, QmPracticeReleasedFireState(Input.m_Fire));
	return Input;
}

// 分身晚接入时，主角色必须继续沿用进入练习前的锁定输入；只有还没锁过的槽位
// （新接入的分身）才重新采样。按连接槽位对应的 client id 判断，而不是槽位号，
// 因为 cl_dummy 切换会改变哪个槽位控制哪个角色。
inline bool QmPracticeShouldCaptureServerLockedInput(bool Enabled, bool AlreadyLocked, int SlotClientId, int EnableLocalClientId)
{
	return !Enabled || !AlreadyLocked || SlotClientId != EnableLocalClientId;
}

struct SQmPracticeRebindDecision
{
	bool m_Changed = false;
	int m_LocalClientId = -1;
	int m_DummyClientId = -1;
	// 旧 id 的检查点、救援点需要迁移到新 id；参与者 id 互换时不迁移，
	// 否则会把本体的状态覆盖到分身槽位上。
	bool m_MigrateLocalState = false;
	bool m_MigrateDummyState = false;
};

// 服务器死亡重进会分配新的 client id。角色身份跟着启用时的连接走（LocalConn），
// 而不是跟着 cl_dummy 当前指向，这样中途切换控制对象也不会把两个参与者互换。
// 分身重连期间允许暂时为 -1，连回来后的下一次重绑会补上。
inline SQmPracticeRebindDecision QmResolvePracticeRebind(const int aLocalIds[NUM_DUMMIES], int LocalConn, int OldLocalId, int OldDummyId, bool RequireDummy)
{
	SQmPracticeRebindDecision Decision;
	if(LocalConn < 0 || LocalConn >= NUM_DUMMIES)
		return Decision;
	const int LocalId = aLocalIds[LocalConn];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return Decision;
	Decision.m_LocalClientId = LocalId;

	if(RequireDummy)
	{
		const int DummyId = aLocalIds[LocalConn ^ 1];
		if(DummyId >= 0 && DummyId < MAX_CLIENTS && DummyId != LocalId)
			Decision.m_DummyClientId = DummyId;
	}

	Decision.m_Changed = Decision.m_LocalClientId != OldLocalId || Decision.m_DummyClientId != OldDummyId;
	Decision.m_MigrateLocalState = OldLocalId >= 0 && OldLocalId < MAX_CLIENTS &&
				       Decision.m_LocalClientId != OldLocalId && Decision.m_LocalClientId != OldDummyId;
	Decision.m_MigrateDummyState = OldDummyId >= 0 && OldDummyId < MAX_CLIENTS &&
				       Decision.m_DummyClientId >= 0 && Decision.m_DummyClientId != OldDummyId && Decision.m_DummyClientId != OldLocalId;
	return Decision;
}

enum class EQmPracticeTransientFailureAction
{
	HOLD,
	DISABLE,
};

// 角色或输入短暂解析失败时先冻结分支世界等恢复；超过宽限才真正退出练习。
inline int QmPracticeTransientGraceTicks(int TickSpeed)
{
	return std::max(TickSpeed, 1) * 2;
}

inline EQmPracticeTransientFailureAction QmResolvePracticeTransientFailure(int ConsecutiveFailures, int GraceTicks)
{
	return ConsecutiveFailures <= GraceTicks ? EQmPracticeTransientFailureAction::HOLD : EQmPracticeTransientFailureAction::DISABLE;
}

// 服务器暂停恢复时分支时钟整体平移。预测事件队列会随 CopyWorldClean 进入预测
// 世界，tick 不一起平移的话，平移后的第一帧会把积压事件一次性播出来。
inline void QmShiftPracticePredictedEventTicks(std::vector<CGameWorld::CPredictedEvent> &vEvents, int TickDelta)
{
	for(auto &Event : vEvents)
		Event.m_Tick += TickDelta;
}

#endif
