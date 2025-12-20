/* Player Points Query System */
#include "player_points.h"

#include <base/log.h>
#include <base/system.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <game/client/gameclient.h>

#include <cstring>

// Custom HTTP request that truncates response after finding points field
class CPlayerPointsRequest : public CHttpRequest
{
private:
	std::string m_PlayerName;
	int m_Points = 0;
	bool m_PointsFound = false;
	size_t m_BytesRead = 0;
	static constexpr size_t MAX_READ_BYTES = 1024*1000; // 最多读取2KB数据以查找点字段
	
	char m_aPartialData[MAX_READ_BYTES + 1] = {0};
	
protected:
	void OnCompletion(EHttpState State) override
	{
		// This runs on curl thread, just mark completion
	}
	
public:
	CPlayerPointsRequest(const char *pUrl, const char *pPlayerName) :
		CHttpRequest(pUrl),
		m_PlayerName(pPlayerName)
	{
		// Limit response size to avoid downloading huge JSON
		MaxResponseSize(MAX_READ_BYTES);
	}
	
	const char *GetPlayerName() const { return m_PlayerName.c_str(); }
	bool PointsFound() const { return m_PointsFound; }
	int GetPoints() const { return m_Points; }
	
	const char *GetPartialData() const { return m_aPartialData; }
	size_t GetBytesRead() const { return m_BytesRead; }
	
	void SetPointsFound(int Points)
	{
		m_PointsFound = true;
		m_Points = Points;
	}
};

void CPlayerPoints::OnRender()
{
	ProcessCompletedRequests();
}

void CPlayerPoints::EnsureQueried(const char *pPlayerName)
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return;
	
	std::string Name(pPlayerName);
	
	// Check if already in cache
	auto it = m_Cache.find(Name);
	if(it != m_Cache.end())
	{
		const SPlayerPointsEntry &Entry = it->second;
		
		// If fetching, do nothing
		if(Entry.m_Status == EPointsStatus::FETCHING)
			return;
		
		// If ready and not expired, do nothing
		if(Entry.m_Status == EPointsStatus::READY)
		{
			int64_t Now = time_get();
			int64_t ElapsedMs = (Now - Entry.m_LastSuccessTime) * 1000 / time_freq();
			if(ElapsedMs < CACHE_TTL_MS)
				return;
		}
		
		// If failed, check retry delay
		if(Entry.m_Status == EPointsStatus::FAILED)
		{
			int64_t Now = time_get();
			int64_t ElapsedMs = (Now - Entry.m_LastFailTime) * 1000 / time_freq();
			if(ElapsedMs < FAIL_RETRY_DELAY_MS)
				return;
		}
	}
	
	// Check concurrent request limit
	if(m_ActiveRequests.size() >= MAX_CONCURRENT_REQUESTS)
		return;
	
	// Check if already requesting
	if(m_ActiveRequests.find(Name) != m_ActiveRequests.end())
		return;
	
	// Start new request
	StartRequest(pPlayerName);
}

SPlayerPointsResult CPlayerPoints::GetPoints(const char *pPlayerName)
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return {EPointsStatus::NOT_REQUESTED, 0};
	
	std::string Name(pPlayerName);
	auto it = m_Cache.find(Name);
	if(it == m_Cache.end())
		return {EPointsStatus::NOT_REQUESTED, 0};
	
	const SPlayerPointsEntry &Entry = it->second;
	return {Entry.m_Status, Entry.m_Points};
}

void CPlayerPoints::StartRequest(const char *pPlayerName)
{
	// URL encode player name
	char aEncodedName[256];
	EscapeUrl(aEncodedName, sizeof(aEncodedName), pPlayerName);
	
	// Construct URL
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEncodedName);
	
	dbg_msg("player_points", "StartRequest: name='%s', url='%s'", pPlayerName, aUrl);
	
	// Create request
	auto pRequest = std::make_shared<CPlayerPointsRequest>(aUrl, pPlayerName);
	pRequest->Timeout(CTimeout{10000, 30000, 100, 10});
	pRequest->LogProgress(HTTPLOG::FAILURE);
	
	// Mark as fetching in cache
	std::string Name(pPlayerName);
	m_Cache[Name].m_Status = EPointsStatus::FETCHING;
	
	// Store request and run
	m_ActiveRequests[Name] = pRequest;
	Http()->Run(pRequest);
}

void CPlayerPoints::ProcessCompletedRequests()
{
	auto it = m_ActiveRequests.begin();
	while(it != m_ActiveRequests.end())
	{
		const std::string &Name = it->first;
		std::shared_ptr<CHttpRequest> pRequest = it->second;
		
		if(!pRequest->Done())
		{
			++it;
			continue;
		}
		
		SPlayerPointsEntry &Entry = m_Cache[Name];
		EHttpState State = pRequest->State();
		
		if(State == EHttpState::DONE)
		{
			unsigned char *pData = nullptr;
			size_t DataSize = 0;
			pRequest->Result(&pData, &DataSize);
			
			dbg_msg("player_points", "Response for '%s': %zu bytes, status=%d", Name.c_str(), DataSize, pRequest->StatusCode());
			if(State == EHttpState::DONE)
			{
				const int Code = pRequest->StatusCode();
				dbg_msg("player_points", "Response for '%s': http=%d", Name.c_str(), Code);

				if(Code != 200)
				{
					Entry.m_Status = EPointsStatus::FAILED;
					Entry.m_LastFailTime = time_get();
				}
				else
				{
					json_value *pRoot = pRequest->ResultJson();
					if(!pRoot)
					{
						Entry.m_Status = EPointsStatus::FAILED;
						Entry.m_LastFailTime = time_get();
						dbg_msg("player_points", "'%s' -> JSON parse failed", Name.c_str());
					}
					else
					{
						// root["points"]["points"]
						const json_value *pPointsObj = json_object_get(pRoot, "points");
						const json_value *pPointsVal = pPointsObj ? json_object_get(pPointsObj, "points") : nullptr;

						if(!pPointsVal)
						{
							// 很常见：玩家不存在时 ddnet 返回 {}
							Entry.m_Status = EPointsStatus::FAILED;
							Entry.m_LastFailTime = time_get();
							dbg_msg("player_points", "'%s' -> points missing (maybe player not found)", Name.c_str());
						}
						else
						{
							// json_int_get 需要是 integer 节点，否则可能内部 assert/崩
							// 如果你们 json-parser 对非 int 会返回 0，这里也至少有日志可看
							int Points = json_int_get(pPointsVal);

							Entry.m_Points = Points;
							Entry.m_Status = EPointsStatus::READY;
							Entry.m_LastSuccessTime = time_get();
							dbg_msg("player_points", "'%s' -> %d points", Name.c_str(), Points);
						}
					}
				}
			}
		}
		else
		{
			Entry.m_Status = EPointsStatus::FAILED;
			Entry.m_LastFailTime = time_get();
			const char *pStateStr = "UNKNOWN";
			switch(State)
			{
			case EHttpState::ERROR: pStateStr = "ERROR"; break;
			case EHttpState::ABORTED: pStateStr = "ABORTED"; break;
			case EHttpState::QUEUED: pStateStr = "QUEUED"; break;
			case EHttpState::RUNNING: pStateStr = "RUNNING"; break;
			case EHttpState::DONE: pStateStr = "DONE"; break;
			}
			dbg_msg("player_points", "'%s' -> HTTP failed, state=%s", Name.c_str(), pStateStr);
		}
		
		it = m_ActiveRequests.erase(it);
	}
}

bool CPlayerPoints::ParsePointsFromPartialJson(const char *pData, size_t DataSize, int &OutPoints)
{
	if(!pData || DataSize == 0)
		return false;
	
	// 检查是否为空对象 {} (玩家不存在)
	if(DataSize <= 2)
		return false;
	
	// First, try to find the "points" object
	const char *pPointsObj = str_find(pData, "\"points\"");
	if(!pPointsObj)
		return false;
	
	// Move past "points":
	pPointsObj += 8;
	
	// Skip whitespace and colon
	while(*pPointsObj && (*pPointsObj == ' ' || *pPointsObj == '\t' || *pPointsObj == '\n' || *pPointsObj == '\r' || *pPointsObj == ':'))
		pPointsObj++;
	
	// Expect opening brace
	if(*pPointsObj != '{')
		return false;
	pPointsObj++;
	
	// Now find the nested "points" field
	const char *pPointsField = str_find(pPointsObj, "\"points\"");
	if(!pPointsField)
		return false;
	
	// Move past "points":
	pPointsField += 8;
	
	// Skip whitespace and colon
	while(*pPointsField && (*pPointsField == ' ' || *pPointsField == '\t' || *pPointsField == '\n' || *pPointsField == '\r' || *pPointsField == ':'))
		pPointsField++;
	
	// Parse integer value
	char *pEnd = nullptr;
	long Value = strtol(pPointsField, &pEnd, 10);
	
	if(pEnd == pPointsField || Value < 0)
		return false;
	
	OutPoints = (int)Value;
	return true;
}
