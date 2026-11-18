/* (c) Rajh, Redix and Sushi. */

#include "ghost.h"

#include <base/log.h>
#include <base/mem.h>

#include <engine/demo.h>
#include <engine/ghost.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <game/client/components/menus.h>
#include <game/client/components/players.h>
#include <game/client/components/qmclient/demo_display.h>
#include <game/client/components/qmclient/ghost_start_detection.h>
#include <game/client/components/qmclient/ghost_team_metadata.h>
#include <game/client/components/qmclient/practice_timeline.h>
#include <game/client/components/qmclient/rank_view_timeline.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/fast_practice.h>
#include <game/client/gameclient.h>
#include <game/client/race.h>

#include <algorithm>

const char *CGhost::ms_pGhostDir = "ghosts";

static const LOG_COLOR LOG_COLOR_GHOST{165, 153, 153};

void CGhost::SetGhostSkinData(CGhostSkin *pSkin, const char *pSkinName, int UseCustomColor, int ColorBody, int ColorFeet)
{
	StrToInts(pSkin->m_aSkin, std::size(pSkin->m_aSkin), pSkinName);
	pSkin->m_UseCustomColor = UseCustomColor;
	pSkin->m_ColorBody = ColorBody;
	pSkin->m_ColorFeet = ColorFeet;
}

void CGhost::GetGhostCharacter(CGhostCharacter *pGhostChar, const CNetObj_Character *pChar, const CNetObj_DDNetCharacter *pDDnetChar)
{
	pGhostChar->m_X = pChar->m_X;
	pGhostChar->m_Y = pChar->m_Y;
	pGhostChar->m_VelX = pChar->m_VelX;
	// m_VelY 不参与回放插值，改作显式冻结标记（见 ghost_freeze_bar.h）：
	// 旧版文件没有标记位，播放侧才回退到“武器=忍者”的历史判定。
	pGhostChar->m_VelY = QmEncodeGhostFreezeMarker(pDDnetChar != nullptr ? QmGhostFreezeTicksFromEnd(pDDnetChar->m_FreezeEnd, pChar->m_Tick) : 0);
	pGhostChar->m_Angle = pChar->m_Angle;
	pGhostChar->m_Direction = QmEncodeGhostDirection(pChar->m_Direction, pDDnetChar != nullptr ? pDDnetChar->m_StrongWeakId : 0);
	pGhostChar->m_Weapon = pChar->m_Weapon;
	pGhostChar->m_HookState = pChar->m_HookState;
	pGhostChar->m_HookX = pChar->m_HookX;
	pGhostChar->m_HookY = pChar->m_HookY;
	pGhostChar->m_AttackTick = pChar->m_AttackTick;
	pGhostChar->m_Tick = pChar->m_Tick;
}

void CGhost::GetNetObjCharacter(CNetObj_Character *pChar, const CGhostCharacter *pGhostChar)
{
	mem_zero(pChar, sizeof(CNetObj_Character));
	pChar->m_X = pGhostChar->m_X;
	pChar->m_Y = pGhostChar->m_Y;
	pChar->m_VelX = pGhostChar->m_VelX;
	pChar->m_VelY = 0;
	pChar->m_Angle = pGhostChar->m_Angle;
	pChar->m_Direction = QmDecodeGhostDirection(pGhostChar->m_Direction);
	pChar->m_Weapon = pGhostChar->m_Weapon;
	pChar->m_HookState = pGhostChar->m_HookState;
	pChar->m_HookX = pGhostChar->m_HookX;
	pChar->m_HookY = pGhostChar->m_HookY;
	pChar->m_AttackTick = pGhostChar->m_AttackTick;
	pChar->m_HookedPlayer = -1;
	pChar->m_Tick = pGhostChar->m_Tick;
}

void CGhost::GetPath(char *pBuf, int Size, const char *pPlayerName, int Time) const
{
	const char *pMap = Client()->GetCurrentMap();
	SHA256_DIGEST Sha256 = GameClient()->Map()->Sha256();
	char aSha256[SHA256_MAXSTRSIZE];
	sha256_str(Sha256, aSha256, sizeof(aSha256));

	char aPlayerName[MAX_NAME_LENGTH];
	str_copy(aPlayerName, pPlayerName);
	str_sanitize_filename(aPlayerName);

	char aTimestamp[32];
	str_timestamp_format(aTimestamp, sizeof(aTimestamp), FORMAT_NOSPACE);

	if(Time < 0)
		str_format(pBuf, Size, "%s/%s_%s_%s_tmp_%d.gho", ms_pGhostDir, pMap, aPlayerName, aSha256, pid());
	else
		str_format(pBuf, Size, "%s/%s_%s_%d.%03d_%s_%s.gho", ms_pGhostDir, pMap, aPlayerName, Time / 1000, Time % 1000, aTimestamp, aSha256);
}

void CGhost::AddInfos(const CNetObj_Character *pChar, const CNetObj_DDNetCharacter *pDDnetChar)
{
	int NumTicks = m_CurGhost.m_Path.Size();

	// do not start writing to file as long as we still touch the start line
	if(g_Config.m_ClRaceSaveGhost && !GhostRecorder()->IsRecording() && NumTicks > 0)
	{
		GetPath(m_aTmpFilename, sizeof(m_aTmpFilename), m_CurGhost.m_aPlayer);
		if(GhostRecorder()->Start(m_aTmpFilename, Client()->GetCurrentMap(), GameClient()->Map()->Sha256(), m_CurGhost.m_aPlayer) == 0)
		{
			GhostRecorder()->WriteData(GHOSTDATA_TYPE_START_TICK, &m_CurGhost.m_StartTick, sizeof(int));
			GhostRecorder()->WriteData(GHOSTDATA_TYPE_SKIN, &m_CurGhost.m_Skin, sizeof(CGhostSkin));
			for(int i = 0; i < NumTicks; i++)
				GhostRecorder()->WriteData(GHOSTDATA_TYPE_CHARACTER, m_CurGhost.m_Path.Get(i), sizeof(CGhostCharacter));
		}
	}

	CGhostCharacter GhostChar;
	GetGhostCharacter(&GhostChar, pChar, pDDnetChar);
	if(!m_CurGhost.m_Path.Add(GhostChar))
		return;
	if(GhostRecorder()->IsRecording())
		GhostRecorder()->WriteData(GHOSTDATA_TYPE_CHARACTER, &GhostChar, sizeof(CGhostCharacter));
}

int CGhost::GetSlot() const
{
	for(int i = 0; i < MAX_ACTIVE_GHOSTS; i++)
		if(m_aActiveGhosts[i].Empty())
			return i;
	return -1;
}

int CGhost::FreeSlots() const
{
	int Num = 0;
	for(const auto &ActiveGhost : m_aActiveGhosts)
		if(ActiveGhost.Empty())
			Num++;
	return Num;
}

bool CGhost::IsSlotActive(int Slot) const
{
	return Slot >= 0 && Slot < MAX_ACTIVE_GHOSTS && !m_aActiveGhosts[Slot].Empty();
}

bool CGhost::IsSlotLoadedWith(int Slot, const char *pFilename) const
{
	return IsSlotActive(Slot) && pFilename != nullptr && str_comp_nocase(m_aActiveGhosts[Slot].m_aFilename, pFilename) == 0;
}

void CGhost::CheckStart()
{
	int RaceTick = -GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer;
	int RenderTick = m_NewRenderTick;

	if(GameClient()->LastRaceTick() != RaceTick && Client()->GameTick(g_Config.m_ClDummy) - RaceTick < Client()->GameTickSpeed())
	{
		if(m_Rendering && m_RenderingStartedByServer) // race restarted: stop rendering
			StopRender();
		if(m_Recording && GameClient()->LastRaceTick() != -1) // race restarted: activate restarting for local start detection so we have a smooth transition
			m_AllowRestart = true;
		if(GameClient()->LastRaceTick() == -1) // no restart: reset rendering preparations
			m_NewRenderTick = -1;
		if(GhostRecorder()->IsRecording()) // race restarted: stop recording
			GhostRecorder()->Stop(0, -1);
		int StartTick = RaceTick;

		if(GameClient()->m_GameInfo.m_BugDDRaceGhost) // the client recognizes the start one tick earlier than ddrace servers
			StartTick--;
		StartRecord(StartTick);
		RenderTick = StartTick;
	}

	TryRenderStart(RenderTick, true);
}

void CGhost::CheckStartLocal(bool Predicted)
{
	if(Predicted) // rendering
	{
		int RenderTick = m_NewRenderTick;

		vec2 PrevPos = GameClient()->m_PredictedPrevChar.m_Pos;
		vec2 Pos = GameClient()->m_PredictedChar.m_Pos;
		if(((!m_Rendering && RenderTick == -1) || m_AllowRestart) && GameClient()->RaceHelper()->IsStart(PrevPos, Pos))
		{
			if(m_Rendering && !m_RenderingStartedByServer) // race restarted: stop rendering
				StopRender();
			RenderTick = Client()->PredGameTick(g_Config.m_ClDummy);
		}

		TryRenderStart(RenderTick, false);
	}
	else // recording
	{
		if(!m_Recording || m_AllowRestart)
		{
			const auto *pPrev = GameClient()->m_Snap.m_pLocalPrevCharacter;
			const auto *pCur = GameClient()->m_Snap.m_pLocalCharacter;
			const qm_ghost_start::SSample Prev{pPrev->m_Tick, Client()->PrevGameTick(g_Config.m_ClDummy), vec2(pPrev->m_X, pPrev->m_Y)};
			const qm_ghost_start::SSample Cur{pCur->m_Tick, Client()->GameTick(g_Config.m_ClDummy), vec2(pCur->m_X, pCur->m_Y)};
			const int RecordTick = qm_ghost_start::FindStartTick(Prev, Cur, m_LastDeathTick, m_AllowRestart,
				[this](vec2 PrevPos, vec2 Pos) { return GameClient()->RaceHelper()->IsStart(PrevPos, Pos); });
			if(RecordTick != -1)
			{
				if(GhostRecorder()->IsRecording()) // race restarted: stop recording
					GhostRecorder()->Stop(0, -1);
				StartRecord(RecordTick);
			}
		}
	}
}

void CGhost::TryRenderStart(int Tick, bool ServerControl)
{
	// only restart rendering if it did not change since last tick to prevent stuttering
	if(m_NewRenderTick != -1 && m_NewRenderTick == Tick)
	{
		StartRender(Tick);
		Tick = -1;
		m_RenderingStartedByServer = ServerControl;
	}
	m_NewRenderTick = Tick;
}

void CGhost::OnNewSnapshot()
{
	// QmClient: 查看模式由手动时间线驱动，跑图起跑检测不参与
	if(m_ManualMode)
		return;
	if(!GameClient()->m_GameInfo.m_Race || !g_Config.m_ClRaceGhost || Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!GameClient()->m_Snap.m_pGameInfoObj || GameClient()->m_Snap.m_SpecInfo.m_Active || !GameClient()->m_Snap.m_pLocalCharacter || !GameClient()->m_Snap.m_pLocalPrevCharacter)
		return;

	const bool RaceFlag = GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME;
	const bool ServerControl = RaceFlag && g_Config.m_ClRaceGhostServerControl;

	if(!ServerControl)
		CheckStartLocal(false);
	else
		CheckStart();

	if(m_Recording)
		AddInfos(GameClient()->m_Snap.m_pLocalCharacter, (GameClient()->m_Snap.m_LocalClientId != -1 && GameClient()->m_Snap.m_aCharacters[GameClient()->m_Snap.m_LocalClientId].m_HasExtendedData) ? &GameClient()->m_Snap.m_aCharacters[GameClient()->m_Snap.m_LocalClientId].m_ExtendedData : nullptr);
}

void CGhost::OnNewPredictedSnapshot()
{
	if(m_ManualMode)
		return;
	if(!GameClient()->m_GameInfo.m_Race || !g_Config.m_ClRaceGhost || Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!GameClient()->m_Snap.m_pGameInfoObj || GameClient()->m_Snap.m_SpecInfo.m_Active || !GameClient()->m_Snap.m_pLocalCharacter || !GameClient()->m_Snap.m_pLocalPrevCharacter)
		return;

	const bool RaceFlag = GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME;
	const bool ServerControl = RaceFlag && g_Config.m_ClRaceGhostServerControl;

	if(!ServerControl)
		CheckStartLocal(true);
}

// 练习组里的交互时间线：与 Rank 1 查看模式读取的 .qmevt 完全同格式
// （事件 / 开关状态 / 消息 三段）。消息段练习里没有，写 0 条。
static bool WritePracticeViewTimeline(IStorage *pStorage, const char *pGroupDir,
	const std::vector<SLocalActorEvent> &vEvents, const std::vector<SLocalSwitchState> &vSwitchStates)
{
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/events.qmevt", pGroupDir);
	char aTmpPath[IO_MAX_PATH_LENGTH];
	str_format(aTmpPath, sizeof(aTmpPath), "%s.tmp_%d", aPath, pid());
	pStorage->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
	IOHANDLE File = pStorage->OpenFile(aTmpPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File == nullptr)
		return false;

	// 写出逻辑统一在 rank_view_timeline：练习只负责把中性轨迹适配成查看格式。
	const qmclient::rank_ghost::SViewTimeline Timeline = QmBuildPracticeViewTimeline(vEvents, vSwitchStates);
	const bool Written = qmclient::rank_ghost::WriteViewTimeline(File, Timeline);
	const bool Closed = io_close(File) == 0;
	if(!Written || !Closed)
	{
		log_error("ghost", "failed to write practice view timeline '%s'", aTmpPath);
		pStorage->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
		return false;
	}
	// RenameFile 在 Windows 上带替换语义；先删旧文件会在改名失败时丢掉可用时间线。
	if(!pStorage->RenameFile(aTmpPath, aPath, IStorage::TYPE_SAVE))
	{
		log_error("ghost", "failed to publish practice view timeline '%s'", aPath);
		pStorage->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
		return false;
	}
	return true;
}

bool CGhost::SavePracticeTrack(const char *pName, const SLocalSegment &Segment)
{
	if(Segment.m_vTracks.empty() || Segment.m_vTracks.front().m_vFrames.empty() || GhostRecorder()->IsRecording())
		return false;

	const auto &vMainFrames = Segment.m_vTracks.front().m_vFrames;
	// 这里的"时间"只是影子页的可读标签（这段练习跑了多久），不是竞速成绩。
	const int TickSpeed = std::max(1, Client()->GameTickSpeed());
	const int TimeMs = std::max(1, (int)((vMainFrames.back().m_Tick - Segment.m_StartTick) * 1000.0f / TickSpeed));

	// 练习线路不是成绩：玩家名带 [P]，且整条路径都不经过 StopRecord 的
	// GetOwnGhost / cl_race_ghost_save_best 比较，不会顶掉个人最佳。
	char aBaseName[MAX_NAME_LENGTH];
	if(pName && pName[0])
		str_format(aBaseName, sizeof(aBaseName), "[P] %s", pName);
	else
		str_copy(aBaseName, "[P] practice");

	// 组目录名沿用标准 ghost 命名（地图 / 名字 / 时长 / 时间戳 / sha），
	// 去掉 .gho 后缀即是 Rank 1 组回放认的目录；LoadGhostGroup 会扫其中所有轨。
	char aGroupFile[IO_MAX_PATH_LENGTH];
	GetPath(aGroupFile, sizeof(aGroupFile), aBaseName, TimeMs);
	char aGroupDir[IO_MAX_PATH_LENGTH];
	str_copy(aGroupDir, aGroupFile, sizeof(aGroupDir));
	const size_t Len = str_length(aGroupDir);
	if(Len <= 4 || !str_endswith(aGroupDir, ".gho"))
		return false;
	aGroupDir[Len - 4] = '\0';
	if(!Storage()->CreateFolder(aGroupDir, IStorage::TYPE_SAVE) && !Storage()->FolderExists(aGroupDir, IStorage::TYPE_SAVE))
		return false;

	// 皮肤取本地 Tee；两条轨共用一套皮肤，回放成员靠名字区分。
	// 停止录制可以从控制台命令直接触发，快照失效帧里 m_LocalClientId 可能是 -1；
	// 拿不到本地皮肤时退回默认皮肤，不能拿 -1 直接索引 m_aClients。
	CGhostSkin Skin;
	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
	{
		const CGameClient::CClientData *pData = &GameClient()->m_aClients[LocalClientId];
		SetGhostSkinData(&Skin, pData->m_aSkinName, pData->m_UseCustomColor, pData->m_ColorBody, pData->m_ColorFeet);
	}
	else
	{
		SetGhostSkinData(&Skin, "default", 0, 0, 0);
	}

	std::vector<SGhostTeamEntry> vTeamEntries;
	for(size_t TrackIndex = 0; TrackIndex < Segment.m_vTracks.size(); TrackIndex++)
	{
		const SLocalTrack &Track = Segment.m_vTracks[TrackIndex];
		if(Track.m_vFrames.empty())
			continue;

		char aPlayer[MAX_NAME_LENGTH];
		if(TrackIndex == 0)
			str_copy(aPlayer, aBaseName, sizeof(aPlayer));
		else
			str_format(aPlayer, sizeof(aPlayer), "%s (dummy)", aBaseName);

		char aTmp[IO_MAX_PATH_LENGTH];
		str_format(aTmp, sizeof(aTmp), "%s/track%d_tmp_%d.gho", aGroupDir, (int)TrackIndex, pid());
		if(GhostRecorder()->Start(aTmp, Client()->GetCurrentMap(), GameClient()->Map()->Sha256(), aPlayer) != 0)
			return false;

		GhostRecorder()->WriteData(GHOSTDATA_TYPE_START_TICK, &Segment.m_StartTick, sizeof(int));
		GhostRecorder()->WriteData(GHOSTDATA_TYPE_SKIN, &Skin, sizeof(CGhostSkin));
		for(const SLocalActorFrame &LocalFrame : Track.m_vFrames)
		{
			CNetObj_Character Obj = QmLocalWorldFrameToNetObj(LocalFrame);
			CGhostCharacter GhostChar;
			GetGhostCharacter(&GhostChar, &Obj, nullptr);
			// 练习轨没有 DDNetCharacter，冻结信息直接取录制帧里的剩余 tick。
			GhostChar.m_VelY = QmEncodeGhostFreezeMarker(LocalFrame.m_FreezeTicks);
			GhostRecorder()->WriteData(GHOSTDATA_TYPE_CHARACTER, &GhostChar, sizeof(CGhostCharacter));
		}
		GhostRecorder()->Stop((int)Track.m_vFrames.size(), TimeMs);
		if(GhostRecorder()->HasWriteError())
		{
			log_error("ghost", "failed to write practice track '%s'", aTmp);
			Storage()->RemoveFile(aTmp, IStorage::TYPE_SAVE);
			return false;
		}

		char aFinal[IO_MAX_PATH_LENGTH];
		str_format(aFinal, sizeof(aFinal), "%s/track%d.gho", aGroupDir, (int)TrackIndex);
		if(!Storage()->RenameFile(aTmp, aFinal, IStorage::TYPE_SAVE))
		{
			log_error("ghost", "failed to publish practice track '%s'", aFinal);
			Storage()->RemoveFile(aTmp, IStorage::TYPE_SAVE);
			return false;
		}
		if(Track.m_Team >= TEAM_FLOCK && Track.m_Team < NUM_DDRACE_TEAMS)
		{
			char aTrackName[32];
			str_format(aTrackName, sizeof(aTrackName), "track%d.gho", (int)TrackIndex);
			vTeamEntries.push_back({aTrackName, Track.m_Team});
		}
	}

	char aTeamPath[IO_MAX_PATH_LENGTH];
	str_format(aTeamPath, sizeof(aTeamPath), "%s/%s", aGroupDir, QmGhostTeamMetadataName());
	if(!QmWriteGhostTeamMetadata(Storage(), aTeamPath, vTeamEntries))
		log_error("ghost", "failed to write practice ghost team metadata '%s'", aTeamPath);
	return WritePracticeViewTimeline(Storage(), aGroupDir, Segment.m_vEvents, Segment.m_vSwitchStates);
}

namespace
{
	struct SPracticeGroupScanContext
	{
		IStorage *m_pStorage = nullptr;
		IGhostLoader *m_pLoader = nullptr;
		// ms_pGhostDir 是私有静态成员，由 ListPracticeGroups 传进来，回调不直接碰。
		const char *m_pGhostDir = nullptr;
		const char *m_pMap = nullptr;
		SHA256_DIGEST m_MapSha256{};
		unsigned m_MapCrc = 0;
		std::vector<CGhost::SPracticeGroup> *m_pOut = nullptr;
	};

	int PracticeGroupScanCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
	{
		(void)StorageType;
		auto *pCtx = static_cast<SPracticeGroupScanContext *>(pUser);
		if(!IsDir)
			return 0;

		char aDir[IO_MAX_PATH_LENGTH];
		char aEvents[IO_MAX_PATH_LENGTH];
		char aMain[IO_MAX_PATH_LENGTH];
		str_format(aDir, sizeof(aDir), "%s/%s", pCtx->m_pGhostDir, pInfo->m_pName);
		str_format(aEvents, sizeof(aEvents), "%s/events.qmevt", aDir);
		str_format(aMain, sizeof(aMain), "%s/track0.gho", aDir);
		if(!pCtx->m_pStorage->FileExists(aEvents, IStorage::TYPE_SAVE))
			return 0;

		CGhostInfo Info;
		if(!pCtx->m_pLoader->GetGhostInfo(aMain, &Info, pCtx->m_pMap, pCtx->m_MapSha256, pCtx->m_MapCrc))
			return 0;

		CGhost::SPracticeGroup Group;
		str_copy(Group.m_aDir, aDir);
		str_copy(Group.m_aOwner, Info.m_aOwner);
		Group.m_Time = Info.m_Time;
		Group.m_TimeModified = pInfo->m_TimeModified;
		pCtx->m_pOut->push_back(Group);
		return 0;
	}
} // namespace

void CGhost::ListPracticeGroups(std::vector<SPracticeGroup> &vOut) const
{
	vOut.clear();
	if(!m_pGhostLoader || !GameClient()->Map())
		return;

	SPracticeGroupScanContext Ctx;
	Ctx.m_pStorage = Storage();
	Ctx.m_pLoader = m_pGhostLoader;
	Ctx.m_pGhostDir = ms_pGhostDir;
	Ctx.m_pMap = Client()->GetCurrentMap();
	Ctx.m_MapSha256 = GameClient()->Map()->Sha256();
	Ctx.m_MapCrc = GameClient()->Map()->Crc();
	Ctx.m_pOut = &vOut;
	Storage()->ListDirectoryInfo(IStorage::TYPE_SAVE, ms_pGhostDir, PracticeGroupScanCallback, &Ctx);
	// 目录遍历顺序由文件系统决定，菜单要稳定顺序：最近的组排前面。
	std::sort(vOut.begin(), vOut.end(), [](const SPracticeGroup &Left, const SPracticeGroup &Right) {
		return Left.m_TimeModified > Right.m_TimeModified;
	});
}

void CGhost::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	// Play the ghost
	if(!m_Rendering || (!m_ManualMode && !g_Config.m_ClRaceShowGhost))
		return;

	int PlaybackTick;
	if(m_ManualMode)
	{
		// 查看模式：独立时间线，播到尽头自动停在最后一帧
		PlaybackTick = ManualPlaybackTick();
		if(m_ManualClock.Playing() && m_ManualEndTick > 0 && PlaybackTick >= m_ManualEndTick)
		{
			ManualSeek(m_ManualEndTick);
			PlaybackTick = ManualPlaybackTick();
		}
	}
	else
	{
		PlaybackTick = Client()->PredGameTick(g_Config.m_ClDummy) - m_StartRenderTick;
		// 快速练习接管了客户端侧权威世界：它的时钟在暂停/冻结时会与
		// PredGameTick 分叉，幽灵必须跟着分支世界走，否则会凭空超前。
		int PracticePlaybackTick;
		if(GameClient()->m_FastPractice.ResolvePracticePlaybackTick(PracticePlaybackTick))
			PlaybackTick = PracticePlaybackTick;
	}

	CScreenRect ScreenRect = Graphics()->GetScreen();

	// 玩家周围 200x200 的盒子
	ScreenRect.Expand(100.0f);

	// QmClient: 先算出本帧每个可见虚影的插值数据再统一绘制。
	// 查看模式必须先把所有 hook 画完再画所有 Tee——与 demo 播放的玩家渲染
	// 顺序（CPlayers::OnRender：先全部 hook，再全部 Tee）一致；否则多轨回放里
	// 靠后虚影的 hook 会压在前一个虚影的 Tee 上，出现“hook 盖住 Tee”。
	// 跑图模式仍按上游的逐槽位顺序绘制。
	m_vGhostDraws.clear();
	for(int Slot = 0; Slot < MAX_ACTIVE_GHOSTS; ++Slot)
	{
		CGhostItem &Ghost = m_aActiveGhosts[Slot];
		if(Ghost.Empty() || (m_ManualMode && !m_aManualSlots[Slot]))
			continue;

		int GhostTick = Ghost.m_StartTick + PlaybackTick;
		// QmClient：查看模式把播放头钳在轨迹首尾采样内——播到尽头时虚影停在最后一帧，
		// 而不是让只前进的快照游标越界置 -1 后整只虚影消失（跑图模式保持原语义）
		if(m_ManualMode)
			GhostTick = std::clamp(GhostTick, Ghost.m_Path.Get(0)->m_Tick, Ghost.m_Path.Get(Ghost.m_Path.Size() - 1)->m_Tick);
		while(Ghost.m_PlaybackPos >= 0 && Ghost.m_Path.Get(Ghost.m_PlaybackPos)->m_Tick < GhostTick)
		{
			if(Ghost.m_PlaybackPos < Ghost.m_Path.Size() - 1)
				Ghost.m_PlaybackPos++;
			else
				Ghost.m_PlaybackPos = -1;
		}

		if(Ghost.m_PlaybackPos < 0)
			continue;

		int CurPos = Ghost.m_PlaybackPos;
		int PrevPos = maximum(0, CurPos - 1);
		if(Ghost.m_Path.Get(PrevPos)->m_Tick > GhostTick)
			continue;

		SGhostDrawData Draw;
		Draw.m_Slot = Slot;
		GetNetObjCharacter(&Draw.m_Player, Ghost.m_Path.Get(CurPos));
		GetNetObjCharacter(&Draw.m_Prev, Ghost.m_Path.Get(PrevPos));
		Draw.m_StrongWeakId = QmDecodeGhostStrongWeak(Ghost.m_Path.Get(CurPos)->m_Direction);
		Draw.m_HasStrongWeak = Draw.m_StrongWeakId >= 0;

		int TickDiff = Draw.m_Player.m_Tick - Draw.m_Prev.m_Tick;
		if(TickDiff > 0)
		{
			// QmClient：查看模式使用手动时间线的插值相位；本地预测相位会持续波动，
			// 暂停/播完时直接沿用会让虚影在末段两点间抖动
			const float IntraPhase = m_ManualMode ? ManualRenderIntra() : Client()->PredIntraGameTick(g_Config.m_ClDummy);
			Draw.m_IntraTick = (GhostTick - Draw.m_Prev.m_Tick - 1 + IntraPhase) / TickDiff;
		}

		if(m_ManualMode)
		{
			// 查看模式：挥锤/开枪相位只跟回放播放头走。实时游戏 tick 在暂停时仍在推进，
			// 沿用它会让暂停画面里的锤子一直上下摆动。
			const float PlaybackTick = GhostTick + ManualRenderIntra();
			Draw.m_AttackTimeOverride = maximum(0.0f, (PlaybackTick - 1.0f - Draw.m_Prev.m_AttackTick) / (float)Client()->GameTickSpeed());
		}
		else
		{
			Draw.m_Player.m_AttackTick += Client()->GameTick(g_Config.m_ClDummy) - GhostTick;
		}

		// QmClient：新格式在 m_VelY 写显式冻结标记；旧格式没有标记位时才回退到
		// WEAPON_NINJA 判定（兼容旧幽灵文件，代价是旧文件里的真实忍者帧仍会被
		// 当作冻结）。冻结表现完全复刻 demo 播放的玩家渲染（players.cpp）：
		// 冰冻着色 + 隐藏武器、旧版卡塔纳手随 TClient 的 qm_frozen_katana、
		// 忍者皮肤随 cl_show_ninja
		const CGhostCharacter *pCurFrame = Ghost.m_Path.Get(CurPos);
		const bool GhostFrozen = QmGhostFrameFrozen(pCurFrame->m_VelY, pCurFrame->m_Weapon == WEAPON_NINJA);
		unsigned FrozenFlags = 0;
		int RenderWeapon = Draw.m_Player.m_Weapon;
		if(GhostFrozen)
		{
			FrozenFlags = TEE_EFFECT_FROZEN | TEE_NO_WEAPON;
			// 数据中武器被用作冻结标记，还原为原始武器（DDRace 默认为锤）；
			// 开启 qm_frozen_katana 时保留忍者武器以复现旧版卡塔纳手
			RenderWeapon = WEAPON_HAMMER;
			if(g_Config.m_QmFreezeKatana > 0)
			{
				RenderWeapon = WEAPON_NINJA;
				FrozenFlags &= ~TEE_NO_WEAPON;
			}
		}
		Draw.m_Player.m_Weapon = RenderWeapon;
		Draw.m_Prev.m_Weapon = RenderWeapon;

		Draw.m_pSharedRenderInfo = &Ghost.m_pManagedTeeRenderInfo->TeeRenderInfo();
		if(GhostFrozen)
		{
			Draw.m_UseOwnRenderInfo = true;
			Draw.m_OwnRenderInfo = *Draw.m_pSharedRenderInfo;
			Draw.m_OwnRenderInfo.m_TeeRenderFlags |= FrozenFlags;
			if(g_Config.m_ClShowNinja)
			{
				// change the skin for the ghost to the ninja
				Draw.m_OwnRenderInfo.ApplySkin(GameClient()->m_Players.NinjaTeeRenderInfo()->TeeRenderInfo());
				Draw.m_OwnRenderInfo.m_CustomColoredSkin = GameClient()->IsTeamPlay();
				if(!Draw.m_OwnRenderInfo.m_CustomColoredSkin)
				{
					Draw.m_OwnRenderInfo.m_ColorBody = ColorRGBA(1, 1, 1);
					Draw.m_OwnRenderInfo.m_ColorFeet = ColorRGBA(1, 1, 1);

					// 与玩家侧 TClient 的彩色冻结皮肤一致（qm_color_freeze）
					if(g_Config.m_QmColorFreeze)
					{
						const CTeeRenderInfo &OwnInfo = *Draw.m_pSharedRenderInfo;
						Draw.m_OwnRenderInfo.m_CustomColoredSkin = OwnInfo.m_CustomColoredSkin;
						Draw.m_OwnRenderInfo.m_ColorFeet = g_Config.m_QmColorFreezeFeet ? OwnInfo.m_ColorFeet : ColorRGBA(1, 1, 1);
						const float Darken = (g_Config.m_QmColorFreezeDarken / 100.0f) * 0.5f + 0.5f;
						const ColorRGBA Body = OwnInfo.m_CustomColoredSkin ? OwnInfo.m_ColorBody : ColorRGBA(1, 1, 1);
						Draw.m_OwnRenderInfo.m_ColorBody = ColorRGBA(Body.r * Darken, Body.g * Darken, Body.b * Darken, 1.0f);
					}
				}
			}
		}

		// QmClient: 查看模式记录每个槽位虚影的插值位置（镜头跟随用）
		Draw.m_Pos = mix(vec2(Draw.m_Prev.m_X, Draw.m_Prev.m_Y), vec2(Draw.m_Player.m_X, Draw.m_Player.m_Y), Draw.m_IntraTick);
		if(m_ManualMode)
		{
			m_aManualRenderPos[Slot] = Draw.m_Pos;
			m_aManualRenderPosValid[Slot] = true;
		}

		m_vGhostDraws.push_back(Draw);
	}

	// 每帧解析该虚影实际使用的渲染信息（冻结换肤时用本帧副本，否则用共享信息）
	auto ResolveRenderInfo = [](const SGhostDrawData &Draw) {
		return Draw.m_UseOwnRenderInfo ? &Draw.m_OwnRenderInfo : Draw.m_pSharedRenderInfo;
	};

	if(!m_ManualMode)
	{
		// 跑图模式：保持上游逐槽位顺序（每个虚影的 hook 紧跟自己的 Tee）
		for(const SGhostDrawData &Draw : m_vGhostDraws)
		{
			GameClient()->m_Players.RenderHook(ScreenRect, &Draw.m_Prev, &Draw.m_Player, ResolveRenderInfo(Draw), -2, Draw.m_IntraTick);
			GameClient()->m_Players.RenderPlayer(ScreenRect, &Draw.m_Prev, &Draw.m_Player, ResolveRenderInfo(Draw), -2, Draw.m_IntraTick);
		}
		return;
	}

	// 查看模式：全部 hook -> 全部 Tee -> 名字、强弱钩与方向指示叠加。
	// 冻结条由 CFreezeBars 在玩家与地图前景之后绘制。
	for(const SGhostDrawData &Draw : m_vGhostDraws)
		GameClient()->m_Players.RenderHook(ScreenRect, &Draw.m_Prev, &Draw.m_Player, ResolveRenderInfo(Draw), -2, Draw.m_IntraTick);
	for(const SGhostDrawData &Draw : m_vGhostDraws)
		GameClient()->m_Players.RenderPlayer(ScreenRect, &Draw.m_Prev, &Draw.m_Player, ResolveRenderInfo(Draw), -2, Draw.m_IntraTick, Draw.m_AttackTimeOverride);

	int PrimaryStrongWeakId = -1;
	for(const SGhostDrawData &Draw : m_vGhostDraws)
	{
		if(Draw.m_Slot == m_ManualPrimarySlot && Draw.m_HasStrongWeak)
		{
			PrimaryStrongWeakId = Draw.m_StrongWeakId;
			break;
		}
	}

	// QmClient: 查看模式下为每个虚影标注名字（多轨回放的信息可读性，对齐 demo 播放器）
	// 名字尺寸跟随客户端名字牌大小设置；方向键指示与名字牌共用 cl_show_direction 数据
	for(const SGhostDrawData &Draw : m_vGhostDraws)
	{
		const CGhostItem &Ghost = m_aActiveGhosts[Draw.m_Slot];
		const vec2 TeePos = Draw.m_Pos;
		float NameWidth = 0.0f;
		if(g_Config.m_ClNamePlates && Ghost.m_aPlayer[0] != '\0')
		{
			const float NameFontSize = 18.0f + 20.0f * g_Config.m_ClNamePlatesSize / 100.0f;
			NameWidth = TextRender()->TextWidth(NameFontSize, Ghost.m_aPlayer, -1);
			TextRender()->TextColor(1, 1, 1, 1);
			TextRender()->TextOutlineColor(0, 0, 0, 0.6f);
			TextRender()->Text(TeePos.x - NameWidth / 2, TeePos.y - 56.0f, NameFontSize, Ghost.m_aPlayer);
		}

		// 强弱钩与官方 demo 名字牌同语义：主选手作为参照，其它成员显示强/弱钩。
		const auto DisplaySettings = qm_demo_display::Resolve(g_Config, true, false);
		if(Draw.m_HasStrongWeak && PrimaryStrongWeakId >= 0 && DisplaySettings.StrongWeakEnabled())
		{
			const bool Own = Draw.m_Slot == m_ManualPrimarySlot;
			int State = 0;
			if(!Own)
				State = PrimaryStrongWeakId > Draw.m_StrongWeakId ? 1 : -1;
			const bool Strong = State > 0;
			const bool Weak = State < 0;
			if(DisplaySettings.ShowStrongWeak(Own, Strong, Weak))
			{
				const float StrongSize = 18.0f + 20.0f * g_Config.m_ClNamePlatesStrongSize / 100.0f;
				const vec2 IconPos(
					TeePos.x - (NameWidth > 0.0f ? NameWidth * 0.5f + StrongSize * 0.75f + 4.0f : 0.0f),
					TeePos.y - 56.0f);
				const ColorRGBA StrongColor = State > 0 ?
								      color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmNameplateStrongHookColor)) :
							      State < 0 ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmNameplateWeakHookColor)) :
									  ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
				Graphics()->TextureSet(g_pData->m_aImages[IMAGE_STRONGWEAK].m_Id);
				Graphics()->QuadsBegin();
				Graphics()->SetColor(StrongColor);
				Graphics()->SelectSprite(State > 0 ? SPRITE_HOOK_STRONG : State < 0 ? SPRITE_HOOK_WEAK :
												      SPRITE_HOOK_ICON);
				Graphics()->DrawSprite(IconPos.x, IconPos.y, StrongSize, StrongSize);
				Graphics()->QuadsEnd();
				if(DisplaySettings.ShowStrongWeakId())
				{
					char aStrongWeakId[4];
					str_format(aStrongWeakId, sizeof(aStrongWeakId), "%d", Draw.m_StrongWeakId);
					TextRender()->TextColor(StrongColor);
					TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.6f);
					TextRender()->Text(IconPos.x + StrongSize * 0.7f, IconPos.y - StrongSize * 0.45f, StrongSize * 0.8f, aStrongWeakId);
				}
			}
		}

		// 查看模式使用共用 demo 的方向显示配置，主选手作为回放的自身成员。
		const int Direction = qm_demo_display::Resolve(g_Config, true, false).m_Direction;
		const bool Own = Draw.m_Slot == m_ManualPrimarySlot;
		if(!g_Config.m_QmRankGhostShowDirection || Direction == 0 || (Direction == 1 && Own) || (Direction == 3 && !Own))
			continue;
		const float DirSize = 18.0f + 20.0f * g_Config.m_ClNamePlatesSize / 100.0f;
		const bool DirLeft = Draw.m_Player.m_Direction == -1;
		const bool DirRight = Draw.m_Player.m_Direction == 1;
		const bool DirJump = (Draw.m_Player.m_Jumped & 1) != 0;
		if(!DirLeft && !DirRight && !DirJump)
			continue;
		const vec2 Center = TeePos - vec2(0.0f, 78.0f);
		const float Spacing = DirSize * 0.9f;
		Graphics()->TextureSet(g_pData->m_aImages[IMAGE_ARROW].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.9f);
		if(DirLeft)
		{
			IGraphics::CQuadItem Quad(Center.x - Spacing - DirSize / 2.0f, Center.y - DirSize / 2.0f, DirSize, DirSize);
			Graphics()->QuadsSetRotation(pi);
			Graphics()->QuadsDrawTL(&Quad, 1);
		}
		if(DirJump)
		{
			IGraphics::CQuadItem Quad(Center.x - DirSize / 2.0f, Center.y - DirSize / 2.0f, DirSize, DirSize);
			Graphics()->QuadsSetRotation(pi / -2.0f);
			Graphics()->QuadsDrawTL(&Quad, 1);
		}
		if(DirRight)
		{
			IGraphics::CQuadItem Quad(Center.x + Spacing - DirSize / 2.0f, Center.y - DirSize / 2.0f, DirSize, DirSize);
			Graphics()->QuadsSetRotation(0.0f);
			Graphics()->QuadsDrawTL(&Quad, 1);
		}
		Graphics()->QuadsEnd();
		Graphics()->QuadsSetRotation(0.0f);
	}

	// 自由视角或镜头过渡时，主选手可能在屏幕外：边缘箭头持续指向影子，
	// 玩家不用逐个拖动镜头寻找。
	if(m_ManualFocusSlot >= 0 && m_aManualRenderPosValid[m_ManualFocusSlot])
	{
		float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
		Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
		const vec2 Focus = m_aManualRenderPos[m_ManualFocusSlot];
		const float Margin = 28.0f;
		if(Focus.x < ScreenX0 + Margin || Focus.x > ScreenX1 - Margin || Focus.y < ScreenY0 + Margin || Focus.y > ScreenY1 - Margin)
		{
			const vec2 Center = GameClient()->m_Camera.m_Center;
			const vec2 Direction = normalize(Focus - Center);
			const vec2 MarkerPos(
				std::clamp(Focus.x, ScreenX0 + Margin, ScreenX1 - Margin),
				std::clamp(Focus.y, ScreenY0 + Margin, ScreenY1 - Margin));
			const float MarkerSize = 22.0f;
			Graphics()->TextureSet(g_pData->m_aImages[IMAGE_ARROW].m_Id);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.85f);
			Graphics()->QuadsSetRotation(angle(Direction));
			IGraphics::CQuadItem Quad(MarkerPos.x - MarkerSize * 0.5f, MarkerPos.y - MarkerSize * 0.5f, MarkerSize, MarkerSize);
			Graphics()->QuadsDrawTL(&Quad, 1);
			Graphics()->QuadsEnd();
			Graphics()->QuadsSetRotation(0.0f);
		}
	}
}

void CGhost::UpdateTeeRenderInfo(CGhostItem &Ghost)
{
	CSkinDescriptor SkinDescriptor;
	SkinDescriptor.m_Flags = CSkinDescriptor::FLAG_SIX;
	IntsToStr(Ghost.m_Skin.m_aSkin, std::size(Ghost.m_Skin.m_aSkin), SkinDescriptor.m_aSkinName, std::size(SkinDescriptor.m_aSkinName));
	if(!CSkin::IsValidName(SkinDescriptor.m_aSkinName))
	{
		str_copy(SkinDescriptor.m_aSkinName, "default");
	}

	CTeeRenderInfo TeeRenderInfo;
	TeeRenderInfo.ApplyColors(Ghost.m_Skin.m_UseCustomColor, Ghost.m_Skin.m_ColorBody, Ghost.m_Skin.m_ColorFeet);
	TeeRenderInfo.m_Size = 64.0f;

	Ghost.m_pManagedTeeRenderInfo = GameClient()->CreateManagedTeeRenderInfo(TeeRenderInfo, SkinDescriptor);
}

void CGhost::StartRecord(int Tick)
{
	m_Recording = true;
	m_CurGhost.Reset();
	m_CurGhost.m_StartTick = Tick;

	const CGameClient::CClientData *pData = &GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId];
	str_copy(m_CurGhost.m_aPlayer, Client()->PlayerName());
	SetGhostSkinData(&m_CurGhost.m_Skin, pData->m_aSkinName, pData->m_UseCustomColor, pData->m_ColorBody, pData->m_ColorFeet);
	UpdateTeeRenderInfo(m_CurGhost);
}

void CGhost::StopRecord(int Time)
{
	const bool WasRecording = m_Recording;
	m_Recording = false;
	const bool RecordingToFile = GhostRecorder() != nullptr && GhostRecorder()->IsRecording();
	if(!WasRecording && !RecordingToFile && m_aTmpFilename[0] == '\0')
	{
		m_CurGhost.Reset();
		return;
	}

	CMenus::CGhostItem *pOwnGhost = nullptr;
	if(Time > 0)
		pOwnGhost = GameClient()->m_Menus.GetOwnGhost();
	const bool StoreGhost = Time > 0 && !m_CurGhost.Empty() && (!pOwnGhost || Time < pOwnGhost->m_Time || !g_Config.m_ClRaceGhostSaveBest);

	bool WriteFailed = false;
	if(RecordingToFile)
	{
		GhostRecorder()->Stop(m_CurGhost.m_Path.Size(), StoreGhost ? Time : -1);
		WriteFailed = GhostRecorder()->HasWriteError();
	}

	if(StoreGhost)
	{
		// add to active ghosts
		int Slot = GetSlot();
		if(Slot != -1 && (!pOwnGhost || Time < pOwnGhost->m_Time))
			m_aActiveGhosts[Slot] = std::move(m_CurGhost);

		if(pOwnGhost && pOwnGhost->Active() && Time < pOwnGhost->m_Time)
			Unload(pOwnGhost->m_Slot);

		// create ghost item
		CMenus::CGhostItem Item;
		if(RecordingToFile && !WriteFailed)
			GetPath(Item.m_aFilename, sizeof(Item.m_aFilename), m_CurGhost.m_aPlayer, Time);
		str_copy(Item.m_aPlayer, m_CurGhost.m_aPlayer);
		Item.m_Time = Time;
		Item.m_Slot = Slot;

		// save new ghost file
		if(Item.HasFile() && !Storage()->RenameFile(m_aTmpFilename, Item.m_aFilename, IStorage::TYPE_SAVE))
		{
			log_error("ghost", "failed to publish ghost file '%s'", Item.m_aFilename);
			Item.m_aFilename[0] = '\0';
		}
		else if(Item.HasFile() && Slot >= 0)
			str_copy(m_aActiveGhosts[Slot].m_aFilename, Item.m_aFilename);

		// add item to menu list
		GameClient()->m_Menus.UpdateOwnGhost(Item);
	}

	m_aTmpFilename[0] = '\0';
	m_CurGhost.Reset();
}

void CGhost::StartRender(int Tick)
{
	m_Rendering = true;
	m_ManualMode = false;
	m_StartRenderTick = Tick;
	for(auto &Ghost : m_aActiveGhosts)
		Ghost.m_PlaybackPos = 0;
}

void CGhost::StopRender()
{
	m_Rendering = false;
	m_NewRenderTick = -1;
	m_ManualMode = false;
	m_ManualPrimarySlot = -1;
	m_ManualFocusSlot = -1;
	m_ManualClock.SetPlaying(false, Client()->LocalTime());
	std::fill(std::begin(m_aManualRenderPosValid), std::end(m_aManualRenderPosValid), false);
	for(auto &vSpans : m_aManualFreezeSpans)
		vSpans.clear();
}

void CGhost::BuildManualFreezeSpans(const std::vector<int> &vSlots)
{
	for(int Slot : vSlots)
	{
		if(Slot < 0 || Slot >= MAX_ACTIVE_GHOSTS)
			continue;
		std::vector<SGhostFreezeSpan> &vSpans = m_aManualFreezeSpans[Slot];
		vSpans.clear();
		CGhostItem &Ghost = m_aActiveGhosts[Slot];
		if(Ghost.Empty())
			continue;
		// 连续冻结帧 = 一个冻结段。新文件用 m_VelY 显式标记，旧文件回退到
		// NINJA 判定（见 ghost_freeze_bar.h）；断档只出现在解除冻结或轨迹分段，
		// 按帧序号还原起止即可。
		std::vector<int> vRelativeTicks;
		std::vector<unsigned char> vFrozen;
		vRelativeTicks.reserve(Ghost.m_Path.Size());
		vFrozen.reserve(Ghost.m_Path.Size());
		for(int Index = 0; Index < Ghost.m_Path.Size(); ++Index)
		{
			const CGhostCharacter *pFrame = Ghost.m_Path.Get(Index);
			vRelativeTicks.push_back(pFrame->m_Tick - Ghost.m_StartTick);
			vFrozen.push_back(QmGhostFrameFrozen(pFrame->m_VelY, pFrame->m_Weapon == WEAPON_NINJA) ? 1 : 0);
		}
		QmCollectGhostFreezeSpans(vRelativeTicks, vFrozen, vSpans);
	}
}

void CGhost::ManualSetSpeed(float Speed)
{
	if(m_ManualMode)
		m_ManualClock.SetSpeed(Speed, Client()->LocalTime());
}

void CGhost::ManualSetFocusSlot(int Slot)
{
	if(m_ManualMode && Slot >= 0 && Slot < MAX_ACTIVE_GHOSTS && m_aManualSlots[Slot])
		m_ManualFocusSlot = Slot;
}

void CGhost::RenderManualFreezeBars()
{
	if(!m_ManualMode)
		return;
	const int PlaybackTick = ManualPlaybackTick();
	for(int Slot = 0; Slot < MAX_ACTIVE_GHOSTS; ++Slot)
	{
		if(!m_aManualSlots[Slot] || !m_aManualRenderPosValid[Slot])
			continue;
		const float FreezeProgress = QmGhostFreezeProgress(m_aManualFreezeSpans[Slot], PlaybackTick);
		if(FreezeProgress <= 0.0f)
			continue;
		GameClient()->m_FreezeBars.RenderBarAt(m_aManualRenderPos[Slot], FreezeProgress);
	}
}

void CGhost::StartRenderManual(const std::vector<int> &vSlots)
{
	StopRender();
	BuildManualFreezeSpans(vSlots);
	m_ManualPrimarySlot = vSlots.empty() ? -1 : vSlots.front();
	m_ManualFocusSlot = m_ManualPrimarySlot;
	m_vManualSampleTicks.clear();
	std::fill(std::begin(m_aManualSlots), std::end(m_aManualSlots), false);
	for(int Slot : vSlots)
		if(Slot >= 0 && Slot < MAX_ACTIVE_GHOSTS)
			m_aManualSlots[Slot] = true;
	bool HaveGhost = false;
	m_ManualEndTick = 0;
	for(int Slot : vSlots)
	{
		if(Slot < 0 || Slot >= MAX_ACTIVE_GHOSTS)
			continue;
		CGhostItem &Ghost = m_aActiveGhosts[Slot];
		if(Ghost.Empty())
			continue;
		HaveGhost = true;
		if(Slot == m_ManualPrimarySlot)
		{
			m_vManualSampleTicks.reserve(Ghost.m_Path.Size());
			for(int Index = 0; Index < Ghost.m_Path.Size(); ++Index)
				m_vManualSampleTicks.push_back(Ghost.m_Path.Get(Index)->m_Tick - Ghost.m_StartTick);
		}
		// 总时长取所有激活影子中最长的一条轨迹（相对 tick）：团队/接力回放里先
		// 完成、中途加入或提前离开的玩家轨迹更短，取最短会把整组截断在最早那下；
		// 短轨迹播到自己的末帧后停在原处（查看模式按各自轨迹范围钳制）
		const int EndTick = Ghost.m_Path.Get(Ghost.m_Path.Size() - 1)->m_Tick - Ghost.m_StartTick;
		if(EndTick > m_ManualEndTick)
			m_ManualEndTick = EndTick;
		Ghost.m_PlaybackPos = 0;
	}
	if(!HaveGhost || m_ManualEndTick <= 0)
		return;
	m_ManualMode = true;
	m_Rendering = true;
	m_RenderingStartedByServer = false;
	m_ManualClock.Start(Client()->LocalTime(), m_ManualEndTick, Client()->GameTickSpeed());
	std::fill(std::begin(m_aManualRenderPosValid), std::end(m_aManualRenderPosValid), false);
}

int CGhost::ManualPlaybackTick() const
{
	return m_ManualMode ? m_ManualClock.Tick(Client()->LocalTime()) : 0;
}

float CGhost::ManualRenderIntra() const
{
	return m_ManualMode ? m_ManualClock.Intra(Client()->LocalTime()) : 0.0f;
}

bool CGhost::GetManualRenderPosAtTick(int Slot, int Tick, vec2 *pOut) const
{
	if(pOut == nullptr || Slot < 0 || Slot >= MAX_ACTIVE_GHOSTS)
		return false;
	const CGhostItem &Ghost = m_aActiveGhosts[Slot];
	if(Ghost.Empty())
		return false;
	const int Size = Ghost.m_Path.Size();
	if(Size <= 0)
		return false;

	const int TargetTick = Ghost.m_StartTick + std::clamp(Tick, 0, m_ManualEndTick);
	int Lo = 0;
	int Hi = Size - 1;
	while(Lo < Hi)
	{
		const int Mid = (Lo + Hi) / 2;
		if(Ghost.m_Path.Get(Mid)->m_Tick < TargetTick)
			Lo = Mid + 1;
		else
			Hi = Mid;
	}
	const CGhostCharacter *pCur = Ghost.m_Path.Get(Lo);
	const CGhostCharacter *pPrev = Ghost.m_Path.Get(maximum(0, Lo - 1));
	if(Lo == 0 || pCur->m_Tick <= pPrev->m_Tick)
	{
		*pOut = vec2(pCur->m_X, pCur->m_Y);
		return true;
	}
	const float Ratio = std::clamp((TargetTick - pPrev->m_Tick) / (float)(pCur->m_Tick - pPrev->m_Tick), 0.0f, 1.0f);
	*pOut = mix(vec2(pPrev->m_X, pPrev->m_Y), vec2(pCur->m_X, pCur->m_Y), Ratio);
	return true;
}

void CGhost::ManualSetPlaying(bool Playing)
{
	if(m_ManualMode)
		m_ManualClock.SetPlaying(Playing, Client()->LocalTime());
}

void CGhost::ManualSeek(int RelativeTick)
{
	if(!m_ManualMode)
		return;
	m_ManualClock.Seek(RelativeTick, Client()->LocalTime());

	// 快照游标只前进，seek 后必须重扫定位
	const int PlaybackTick = ManualPlaybackTick();
	for(auto &Ghost : m_aActiveGhosts)
	{
		if(Ghost.Empty())
			continue;
		const int TargetTick = Ghost.m_StartTick + PlaybackTick;
		const int Size = Ghost.m_Path.Size();
		int Pos = 0;
		int Last = 0;
		while(Pos < Size && Ghost.m_Path.Get(Pos)->m_Tick <= TargetTick)
		{
			Last = Pos;
			++Pos;
		}
		Ghost.m_PlaybackPos = Pos >= Size ? Size - 1 : Last;
	}
}

int CGhost::Load(const char *pFilename)
{
	int Slot = GetSlot();
	if(Slot == -1)
		return -1;

	if(!GhostLoader()->Load(pFilename, Client()->GetCurrentMap(), GameClient()->Map()->Sha256(), GameClient()->Map()->Crc()))
		return -1;

	const CGhostInfo *pInfo = GhostLoader()->GetInfo();

	// select ghost
	CGhostItem *pGhost = &m_aActiveGhosts[Slot];
	pGhost->Reset();

	str_copy(pGhost->m_aPlayer, pInfo->m_aOwner);

	int Index = 0;
	bool FoundSkin = false;
	bool FoundCharacterNoTick = false;
	bool FoundCharacterTick = false;
	bool Error = false;

	int Type;
	while(GhostLoader()->ReadNextType(&Type))
	{
		if(Index == pInfo->m_NumTicks && (Type == GHOSTDATA_TYPE_CHARACTER || Type == GHOSTDATA_TYPE_CHARACTER_NO_TICK))
		{
			log_error_color(LOG_COLOR_GHOST, "ghost", "Failed to read ghost data: too many ghost characters");
			Error = true;
			break;
		}

		if(Type == GHOSTDATA_TYPE_SKIN && !FoundSkin)
		{
			if(!GhostLoader()->ReadData(Type, &pGhost->m_Skin, sizeof(CGhostSkin)))
			{
				log_error_color(LOG_COLOR_GHOST, "ghost", "Failed to read ghost data: failed to read skin");
				Error = true;
				break;
			}
			FoundSkin = true;
		}
		else if(Type == GHOSTDATA_TYPE_CHARACTER_NO_TICK)
		{
			if(FoundCharacterTick)
			{
				log_error_color(LOG_COLOR_GHOST, "ghost", "Failed to read ghost data: ghost character with and without tick cannot be mixed");
				Error = true;
				break;
			}
			else if(!pGhost->m_Path.ReadAndAdd([&](CGhostCharacter &Value) { return GhostLoader()->ReadData(Type, &Value, sizeof(CGhostCharacter_NoTick)); }))
			{
				log_error_color(LOG_COLOR_GHOST, "ghost", "Failed to read ghost data: failed to read ghost character (without tick)");
				Error = true;
				break;
			}
			++Index;
			FoundCharacterNoTick = true;
		}
		else if(Type == GHOSTDATA_TYPE_CHARACTER)
		{
			if(FoundCharacterNoTick)
			{
				log_error_color(LOG_COLOR_GHOST, "ghost", "Failed to read ghost data: ghost character with and without tick cannot be mixed");
				Error = true;
				break;
			}
			else if(!pGhost->m_Path.ReadAndAdd([&](CGhostCharacter &Value) { return GhostLoader()->ReadData(Type, &Value, sizeof(CGhostCharacter)); }))
			{
				log_error_color(LOG_COLOR_GHOST, "ghost", "Failed to read ghost data: failed to read ghost character (with tick)");
				Error = true;
				break;
			}
			++Index;
			FoundCharacterTick = true;
		}
		else if(Type == GHOSTDATA_TYPE_START_TICK)
		{
			if(!GhostLoader()->ReadData(Type, &pGhost->m_StartTick, sizeof(int)))
			{
				log_error_color(LOG_COLOR_GHOST, "ghost", "Failed to read ghost data: failed to read start tick");
				Error = true;
				break;
			}
		}
	}

	GhostLoader()->Close();

	if(!Error && Index != pInfo->m_NumTicks)
	{
		log_error_color(LOG_COLOR_GHOST, "ghost", "Failed to read all ghost data (got '%d' ticks, wanted '%d' ticks)", Index, pInfo->m_NumTicks);
		Error = true;
	}

	if(Error)
	{
		pGhost->Reset();
		return -1;
	}

	if(FoundCharacterNoTick)
	{
		int StartTick = 0;
		for(int i = 1; i < pInfo->m_NumTicks; i++) // estimate start tick
			if(pGhost->m_Path.Get(i)->m_AttackTick != pGhost->m_Path.Get(i - 1)->m_AttackTick)
				StartTick = pGhost->m_Path.Get(i)->m_AttackTick - i;
		for(int i = 0; i < pInfo->m_NumTicks; i++)
			pGhost->m_Path.Get(i)->m_Tick = StartTick + i;
	}

	if(pGhost->m_StartTick == -1)
		pGhost->m_StartTick = pGhost->m_Path.Get(0)->m_Tick;

	if(!FoundSkin)
	{
		SetGhostSkinData(&pGhost->m_Skin, "default", 0, 0, 0);
	}
	str_copy(pGhost->m_aFilename, pFilename);
	UpdateTeeRenderInfo(*pGhost);

	return Slot;
}

void CGhost::Unload(int Slot)
{
	m_aManualSlots[Slot] = false;
	m_aManualRenderPosValid[Slot] = false;
	m_aActiveGhosts[Slot].Reset();
}

void CGhost::UnloadAll()
{
	for(int i = 0; i < MAX_ACTIVE_GHOSTS; i++)
		Unload(i);
}

void CGhost::SaveGhost(CMenus::CGhostItem *pItem)
{
	int Slot = pItem->m_Slot;
	if(!pItem->Active() || pItem->HasFile() || m_aActiveGhosts[Slot].Empty() || GhostRecorder()->IsRecording())
		return;

	CGhostItem *pGhost = &m_aActiveGhosts[Slot];

	int NumTicks = pGhost->m_Path.Size();
	char aFilename[IO_MAX_PATH_LENGTH];
	GetPath(aFilename, sizeof(aFilename), pItem->m_aPlayer, pItem->m_Time);
	if(GhostRecorder()->Start(aFilename, Client()->GetCurrentMap(), GameClient()->Map()->Sha256(), pItem->m_aPlayer) != 0)
		return;

	GhostRecorder()->WriteData(GHOSTDATA_TYPE_START_TICK, &pGhost->m_StartTick, sizeof(int));
	GhostRecorder()->WriteData(GHOSTDATA_TYPE_SKIN, &pGhost->m_Skin, sizeof(CGhostSkin));
	for(int i = 0; i < NumTicks; i++)
		GhostRecorder()->WriteData(GHOSTDATA_TYPE_CHARACTER, pGhost->m_Path.Get(i), sizeof(CGhostCharacter));

	GhostRecorder()->Stop(NumTicks, pItem->m_Time);
	if(GhostRecorder()->HasWriteError())
	{
		Storage()->RemoveFile(aFilename, IStorage::TYPE_SAVE);
		log_error("ghost", "failed to save ghost file '%s'", aFilename);
		return;
	}
	str_copy(pItem->m_aFilename, aFilename);
	str_copy(pGhost->m_aFilename, aFilename);
}

void CGhost::ConGPlay(IConsole::IResult *pResult, void *pUserData)
{
	CGhost *pGhost = (CGhost *)pUserData;
	pGhost->StartRender(pGhost->Client()->PredGameTick(g_Config.m_ClDummy));
}

void CGhost::OnConsoleInit()
{
	m_pGhostLoader = Kernel()->RequestInterface<IGhostLoader>();
	m_pGhostRecorder = Kernel()->RequestInterface<IGhostRecorder>();

	Console()->Register("gplay", "", CFGFLAG_CLIENT, ConGPlay, this, "Start playback of ghosts");
}

void CGhost::OnMessage(int MsgType, void *pRawMsg)
{
	// check for messages from server
	if(MsgType == NETMSGTYPE_SV_KILLMSG)
	{
		CNetMsg_Sv_KillMsg *pMsg = (CNetMsg_Sv_KillMsg *)pRawMsg;
		if(pMsg->m_Victim == GameClient()->m_Snap.m_LocalClientId)
		{
			if(m_Recording)
				StopRecord();
			// QmClient: 查看模式与玩家生死无关，继续播放
			if(!m_ManualMode)
				StopRender();
			m_LastDeathTick = Client()->GameTick(g_Config.m_ClDummy);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_KILLMSGTEAM)
	{
		CNetMsg_Sv_KillMsgTeam *pMsg = (CNetMsg_Sv_KillMsgTeam *)pRawMsg;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameClient()->m_Teams.Team(i) == pMsg->m_Team && i == GameClient()->m_Snap.m_LocalClientId)
			{
				if(m_Recording)
					StopRecord();
				if(!m_ManualMode)
					StopRender();
				m_LastDeathTick = Client()->GameTick(g_Config.m_ClDummy);
			}
		}
	}
	else if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		if(pMsg->m_ClientId == -1 && m_Recording)
		{
			char aName[MAX_NAME_LENGTH];
			int Time = CRaceHelper::TimeFromFinishMessage(pMsg->m_pMessage, aName, sizeof(aName));
			if(Time > 0 && GameClient()->m_Snap.m_LocalClientId >= 0 && str_comp(aName, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_aName) == 0)
			{
				if(m_Recording)
					StopRecord(Time);
				// QmClient: 查看模式不受本人完赛影响，继续独立时间线播放
				if(!m_ManualMode)
					StopRender();
			}
		}
	}
	else if(MsgType == NETMSGTYPE_SV_RACEFINISH)
	{
		CNetMsg_Sv_RaceFinish *pMsg = (CNetMsg_Sv_RaceFinish *)pRawMsg;
		if(m_Recording && pMsg->m_ClientId == GameClient()->m_Snap.m_LocalClientId)
		{
			if(m_Recording)
				StopRecord(pMsg->m_Time);
			if(!m_ManualMode)
				StopRender();
		}
	}
}

void CGhost::OnReset()
{
	StopRecord();
	StopRender();
	m_LastDeathTick = -1;
}

void CGhost::OnShutdown()
{
	OnReset();
}

void CGhost::OnMapLoad()
{
	OnReset();
	UnloadAll();
	GameClient()->m_Menus.GhostlistPopulate();
	m_AllowRestart = false;
}
