// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "player_points.h"

#include "qmclient/update_proxy.h"

#include <base/detect.h>
#include <base/log.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/json.h>
#include <engine/sqlite.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <sqlite3.h>

#include <cstring>

namespace
{
	// JSON 解析放到后台任务：响应体较大时主线程解析会打帧。
	class CPlayerPointsParseJob final : public IJob
	{
	public:
		using SResult = SPlayerPointsParseResult;

	private:
		std::shared_ptr<IHttpRequest> m_pRequest;
		std::string m_PlayerName;
		bool m_Playtime;
		SResult m_Result;

	protected:
		void Run() override
		{
			if(!m_pRequest || m_pRequest->State() != EHttpState::DONE || m_pRequest->StatusCode() != 200)
				return;
			if(m_Playtime)
			{
				unsigned char *pData = nullptr;
				size_t Size = 0;
				m_pRequest->Result(&pData, &Size);
				m_Result.m_PlaytimeHours = ExtractPlayerDdStatsHours(reinterpret_cast<const char *>(pData), Size, m_PlayerName.c_str());
				m_Result.m_JsonParsed = m_Result.m_PlaytimeHours >= 0;
				return;
			}

			// ResultJson 每次调用新建解析树，所有权在本函数。
			json_value *pRoot = m_pRequest->ResultJson();
			if(!pRoot)
				return;

			m_Result = ExtractPlayerPointsJson(pRoot);
			json_value_free(pRoot);
		}

	public:
		CPlayerPointsParseJob(std::shared_ptr<IHttpRequest> pRequest, const std::string &PlayerName, bool Playtime) :
			m_pRequest(std::move(pRequest)),
			m_PlayerName(PlayerName),
			m_Playtime(Playtime)
		{
		}

		SResult TakeResult()
		{
			return std::move(m_Result);
		}
	};
}
void CPlayerPoints::OnInit()
try
{
	m_pStoreStmt.reset();
	m_pLoadStmt.reset();
	m_pDb.reset();
	Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
	m_pDb = SqliteOpen(Console(), Storage(), "qmclient/score_cache.sqlite3");
	if(!m_pDb)
	{
		log_warn("player_points", "failed to open qmclient/score_cache.sqlite3");
		return;
	}
	sqlite3 *pSqlite = m_pDb.get();
	static const char TABLE[] =
		"CREATE TABLE IF NOT EXISTS player_points "
		"(name TEXT PRIMARY KEY NOT NULL, points INTEGER NOT NULL)";
	if(SqliteHandleError(Console(), sqlite3_exec(pSqlite, TABLE, nullptr, nullptr, nullptr), pSqlite, TABLE) != SQLITE_OK)
	{
		m_pDb = nullptr;
		return;
	}
	m_pLoadStmt = SqlitePrepare(Console(), pSqlite, "SELECT name, points FROM player_points");
	m_pStoreStmt = SqlitePrepare(Console(), pSqlite, "INSERT OR REPLACE INTO player_points (name, points) VALUES (?, ?)");
	if(!m_pLoadStmt || !m_pStoreStmt)
	{
		m_pStoreStmt.reset();
		m_pLoadStmt.reset();
		m_pDb = nullptr;
		return;
	}

	// 加载全部缓存行：标记为 READY 但 LastSuccessTime=0，
	// 使 EnsureQueried 在下次调用时立即触发后台刷新（stale-while-revalidate）。
	bool Error = false;
	Error = Error || SqliteHandleError(Console(), sqlite3_reset(m_pLoadStmt.get()), pSqlite, "reset load") != SQLITE_OK;
	while(!Error)
	{
		const int Step = sqlite3_step(m_pLoadStmt.get());
		if(Step == SQLITE_DONE)
			break;
		if(Step != SQLITE_ROW)
		{
			SqliteHandleError(Console(), Step, pSqlite, "step load");
			Error = true;
			break;
		}
		const char *pName = reinterpret_cast<const char *>(sqlite3_column_text(m_pLoadStmt.get(), 0));
		const int Points = sqlite3_column_int(m_pLoadStmt.get(), 1);
		if(!pName || pName[0] == '\0')
			continue;
		m_Cache.Load(pName, Points);
	}
}
catch(...)
{
	// 可选的磁盘缓存失败不终止客户端；已加载的积分仍可用。
	m_pStoreStmt.reset();
	m_pLoadStmt.reset();
	m_pDb.reset();
}

int64_t CPlayerPoints::Now() const noexcept
{
	return time_get();
}

void CPlayerPoints::SubmitRequest(const std::shared_ptr<IHttpRequest> &pRequest)
{
	Http()->Run(pRequest);
}

bool CPlayerPoints::SubmitParseJob(const std::shared_ptr<IJob> &pJob)
{
	return Engine()->TryAddJob(pJob);
}

void CPlayerPoints::OnRender()
{
	ProcessCompletedRequests(m_ActiveRequests, m_Cache, false);
	ProcessCompletedRequests(m_PlaytimeRequests, m_PlaytimeCache, true);
}

void CPlayerPoints::StoreToDb(const char *pPlayerName, int Points)
{
	if(!m_pDb || !m_pStoreStmt)
		return;
	sqlite3 *pSqlite = m_pDb.get();
	bool Error = false;
	Error = Error || SqliteHandleError(Console(), sqlite3_reset(m_pStoreStmt.get()), pSqlite, "reset store") != SQLITE_OK;
	Error = Error || SqliteHandleError(Console(), sqlite3_bind_text(m_pStoreStmt.get(), 1, pPlayerName, -1, SQLITE_TRANSIENT), pSqlite, "bind name") != SQLITE_OK;
	Error = Error || SqliteHandleError(Console(), sqlite3_bind_int(m_pStoreStmt.get(), 2, Points), pSqlite, "bind points") != SQLITE_OK;
	Error = Error || SqliteHandleError(Console(), sqlite3_step(m_pStoreStmt.get()), pSqlite, "step store") != SQLITE_DONE;
	if(Error)
		log_warn("player_points", "failed to store points for '%s'", pPlayerName);
}

void CPlayerPoints::CancelRequests(bool NewServer) noexcept
{
	for(auto *pRequests : {&m_ActiveRequests, &m_PlaytimeRequests})
	{
		for(auto &[Name, Slot] : *pRequests)
		{
			try
			{
				Slot.m_Request.CancelWithSubmit([this](const auto &pRequest) { SubmitRequest(pRequest); });
			}
			catch(...)
			{
				// 单个外部取消失败不能阻止其余槽位和代际失效。
			}
		}
		// 清掉槽位后，仍在后台解析的旧响应不会再发布。
		pRequests->clear();
	}
	if(NewServer)
	{
		m_Cache.BeginServerSession();
		m_PlaytimeCache.BeginServerSession();
	}
	else
	{
		m_Cache.CancelPendingRequests();
		m_PlaytimeCache.CancelPendingRequests();
	}
}

void CPlayerPoints::OnShutdown()
{
	CancelRequests();
	m_Cache.Clear();
	m_PlaytimeCache.Clear();
}

void CPlayerPoints::OnReset()
{
	CancelRequests();
}

void CPlayerPoints::OnStateChange(int NewState, int OldState)
{
	if(NewState < IClient::STATE_ONLINE)
		CancelRequests();
	else if(NewState == IClient::STATE_ONLINE && OldState < IClient::STATE_ONLINE)
		CancelRequests(true);
}

void CPlayerPoints::EnsureQueried(const char *pPlayerName)
{
	EnsureRequest(pPlayerName, false);
}

void CPlayerPoints::EnsurePlaytimeQueried(const char *pPlayerName)
{
	EnsureRequest(pPlayerName, true);
}

void CPlayerPoints::EnsureRequest(const char *pPlayerName, bool Playtime)
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return;
	const std::string_view Name(pPlayerName);
	const auto &Cache = Playtime ? m_PlaytimeCache : m_Cache;
	const auto &Requests = Playtime ? m_PlaytimeRequests : m_ActiveRequests;
	if(!Cache.ShouldQuery(Name, Now(), time_freq()) ||
		m_ActiveRequests.size() + m_PlaytimeRequests.size() >= MAX_CONCURRENT_REQUESTS || Requests.contains(Name))
		return;
	StartRequest(pPlayerName, Playtime);
}

SPlayerPointsResult CPlayerPoints::GetPoints(const char *pPlayerName)
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return {EPointsStatus::NOT_REQUESTED, 0};
	return m_Cache.Get(pPlayerName);
}

SPlayerPlaytimeResult CPlayerPoints::GetPlaytime(const char *pPlayerName) const
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return {EPointsStatus::NOT_REQUESTED, -1};
	return m_PlaytimeCache.GetPlaytime(pPlayerName);
}

std::shared_ptr<IHttpRequest> CPlayerPoints::CreateRequest(const char *pPlayerName, bool Playtime)
{
	// 玩家名先做 URL 编码。
	char aEncodedName[256];
	EscapeUrl(aEncodedName, sizeof(aEncodedName), pPlayerName);

	// 拼接查询 URL。
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEncodedName);
	if(Playtime)
		str_copy(aUrl, QmBuildDdStatsPlayerUrl(pPlayerName).c_str(), sizeof(aUrl));

	// 创建并配置 HTTP 请求。
	std::shared_ptr<IHttpRequest> pRequest = HttpGet(aUrl);
	pRequest->MaxResponseSize(Playtime ? 8 * 1024 * 1024 : 1024 * 1000);
	if(Playtime)
	{
		// DDStats 现算历史数据，沿用现有采集器的慢响应及 IPv4 策略。
		pRequest->Timeout(CTimeout{5000, 90000, 0, 0});
		pRequest->IpResolve(IPRESOLVE::V4);
	}
	else
		pRequest->Timeout(CTimeout{10000, 30000, 100, 10});
	pRequest->LogProgress(HTTPLOG::FAILURE);

	return pRequest;
}

std::shared_ptr<qm_update::CSystemProxyJob> CPlayerPoints::CreateProxyJob(const std::shared_ptr<IHttpRequest> &pRequest)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(!qm_update::HasEnvironmentProxy())
		return std::make_shared<qm_update::CSystemProxyJob>(pRequest->Url());
#endif
	return nullptr;
}

void CPlayerPoints::StartRequest(const char *pPlayerName, bool Playtime)
{
	auto &Cache = Playtime ? m_PlaytimeCache : m_Cache;
	auto &Requests = Playtime ? m_PlaytimeRequests : m_ActiveRequests;
	auto It = Requests.end();
	try
	{
		const auto [Candidate, Inserted] = Requests.try_emplace(pPlayerName);
		if(!Inserted)
			return;
		It = Candidate;
		auto &Slot = It->second;
		Slot.m_Token = Cache.BeginRequest(It->first);
		auto pRequest = CreateRequest(It->first.c_str(), Playtime);
		if(!pRequest)
		{
			Cache.CompleteFailure(It->first, Slot.m_Token, Now());
			Requests.erase(It);
			return;
		}
		const double CurrentTime = Now() / static_cast<double>(time_freq());
		Slot.m_Request = CQmPlayerPointsRequest(pRequest, CurrentTime);
		Slot.m_pProxyJob = CreateProxyJob(pRequest);
		// 作业池关闭时沿原代理超时路径回退直连，不保留永远无法完成的作业。
		if(Slot.m_pProxyJob && !SubmitParseJob(Slot.m_pProxyJob))
			Slot.m_pProxyJob.reset();
		if(!Slot.m_pProxyJob)
			Slot.m_Request.PollWithSubmit([this](const auto &pPending) { SubmitRequest(pPending); }, CurrentTime, true, nullptr);
	}
	catch(...)
	{
		if(It != Requests.end())
		{
			try
			{
				It->second.m_Request.CancelWithSubmit([this](const auto &pRequest) { SubmitRequest(pRequest); });
			}
			catch(...)
			{
			}
			Cache.CompleteFailure(It->first, It->second.m_Token, Now());
			Requests.erase(It);
		}
	}
}

void CPlayerPoints::ProcessCompletedRequests(TRequests &Requests, CQmPlayerPointsCache &Cache, bool Playtime)
{
	auto Iter = Requests.begin();
	while(Iter != Requests.end())
	{
		const std::string &Name = Iter->first;
		SRequestSlot &Slot = Iter->second;
		std::shared_ptr<IHttpRequest> pRequest = Slot.m_Request.Request();
		const CQmPlayerPointsCache::SRequestToken Token = Slot.m_Token;

		try
		{
			if(!Slot.m_Request.Submitted())
			{
				const bool ProxyReady = !Slot.m_pProxyJob || Slot.m_pProxyJob->State() == IJob::STATE_DONE;
				const char *pProxy = ProxyReady && Slot.m_pProxyJob && Slot.m_pProxyJob->HasDecision() ? Slot.m_pProxyJob->Proxy().c_str() : nullptr;
				if(!Slot.m_Request.PollWithSubmit([this](const auto &pPending) { SubmitRequest(pPending); }, Now() / static_cast<double>(time_freq()), ProxyReady, pProxy))
				{
					++Iter;
					continue;
				}
				// 超时或取消后丢弃作业槽位，迟到的代理结果不能再次发起请求。
				Slot.m_pProxyJob.reset();
			}

			if(!pRequest->Done())
			{
				++Iter;
				continue;
			}

			// DDStats 用 404 表示没有该玩家的记录：时长按 0 小时记入成功结果，
			// 计分板显示 0，不再把"查无此人"当成查询失败显示问号并按失败反复重试。
			if(QmDdStatsPlaytimeMissingRecord(Playtime, pRequest->CompletedStatusCode()))
			{
				Cache.CompleteSuccess(Name, Token, 0, Now(), 0);
				Slot.m_pParseJob.reset();
				Iter = Requests.erase(Iter);
				continue;
			}

			EHttpState State = pRequest->State();

			if(State == EHttpState::DONE)
			{
				const int Code = pRequest->StatusCode();

				if(Code != 200)
				{
					unsigned char *pData = nullptr;
					size_t DataSize = 0;
					pRequest->Result(&pData, &DataSize);
					// 仅失败时记录详细日志。
					dbg_msg("player_points", "Response for '%s': %zu bytes, status=%d (failed)", Name.c_str(), DataSize, Code);
					Cache.CompleteFailure(Name, Token, Now());
					Slot.m_pParseJob.reset();
					Iter = Requests.erase(Iter);
					continue;
				}

				// 解析下放到后台任务；同一来源的每个玩家同时只跑一个解析任务。
				if(!Slot.m_pParseJob)
				{
					auto pParseJob = std::make_shared<CPlayerPointsParseJob>(pRequest, Name, Playtime);
					Slot.m_pParseJob = pParseJob;
					if(!SubmitParseJob(pParseJob))
					{
						Cache.CompleteFailure(Name, Token, Now());
						Iter = Requests.erase(Iter);
						continue;
					}
					++Iter;
					continue;
				}

				const auto ParseState = Slot.m_pParseJob->State();
				if(ParseState == IJob::STATE_QUEUED || ParseState == IJob::STATE_RUNNING)
				{
					++Iter;
					continue;
				}

				if(ParseState != IJob::STATE_DONE)
				{
					Cache.CompleteFailure(Name, Token, Now());
					Iter = Requests.erase(Iter);
					continue;
				}

				auto pParseJob = std::static_pointer_cast<CPlayerPointsParseJob>(Slot.m_pParseJob);
				const SPlayerPointsParseResult Result = pParseJob->TakeResult();
				Slot.m_pParseJob.reset();
				if(!Result.m_JsonParsed)
				{
					Cache.CompleteFailure(Name, Token, Now());
					dbg_msg("player_points", "'%s' -> JSON parse failed", Name.c_str());
				}
				else if(!Playtime && !Result.m_PointsFound)
				{
					// 常见情况：玩家不存在时 DDNet 会返回 {}。
					Cache.CompleteFailure(Name, Token, Now());
					dbg_msg("player_points", "'%s' -> points missing (maybe player not found)", Name.c_str());
				}
				else
				{
					if(Cache.CompleteSuccess(Name, Token, Result.m_Points, Now(), Result.m_PlaytimeHours) && !Playtime)
						StoreToDb(Name.c_str(), Result.m_Points);
					// 成功路径默认不打日志，避免刷屏。
				}
			}
			else
			{
				Cache.CompleteFailure(Name, Token, Now());
				const char *pStateStr = nullptr;
				switch(State)
				{
				case EHttpState::ERROR: pStateStr = "ERROR"; break;
				case EHttpState::ABORTED: pStateStr = "ABORTED"; break;
				case EHttpState::QUEUED: pStateStr = "QUEUED"; break;
				case EHttpState::RUNNING: pStateStr = "RUNNING"; break;
				case EHttpState::DONE: pStateStr = "DONE"; break;
				}
				dbg_msg("player_points", "'%s' -> HTTP failed, state=%s", Name.c_str(), pStateStr ? pStateStr : "UNKNOWN");
			}
		}
		catch(...)
		{
			Cache.CompleteFailure(Name, Token, Now());
		}

		Iter = Requests.erase(Iter);
	}
}
