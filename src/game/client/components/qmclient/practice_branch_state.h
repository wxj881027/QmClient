#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_BRANCH_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRACTICE_BRANCH_STATE_H

#include <engine/client/enums.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/input_state.h>
#include <game/client/components/qmclient/world_event_gate.h>
#include <game/client/prediction/gameworld.h>

#include <algorithm>
#include <string_view>
#include <vector>

// 快速练习分支世界的生命周期判定。这里只放不依赖 CUi / CGameClient 的纯策略，
// 让「重进换 id」「真实输入锁」「瞬态失败宽限」「暂停平移事件」这些出过实机
// 问题的路径可以被行为测试直接覆盖。

// 开火位图里 bit0 表示「本 tick 按下」。把按住状态改写成已释放，避免服务器把
// 练习期间一直按住的键当成一次新的按下（退出或重开落地时立刻挥锤、开枪）。
inline int QmPracticeReleasedFireState(int FireState)
{
	return QmReleasedFireState(FireState) & INPUT_STATE_MASK;
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

// 没有可锁定的服务器输入时使用的中性输入：清掉移动/跳跃/钩子/换武器，
// 保留瞄准方向（零向量时补一个合法方向），开火位图改为已释放。
inline void QmNeutralizePracticeInput(CNetObj_PlayerInput &Input)
{
	Input.m_Direction = 0;
	Input.m_Jump = 0;
	Input.m_Hook = 0;
	QmPracticeSuppressActionInput(Input, QmPracticeReleasedFireState(Input.m_Fire));
	if(Input.m_TargetX == 0 && Input.m_TargetY == 0)
	{
		Input.m_TargetX = 1;
		Input.m_TargetY = 0;
	}
}

// 练习录制的事件过滤：只保留 .qmevt 读取器认识、且播放侧当时确实会发声/画特效
// 的类型。世界音效与查看模式共用 QmShouldPlayWorldSound；地图世界音效与
// DispatchViewEvent 一样只看游戏音效总开关。
inline bool QmShouldRecordPracticeEvent(int EventId, int ExtraInfo, bool SndGame, bool PlayJumpSound,
	bool PlayDeathOrSpawnSound, bool RaceSounds, bool SndGun, bool SndLongPain)
{
	switch(EventId)
	{
	case NETEVENTTYPE_HAMMERHIT:
	case NETEVENTTYPE_EXPLOSION:
	case NETEVENTTYPE_DAMAGEIND:
		return true;
	case NETEVENTTYPE_SOUNDWORLD:
		return QmShouldPlayWorldSound(ExtraInfo, SndGame, PlayJumpSound, PlayDeathOrSpawnSound, RaceSounds, SndGun, SndLongPain);
	case NETEVENTTYPE_MAPSOUNDWORLD:
		return SndGame;
	default:
		return false;
	}
}

// 分身晚接入时，主角色必须继续沿用进入练习前的锁定输入；只有还没锁过的槽位
// （新接入的分身）才重新采样。按连接槽位对应的 client id 判断，而不是槽位号，
// 因为 cl_dummy 切换会改变哪个槽位控制哪个角色。
inline bool QmPracticeShouldCaptureServerLockedInput(bool Enabled, bool AlreadyLocked, int SlotClientId, int EnableLocalClientId)
{
	return !Enabled || !AlreadyLocked || SlotClientId != EnableLocalClientId;
}

inline bool QmIsPracticeParticipant(int ClientId, int EnableLocalClientId, int EnableDummyClientId)
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS &&
	       (ClientId == EnableLocalClientId || ClientId == EnableDummyClientId);
}

struct SQmPracticeEyeEmoteState
{
	int m_Emote = -1;
	int m_EndTick = -1;
};

inline bool QmPracticeEyeEmoteNameEquals(std::string_view Name, std::string_view Expected)
{
	if(Name.size() != Expected.size())
		return false;
	for(size_t Index = 0; Index < Name.size(); ++Index)
	{
		char Left = Name[Index];
		char Right = Expected[Index];
		if(Left >= 'A' && Left <= 'Z')
			Left = (char)(Left - 'A' + 'a');
		if(Right >= 'A' && Right <= 'Z')
			Right = (char)(Right - 'A' + 'a');
		if(Left != Right)
			return false;
	}
	return true;
}

// 与服务器 /emote 使用同一组名字；close 是 blink 的别名。
inline int QmParsePracticeEyeEmote(std::string_view Name)
{
	if(QmPracticeEyeEmoteNameEquals(Name, "normal"))
		return EMOTE_NORMAL;
	if(QmPracticeEyeEmoteNameEquals(Name, "pain"))
		return EMOTE_PAIN;
	if(QmPracticeEyeEmoteNameEquals(Name, "happy"))
		return EMOTE_HAPPY;
	if(QmPracticeEyeEmoteNameEquals(Name, "surprise"))
		return EMOTE_SURPRISE;
	if(QmPracticeEyeEmoteNameEquals(Name, "angry"))
		return EMOTE_ANGRY;
	if(QmPracticeEyeEmoteNameEquals(Name, "blink") || QmPracticeEyeEmoteNameEquals(Name, "close"))
		return EMOTE_BLINK;
	return -1;
}

inline bool QmPracticeEyeEmoteActive(const SQmPracticeEyeEmoteState &Override, int Tick)
{
	return Override.m_Emote >= 0 && Override.m_Emote < NUM_EMOTES && Override.m_EndTick >= Tick;
}

// 练习参与者的服务器输入被锁成中性，服务器会长期判它 AFK，并把快照表情固定成眨眼。
// 因此 AFK 或快照失活时改用本地默认眼睛；手动 /emote 在有效期内优先。
inline int QmResolvePracticeEyeEmote(int ServerEmote, bool ServerActive, bool ServerAfk, int ConfiguredEmote,
	const SQmPracticeEyeEmoteState &Override, int Tick)
{
	if(QmPracticeEyeEmoteActive(Override, Tick))
		return Override.m_Emote;
	if(ServerActive && !ServerAfk && ServerEmote >= 0 && ServerEmote < NUM_EMOTES)
		return ServerEmote;
	return std::clamp(ConfiguredEmote, 0, NUM_EMOTES - 1);
}

// 练习命令、检查点与死亡回退都作用在“当前控制的 tee”上：cl_dummy 切换后，
// 用户按的键属于另一条连接。控制连接不是参与者时退回另一条参与者连接；
// 两条都不是（旁观、掉线）返回 -1，由调用方决定是否回退到启用时的本体。
// 不能用启用时记录的 m_EnableLocalClientId 直接代替——分身接管时那会操作错人。
inline int QmResolveControlledPracticeClientId(const int aLocalIds[NUM_DUMMIES], bool ControlDummy, int EnableLocalClientId, int EnableDummyClientId)
{
	const int ActiveClientId = aLocalIds[ControlDummy ? 1 : 0];
	if(QmIsPracticeParticipant(ActiveClientId, EnableLocalClientId, EnableDummyClientId))
		return ActiveClientId;

	const int InactiveClientId = aLocalIds[ControlDummy ? 0 : 1];
	if(QmIsPracticeParticipant(InactiveClientId, EnableLocalClientId, EnableDummyClientId))
		return InactiveClientId;

	return -1;
}

// 服务器 HandleSkippableTiles：super / invincible 时死亡 tile 完全不生效（也不播死亡反馈）；
// 练习队伍里命中死亡 tile 只是 Freeze()。快速练习的分支世界把这个反馈改成
// “播一次死亡效果并回到救援点”，与 /r 的落点一致。
struct SQmPracticeDeathTileDecision
{
	bool m_PlayFeedback = false;
	bool m_ReturnToSavedPoint = false;
};

inline SQmPracticeDeathTileDecision QmEvaluatePracticeDeathTile(bool NewlyInDeath, bool InvincibleOrSuper, bool HasSavedPoint)
{
	SQmPracticeDeathTileDecision Decision;
	// 只在“刚踩进去”的那一 tick 触发：停在 tile 边缘时不能每 tick 都传送。
	if(!NewlyInDeath || InvincibleOrSuper)
		return Decision;
	Decision.m_PlayFeedback = true;
	Decision.m_ReturnToSavedPoint = HasSavedPoint;
	return Decision;
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

// 重新 fork 会把分支世界换回服务器世界的开关状态，练习里 /setswitch 的改动会因此
// 丢失。把旧分支的开关状态搬进新分支：定时开关的结束/更新时间按 tick 差平移，
// 剩余时长保持不变；无限期开关的 tick 是 0，不平移。开关数量对不上（换图）时整份放弃。
inline void QmCarryPracticeSwitchers(std::vector<SSwitchers> &vTarget, const std::vector<SSwitchers> &vSource, int TickDelta)
{
	if(vSource.size() != vTarget.size())
		return;
	for(size_t Index = 0; Index < vTarget.size(); Index++)
	{
		SSwitchers Carried = vSource[Index];
		for(int Team = 0; Team < NUM_DDRACE_TEAMS; Team++)
		{
			if(Carried.m_aEndTick[Team] > 0)
				Carried.m_aEndTick[Team] += TickDelta;
			if(Carried.m_aLastUpdateTick[Team] > 0)
				Carried.m_aLastUpdateTick[Team] += TickDelta;
		}
		vTarget[Index] = Carried;
	}
}

// 事件屏障：<= BarrierTick 的分支 tick 已经创建并消费过预测事件。
// 分支世界重新 fork（重进服务器、分身重连）会从服务器 tick 往回重算一段已经
// 播过的 tick；预测事件去重依赖世界里的队列记录，而重建世界会把记录换成
// 服务器世界那份旧快照，同一 tick 的开火声因此会被再排一份、再播一次。
// 屏障随分支时钟单调前进，重算范围内的 tick 直接关闭事件创建。
inline bool QmPracticeTickAllowsPredictedEvents(int Tick, int BarrierTick)
{
	return Tick > BarrierTick;
}

// 重新 fork 出来的是新的分支时间线，旧队列整份作废：带进新世界的事件要么
// 属于已经播过的时间线，要么带着对不上的 tick，统一丢掉最安全。
inline void QmResetPracticePredictedEvents(std::vector<CGameWorld::CPredictedEvent> &vEvents)
{
	vEvents.clear();
}

// 重开练习世界会连同快照初刻的事件队列一起回来。那些事件对应的 tick 早已
// 被屏障盖过，留着会在下一帧集中播放（连续开火声），必须按屏障摘掉。
inline void QmDropPracticePredictedEventsUntil(std::vector<CGameWorld::CPredictedEvent> &vEvents, int BarrierTick)
{
	std::erase_if(vEvents, [BarrierTick](const CGameWorld::CPredictedEvent &Event) { return Event.m_Tick <= BarrierTick; });
}

#endif
