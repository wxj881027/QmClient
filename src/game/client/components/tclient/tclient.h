#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H

#include <base/color.h>
#include <base/hash.h>

#include <engine/client/enums.h>
#include <engine/external/regex.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/console.h>
#include <engine/shared/json.h>
#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/friend_enter_tracker.h>
#include <game/client/components/qmclient/friend_online_tracker.h>
#include <game/client/components/qmclient/local_saves.h>
#include <game/client/components/qmclient/map_progress.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/red_packet_auto_claim.h>
#include <game/client/components/qmclient/route_start_index.h>
#include <game/client/components/qmclient/route_visited.h>
#include <game/client/components/qmclient/update_manifest.h>
#include <game/client/components/tclient/map_history.h>
#include <game/client/components/tclient/swap_countdown_message.h>

#include <deque>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class IJob;
struct CNetMsg_Sv_Chat;

// 玩家统计数据结构
struct SPlayerStats
{
	// 存活时长统计
	int m_TotalAliveTime = 0; // 总存活时间（tick）
	int m_MaxAliveTime = 0; // 最大存活时间（tick）
	int m_AliveCount = 0; // 存活次数（用于计算平均）
	int m_CurrentAliveStart = 0; // 当前存活开始时间（tick）
	bool m_IsAlive = false; // 当前是否存活（未被freeze）
	float m_FreezeX = 0.0f; // 被冻结时的X位置
	float m_FreezeY = 0.0f; // 被冻结时的Y位置

	// 被救/落水统计
	int m_RescueCount = 0; // 被救醒次数（被别人解冻）
	int m_FreezeCount = 0; // 落水次数（自己被冻结）

	// 出钩统计
	int m_HookLeftCount = 0; // 向左出钩次数
	int m_HookRightCount = 0; // 向右出钩次数
	bool m_WasHooking = false; // 上一帧是否在出钩

	void Reset()
	{
		m_TotalAliveTime = 0;
		m_MaxAliveTime = 0;
		m_AliveCount = 0;
		m_CurrentAliveStart = 0;
		m_IsAlive = false;
		m_FreezeX = 0.0f;
		m_FreezeY = 0.0f;
		m_RescueCount = 0;
		m_FreezeCount = 0;
		m_HookLeftCount = 0;
		m_HookRightCount = 0;
		m_WasHooking = false;
	}

	float GetAverageAliveTime(int TickSpeed) const
	{
		if(m_AliveCount == 0)
			return 0.0f;
		return (float)m_TotalAliveTime / (float)m_AliveCount / (float)TickSpeed;
	}

	float GetMaxAliveTime(int TickSpeed) const
	{
		return (float)m_MaxAliveTime / (float)TickSpeed;
	}

	float GetCurrentAliveTime(int CurrentTick, int TickSpeed) const
	{
		if(!m_IsAlive || m_CurrentAliveStart == 0)
			return 0.0f;
		int AliveTime = CurrentTick - m_CurrentAliveStart;
		return (float)AliveTime / (float)TickSpeed;
	}

	float GetHookLeftRatio() const
	{
		int Total = m_HookLeftCount + m_HookRightCount;
		if(Total == 0)
			return 0.5f;
		return (float)m_HookLeftCount / (float)Total;
	}

	float GetHookRightRatio() const
	{
		return 1.0f - GetHookLeftRatio();
	}
};

class CTClient : public CComponent
{
	std::deque<vec2> m_aAirRescuePositions[NUM_DUMMIES];
	void AirRescue();
	static void ConAirRescue(IConsole::IResult *pResult, void *pUserData);

	static void ConCalc(IConsole::IResult *pResult, void *pUserData);
	static void ConRandomTee(IConsole::IResult *pResult, void *pUserData);
	static void ConchainRandomColor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void RandomBodyColor();
	static void RandomFeetColor();
	static void RandomSkin(void *pUserData);
	static void RandomFlag(void *pUserData);

	static void ConSpecId(IConsole::IResult *pResult, void *pUserData);
	void SpecId(int ClientId);

	// 单刷模式：一键把本体与分身分到两个空闲 team；已分队时一键回 team 0。
	static void ConSoloSplit(IConsole::IResult *pResult, void *pUserData);
	void SoloSplitToggle();

	int m_EmoteCycle = 0;
	static void ConEmoteCycle(IConsole::IResult *pResult, void *pUserData);

	class IEngineGraphics *m_pGraphics = nullptr;

	char m_PreviousOwnMessage[2048] = {};

	bool SendNonDuplicateMessage(int Team, const char *pLine);
	void TryAppendKeywordReplyRenameSuffix(bool UseDummy);

	float m_FinishTextTimeout = 0.0f;
	bool m_aFinishRenamePending[NUM_DUMMIES] = {false, false};
	int m_aFinishRenameAttempts[NUM_DUMMIES] = {0, 0};
	int64_t m_aFinishRenamePendingSince[NUM_DUMMIES] = {0, 0};
	char m_aaFinishRenameTarget[NUM_DUMMIES][MAX_NAME_LENGTH] = {};
	void ResetFinishRenameState(int Dummy = -1);
	void DoFinishCheck();
	const char *CurrentCommunityIdForFinishCheck() const;
	void StartUpdateDownload();
	void ResetUpdateDownloadTasks();
	void RemoveUpdateTempFiles();
	bool LaunchUpdateInstaller();
	void ResetUpdateTasks();
	void StartUpdateCheckIfDue();
	void FinishUpdateDownloads();

	bool ServerCommandExists(const char *pCommand);
	int64_t m_LastAutoReplyTime = 0;
	CQmRedPacketAutoClaim m_RedPacketAutoClaim;
	bool TryHandleRedPacketAutoClaim(const CNetMsg_Sv_Chat *pMsg);

	// Water Fall Detection
	bool m_aWasInDeath[NUM_DUMMIES] = {false, false};
	int64_t m_aLastWaterFallTime[NUM_DUMMIES] = {0, 0};
	int64_t m_aLastWaterHeartTime[NUM_DUMMIES] = {0, 0}; // 添加爱心发送时间记录
	int64_t m_aLastWaterMessageTime[NUM_DUMMIES] = {0, 0}; // 添加消息发送时间记录
	void CheckWaterFall();

	// Freeze Detection
	bool m_aWasInFreeze[NUM_DUMMIES] = {false, false};
	int64_t m_aLastFreezeEmoteTime[NUM_DUMMIES] = {0, 0};
	int64_t m_aLastFreezeMessageTime[NUM_DUMMIES] = {0, 0};
	void CheckFreeze();

	struct SFreezeWakeupPopup
	{
		bool m_Active = false;
		int m_AnchorClientId = -1;
		int m_TextType = 0;
		float m_StartTime = 0.0f;
		float m_HorizontalSign = 1.0f;
		float m_ColorPhase = 0.0f;
		bool m_UseRollingColor = false;
		ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	};
	static constexpr int FREEZE_WAKEUP_POPUP_MAX = 8;
	static constexpr int TEXT_POPUP_TEXTURE_MAX = 1;
	struct STextPopupCache
	{
		STextContainerIndex m_TextContainerIndex;
		vec2 m_TextSize = vec2(0.0f, 0.0f);
	};
	SFreezeWakeupPopup m_aFreezeWakeupPopups[FREEZE_WAKEUP_POPUP_MAX];
	STextPopupCache m_aTextPopupCaches[TEXT_POPUP_TEXTURE_MAX];
	char m_aTextPopupFont[256] = "";
	void CheckHammerWakeupActions();
	void CheckComboPopup();
	void AddFreezeWakeupPopup(int WokenDummy);
	bool EnsureTextPopupCache(int TextType);
	bool AddTextPopup(int AnchorClientId, int TextType, bool UseRollingColor, ColorRGBA Color);
	void UnloadTextPopupCaches();
	void ClearFreezeWakeupPopups();
	void ResetComboState(int Dummy = -1);
	int m_aComboPopupCount[NUM_DUMMIES] = {0, 0};
	int m_aComboLastEventTick[NUM_DUMMIES] = {-1, -1};
	int m_aaComboLastHammerHitSnapshotTick[NUM_DUMMIES][MAX_CLIENTS] = {};
	int m_aComboLastHookedPlayer[NUM_DUMMIES] = {-1, -1};

	// Auto Switch on Unfreeze (HJ大佬辅助)
	bool m_aWasInFreezeForSwitch[NUM_DUMMIES] = {false, false};
	void CheckAutoSwitchOnUnfreeze();
	bool m_aWasInFreezeForGoresHammer[NUM_DUMMIES] = {false, false};
	bool m_aGoresHammerWakeupFirePendingRelease[NUM_DUMMIES] = {false, false};

	// Auto Close Chat on Unfreeze (HJ大佬辅助)
	bool m_aWasInFreezeForChatClose[NUM_DUMMIES] = {false, false};
	void CheckAutoCloseChatOnUnfreeze();

	// 玩家统计跟踪
	SPlayerStats m_aPlayerStats[NUM_DUMMIES];
	int m_aLastGameplayLogicTick[NUM_DUMMIES] = {-1, -1};
	void UpdatePlayerStats();
	void TrackHookDirection(int Dummy);

	// 地图进度：保留 Gores 距离场，DDRace 使用计时 CP 分段路径场。
	QmMapProgress::CMap m_QmDDraceProgressMap;
	QmMapProgress::CPlayer m_aQmDDraceProgress[NUM_DUMMIES];
	const void *m_pQmDDraceProgressGame = nullptr;
	const void *m_pQmDDraceProgressFront = nullptr;
	const void *m_pQmDDraceProgressTele = nullptr;
	int m_QmDDraceProgressWidth = 0;
	int m_QmDDraceProgressScanCursor = 0;
	int m_aQmDDraceProgressClientId[NUM_DUMMIES] = {-1, -1};
	int m_aQmDDraceTeleCheckpoint[NUM_DUMMIES] = {0, 0};
	vec2 m_aQmDDraceProgressPreviousPos[NUM_DUMMIES] = {};
	bool m_aQmDDraceProgressHasPreviousPos[NUM_DUMMIES] = {false, false};
	bool IsDDraceMapProgressMap() const;
	void ResetDDraceMapProgress();
	void UpdateDDraceMapProgress();

	enum class EGoresDistanceFieldBuildStage
	{
		IDLE,
		SCAN_TILES,
		SCAN_VISUAL_LAYERS,
		INIT_QUEUE,
		DIJKSTRA,
		CHECK_REACHABLE_START,
	};
	bool m_GoresDistanceFieldValid = false;
	bool m_GoresDistanceFieldAttempted = false;
	int64_t m_GoresDistanceFieldNextBuildTryTick = 0;
	char m_aGoresDistanceFieldMap[128] = "";
	int m_GoresDistanceFieldWidth = 0;
	int m_GoresDistanceFieldHeight = 0;
	std::vector<unsigned char> m_vGoresCMap; // 0=normal 1=blocked 2=tele 3=penalty 4=reward
	std::vector<std::vector<int>> m_vvGoresDirectTeleOuts;
	std::vector<int> m_vGoresDistanceToFinish;
	// 路线显示的两处每帧开销：起点靠扫描全图、访问位图整张分配并清零。
	// 前者沿用距离场已有的递增地图扫描记录潜在起点，后者只清理上一条路径触及的位图字。
	CQmRouteStartIndex m_GoresRouteStartIndex;
	mutable CQmRouteVisited m_GoresDebugRouteVisited;
	EGoresDistanceFieldBuildStage m_GoresDistanceFieldBuildStage = EGoresDistanceFieldBuildStage::IDLE;
	int m_GoresDistanceFieldBuildMapSize = 0;
	int m_GoresDistanceFieldBuildCursor = 0;
	int m_GoresDistanceFieldBuildGroup = 0;
	int m_GoresDistanceFieldBuildLayer = 0;
	int m_GoresDistanceFieldBuildLoadedVisualLayerData = -1;
	bool m_GoresDistanceFieldBuildHadStart = false;
	const void *m_pGoresDistanceFieldBuildMap = nullptr;
	const void *m_pGoresDistanceFieldBuildGameLayer = nullptr;
	const void *m_pGoresDistanceFieldBuildFrontLayer = nullptr;
	const void *m_pGoresDistanceFieldBuildTeleLayer = nullptr;
	int m_GoresDistanceFieldBuildPendingTeleNumber = 0;
	int m_GoresDistanceFieldBuildPendingTeleCursor = 0;
	int m_GoresDistanceFieldBuildPendingTeleDistance = 0;
	std::vector<unsigned char> m_vGoresDistanceFieldBuildPassable;
	std::vector<unsigned char> m_vGoresDistanceFieldBuildImageSemantics;
	std::vector<int> m_vGoresDistanceFieldBuildFinishIndices;
	std::vector<std::vector<int>> m_vvGoresDistanceFieldBuildDirectTeleInputs;
	using TGoresDistanceNode = std::pair<int, int>;
	std::priority_queue<TGoresDistanceNode, std::vector<TGoresDistanceNode>, std::greater<TGoresDistanceNode>> m_GoresDistanceFieldBuildQueue;
	bool m_aGoresWasOnStartLastTick[NUM_DUMMIES] = {false, false};
	bool m_aGoresRunStarted[NUM_DUMMIES] = {false, false};
	int m_aGoresRunStartDistanceToFinish[NUM_DUMMIES] = {0, 0};
	bool m_aGoresMapProgressValid[NUM_DUMMIES] = {false, false};
	float m_aGoresMapProgress[NUM_DUMMIES] = {0.0f, 0.0f};
	int m_aGoresPreHammerWeapon[NUM_DUMMIES] = {WEAPON_GUN, WEAPON_GUN};
	bool m_aGoresHasPreHammerWeapon[NUM_DUMMIES] = {false, false};
	bool m_aPrevFireForGores[NUM_DUMMIES] = {false, false};
	bool IsGoresGameMode() const;
	bool IsGoresMapProgressMap() const;
	bool IsGoresModuleEnabled() const;
	bool HasBlockingGoresWeapon() const;
	bool HasExtraGoresWeapon() const;
	void UpdateGoresWeaponCycle();
	void InvalidateGoresDistanceField();
	void EnsureGoresDistanceField();
	void ResetGoresDistanceFieldBuild();
	void ReleaseGoresDistanceFieldVisualLayerData();
	void StartGoresDistanceFieldBuild();
	void ContinueGoresDistanceFieldBuild();
	bool IsGoresDistanceFieldBuildContextCurrent() const;
	void FailGoresDistanceFieldBuild();
	void CompleteGoresDistanceFieldBuild();
	void StepGoresDistanceFieldTileScan(int Budget);
	void StepGoresDistanceFieldVisualLayers(int Budget);
	void StepGoresDistanceFieldQueueInit(int Budget);
	void StepGoresDistanceFieldDijkstra(int Budget);
	void StepGoresDistanceFieldReachableStartCheck(int Budget);
	void UpdateGoresMapProgress();
	bool IsGoresMapProgressDebugRouteEnabled() const;
	bool BuildGoresDebugRoute(std::vector<vec2> &vRoutePoints, int Dummy) const;
	void RenderGoresDebugRoute();

	// 收藏地图功能
	std::set<std::string> m_FavoriteMaps;
	static void ConAddFavoriteMap(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveFavoriteMap(IConsole::IResult *pResult, void *pUserData);
	static void ConClearFavoriteMaps(IConsole::IResult *pResult, void *pUserData);
	static void ConfigSaveFavoriteMaps(IConfigManager *pConfigManager, void *pUserData);

	// Map category cache
	std::unordered_map<std::string, std::string> m_MapCategoryCache;
	bool m_MapCategoryCacheDirty = false;
	int64_t m_MapCategoryCacheNextSave = 0;
	void LoadMapCategoryCache();
	void SaveMapCategoryCache();
	void MaybeSaveMapCategoryCache();

	// Map notes
	std::unordered_map<std::string, std::string> m_MapNotes;
	bool m_MapNotesDirty = false;
	int64_t m_MapNotesNextSave = 0;
	void LoadMapNotes();
	void SaveMapNotes();
	void MaybeSaveMapNotes();

	// Map play history
	QmMapHistory::CMapHistory m_MapHistory;
	bool m_MapHistoryDirty = false;
	bool m_MapHistorySessionActive = false;
	int64_t m_MapHistorySessionStart = 0;
	std::string m_MapHistoryActiveMapId;
	std::string m_MapHistoryActiveMapName;
	std::string m_MapHistorySuppressedMapId;
	void LoadMapHistory();
	void SaveMapHistory();
	void MarkMapHistoryDirty();
	void StartMapHistorySession();
	void EndMapHistorySession(bool SaveNow);
	void UpdateMapHistorySession();
	void TouchMapHistoryPlayTime();
	std::string CurrentMapHistoryId() const;
	int64_t CurrentMapHistoryPlayTimeMs() const;
	void HandleMapHistoryDeath(int ClientId);
	void HandleMapHistoryTeamDeath(int Team);
	void HandleMapHistoryFinish(int ClientId, int FinishTimeMs);

	// 本地存档列表
	using SLocalSaveEntry = QmLocalSaves::SEntry;
	char m_aLastLocalSaveHintMap[128] = "";
	std::vector<SLocalSaveEntry> m_vLocalSaveCandidates;
	bool m_LocalSavePromptActive = false;
	QmLocalSaves::CConfirmation m_LocalSaveConfirmation;
	QmLocalSaves::CRestore m_LocalSaveRestore;
	bool LoadLocalSaveEntries(std::vector<SLocalSaveEntry> &vEntries, bool *pFileExists = nullptr) const;
	bool RemoveLocalSaveByCode(const char *pMap, const char *pCode);
	void MaybeShowLocalSaveJoinHint();
	QmLocalSaves::CRestore::SWorld LocalSaveWorld() const;
	void UpdateLocalSaveRestore();
	void CompleteLocalSaveLoad(bool Success);
	static void ConSaveList(IConsole::IResult *pResult, void *pUserData);

	// 复读功能
	char m_aLastChatMessage[2048] = ""; // 最新一条公屏或队伍聊天消息
	int m_LastChatTeam = 0;
	int64_t m_LastRepeatTime = 0; // 上次发送复读时间
	int64_t m_LastRepeatKeyPressTime = 0; // 上次按下复读按键时间
	bool m_RepeatKeyDown = false; // 仅在按下沿计次，避免长按触发双击
	void RepeatLastMessage();
	static void ConRepeat(IConsole::IResult *pResult, void *pUserData);

	// Swap倒计时提示
	CSwapCountdownTracker m_aSwapCountdownTrackers[NUM_DUMMIES];
	void StartSwapCountdown(int Dummy, const char *pCounterpart, bool Outgoing);
	void ClearSwapCountdown(int Dummy = -1);

	// 好友上线提醒
	struct SFriendOnlineState
	{
		float m_LastSeen = 0.0f;
	};
	std::unordered_map<std::string, SFriendOnlineState> m_FriendOnline;
	qm_friend_notify::COnlineTracker m_FriendOnlineTracker;
	std::vector<qm_friend_notify::CFriend> m_vFriendOnlineScan;
	std::unordered_set<std::string> m_FriendOnlineAvailableServers;
	std::unordered_set<std::string> m_FriendOnlineNames;
	float m_FriendNotifyNextCheck = 0.0f;
	int m_FriendNotifyPrevEnabled = -1;
	int m_FriendNotifyPrevIgnoreClan = -1;
	uint64_t m_FriendNotifyPrevRevision = 0;
	bool m_FriendNotifyScanRunning = false;
	int m_FriendNotifyScanIndex = 0;
	float m_FriendAutoRefreshNext = 0.0f;
	int m_FriendAutoRefreshPrevEnabled = -1;
	int m_FriendAutoRefreshPrevSeconds = -1;
	void CheckFriendOnline();
	// 本服进服通知和自动问候共享同一份出入状态。
	qm_friend_notify::CEnterTracker m_FriendEnterTracker;
	int m_FriendEnterPrevEnabled = -1;
	int m_FriendEnterPrevIgnoreClan = -1;
	int m_FriendEnterPrevDummy = -1;
	uint64_t m_FriendEnterPrevRevision = 0;
	float m_FriendEnterNextCheck = 0.0f;
	std::string m_FriendEnterPendingNames;
	float m_FriendEnterPendingSendAt = 0.0f;
	void ResetFriendEnter();
	void CheckFriendEnterGreet();

	bool m_QmAspectApplyPending = false;

public:
	CTClient();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnWindowResize() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool ShouldAppendGoresPrevWeapon() const;
	// Gores 自动切锤是否正在接管武器（锤后自动切回，或拿到额外武器后的脉冲模式）。
	bool IsGoresWeaponCycleActive() const;
	// Gores 自动切锤引起的锤子切换是否要跳过切换动画（受 qm_gores_suppress_switch_anim 控制）。
	bool ShouldSkipGoresHammerSwitchAnimation(int ClientId, int PreviousWeapon, int CurrentWeapon) const;
	bool IsFinishRenamePending(int Dummy) const { return Dummy >= 0 && Dummy < NUM_DUMMIES && m_aFinishRenamePending[Dummy]; }

	void OnStateChange(int NewState, int OldState) override;
	void OnNewSnapshot() override;
	void QueueAspectApply();
	void SetForcedAspect();
	bool HasFreezeWakeupPopups() const;
	void RenderFreezeWakeupPopups();
	bool PrepareForShutdown(bool Force);
	const char *UpdateShutdownMessage() const;
	bool IsPreparingUpdateForShutdown() const { return m_UpdateShutdownRequested; }

	std::shared_ptr<IHttpRequest> m_pQmClientUpdateInfoTask = nullptr;
	std::shared_ptr<IHttpRequest> m_pUpdatePackageTask = nullptr;
	std::shared_ptr<IHttpRequest> m_pUpdatePackageSignatureTask = nullptr;
	std::shared_ptr<IHttpRequest> m_pUpdateManifestTask = nullptr;
	std::shared_ptr<IHttpRequest> m_pUpdateManifestSignatureTask = nullptr;
	void FetchQmClientUpdateInfo();
	void FinishQmClientUpdateInfo();
	void ResetQmClientUpdateInfoTask();
	bool NeedQmClientUpdate();
	void RequestQmClientUpdateCheckAndUpdate();
	bool IsUpdateChecking() const { return m_pQmClientUpdateInfoTask != nullptr; }
	bool IsUpdateDownloading() const
	{
		return m_pUpdatePackageTask != nullptr ||
		       m_pUpdatePackageSignatureTask != nullptr ||
		       m_pUpdateManifestTask != nullptr ||
		       m_pUpdateManifestSignatureTask != nullptr;
	}

	void RenderMiniVoteHud(bool HudEditorPreview = false);
	void RenderCenterLines();
	void RenderCtfFlag(vec2 Pos, float Alpha);

	bool ChatDoSpecId(const char *pInput);
	bool InfoTaskDone() const { return m_pQmClientUpdateInfoTask && m_pQmClientUpdateInfoTask->State() == EHttpState::DONE; }
	bool m_FetchedQmClientUpdateInfo = false;
	bool m_QmClientAutoUpdateAfterCheck = false;
	char m_aUpdatePackageTmp[IO_MAX_PATH_LENGTH] = "";
	char m_aUpdatePackageSignatureTmp[IO_MAX_PATH_LENGTH] = "";
	char m_aUpdateManifestTmp[IO_MAX_PATH_LENGTH] = "";
	char m_aUpdateManifestSignatureTmp[IO_MAX_PATH_LENGTH] = "";
	char m_aUpdateInstallerTmp[IO_MAX_PATH_LENGTH] = "";
	SQmClientUpdateRelease m_UpdateRelease;
	bool m_UpdateShutdownRequested = false;
	bool m_UpdateInstallerStarted = false;
	bool m_UpdateReady = false;
	bool m_UpdateCheckFailed = false;
	bool m_UpdateFailureNoticeShown = false;
	bool m_UpdateAutoEnabled = false;
	int64_t m_UpdateFailureExitAt = 0;
	int64_t m_UpdateNextCheck = 0;
	char m_aUpdateError[256] = "";
	char m_aQmClientLatestVersionStr[32] = "0";

	Regex m_RegexChatIgnore;

	// 玩家统计公开接口
	const SPlayerStats &GetPlayerStats(int Dummy = 0) const { return m_aPlayerStats[Dummy]; }
	void ResetPlayerStats(int Dummy = -1); // -1 = 重置所有

	// Swap倒计时公开接口
	void HandleSwapCountdownMessage(const char *pText, int Dummy);
	bool HasSwapCountdown(int Dummy = -1) const;
	const std::vector<SSwapCountdownState> &GetSwapCountdowns(int Dummy) const;

	// 收藏地图公开接口
	bool IsFavoriteMap(const char *pMapName) const;
	void AddFavoriteMap(const char *pMapName);
	void RemoveFavoriteMap(const char *pMapName);
	void ClearFavoriteMaps();
	const std::set<std::string> &GetFavoriteMaps() const { return m_FavoriteMaps; }
	const char *GetCachedMapCategoryKey(const char *pMapName) const;
	void UpdateMapCategoryCache(const char *pMapName, const char *pCategoryKey);
	const char *GetMapNote(const char *pMapName) const;
	void SetMapNote(const char *pMapName, const char *pNote);
	const QmMapHistory::CMapHistory &GetMapHistory() const { return m_MapHistory; }
	std::vector<QmMapHistory::SMapHistoryRecord> GetMapHistoryRecords(QmMapHistory::EMapHistoryFilter Filter) const { return m_MapHistory.Sorted(Filter); }
	void RemoveMapHistoryRecord(const char *pMapId);
	void ClearFinishedMapHistory();
	void ClearAllMapHistory();
	void TrackLocalSaveLoadCommand(int Conn, const char *pLine);
	bool TryHandleLocalSaveReply(const char *pLine);
	void HandleLocalSaveMessage(const CNetMsg_Sv_Chat *pMsg, int Conn);
	bool IsGoresMapProgressEnabled() const;
	bool ShouldHideGoresGuides(bool ManualGuideVisible = false) const;
	bool HasGoresMapProgress(int Dummy = 0) const
	{
		const int Idx = Dummy < 0 ? 0 : (Dummy >= NUM_DUMMIES ? NUM_DUMMIES - 1 : Dummy);
		return m_aGoresMapProgressValid[Idx];
	}
	float GetGoresMapProgress(int Dummy = 0) const
	{
		const int Idx = Dummy < 0 ? 0 : (Dummy >= NUM_DUMMIES ? NUM_DUMMIES - 1 : Dummy);
		return m_aGoresMapProgress[Idx];
	}

	// Focus Mode (Zen Mode)
	bool m_FocusModeStateKnown = false;
	bool m_PrevFocusModeActive = false;
	SQmFocusConfigOverrideState m_FocusHudOverrideState;
	SQmFocusConfigOverrideState m_FocusStatusBarOverrideState;
	SQmFocusConfigOverrideState m_FocusNamePlatesOverrideState;
	SQmFocusConfigOverrideState m_FocusNamePlatesOwnOverrideState;
	SQmFocusConfigOverrideState m_FocusNameplateShowScopeOverrideState;
	SQmFocusConfigOverrideState m_FocusNameplateCoordsOverrideState;
	SQmFocusConfigOverrideState m_FocusNameplateCoordsOwnOverrideState;
	SQmFocusConfigOverrideState m_FocusNameplateCoordXOverrideState;
	SQmFocusConfigOverrideState m_FocusNameplateCoordYOverrideState;
	SQmFocusConfigOverrideState m_FocusDirectionOverrideState;
	void ApplyFocusModeEffects();

	// Gores 快速输入临时覆盖
	bool m_GoresModeStateKnown = false;
	bool m_PrevGoresModeActive = false;
	bool m_GoresGameModeStateKnown = false;
	bool m_PrevGoresGameMode = false;
	SQmFocusConfigOverrideState m_GoresAutoEnableOverride;
	SQmFocusConfigOverrideState m_GoresFastInputOverride;
	SQmFocusConfigOverrideState m_GoresFastInputOthersOverride;
	SQmFocusConfigOverrideState m_GoresDummyHammerOverride;
	void ResetGoresConfigOverrides();
	bool IsFastInputActive() const;
	bool IsFastInputOthersActive() const;
	void ApplyGoresFastInputLink();
};

#endif
