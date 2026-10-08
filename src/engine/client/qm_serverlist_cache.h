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

public:
	bool Publish(std::vector<TServer> vServers, int Age, int64_t Now)
	{
		if(vServers.empty())
		{
			RefreshFailed();
			return false;
		}
		m_vServers = std::move(vServers);
		m_ReceivedAt = Now;
		m_SourceAge = std::max(0, Age);
		m_RefreshFailed = false;
		return true;
	}
	void RefreshFailed() { m_RefreshFailed = true; }
	bool HasRefreshFailed() const { return m_RefreshFailed; }
	bool IsStale(int64_t Now) const { return !m_vServers.empty() && m_SourceAge + std::max<int64_t>(0, Now - m_ReceivedAt) > 300; }
	const std::vector<TServer> &Servers() const { return m_vServers; }
};

#endif
