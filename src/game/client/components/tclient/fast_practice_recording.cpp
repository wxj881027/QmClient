/* Copyright © 2026 BestProject Team */
// 快速练习的录制与保存：轨迹采样、交互事件/机关状态录制、/pg 落盘。
// 与预测循环、命令层分开，录制规则只在这里维护。

#include "fast_practice.h"

#include <base/system.h>

#include <game/client/components/ghost.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/practice_branch_state.h>
#include <game/client/components/qmclient/world_event_gate.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/gamecore.h>

#include <algorithm>

void CFastPractice::BeginPracticeTrack(int StartTick)
{
	// 多轨身份：本体轨恒为 0，分身存在时占轨 1。名字用于回放成员列表，
	// 与服务器快照里的名字同源（练习里没有别的玩家）。
	const int LocalId = m_EnableLocalClientId;
	const int DummyId = m_EnableDummyClientId;
	const char *pLocalName = LocalId >= 0 && LocalId < MAX_CLIENTS ? GameClient()->m_aClients[LocalId].m_aName : "";
	const char *pDummyName = DummyId >= 0 && DummyId < MAX_CLIENTS ? GameClient()->m_aClients[DummyId].m_aName : "";
	m_Recorder.Begin(GameClient()->m_LocalWorld, StartTick, LocalId, DummyId, pLocalName, pDummyName);
}

void CFastPractice::RecordPracticeFrames(CGameWorld &World, int Tick, int LocalClientId, int DummyClientId)
{
	if(!GameClient()->m_LocalWorld.TrackActive() || m_Recorder.Paused())
		return;

	const auto Record = [&](int TrackIndex, int ClientId) {
		if(ClientId < 0)
			return;
		CCharacter *pChar = World.GetCharacterById(ClientId);
		if(!pChar)
			return;

		// 采样源是分支世界的角色，不是服务器快照：练习中服务器侧的自己被锁了输入，
		// 一直停在开练习那一刻，用快照录出来的只会是一条静止的线。
		const CCharacterCore *pCore = pChar->Core();
		SLocalActorFrame Frame = {};
		Frame.m_Tick = Tick;
		Frame.m_X = (int)pCore->m_Pos.x;
		Frame.m_Y = (int)pCore->m_Pos.y;
		// 协议里速度是 1/256 单位，写回去再由 GetGhostCharacter 转成幽灵格式。
		Frame.m_VelX = (int)(pCore->m_Vel.x * 256.0f);
		Frame.m_VelY = (int)(pCore->m_Vel.y * 256.0f);
		Frame.m_Angle = pCore->m_Angle;
		Frame.m_Direction = pCore->m_Direction;
		Frame.m_Weapon = pCore->m_ActiveWeapon;
		Frame.m_HookState = pCore->m_HookState;
		Frame.m_HookX = (int)pCore->m_HookPos.x;
		Frame.m_HookY = (int)pCore->m_HookPos.y;
		Frame.m_AttackTick = pChar->GetAttackTick();
		// 分支世界没有服务器快照，冻结状态取预测角色自己的倒计时；
		// 保存幽灵时写入显式冻结标记，回放才有冻结条与冻结表现。
		Frame.m_FreezeTicks = pChar->m_FreezeTime;
		GameClient()->m_LocalWorld.AddTrackFrame(TrackIndex, Frame);
	};
	Record(0, LocalClientId);
	Record(1, DummyClientId);
}

void CFastPractice::RecordPracticeEvents(CGameWorld &World, int Tick)
{
	// 与 HandlePredictedEvents 同一门控：被压制的 tick 当时就没播特效/音效，
	// 回放里也不该凭空多出来。
	if(!GameClient()->m_LocalWorld.TrackActive() || m_Recorder.Paused() || GameClient()->m_SuppressEvents)
		return;

	// 分支世界里真实发生的交互（枪声/锤击音、锤击环、爆炸、伤害指示）要跟轨迹
	// 一起存，回放时查看模式才会像 demo 一样播特效/音效。只收 .qmevt 读取器认识
	// 且 HandlePredictedEvents 当时确实会播的类型；音效走与播放侧完全相同的门控，
	// 否则回看会冒出练习时根本没听见的声音。
	const SQmFocusModeDecisions Focus = GetQmFocusModeDecisions();
	for(const CGameWorld::CPredictedEvent &Event : World.m_PredictedEvents)
	{
		// m_Handled / m_ServerConfirmed 是主世界快照确认事件：属于另一条时间线，
		// 练习画面里不播，也绝不能录进这条分支的回放。
		if(Event.m_Tick != Tick || Event.m_Handled || Event.m_ServerConfirmed)
			continue;
		if(!QmShouldRecordPracticeEvent(Event.m_EventId, Event.m_ExtraInfo, g_Config.m_SndGame,
			   Focus.m_AirJump.m_PlaySound, Focus.m_PlayDeathOrSpawnSound,
			   GameClient()->m_GameInfo.m_RaceSounds, g_Config.m_SndGun, g_Config.m_SndLongPain))
			continue;
		SLocalActorEvent Local = {};
		Local.m_Tick = Tick;
		Local.m_Type = Event.m_EventId;
		Local.m_aData[0] = (int)Event.m_Pos.x;
		Local.m_aData[1] = (int)Event.m_Pos.y;
		Local.m_aData[2] = Event.m_ExtraInfo;
		GameClient()->m_LocalWorld.AddTrackEvent(Local);
	}
}

void CFastPractice::RecordPracticeSwitchState(CGameWorld &World, int Tick)
{
	if(!GameClient()->m_LocalWorld.TrackActive() || m_Recorder.Paused() || World.Switchers().empty())
		return;

	// 队伍与查看模式 ApplyViewSwitchState 用同一个来源，保证录进去的状态
	// 回放时能落回同一批 switcher 槽位。分支世界的开关由预测角色踩 tile
	// 更新（character.cpp HandleTiles）+ CGameWorld::Tick 处理定时到期，
	// 这里只做全量采样，去重由 CLocalWorld 负责。
	const int Team = std::clamp(GameClient()->SwitchStateTeam(), (int)TEAM_FLOCK, NUM_DDRACE_TEAMS - 1);
	GameClient()->m_LocalWorld.AddTrackSwitchState(QmEncodeLocalSwitchState(Tick, World.Switchers(), Team));
}

void CFastPractice::OnPracticePositionJump()
{
	// 位置突变既是录制分段点，也是计时起点：/r、传送与死亡回退之后
	// 玩家关心的是“这一段跑到这里用了多久”。
	m_PracticeRunTicks = 0;
	// 低频诊断点：位置突变必须切段，否则 /pg 出来的线路会带一道穿墙直线。
	dbg_msg("fast_practice", "segment boundary: frames=%d events=%d switches=%d",
		GameClient()->m_LocalWorld.CurrentFrameCount(), GameClient()->m_LocalWorld.CurrentEventCount(),
		GameClient()->m_LocalWorld.CurrentSwitchStateCount());
	m_Recorder.MarkBoundary(GameClient()->m_LocalWorld);
}

bool CFastPractice::SavePracticeGhost(const char *pName)
{
	SLocalSegment Segment;
	if(!GameClient()->m_LocalWorld.BestSegment(&Segment))
	{
		EchoPractice("no practice line to save yet (run for at least a second)");
		return false;
	}
	if(!GameClient()->m_Ghost.SavePracticeTrack(pName, Segment))
	{
		EchoPractice("could not save practice line (ghost recorder busy)");
		return false;
	}
	// 保存不等于播放：用户到影子页点 View 才进入 demo 式回放，不退出服务器。
	dbg_msg("fast_practice", "ghost saved: tracks=%d frames=%d events=%d switches=%d start_tick=%d",
		(int)Segment.m_vTracks.size(), (int)Segment.m_vTracks.front().m_vFrames.size(),
		(int)Segment.m_vEvents.size(), (int)Segment.m_vSwitchStates.size(), Segment.m_StartTick);
	GameClient()->m_Menus.GhostlistPopulate();
	EchoPractice("practice line saved (%d tracks, %d ticks); open the Ghost page and press View to replay it",
		(int)Segment.m_vTracks.size(), (int)Segment.m_vTracks.front().m_vFrames.size());
	return true;
}

void CFastPractice::StartPracticeRecording()
{
	if(!m_Enabled)
	{
		EchoPractice("practice recording: fast practice is not active");
		return;
	}
	if(m_Recorder.Active(GameClient()->m_LocalWorld))
	{
		if(!m_Recorder.Paused())
		{
			EchoPractice("practice recording is already running");
			return;
		}
		// 暂停后按开始 = 继续；Recorder 先切段，暂停期间的空洞不会连成一条直线。
		m_Recorder.Resume(GameClient()->m_LocalWorld);
		EchoPractice("practice recording resumed");
		return;
	}
	BeginPracticeTrack(m_PracticeBaseWorld.GameTick());
	EchoPractice("practice recording started");
}

void CFastPractice::PausePracticeRecording()
{
	if(!m_Enabled || !m_Recorder.Active(GameClient()->m_LocalWorld) || m_Recorder.Paused())
	{
		EchoPractice("practice recording is not running");
		return;
	}
	m_Recorder.Pause();
	// 暂停期间 tee 照常跑，采样必须停：继续录会把空洞连成一条直线，回放里是假轨迹。
	EchoPractice("practice recording paused (resuming starts a new take)");
}

void CFastPractice::StopPracticeRecording()
{
	if(!m_Enabled || !m_Recorder.Active(GameClient()->m_LocalWorld))
	{
		EchoPractice("practice recording is not running");
		return;
	}
	// 先落盘再结束：End 会清空轨迹缓冲，顺序反了就什么都存不下来。
	// 保存失败（线路太短或录音器忙）时不清空轨迹，暂停住让用户接着跑再结束。
	if(!SavePracticeGhost(""))
	{
		m_Recorder.Pause();
		return;
	}
	m_Recorder.End(GameClient()->m_LocalWorld);
	EchoPractice("practice recording stopped and saved");
}
