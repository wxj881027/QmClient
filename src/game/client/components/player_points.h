// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* Player Points Query System */
#ifndef GAME_CLIENT_COMPONENTS_PLAYER_POINTS_H
#define GAME_CLIENT_COMPONENTS_PLAYER_POINTS_H

#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>

#include <game/client/component.h>

#include <map>
#include <memory>
#include <string>

enum class EPointsStatus
{
	NOT_REQUESTED,
	FETCHING,
	READY,
	FAILED
};

struct SPlayerPointsEntry
{
	int m_Points = 0;
	EPointsStatus m_Status = EPointsStatus::NOT_REQUESTED;
	int64_t m_LastSuccessTime = 0; // timestamp
	int64_t m_LastFailTime = 0; // timestamp for failed requests
};

struct SPlayerPointsResult
{
	EPointsStatus m_Status;
	int m_Points;
};

struct SPlayerPointsParseResult
{
	bool m_JsonParsed = false;
	bool m_PointsFound = false;
	int m_Points = 0;
};

inline SPlayerPointsParseResult ExtractPlayerPointsJson(const json_value *pRoot)
{
	SPlayerPointsParseResult Result;
	if(!pRoot)
		return Result;
	Result.m_JsonParsed = true;
	const json_value *pPointsObj = json_object_get(pRoot, "points");
	const json_value *pPointsVal = pPointsObj ? json_object_get(pPointsObj, "points") : nullptr;
	if(pPointsVal)
	{
		// 保持原有接口对整数节点的接受语义，不新增类型过滤。
		Result.m_Points = json_int_get(pPointsVal);
		Result.m_PointsFound = true;
	}
	return Result;
}
class CPlayerPoints : public CComponent
{
private:
	// Cache: player name -> points data
	std::map<std::string, SPlayerPointsEntry> m_Cache;

	// Active HTTP requests: player name -> request
	std::map<std::string, std::shared_ptr<IHttpRequest>> m_ActiveRequests;
	// 已完成的 HTTP 响应每个玩家最多交给一个后台任务解析。
	std::map<std::string, std::shared_ptr<IJob>> m_ParseJobs;

	// Constants
	static constexpr int64_t CACHE_TTL_MS = 2 * 60 * 60 * 1000; // 2 hours
	static constexpr int64_t FAIL_RETRY_DELAY_MS = 30 * 1000; // 30 seconds
	static constexpr int MAX_CONCURRENT_REQUESTS = 2;

	// Helper functions
	void StartRequest(const char *pPlayerName);
	void ProcessCompletedRequests();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnShutdown() override;
	void OnRender() override;

	// Public interface
	void EnsureQueried(const char *pPlayerName);
	SPlayerPointsResult GetPoints(const char *pPlayerName);
};

#endif
