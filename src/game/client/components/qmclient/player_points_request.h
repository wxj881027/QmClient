#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYER_POINTS_REQUEST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYER_POINTS_REQUEST_H

#include <engine/http.h>

#include <memory>
#include <utility>

// 代理解析与 HTTP 移交分开，后台作业不持有 HTTP 或客户端指针。
class CQmPlayerPointsRequest
{
	std::shared_ptr<IHttpRequest> m_pRequest;
	double m_ProxyDeadline;
	bool m_Submitted = false;

public:
	CQmPlayerPointsRequest(std::shared_ptr<IHttpRequest> pRequest, double Now) :
		m_pRequest(std::move(pRequest)),
		m_ProxyDeadline(Now + 8.0)
	{
	}
	CQmPlayerPointsRequest(const CQmPlayerPointsRequest &) = delete;
	CQmPlayerPointsRequest &operator=(const CQmPlayerPointsRequest &) = delete;
	CQmPlayerPointsRequest(CQmPlayerPointsRequest &&) = default;
	CQmPlayerPointsRequest &operator=(CQmPlayerPointsRequest &&) = default;

	// nullptr 表示没有代理决定；空字符串表示系统明确选择直连。
	template<typename TSubmit>
	bool PollWithSubmit(TSubmit &&Submit, double Now, bool ProxyReady, const char *pProxy)
	{
		if(!m_pRequest)
			return true;
		if(m_Submitted)
			return true;
		if(!m_pRequest->IsAbortRequested() && !ProxyReady && Now < m_ProxyDeadline)
			return false;
		if(!m_pRequest->IsAbortRequested() && ProxyReady && pProxy != nullptr)
			m_pRequest->Proxy(pProxy);
		m_Submitted = true;
		Submit(m_pRequest);
		return true;
	}

	template<typename TSubmit>
	void CancelWithSubmit(TSubmit &&Submit)
	{
		if(!m_pRequest)
			return;
		if(!m_pRequest->Done())
			m_pRequest->Abort();
		// 尚未发出的请求也交给 HTTP 完成取消，不能留在 QUEUED。
		PollWithSubmit(std::forward<TSubmit>(Submit), m_ProxyDeadline, false, nullptr);
	}

	bool Poll(IHttp &Http, double Now, bool ProxyReady, const char *pProxy)
	{
		return PollWithSubmit([&](const auto &pRequest) { Http.Run(pRequest); }, Now, ProxyReady, pProxy);
	}
	void Cancel(IHttp &Http)
	{
		CancelWithSubmit([&](const auto &pRequest) { Http.Run(pRequest); });
	}

	const std::shared_ptr<IHttpRequest> &Request() const { return m_pRequest; }
	bool Submitted() const { return m_Submitted; }
};

#endif
