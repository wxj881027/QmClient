// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* Player Points Query System */
#ifndef GAME_CLIENT_COMPONENTS_PLAYER_POINTS_H
#define GAME_CLIENT_COMPONENTS_PLAYER_POINTS_H

#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/sqlite.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/player_points_state.h>

#include <map>
#include <memory>
#include <string>

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
	CQmPlayerPointsCache m_Cache;

	struct SRequestSlot
	{
		std::shared_ptr<IHttpRequest> m_pRequest;
		CQmPlayerPointsCache::SRequestToken m_Token;
	};
	std::map<std::string, SRequestSlot> m_ActiveRequests;
	// 已完成的 HTTP 响应每个玩家最多交给一个后台任务解析。
	std::map<std::string, std::shared_ptr<IJob>> m_ParseJobs;

	// Constants
	static constexpr int MAX_CONCURRENT_REQUESTS = 2;

	CSqlite m_pDb;
	CSqliteStmt m_pLoadStmt;
	CSqliteStmt m_pStoreStmt;

	// Helper functions
	void StartRequest(const char *pPlayerName);
	void ProcessCompletedRequests();
	void StoreToDb(const char *pPlayerName, int Points);
	void CancelRequests(bool NewServer = false);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;

	// Public interface
	void EnsureQueried(const char *pPlayerName);
	SPlayerPointsResult GetPoints(const char *pPlayerName);
};

#endif
