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
{
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

void CPlayerPoints::CancelRequests(bool NewServer)
{
	for(auto &[Name, Slot] : m_ActiveRequests)
	{
		if(Slot.m_pRequest)
			Slot.m_pRequest->Abort();
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
	const std::string Name(pPlayerName);
	if(!m_Cache.ShouldQuery(Name, time_get(), time_freq()) ||
		m_ActiveRequests.size() >= MAX_CONCURRENT_REQUESTS || m_ActiveRequests.contains(Name))
		return;
	StartRequest(pPlayerName);
}

SPlayerPointsResult CPlayerPoints::GetPoints(const char *pPlayerName)
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return {EPointsStatus::NOT_REQUESTED, 0};
	return m_Cache.Get(pPlayerName);
}

void CPlayerPoints::StartRequest(const char *pPlayerName)
{
	// 玩家名先做 URL 编码。
	char aEncodedName[256];
	EscapeUrl(aEncodedName, sizeof(aEncodedName), pPlayerName);

	// 拼接查询 URL。
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEncodedName);

	// 创建并配置 HTTP 请求。
	std::shared_ptr<IHttpRequest> pRequest = HttpGet(aUrl);
	pRequest->MaxResponseSize(1024 * 1000);
	pRequest->Timeout(CTimeout{10000, 30000, 100, 10});
	pRequest->LogProgress(HTTPLOG::FAILURE);

	// 保留已有 READY 数据可见；只有在没有任何缓存时才切换到 FETCHING，
	// 避免重新获取过程中记分板短暂显示 "..."。
	std::string Name(pPlayerName);
	const CQmPlayerPointsCache::SRequestToken Token = m_Cache.BeginRequest(Name);

	// 记录入服代际和请求序号，迟到结果不能覆盖后续入服的新缓存。
	m_ActiveRequests[Name] = {pRequest, Token};
	Http()->Run(pRequest);
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
				m_Cache.CompleteFailure(Name, Token, time_get());
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
				Engine()->AddJob(pParseJob);
				++Iter;
				continue;
			}

			if(ParseIter->second->State() != IJob::STATE_DONE)
			{
				++Iter;
				continue;
			}

			auto pParseJob = std::static_pointer_cast<CPlayerPointsParseJob>(ParseIter->second);
			const SPlayerPointsParseResult Result = pParseJob->TakeResult();
			m_ParseJobs.erase(ParseIter);
			if(!Result.m_JsonParsed)
			{
				m_Cache.CompleteFailure(Name, Token, time_get());
				dbg_msg("player_points", "'%s' -> JSON parse failed", Name.c_str());
			}
			else if(!Result.m_PointsFound)
			{
				// 常见情况：玩家不存在时 DDNet 会返回 {}。
				m_Cache.CompleteFailure(Name, Token, time_get());
				dbg_msg("player_points", "'%s' -> points missing (maybe player not found)", Name.c_str());
			}
			else
			{
				if(m_Cache.CompleteSuccess(Name, Token, Result.m_Points, time_get()))
					StoreToDb(Name.c_str(), Result.m_Points);
				// 成功路径默认不打日志，避免刷屏。
			}
		}
		else
		{
			m_Cache.CompleteFailure(Name, Token, time_get());
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

		Iter = m_ActiveRequests.erase(Iter);
	}
}
