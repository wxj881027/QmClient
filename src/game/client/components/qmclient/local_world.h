#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_WORLD_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_LOCAL_WORLD_H

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/gamecore.h>

#include <algorithm>
#include <vector>

// 客户端侧权威世界里的一帧角色状态。
//
// 刻意不复用 CGhostCharacter：这一层是所有"没有服务器的世界"的公共父集，
// 输入流、地图状态、实体快照等字段以后都会加进来，不能反过来被某一种录像格式绑死。
// 反过来它也不绑 CGameWorld：这一层只存轨迹，不碰物理。
struct SLocalActorFrame
{
	int m_Tick = 0;
	int m_X = 0;
	int m_Y = 0;
	int m_VelX = 0;
	int m_VelY = 0;
	int m_Angle = 0;
	int m_Direction = 0;
	int m_Weapon = 0;
	int m_HookState = 0;
	int m_HookX = 0;
	int m_HookY = 0;
	int m_AttackTick = 0;
	// 冻结剩余 tick（0 = 未冻结）。只用于写入幽灵的显式冻结标记，
	// 不是协议字段，也不进入 QmLocalWorldFrameToNetObj。
	int m_FreezeTicks = 0;
};

// 录制帧 -> 协议角色对象。这是轨迹与录像格式之间唯一的数据边界。
// 速度单位是协议单位（下游按 /256 换算）；录制里的钩子只是视觉状态，
// 不建立真实挂接，也不留下会超时脱钩的 HookTick。
// 抽成纯函数是为了能直接对这条边界写行为测试。
inline CNetObj_Character QmLocalWorldFrameToNetObj(const SLocalActorFrame &Frame)
{
	CNetObj_Character Obj = {};
	Obj.m_Tick = Frame.m_Tick;
	Obj.m_X = Frame.m_X;
	Obj.m_Y = Frame.m_Y;
	Obj.m_VelX = Frame.m_VelX;
	Obj.m_VelY = Frame.m_VelY;
	Obj.m_Angle = Frame.m_Angle;
	Obj.m_Direction = Frame.m_Direction;
	Obj.m_Weapon = Frame.m_Weapon;
	Obj.m_HookState = Frame.m_HookState;
	Obj.m_HookX = Frame.m_HookX;
	Obj.m_HookY = Frame.m_HookY;
	Obj.m_HookTick = 0;
	Obj.m_HookedPlayer = -1;
	Obj.m_AttackTick = Frame.m_AttackTick;
	return Obj;
}

// 分支世界里发生的一次交互（开火、锤击、爆炸、伤害…）。
// 字段布局与查看模式 .qmevt 的 SViewEvent 一致，但公共层不引用查看模式的实现头，
// 免得把"客户端侧世界"绑死在 Rank 1 的文件格式上。
struct SLocalActorEvent
{
	int m_Tick = 0; // 分支世界绝对 tick
	int m_Type = 0; // NETEVENTTYPE_*
	int m_aData[3] = {}; // 位置 X / Y + 额外信息（SoundId、方向、Cid…）
};

// 分支世界的一次机关开关状态快照（与 .qmevt 的 SViewSwitchState 同布局）。
// 解密型地图靠它回看：门开没开、定时开关到没到期，回放时必须和当时一致。
struct SLocalSwitchState
{
	int m_Tick = 0;
	int m_HighestSwitchNumber = 0;
	int m_Team = 0;
	unsigned m_aStatus[8] = {};
};

// 把预测世界的 switcher 向量编码成时间线状态位。抽成纯函数便于行为测试与基准。
inline SLocalSwitchState QmEncodeLocalSwitchState(int Tick, const std::vector<SSwitchers> &vSwitchers, int Team)
{
	SLocalSwitchState State;
	State.m_Tick = Tick;
	State.m_HighestSwitchNumber = std::clamp((int)vSwitchers.size() - 1, 0, 255);
	const int SafeTeam = std::clamp(Team, 0, NUM_DDRACE_TEAMS - 1);
	State.m_Team = SafeTeam;
	for(int j = 0; j <= State.m_HighestSwitchNumber; j++)
	{
		if(vSwitchers[j].m_aStatus[SafeTeam])
			State.m_aStatus[j / 32] |= 1u << (j % 32);
	}
	return State;
}

// 一条录制轨：本体或分身各自一条，回放时是同组里并行的虚影。
struct SLocalTrack
{
	int m_ClientId = -1;
	char m_aName[MAX_NAME_LENGTH] = {};
	std::vector<SLocalActorFrame> m_vFrames;
};

// 一次可保存的连续线路。事件/开关 tick 已经重定基为相对段起点，可直接写 .qmevt。
struct SLocalSegment
{
	int m_StartTick = 0;
	std::vector<SLocalTrack> m_vTracks;
	std::vector<SLocalActorEvent> m_vEvents;
	std::vector<SLocalSwitchState> m_vSwitchStates;
};

// 客户端侧权威世界：快速练习、录像回放、以及以后任何"没有服务器"的场景共用。
//
// 它管的是**轨迹会话**，不是碰撞：Rank 1 幽灵来自另一个平行时空，只用来看，
// 绝不能和玩家勾、挡、吃机关；快速练习操控的是我们自己这条分叉时空里的 Tee。
// 所以这里只负责三件与"平行时空"有关的事：
//
//   ① 时间线：世界 tick 与回放播放头换算（暂停/冻结时不会分叉）；
//   ② 多轨采样：把分叉时空里每个 Tee 的每 tick 状态、以及世界里发生的交互
//      事件记成一段连续线路；
//   ③ 自动分段：/r、传送、死亡、重进练习都结束当前段、另起一段，
//      保证存下来的每一段都没有瞬移，可以直接当 demo 式回放对照。
//
// 世界树模型（用户约定）：
//   主服务器世界 m_GameWorld 永远独立推进；进练习 = fork 出 m_PracticeBaseWorld
//   这条分支，退出 = 销毁分支（CGameWorld::Clear），下次再进重新 fork。
//   本层只保存分支的轨迹会话，不持有世界实例；已落盘的 .gho 是独立文件，
//   分支销毁后依然存在。
class CLocalWorld
{
public:
	static constexpr int MAX_TRACKS = 2; // 本体 + 分身
	static constexpr int MIN_SEGMENT_FRAMES = 50; // 约 1 秒，短于这个不算一段线路

	void BeginTrack(int StartTick);
	void EndTrack();
	bool TrackActive() const { return m_TrackActive; }
	int TrackStartTick() const { return m_TrackStartTick; }

	// 绑定轨道身份（Cid 与回放时显示的名字）。必须在 AddTrackFrame 之前调用。
	void SetTrackIdentity(int TrackIndex, int ClientId, const char *pName);
	// 每 tick 追加一帧。帧的 m_Tick 必须是这条世界线的绝对 tick。
	void AddTrackFrame(int TrackIndex, const SLocalActorFrame &Frame);
	void AddTrackFrame(const SLocalActorFrame &Frame) { AddTrackFrame(0, Frame); }
	// 每 tick 追加本 tick 发生的交互事件；由查看模式按播放头重放特效/音效。
	void AddTrackEvent(const SLocalActorEvent &Event);
	// 每 tick 追加一次机关开关状态；内部去重，状态没变就不留记录。
	void AddTrackSwitchState(const SLocalSwitchState &State);

	// 位置突变（/r、/tc、/tp、死亡、重进练习）前调用：当前段封存为"上一段"，
	// 从下一帧开启新段。只保留最近一个完成段，练习里反复重试不会堆垃圾。
	void MarkSegmentBoundary();

	// 取出适合保存的一段：当前段够长就用当前段，否则用刚封存的那段。
	// 返回 false 表示还没有可保存的线路。
	bool BestSegment(SLocalSegment *pOut) const;
	// 只取主轨的便捷包装（单轨调用方与行为测试用）。
	bool BestSegment(std::vector<SLocalActorFrame> *pOut, int *pStartTick) const;
	bool HasCompletedSegment() const { return !m_aTracks[0].m_vCompleted.empty(); }

	// 世界被外部时钟平移时（服务器暂停恢复），绝对时间量要跟着走。
	void ShiftTrackTicks(int TickDelta);

	// 查看模式会话登记：Rank 1 与练习组回放都在这里挂号，父集因此知道
	// "当前有没有独立时间线在播、几条轨"。只记状态，播放仍由 CGhost /
	// CRankGhost 原路径负责，绝不改播放效果。
	void BeginViewSession(int SlotCount);
	void EndViewSession();
	bool ViewSessionActive() const { return m_ViewSessionActive; }
	int ViewSessionSlotCount() const { return m_ViewSessionSlotCount; }

	// 诊断用计数（qm_local_world_dump 与反馈闭环）：不参与任何行为判定。
	int CurrentFrameCount() const { return (int)m_aTracks[0].m_vFrames.size(); }
	int CurrentEventCount() const { return (int)m_vEvents.size(); }
	int CurrentSwitchStateCount() const { return (int)m_vSwitchStates.size(); }
	int CompletedFrameCount() const { return (int)m_aTracks[0].m_vCompleted.size(); }

private:
	struct STrackBuffer
	{
		int m_ClientId = -1;
		char m_aName[MAX_NAME_LENGTH] = {};
		std::vector<SLocalActorFrame> m_vFrames;
		std::vector<SLocalActorFrame> m_vCompleted;
	};

	bool m_TrackActive = false;
	int m_TrackStartTick = 0;
	int m_CompletedStartTick = 0;
	STrackBuffer m_aTracks[MAX_TRACKS];
	std::vector<SLocalActorEvent> m_vEvents;
	std::vector<SLocalActorEvent> m_vCompletedEvents;
	std::vector<SLocalSwitchState> m_vSwitchStates;
	std::vector<SLocalSwitchState> m_vCompletedSwitchStates;
	bool m_ViewSessionActive = false;
	int m_ViewSessionSlotCount = 0;
};

#endif
