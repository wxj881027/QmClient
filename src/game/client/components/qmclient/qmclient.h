#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H

#include <base/color.h>
#include <base/hash.h>

#include <engine/client/enums.h>
#include <engine/external/regex.h>
#include <engine/graphics.h>
#include <engine/shared/console.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>


#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class IJob;

// 玩家统计数据结构
struct SPlayerStats
{
	// 存活时长统计
	int m_TotalAliveTime = 0;      // 总存活时间（tick）
	int m_MaxAliveTime = 0;        // 最大存活时间（tick）
	int m_AliveCount = 0;          // 存活次数（用于计算平均）
	int m_CurrentAliveStart = 0;   // 当前存活开始时间（tick）
	bool m_IsAlive = false;        // 当前是否存活（未被freeze）
	float m_FreezeX = 0.0f;        // 被冻结时的X位置
	float m_FreezeY = 0.0f;        // 被冻结时的Y位置

	// 被救/落水统计
	int m_RescueCount = 0;         // 被救醒次数（被别人解冻）
	int m_FreezeCount = 0;         // 落水次数（自己被冻结）

	// 出钩统计
	int m_HookLeftCount = 0;       // 向左出钩次数
	int m_HookRightCount = 0;      // 向右出钩次数
	bool m_WasHooking = false;     // 上一帧是否在出钩

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

struct SQmClientServerDistribution
{
	std::string m_ServerAddress;
	int m_UserCount = 0;
	int m_DummyCount = 0;
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

	int m_EmoteCycle = 0;
	static void ConEmoteCycle(IConsole::IResult *pResult, void *pUserData);

	class IEngineGraphics *m_pGraphics = nullptr;

	char m_PreviousOwnMessage[2048] = {};

	bool SendNonDuplicateMessage(int Team, const char *pLine);
	void TryAppendKeywordReplyRenameSuffix(bool UseDummy);

	float m_FinishTextTimeout = 0.0f;
	void DoFinishCheck();
	const char *CurrentCommunityIdForFinishCheck() const;
	void StartUpdateDownload();
	void ResetUpdateExeTask();
	bool ReplaceClientFromUpdate();

	bool ServerCommandExists(const char *pCommand);
	int64_t m_LastAutoReplyTime = 0;

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

	// Auto Unspec on Unfreeze
	bool m_aWasInFreezeForUnspec[NUM_DUMMIES] = {false, false};
	void CheckAutoUnspecOnUnfreeze();

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
	static constexpr int TEXT_POPUP_TEXTURE_MAX = 5;
	bool m_aWasInFreezeForWakeupPopup[NUM_DUMMIES] = {false, false};
	SFreezeWakeupPopup m_aFreezeWakeupPopups[FREEZE_WAKEUP_POPUP_MAX];
	IGraphics::CTextureHandle m_aTextPopupTextures[TEXT_POPUP_TEXTURE_MAX];
	char m_aTextPopupFont[256] = "";
	void CheckFreezeWakeupPopup();
	void CheckComboPopup();
	void AddFreezeWakeupPopup(int WokenDummy);
	bool EnsureTextPopupTexture(int TextType);
	bool AddTextPopup(int AnchorClientId, int TextType, bool UseRollingColor, ColorRGBA Color);
	void UnloadTextPopupTextures();
	void ClearFreezeWakeupPopups();
	void ResetComboState(int Dummy = -1);
	int m_aComboPopupCount[NUM_DUMMIES] = {0, 0};
	int m_aComboLastEventTick[NUM_DUMMIES] = {-1, -1};
	int m_aComboLastTargetPlayer[NUM_DUMMIES] = {-1, -1};
	int m_aComboLastHookedPlayer[NUM_DUMMIES] = {-1, -1};

	// Auto Switch on Unfreeze (HJ大佬辅助)
	bool m_aWasInFreezeForSwitch[NUM_DUMMIES] = {false, false};
	void CheckAutoSwitchOnUnfreeze();

	// Auto Close Chat on Unfreeze (HJ大佬辅助)
	bool m_aWasInFreezeForChatClose[NUM_DUMMIES] = {false, false};
	void CheckAutoCloseChatOnUnfreeze();

	// 玩家统计跟踪
	SPlayerStats m_aPlayerStats[NUM_DUMMIES];
	int m_aLastGameplayLogicTick[NUM_DUMMIES] = {-1, -1};
	void UpdatePlayerStats();
	void TrackHookDirection(int Dummy);

	// Gores 地图进度（全图距离场估算）
	bool m_GoresDistanceFieldValid = false;
	bool m_GoresDistanceFieldAttempted = false;
	int64_t m_GoresDistanceFieldNextBuildTryTick = 0;
	char m_aGoresDistanceFieldMap[128] = "";
	int m_GoresDistanceFieldWidth = 0;
	int m_GoresDistanceFieldHeight = 0;
	std::vector<unsigned char> m_vGoresCMap; // 0=normal 1=blocked 2=tele 3=penalty 4=reward
	std::vector<std::vector<int>> m_vvGoresDirectTeleOuts;
	std::vector<int> m_vGoresDistanceToFinish;
	bool m_aGoresWasOnStartLastTick[NUM_DUMMIES] = {false, false};
	bool m_aGoresRunStarted[NUM_DUMMIES] = {false, false};
	int m_aGoresRunStartDistanceToFinish[NUM_DUMMIES] = {0, 0};
	bool m_aGoresMapProgressValid[NUM_DUMMIES] = {false, false};
	float m_aGoresMapProgress[NUM_DUMMIES] = {0.0f, 0.0f};
	bool IsGoresGameMode() const;
	bool IsGoresModuleEnabled() const;
	bool HasBlockingGoresWeapon() const;
	void UpdateGoresWeaponCycle();
	void InvalidateGoresDistanceField();
	void EnsureGoresDistanceField();
	void BuildGoresDistanceField();
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

	// 本地存档列表
	static void ConSaveList(IConsole::IResult *pResult, void *pUserData);

	// 复读功能
	char m_aLastChatMessage[2048] = "";  // 最新一条公屏消息
	int64_t m_LastRepeatTime = 0; // 上次发送复读时间
	int64_t m_LastRepeatKeyPressTime = 0; // 上次按下复读按键时间
	bool m_RepeatKeyDown = false; // 仅在按下沿计次，避免长按触发双击
	void RepeatLastMessage();
	static void ConRepeat(IConsole::IResult *pResult, void *pUserData);

	// Swap倒计时提示
	bool m_SwapCountdownActive = false;
	int m_SwapCountdownStartTick = 0;
	void StartSwapCountdown();
	void ClearSwapCountdown();

	// 好友上线提醒
	struct SFriendOnlineState
	{
		float m_LastSeen = 0.0f;
		std::string m_Name;
		std::string m_Map;
		int m_LastSeenScanId = 0;
	};
	std::unordered_map<std::string, SFriendOnlineState> m_FriendOnline;
	float m_FriendNotifyNextCheck = 0.0f;
	int m_FriendNotifyPrevEnabled = -1;
	int m_FriendNotifyPrevIgnoreClan = -1;
	bool m_FriendNotifyScanRunning = false;
	int m_FriendNotifyScanIndex = 0;
	int m_FriendNotifyScanId = 0;
	float m_FriendAutoRefreshNext = 0.0f;
	int m_FriendAutoRefreshPrevEnabled = -1;
	int m_FriendAutoRefreshPrevSeconds = -1;
	void CheckFriendOnline();
	// 好友进图自动打招呼
	std::unordered_set<std::string> m_FriendEnterOnline;
	int m_FriendEnterPrevEnabled = -1;
	int m_FriendEnterPrevIgnoreClan = -1;
	bool m_FriendEnterInitialized = false;
	float m_FriendEnterNextCheck = 0.0f;
	std::string m_FriendEnterPendingNames;
	float m_FriendEnterPendingSendAt = 0.0f;
	void CheckFriendEnterGreet();

	// Q1menG client recognition sync
	std::shared_ptr<CHttpRequest> m_pQmClientAuthTokenTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientUsersTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientUsersSendTask = nullptr;
	std::shared_ptr<IJob> m_pQmClientUsersParseJob = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientLifecycleStartTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientLifecycleCrashTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientLifecycleStopTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientServerTimeTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientPlaytimeQueryTask = nullptr;
	std::shared_ptr<IJob> m_pQmClientLifecycleMarkerWriteJob = nullptr;
	char m_aQmClientAuthToken[256] = "";
	char m_aQmClientMachineHash[SHA256_MAXSTRSIZE] = "";
	char m_aQmClientLifecycleSessionId[64] = "";
	char m_aQmClientPlaytimeClientId[65] = "";
	int64_t m_QmClientLastSync = 0;
	int64_t m_QmClientServerNow = 0;
	int64_t m_QmClientServerSessionStart = 0;
	int64_t m_QmClientServerTimeLastSync = 0;
	int64_t m_QmClientServerPlaytimeSeconds = -1;
	int64_t m_QmClientPlaytimeLastSync = 0;
	int64_t m_QmClientRecoveryStopAt = 0;
	int64_t m_QmClientRecoveryNextRetry = 0;
	int64_t m_QmClientStartupNextRetry = 0;
	int64_t m_QmClientMarkerStartedAt = 0;
	int64_t m_QmClientMarkerLastSeenAt = 0;
	int64_t m_QmClientMarkerLastFlushTick = 0;
	std::vector<SQmClientServerDistribution> m_vQmClientServerDistribution;
	int m_QmClientOnlineUserCount = 0;
	int m_QmClientOnlineDummyCount = 0;
	bool m_QmClientShutdownReported = false;
	bool m_QmClientAwaitingRecoveryStop = false;
	bool m_QmClientStartupSent = false;
	void UpdateQmClientRecognition();
	void SyncQmClientUsers();
	void FetchQmClientAuthToken();
	void SendQmClientPlayerData();
	void FetchQmClientUsers();
	void FinishQmClientAuthToken();
	void FinishQmClientUsers();
	void ResetQmClientRecognitionTasks();
	bool NeedsQmClientRecognition() const;
	bool NeedsFastQmClientSync() const;
	bool EnsureQmClientMachineHash();
	bool BuildQmClientRecognitionUrl(const char *pPath, char *pBuf, size_t BufSize, const char *pQuery = nullptr) const;
	void ClearQmClientServerDistribution();
	void InitQmClientLifecycle();
	void UpdateQmClientLifecycleAndServerTime();
	void SendQmClientLifecyclePing(const char *pEvent, std::shared_ptr<CHttpRequest> &pTaskSlot);
	void FinishQmClientServerTimeTask();
	bool FinishQmClientPlaytimeTask(std::shared_ptr<CHttpRequest> &pTaskSlot, bool UpdateSessionStart);
	void SendQmClientPlaytimeRequest(const char *pUrl, std::shared_ptr<CHttpRequest> &pTaskSlot, int64_t StopAt = 0);
	void EnsureQmClientPlaytimeClientId();
	bool ReadQmClientLifecycleMarker(int64_t &OutStartedAt, int64_t &OutLastSeenAt);
	void TouchQmClientLifecycleMarker(bool ForceWrite);
	void WriteQmClientLifecycleMarker();
	void ClearQmClientLifecycleMarker();

	// DDNet player stats (favorite partner + total finishes)
	std::shared_ptr<CHttpRequest> m_pQmDdnetPlayerTask = nullptr;
	std::shared_ptr<IJob> m_pQmDdnetPlayerParseJob = nullptr;
	int64_t m_QmDdnetPlayerLastSync = 0;
	int64_t m_QmDdnetPlayerNextRetry = 0;
	char m_aQmDdnetPlayerName[MAX_NAME_LENGTH] = "";
	char m_aQmDdnetFavoritePartner[MAX_NAME_LENGTH] = "";
	int m_QmDdnetTotalFinishes = -1;
	bool m_QmAspectApplyPending = false;
	void UpdateQmDdnetPlayerStats();
	void FetchQmDdnetPlayerStats(const char *pPlayerName);
	void FinishQmDdnetPlayerStats();


public:
	CTClient();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool ShouldAppendGoresPrevWeapon() const;

	void OnStateChange(int NewState, int OldState) override;
	void OnNewSnapshot() override;
	void QueueAspectApply();
	void SetForcedAspect();
	bool HasFreezeWakeupPopups() const;
	void RenderFreezeWakeupPopups();

	std::shared_ptr<CHttpRequest> m_pTClientInfoTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pUpdateExeTask = nullptr;
	void FetchTClientInfo();
	void FinishTClientInfo();
	void ResetTClientInfoTask();
	bool NeedUpdate();
	void RequestUpdateCheckAndUpdate();
	bool IsUpdateChecking() const { return m_pTClientInfoTask && !m_pTClientInfoTask->Done(); }
	bool IsUpdateDownloading() const { return m_pUpdateExeTask && !m_pUpdateExeTask->Done(); }

	void RenderMiniVoteHud();
	void RenderCenterLines();
	void RenderCtfFlag(vec2 Pos, float Alpha);

	bool ChatDoSpecId(const char *pInput);
	bool InfoTaskDone() { return m_pTClientInfoTask && m_pTClientInfoTask->State() == EHttpState::DONE; }
	bool m_FetchedTClientInfo = false;
	bool m_AutoUpdateAfterCheck = false;
	char m_aUpdateExeTmp[64] = "";
	char m_aVersionStr[10] = "0";

	Regex m_RegexChatIgnore;

	// 玩家统计公开接口
	const SPlayerStats &GetPlayerStats(int Dummy = 0) const { return m_aPlayerStats[Dummy]; }
	void ResetPlayerStats(int Dummy = -1); // -1 = 重置所有

	// Swap倒计时公开接口
	bool HasSwapCountdown() const { return m_SwapCountdownActive; }
	int GetSwapCountdownStartTick() const { return m_SwapCountdownStartTick; }

	// 收藏地图公开接口
	bool IsFavoriteMap(const char *pMapName) const;
	void AddFavoriteMap(const char *pMapName);
	void RemoveFavoriteMap(const char *pMapName);
	void ClearFavoriteMaps();
	const std::set<std::string> &GetFavoriteMaps() const { return m_FavoriteMaps; }
	const char *GetCachedMapCategoryKey(const char *pMapName) const;
	void UpdateMapCategoryCache(const char *pMapName, const char *pCategoryKey);
	bool HasQmServerTime() const { return m_QmClientServerNow > 0; }
	int64_t QmServerTimeNow() const { return m_QmClientServerNow; }
	int64_t QmServerSessionStartTime() const { return m_QmClientServerSessionStart; }
	bool HasQmServerPlaytime() const { return m_QmClientServerPlaytimeSeconds >= 0; }
	int64_t QmServerPlaytimeSeconds() const { return m_QmClientServerPlaytimeSeconds; }
	const std::vector<SQmClientServerDistribution> &QmClientServerDistribution() const { return m_vQmClientServerDistribution; }
	int QmClientOnlineUserCount() const { return m_QmClientOnlineUserCount; }
	int QmClientOnlineDummyCount() const { return m_QmClientOnlineDummyCount; }
	int QmDdnetTotalFinishes() const { return m_QmDdnetTotalFinishes; }
	const char *QmDdnetFavoritePartner() const { return m_aQmDdnetFavoritePartner; }
	bool IsGoresMapProgressEnabled() const;
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

};

#endif
