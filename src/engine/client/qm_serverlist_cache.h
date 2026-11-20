#ifndef ENGINE_CLIENT_QM_SERVERLIST_CACHE_H
#define ENGINE_CLIENT_QM_SERVERLIST_CACHE_H

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// 新结果完整且非空才替换缓存；失败与缓存新鲜度相互独立。
template<typename TServer>
class CQmServerListCache
{
	std::vector<TServer> m_vServers;
	int64_t m_ReceivedAt = 0;
	int m_SourceAge = 0;
	bool m_RefreshFailed = false;
	bool m_RequireLiveRefresh = false;

public:
	bool Publish(std::vector<TServer> vServers, int Age, int64_t Now, bool RequireLiveRefresh = false)
	{
		if(vServers.empty())
		{
			RefreshFailed();
			return false;
		}
		m_vServers = std::move(vServers);
		m_ReceivedAt = Now;
		m_SourceAge = std::max(0, Age);
		RefreshSucceeded();
		m_RequireLiveRefresh = RequireLiveRefresh;
		return true;
	}
	void RefreshSucceeded() { m_RefreshFailed = false; }
	void RefreshFailed() { m_RefreshFailed = true; }
	bool HasRefreshFailed() const { return m_RefreshFailed; }
	int64_t Age(int64_t Now) const { return m_SourceAge + std::max<int64_t>(0, Now - m_ReceivedAt); }
	bool IsStale(int64_t Now) const { return !m_vServers.empty() && (m_RequireLiveRefresh || Age(Now) > 300); }
	const std::vector<TServer> &Servers() const { return m_vServers; }
};

#endif
