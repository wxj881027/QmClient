// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QMCLIENT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QMCLIENT_H

#include "qmclient_utils.h"

#include <base/hash.h>

#include <engine/shared/http.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <memory>
#include <mutex>

class IJob;

class CQmClient : public CComponent
{
	std::shared_ptr<CHttpRequest> m_pTitleOperation;
	std::shared_ptr<CHttpRequest> m_pTitleReport;
	std::shared_ptr<CHttpRequest> m_pTitleList;
	char m_aTitleToken[65] = "";
	char m_aTitleText[64] = "";
	char m_aTitleBoundName[64] = "";
	char m_aTitlePendingServer[NETADDR_MAXSTRSIZE] = "";
	char m_aaPlayerTitles[MAX_CLIENTS][64] = {};
	char m_aaTitleNames[MAX_CLIENTS][MAX_NAME_LENGTH] = {};
	int64_t m_aTitleExpires[MAX_CLIENTS] = {};
	int64_t m_TitleLastSync = 0;
	bool m_TitleAuthenticated = false;
	int m_TitleRevision = 0;
	const char *m_pTitleStatus = "Enter your sponsor code";
	void InitTitleAuthentication();
	void UpdateTitleAuthentication();
	void ResetTitlePresences();
	void StartTitleRequest(const char *pPath, const char *pBody, std::shared_ptr<CHttpRequest> &pTask);

	std::shared_ptr<CHttpRequest> m_pQmClientAuthTokenTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientUsersTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientUsersSendTask = nullptr;
	std::shared_ptr<IJob> m_pQmClientUsersParseJob = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmDeveloperPresenceTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmDeveloperPresencesTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientLifecycleStartTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientLifecycleCrashTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientLifecycleStopTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientServerTimeTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pQmClientPlaytimeQueryTask = nullptr;
	std::shared_ptr<IJob> m_pQmClientLifecycleMarkerWriteJob = nullptr;
	std::shared_ptr<std::mutex> m_pQmClientLifecycleMarkerMutex = std::make_shared<std::mutex>();
	std::shared_ptr<CHttpRequest> m_pQmDdnetPlayerTask = nullptr;
	std::shared_ptr<IJob> m_pQmDdnetPlayerParseJob = nullptr;

	char m_aQmClientAuthToken[256] = "";
	char m_aQmClientMachineHash[SHA256_MAXSTRSIZE] = "";
	char m_aQmClientLifecycleSessionId[64] = "";
	char m_aQmClientPlaytimeClientId[65] = "";
	char m_aQmDdnetPlayerName[MAX_NAME_LENGTH] = "";
	char m_aQmDdnetFavoritePartner[MAX_NAME_LENGTH] = "";
	char m_aQmClientPendingVoicePresenceServerAddress[NETADDR_MAXSTRSIZE] = "";
	char m_aQmDeveloperToken[65] = "";
	char m_aQmDeveloperSessionId[33] = "";
	char m_aQmDeveloperPendingServerAddress[NETADDR_MAXSTRSIZE] = "";

	int64_t m_QmClientLastSync = 0;
	int64_t m_QmDeveloperLastSync = 0;
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
	int64_t m_QmDdnetPlayerLastSync = 0;
	int64_t m_QmDdnetPlayerNextRetry = 0;
	int m_QmClientOnlineUserCount = 0;
	int m_QmClientOnlineDummyCount = 0;
	int m_QmDdnetTotalFinishes = -1;
	int m_QmClientPendingVoicePresencePlayers = 0;
	bool m_QmClientDistributionSuccessLatched = false;
	bool m_QmClientShutdownReported = false;
	bool m_QmClientAwaitingRecoveryStop = false;
	bool m_QmClientStartupSent = false;
	std::vector<SQmClientServerDistribution> m_vQmClientServerDistribution;

	void InitQmClientLifecycle();
	void UpdateQmClientLifecycleAndServerTime();
	void SendQmClientLifecyclePing(const char *pEvent, std::shared_ptr<CHttpRequest> &pTaskSlot);
	bool FinishQmClientPlaytimeTask(std::shared_ptr<CHttpRequest> &pTaskSlot, bool UpdateSessionStart);
	void FinishQmClientServerTimeTask();
	void SendQmClientPlaytimeRequest(const char *pUrl, std::shared_ptr<CHttpRequest> &pTaskSlot, int64_t StopAt = 0);
	void EnsureQmClientPlaytimeClientId();
	bool ReadQmClientLifecycleMarker(int64_t &OutStartedAt, int64_t &OutLastSeenAt);
	void TouchQmClientLifecycleMarker(bool ForceWrite);
	void WriteQmClientLifecycleMarker();
	void ClearQmClientLifecycleMarker();

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
	void InitQmDeveloperAuthentication();
	void UpdateQmDeveloperPresence();
	void SendQmDeveloperPresence(const char *pServerAddress);
	void FetchQmDeveloperPresences(const char *pServerAddress);
	void FinishQmDeveloperPresences(const char *pServerAddress);
	void ResetQmDeveloperPresenceTasks();

	void UpdateQmDdnetPlayerStats();
	void FetchQmDdnetPlayerStats(const char *pPlayerName);
	void FinishQmDdnetPlayerStats();

public:
	void RedeemTitleCode(const char *pCode);
	void SaveTitleProfile(const char *pTitle, const char *pBoundName);
	void RefreshTitleProfile();
	bool TitleBusy() const { return m_pTitleOperation != nullptr; }
	bool TitleAuthenticated() const { return m_TitleAuthenticated; }
	const char *TitleStatus() const { return m_pTitleStatus; }
	const char *TitleText() const { return m_aTitleText; }
	const char *TitleBoundName() const { return m_aTitleBoundName; }
	int TitleRevision() const { return m_TitleRevision; }
	const char *PlayerTitle(int ClientId) const;
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
	const std::vector<SQmClientServerDistribution> &QmClientServerDistribution() const { return m_vQmClientServerDistribution; }
	int QmClientOnlineUserCount() const { return m_QmClientOnlineUserCount; }
	int QmClientOnlineDummyCount() const { return m_QmClientOnlineDummyCount; }
	int QmDdnetTotalFinishes() const { return m_QmDdnetTotalFinishes; }
	const char *QmDdnetFavoritePartner() const { return m_aQmDdnetFavoritePartner; }
};

#endif
