// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "player_points.h"

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
		SResult m_Result;

	protected:
		void Run() override
		{
			if(!m_pRequest || m_pRequest->State() != EHttpState::DONE || m_pRequest->StatusCode() != 200)
				return;

			// ResultJson 每次调用新建解析树，所有权在本函数。
			json_value *pRoot = m_pRequest->ResultJson();
			if(!pRoot)
				return;

			m_Result = ExtractPlayerPointsJson(pRoot);
			json_value_free(pRoot);
		}

	public:
		explicit CPlayerPointsParseJob(std::shared_ptr<IHttpRequest> pRequest) :
			m_pRequest(std::move(pRequest))
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
	ProcessCompletedRequests();
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
	for(auto &[Name, Slot] : m_ActiveRequests)
	{
		try
		{
			if(Slot.m_pRequest && !Slot.m_pRequest->Done())
				Slot.m_pRequest->Abort();
		}
		catch(...)
		{
			// 单个外部取消失败不能阻止其余槽位失效。
		}
	}
	m_ActiveRequests.clear();
	// 后台任务只拥有响应体；移除槽位后，旧解析结果不会再发布到缓存。
	m_ParseJobs.clear();
	if(NewServer)
		m_Cache.BeginServerSession();
	else
		m_Cache.CancelPendingRequests();
}

void CPlayerPoints::OnShutdown()
{
	CancelRequests();
	m_Cache.Clear();
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
	if(!pPlayerName || pPlayerName[0] == '\0')
		return;
	// 绘制和排序只借用名字查表，命中缓存不再临时分配字符串。
	const std::string_view Name(pPlayerName);
	if(m_ActiveRequests.size() >= MAX_CONCURRENT_REQUESTS || m_ActiveRequests.contains(Name) ||
		!m_Cache.ShouldQuery(Name, Now(), time_freq()))
		return;
	StartRequest(pPlayerName);
}

SPlayerPointsResult CPlayerPoints::GetPoints(const char *pPlayerName)
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return {EPointsStatus::NOT_REQUESTED, 0};
	return m_Cache.Get(pPlayerName);
}

std::shared_ptr<IHttpRequest> CPlayerPoints::CreateRequest(const char *pPlayerName)
{
	char aEncodedName[256];
	EscapeUrl(aEncodedName, sizeof(aEncodedName), pPlayerName);
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEncodedName);
	std::shared_ptr<IHttpRequest> pRequest = HttpGet(aUrl);
	pRequest->MaxResponseSize(1024 * 1000);
	pRequest->Timeout(CTimeout{10000, 30000, 100, 10});
	pRequest->LogProgress(HTTPLOG::FAILURE);
	return pRequest;
}

void CPlayerPoints::StartRequest(const char *pPlayerName)
{
	auto Slot = m_ActiveRequests.end();
	try
	{
		// 先准备拥有名字的槽位，再发布FETCHING令牌和交给外部启动。
		const auto [It, Inserted] = m_ActiveRequests.try_emplace(pPlayerName);
		if(!Inserted)
			return;
		Slot = It;
		Slot->second.m_Token = m_Cache.BeginRequest(Slot->first);
		Slot->second.m_pRequest = CreateRequest(Slot->first.c_str());
		if(!Slot->second.m_pRequest)
		{
			m_Cache.CompleteFailure(Slot->first, Slot->second.m_Token, Now());
			m_ActiveRequests.erase(Slot);
			return;
		}
		SubmitRequest(Slot->second.m_pRequest);
	}
	catch(...)
	{
		if(Slot != m_ActiveRequests.end())
		{
			try
			{
				if(Slot->second.m_pRequest && !Slot->second.m_pRequest->Done())
					Slot->second.m_pRequest->Abort();
			}
			catch(...)
			{
			}
			m_Cache.CompleteFailure(Slot->first, Slot->second.m_Token, Now());
			m_ActiveRequests.erase(Slot);
		}
	}
}

void CPlayerPoints::ProcessCompletedRequests()
{
	auto Iter = m_ActiveRequests.begin();
	while(Iter != m_ActiveRequests.end())
	{
		const std::string &Name = Iter->first;
		std::shared_ptr<IHttpRequest> pRequest = Iter->second.m_pRequest;
		const CQmPlayerPointsCache::SRequestToken Token = Iter->second.m_Token;

		if(!pRequest->Done())
		{
			++Iter;
			continue;
		}

		try
		{
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
					m_Cache.CompleteFailure(Name, Token, Now());
					m_ParseJobs.erase(Name);
					Iter = m_ActiveRequests.erase(Iter);
					continue;
				}

				// 解析下放到后台任务；每个玩家同时只跑一个解析任务。
				auto ParseIter = m_ParseJobs.find(Name);
				if(ParseIter == m_ParseJobs.end())
				{
					auto pParseJob = std::make_shared<CPlayerPointsParseJob>(pRequest);
					m_ParseJobs.emplace(Name, pParseJob);
					if(SubmitParseJob(pParseJob))
					{
						++Iter;
						continue;
					}
					m_ParseJobs.erase(Name);
					m_Cache.CompleteFailure(Name, Token, Now());
					Iter = m_ActiveRequests.erase(Iter);
					continue;
				}

				const IJob::EJobState ParseState = ParseIter->second->State();
				if(ParseState == IJob::STATE_QUEUED || ParseState == IJob::STATE_RUNNING)
				{
					++Iter;
					continue;
				}
				if(ParseState != IJob::STATE_DONE)
				{
					m_ParseJobs.erase(ParseIter);
					m_Cache.CompleteFailure(Name, Token, Now());
					Iter = m_ActiveRequests.erase(Iter);
					continue;
				}

				auto pParseJob = std::static_pointer_cast<CPlayerPointsParseJob>(ParseIter->second);
				const SPlayerPointsParseResult Result = pParseJob->TakeResult();
				m_ParseJobs.erase(ParseIter);
				if(!Result.m_JsonParsed)
				{
					m_Cache.CompleteFailure(Name, Token, Now());
					dbg_msg("player_points", "'%s' -> JSON parse failed", Name.c_str());
				}
				else if(!Result.m_PointsFound)
				{
					// 常见情况：玩家不存在时 DDNet 会返回 {}。
					m_Cache.CompleteFailure(Name, Token, Now());
					dbg_msg("player_points", "'%s' -> points missing (maybe player not found)", Name.c_str());
				}
				else
				{
					if(m_Cache.CompleteSuccess(Name, Token, Result.m_Points, Now()))
						StoreToDb(Name.c_str(), Result.m_Points);
					// 成功路径默认不打日志，避免刷屏。
				}
			}
			else
			{
				m_Cache.CompleteFailure(Name, Token, Now());
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
			m_Cache.CompleteFailure(Name, Token, Now());
			m_ParseJobs.erase(Name);
		}

		Iter = m_ActiveRequests.erase(Iter);
	}
}
