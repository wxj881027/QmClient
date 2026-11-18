/* (c) Rajh, Redix and Sushi. */

#ifndef GAME_CLIENT_COMPONENTS_GHOST_H
#define GAME_CLIENT_COMPONENTS_GHOST_H

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/ghost_extra_data.h>
#include <game/client/components/qmclient/ghost_freeze_bar.h>
#include <game/client/components/qmclient/ghost_path.h>
#include <game/client/components/qmclient/local_world.h>
#include <game/client/components/qmclient/online_replay_player.h>
#include <game/client/render.h>

#include <array>
#include <optional>
#include <vector>

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
	using CGhostPath = qm_ghost_path::CChunkedPath<CGhostCharacter>;

	class CGhostItem
	{
	public:
		std::shared_ptr<CManagedTeeRenderInfo> m_pManagedTeeRenderInfo;
		CGhostSkin m_Skin;
		CGhostPath m_Path;
		int m_StartTick;
		char m_aPlayer[MAX_NAME_LENGTH];
		// 槽位实际装载的文件。地图重载后菜单的旧列表项会按槽位复用，
		// 只判断“槽位非空”会把别的影子误标成已激活。
		char m_aFilename[IO_MAX_PATH_LENGTH];
		int m_PlaybackPos;

		CGhostItem() { Reset(); }

		bool Empty() const { return m_Path.Size() == 0; }
		void Reset()
		{
			m_pManagedTeeRenderInfo = nullptr;
			m_Path.Reset();
			m_StartTick = -1;
			m_PlaybackPos = -1;
			m_aFilename[0] = '\0';
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
		// 查看模式：挥锤/开枪相位（秒），由回放播放头换算；跑图模式不用。
		std::optional<float> m_AttackTimeOverride;
		// 新转换的影子把 DDNetCharacter 的 StrongWeakId 打包在方向字段高位。
		bool m_HasStrongWeak = false;
		int m_StrongWeakId = 0;
	};
	std::vector<SGhostDrawData> m_vGhostDraws;

	// 查看模式的冻结段：幽灵格式没有冻结起止字段，新格式在 m_VelY 写显式冻结
	// 标记（见 ghost_freeze_bar.h），旧格式才回退到 WEAPON_NINJA 判定。播放开始时
	// 扫一次路径还原成相对 tick 区间，用来画与实机一致的冻结进度条
	// （cl_show_freeze_bars）。区间与进度计算在 ghost_freeze_bar.h，渲染由
	// CFreezeBars::RenderBarAt 负责。
	std::array<std::vector<SGhostFreezeSpan>, MAX_ACTIVE_GHOSTS> m_aManualFreezeSpans;
	void BuildManualFreezeSpans(const std::vector<int> &vSlots);

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
	int m_ManualFocusSlot = -1;
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

	// 分支世界时钟与幽灵播放头的换算基准。
	int RunningPlaybackStartTick() const { return m_StartRenderTick; }

	// 快速练习线路：把分叉时空里录下的多轨连续线路 + 交互事件写成一个影子组
	// （组目录内 trackN.gho 与 events.qmevt），与 Rank 1 组回放共用同一套格式。
	// 保存不等于播放：用户在影子页点 View 才进入 demo 式查看模式，不退出服务器。
	// 不走 StopRecord 的"个人最佳"逻辑——练习段时长不是成绩；玩家名带 [P] 标记。
	bool SavePracticeTrack(const char *pName, const SLocalSegment &Segment);

	// 当前地图可回放的练习影子组（ghosts/<dir>/events.qmevt）。菜单面板用它生成
	// “直接播放”入口；只列出能读出 track0.gho 文件头且属于当前地图的组。
	struct SPracticeGroup
	{
		char m_aDir[IO_MAX_PATH_LENGTH] = "";
		char m_aOwner[MAX_NAME_LENGTH] = "";
		int m_Time = 0;
		// 目录修改时间，仅用于"最近的排前面"。练习组没有成绩语义，按时间排就够。
		int64_t m_TimeModified = 0;
	};
	void ListPracticeGroups(std::vector<SPracticeGroup> &vOut) const;

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
	// 冻结条必须画在玩家与地图前景之后，否则会被前景瓦片盖住。
	void RenderManualFreezeBars();
	// 边缘定位箭头跟随当前查看目标；多成员回放里切换成员后不再只指向主轨。
	void ManualSetFocusSlot(int Slot);
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
	// 任意回放 tick 的轨迹位置（线性插值）。影子机关按射击时刻查询目标，
	// 不能只拿当前帧位置反推过去。
	bool GetManualRenderPosAtTick(int Slot, int Tick, vec2 *pOut) const;
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
	// 菜单保存的槽位可能跨地图重载失效，写回前必须以引擎当前状态为准。
	bool IsSlotActive(int Slot) const;
	// 槽位里装的必须仍是同一个文件，不能只看“槽位非空”。
	bool IsSlotLoadedWith(int Slot, const char *pFilename) const;
	int Load(const char *pFilename);
	void Unload(int Slot);
	void UnloadAll();

	void SaveGhost(CMenus::CGhostItem *pItem);

	const char *GetGhostDir() const { return ms_pGhostDir; }

	class IGhostLoader *GhostLoader() const { return m_pGhostLoader; }
	class IGhostRecorder *GhostRecorder() const { return m_pGhostRecorder; }
};

#endif
