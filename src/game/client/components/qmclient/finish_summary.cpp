#include "finish_summary.h"

#include "axiom_scores_data.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/engine.h>
#include <engine/http.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/client/race.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace QmFinishSummary;

class CQmFinishSummary::CParseJob : public IJob
{
	std::shared_ptr<IHttpRequest> m_pRequest;
	EQuery m_Query;
	ECommunity m_Community;
	std::string m_Map;
	std::string m_Player;
	std::string m_Category;
	int64_t m_UserId;

	void Run() override
	{
		unsigned char *pData = nullptr;
		size_t Length = 0;
		m_pRequest->Result(&pData, &Length);
		const char *pJson = reinterpret_cast<const char *>(pData);
		const char *pMode = m_Community == ECommunity::AXIOM_GORES ? "gores" : "axrace";
		switch(m_Query)
		{
		case EQuery::MAP:
			m_Success = m_Community == ECommunity::DDNET ? ParseDdnetMap(pJson, Length, m_Map, m_Result) : ParseAxiomMap(pJson, Length, m_Map, m_Result, pMode);
			break;
		case EQuery::PLAYER: m_Success = ParseDdnetPlayer(pJson, Length, m_Player, m_Map, m_Result); break;
		case EQuery::CHINA: m_Success = ParseDdnetChinaRank(pJson, Length, m_Player, m_Map, m_Result); break;
		case EQuery::IDENTITY: m_Success = ParseAxiomSearch(pJson, Length, m_Player, m_Result); break;
		case EQuery::RANK: m_Success = ParseAxiomTop(pJson, Length, m_Map, m_UserId, m_Result, pMode); break;
		case EQuery::POINTS: m_Success = ParseAxiomPoints(pJson, Length, m_UserId, m_Result, pMode); break;
		case EQuery::PROGRESS: m_Success = ParseAxiomProgress(pJson, Length, m_UserId, m_Category, m_Result, pMode); break;
		case EQuery::TIMES: m_Success = ParseAxiomTimes(pJson, Length, m_Map, m_UserId, m_Result, pMode); break;
		case EQuery::COUNT: break;
		}
		m_pRequest.reset();
	}

public:
	bool m_Success = false;
	SRemoteData m_Result;
	uint64_t m_Generation;
	CParseJob(std::shared_ptr<IHttpRequest> pRequest, EQuery Query, ECommunity Community, const std::string &Map, const std::string &Player, const SRemoteData &Data, uint64_t Generation) :
		m_pRequest(std::move(pRequest)), m_Query(Query), m_Community(Community), m_Map(Map), m_Player(Player), m_Category(Data.m_Category), m_UserId(Data.m_UserId), m_Generation(Generation) {}
};

double CQmFinishSummary::Now()
{
	return time_get() / static_cast<double>(time_freq());
}

CQmFinishSummary::~CQmFinishSummary()
{
	CancelRequests();
}

void CQmFinishSummary::CancelRequests()
{
	++m_Generation;
	for(auto &Request : m_aRequests)
	{
		if(Request.m_pHttp)
			Request.m_pHttp->Abort();
		// 解析任务只持有自己的请求与值数据，释放句柄即可隔离迟到结果。
		Request = {};
		Request.m_Done = true;
	}
}

void CQmFinishSummary::OnReset()
{
	CancelRequests();
	m_State.Reset();
	m_pManagedTeeRenderInfo.reset();
	m_Data = {};
	m_Before = {};
	m_Community = ECommunity::NONE;
	m_Map.clear();
	m_Player.clear();
	m_ClientId = -1;
	m_Connection = -1;
	m_RunStartTick = -1;
	m_FinishGameTick = -1;
	m_FinishTimestamp = 0;
	m_PreviousBest.reset();
	m_PreviousHistoryKnown = false;
	m_vFinishOnlineTimes.clear();
	m_FinishOnlineTimesKnown = false;
}

void CQmFinishSummary::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE)
		OnReset();
}

bool CQmFinishSummary::UpdateContext()
{
	if(!g_Config.m_QmFinishSummary || g_Config.m_QmHudIslandUseOriginalStyle || !g_Config.m_ClShowhud || Client()->State() != IClient::STATE_ONLINE || GameClient()->m_RankGhost.IsViewModeActive())
	{
		if(m_Community != ECommunity::NONE)
			OnReset();
		return false;
	}
	const int ClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
	{
		if(m_Community != ECommunity::NONE)
			OnReset();
		return false;
	}
	const CServerInfo &Server = Client()->ServerInfo();
	const CServerInfo *pCommunityServer = &Server;
	if(Server.m_aCommunityId[0] == '\0')
	{
		const auto *pEntry = ServerBrowser() && Client()->ServerAddress() ? ServerBrowser()->Find(*Client()->ServerAddress()) : nullptr;
		if(pEntry)
			pCommunityServer = &pEntry->m_Info;
		else if(GameClient()->m_ConnectServerInfo)
			pCommunityServer = &*GameClient()->m_ConnectServerInfo;
	}
	ECommunity Community = ECommunity::NONE;
	if(str_comp(pCommunityServer->m_aCommunityId, IServerBrowser::COMMUNITY_DDNET) == 0)
		Community = ECommunity::DDNET;
	else if(GameClient()->m_QmAxiomAutoLogin.IsAxiomCommunity())
	{
		const EQmAxiomMode Mode = QmResolveAxiomModeFromServerContext({pCommunityServer->m_aCommunityType, Server.m_aName});
		if(Mode == EQmAxiomMode::GORES)
			Community = ECommunity::AXIOM_GORES;
		else if(Mode == EQmAxiomMode::AXRACE)
			Community = ECommunity::AXIOM_AXRACE;
	}
	if(Community == ECommunity::NONE || Server.m_aMap[0] == '\0')
	{
		if(m_Community != ECommunity::NONE)
			OnReset();
		return false;
	}
	const char *pPlayer = GameClient()->m_aClients[ClientId].m_aName;
	if(m_Community != Community || m_Map != Server.m_aMap || m_Player != pPlayer || m_ClientId != ClientId || m_Connection != g_Config.m_ClDummy)
	{
		OnReset();
		m_Community = Community;
		m_Map = Server.m_aMap;
		m_Player = pPlayer;
		m_ClientId = ClientId;
		m_Connection = g_Config.m_ClDummy;
		PrepareRequests(false);
	}
	return true;
}

void CQmFinishSummary::PrepareRequests(bool AfterFinish)
{
	CancelRequests();
	// 刷新成绩时只保留地图资料与账号身份，旧成绩由 Before 单独标注。
	if(AfterFinish)
	{
		SRemoteData Metadata;
		Metadata.m_Map = m_Data.m_Map;
		Metadata.m_Mapper = m_Data.m_Mapper;
		Metadata.m_Category = m_Data.m_Category;
		Metadata.m_Stars = m_Data.m_Stars;
		Metadata.m_MapPoints = m_Data.m_MapPoints;
		Metadata.m_UserId = m_Data.m_UserId;
		m_Data = std::move(Metadata);
	}
	for(size_t i = 0; i < m_aRequests.size(); ++i)
	{
		const EQuery Query = static_cast<EQuery>(i);
		bool Needed = Query == EQuery::MAP;
		if(m_Community == ECommunity::DDNET)
			Needed = Needed || Query == EQuery::PLAYER || Query == EQuery::CHINA;
		else
			Needed = Needed || Query >= EQuery::IDENTITY;
		m_aRequests[i].m_Done = !Needed;
	}
	if(AfterFinish && !m_Data.m_Category.empty())
		m_aRequests[static_cast<size_t>(EQuery::MAP)].m_Done = true;
	if(m_Data.m_UserId >= 0)
		m_aRequests[static_cast<size_t>(EQuery::IDENTITY)].m_Done = true;
	m_NextRequest = Now() + (AfterFinish ? 0.5 : 0.0);
}

std::string CQmFinishSummary::QueryUrl(EQuery Query) const
{
	char aMap[MAX_MAP_LENGTH * 3];
	char aPlayer[MAX_NAME_LENGTH * 3];
	EscapeUrl(aMap, m_Map.c_str());
	EscapeUrl(aPlayer, m_Player.c_str());
	char aUrl[2048];
	if(m_Community == ECommunity::DDNET)
	{
		if(Query == EQuery::PLAYER)
			str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aPlayer);
		else
			str_format(aUrl, sizeof(aUrl), "https://ddnet.org/maps/?json=%s%s", aMap, Query == EQuery::CHINA ? "&country=CHN" : "");
		return aUrl;
	}
	const char *pMode = m_Community == ECommunity::AXIOM_GORES ? "gores" : "axrace";
	const char *pBase = "https://api.axiom.teeworlds.cn/v1/open";
	switch(Query)
	{
	case EQuery::MAP: str_format(aUrl, sizeof(aUrl), "%s/map/info?mode=%s&map=%s", pBase, pMode, aMap); break;
	case EQuery::IDENTITY: str_format(aUrl, sizeof(aUrl), "%s/user/info?name=%s", pBase, aPlayer); break;
	case EQuery::RANK: str_format(aUrl, sizeof(aUrl), "%s/map/top?mode=%s&map=%s&page=1&limit=200", pBase, pMode, aMap); break;
	case EQuery::POINTS: str_format(aUrl, sizeof(aUrl), "%s/user/points?mode=%s&user_id=%lld", pBase, pMode, static_cast<long long>(m_Data.m_UserId)); break;
	case EQuery::PROGRESS: str_format(aUrl, sizeof(aUrl), "%s/user/difficulty?mode=%s&user_id=%lld", pBase, pMode, static_cast<long long>(m_Data.m_UserId)); break;
	case EQuery::TIMES: str_format(aUrl, sizeof(aUrl), "%s/player/times?mode=%s&map=%s&offset=1", pBase, pMode, aMap); break;
	default: return {};
	}
	return aUrl;
}

void CQmFinishSummary::MergeResult(EQuery Query, const SRemoteData &Result)
{
	switch(Query)
	{
	case EQuery::MAP:
		m_Data.m_Map = Result.m_Map;
		m_Data.m_Mapper = Result.m_Mapper;
		m_Data.m_Category = Result.m_Category;
		m_Data.m_Stars = Result.m_Stars;
		m_Data.m_MapPoints = Result.m_MapPoints;
		break;
	case EQuery::PLAYER:
		m_Data.m_BestTimeMs = Result.m_BestTimeMs;
		m_Data.m_HistoryKnown = Result.m_HistoryKnown;
		m_Data.m_Rank = Result.m_Rank;
		m_Data.m_Points = Result.m_Points;
		m_Data.m_CompletedMaps = Result.m_CompletedMaps;
		m_Data.m_TotalMaps = Result.m_TotalMaps;
		m_Data.m_LatestTimeMs = Result.m_LatestTimeMs;
		m_Data.m_LatestTimestamp = Result.m_LatestTimestamp;
		if(m_Data.m_Category.empty())
			m_Data.m_Category = Result.m_Category;
		if(m_Data.m_MapPoints < 0)
			m_Data.m_MapPoints = Result.m_MapPoints;
		break;
	case EQuery::CHINA:
		m_Data.m_ChinaRank = Result.m_ChinaRank;
		m_Data.m_ChinaBestTimeMs = Result.m_ChinaBestTimeMs;
		m_Data.m_ChinaRankKnown = Result.m_ChinaRankKnown;
		break;
	case EQuery::IDENTITY:
		m_Data.m_UserId = Result.m_UserId;
		break;
	case EQuery::RANK:
		m_Data.m_Rank = Result.m_Rank;
		m_Data.m_AccountBestTimeMs = Result.m_AccountBestTimeMs;
		m_Data.m_AccountHistoryKnown = Result.m_AccountHistoryKnown;
		break;
	case EQuery::POINTS: m_Data.m_Points = Result.m_Points; break;
	case EQuery::PROGRESS:
		m_Data.m_CompletedMaps = Result.m_CompletedMaps;
		m_Data.m_TotalMaps = Result.m_TotalMaps;
		break;
	case EQuery::TIMES:
		m_Data.m_LatestTimeMs = Result.m_LatestTimeMs;
		m_Data.m_LatestTimestamp = Result.m_LatestTimestamp;
		break;
	case EQuery::COUNT: break;
	}
}

void CQmFinishSummary::UpdateRequests(double CurrentTime)
{
	bool Changed = false;
	for(size_t i = 0; i < m_aRequests.size(); ++i)
	{
		auto &Request = m_aRequests[i];
		const EQuery Query = static_cast<EQuery>(i);
		if(Request.m_pHttp && Request.m_pHttp->Done())
		{
			Changed = true;
			if(Request.m_pHttp->State() == EHttpState::DONE && Request.m_pHttp->StatusCode() == 200)
			{
				Request.m_pJob = std::make_shared<CParseJob>(Request.m_pHttp, Query, m_Community, m_Map, m_Player, m_Data, m_Generation);
				Engine()->AddJob(Request.m_pJob);
			}
			else
			{
				const auto RetryAfter = Request.m_pHttp->ResultRetryAfterSeconds();
				Request.m_NotBefore = CurrentTime + std::max<int64_t>(3, RetryAfter.value_or(3));
				if(RetryAfter)
					m_NextRequest = std::max(m_NextRequest, Request.m_NotBefore);
				Request.m_Done = Request.m_Attempts >= 2;
			}
			Request.m_pHttp.reset();
		}
		if(Request.m_pJob && Request.m_pJob->State() == IJob::STATE_DONE)
		{
			Changed = true;
			if(Request.m_pJob->m_Generation == m_Generation && Request.m_pJob->m_Success)
			{
				MergeResult(Query, Request.m_pJob->m_Result);
				Request.m_Succeeded = true;
				Request.m_Done = true;
				// 通关后的动态资料最多再查一次，让成绩数据库有机会完成更新。
				if(m_FinishTimestamp > 0 && Query != EQuery::MAP && Query != EQuery::IDENTITY && Request.m_Attempts < 2)
					Request.m_Done = false;
			}
			else
				Request.m_Done = Request.m_Attempts >= 2;
			Request.m_NotBefore = CurrentTime + 3.0;
			Request.m_pJob.reset();
		}
		if(Request.m_Done || Request.m_pHttp || Request.m_pJob || CurrentTime < Request.m_NotBefore || CurrentTime < m_NextRequest)
			continue;
		if(Query >= EQuery::RANK && m_Data.m_UserId < 0)
		{
			Request.m_Done = m_aRequests[static_cast<size_t>(EQuery::IDENTITY)].m_Done;
			Changed |= Request.m_Done;
			continue;
		}
		if(Query == EQuery::PROGRESS && m_Data.m_Category.empty())
		{
			Request.m_Done = m_aRequests[static_cast<size_t>(EQuery::MAP)].m_Done;
			Changed |= Request.m_Done;
			continue;
		}
		const std::string Url = QueryUrl(Query);
		if(Url.empty())
		{
			Request.m_Done = true;
			continue;
		}
		Request.m_pHttp = HttpGet(Url.c_str());
		Request.m_pHttp->Timeout({3000, 8000, 128, 5});
		Request.m_pHttp->MaxResponseSize(Query == EQuery::PLAYER ? 8 * 1024 * 1024 : 2 * 1024 * 1024);
		Request.m_pHttp->LogProgress(HTTPLOG::NONE);
		Http()->Run(Request.m_pHttp);
		++Request.m_Attempts;
		m_NextRequest = CurrentTime + 0.5;
	}
	if(Changed)
		RefreshSnapshot();
}

void CQmFinishSummary::RefreshSnapshot()
{
	auto *pSnapshot = m_State.MutableSnapshot();
	if(!pSnapshot || !m_State.Visible())
		return;
	const bool Confirmed = m_Data.m_LatestTimeMs && m_Data.m_LatestTimestamp &&
			       (*m_Data.m_LatestTimeMs + 5) / 10 == (pSnapshot->m_TimeMs + 5) / 10 &&
			       std::abs(*m_Data.m_LatestTimestamp - m_FinishTimestamp) <= 120 &&
			       (!m_Before.m_LatestTimestamp || *m_Data.m_LatestTimestamp > *m_Before.m_LatestTimestamp);
	ApplyRemote(*pSnapshot, m_Before, m_Data, Confirmed);
	++pSnapshot->m_Revision;
	const auto Finalize = [&](SValue &Value, EQuery Query, bool AwaitingSync = false) {
		if(Value.m_Value >= 0 || Value.m_Status == EStatus::UNAVAILABLE)
			return;
		const auto &Request = m_aRequests[static_cast<size_t>(Query)];
		if(!Request.m_Succeeded && !Request.m_Done)
			Value.m_Status = EStatus::LOADING;
		else if(!Request.m_Succeeded || !AwaitingSync)
			Value.m_Status = EStatus::UNAVAILABLE;
	};
	const bool Ddnet = m_Community == ECommunity::DDNET;
	Finalize(pSnapshot->m_Rank, Ddnet ? EQuery::PLAYER : EQuery::RANK, Ddnet && m_Data.m_HistoryKnown && !m_Data.m_BestTimeMs);
	Finalize(pSnapshot->m_ChinaRank, EQuery::CHINA);
	Finalize(pSnapshot->m_Points, Ddnet ? EQuery::PLAYER : EQuery::POINTS);
	Finalize(pSnapshot->m_CompletedMaps, Ddnet ? EQuery::PLAYER : EQuery::PROGRESS);
	Finalize(pSnapshot->m_TotalMaps, Ddnet ? EQuery::PLAYER : EQuery::PROGRESS);
	const bool FirstFinish = Ddnet ? m_Before.m_HistoryKnown && !m_Before.m_BestTimeMs : m_Before.m_AccountHistoryKnown && !m_Before.m_AccountBestTimeMs;
	Finalize(pSnapshot->m_PointsGained, Ddnet ? EQuery::PLAYER : EQuery::TIMES,
		FirstFinish && (m_Data.m_MapPoints >= 0 || m_Before.m_MapPoints >= 0));
}

void CQmFinishSummary::StartRun(int StartTick)
{
	m_RunStartTick = StartTick;
	if(m_FinishGameTick < 0 || StartTick <= m_FinishGameTick)
		return;
	m_State.StartRun();
	m_FinishTimestamp = 0;
	m_FinishGameTick = -1;
	PrepareRequests(false);
}

void CQmFinishSummary::OnNewSnapshot()
{
	if(!g_Config.m_QmFinishSummary || m_FinishGameTick < 0 || !UpdateContext())
		return;
	const auto &Snap = GameClient()->m_Snap;
	if(!Snap.m_pGameInfoObj || (Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME) ||
		Snap.m_SpecInfo.m_Active || !Snap.m_pLocalCharacter || !Snap.m_pLocalPrevCharacter ||
		Client()->GameTick(m_Connection) <= m_FinishGameTick)
		return;
	// broadcast/none 计时模式没有起跑 tick，复用录像功能的起跑线检测。
	const vec2 Previous(Snap.m_pLocalPrevCharacter->m_X, Snap.m_pLocalPrevCharacter->m_Y);
	const vec2 Current(Snap.m_pLocalCharacter->m_X, Snap.m_pLocalCharacter->m_Y);
	if(GameClient()->RaceHelper()->IsStart(Previous, Current))
		StartRun(Client()->GameTick(m_Connection));
}

void CQmFinishSummary::OnUpdate()
{
	if(!UpdateContext())
		return;
	const double CurrentTime = Now();
	const auto *pGameInfo = GameClient()->m_Snap.m_pGameInfoObj;
	const int StartTick = pGameInfo && (pGameInfo->m_GameStateFlags & GAMESTATEFLAG_RACETIME) && pGameInfo->m_WarmupTimer < 0 && pGameInfo->m_WarmupTimer > std::numeric_limits<int>::min() ? -pGameInfo->m_WarmupTimer : -1;
	if(!GameClient()->m_Snap.m_SpecInfo.m_Active && StartTick > 0 && m_RunStartTick != StartTick)
		StartRun(StartTick);
	const bool WasVisible = m_State.Visible();
	m_State.Advance(CurrentTime);
	if(WasVisible && !m_State.Visible())
		CancelRequests();
	UpdateRequests(CurrentTime);
	if(!m_State.Visible())
	{
		m_PreviousBest = m_Data.m_BestTimeMs;
		m_PreviousHistoryKnown = m_Data.m_HistoryKnown;
		if(GameClient()->m_ReceivedDDNetPlayerFinishTimes)
		{
			const auto &Player = GameClient()->m_aClients[m_ClientId];
			if(Player.m_FinishTimeSeconds >= 0)
			{
				m_PreviousBest = static_cast<int64_t>(Player.m_FinishTimeSeconds) * 1000 + std::max(0, Player.m_FinishTimeMillis);
				m_PreviousHistoryKnown = true;
			}
			else if(Player.m_FinishTimeSeconds == FinishTime::NOT_FINISHED_MILLIS)
			{
				m_PreviousBest.reset();
				m_PreviousHistoryKnown = true;
			}
		}
	}
}

void CQmFinishSummary::Finish(int64_t TimeMs, std::optional<int64_t> Difference, bool PersonalRecord, bool Structured)
{
	if(TimeMs <= 0 || !UpdateContext())
		return;
	SSnapshot Snapshot;
	Snapshot.m_Community = m_Community;
	Snapshot.m_Map = m_Map;
	Snapshot.m_Player = m_Player;
	Snapshot.m_ClientId = m_ClientId;
	Snapshot.m_MaxStars = m_Community == ECommunity::AXIOM_GORES ? 3 : 5;
	Snapshot.m_TimeMs = TimeMs;
	ResolveComparison(Snapshot, m_PreviousBest, m_PreviousHistoryKnown, Difference, PersonalRecord);
	if(!m_State.Begin(std::move(Snapshot), Now(), m_RunStartTick))
	{
		if(Structured)
		{
			auto *pSnapshot = m_State.MutableSnapshot();
			if(pSnapshot)
			{
				pSnapshot->m_TimeMs = TimeMs;
				ResolveComparison(*pSnapshot, m_Before.m_BestTimeMs, m_Before.m_HistoryKnown, Difference, PersonalRecord);
				ResolveLocalRank(*pSnapshot, m_vFinishOnlineTimes, m_Before.m_BestTimeMs, m_FinishOnlineTimesKnown);
				++pSnapshot->m_Revision;
			}
		}
		return;
	}
	m_pManagedTeeRenderInfo = GameClient()->CreateManagedTeeRenderInfo(GameClient()->m_aClients[m_ClientId]);
	m_Before = m_Data;
	// 精确的服务端历史成绩补足网页基线；积分历史仍按对应社区身份判断。
	m_Before.m_BestTimeMs = m_PreviousBest;
	m_Before.m_HistoryKnown = m_PreviousHistoryKnown;
	m_FinishGameTick = Client()->GameTick(g_Config.m_ClDummy);
	m_FinishTimestamp = time_timestamp();
	auto &Summary = *m_State.MutableSnapshot();
	char aDate[64];
	str_timestamp_ex(m_FinishTimestamp, aDate, sizeof(aDate), "%Y-%m-%d %H:%M");
	Summary.m_FinishedAt = aDate;
	m_vFinishOnlineTimes.clear();
	m_FinishOnlineTimesKnown = GameClient()->m_ReceivedDDNetPlayerFinishTimes;
	for(const auto &Player : GameClient()->m_aClients)
	{
		if(!Player.m_Active)
			continue;
		SOnlineTime Entry;
		Entry.m_Identity = Player.m_aName;
		Entry.m_Known = GameClient()->m_ReceivedDDNetPlayerFinishTimes && Player.m_FinishTimeSeconds != FinishTime::UNSET;
		if(Player.m_FinishTimeSeconds >= 0)
			Entry.m_TimeMs = static_cast<int64_t>(Player.m_FinishTimeSeconds) * 1000 + std::max(0, Player.m_FinishTimeMillis);
		m_vFinishOnlineTimes.push_back(std::move(Entry));
	}
	ResolveLocalRank(Summary, m_vFinishOnlineTimes, m_PreviousBest, m_FinishOnlineTimesKnown);
	PrepareRequests(true);
	RefreshSnapshot();
}

bool CQmFinishSummary::SuppressesFinishMessage(int ClientId, int64_t TimeMs) const
{
	if(!g_Config.m_QmFinishSummary || g_Config.m_QmHudIslandUseOriginalStyle || !g_Config.m_ClShowhud ||
		Client()->State() != IClient::STATE_ONLINE || GameClient()->m_RankGhost.IsViewModeActive())
		return false;
	if(m_Connection != g_Config.m_ClDummy || m_ClientId != GameClient()->m_aLocalIds[g_Config.m_ClDummy])
		return false;
	// 收起时仍由保留快照匹配，消息沿用自身的十秒过期时间，关闭功能则立即恢复。
	return m_State.MatchesFinish(ClientId, TimeMs);
}

void CQmFinishSummary::OnMessage(int MsgType, void *pRawMsg)
{
	if(!g_Config.m_QmFinishSummary || Client()->State() != IClient::STATE_ONLINE)
		return;
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return;
	if(MsgType == NETMSGTYPE_SV_RACEFINISH)
	{
		const auto *pMsg = static_cast<const CNetMsg_Sv_RaceFinish *>(pRawMsg);
		if(pMsg->m_ClientId == LocalId)
			Finish(pMsg->m_Time, pMsg->m_Diff, pMsg->m_RecordPersonal != 0, true);
	}
	else if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		const auto *pMsg = static_cast<const CNetMsg_Sv_Chat *>(pRawMsg);
		if(pMsg->m_ClientId != -1 || !pMsg->m_pMessage)
			return;
		char aName[MAX_NAME_LENGTH];
		const int TimeMs = CRaceHelper::TimeFromFinishMessage(pMsg->m_pMessage, aName, sizeof(aName));
		if(TimeMs > 0 && str_comp(aName, GameClient()->m_aClients[LocalId].m_aName) == 0)
			Finish(TimeMs, std::nullopt, false, false);
	}
}
