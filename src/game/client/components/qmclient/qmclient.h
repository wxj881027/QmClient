// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QMCLIENT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QMCLIENT_H

#include "ddnet_player_stats_state.h"
#include "markdown_cache_writer.h"
#include "qm_markdown_broadcast.h"
#include "qm_realtime.h"
#include "qm_sponsors.h"
#include "qmclient_utils.h"

#include <base/hash.h>

#include <engine/http.h>
#include <engine/shared/protocol.h>
#include <engine/shared/websocket_client.h>

#include <game/client/component.h>

#include <deque>
#include <memory>
#include <mutex>

class IJob;

struct SQmClientLocalModeStats
{
	std::string m_GameMode;
	std::string m_CommunityId;
	bool m_IsAxiom = false;
	int m_Maps = 0;
	int64_t m_Score = 0;
	int64_t m_PlaytimeSeconds = 0;
};

struct SQmClientDdnetPlayerStats
{
	std::string m_PlayerName;
	std::string m_FavoritePartner;
	int m_TotalFinishes = -1;
	int64_t m_Points = -1;
	int64_t m_PointsTotal = -1;
	// 官方 json2 的 activity[] 逐日 hours_played 求和，即生涯累计游玩小时数。
	// -1 表示该玩家数据里没有可用的 activity 记录。
	int64_t m_PlaytimeHours = -1;
	// 官方 hours_played_past_365_days，最近一年游玩小时数，-1 表示缺失。
	int64_t m_PlaytimeHoursPastYear = -1;
};

class CQmClient : public CComponent
{
	std::shared_ptr<IHttpRequest> m_pTitleOperation;
	char m_aTitleToken[65] = "";
	char m_aTitleText[64] = "";
	char m_aTitleBoundName[64] = "";
	char m_aTitleProfileStyle[64] = "";
	char m_aaPlayerTitles[MAX_CLIENTS][64] = {};
	char m_aaPlayerTitleStyles[MAX_CLIENTS][48] = {};
	char m_aaTitleNames[MAX_CLIENTS][MAX_NAME_LENGTH] = {};
	int64_t m_aTitleExpires[MAX_CLIENTS] = {};
	double m_TitleServerTimeOffset = 0.0;
	bool m_TitleServerTimeOffsetValid = false;
	bool m_TitleAuthenticated = false;
	int m_TitleRevision = 0;
	const char *m_pTitleStatus = "Enter your sponsor code";
	void InitTitleAuthentication();
	void UpdateTitleAuthentication();
	void ResetTitlePresences();
	void StartTitleRequest(const char *pPath, const char *pBody, std::shared_ptr<IHttpRequest> &pTask);

	std::shared_ptr<IJob> m_pQmClientUsersParseJob = nullptr;
	std::shared_ptr<IJob> m_pQmClientLifecycleMarkerWriteJob = nullptr;
	std::shared_ptr<std::mutex> m_pQmClientLifecycleMarkerMutex = std::make_shared<std::mutex>();
	std::shared_ptr<IHttpRequest> m_pQmDdnetPlayerTask = nullptr;
	std::shared_ptr<IJob> m_pQmDdnetPlayerParseJob = nullptr;
	CQmDdnetPlayerStatsState m_QmDdnetPlayerState;
	CQmMarkdownBroadcast m_QmMarkdownBroadcast;
	// 每个缓存文件一个写作业实例：界面立即更新，磁盘只保留最新完整快照。
	CQmMarkdownCacheWriter m_QmMarkdownBroadcastCacheWriter;
	std::string m_QmNewsDraft;
	std::shared_ptr<IHttpRequest> m_pQmNewsPublishTask;
	enum class EQmNewsStatus
	{
		IDLE,
		PUBLISHING,
		PUBLISHED,
		PUBLISH_DENIED,
		PUBLISH_TOO_LARGE,
		PUBLISH_FAILED,
	};
	EQmNewsStatus m_QmNewsStatus = EQmNewsStatus::IDLE;
	CQmMarkdownCacheWriter m_QmSponsorsCacheWriter;
	qm_sponsors::CSnapshot m_QmSponsors;
	std::string m_QmSponsorsDraft;
	std::shared_ptr<IHttpRequest> m_pQmSponsorsPublishTask;
	enum class EQmSponsorsStatus
	{
		IDLE,
		EMPTY,
		READY,
		PUBLISHING,
		PUBLISHED,
		PUBLISH_DENIED,
		PUBLISH_TOO_LARGE,
		PUBLISH_FAILED,
	};
	EQmSponsorsStatus m_QmSponsorsStatus = EQmSponsorsStatus::IDLE;
	int m_QmSponsorsStatusRevision = 0;
	std::unique_ptr<IQmWebSocketClient> m_pQmRealtimeTransport;
	char m_aQmRealtimeUrl[512] = "";
	bool m_QmRealtimeFailureLogged = false;
	std::unique_ptr<IQmWebSocketClient> m_pQmAnonymousEmote;
	char m_aQmAnonymousClientId[65] = "";
	char m_aQmAnonymousSessionId[65] = "";
	int64_t m_QmAnonymousConnectedTick = 0;
	int64_t m_QmAnonymousNextHelloCheck = 0;
	std::string m_QmAnonymousHelloBody;
	std::deque<SQmRealtimeMessage> m_QmRealtimeEvents;
	std::deque<SQmRealtimeMessage> m_QmRealtimeEmoticonEvents;
	std::shared_ptr<const json_value> m_pQmRealtimeUsersPayload;
	char m_aQmRealtimeUsersServer[NETADDR_MAXSTRSIZE] = "";
	int64_t m_QmRealtimeUsersExpireTick = 0;
	bool m_QmRealtimeHelloSent = false;
	int m_QmRealtimeTitleRevision = -1;
	int64_t m_QmRealtimeConnectedTick = 0;
	int64_t m_QmRealtimeLastPresence = 0;
	int64_t m_QmRealtimeNextPresenceCheck = 0;
	std::string m_QmRealtimePresenceBody;
	int64_t m_QmClientPlaytimeManualRefreshTick = 0;

	char m_aQmClientMachineHash[SHA256_MAXSTRSIZE] = "";
	char m_aQmClientLifecycleSessionId[64] = "";
	char m_aQmClientPlaytimeClientId[65] = "";
	char m_aQmDdnetPlayerName[MAX_NAME_LENGTH] = "";
	char m_aQmDdnetFavoritePartner[MAX_NAME_LENGTH] = "";
	char m_aQmDeveloperToken[65] = "";
	char m_aQmDeveloperSessionId[33] = "";
	int64_t m_QmClientServerNow = 0;
	int64_t m_QmClientServerSessionStart = 0;
	int64_t m_QmClientServerTimeLastSync = 0;
	int64_t m_QmClientServerPlaytimeSeconds = -1;
	int64_t m_QmClientPlaytimeLastSync = 0;
	int64_t m_QmClientPlaytimeLastSuccessfulSyncTimestamp = 0;
	int64_t m_QmClientRecoveryStopAt = 0;
	int64_t m_QmClientMarkerStartedAt = 0;
	int64_t m_QmClientMarkerLastSeenAt = 0;
	int64_t m_QmClientMarkerLastFlushTick = 0;
	int m_QmDdnetTotalFinishes = -1;
	int64_t m_QmDdnetPoints = -1;
	int64_t m_QmDdnetPointsTotal = -1;
	int64_t m_QmDdnetPlaytimeHours = -1;
	int64_t m_QmDdnetPlaytimeHoursPastYear = -1;
	mutable bool m_QmStatisticsFileExists = false;
	mutable bool m_QmStatisticsFileInvalid = false;
	mutable int64_t m_QmStatisticsNextSaveRetryTick = 0;
	// 本地统计在会话内被修改后置脏；落盘按 m_QmStatisticsLocalSaveDueTick
	// 节流，避免逐秒游玩时长触发逐秒写文件。崩溃时最多丢一个节流窗口。
	bool m_QmStatisticsLocalStatsDirty = false;
	int64_t m_QmStatisticsLocalSaveDueTick = 0;
	// 完成事件有两条通道：0.6 是聊天广播，0.7 是 RaceFinish 事件；官方
	// 服务端按协议二选一，个别魔改服可能都发，用 1 秒窗口去重兜底。
	int64_t m_QmLastLocalFinishRecordTime = -1;
	// 本次会话内是否成功拉取过 DDNet 档案；用于区分「查询中」与「查无此人」。
	bool m_QmDdnetStatsSucceededOnce = false;
	bool m_QmClientDistributionSuccessLatched = false;
	bool m_QmClientShutdownReported = false;
	bool m_QmClientAwaitingRecoveryStop = false;
	bool m_QmClientStartupSent = false;
	bool m_QmClientPlaytimeManualRefreshActive = false;
	bool m_QmClientPlaytimeManualRefreshFailed = false;
	SQmClientDistributionSnapshot m_QmClientDistribution;
	std::vector<SQmClientLocalModeStats> m_vQmClientLocalModeStats;
	std::vector<SQmClientDdnetPlayerStats> m_vQmClientDdnetPlayerStats;
	std::string m_QmDdnetPrimaryPlayerName;
	std::string m_QmClientActiveLocalMode;
	std::string m_QmClientActiveLocalCommunityId;
	bool m_QmClientActiveLocalIsAxiom = false;
	int64_t m_QmClientLocalModeLastTick = 0;
	int64_t m_QmClientLocalModeTickRemainder = 0;

	void InitQmClientLifecycle();
	void UpdateQmClientLifecycleAndServerTime();
	void EnsureQmClientPlaytimeClientId();
	bool ReadQmClientLifecycleMarker(int64_t &OutStartedAt, int64_t &OutLastSeenAt);
	void TouchQmClientLifecycleMarker(bool ForceWrite);
	void WriteQmClientLifecycleMarker();
	void ClearQmClientLifecycleMarker();

	void UpdateQmClientRecognition();
	void FinishQmClientUsers();
	bool EnsureQmClientMachineHash();
	void PushQmClientServerCounts();
	void InitQmDeveloperAuthentication();
	void ResetQmDeveloperPresenceTasks();

	void UpdateQmDdnetPlayerStats();
	void FetchQmDdnetPlayerStats(const char *pPlayerName);
	void FinishQmDdnetPlayerStats();
	void StoreQmDdnetPlayerStats(const char *pPlayerName, const std::string &FavoritePartner, int TotalFinishes, int64_t Points, int64_t PointsTotal, int64_t PlaytimeHours, int64_t PlaytimeHoursPastYear);
	void SelectQmDdnetPlayerStats(const char *pFallbackPlayerName = nullptr);
	const SQmClientDdnetPlayerStats *FindQmDdnetPlayerStats(const char *pPlayerName) const;
	void LoadQmClientLocalModeStats();
	void UpdateQmClientLocalModePlaytime();
	void AccumulateQmClientLocalModePlaytime(int64_t Now);
	void EndQmClientLocalModePlaytime();
	void RecordQmClientLocalRaceFinish(int TimeMs);
	void RefreshQmDdnetPlayerStats();
	void RefreshQmClientPlaytime();
	void UpdateQmRealtime();
	void UpdateQmAnonymousEmotes();
	void StopQmAnonymousEmotes();
	std::string BuildQmAnonymousEmoteHello() const;
	void SendQmAnonymousEmoteHello();
	void QueueQmAnonymousEmoticonEvent(SQmRealtimeMessage Message);
	void ApplyQmRealtimeServiceData(const SQmRealtimeMessage &Message);
	void ApplyQmRealtimeBroadcast(const SQmRealtimeMessage &Message);
	void ApplyQmRealtimeTitles(const SQmRealtimeMessage &Message);
	void ApplyQmRealtimeUsers(const SQmRealtimeMessage &Message);
	void ApplyQmRealtimeDevelopers(const SQmRealtimeMessage &Message);
	std::string BuildQmRealtimePresence(bool Hello) const;
	bool RequestQmRealtimeTitleRefresh();
	void SendQmRealtimeStop();
	// 广播 markdown 的磁盘缓存：Apply 成功后落盘（交给作业），启动时读回上次内容。
	void SaveQmMarkdownBroadcastCache();
	void LoadQmMarkdownBroadcastCache();
	void FinishQmNewsPublish();
	void LoadQmSponsorsCache();
	void SaveQmSponsorsCache();
	bool ApplyQmSponsorsPayload(const json_value *pPayload, bool SaveCache);
	void FinishQmSponsorsPublish();

public:
	using ENewsStatus = EQmNewsStatus;
	const char *QmNewsDraft() const { return m_QmNewsDraft.c_str(); }
	ENewsStatus QmNewsStatus() const { return m_QmNewsStatus; }
	bool QmNewsPublishing() const { return m_pQmNewsPublishTask != nullptr; }
	void QmNewsReloadDraft();
	void QmNewsPublishDraft();
	using ESponsorsStatus = EQmSponsorsStatus;
	const std::vector<std::string> &QmSponsorNames() const { return m_QmSponsors.Names(); }
	const char *QmSponsorsDraft() const { return m_QmSponsorsDraft.c_str(); }
	ESponsorsStatus QmSponsorsStatus() const { return m_QmSponsorsStatus; }
	int QmSponsorsRevision() const { return m_QmSponsors.Revision() + m_QmSponsorsStatusRevision; }
	bool QmSponsorsPublishing() const { return m_pQmSponsorsPublishTask != nullptr; }
	bool HasDeveloperCredential() const { return m_aQmDeveloperToken[0] != '\0'; }
	void QmSponsorsRefresh();
	void QmSponsorsReloadDraft();
	void QmSponsorsPublishDraft();
	void RedeemTitleCode(const char *pCode);
	void SaveTitleProfile(const char *pTitle, const char *pBoundName, const char *pStyle);
	void RefreshTitleProfile();
	bool TitleBusy() const { return m_pTitleOperation != nullptr; }
	bool TitleAuthenticated() const { return m_TitleAuthenticated; }
	const char *TitleStatus() const { return m_pTitleStatus; }
	const char *TitleText() const { return m_aTitleText; }
	const char *TitleBoundName() const { return m_aTitleBoundName; }
	const char *TitleProfileStyle() const { return m_aTitleProfileStyle; }
	int TitleRevision() const { return m_TitleRevision; }
	const char *PlayerTitle(int ClientId) const;
	const char *PlayerTitleStyle(int ClientId) const;
	double TitleAnimationTime() const;
	bool HasQmMarkdownBroadcast() const { return m_QmMarkdownBroadcast.HasMarkdown(); }
	const char *QmMarkdownBroadcast() const { return m_QmMarkdownBroadcast.Markdown(); }
	int QmMarkdownBroadcastVersion() const { return m_QmMarkdownBroadcast.Version(); }
	int QmMarkdownBroadcastRevision() const { return m_QmMarkdownBroadcast.Revision(); }
	void EnqueueQmRealtimeMessage(const char *pData, size_t Size);
	bool PopQmRealtimeMessage(SQmRealtimeMessage &Message);
	bool PopQmRealtimeEmoticon(SQmRealtimeMessage &Message);
	void SendQmAnonymousEmoticon(int Emoticon, int PlayerId, bool LaunchMode, bool SuperLaunch);
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnUpdate() override;
	void OnStateChange(int NewState, int OldState) override;

	bool HasQmClientRecognitionService() const;
	bool HasQmServerTime() const { return m_QmClientServerNow > 0; }
	int64_t QmServerTimeNow() const { return m_QmClientServerNow; }
	int64_t QmServerSessionStartTime() const { return m_QmClientServerSessionStart; }
	bool HasQmServerPlaytime() const { return m_QmClientServerPlaytimeSeconds >= 0; }
	int64_t QmServerPlaytimeSeconds() const { return m_QmClientServerPlaytimeSeconds; }
	const std::vector<SQmClientServerDistribution> &QmClientServerDistribution() const { return m_QmClientDistribution.m_vServers; }
	int QmClientOnlineUserCount() const { return m_QmClientDistribution.m_OnlineUserCount; }
	int QmClientOnlineDummyCount() const { return m_QmClientDistribution.m_OnlineDummyCount; }
	bool QmClientDistributionSyncing() const;
	int QmDdnetTotalFinishes() const { return m_QmDdnetTotalFinishes; }
	int64_t QmDdnetPoints() const { return m_QmDdnetPoints; }
	int64_t QmDdnetPointsTotal() const { return m_QmDdnetPointsTotal; }
	// 官方 DDNet 统计给出的游玩时长，单位小时。-1 表示尚未取得。
	int64_t QmDdnetPlaytimeHours() const { return m_QmDdnetPlaytimeHours; }
	int64_t QmDdnetPlaytimeHoursPastYear() const { return m_QmDdnetPlaytimeHoursPastYear; }
	const char *QmDdnetPlayerName() const { return m_aQmDdnetPlayerName; }
	const char *QmDdnetPrimaryPlayerName() const { return m_QmDdnetPrimaryPlayerName.c_str(); }
	const char *QmDdnetFavoritePartner() const { return m_aQmDdnetFavoritePartner; }
	bool QmDdnetStatsIsFetching() const { return m_QmDdnetPlayerState.IsFetching(); }
	bool QmDdnetStatsLastRequestFailed() const { return m_QmDdnetPlayerState.LastRequestFailed(); }
	// 本次会话内是否完成过一次成功的 DDNet 档案查询（HTTP 200 且 JSON 可解析）。
	bool QmDdnetStatsSucceededOnce() const { return m_QmDdnetStatsSucceededOnce; }
	// 统计文件加载失败时为真；此时保存被拒绝，UI 应给出提示。
	bool QmStatisticsFileInvalid() const { return m_QmStatisticsFileInvalid; }
	int64_t QmDdnetStatsLastSuccessfulSyncTimestamp() const { return m_QmDdnetPlayerState.LastSuccessfulSyncTimestamp(); }
	bool QmStatisticsIsFetching() const { return m_QmDdnetPlayerState.IsFetching() || m_QmClientPlaytimeManualRefreshActive; }
	bool QmStatisticsLastRequestFailed() const { return m_QmDdnetPlayerState.LastRequestFailed() || m_QmClientPlaytimeManualRefreshFailed; }
	int64_t QmStatisticsLastSuccessfulSyncTimestamp() const;
	const std::vector<SQmClientLocalModeStats> &QmClientLocalModeStats() const { return m_vQmClientLocalModeStats; }
	const std::vector<SQmClientDdnetPlayerStats> &QmClientDdnetPlayerStats() const { return m_vQmClientDdnetPlayerStats; }
	bool SaveQmClientStatistics() const;
	void RecordQmClientLocalMapFinish(const char *pGameMode, int Score);
	void OnMessage(int MsgType, void *pRawMsg) override;
	void UseCurrentQmDdnetPlayerName();
	void RefreshQmClientStatistics();
};

#endif
