/* (c) Rajh, Redix and Sushi. */

#include "ghost.h"

#include <base/log.h>

#include <engine/ghost.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <game/client/components/menus.h>
#include <game/client/components/players.h>
#include <game/client/components/skins.h>
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
	pGhostChar->m_VelY = 0;
	pGhostChar->m_Angle = pChar->m_Angle;
	pGhostChar->m_Direction = pChar->m_Direction;
	int Weapon = pChar->m_Weapon;
	if(pDDnetChar != nullptr && pDDnetChar->m_FreezeEnd != 0)
	{
		Weapon = WEAPON_NINJA;
	}
	pGhostChar->m_Weapon = Weapon;
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
	pChar->m_Direction = pGhostChar->m_Direction;
	pChar->m_Weapon = pGhostChar->m_Weapon;
	pChar->m_HookState = pGhostChar->m_HookState;
	pChar->m_HookX = pGhostChar->m_HookX;
	pChar->m_HookY = pGhostChar->m_HookY;
	pChar->m_AttackTick = pGhostChar->m_AttackTick;
	pChar->m_HookedPlayer = -1;
	pChar->m_Tick = pGhostChar->m_Tick;
}

CGhost::CGhostPath::CGhostPath(CGhostPath &&Other) noexcept :
	m_ChunkSize(Other.m_ChunkSize), m_NumItems(Other.m_NumItems), m_vpChunks(std::move(Other.m_vpChunks))
{
	Other.m_NumItems = 0;
	Other.m_vpChunks.clear();
}

CGhost::CGhostPath &CGhost::CGhostPath::operator=(CGhostPath &&Other) noexcept
{
	Reset(Other.m_ChunkSize);
	m_NumItems = Other.m_NumItems;
	m_vpChunks = std::move(Other.m_vpChunks);
	Other.m_NumItems = 0;
	Other.m_vpChunks.clear();
	return *this;
}

void CGhost::CGhostPath::Reset(int ChunkSize)
{
	for(auto &pChunk : m_vpChunks)
		free(pChunk);
	m_vpChunks.clear();
	m_ChunkSize = ChunkSize;
	m_NumItems = 0;
}

void CGhost::CGhostPath::SetSize(int Items)
{
	int Chunks = m_vpChunks.size();
	int NeededChunks = (Items + m_ChunkSize - 1) / m_ChunkSize;

	if(NeededChunks > Chunks)
	{
		m_vpChunks.resize(NeededChunks);
		for(int i = Chunks; i < NeededChunks; i++)
			m_vpChunks[i] = (CGhostCharacter *)calloc(m_ChunkSize, sizeof(CGhostCharacter));
	}

	m_NumItems = Items;
}

void CGhost::CGhostPath::Add(const CGhostCharacter &Char)
{
	SetSize(m_NumItems + 1);
	*Get(m_NumItems - 1) = Char;
}

CGhostCharacter *CGhost::CGhostPath::Get(int Index)
{
	if(Index < 0 || Index >= m_NumItems)
		return nullptr;

	int Chunk = Index / m_ChunkSize;
	int Pos = Index % m_ChunkSize;
	return &m_vpChunks[Chunk][Pos];
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
		GhostRecorder()->Start(m_aTmpFilename, Client()->GetCurrentMap(), GameClient()->Map()->Sha256(), m_CurGhost.m_aPlayer);

		GhostRecorder()->WriteData(GHOSTDATA_TYPE_START_TICK, &m_CurGhost.m_StartTick, sizeof(int));
		GhostRecorder()->WriteData(GHOSTDATA_TYPE_SKIN, &m_CurGhost.m_Skin, sizeof(CGhostSkin));
		for(int i = 0; i < NumTicks; i++)
			GhostRecorder()->WriteData(GHOSTDATA_TYPE_CHARACTER, m_CurGhost.m_Path.Get(i), sizeof(CGhostCharacter));
	}

	CGhostCharacter GhostChar;
	GetGhostCharacter(&GhostChar, pChar, pDDnetChar);
	m_CurGhost.m_Path.Add(GhostChar);
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
		int PrevTick = GameClient()->m_Snap.m_pLocalPrevCharacter->m_Tick;
		int CurTick = GameClient()->m_Snap.m_pLocalCharacter->m_Tick;
		vec2 PrevPos = vec2(GameClient()->m_Snap.m_pLocalPrevCharacter->m_X, GameClient()->m_Snap.m_pLocalPrevCharacter->m_Y);
		vec2 Pos = vec2(GameClient()->m_Snap.m_pLocalCharacter->m_X, GameClient()->m_Snap.m_pLocalCharacter->m_Y);

		// detecting death, needed because race allows immediate respawning
		if((!m_Recording || m_AllowRestart) && m_LastDeathTick < PrevTick)
		{
			// estimate the exact start tick
			int RecordTick = -1;
			int TickDiff = CurTick - PrevTick;
			for(int i = 0; i < TickDiff; i++)
			{
				if(GameClient()->RaceHelper()->IsStart(mix(PrevPos, Pos, (float)i / TickDiff), mix(PrevPos, Pos, (float)(i + 1) / TickDiff)))
				{
					RecordTick = PrevTick + i + 1;
					if(!m_AllowRestart)
						break;
				}
			}
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

void CGhost::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	// Play the ghost
	if(!m_Rendering || !g_Config.m_ClRaceShowGhost)
		return;

	int PlaybackTick;
	if(m_ManualMode)
	{
		// 查看模式：独立时间线，播到尽头自动停在最后一帧
		PlaybackTick = ManualPlaybackTick();
		if(m_ManualPlaying && m_ManualEndTick > 0 && PlaybackTick >= m_ManualEndTick)
		{
			ManualSeek(m_ManualEndTick);
			PlaybackTick = m_ManualBaseTick;
		}
	}
	else
		PlaybackTick = Client()->PredGameTick(g_Config.m_ClDummy) - m_StartRenderTick;

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
		if(Ghost.Empty())
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

		int TickDiff = Draw.m_Player.m_Tick - Draw.m_Prev.m_Tick;
		if(TickDiff > 0)
		{
			// QmClient：查看模式使用手动时间线的插值相位；本地预测相位会持续波动，
			// 暂停/播完时直接沿用会让虚影在末段两点间抖动
			const float IntraPhase = m_ManualMode ? ManualRenderIntra() : Client()->PredIntraGameTick(g_Config.m_ClDummy);
			Draw.m_IntraTick = (GhostTick - Draw.m_Prev.m_Tick - 1 + IntraPhase) / TickDiff;
		}

		Draw.m_Player.m_AttackTick += Client()->GameTick(g_Config.m_ClDummy) - GhostTick;

		// QmClient：转换侧以 WEAPON_NINJA 标记冻结段（幽灵格式无独立冻结字段）。
		// 冻结表现完全复刻 demo 播放的玩家渲染（players.cpp）：冰冻着色 + 隐藏武器、
		// 旧版卡塔纳手随 TClient 的 tc_frozen_katana、忍者皮肤随 cl_show_ninja
		const bool GhostFrozen = Draw.m_Player.m_Weapon == WEAPON_NINJA;
		unsigned FrozenFlags = 0;
		int RenderWeapon = Draw.m_Player.m_Weapon;
		if(GhostFrozen)
		{
			FrozenFlags = TEE_EFFECT_FROZEN | TEE_NO_WEAPON;
			// 数据中武器被用作冻结标记，还原为原始武器（DDRace 默认为锤）；
			// 开启 tc_frozen_katana 时保留忍者武器以复现旧版卡塔纳手
			RenderWeapon = WEAPON_HAMMER;
			if(g_Config.m_TcFreezeKatana > 0)
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

					// 与玩家侧 TClient 的彩色冻结皮肤一致（tc_color_freeze）
					if(g_Config.m_TcColorFreeze)
					{
						const CTeeRenderInfo &OwnInfo = *Draw.m_pSharedRenderInfo;
						Draw.m_OwnRenderInfo.m_CustomColoredSkin = OwnInfo.m_CustomColoredSkin;
						Draw.m_OwnRenderInfo.m_ColorFeet = g_Config.m_TcColorFreezeFeet ? OwnInfo.m_ColorFeet : ColorRGBA(1, 1, 1);
						const float Darken = (g_Config.m_TcColorFreezeDarken / 100.0f) * 0.5f + 0.5f;
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

	// 查看模式：全部 hook -> 全部 Tee -> 名字与方向指示叠加
	for(const SGhostDrawData &Draw : m_vGhostDraws)
		GameClient()->m_Players.RenderHook(ScreenRect, &Draw.m_Prev, &Draw.m_Player, ResolveRenderInfo(Draw), -2, Draw.m_IntraTick);
	for(const SGhostDrawData &Draw : m_vGhostDraws)
		GameClient()->m_Players.RenderPlayer(ScreenRect, &Draw.m_Prev, &Draw.m_Player, ResolveRenderInfo(Draw), -2, Draw.m_IntraTick);

	// QmClient: 查看模式下为每个虚影标注名字（多轨回放的信息可读性，对齐 demo 播放器）
	// 名字尺寸跟随客户端名字牌大小设置；方向键指示与名字牌共用 cl_show_direction 数据
	for(const SGhostDrawData &Draw : m_vGhostDraws)
	{
		const CGhostItem &Ghost = m_aActiveGhosts[Draw.m_Slot];
		const vec2 TeePos = Draw.m_Pos;
		if(Ghost.m_aPlayer[0] != '\0')
		{
			const float NameFontSize = 18.0f + 20.0f * g_Config.m_ClNamePlatesSize / 100.0f;
			const float NameWidth = TextRender()->TextWidth(NameFontSize, Ghost.m_aPlayer, -1);
			TextRender()->TextColor(1, 1, 1, 1);
			TextRender()->TextOutlineColor(0, 0, 0, 0.6f);
			TextRender()->Text(TeePos.x - NameWidth / 2, TeePos.y - 56.0f, NameFontSize, Ghost.m_aPlayer);
		}

		// QmClient: 查看模式方向键指示（面板开关持久化于 qm_rank_ghost_show_direction），
		// 数据来自影子路径的完整角色快照，与名字牌的方向指示同一来源
		if(!g_Config.m_QmRankGhostShowDirection)
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
	const bool StoreGhost = Time > 0 && (!pOwnGhost || Time < pOwnGhost->m_Time || !g_Config.m_ClRaceGhostSaveBest);

	if(RecordingToFile)
		GhostRecorder()->Stop(m_CurGhost.m_Path.Size(), StoreGhost ? Time : -1);

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
		if(RecordingToFile)
			GetPath(Item.m_aFilename, sizeof(Item.m_aFilename), m_CurGhost.m_aPlayer, Time);
		str_copy(Item.m_aPlayer, m_CurGhost.m_aPlayer);
		Item.m_Time = Time;
		Item.m_Slot = Slot;

		// save new ghost file
		if(Item.HasFile())
			Storage()->RenameFile(m_aTmpFilename, Item.m_aFilename, IStorage::TYPE_SAVE);

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
	m_ManualPlaying = false;
	std::fill(std::begin(m_aManualRenderPosValid), std::end(m_aManualRenderPosValid), false);
}

void CGhost::ManualSetSpeed(float Speed)
{
	if(!m_ManualMode)
		return;
	Speed = std::clamp(Speed, 0.1f, 4.0f);
	if(m_ManualSpeed == Speed)
		return;
	if(m_ManualPlaying)
	{
		// 以当前播放头为基准重整时间轴，变速瞬间不跳帧
		const float Elapsed = ManualElapsedTicks();
		m_ManualBaseTick = (int)Elapsed;
		m_ManualStartTime = Client()->LocalTime();
	}
	m_ManualSpeed = Speed;
}

void CGhost::StartRenderManual()
{
	bool HaveGhost = false;
	m_ManualEndTick = 0;
	for(auto &Ghost : m_aActiveGhosts)
	{
		if(Ghost.Empty())
			continue;
		HaveGhost = true;
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
	m_ManualPlaying = true;
	m_ManualBaseTick = 0;
	m_ManualStartTime = Client()->LocalTime();
	std::fill(std::begin(m_aManualRenderPosValid), std::end(m_aManualRenderPosValid), false);
}

int CGhost::ManualPlaybackTick() const
{
	if(!m_ManualMode)
		return 0;
	if(!m_ManualPlaying)
		return m_ManualBaseTick;
	return m_ManualBaseTick + (int)ManualElapsedTicks();
}

float CGhost::ManualElapsedTicks() const
{
	// 相对手动播放头的 tick 数（含小数相位），按倍速缩放。
	// 用 LocalTime（单调）而非本地预测 tick：预测会回滚，导致播放头倒退、
	// 快照游标（只前进）与 tick 错位，表现为虚影闪烁/左右乱跳。
	return maximum(0.0f, (Client()->LocalTime() - m_ManualStartTime) * (float)Client()->GameTickSpeed() * m_ManualSpeed);
}

float CGhost::ManualRenderIntra() const
{
	if(!m_ManualMode)
		return 0.0f;
	if(!m_ManualPlaying)
		return m_ManualPauseIntra;
	const float Elapsed = ManualElapsedTicks();
	return Elapsed - (int)Elapsed;
}

void CGhost::ManualSetPlaying(bool Playing)
{
	if(!m_ManualMode || Playing == m_ManualPlaying)
		return;
	if(Playing)
	{
		m_ManualStartTime = Client()->LocalTime();
	}
	else
	{
		// 暂停时把播放头冻结在当前进度，并锁存 tick 内相位：
		// 本地预测的插值相位会持续波动，直接沿用会让暂停后的画面抖动
		const float Elapsed = ManualElapsedTicks();
		m_ManualBaseTick = (int)Elapsed;
		m_ManualPauseIntra = Elapsed - (int)Elapsed;
	}
	m_ManualPlaying = Playing;
}

void CGhost::ManualSeek(int RelativeTick)
{
	if(!m_ManualMode)
		return;
	const int MaxTick = maximum(1, m_ManualEndTick);
	m_ManualBaseTick = std::clamp(RelativeTick, 0, MaxTick);
	m_ManualStartTime = Client()->LocalTime();
	m_ManualPauseIntra = 0.0f;
	if(m_ManualBaseTick >= MaxTick)
		m_ManualPlaying = false;

	// 快照游标只前进，seek 后必须重扫定位
	const int PlaybackTick = m_ManualBaseTick;
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
	pGhost->m_Path.SetSize(pInfo->m_NumTicks);

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
			else if(!GhostLoader()->ReadData(Type, pGhost->m_Path.Get(Index++), sizeof(CGhostCharacter_NoTick)))
			{
				log_error_color(LOG_COLOR_GHOST, "ghost", "Failed to read ghost data: failed to read ghost character (without tick)");
				Error = true;
				break;
			}
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
			else if(!GhostLoader()->ReadData(Type, pGhost->m_Path.Get(Index++), sizeof(CGhostCharacter)))
			{
				log_error_color(LOG_COLOR_GHOST, "ghost", "Failed to read ghost data: failed to read ghost character (with tick)");
				Error = true;
				break;
			}
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
	UpdateTeeRenderInfo(*pGhost);

	return Slot;
}

void CGhost::Unload(int Slot)
{
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
	GetPath(pItem->m_aFilename, sizeof(pItem->m_aFilename), pItem->m_aPlayer, pItem->m_Time);
	GhostRecorder()->Start(pItem->m_aFilename, Client()->GetCurrentMap(), GameClient()->Map()->Sha256(), pItem->m_aPlayer);

	GhostRecorder()->WriteData(GHOSTDATA_TYPE_START_TICK, &pGhost->m_StartTick, sizeof(int));
	GhostRecorder()->WriteData(GHOSTDATA_TYPE_SKIN, &pGhost->m_Skin, sizeof(CGhostSkin));
	for(int i = 0; i < NumTicks; i++)
		GhostRecorder()->WriteData(GHOSTDATA_TYPE_CHARACTER, pGhost->m_Path.Get(i), sizeof(CGhostCharacter));

	GhostRecorder()->Stop(NumTicks, pItem->m_Time);
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
