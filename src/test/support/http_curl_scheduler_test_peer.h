#ifndef TEST_SUPPORT_HTTP_CURL_SCHEDULER_TEST_PEER_H
#define TEST_SUPPORT_HTTP_CURL_SCHEDULER_TEST_PEER_H

#include <engine/shared/http_curl.h>

#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <vector>

// 只安排已初始化的传输边界；调用真实调度器，不执行multi_perform或访问网络。
class CHttpCurlSchedulerTestPeer
{
	CHttpCurl m_Http;

public:
	CHttpCurlSchedulerTestPeer()
	{
		m_Http.m_pMultiH = curl_multi_init();
		m_Http.m_State = CHttpCurl::RUNNING;
	}
	~CHttpCurlSchedulerTestPeer()
	{
		m_Http.DrainRequests(true);
		curl_multi_cleanup(m_Http.m_pMultiH);
		m_Http.m_pMultiH = nullptr;
	}
	bool Ready() const { return m_Http.m_pMultiH != nullptr; }
	IHttp &Http() { return m_Http; }
	void Run(const std::shared_ptr<IHttpRequest> &pRequest) { m_Http.Run(pRequest); }
	bool Pump() { return m_Http.ProcessPendingRequests(); }
	void RejectNewRequests() { m_Http.m_State = CHttpCurl::ERROR; }
	void Drain() { m_Http.DrainRequests(true); }
	void Cancel() { m_Http.CancelAbortedRequests(); }
	size_t Running() const { return m_Http.m_RunningRequests.size(); }
	size_t RunningForHost(std::string_view Host) const { return m_Http.RunningForHost(Host); }
	bool Tracked(const IHttpRequest *pRequest) const
	{
		for(const auto &Entry : m_Http.m_RunningRequests)
			if(Entry.second.get() == pRequest)
				return true;
		return false;
	}
	std::vector<std::shared_ptr<CHttpRequestCurl>> Pending() const
	{
		return {m_Http.m_PendingRequests.begin(), m_Http.m_PendingRequests.end()};
	}
};

#endif

#endif
