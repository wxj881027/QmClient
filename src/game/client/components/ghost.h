/* (c) Rajh, Redix and Sushi. */

#ifndef GAME_CLIENT_COMPONENTS_GHOST_H
#define GAME_CLIENT_COMPONENTS_GHOST_H

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/online_replay_player.h>
#include <game/client/render.h>

struct CNetObj_Character;

enum
{
	GHOSTDATA_TYPE_SKIN = 0,
	GHOSTDATA_TYPE_CHARACTER_NO_TICK,
	GHOSTDATA_TYPE_CHARACTER,
	GHOSTDATA_TYPE_START_TICK
};

struct CGhostSkin
{
	int m_aSkin[6];
	int m_UseCustomColor;
	int m_ColorBody;
	int m_ColorFeet;
};

struct CGhostCharacter_NoTick
{
	int m_X;
	int m_Y;
	int m_VelX;
	int m_VelY;
	int m_Angle;
	int m_Direction;
	int m_Weapon;
	int m_HookState;
	int m_HookX;
	int m_HookY;
	int m_AttackTick;
};

struct CGhostCharacter : public CGhostCharacter_NoTick
{
	int m_Tick;
};

class CGhost : public CComponent
{
public:
	enum
	{
		MAX_ACTIVE_GHOSTS = 256,
	};

private:
	class CGhostPath
	{
		int m_ChunkSize;
		int m_NumItems;

		std::vector<CGhostCharacter *> m_vpChunks;

	public:
		CGhostPath() { Reset(); }
		~CGhostPath() { Reset(); }
		CGhostPath(const CGhostPath &Other) = delete;
		CGhostPath &operator=(const CGhostPath &Other) = delete;

		CGhostPath(CGhostPath &&Other) noexcept;
		CGhostPath &operator=(CGhostPath &&Other) noexcept;

		void Reset(int ChunkSize = 25 * 60); // one minute with default snap rate
		void SetSize(int Items);
		int Size() const { return m_NumItems; }

		void Add(const CGhostCharacter &Char);
		CGhostCharacter *Get(int Index);
	};

	class CGhostItem
	{
	public:
		std::shared_ptr<CManagedTeeRenderInfo> m_pManagedTeeRenderInfo;
		CGhostSkin m_Skin;
		CGhostPath m_Path;
		int m_StartTick;
		char m_aPlayer[MAX_NAME_LENGTH];
		int m_PlaybackPos;

		CGhostItem() { Reset(); }

		bool Empty() const { return m_Path.Size() == 0; }
		void Reset()
		{
			m_pManagedTeeRenderInfo = nullptr;
			m_Path.Reset();
			m_StartTick = -1;
			m_PlaybackPos = -1;
		}
	};

	static const char *ms_pGhostDir;

	class IGhostLoader *m_pGhostLoader = nullptr;
	class IGhostRecorder *m_pGhostRecorder = nullptr;

	CGhostItem m_aActiveGhosts[MAX_ACTIVE_GHOSTS];
	CGhostItem m_CurGhost;

	// QmClient: 查看模式要把所有虚影的 hook 先画完再画所有 Tee（对齐 demo 播放的玩家
	// 渲染顺序），所以先把本帧各虚影的插值数据算出来缓存，再统一绘制。
	// m_pSharedRenderInfo 指向影子自己的共享渲染信息；冻结/忍者换肤时用帧内副本。
	struct SGhostDrawData
	{
		CNetObj_Character m_Player;
		CNetObj_Character m_Prev;
		float m_IntraTick = 0.0f;
		int m_Slot = -1;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		bool m_UseOwnRenderInfo = false;
		CTeeRenderInfo m_OwnRenderInfo;
		const CTeeRenderInfo *m_pSharedRenderInfo = nullptr;
	};
	std::vector<SGhostDrawData> m_vGhostDraws;

	char m_aTmpFilename[IO_MAX_PATH_LENGTH] = "";

	int m_NewRenderTick = -1;
	int m_StartRenderTick = -1;
	int m_LastDeathTick = -1;
	bool m_Recording = false;
	bool m_Rendering = false;
	bool m_RenderingStartedByServer = false;

	// QmClient: 查看模式——影子按独立时间线播放（游戏内 demo 播放器），不与玩家跑图同步
	bool m_aManualSlots[MAX_ACTIVE_GHOSTS] = {};
	bool m_ManualMode = false;
	COnlineReplayClock m_ManualClock;
	int m_ManualPrimarySlot = -1;
	std::vector<int> m_vManualSampleTicks;
	int m_ManualEndTick = 0; // 本回放最长成员轨迹
	vec2 m_aManualRenderPos[MAX_ACTIVE_GHOSTS] = {}; // 每个槽位虚影当前插值位置（供镜头跟随）
	bool m_aManualRenderPosValid[MAX_ACTIVE_GHOSTS] = {};

	static void SetGhostSkinData(CGhostSkin *pSkin, const char *pSkinName, int UseCustomColor, int ColorBody, int ColorFeet);
	static void GetGhostCharacter(CGhostCharacter *pGhostChar, const CNetObj_Character *pChar, const CNetObj_DDNetCharacter *pDDnetChar);
	static void GetNetObjCharacter(CNetObj_Character *pChar, const CGhostCharacter *pGhostChar);

	void GetPath(char *pBuf, int Size, const char *pPlayerName, int Time = -1) const;

	void AddInfos(const CNetObj_Character *pChar, const CNetObj_DDNetCharacter *pDDnetChar);
	int GetSlot() const;

	void CheckStart();
	void CheckStartLocal(bool Predicted);
	void TryRenderStart(int Tick, bool ServerControl);

	void StartRecord(int Tick);
	void StopRecord(int Time = -1);
	void StopRender();

	void UpdateTeeRenderInfo(CGhostItem &Ghost);

	static void ConGPlay(IConsole::IResult *pResult, void *pUserData);

public:
	bool m_AllowRestart;

	int Sizeof() const override { return sizeof(*this); }

	void OnRender() override;
	void OnConsoleInit() override;
	void OnReset() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnMapLoad() override;
	void OnShutdown() override;
	void OnNewSnapshot() override;

	void OnNewPredictedSnapshot();

	// QmClient: 影子加载时玩家可能已经在跑图中，需要立即按当前 run 进度开始播放
	void StartRender(int Tick);

	// QmClient: 查看模式——独立时间线播放（进度/暂停/拖动由 CRankGhost 控制）
	void StartRenderManual(const std::vector<int> &vSlots);
	void ManualSetPlaying(bool Playing);
	void ManualSeek(int RelativeTick);
	void ManualSetSpeed(float Speed);
	float ManualSpeed() const { return m_ManualClock.Speed(); }
	void StopManual() { StopRender(); }
	bool ManualModeActive() const { return m_ManualMode; }
	bool ManualPlaying() const { return m_ManualClock.Playing(); }
	int ManualPlaybackTick() const;
	int ManualAdjacentTick(int Current, IDemoPlayer::ETickOffset Offset) const { return OnlineReplayAdjacentTick(m_vManualSampleTicks, Current, Offset); }
	// 手动播放的 tick 内插值相位（0–1）；暂停时冻结，不随本地预测波动（避免画面抖动）
	float ManualRenderIntra() const;
	int ManualEndTick() const { return m_ManualEndTick; }
	// 槽位虚影当前插值位置（查看模式下供镜头跟随选中成员）；false = 该槽位本帧未渲染
	bool GetManualRenderPos(int Slot, vec2 *pOut) const
	{
		if(Slot < 0 || Slot >= MAX_ACTIVE_GHOSTS || !m_aManualRenderPosValid[Slot])
			return false;
		*pOut = m_aManualRenderPos[Slot];
		return true;
	}
	// 槽位虚影的所有者名字（ghost 文件头）
	bool GetGhostPlayer(int Slot, char *pBuf, size_t BufSize) const
	{
		if(Slot < 0 || Slot >= MAX_ACTIVE_GHOSTS || m_aActiveGhosts[Slot].Empty())
			return false;
		str_copy(pBuf, m_aActiveGhosts[Slot].m_aPlayer, BufSize);
		return pBuf[0] != '\0';
	}

	// 旁观 HUD 读取回放成员的真实皮肤，不借用同 ID 的服务器玩家。
	bool GetGhostRenderInfo(int Slot, CTeeRenderInfo *pInfo) const
	{
		if(Slot < 0 || Slot >= MAX_ACTIVE_GHOSTS || m_aActiveGhosts[Slot].Empty())
			return false;
		if(!m_aActiveGhosts[Slot].m_pManagedTeeRenderInfo)
			return false;
		*pInfo = m_aActiveGhosts[Slot].m_pManagedTeeRenderInfo->TeeRenderInfo();
		return true;
	}

	int FreeSlots() const;
	int Load(const char *pFilename);
	void Unload(int Slot);
	void UnloadAll();

	void SaveGhost(CMenus::CGhostItem *pItem);

	const char *GetGhostDir() const { return ms_pGhostDir; }

	class IGhostLoader *GhostLoader() const { return m_pGhostLoader; }
	class IGhostRecorder *GhostRecorder() const { return m_pGhostRecorder; }
};

#endif
