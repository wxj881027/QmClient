#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H

#include <engine/client/enums.h>
#include <engine/external/regex.h>
#include <engine/shared/console.h>
#include <engine/shared/http.h>

#include <game/client/component.h>

#if defined(CONF_WHISPER)
#include <engine/client/stt.h>
#endif

#include <deque>
#include <set>
#include <string>
#include <unordered_map>

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

	float m_FinishTextTimeout = 0.0f;
	void DoFinishCheck();
	void StartUpdateDownload();
	void ResetUpdateExeTask();
	bool ReplaceClientFromUpdate();

	bool ServerCommandExists(const char *pCommand);
	bool IsQiaFenFinishedMap() const;

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

	// Auto Switch on Unfreeze (HJ大佬辅助)
	bool m_aWasInFreezeForSwitch[NUM_DUMMIES] = {false, false};
	void CheckAutoSwitchOnUnfreeze();

	// Auto Close Chat on Unfreeze (HJ大佬辅助)
	bool m_aWasInFreezeForChatClose[NUM_DUMMIES] = {false, false};
	void CheckAutoCloseChatOnUnfreeze();

	// 玩家统计跟踪
	SPlayerStats m_aPlayerStats[NUM_DUMMIES];
	void UpdatePlayerStats();
	void TrackHookDirection(int Dummy);

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
	int64_t m_LastRepeatTime = 0;           // 上次复读时间
	void RepeatLastMessage();
	static void ConRepeat(IConsole::IResult *pResult, void *pUserData);

	// 好友上线提醒
	struct SFriendOnlineState
	{
		float m_LastSeen = 0.0f;
		std::string m_Name;
		std::string m_Map;
	};
	std::unordered_map<std::string, SFriendOnlineState> m_FriendOnline;
	float m_FriendNotifyNextCheck = 0.0f;
	int m_FriendNotifyPrevEnabled = -1;
	float m_FriendAutoRefreshNext = 0.0f;
	int m_FriendAutoRefreshPrevEnabled = -1;
	int m_FriendAutoRefreshPrevSeconds = -1;
	void CheckFriendOnline();

	// Speech-to-Text (STT)
#if defined(CONF_WHISPER)
	CStt m_Stt;
	bool m_SttInitialized = false;
	int m_SttRecordingState = 0; // 0=idle, 1=recording
	char m_aSttTranscription[2048] = ""; // Last transcription result
	bool m_SttHasTranscription = false;
	static void ConSttToggle(IConsole::IResult *pResult, void *pUserData);
	void OnSttTranscription(const char *pText, bool IsFinal);
	void InitStt();
#endif

public:
	CTClient();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnConsoleInit() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;

	void OnStateChange(int OldState, int NewState) override;
	void OnNewSnapshot() override;
	void SetForcedAspect();

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

	// 收藏地图公开接口
	bool IsFavoriteMap(const char *pMapName) const;
	void AddFavoriteMap(const char *pMapName);
	void RemoveFavoriteMap(const char *pMapName);
	void ClearFavoriteMaps();
	const std::set<std::string> &GetFavoriteMaps() const { return m_FavoriteMaps; }
	const char *GetCachedMapCategoryKey(const char *pMapName) const;
	void UpdateMapCategoryCache(const char *pMapName, const char *pCategoryKey);

	// STT 公开接口
#if defined(CONF_WHISPER)
	bool IsSttRecording() const { return m_Stt.IsRecording(); }
	const char *GetSttState() const { return m_Stt.GetStateString(); }
#endif
};

#endif
