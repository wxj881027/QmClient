#ifndef ENGINE_HTTP_RESPONSE_VALIDATION_H
#define ENGINE_HTTP_RESPONSE_VALIDATION_H

#include <engine/http.h>

// 借用已完成且启用 ValidateBeforeOverwrite 的请求；同线程决定发布，未接受则丢弃临时文件。
class CHttpResponseValidation
{
	IHttpRequest *m_pRequest;

public:
	explicit CHttpResponseValidation(IHttpRequest &Request) noexcept :
		m_pRequest(&Request)
	{
		dbg_assert(Request.State() == EHttpState::DONE, "HTTP response is not ready for validation");
	}
	CHttpResponseValidation(const CHttpResponseValidation &) = delete;
	CHttpResponseValidation &operator=(const CHttpResponseValidation &) = delete;
	~CHttpResponseValidation()
	{
		if(m_pRequest)
			m_pRequest->OnValidation(false);
	}
	bool Commit() noexcept
	{
		IHttpRequest *pRequest = m_pRequest;
		m_pRequest = nullptr;
		if(!pRequest)
			return false;
		try
		{
			const bool Accept = pRequest->State() == EHttpState::DONE && !pRequest->IsAbortRequested();
			pRequest->OnValidation(Accept);
			return Accept && pRequest->State() == EHttpState::DONE;
		}
		catch(...)
		{
			return false;
		}
	}
};

#endif
