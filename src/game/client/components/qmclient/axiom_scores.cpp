#include "axiom_scores.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/http.h>

#include <cstddef>
#include <utility>

namespace
{
	constexpr int64_t AXIOM_MATCH_CACHE_TTL_MS = 2 * 60 * 60 * 1000;
	constexpr int64_t AXIOM_POINTS_CACHE_TTL_MS = 30 * 60 * 1000;
	constexpr int64_t AXIOM_FAILURE_RETRY_MS = 30 * 1000;
	constexpr int64_t AXIOM_MAX_RESPONSE_BYTES = 8 * 1024 * 1024;
	constexpr size_t AXIOM_MAX_CACHE_ENTRIES = 128;
	constexpr size_t AXIOM_MAX_QUERY_NAME_BYTES = 256;
	// 记分板最多 64 行，每名玩家同时只允许一个在途请求，槽位上限覆盖整屏查询。
	constexpr size_t AXIOM_MAX_CONCURRENT_REQUESTS = 64;
	// 每帧最多为此数量的新玩家发起搜索，避免一次进服打出几十个并发请求。
	constexpr int AXIOM_SEARCH_STARTS_PER_FRAME = 2;
	constexpr int AXIOM_CONNECT_TIMEOUT_MS = 5000;
	constexpr int AXIOM_SEARCH_TIMEOUT_MS = 10000;
	constexpr int AXIOM_INFO_TIMEOUT_MS = 45000;

	void PrepareRequest(CHttpRequest *pRequest, int TimeoutMs)
	{
		pRequest->Timeout(CTimeout{AXIOM_CONNECT_TIMEOUT_MS, TimeoutMs, 0, 0});
		pRequest->MaxResponseSize(AXIOM_MAX_RESPONSE_BYTES);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->HeaderString("Accept", "application/json");
		pRequest->HeaderString("User-Agent", "QmClient (https://github.com/wxj881027/QmClient)");
	}

	class CNativeAxiomHttpRequest final : public IQmAxiomHttpRequest
	{
		std::shared_ptr<CHttpRequest> m_pRequest;

	public:
		explicit CNativeAxiomHttpRequest(std::shared_ptr<CHttpRequest> pRequest) :
			m_pRequest(std::move(pRequest))
		{
		}

		bool Done() const override { return m_pRequest->Done(); }
		bool TransportSucceeded() const override { return m_pRequest->State() == EHttpState::DONE; }
		int StatusCode() const override { return m_pRequest->StatusCode(); }
		void Result(const unsigned char **ppData, size_t *pDataSize) const override
		{
			unsigned char *pData = nullptr;
			m_pRequest->Result(&pData, pDataSize);
			*ppData = pData;
		}
		void Abort() override { m_pRequest->Abort(); }
	};
}

EQmAxiomScoreStatus CQmAxiomScores::ParseStatus(EQmAxiomParseResult Result)
{
	switch(Result)
	{
	case EQmAxiomParseResult::SUCCESS: return EQmAxiomScoreStatus::READY;
	case EQmAxiomParseResult::NOT_FOUND: return EQmAxiomScoreStatus::NOT_FOUND;
	case EQmAxiomParseResult::AMBIGUOUS: return EQmAxiomScoreStatus::AMBIGUOUS;
	case EQmAxiomParseResult::API_ERROR: return EQmAxiomScoreStatus::API_ERROR;
	case EQmAxiomParseResult::INVALID_RESPONSE: return EQmAxiomScoreStatus::INVALID_RESPONSE;
	}
	return EQmAxiomScoreStatus::INVALID_RESPONSE;
}

bool CQmAxiomScores::IsFailureStatus(EQmAxiomScoreStatus Status)
{
	return Status == EQmAxiomScoreStatus::NOT_FOUND ||
	       Status == EQmAxiomScoreStatus::AMBIGUOUS ||
	       Status == EQmAxiomScoreStatus::HTTP_ERROR ||
	       Status == EQmAxiomScoreStatus::API_ERROR ||
	       Status == EQmAxiomScoreStatus::INVALID_RESPONSE;
}

bool CQmAxiomScores::IsWithinWindow(int64_t Timestamp, int64_t Now, int64_t WindowMs)
{
	if(Timestamp <= 0 || Now < Timestamp)
		return false;
	return Now - Timestamp < WindowMs * time_freq() / 1000;
}

int64_t CQmAxiomScores::CurrentTick() const
{
	return time_get();
}

std::shared_ptr<IQmAxiomHttpRequest> CQmAxiomScores::StartRequest(const char *pUrl, int TimeoutMs)
{
	if(m_pHttpOverride != nullptr)
		return m_pHttpOverride->Get(pUrl, AXIOM_CONNECT_TIMEOUT_MS, TimeoutMs, AXIOM_MAX_RESPONSE_BYTES);
	if(Http() == nullptr)
		return nullptr;

	auto pRequest = std::make_shared<CHttpRequest>(pUrl);
	PrepareRequest(pRequest.get(), TimeoutMs);
	Http()->Run(pRequest);
	return std::make_shared<CNativeAxiomHttpRequest>(std::move(pRequest));
}

bool CQmAxiomScores::StartSearchRequest(const std::string &PlayerName, SCacheEntry &Entry)
{
	Entry.m_SearchStatus = EQmAxiomScoreStatus::FETCHING;
	Entry.m_Match = {};
	// 保留已有点数：换名字后重新搜索时，旧点数仍可与新 user_id 匹配而继续使用。
	Entry.m_PointsStatus = EQmAxiomScoreStatus::NOT_REQUESTED;
	Entry.m_Points = 0;
	Entry.m_LastPointsSuccessTick = 0;
	Entry.m_LastPointsFailureTick = 0;

	const std::string Url = QmBuildAxiomSearchUrl(PlayerName.c_str());
	if(Url.empty())
	{
		Entry.m_SearchStatus = EQmAxiomScoreStatus::INVALID_RESPONSE;
		Entry.m_LastSearchFailureTick = CurrentTick();
		return false;
	}

	SRequestSlot Slot;
	Slot.m_pRequest = StartRequest(Url.c_str(), AXIOM_SEARCH_TIMEOUT_MS);
	if(!Slot.m_pRequest)
	{
		Entry.m_SearchStatus = EQmAxiomScoreStatus::HTTP_ERROR;
		Entry.m_LastSearchFailureTick = CurrentTick();
		return false;
	}
	Slot.m_Generation = m_Generation;
	Slot.m_PlayerName = PlayerName;
	// 搜索结果与模式无关，但仍要记录发起时的模式，避免换模式后旧响应被采纳。
	Slot.m_Mode = m_Mode;
	m_SearchRequests[PlayerName] = std::move(Slot);
	return true;
}

bool CQmAxiomScores::StartModeRequest(const std::string &PlayerName, SCacheEntry &Entry)
{
	Entry.m_PointsStatus = EQmAxiomScoreStatus::FETCHING;
	Entry.m_Points = 0;

	if(Entry.m_Match.m_UserId <= 0 || m_Mode == EQmAxiomMode::NONE)
	{
		Entry.m_PointsStatus = EQmAxiomScoreStatus::HTTP_ERROR;
		Entry.m_LastPointsFailureTick = CurrentTick();
		return false;
	}

	SRequestSlot Slot;
	const std::string Url = QmBuildAxiomInfoUrl(Entry.m_Match.m_UserId, m_Mode);
	Slot.m_pRequest = StartRequest(Url.c_str(), AXIOM_INFO_TIMEOUT_MS);
	if(!Slot.m_pRequest)
	{
		Entry.m_PointsStatus = EQmAxiomScoreStatus::HTTP_ERROR;
		Entry.m_LastPointsFailureTick = CurrentTick();
		return false;
	}
	Slot.m_Generation = m_Generation;
	Slot.m_PlayerName = PlayerName;
	Slot.m_Mode = m_Mode;
	m_ModeRequests[PlayerName] = std::move(Slot);
	return true;
}

int CQmAxiomScores::CountRequestsForPlayer(const std::string &PlayerName) const
{
	int Count = 0;
	if(m_SearchRequests.contains(PlayerName))
		++Count;
	if(m_ModeRequests.contains(PlayerName))
		++Count;
	return Count;
}

void CQmAxiomScores::AbortActiveRequests(bool ResetFetchingStates)
{
	for(auto &[PlayerName, Slot] : m_SearchRequests)
	{
		Slot.m_pRequest->Abort();
		if(ResetFetchingStates)
		{
			const auto CacheIt = m_Cache.find(PlayerName);
			if(CacheIt != m_Cache.end() && CacheIt->second.m_SearchStatus == EQmAxiomScoreStatus::FETCHING)
				CacheIt->second.m_SearchStatus = EQmAxiomScoreStatus::NOT_REQUESTED;
		}
	}
	m_SearchRequests.clear();

	for(auto &[PlayerName, Slot] : m_ModeRequests)
	{
		Slot.m_pRequest->Abort();
		if(ResetFetchingStates)
		{
			const auto CacheIt = m_Cache.find(PlayerName);
			if(CacheIt != m_Cache.end() && CacheIt->second.m_PointsStatus == EQmAxiomScoreStatus::FETCHING)
				CacheIt->second.m_PointsStatus = EQmAxiomScoreStatus::NOT_REQUESTED;
		}
	}
	m_ModeRequests.clear();

	++m_Generation;
}

void CQmAxiomScores::SetMode(EQmAxiomMode Mode)
{
	if(Mode == m_Mode)
		return;
	ResetForMode(Mode);
}

void CQmAxiomScores::ResetForMode(EQmAxiomMode Mode)
{
	AbortActiveRequests(true);
	m_Mode = Mode;
	// 两个模式的积分互不相通，切服或换模式后必须丢弃上一模式的分数。
	m_Cache.clear();
}

void CQmAxiomScores::EvictCacheEntryIfNeeded()
{
	if(m_Cache.size() < AXIOM_MAX_CACHE_ENTRIES)
		return;
	auto Oldest = m_Cache.end();
	for(auto It = m_Cache.begin(); It != m_Cache.end(); ++It)
	{
		if(Oldest == m_Cache.end() || It->second.m_LastAccessTick < Oldest->second.m_LastAccessTick)
			Oldest = It;
	}
	if(Oldest != m_Cache.end())
		m_Cache.erase(Oldest);
}

void CQmAxiomScores::EnsureQueried(const char *pPlayerName)
{
	if(!pPlayerName || pPlayerName[0] == '\0' || str_length(pPlayerName) > AXIOM_MAX_QUERY_NAME_BYTES || !str_utf8_check(pPlayerName))
		return;
	if(m_Mode == EQmAxiomMode::NONE)
		return;

	const int64_t Now = CurrentTick();
	std::string PlayerName(pPlayerName);
	auto CacheIt = m_Cache.find(PlayerName);
	if(CacheIt == m_Cache.end())
	{
		EvictCacheEntryIfNeeded();
		CacheIt = m_Cache.emplace(PlayerName, SCacheEntry{}).first;
	}
	SCacheEntry &Entry = CacheIt->second;
	Entry.m_LastAccessTick = Now;

	const bool MatchFresh = Entry.m_SearchStatus == EQmAxiomScoreStatus::READY &&
				IsWithinWindow(Entry.m_LastSearchSuccessTick, Now, AXIOM_MATCH_CACHE_TTL_MS);
	if(!MatchFresh)
	{
		if(m_SearchRequests.contains(PlayerName))
			return;
		if(IsFailureStatus(Entry.m_SearchStatus) && IsWithinWindow(Entry.m_LastSearchFailureTick, Now, AXIOM_FAILURE_RETRY_MS))
			return;
		if(m_SearchRequests.size() >= AXIOM_MAX_CONCURRENT_REQUESTS || m_SearchStartsThisFrame >= AXIOM_SEARCH_STARTS_PER_FRAME)
			return;

		if(StartSearchRequest(PlayerName, Entry))
			++m_SearchStartsThisFrame;
		return;
	}

	const bool PointsFresh = Entry.m_PointsStatus == EQmAxiomScoreStatus::READY &&
				 IsWithinWindow(Entry.m_LastPointsSuccessTick, Now, AXIOM_POINTS_CACHE_TTL_MS);
	if(PointsFresh || m_ModeRequests.contains(PlayerName))
		return;
	if(IsFailureStatus(Entry.m_PointsStatus) && IsWithinWindow(Entry.m_LastPointsFailureTick, Now, AXIOM_FAILURE_RETRY_MS))
		return;
	if(m_ModeRequests.size() >= AXIOM_MAX_CONCURRENT_REQUESTS)
		return;

	StartModeRequest(PlayerName, Entry);
}

SQmAxiomLookupResult CQmAxiomScores::GetLookup(const char *pPlayerName) const
{
	SQmAxiomLookupResult Result;
	if(!pPlayerName || pPlayerName[0] == '\0')
		return Result;

	const auto It = m_Cache.find(pPlayerName);
	if(It == m_Cache.end())
		return Result;

	const SCacheEntry &Entry = It->second;
	Result.m_Points = Entry.m_Points;
	if(Entry.m_SearchStatus != EQmAxiomScoreStatus::READY)
	{
		Result.m_Status = Entry.m_SearchStatus;
		return Result;
	}
	Result.m_Status = Entry.m_PointsStatus;
	return Result;
}

void CQmAxiomScores::ProcessSearchRequests()
{
	for(auto It = m_SearchRequests.begin(); It != m_SearchRequests.end();)
	{
		const std::string PlayerName = It->first;
		SRequestSlot &Slot = It->second;
		if(!Slot.m_pRequest->Done())
		{
			++It;
			continue;
		}

		std::shared_ptr<IQmAxiomHttpRequest> pRequest = std::move(Slot.m_pRequest);
		const uint64_t ResponseGeneration = Slot.m_Generation;
		It = m_SearchRequests.erase(It);
		if(!QmAxiomResponseIsCurrent(m_Generation, ResponseGeneration, m_Mode, Slot.m_Mode))
			continue;

		const auto CacheIt = m_Cache.find(PlayerName);
		if(CacheIt == m_Cache.end())
			continue;
		SCacheEntry &Entry = CacheIt->second;
		const int64_t Now = CurrentTick();

		if(!pRequest->TransportSucceeded() || pRequest->StatusCode() != 200)
		{
			Entry.m_SearchStatus = EQmAxiomScoreStatus::HTTP_ERROR;
			Entry.m_LastSearchFailureTick = Now;
			continue;
		}

		const unsigned char *pData = nullptr;
		size_t DataSize = 0;
		pRequest->Result(&pData, &DataSize);
		SQmAxiomSearchMatch Match;
		const EQmAxiomParseResult ParseResult = QmParseAxiomSearchResponse(reinterpret_cast<const char *>(pData), DataSize, PlayerName.c_str(), Match);
		Entry.m_SearchStatus = ParseStatus(ParseResult);
		if(ParseResult != EQmAxiomParseResult::SUCCESS)
		{
			Entry.m_LastSearchFailureTick = Now;
			continue;
		}

		Entry.m_Match = std::move(Match);
		Entry.m_LastSearchSuccessTick = Now;
		Entry.m_LastSearchFailureTick = 0;
	}
}

void CQmAxiomScores::ProcessModeRequests()
{
	for(auto It = m_ModeRequests.begin(); It != m_ModeRequests.end();)
	{
		const std::string PlayerName = It->first;
		SRequestSlot &Slot = It->second;
		if(!Slot.m_pRequest->Done())
		{
			++It;
			continue;
		}

		std::shared_ptr<IQmAxiomHttpRequest> pRequest = std::move(Slot.m_pRequest);
		const uint64_t ResponseGeneration = Slot.m_Generation;
		const EQmAxiomMode ResponseMode = Slot.m_Mode;
		It = m_ModeRequests.erase(It);
		if(!QmAxiomResponseIsCurrent(m_Generation, ResponseGeneration, m_Mode, ResponseMode))
			continue;

		const auto CacheIt = m_Cache.find(PlayerName);
		if(CacheIt == m_Cache.end())
			continue;
		SCacheEntry &Entry = CacheIt->second;
		const int64_t Now = CurrentTick();

		if(!pRequest->TransportSucceeded() || pRequest->StatusCode() != 200)
		{
			Entry.m_PointsStatus = EQmAxiomScoreStatus::HTTP_ERROR;
			Entry.m_LastPointsFailureTick = Now;
			continue;
		}

		const unsigned char *pData = nullptr;
		size_t DataSize = 0;
		pRequest->Result(&pData, &DataSize);
		SQmAxiomModeScore Score;
		const EQmAxiomParseResult ParseResult = QmParseAxiomInfoResponse(reinterpret_cast<const char *>(pData), DataSize, Score);
		Entry.m_PointsStatus = ParseStatus(ParseResult);
		if(ParseResult != EQmAxiomParseResult::SUCCESS || Score.m_PlayerName != Entry.m_Match.m_PlayerName)
		{
			if(ParseResult == EQmAxiomParseResult::SUCCESS)
				Entry.m_PointsStatus = EQmAxiomScoreStatus::INVALID_RESPONSE;
			Entry.m_LastPointsFailureTick = Now;
			continue;
		}

		Entry.m_Points = Score.m_Points;
		Entry.m_LastPointsSuccessTick = Now;
		Entry.m_LastPointsFailureTick = 0;
	}
}

void CQmAxiomScores::OnUpdate()
{
	m_SearchStartsThisFrame = 0;
	ProcessSearchRequests();
	ProcessModeRequests();
}

void CQmAxiomScores::OnReset()
{
	AbortActiveRequests(true);
}

void CQmAxiomScores::OnShutdown()
{
	AbortActiveRequests(false);
	m_Cache.clear();
}

void CQmAxiomScores::OnStateChange(int NewState, int OldState)
{
	if(NewState < IClient::STATE_ONLINE)
		AbortActiveRequests(true);
}
