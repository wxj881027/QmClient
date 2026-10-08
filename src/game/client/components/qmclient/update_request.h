#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_REQUEST_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_UPDATE_REQUEST_H

#include "update_sources.h"

#include <engine/http.h>

#include <functional>
#include <memory>

namespace qm_update
{
	// 代理作业尚未移交的请求也必须走 HTTP 的取消完成路径，不能永远停在 QUEUED。
	inline bool CompleteAbortedUpdateRequest(IHttp &Http, const std::shared_ptr<IHttpRequest> &Request)
	{
		if(!Request->IsAbortRequested())
			return false;
		if(!Request->Done())
			Http.Run(Request);
		return true;
	}

	// 429 没有有效 Retry-After 时仍需冷却，防止用户连续重查密集请求官方源。
	inline double SourceRetryDelay(const IHttpRequest *pRequest)
	{
		if(!pRequest || !pRequest->Done())
			return 0;
		const double RetryAfter = static_cast<double>(pRequest->ResultRetryAfterSeconds().value_or(0));
		return pRequest->CompletedStatusCode() == 429 ? std::max(300.0, RetryAfter) : RetryAfter;
	}

	// 官方显式代理和直连共用批准 URL；最多一次路由回退，不能绕过限流窗口。
	template<size_t N>
	bool CanRetryOfficialDirect(const CSource *Source, bool AlreadyRetried, const std::shared_ptr<IHttpRequest> (&Requests)[N])
	{
		if(!Source || !Source->m_Prefix.empty() || AlreadyRetried)
			return false;
		bool UsedProxy = false;
		for(const auto &Request : Requests)
		{
			if(!Request)
				continue;
			if(!Request->Done() || Request->CompletedStatusCode() == 429 || Request->ResultRetryAfterSeconds().value_or(0) > 0)
				return false;
			UsedProxy |= Request->CompletedUsedProxy() || Request->ProxyUrl()[0] != '\0';
		}
		return UsedProxy;
	}

	// 工厂只负责启动请求；验证器调用真实解析或签名验证，不在状态机中复制格式逻辑。
	class CUpdateRequest
	{
	public:
		enum class EState
		{
			IDLE,
			RUNNING,
			SUCCEEDED,
			FAILED,
			CANCELLED
		};
		using TFactory = std::function<std::shared_ptr<IHttpRequest>(const std::string &)>;
		using TValidator = std::function<bool(const IHttpRequest &)>;

	private:
		CSourceRegistry *m_pSources = nullptr;
		CSourceAttempt m_Attempt;
		CProgressDeadline m_Deadline;
		std::shared_ptr<IHttpRequest> m_pRequest;
		TFactory m_Factory;
		TFactory m_DirectOfficialFactory;
		bool m_DirectOfficialRetried = false;
		TValidator m_Validator;
		std::string m_OfficialUrl;
		EState m_State = EState::IDLE;
		bool m_RejectedContent = false;

		void Start(double Now)
		{
			m_Deadline.Begin(Now);
			m_pRequest = m_Factory(m_Attempt.Current()->m_Prefix + m_OfficialUrl);
		}

	public:
		~CUpdateRequest() { Cancel(); }
		void Begin(CSourceRegistry &Sources, const std::string &OfficialUrl, EResource Resource, double Now, TFactory Factory, TValidator Validator, TFactory DirectOfficialFactory = {})
		{
			Cancel();
			m_RejectedContent = false;
			m_DirectOfficialRetried = false;
			m_DirectOfficialFactory = std::move(DirectOfficialFactory);
			m_pSources = &Sources;
			m_OfficialUrl = OfficialUrl;
			m_Factory = std::move(Factory);
			m_Validator = std::move(Validator);
			m_Attempt.Begin(Sources.Candidates(OfficialUrl, Resource, Now));
			m_State = m_Attempt.Current() ? EState::RUNNING : EState::FAILED;
			if(m_State == EState::RUNNING)
				Start(Now);
		}
		EState Poll(double Now)
		{
			if(m_State != EState::RUNNING)
				return m_State;
			if(m_pRequest && !m_pRequest->Done())
			{
				if(m_Deadline.Expired(Now, m_pRequest->Current()))
					m_pRequest->Abort();
				return m_State;
			}
			if(m_pRequest && m_pRequest->State() == EHttpState::DONE && m_pRequest->StatusCode() == 200 && m_Validator(*m_pRequest))
			{
				m_pSources->Succeeded(*m_Attempt.Current());
				m_State = EState::SUCCEEDED;
			}
			else
			{
				m_RejectedContent |= m_pRequest && m_pRequest->State() == EHttpState::DONE && m_pRequest->StatusCode() == 200;
				const std::shared_ptr<IHttpRequest> apRequests[] = {m_pRequest};
				if(m_DirectOfficialFactory && CanRetryOfficialDirect(m_Attempt.Current(), m_DirectOfficialRetried, apRequests))
				{
					m_DirectOfficialRetried = true;
					m_Deadline.Begin(Now);
					m_pRequest = m_DirectOfficialFactory(m_OfficialUrl);
					return m_State;
				}
				m_pSources->Failed(*m_Attempt.Current(), Now, SourceRetryDelay(m_pRequest.get()));
				if(m_Attempt.Next())
					Start(Now);
				else
					m_State = EState::FAILED;
			}
			return m_State;
		}
		void Cancel()
		{
			if(m_pRequest && !m_pRequest->Done())
				m_pRequest->Abort();
			m_pRequest.reset();
			m_Attempt.Cancel();
			m_State = EState::CANCELLED;
		}
		const std::shared_ptr<IHttpRequest> &Request() const { return m_pRequest; }
		const CSource *Source() const { return m_Attempt.Current(); }
		bool RejectedContent() const { return m_RejectedContent; }
		// 系统代理解析有独立预算，移交 HTTP 时才开始传输截止时间。
		void BeginTransfer(double Now)
		{
			if(m_State == EState::RUNNING)
				m_Deadline.Begin(Now);
		}
	};
	// 失败换源时旧请求虽已完成但指针仍在；允许完成批次重启，禁止运行中重入。
	template<size_t N>
	bool CanStartDownloadBatch(bool SurveyRunning, bool Retry, const std::shared_ptr<IHttpRequest> (&Requests)[N])
	{
		if(SurveyRunning)
			return false;
		for(const auto &Request : Requests)
			if(Request && (!Retry || !Request->Done()))
				return false;
		return true;
	}

	// 所有附件共用批次失败和进度监控，包含索引零的完整包。
	template<size_t N>
	void PollDownloadBatch(const std::shared_ptr<IHttpRequest> (&Requests)[N], CProgressDeadline (&Deadlines)[N], double Now)
	{
		bool Failed = false;
		for(const auto &Request : Requests)
			Failed |= Request && Request->Done() && (Request->State() != EHttpState::DONE || Request->StatusCode() != 200);
		for(size_t Index = 0; Index < N; ++Index)
			if(Requests[Index] && !Requests[Index]->Done() && (Failed || Deadlines[Index].Expired(Now, Requests[Index]->Current())))
				Requests[Index]->Abort();
	}
}
#endif
