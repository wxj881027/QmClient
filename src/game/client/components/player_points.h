/* Player Points Query System */
#ifndef GAME_CLIENT_COMPONENTS_PLAYER_POINTS_H
#define GAME_CLIENT_COMPONENTS_PLAYER_POINTS_H

#include <engine/shared/http.h>
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

class CPlayerPoints : public CComponent
{
private:
	// Cache: player name -> points data
	std::map<std::string, SPlayerPointsEntry> m_Cache;
	
	// Active HTTP requests: player name -> request
	std::map<std::string, std::shared_ptr<CHttpRequest>> m_ActiveRequests;
	
	// Constants
	static constexpr int64_t CACHE_TTL_MS = 10 * 60 * 1000; // 10 minutes
	static constexpr int64_t FAIL_RETRY_DELAY_MS = 30 * 1000; // 30 seconds
	static constexpr int MAX_CONCURRENT_REQUESTS = 2;
	
	// Helper functions
	void StartRequest(const char *pPlayerName);
	void ProcessCompletedRequests();
	bool ParsePointsFromPartialJson(const char *pData, size_t DataSize, int &OutPoints);
	
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	
	// Public interface
	void EnsureQueried(const char *pPlayerName);
	SPlayerPointsResult GetPoints(const char *pPlayerName);
};

#endif
