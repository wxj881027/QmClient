// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "player_points.h"

#include <base/log.h>
#include <base/system.h>

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
		SPlayerPointsEntry &Entry = m_Cache[pName];
		Entry.m_Points = Points;
		Entry.m_Status = EPointsStatus::READY;
		Entry.m_LastSuccessTime = 0; // TTL 已过期，触发后台刷新
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

void CPlayerPoints::OnShutdown()
{
	for(auto &Pair : m_ActiveRequests)
	{
		if(Pair.second)
			Pair.second->Abort();
	}
	m_ActiveRequests.clear();
	m_ParseJobs.clear();
	m_Cache.clear();
}

void CPlayerPoints::EnsureQueried(const char *pPlayerName)
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return;

	std::string Name(pPlayerName);

	// 先看缓存状态，避免重复请求。
	auto Iter = m_Cache.find(Name);
	if(Iter != m_Cache.end())
	{
		const SPlayerPointsEntry &Entry = Iter->second;

		// 正在请求中，直接返回。
		if(Entry.m_Status == EPointsStatus::FETCHING)
			return;

		// 命中有效缓存，直接返回。
		if(Entry.m_Status == EPointsStatus::READY)
		{
			int64_t Now = time_get();
			int64_t ElapsedMs = (Now - Entry.m_LastSuccessTime) * 1000 / time_freq();
			if(ElapsedMs < CACHE_TTL_MS)
				return;
			// TTL 过期，但上次刷新失败时先等退避期再重试，避免每帧发请求。
			if(Entry.m_LastFailTime > 0)
			{
				int64_t FailElapsedMs = (Now - Entry.m_LastFailTime) * 1000 / time_freq();
				if(FailElapsedMs < FAIL_RETRY_DELAY_MS)
					return;
			}
		}

		// 上次失败后在退避时间内，不重试。
		if(Entry.m_Status == EPointsStatus::FAILED)
		{
			int64_t Now = time_get();
			int64_t ElapsedMs = (Now - Entry.m_LastFailTime) * 1000 / time_freq();
			if(ElapsedMs < FAIL_RETRY_DELAY_MS)
				return;
		}
	}

	// 并发请求上限保护。
	if(m_ActiveRequests.size() >= MAX_CONCURRENT_REQUESTS)
		return;

	// 同名请求已在队列中。
	if(m_ActiveRequests.contains(Name))
		return;

	// 发起新请求。
	StartRequest(pPlayerName);
}

SPlayerPointsResult CPlayerPoints::GetPoints(const char *pPlayerName)
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return {EPointsStatus::NOT_REQUESTED, 0};

	std::string Name(pPlayerName);
	auto Iter = m_Cache.find(Name);
	if(Iter == m_Cache.end())
		return {EPointsStatus::NOT_REQUESTED, 0};

	const SPlayerPointsEntry &Entry = Iter->second;
	return {Entry.m_Status, Entry.m_Points};
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
	SPlayerPointsEntry &CacheEntry = m_Cache[Name];
	if(CacheEntry.m_Status != EPointsStatus::READY)
		CacheEntry.m_Status = EPointsStatus::FETCHING;

	// 记录活跃请求并提交执行。
	m_ActiveRequests[Name] = pRequest;
	Http()->Run(pRequest);
}

void CPlayerPoints::ProcessCompletedRequests()
{
	auto Iter = m_ActiveRequests.begin();
	while(Iter != m_ActiveRequests.end())
	{
		const std::string &Name = Iter->first;
		std::shared_ptr<IHttpRequest> pRequest = Iter->second;

		if(!pRequest->Done())
		{
			++Iter;
			continue;
		}

		SPlayerPointsEntry &Entry = m_Cache[Name];
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
				if(Entry.m_Status != EPointsStatus::READY)
					Entry.m_Status = EPointsStatus::FAILED;
				Entry.m_LastFailTime = time_get();
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
				if(Entry.m_Status != EPointsStatus::READY)
					Entry.m_Status = EPointsStatus::FAILED;
				Entry.m_LastFailTime = time_get();
				dbg_msg("player_points", "'%s' -> JSON parse failed", Name.c_str());
			}
			else if(!Result.m_PointsFound)
			{
				// 常见情况：玩家不存在时 DDNet 会返回 {}。
				if(Entry.m_Status != EPointsStatus::READY)
					Entry.m_Status = EPointsStatus::FAILED;
				Entry.m_LastFailTime = time_get();
				dbg_msg("player_points", "'%s' -> points missing (maybe player not found)", Name.c_str());
			}
			else
			{
				Entry.m_Points = Result.m_Points;
				Entry.m_Status = EPointsStatus::READY;
				Entry.m_LastSuccessTime = time_get();
				StoreToDb(Name.c_str(), Result.m_Points);
				// 成功路径默认不打日志，避免刷屏。
			}
		}
		else
		{
			if(Entry.m_Status != EPointsStatus::READY)
				Entry.m_Status = EPointsStatus::FAILED;
			Entry.m_LastFailTime = time_get();
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
