#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_AXIOM_SCORES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_AXIOM_SCORES_H

#include "axiom_scores_data.h"

#include <engine/http.h>

#include <game/client/component.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

class CJsonFileWriter;

class IQmAxiomHttpRequest
{
public:
	virtual ~IQmAxiomHttpRequest() = default;
	virtual bool Done() const = 0;
	virtual bool TransportSucceeded() const = 0;
	virtual int StatusCode() const = 0;
	virtual void Result(const unsigned char **ppData, size_t *pDataSize) const = 0;
	virtual void Abort() = 0;
	// 传输失败的简短原因（如 "timeout" / "aborted" / "http 500"），
	// 供统计页把「为什么没同步上」显示给用户；成功时返回空串。
	virtual const char *ErrorDetail() const { return ""; }
};

class IQmAxiomHttp
{
public:
	virtual ~IQmAxiomHttp() = default;
	virtual std::shared_ptr<IQmAxiomHttpRequest> Get(const char *pUrl, int ConnectTimeoutMs, int TimeoutMs, int64_t MaxResponseBytes) = 0;
};

enum class EQmAxiomScoreStatus
{
	NOT_REQUESTED,
	FETCHING,
	READY,
	NOT_FOUND,
	AMBIGUOUS,
	HTTP_ERROR,
	API_ERROR,
	INVALID_RESPONSE,
};

// 记分板一行的取分结果：玩家名 -> user_id 的搜索与模式分数合并成一个状态。
struct SQmAxiomLookupResult
{
	EQmAxiomScoreStatus m_Status = EQmAxiomScoreStatus::NOT_REQUESTED;
	int64_t m_Points = 0;
};

struct SQmAxiomModeResult
{
	EQmAxiomScoreStatus m_Status = EQmAxiomScoreStatus::NOT_REQUESTED;
	bool m_HasData = false;
	SQmAxiomModeScore m_Score;
	// 最近一次失败的具体原因（HTTP 超时 / 非 200 / 解析错误），成功时清空。
	std::string m_ErrorDetail;
};

struct SQmAxiomPlayerResult
{
	EQmAxiomScoreStatus m_SearchStatus = EQmAxiomScoreStatus::NOT_REQUESTED;
	SQmAxiomSearchMatch m_Match;
	std::array<SQmAxiomModeResult, 2> m_aModes;
	// 搜索阶段最近一次失败的具体原因；搜索成功后清空。
	std::string m_SearchErrorDetail;

	const SQmAxiomModeResult &Mode(EQmAxiomMode Mode) const;
};

class CQmAxiomScores : public CComponent
{
	struct SCacheEntry
	{
		SQmAxiomPlayerResult m_Result;
		int64_t m_LastSearchSuccessTick = 0;
		int64_t m_LastSearchFailureTick = 0;
		std::array<int64_t, 2> m_aLastModeSuccessTick{};
		std::array<int64_t, 2> m_aLastModeFailureTick{};
		std::array<int64_t, 2> m_aDdStatsPlayTime{};
		std::array<bool, 2> m_aHasDdStatsPlayTime{};
		std::array<int64_t, 2> m_aAxiomPlayTime{};
		std::array<bool, 2> m_aHasAxiomPlayTime{};
		std::vector<SQmDdStatsGameType> m_vDdStatsGameTypes;
		int64_t m_LastDdStatsSuccessTick = 0;
		int64_t m_LastDdStatsFailureTick = 0;
		int64_t m_LastAccessTick = 0;
		// 同一帧内 Tick 相同，LRU 用单调访问序号打破平局。
		uint64_t m_LastAccessOrder = 0;
	};

	struct SRequestSlot
	{
		std::shared_ptr<IQmAxiomHttpRequest> m_pRequest;
		uint64_t m_Generation = 0;
		std::string m_PlayerName;
		EQmAxiomMode m_Mode = EQmAxiomMode::NONE;
	};

	std::map<std::string, SCacheEntry> m_Cache;
	uint64_t m_AccessOrderClock = 0;
	SRequestSlot m_SearchRequest;
	std::array<SRequestSlot, 2> m_aModeRequests;
	SRequestSlot m_DdStatsRequest;
	std::string m_ActivePlayerName;
	std::map<std::string, SRequestSlot> m_SearchRequests;
	std::map<std::string, SRequestSlot> m_ModeRequests;
	EQmAxiomMode m_Mode = EQmAxiomMode::NONE;
	int m_SearchStartsThisFrame = 0;
	uint64_t m_Generation = 0;
	IQmAxiomHttp *m_pHttpOverride = nullptr;
	bool m_PersistentCacheDirty = false;
	bool m_DdStatsEnabled = true;
	int64_t m_LastSuccessfulSyncTimestamp = 0;

	static bool IsFailureStatus(EQmAxiomScoreStatus Status);
	static int ModeIndex(EQmAxiomMode Mode);
	static EQmAxiomMode ModeFromIndex(int Index);
	static bool IsWithinWindow(int64_t Timestamp, int64_t Now, int64_t WindowMs);

	void AbortActiveRequests(bool ResetFetchingStates);
	void BeginActiveQuery(const char *pPlayerName);
	void StartSearchRequest(const char *pPlayerName, SCacheEntry &Entry);
	void StartModeRequest(const char *pPlayerName, SCacheEntry &Entry, EQmAxiomMode Mode);
	void StartDdStatsRequest(const char *pPlayerName, SCacheEntry &Entry);
	void ProcessSearchRequest();
	void ProcessModeRequests();
	void ProcessDdStatsRequest();
	void FinishActiveQueryIfIdle();
	void EvictCacheEntryIfNeeded();
	void EnsureScoreboardQueried(const char *pPlayerName);
	void ProcessScoreboardRequests();
	void AbortScoreboardRequests();
	std::shared_ptr<IQmAxiomHttpRequest> StartRequest(const char *pUrl, int TimeoutMs);

protected:
	virtual int64_t CurrentTick() const;

public:
	CQmAxiomScores() = default;
	explicit CQmAxiomScores(IQmAxiomHttp *pHttpOverride) :
		m_pHttpOverride(pHttpOverride)
	{
	}
	int Sizeof() const override { return sizeof(*this); }
	void OnUpdate() override;
	void OnReset() override;
	void OnShutdown() override;
	void OnStateChange(int NewState, int OldState) override;

	// 切换记分板模式只中止该模式的在途预取，保留另一模式和统计页缓存。
	void SetMode(EQmAxiomMode Mode);
	EQmAxiomMode Mode() const { return m_Mode; }

	// 本帧为记分板上这个玩家准备分数，必要时发起请求。必须早于 GetLookup。
	void EnsureQueried(const char *pPlayerName);
	SQmAxiomLookupResult GetLookup(const char *pPlayerName) const;
	void Refresh(const char *pPlayerName);
	// 测试可关闭外部 DDStats 请求，避免旧的 Axiom 请求断言被额外请求干扰。
	void SetDdStatsEnabled(bool Enabled) { m_DdStatsEnabled = Enabled; }
	const SQmAxiomPlayerResult *GetResult(const char *pPlayerName) const;
	const std::vector<SQmDdStatsGameType> *GetDdStatsGameTypes(const char *pPlayerName) const;
	// 该玩家是否有 Axiom 请求在飞（搜索或模式）。
	// 首次查询时缓存条目可能刚建立，UI 不能再依赖 GetResult() 非空来判定「正在同步」。
	bool IsFetchingPlayer(const char *pPlayerName) const;
	// 该玩家 Axiom 侧是否处于失败态（搜索或任一模式）。
	bool IsPlayerFailed(const char *pPlayerName) const;
	int64_t LastSuccessfulSyncTimestamp() const { return m_LastSuccessfulSyncTimestamp; }
	void LoadPersistentCache(const json_value *pRoot);
	void WritePersistentCache(CJsonFileWriter &Writer) const;
	bool PersistentCacheDirty() const { return m_PersistentCacheDirty; }
	void ClearPersistentCacheDirty() { m_PersistentCacheDirty = false; }
};

#endif
