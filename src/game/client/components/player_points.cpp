#include "player_points.h"

#include <base/log.h>
#include <base/system.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <game/client/gameclient.h>

#include <cstring>

// 玩家请求，携带本次请求的上下文信息。
class CPlayerPointsRequest : public CHttpRequest
{
private:
	std::string m_PlayerName;
	int m_Points = 0;
	bool m_PointsFound = false;
	size_t m_BytesRead = 0;
	static constexpr size_t MAX_READ_BYTES = 1024*1000; // 最大响应体大小（约 1000KB）
	
	char m_aPartialData[MAX_READ_BYTES + 1] = {0};
	
protected:
	void OnCompletion(EHttpState State) override
	{
		// 该回调在 curl 线程执行，这里不做耗时逻辑。
	}
	
public:
	CPlayerPointsRequest(const char *pUrl, const char *pPlayerName) :
		CHttpRequest(pUrl),
		m_PlayerName(pPlayerName)
	{
		// 限制响应大小，避免下载过大的 JSON。
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
	
	// 先看缓存状态，避免重复请求。
	auto it = m_Cache.find(Name);
	if(it != m_Cache.end())
	{
		const SPlayerPointsEntry &Entry = it->second;
		
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
	if(m_ActiveRequests.find(Name) != m_ActiveRequests.end())
		return;
	
	// 发起新请求。
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
	// 玩家名先做 URL 编码。
	char aEncodedName[256];
	EscapeUrl(aEncodedName, sizeof(aEncodedName), pPlayerName);
	
	// 拼接查询 URL。
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEncodedName);
	
	// 创建并配置 HTTP 请求。
	auto pRequest = std::make_shared<CPlayerPointsRequest>(aUrl, pPlayerName);
	pRequest->Timeout(CTimeout{10000, 30000, 100, 10});
	pRequest->LogProgress(HTTPLOG::FAILURE);
	
	// 先把缓存状态标记为请求中。
	std::string Name(pPlayerName);
	m_Cache[Name].m_Status = EPointsStatus::FETCHING;
	
	// 记录活跃请求并提交执行。
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
			
		if(State == EHttpState::DONE)
		{
			const int Code = pRequest->StatusCode();

			if(Code != 200)
			{
				// 仅失败时记录详细日志。
				dbg_msg("player_points", "Response for '%s': %zu bytes, status=%d (failed)", Name.c_str(), DataSize, Code);
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
							// 常见情况：玩家不存在时 DDNet 会返回 {}。
							Entry.m_Status = EPointsStatus::FAILED;
							Entry.m_LastFailTime = time_get();
							dbg_msg("player_points", "'%s' -> points missing (maybe player not found)", Name.c_str());
						}
						else
						{
							// 这里期望 points 是整型节点。
							int Points = json_int_get(pPointsVal);

							Entry.m_Points = Points;
							Entry.m_Status = EPointsStatus::READY;
							Entry.m_LastSuccessTime = time_get();
							// 成功路径默认不打日志，避免刷屏。
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
	
	// 空对象 {} 通常表示玩家不存在。
	if(DataSize <= 2)
		return false;
	
	// 先定位外层 "points" 对象。
	const char *pPointsObj = str_find(pData, "\"points\"");
	if(!pPointsObj)
		return false;
	
	// 跳过 "points" 键名。
	pPointsObj += 8;
	
	// 跳过空白和冒号。
	while(*pPointsObj && (*pPointsObj == ' ' || *pPointsObj == '\t' || *pPointsObj == '\n' || *pPointsObj == '\r' || *pPointsObj == ':'))
		pPointsObj++;
	
	// 期待对象起始 '{'。
	if(*pPointsObj != '{')
		return false;
	pPointsObj++;
	
	// 再定位内层 "points" 字段。
	const char *pPointsField = str_find(pPointsObj, "\"points\"");
	if(!pPointsField)
		return false;
	
	// 跳过 "points" 键名。
	pPointsField += 8;
	
	// 跳过空白和冒号。
	while(*pPointsField && (*pPointsField == ' ' || *pPointsField == '\t' || *pPointsField == '\n' || *pPointsField == '\r' || *pPointsField == ':'))
		pPointsField++;
	
	// 解析积分整数值。
	char *pEnd = nullptr;
	long Value = strtol(pPointsField, &pEnd, 10);
	
	if(pEnd == pPointsField || Value < 0)
		return false;
	
	OutPoints = (int)Value;
	return true;
}
