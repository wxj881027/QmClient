// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* Player Points Query System */
#ifndef GAME_CLIENT_COMPONENTS_PLAYER_POINTS_H
#define GAME_CLIENT_COMPONENTS_PLAYER_POINTS_H

#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/sqlite.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/axiom_scores_data.h>
#include <game/client/components/qmclient/player_points_request.h>
#include <game/client/components/qmclient/player_points_state.h>

#include <limits>
#include <map>
#include <memory>
#include <string>

namespace qm_update
{
	class CSystemProxyJob;
}

struct SPlayerPointsParseResult
{
	bool m_JsonParsed = false;
	bool m_PointsFound = false;
	int m_Points = 0;
	int64_t m_PlaytimeHours = -1;
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

inline int64_t ExtractPlayerDdStatsHours(const char *pData, size_t Size, const char *pPlayerName)
{
	if(!pData || Size == 0)
		return -1;
	std::vector<SQmDdStatsGameType> vGameTypes;
	if(QmParseDdStatsPlayerResponse(pData, Size, pPlayerName, vGameTypes) != EQmAxiomParseResult::SUCCESS)
		return -1;
	int64_t Seconds = 0;
	for(const auto &GameType : vGameTypes)
	{
		if(GameType.m_PlayTimeSeconds > std::numeric_limits<int64_t>::max() - Seconds)
			return -1;
		Seconds += GameType.m_PlayTimeSeconds;
	}
	return Seconds / 3600;
}

class CPlayerPoints : public CComponent
{
private:
	CQmPlayerPointsCache m_Cache;
	CQmPlayerPointsCache m_PlaytimeCache;

	struct SRequestSlot
	{
		CQmPlayerPointsRequest m_Request;
		CQmPlayerPointsCache::SRequestToken m_Token;
		std::shared_ptr<qm_update::CSystemProxyJob> m_pProxyJob;
		std::shared_ptr<IJob> m_pParseJob;
	};
	using TRequests = std::map<std::string, SRequestSlot>;
	TRequests m_ActiveRequests;
	TRequests m_PlaytimeRequests;

	// Constants
	static constexpr int MAX_CONCURRENT_REQUESTS = 2;

	CSqlite m_pDb;
	CSqliteStmt m_pLoadStmt;
	CSqliteStmt m_pStoreStmt;

	// Helper functions
	void EnsureRequest(const char *pPlayerName, bool Playtime);
	void StartRequest(const char *pPlayerName, bool Playtime);
	void ProcessCompletedRequests(TRequests &Requests, CQmPlayerPointsCache &Cache, bool Playtime);
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
	void EnsurePlaytimeQueried(const char *pPlayerName);
	SPlayerPointsResult GetPoints(const char *pPlayerName);
	SPlayerPlaytimeResult GetPlaytime(const char *pPlayerName) const;
};

#endif
