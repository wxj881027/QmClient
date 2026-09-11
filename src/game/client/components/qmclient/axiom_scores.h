#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_AXIOM_SCORES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_AXIOM_SCORES_H

#include "axiom_scores_data.h"

#include <engine/shared/http.h>

#include <game/client/component.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

class IQmAxiomHttpRequest
{
public:
	virtual ~IQmAxiomHttpRequest() = default;
	virtual bool Done() const = 0;
	virtual bool TransportSucceeded() const = 0;
	virtual int StatusCode() const = 0;
	virtual void Result(const unsigned char **ppData, size_t *pDataSize) const = 0;
	virtual void Abort() = 0;
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

class CQmAxiomScores : public CComponent
{
	struct SCacheEntry
	{
		SQmAxiomSearchMatch m_Match;
		EQmAxiomScoreStatus m_SearchStatus = EQmAxiomScoreStatus::NOT_REQUESTED;
		EQmAxiomScoreStatus m_PointsStatus = EQmAxiomScoreStatus::NOT_REQUESTED;
		int64_t m_Points = 0;
		int64_t m_LastSearchSuccessTick = 0;
		int64_t m_LastSearchFailureTick = 0;
		int64_t m_LastPointsSuccessTick = 0;
		int64_t m_LastPointsFailureTick = 0;
		int64_t m_LastAccessTick = 0;
	};

	struct SRequestSlot
	{
		std::shared_ptr<IQmAxiomHttpRequest> m_pRequest;
		uint64_t m_Generation = 0;
		std::string m_PlayerName;
		EQmAxiomMode m_Mode = EQmAxiomMode::NONE;
	};

	std::map<std::string, SCacheEntry> m_Cache;
	std::map<std::string, SRequestSlot> m_SearchRequests;
	std::map<std::string, SRequestSlot> m_ModeRequests;
	EQmAxiomMode m_Mode = EQmAxiomMode::NONE;
	int m_SearchStartsThisFrame = 0;
	uint64_t m_Generation = 0;
	IQmAxiomHttp *m_pHttpOverride = nullptr;

	static bool IsFailureStatus(EQmAxiomScoreStatus Status);
	static EQmAxiomScoreStatus ParseStatus(EQmAxiomParseResult Result);
	static bool IsWithinWindow(int64_t Timestamp, int64_t Now, int64_t WindowMs);

	void AbortActiveRequests(bool ResetFetchingStates);
	void ResetForMode(EQmAxiomMode Mode);
	void EvictCacheEntryIfNeeded();
	bool StartSearchRequest(const std::string &PlayerName, SCacheEntry &Entry);
	bool StartModeRequest(const std::string &PlayerName, SCacheEntry &Entry);
	int CountRequestsForPlayer(const std::string &PlayerName) const;
	void ProcessSearchRequests();
	void ProcessModeRequests();
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

	// 切换到某个 Axiom 模式（Gores / AXRace）。模式变化时会中止请求并清空缓存。
	void SetMode(EQmAxiomMode Mode);
	EQmAxiomMode Mode() const { return m_Mode; }

	// 本帧为记分板上这个玩家准备分数，必要时发起请求。必须早于 GetLookup。
	void EnsureQueried(const char *pPlayerName);
	// 只读查询缓存，不发起请求。
	SQmAxiomLookupResult GetLookup(const char *pPlayerName) const;
};

#endif
