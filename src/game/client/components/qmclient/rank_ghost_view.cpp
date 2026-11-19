/* Copyright © 2026 BestProject Team */
// Rank 1 查看模式：把已加载的影子按独立时间线播放，并驱动事件/消息/相机。
// 与清单拉取、demo 解析分开，查看状态只在这里维护。

#include "rank_demo_cache.h"
#include "rank_ghost.h"
#include "rank_view_timeline.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/client/ghost.h>
#include <engine/graphics.h>
#include <engine/message.h>
#include <engine/shared/protocol_ex.h>
#include <engine/shared/snapshot.h>
#include <engine/storage.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/QmUi/QmToast.h>
#include <game/client/components/effects.h>
#include <game/client/components/ghost.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/map_mechanism_builder.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/world_event_gate.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/gameworld.h>
#include <game/gamecore.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
	using qmclient::rank_demo::ListCacheFiles;
}

bool CRankGhost::ViewPracticeGroup(const char *pGroupDir)
{
	if(pGroupDir == nullptr || pGroupDir[0] == '\0' || !Storage()->FolderExists(pGroupDir, IStorage::TYPE_SAVE))
		return false;
	if(IsBusy())
	{
		Echo(Localize("Rank ghost: another task is already running"));
		return false;
	}
	// 练习回放接管影子：清掉跨图 Rank 的等待与待查看标记，
	// 否则目标地图加载后旧 retry 会替换本次练习回放。
	m_RetryLoadPending = false;
	m_RetryLoadDeadline = 0;
	m_RetryLoadNextAttempt = 0;
	m_PendingView = false;

	// 组目录 + ".gho"：ViewTimelinePath / LoadGhostTarget 都用"去掉 .gho 后缀 = 组目录"
	// 这一约定，练习回放因此直接复用 Rank 1 的组加载与 .qmevt 读取，不另写一套。
	str_format(m_aGhostStoragePath, sizeof(m_aGhostStoragePath), "%s.gho", pGroupDir);
	// 练习是客户端分叉世界，没有源 demo；元数据回退到 ManualEndTick 计算，
	// 进度条与虚影位置仍在同一时间基上。
	m_aDemoStoragePath[0] = '\0';
	m_PendingView = true;
	if(!LoadGhostGroup(pGroupDir))
	{
		m_PendingView = false;
		return false;
	}
	LoadViewTimeline();
	return IsViewModeActive();
}

void CRankGhost::EnterViewMode()
{
	if(m_vLoadedSlots.empty())
		return;
	GameClient()->m_Ghost.StartRenderManual(m_vLoadedSlots);
	if(!GameClient()->m_Ghost.ManualModeActive())
		return;
	m_ViewPlaybackMetadata = {};
	str_copy(m_aViewPlaybackFilename, m_aLoadedReplayFilename);
	// 练习 /pg 组没有源 demo，跳过元数据扫描；其余情况只读 tick 与标记，
	// 不播放快照，不改变在线服务器状态。
	CDemoPlayer Metadata(nullptr, nullptr, false);
	if(m_aViewPlaybackFilename[0] != '\0' && Metadata.Load(Storage(), nullptr, m_aViewPlaybackFilename, IStorage::TYPE_SAVE) != -1)
	{
		m_ViewPlaybackMetadata = *Metadata.BaseInfo();
		if(m_ViewPlaybackMetadata.m_NumTimelineMarkers >= 2)
			m_ViewPlaybackMetadata.m_FirstTick = m_ViewPlaybackMetadata.m_aTimelineMarkers[0];
		Metadata.Stop();
	}
	m_ViewPlaybackMetadata.m_FirstTick = maximum(0, m_ViewPlaybackMetadata.m_FirstTick);
	m_ViewPlaybackMetadata.m_LastTick = m_ViewPlaybackMetadata.m_FirstTick + GameClient()->m_Ghost.ManualEndTick();
	int Markers = 0;
	for(int i = 0; i < m_ViewPlaybackMetadata.m_NumTimelineMarkers; ++i)
	{
		const int Tick = m_ViewPlaybackMetadata.m_aTimelineMarkers[i];
		if(Tick >= m_ViewPlaybackMetadata.m_FirstTick && Tick <= m_ViewPlaybackMetadata.m_LastTick)
			m_ViewPlaybackMetadata.m_aTimelineMarkers[Markers++] = Tick;
	}
	m_ViewPlaybackMetadata.m_NumTimelineMarkers = Markers;
	m_ViewPlaybackMetadata.m_LiveDemo = false;
	m_ViewPlaybackMetadata.m_LivePlayback = false;
	m_ViewMembers.Reset(m_vLoadedSlots.size());
	++m_ViewGeneration;
	m_ViewMode = true;
	// 同一影子退出后再次进入时，播放头会从 0 重新开始；游标必须同步归零，
	// 否则在 tick 0 退出的边界场景会漏掉首帧的事件和全局消息。
	m_ViewLastTick = -1;
	m_ViewEventCursor = 0;
	m_ViewMessageCursor = 0;
	m_ViewAppliedSwitch = -1;
	// 回放开关只画在自己的画布上：服务器世界和练习分支世界是两条可玩时间线的
	// 权威状态，回放期间不允许改写它们，退出回放也就不需要还原。
	m_vViewSwitchers = GameClient()->m_GameWorld.m_Core.m_vSwitchers;
	m_ViewSwitchersActive = true;
	// 影子机关世界只保存地图几何和回放开关，不加入主世界 tick。
	// 它只在查看模式存在，因此退出时整棵释放即可，不需要触碰共享 CCollision。
	m_pViewMechanismWorld = std::make_unique<CGameWorld>();
	int NextMechanismEntityId = 600000;
	qm_map_mechanisms::Build(*m_pViewMechanismWorld, GameClient()->Collision(), NextMechanismEntityId,
		{.m_BuildPickups = false, .m_RegisterDoorCollision = false, .m_InheritSnapshotLights = false});
	m_pViewMechanismWorld->Switchers() = m_vViewSwitchers;
	qm_shadow_gun::Load(GameClient()->Collision(), m_vViewGuns);
	qm_shadow_gun::Reset(m_vViewGuns, m_vViewPlasmas);
	m_ViewGunTick = -1;
	// 只在父集登记会话状态；播放链（CGhost 独立时钟 + CRankGhost 事件派发）
	// 保持原样，Rank 1 与练习组回放的观感完全一致。
	GameClient()->m_LocalWorld.BeginViewSession((int)m_vLoadedSlots.size());
	GameClient()->m_Spectator.OnReset();
	// 快捷键与回放面板都要求菜单关闭：从“查看回放”按钮进来时菜单还开着，
	// 不收起会让玩家按了空格/方向键却毫无反应。
	GameClient()->m_Menus.SetActive(false);
	// 查看模式不跟跑，清掉对齐标记，退出后由下一次跑图重新对齐
	m_LastAlignedRaceTick = -1;
	// 首次进入必须给出可退出的指引：控制面板由 ESC 开关，退出回放要走游戏菜单（双击 ESC）
	const char *pHint = Localize("Ghost view mode: ESC toggles the control panel. Double-tap ESC to open the game menu (playback continues). Hold the spectate key for the member list.");
	Echo(pHint);
	GameClient()->m_QmHudNotifications.QueueEcho(pHint, g_Config.m_ClMessageClientColor);
}

bool CRankGhost::IsViewModeActive() const
{
	return m_ViewMode && !m_vLoadedSlots.empty();
}

bool CRankGhost::GetViewState(SViewState &Out) const
{
	Out = SViewState();
	if(!IsViewModeActive())
		return false;
	const int EndTick = GameClient()->m_Ghost.ManualEndTick();
	if(EndTick <= 0)
		return false;
	// 总时长必须与播放头同一时间基（EndTick 个 tick @ 当前客户端 tick 频率）。
	// manifest 里的成绩时间只覆盖 run 本身：回放文件往往还带起点前的等待段，
	// 甚至可能被最短轨迹截断，用它当总时长会让计时飞转、进度条与虚影位置对不上
	const float TickSpeed = maximum(1, Client()->GameTickSpeed());
	const float TotalSeconds = EndTick / TickSpeed;
	const int CurTick = std::clamp(GameClient()->m_Ghost.ManualPlaybackTick(), 0, EndTick);
	Out.m_Active = true;
	Out.m_Playing = GameClient()->m_Ghost.ManualPlaying();
	Out.m_TotalSeconds = TotalSeconds;
	Out.m_CurSeconds = TotalSeconds * CurTick / (float)EndTick;
	Out.m_Progress = CurTick / (float)EndTick;
	// Finish 事件是回放时间线中唯一可靠的终点信号。记录它的相对 tick，
	// 让原生 DDRace Finish HUD 可以使用独立播放时钟完成 4 秒停留 + 2 秒淡出。
	for(const SViewEvent &Event : m_vViewEvents)
	{
		if(Event.m_Type != NETEVENTTYPE_FINISH || Event.m_RelTick > CurTick)
			continue;
		Out.m_Finished = true;
		Out.m_FinishSeconds = TotalSeconds * Event.m_RelTick / (float)EndTick;
		break;
	}

	// 成绩类消息带有真实成绩与差值（快照事件里都没有），优先用它们覆盖上面的估算值。
	// 注意两条消息的时间单位不同：RACE_FINISH 是毫秒，RACE_TIME 的 m_Check 是厘秒。
	if(const SViewMessage *pFinish = FindLatestViewMessage(EViewMessageType::RACE_FINISH, CurTick))
	{
		Out.m_Finished = true;
		Out.m_FinishSeconds = pFinish->m_aData[1] / 1000.0f;
		if(pFinish->m_aData[2] != 0)
		{
			Out.m_HasFinishDiff = true;
			Out.m_FinishDiffSeconds = pFinish->m_aData[2] / 1000.0f;
		}
		Out.m_FinishIsRecord = pFinish->m_aData[3] != 0 || pFinish->m_aData[4] != 0;
	}
	if(const SViewMessage *pRaceTime = FindLatestViewMessage(EViewMessageType::RACE_TIME, CurTick))
	{
		if(pRaceTime->m_aData[2] != 0)
		{
			// 终点那一帧的服务端成绩：比 Finish 事件的估算精确
			Out.m_Finished = true;
			Out.m_FinishSeconds = pRaceTime->m_aData[0] / 1000.0f;
			if(pRaceTime->m_aData[1] != 0 && !Out.m_HasFinishDiff)
			{
				Out.m_HasFinishDiff = true;
				Out.m_FinishDiffSeconds = pRaceTime->m_aData[1] / 100.0f;
			}
		}
		else if(pRaceTime->m_aData[1] != 0)
		{
			// 检查点差值：与原生 HUD 的 m_TimeCpDiff 同一语义，附带播放头年龄
			Out.m_HasCpDiff = true;
			Out.m_CpDiffSeconds = pRaceTime->m_aData[1] / 100.0f;
			Out.m_CpDiffAgeSeconds = (CurTick - pRaceTime->m_RelTick) / TickSpeed;
		}
	}
	return true;
}

void CRankGhost::EndViewSessionState()
{
	m_ViewMode = false;
	m_ViewSwitchersActive = false;
	m_vViewSwitchers.clear();
	m_pViewMechanismWorld.reset();
	m_ViewSwitchTeam = -1;
	m_vViewGuns.clear();
	m_vViewPlasmas.clear();
	m_ViewGunTick = -1;
	GameClient()->m_MovingTilesBackground.ClearViewPlaybackTime();
	GameClient()->m_MovingTilesForeground.ClearViewPlaybackTime();
	m_ViewSelected = 0;
	m_ViewCameraMode = EViewCameraMode::MEMBER;
	m_ViewFreeCameraValid = false;
	m_ViewFreeCameraCenter = vec2(0.0f, 0.0f);
	GameClient()->m_LocalWorld.EndViewSession();
	GameClient()->m_Ghost.StopManual();
	GameClient()->m_Spectator.OnReset();
}

void CRankGhost::ViewStop()
{
	m_PendingView = false;
	const bool WasViewing = m_ViewMode || GameClient()->m_Ghost.ManualModeActive();
	EndViewSessionState();
	if(!WasViewing)
		return;
	m_ViewZoomPersonal = 0.0f;
	Echo(Localize("Ghost view mode off. The ghost follows your runs again."));
}

// ===== 查看模式时间线：录制 / 缓存 / 回放 =====

// 从解析中的快照收集交互事件与开关状态（run 区间内，tick 已转为相对值）
void CRankGhost::RecordViewTimeline(const CSnapshot *pSnapshot, int RelTick, int SwitchTeam)
{
	const int NumItems = pSnapshot->NumItems();
	for(int i = 0; i < NumItems; i++)
	{
		const int Type = pSnapshot->GetItemType(i);
		int NumInts = 0;
		// 事件对象都使用固定的 int 字段；这里按生成协议的实际布局拷贝，
		// 让终点、出生、死亡和地图世界音效也进入独立查看时间线。
		if(Type == NETEVENTTYPE_HAMMERHIT || Type == NETEVENTTYPE_EXPLOSION ||
			Type == NETEVENTTYPE_BIRTHDAY || Type == NETEVENTTYPE_FINISH ||
			Type == NETEVENTTYPE_SPAWN)
			NumInts = 2;
		else if(Type == NETEVENTTYPE_DAMAGEIND || Type == NETEVENTTYPE_DEATH ||
			Type == NETEVENTTYPE_SOUNDWORLD || Type == NETEVENTTYPE_MAPSOUNDWORLD)
			NumInts = 3;
		else if(Type != NETOBJTYPE_SWITCHSTATE)
			continue;

		const CSnapshotItem *pItem = pSnapshot->GetItem(i);
		const int ItemSize = pSnapshot->GetItemSize(i);

		if(Type == NETOBJTYPE_SWITCHSTATE)
		{
			// 服务端每份快照都全量下发，仅在状态变化时记录（32 字节状态位 = 8 个 int）
			if(ItemSize < 36)
				continue;
			const CNetObj_SwitchState *pState = (const CNetObj_SwitchState *)pItem->Data();
			SViewSwitchState Record;
			Record.m_RelTick = RelTick;
			Record.m_HighestSwitchNumber = std::clamp(pState->m_HighestSwitchNumber, 0, 255);
			// -1 表示这份 demo 没带队伍消息，回放时沿用查看者当前队伍。
			Record.m_Team = SwitchTeam >= TEAM_FLOCK && SwitchTeam < NUM_DDRACE_TEAMS ? SwitchTeam : -1;
			mem_zero(Record.m_aStatus, sizeof(Record.m_aStatus));
			const int MaxSwitch = minimum(Record.m_HighestSwitchNumber, (int)std::size(Record.m_aStatus) * 32 - 1);
			for(int j = 0; j <= MaxSwitch; j++)
			{
				if(((unsigned)pState->m_aStatus[j / 32] >> (j % 32)) & 1)
					Record.m_aStatus[j / 32] |= 1u << (j % 32);
			}
			if(!m_vViewSwitchStates.empty())
			{
				const SViewSwitchState &Last = m_vViewSwitchStates.back();
				if(Last.m_HighestSwitchNumber == Record.m_HighestSwitchNumber &&
					Last.m_Team == Record.m_Team &&
					mem_comp(Last.m_aStatus, Record.m_aStatus, sizeof(Record.m_aStatus)) == 0)
					continue;
			}
			m_vViewSwitchStates.push_back(Record);
			continue;
		}

		if(ItemSize < (int)(NumInts * sizeof(int)))
			continue;
		SViewEvent Event;
		Event.m_RelTick = RelTick;
		Event.m_Type = Type;
		mem_zero(Event.m_aData, sizeof(Event.m_aData));
		mem_copy(Event.m_aData, pItem->Data(), NumInts * sizeof(int));
		m_vViewEvents.push_back(Event);
	}
}

// sidecar 缓存路径：多轨组 <base>/events.qmevt，单轨迹 <base>.qmevt
bool CRankGhost::ViewTimelinePath(char *pBuf, size_t BufSize) const
{
	char aBase[IO_MAX_PATH_LENGTH];
	str_copy(aBase, m_aGhostStoragePath, sizeof(aBase));
	const size_t Len = str_length(aBase);
	if(Len > 4 && str_endswith(aBase, ".gho"))
		aBase[Len - 4] = '\0';

	char aGroupDir[IO_MAX_PATH_LENGTH];
	str_copy(aGroupDir, aBase, sizeof(aGroupDir));
	if(Storage()->FolderExists(aGroupDir, IStorage::TYPE_SAVE) && !ListCacheFiles(Storage(), aGroupDir, ".gho").empty())
	{
		str_format(pBuf, BufSize, "%s/events.qmevt", aGroupDir);
		return true;
	}
	str_format(pBuf, BufSize, "%s.qmevt", aBase);
	return false;
}

bool CRankGhost::ViewTimelineCached() const
{
	char aPath[IO_MAX_PATH_LENGTH];
	ViewTimelinePath(aPath, sizeof(aPath));
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(File == nullptr)
		return false;
	// 截断文件即使保留 magic 也不能算有效缓存；完整读取一次才能触发重建。
	qmclient::rank_ghost::SViewTimeline Timeline;
	const bool Current = qmclient::rank_ghost::ReadViewTimeline(File, Timeline);
	io_close(File);
	// 旧版本 sidecar（例如没有消息段的 QMGHEVT1）按未缓存处理，
	// 由补齐流程重放一次回放重建，避免查看模式永远缺终点成绩与全局音效。
	return Current;
}

void CRankGhost::WriteViewTimeline()
{
	if(m_vViewEvents.empty() && m_vViewSwitchStates.empty() && m_vViewMessages.empty())
		return;
	char aPath[IO_MAX_PATH_LENGTH];
	ViewTimelinePath(aPath, sizeof(aPath));
	char aTmpPath[IO_MAX_PATH_LENGTH];
	str_format(aTmpPath, sizeof(aTmpPath), "%s.tmp_%d", aPath, pid());
	Storage()->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
	IOHANDLE File = Storage()->OpenFile(aTmpPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File == nullptr)
	{
		log_error("rank_ghost", "failed to open view timeline '%s'", aTmpPath);
		return;
	}

	const bool Written = qmclient::rank_ghost::WriteViewTimeline(File, m_vViewEvents, m_vViewSwitchStates, m_vViewMessages);
	const bool Closed = io_close(File) == 0;
	if(!Written || !Closed)
	{
		log_error("rank_ghost", "failed to write view timeline '%s'", aTmpPath);
		Storage()->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
		return;
	}
	// RenameFile 在 Windows 上带替换语义；先删旧文件会在改名失败时丢掉可用时间线。
	if(!Storage()->RenameFile(aTmpPath, aPath, IStorage::TYPE_SAVE))
	{
		log_error("rank_ghost", "failed to publish view timeline '%s'", aPath);
		Storage()->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
		return;
	}
	log_info("rank_ghost", "view timeline written: %d events, %d switch states, %d messages ('%s')",
		(int)m_vViewEvents.size(), (int)m_vViewSwitchStates.size(), (int)m_vViewMessages.size(), aPath);
}

bool CRankGhost::LoadViewTimeline()
{
	m_vViewEvents.clear();
	m_vViewSwitchStates.clear();
	m_vViewMessages.clear();
	m_ViewEventCursor = 0;
	m_ViewMessageCursor = 0;
	m_ViewAppliedSwitch = -1;
	m_ViewLastTick = -1;

	char aPath[IO_MAX_PATH_LENGTH];
	ViewTimelinePath(aPath, sizeof(aPath));
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(File == nullptr)
		return false;

	qmclient::rank_ghost::SViewTimeline Timeline;
	const bool Ok = qmclient::rank_ghost::ReadViewTimeline(File, Timeline);
	io_close(File);
	if(!Ok)
	{
		log_error("rank_ghost", "failed to read view timeline '%s'", aPath);
		return false;
	}
	m_vViewEvents = std::move(Timeline.m_vEvents);
	m_vViewSwitchStates = std::move(Timeline.m_vSwitchStates);
	m_vViewMessages = std::move(Timeline.m_vMessages);
	// 旧缓存没有 teams.qmtm：开关时间线里的队伍是同一场回放的主队伍，
	// 可作为同组影子的只读显示兜底；不影响任何判定或相机。
	if(!m_vViewSwitchStates.empty())
	{
		const int FallbackTeam = m_vViewSwitchStates.front().m_Team;
		if(FallbackTeam >= TEAM_FLOCK && FallbackTeam < NUM_DDRACE_TEAMS)
			for(int &Team : m_vLoadedGhostTeams)
				if(Team < TEAM_FLOCK)
					Team = FallbackTeam;
	}
	log_info("rank_ghost", "view timeline loaded: %d events, %d switch states, %d messages ('%s')",
		(int)m_vViewEvents.size(), (int)m_vViewSwitchStates.size(), (int)m_vViewMessages.size(), aPath);
	return true;
}

void CRankGhost::KickViewTimelineRebuild()
{
	if(m_Stage != EStage::IDLE || m_StartPending)
		return;
	if(!Storage()->FileExists(m_aDemoStoragePath, IStorage::TYPE_SAVE))
		return;
	log_info("rank_ghost", "view timeline missing, rebuilding from the cached replay");
	m_EventRebuildOnly = true;
	StartParse();
}

// 查看模式每帧：按播放头派发交互事件、应用开关状态
void CRankGhost::UpdateViewTimeline()
{
	CGhost *pGhost = &GameClient()->m_Ghost;
	const int CurTick = pGhost->ManualPlaybackTick();
	// 移动方块的包络按回放播放头求值，暂停时随播放头冻结。
	GameClient()->m_MovingTilesBackground.SetViewPlaybackTime(CurTick, pGhost->ManualRenderIntra());
	GameClient()->m_MovingTilesForeground.SetViewPlaybackTime(CurTick, pGhost->ManualRenderIntra());
	if(CurTick < m_ViewLastTick)
	{
		// 播放头后退（拖进度条）：重置游标，重放这段的特效与状态
		m_ViewLastTick = CurTick;
		m_ViewAppliedSwitch = -1;
		m_ViewEventCursor = std::lower_bound(m_vViewEvents.begin(), m_vViewEvents.end(), CurTick,
					    [](const SViewEvent &Event, int Tick) { return Event.m_RelTick < Tick; }) -
				    m_vViewEvents.begin();
		m_ViewMessageCursor = std::lower_bound(m_vViewMessages.begin(), m_vViewMessages.end(), CurTick,
					      [](const SViewMessage &Message, int Tick) { return Message.m_RelTick < Tick; }) -
				      m_vViewMessages.begin();
	}
	m_ViewLastTick = CurTick;

	// 事件派发限流：大幅快进时按帧铺开，避免单帧粒子爆炸。但跨越很久的快进里，
	// 积压事件的位置早已与当前画面无关，补播只会得到一串错位的特效/音效；
	// 因此只播最近一个游戏秒内的事件，更老的事件只前进游标、不派发。
	const int MaxEventLagTicks = maximum(1, Client()->GameTickSpeed());
	int Budget = 96;
	while(m_ViewEventCursor < m_vViewEvents.size() && Budget-- > 0)
	{
		const SViewEvent &Event = m_vViewEvents[m_ViewEventCursor];
		if(Event.m_RelTick > CurTick)
			break;
		if(qmclient::rank_ghost::ViewEventFresh(Event.m_RelTick, CurTick, MaxEventLagTicks))
			DispatchViewEvent(Event);
		m_ViewEventCursor++;
	}

	// 消息时间线：只有全局音效是"播一次"的动作，成绩类消息由 GetViewState 按
	// 播放头查询，不需要游标状态
	int MessageBudget = 96;
	while(m_ViewMessageCursor < m_vViewMessages.size() && MessageBudget-- > 0)
	{
		const SViewMessage &Message = m_vViewMessages[m_ViewMessageCursor];
		if(Message.m_RelTick > CurTick)
			break;
		if((Message.m_Type == EViewMessageType::SOUND_GLOBAL || Message.m_Type == EViewMessageType::MAP_SOUND_GLOBAL) &&
			qmclient::rank_ghost::ViewEventFresh(Message.m_RelTick, CurTick, MaxEventLagTicks))
			DispatchViewMessage(Message);
		m_ViewMessageCursor++;
	}

	// 开关状态：应用到渲染所用的队伍槽位；服务端每份快照都会重发真实状态，
	// 退出查看模式后下一次快照即自动恢复，无需备份
	if(!m_vViewSwitchStates.empty())
	{
		if(m_ViewAppliedSwitch < 0)
		{
			auto It = std::upper_bound(m_vViewSwitchStates.begin(), m_vViewSwitchStates.end(), CurTick,
				[](int Tick, const SViewSwitchState &State) { return Tick < State.m_RelTick; });
			m_ViewAppliedSwitch = (int)(It - m_vViewSwitchStates.begin()) - 1;
			if(m_ViewAppliedSwitch >= 0)
				ApplyViewSwitchState(m_vViewSwitchStates[m_ViewAppliedSwitch]);
		}
		else
		{
			while(m_ViewAppliedSwitch + 1 < (int)m_vViewSwitchStates.size() &&
				m_vViewSwitchStates[m_ViewAppliedSwitch + 1].m_RelTick <= CurTick)
			{
				m_ViewAppliedSwitch++;
				ApplyViewSwitchState(m_vViewSwitchStates[m_ViewAppliedSwitch]);
			}
		}
	}

	if(m_pViewMechanismWorld != nullptr)
	{
		m_pViewMechanismWorld->m_GameTick = std::max(0, CurTick);
		qm_map_mechanisms::AdvanceLights(*m_pViewMechanismWorld, CurTick);
	}

	if(!m_vViewGuns.empty())
	{
		if(CurTick < m_ViewGunTick)
		{
			// 拖动回退：机关炮从零点重放，冷却与在飞等离子球不能沿用未来状态。
			qm_shadow_gun::Reset(m_vViewGuns, m_vViewPlasmas);
			m_ViewGunTick = -1;
		}
		const auto TargetProvider = [&](int Tick, std::vector<qm_shadow_gun::STarget> &vTargets) {
			vTargets.clear();
			for(size_t i = 0; i < m_vLoadedSlots.size(); ++i)
			{
				vec2 Pos;
				if(!pGhost->GetManualRenderPosAtTick(m_vLoadedSlots[i], Tick, &Pos))
					continue;
				const int Team = i < m_vLoadedGhostTeams.size() ? m_vLoadedGhostTeams[i] : -1;
				vTargets.push_back({Pos, (int)i, Team});
			}
		};
		// 首次进入从零点开始；单次跳转最多追赶一分钟，超出则只重放最近一段，
		// 避免在超长回放上拖进度条时阻塞渲染线程。
		// 一屏内只需要覆盖等离子寿命与最近几次射击的冷却，500 tick 足够。
		constexpr int MAX_CATCHUP_TICKS = 10 * SERVER_TICK_SPEED;
		if(m_ViewGunTick < 0)
			m_ViewGunTick = std::max(0, CurTick - MAX_CATCHUP_TICKS);
		if(CurTick - m_ViewGunTick > MAX_CATCHUP_TICKS)
		{
			qm_shadow_gun::Reset(m_vViewGuns, m_vViewPlasmas);
			m_ViewGunTick = CurTick - MAX_CATCHUP_TICKS;
		}
		while(m_ViewGunTick < CurTick)
		{
			++m_ViewGunTick;
			qm_shadow_gun::Tick(m_vViewGuns, m_vViewPlasmas, m_ViewGunTick, m_vViewSwitchers,
				m_ViewSwitchTeam >= 0 ? m_ViewSwitchTeam : GameClient()->SwitchStateTeam(),
				GameClient()->Collision(), TargetProvider);
		}
	}
}

void CRankGhost::DispatchViewEvent(const SViewEvent &Event)
{
	// 默认不把影子的命中/爆炸/终点特效叠到玩家正在操作的主世界。
	if(!QmShouldPlayGhostViewFeedback(g_Config.m_QmGhostViewFeedback, Config()->m_SndGame))
		return;
	CGameClient *pGameClient = GameClient();
	const vec2 Pos(Event.m_aData[0], Event.m_aData[1]);
	switch(Event.m_Type)
	{
	case NETEVENTTYPE_HAMMERHIT:
		pGameClient->m_Effects.HammerHit(Pos, 1.0f, 1.0f);
		break;
	case NETEVENTTYPE_EXPLOSION:
		pGameClient->m_Effects.Explosion(Pos, 1.0f);
		break;
	case NETEVENTTYPE_BIRTHDAY:
	case NETEVENTTYPE_FINISH:
		// 与 CGameClient 的快照事件渲染保持一致：终点特效就是 confetti。
		pGameClient->m_Effects.Confetti(Pos, 1.0f);
		break;
	case NETEVENTTYPE_SPAWN:
		pGameClient->m_Effects.PlayerSpawn(Pos, 1.0f, 1.0f);
		break;
	case NETEVENTTYPE_DEATH:
		pGameClient->m_Effects.PlayerDeath(Pos, Event.m_aData[2], 1.0f);
		break;
	case NETEVENTTYPE_DAMAGEIND:
		pGameClient->m_Effects.DamageIndicator(Pos, direction(Event.m_aData[2] / 256.0f), 1.0f);
		break;
	case NETEVENTTYPE_SOUNDWORLD:
	{
		// 普通世界音效走游戏音效；地图世界音效必须走地图音效容器，
		// 否则机关音效的地图专属资源与音量语义会丢失。
		// 与预测侧播放、练习录制共用同一份门控（world_event_gate.h）：
		// 关闭游戏音效、专注模式静音跳跃/死亡音、赛道音效的枪声/长痛音开关。
		// 三处判定不一致会让同一条线路在游玩/练习/回放进播出不同的声音。
		const int SoundId = Event.m_aData[2];
		const SQmFocusModeDecisions Focus = GetQmFocusModeDecisions();
		if(!QmShouldPlayWorldSound(SoundId, Config()->m_SndGame, Focus.m_AirJump.m_PlaySound,
			   Focus.m_PlayDeathOrSpawnSound, pGameClient->m_GameInfo.m_RaceSounds,
			   g_Config.m_SndGun, g_Config.m_SndLongPain))
			break;
		pGameClient->m_Sounds.PlayAt(CSounds::CHN_WORLD, SoundId, 1.0f, Pos);
		break;
	}
	case NETEVENTTYPE_MAPSOUNDWORLD:
		if(Config()->m_SndGame)
			pGameClient->m_MapSounds.PlayAt(CSounds::CHN_WORLD, Event.m_aData[2], Pos);
		break;
	default:
		break;
	}
}

void CRankGhost::DispatchViewMessage(const SViewMessage &Message)
{
	// 只有全局音效需要在这里派发：成绩类消息由 GetViewState 按播放头查询。
	// 门控与 CGameClient::OnMessage 的同名消息保持一致（关闭游戏音效即静音）。
	CGameClient *pGameClient = GameClient();
	if(!QmShouldPlayGhostViewFeedback(g_Config.m_QmGhostViewFeedback, Config()->m_SndGame))
		return;
	if(Message.m_Type == EViewMessageType::SOUND_GLOBAL)
	{
		const int SoundId = Message.m_aData[0];
		// CTF 类全局音效在线路径走 Enqueue（避免同 tick 重复触发），其余直接播
		if(SoundId == SOUND_CTF_DROP || SoundId == SOUND_CTF_RETURN || SoundId == SOUND_CTF_CAPTURE ||
			SoundId == SOUND_CTF_GRAB_EN || SoundId == SOUND_CTF_GRAB_PL)
			pGameClient->m_Sounds.Enqueue(CSounds::CHN_GLOBAL, SoundId);
		else
			pGameClient->m_Sounds.Play(CSounds::CHN_GLOBAL, SoundId, 1.0f);
	}
	else if(Message.m_Type == EViewMessageType::MAP_SOUND_GLOBAL)
	{
		pGameClient->m_MapSounds.Play(CSounds::CHN_GLOBAL, Message.m_aData[0]);
	}
}

void CRankGhost::ApplyViewSwitchState(const SViewSwitchState &State)
{
	// 只改回放自己的开关画布。写 GameClient()->PlayableSwitchers() 会在练习开启时
	// 落到分支世界的权威开关上，退出回放后 /setswitch 与地图开关会被回放状态顶掉。
	std::vector<SSwitchers> *pViewSwitchers = ViewSwitchers();
	if(pViewSwitchers == nullptr || pViewSwitchers->empty())
		return;
	auto &vSwitchers = *pViewSwitchers;
	// 旧 QMGHEVT2 没有录制队伍，沿用查看者当前队伍。
	const int Team = qmclient::rank_ghost::ResolveViewSwitchTeam(State.m_Team, GameClient()->SwitchStateTeam());
	m_ViewSwitchTeam = Team;
	const int Count = minimum(std::clamp(State.m_HighestSwitchNumber, 0, 255) + 1, (int)vSwitchers.size());
	for(int j = 0; j < Count; j++)
	{
		const bool Status = (State.m_aStatus[j / 32] >> (j % 32)) & 1;
		vSwitchers[j].m_aStatus[Team] = Status;
		// demo 里的 endtick 是绝对 tick，放到本地时间轴会瞬间过期；
		// 查看期间按稳定状态处理，定时开关的回关由后续状态变化记录覆盖
		vSwitchers[j].m_aEndTick[Team] = 0;
		vSwitchers[j].m_aType[Team] = Status ? TILE_SWITCHOPEN : TILE_SWITCHCLOSE;
	}
	if(m_pViewMechanismWorld != nullptr)
		m_pViewMechanismWorld->Switchers() = vSwitchers;
}

CGameWorld *CRankGhost::ViewMechanismWorld() const
{
	return IsViewModeActive() ? m_pViewMechanismWorld.get() : nullptr;
}

bool CRankGhost::ViewDraggerTarget(vec2 Pos, vec2 *pTarget, bool IgnoreWalls) const
{
	if(pTarget == nullptr || !IsViewModeActive())
		return false;
	float BestDistance = (float)g_Config.m_SvDraggerRange;
	bool Found = false;
	for(size_t i = 0; i < m_vLoadedSlots.size(); ++i)
	{
		if(!m_ViewMembers.Selected(i))
			continue;
		vec2 MemberPos;
		if(!GameClient()->m_Ghost.GetManualRenderPos(m_vLoadedSlots[i], &MemberPos))
			continue;
		const float Distance = distance(Pos, MemberPos);
		if(Distance > BestDistance)
			continue;
		// 机关按服务器同一规则检查视线；影子世界只读，不会因此改变任何实体。
		const bool Blocked = IgnoreWalls ?
					     GameClient()->Collision()->IntersectNoLaserNoWalls(Pos, MemberPos, nullptr, nullptr) :
					     GameClient()->Collision()->IntersectNoLaser(Pos, MemberPos, nullptr, nullptr);
		if(Blocked)
			continue;
		BestDistance = Distance;
		*pTarget = MemberPos;
		Found = true;
	}
	return Found;
}

void CRankGhost::ViewSelectMember(int Index)
{
	if(!IsViewModeActive() || m_vLoadedSlots.empty())
	{
		m_ViewSelected = 0;
		return;
	}
	m_ViewSelected = std::clamp(Index, 0, (int)m_vLoadedSlots.size() - 1);
	// 点选成员即恢复跟随视角
	m_ViewCameraMode = EViewCameraMode::MEMBER;
	GameClient()->m_Ghost.ManualSetFocusSlot(m_vLoadedSlots[m_ViewSelected]);
}

void CRankGhost::ViewSetCameraMode(EViewCameraMode Mode)
{
	m_ViewCameraMode = Mode;
	if(Mode == EViewCameraMode::FREE)
	{
		// 进入自由视角时锚定当前镜头位置，之后由鼠标平移
		m_ViewFreeCameraCenter = GameClient()->m_Camera.m_Center;
		m_ViewFreeCameraValid = true;
	}
	else
	{
		m_ViewFreeCameraValid = false;
	}
}

// 多人同框的用户倍率：在自动取景之上叠加，使 zoom+/- 仍然可调（正=放大）
void CRankGhost::ViewAdjustZoomPersonal(float DeltaSteps)
{
	ViewSetZoomPersonal(m_ViewZoomPersonal + DeltaSteps);
}

void CRankGhost::ViewSetZoomPersonal(float Steps)
{
	m_ViewZoomPersonal = std::clamp(Steps, -VIEW_ZOOM_PERSONAL_LIMIT, VIEW_ZOOM_PERSONAL_LIMIT);
}

bool CRankGhost::ViewFreeCameraCenter(vec2 *pOut) const
{
	if(m_ViewCameraMode != EViewCameraMode::FREE || !m_ViewFreeCameraValid)
		return false;
	*pOut = m_ViewFreeCameraCenter;
	return true;
}

void CRankGhost::ViewFreeCameraPan(float Dx, float Dy)
{
	if(m_ViewCameraMode != EViewCameraMode::FREE || !m_ViewFreeCameraValid)
		return;
	// Dx/Dy 是原始鼠标增量（窗口像素，未经 UI/菜单灵敏度换算）：按当前缩放 1:1 映射成
	// 世界位移，也就是"世界跟着光标走"的手感。不能用 Ui()->ConvertMouseMove 之后再算——
	// 那套走的是菜单灵敏度（默认 ui_mousesens=200，等于双倍），会明显过快。
	float WorldWidth, WorldHeight;
	Graphics()->CalcScreenParams(Graphics()->GameScreenAspect(), GameClient()->m_Camera.m_Zoom, &WorldWidth, &WorldHeight);
	const float WindowWidth = maximum(1.0f, (float)Graphics()->WindowWidth());
	m_ViewFreeCameraCenter += vec2(Dx, Dy) * (WorldWidth / WindowWidth);
}

bool CRankGhost::ViewFocusAllMembers(vec2 *pCenter, vec2 *pSize) const
{
	if(!IsViewModeActive() || m_vLoadedSlots.empty())
		return false;
	bool HasAny = false;
	vec2 Min(0.0f, 0.0f), Max(0.0f, 0.0f);
	for(size_t i = 0; i < m_vLoadedSlots.size(); i++)
	{
		if(!m_ViewMembers.Selected(i))
			continue;
		vec2 Pos;
		if(!GameClient()->m_Ghost.GetManualRenderPos(m_vLoadedSlots[i], &Pos))
			continue;
		if(!HasAny)
		{
			Min = Pos;
			Max = Pos;
			HasAny = true;
			continue;
		}
		Min = vec2(minimum(Min.x, Pos.x), minimum(Min.y, Pos.y));
		Max = vec2(maximum(Max.x, Pos.x), maximum(Max.y, Pos.y));
	}
	if(!HasAny)
		return false;
	*pCenter = (Min + Max) * 0.5f;
	*pSize = Max - Min;
	return true;
}

bool CRankGhost::ViewMemberName(int Index, char *pBuf, size_t BufSize) const
{
	if(Index < 0 || Index >= (int)m_vLoadedSlots.size())
		return false;
	return GameClient()->m_Ghost.GetGhostPlayer(m_vLoadedSlots[Index], pBuf, BufSize);
}

bool CRankGhost::ViewMemberTeam(int Index, int *pTeam) const
{
	if(pTeam == nullptr || Index < 0 || Index >= (int)m_vLoadedGhostTeams.size())
		return false;
	const int Team = m_vLoadedGhostTeams[Index];
	if(Team < TEAM_FLOCK || Team >= NUM_DDRACE_TEAMS)
		return false;
	*pTeam = Team;
	return true;
}

bool CRankGhost::ViewFocus(vec2 *pOut) const
{
	if(!IsViewModeActive() || m_ViewCameraMode != EViewCameraMode::MEMBER || m_vLoadedSlots.empty())
		return false;
	const int Idx = std::clamp(m_ViewSelected, 0, (int)m_vLoadedSlots.size() - 1);
	return GameClient()->m_Ghost.GetManualRenderPos(m_vLoadedSlots[Idx], pOut);
}
