#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_DOWNLOAD_SESSION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_DOWNLOAD_SESSION_H

#include <base/log.h>

#include <engine/http.h>
#include <engine/shared/jobs.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

// CPU 作业只读取或解码图片；网络等待由主线程轮询，不占通用作业线程。
class IQmSkinDataJob : public IJob
{
public:
	virtual bool HasData() const = 0;
};

class CQmSkinDownloadSession
{
public:
	enum class EResult
	{
		WAITING,
		READY,
		DONE,
		ERROR,
		NOT_FOUND
	};
	using FRequest = std::function<std::shared_ptr<IHttpRequest>(const char *, bool)>;
	using FDecode = std::function<std::shared_ptr<IQmSkinDataJob>(std::shared_ptr<IHttpRequest>)>;
	using FSubmit = std::function<void(std::shared_ptr<IJob>)>;

	CQmSkinDownloadSession(IHttp &Http, std::string OfficialUrl, std::string CommunityUrl,
		std::shared_ptr<IQmSkinDataJob> pCacheJob, FRequest Request, FDecode Decode, FSubmit Submit) :
		m_Http(Http), m_OfficialUrl(std::move(OfficialUrl)), m_CommunityUrl(std::move(CommunityUrl)), m_pJob(std::move(pCacheJob)), m_Request(std::move(Request)), m_Decode(std::move(Decode)), m_Submit(std::move(Submit))
	{
		m_Submit(m_pJob);
	}
	~CQmSkinDownloadSession() { Cancel(); }

	EResult Poll()
	{
		if(m_pReadyJob)
			return EResult::READY;
		if(m_Result != EResult::WAITING)
			return m_Result;
		if(m_pJob)
		{
			if(!m_pJob->Done())
				return EResult::WAITING;
			const bool Valid = m_pJob->State() == IJob::STATE_DONE && m_pJob->HasData();
			if(m_CachePhase)
			{
				m_CachePhase = false;
				m_HasCache = Valid;
				if(Valid)
					m_pReadyJob = std::move(m_pJob);
				else
					m_pJob.reset();
				StartRequest(false);
				return Valid ? EResult::READY : EResult::WAITING;
			}
			// 只有当前会话的有效解码结果才可以替换磁盘缓存。
			m_pRequest->OnValidation(Valid);
			m_NeedsValidation = false;
			if(Valid)
			{
				m_pReadyJob = std::move(m_pJob);
				m_Result = EResult::DONE;
				m_pRequest.reset();
				return EResult::READY;
			}
			return Finish(EResult::ERROR);
		}
		if(!m_pRequest->Done())
			return EResult::WAITING;
		if(m_pRequest->State() != EHttpState::DONE)
		{
			log_error("skins", "Skin request failed: %s (state %d)", m_pRequest->Url(), (int)m_pRequest->State());
			return Finish(EResult::ERROR);
		}
		const int Status = m_pRequest->StatusCode();
		if(Status == 404 && !m_Community && !m_CommunityUrl.empty() && m_CommunityUrl != m_OfficialUrl)
		{
			ValidateRequest(false);
			m_Community = true;
			// 两个来源的修改时间不能互相充当条件请求的验证器。
			StartRequest(true);
			return EResult::WAITING;
		}
		if(Status == 304)
		{
			ValidateRequest(m_HasCache);
			if(m_HasCache)
				return Finish(EResult::DONE);
			if(!m_RetriedUnconditional)
			{
				m_RetriedUnconditional = true;
				StartRequest(true);
				return EResult::WAITING;
			}
			return Finish(EResult::ERROR);
		}
		if(Status != 200)
		{
			log_error("skins", "Skin request returned HTTP %d: %s", Status, m_pRequest->Url());
			return Finish(Status == 404 ? EResult::NOT_FOUND : EResult::ERROR);
		}
		m_pJob = m_Decode(m_pRequest);
		m_Submit(m_pJob);
		return EResult::WAITING;
	}

	std::shared_ptr<IQmSkinDataJob> TakeReadyJob() { return std::exchange(m_pReadyJob, nullptr); }
	void Cancel()
	{
		if(m_pJob)
			m_pJob->Abort();
		if(m_pReadyJob)
			m_pReadyJob->Abort();
		if(m_pRequest)
		{
			if(m_pRequest->Done())
				ValidateRequest(false);
			else
				m_pRequest->Abort();
		}
		m_pJob.reset();
		m_pReadyJob.reset();
		m_pRequest.reset();
		m_Result = EResult::DONE;
	}

private:
	void StartRequest(bool Unconditional)
	{
		m_pRequest = m_Request((m_Community ? m_CommunityUrl : m_OfficialUrl).c_str(), Unconditional);
		m_pRequest->Timeout(CTimeout{10000, 30000, 8192, 10});
		m_pRequest->MaxResponseSize(10 * 1024 * 1024);
		m_pRequest->ValidateBeforeOverwrite(true);
		m_pRequest->LogProgress(HTTPLOG::FAILURE);
		m_pRequest->FailOnErrorStatus(false);
		m_NeedsValidation = true;
		m_Http.Run(m_pRequest);
	}
	void ValidateRequest(bool Valid)
	{
		if(m_NeedsValidation && m_pRequest && m_pRequest->State() == EHttpState::DONE)
			m_pRequest->OnValidation(Valid);
		m_NeedsValidation = false;
	}
	EResult Finish(EResult Result)
	{
		ValidateRequest(false);
		m_pRequest.reset();
		m_pJob.reset();
		// 远端失败不能撤销已经交付的有效缓存。
		m_Result = m_HasCache ? EResult::DONE : Result;
		return m_Result;
	}
	IHttp &m_Http;
	std::string m_OfficialUrl;
	std::string m_CommunityUrl;
	std::shared_ptr<IQmSkinDataJob> m_pJob;
	std::shared_ptr<IQmSkinDataJob> m_pReadyJob;
	std::shared_ptr<IHttpRequest> m_pRequest;
	FRequest m_Request;
	FDecode m_Decode;
	FSubmit m_Submit;
	EResult m_Result = EResult::WAITING;
	bool m_CachePhase = true;
	bool m_HasCache = false;
	bool m_Community = false;
	bool m_RetriedUnconditional = false;
	bool m_NeedsValidation = false;
};

#endif
