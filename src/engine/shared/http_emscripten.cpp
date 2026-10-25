#include "http_emscripten.h"
#if defined(CONF_PLATFORM_EMSCRIPTEN)

#include <base/dbg.h>
#include <base/log.h>
#include <base/str.h>
#include <base/thread.h>

#include <engine/shared/config.h>
#include <engine/shared/http_response_headers.h>
#include <engine/storage.h>

#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>

#include <limits>
#include <thread>

CHttpRequestEmscripten::CHttpRequestEmscripten(const char *pUrl) :
	IHttpRequest(pUrl)
{
}

CHttpRequestEmscripten::~CHttpRequestEmscripten()
{
	dbg_assert(m_pFetch == nullptr, "HTTP request fetch handle was not closed");
}

void CHttpRequestEmscripten::Header(const char *pNameColonValue)
{
	const char *pColon = str_find(pNameColonValue, ":");
	dbg_assert(pColon != nullptr, "Header name and value not separated with colon: '%s'", pNameColonValue);
	std::string Name = std::string(pNameColonValue, pColon - pNameColonValue);
	std::string Value = std::string(str_skip_whitespaces_const(pColon + 1));
	m_vRequestHeaders.emplace_back(std::move(Name), std::move(Value));
}

void CHttpRequestEmscripten::Abort()
{
	if(m_Abort.exchange(true))
	{
		return;
	}

	// 主线程不读取由 SDK 线程拥有的 fetch 指针。
	if(auto *pHttp = m_pHttp.load(); pHttp && !Done())
		pHttp->AddPendingStateChange(*this, EHttpState::ABORTED);
}

EM_JS(void, FormatTimestampJsImpl, (char *pBuf, size_t Size, time_t Timestamp), {
	const timestampString = new Date(Number(Timestamp) * 1000).toUTCString();
	stringToUTF8(timestampString, pBuf, Size);
});

bool CHttpRequestEmscripten::ConfigureAndRun()
{
	if(!BeforeInit())
	{
		return false;
	}

	HeaderString("User-Agent", USER_AGENT_STRING);

	if(m_Type == REQUEST::POST_JSON)
	{
		Header("Content-Type: application/json");
	}
	else if(m_Type == REQUEST::POST)
	{
		Header("Content-Type:");
	}

	if(m_IfModifiedSince >= 0)
	{
		char aTimestamp[64];
		FormatTimestampJsImpl(aTimestamp, sizeof(aTimestamp), m_IfModifiedSince);
		HeaderString("If-Modified-Since", aTimestamp);
	}

	std::vector<const char *> vpPackedHeaders;
	vpPackedHeaders.reserve(2 * m_vRequestHeaders.size() + 1);
	for(const auto &[Name, Value] : m_vRequestHeaders)
	{
		vpPackedHeaders.push_back(Name.c_str());
		vpPackedHeaders.push_back(Value.c_str());
	}
	vpPackedHeaders.push_back(nullptr);

	emscripten_fetch_attr_t FetchAttributes;
	emscripten_fetch_attr_init(&FetchAttributes);
	str_copy(FetchAttributes.requestMethod, GetRequestType(m_Type));
	FetchAttributes.userData = this;
	FetchAttributes.onsuccess = FetchCallbackSuccess;
	FetchAttributes.onerror = FetchCallbackFailure;
	FetchAttributes.onprogress = FetchCallbackProgress;
	FetchAttributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
	// Using low speed limit/time properties of timeout is not supported.
	FetchAttributes.timeoutMSecs = m_Timeout.m_ConnectTimeoutMs + m_Timeout.m_TimeoutMs;
	FetchAttributes.requestHeaders = vpPackedHeaders.data();
	FetchAttributes.requestData = reinterpret_cast<const char *>(m_pBody);
	FetchAttributes.requestDataSize = m_BodyLength;

	{
		std::unique_lock WaitLock(m_WaitMutex);
		m_State = EHttpState::RUNNING;
	}
	m_pFetch = emscripten_fetch(&FetchAttributes, m_aUrl);
	// SDK 的资源申请失败可以返回空句柄，交回既有初始化失败收尾。
	return m_pFetch != nullptr;
}

void CHttpRequestEmscripten::OnSuccess(emscripten_fetch_t *pFetch)
{
	if(m_CallbackFinished)
		return;
	m_pFetch = pFetch;
	m_CallbackFinished = true;

	const uint64_t BodySize = m_pFetch->numBytes;
	const bool Accepted = BodySize <= std::numeric_limits<size_t>::max() &&
			      (OnData(m_pFetch->data, static_cast<size_t>(BodySize)) == BodySize || m_ResponseSampleComplete);
	// 正文超限、不可表示或分配失败时不能发布成功；有界采样主动截断仍成功。
	m_pHttp.load()->AddPendingStateChange(*this, Accepted ? EHttpState::DONE : EHttpState::ERROR);
}

void CHttpRequestEmscripten::OnFailure(emscripten_fetch_t *pFetch)
{
	if(m_CallbackFinished)
	{
		// Failure callback may be called several times after the fetch handle has already been closed.
		return;
	}
	m_pFetch = pFetch;
	m_CallbackFinished = true;

	m_pHttp.load()->AddPendingStateChange(*this, !m_FailOnErrorStatus && pFetch->status >= 400 ? EHttpState::DONE : EHttpState::ERROR);
}

void CHttpRequestEmscripten::OnProgress(emscripten_fetch_t *pFetch)
{
	if(m_CallbackFinished)
		return;
	m_pFetch = pFetch;
	m_Current.store(pFetch->dataOffset, std::memory_order_relaxed);
	m_Size.store(pFetch->totalBytes, std::memory_order_relaxed);
	m_Progress.store(pFetch->totalBytes == 0 ? 0 : (100 * pFetch->dataOffset) / pFetch->totalBytes, std::memory_order_relaxed);
	if(!PublishProgress())
	{
		m_CallbackFinished = true;
		m_pHttp.load()->AddPendingStateChange(*this, EHttpState::ERROR);
	}
}

EM_JS(int64_t, ParseDateJsImpl, (const char *pStr), {
	const parsedDate = Date.parse(UTF8ToString(pStr));
	if(isNaN(parsedDate)) // Invalid dates are represented as NaN for some reason
	{
		return -1;
	}
	return BigInt(Math.floor(parsedDate / 1000));
});

void CHttpRequestEmscripten::OnHeader(const char *pName, const char *pValue)
{
	if(str_comp_nocase(pName, "Date") == 0)
	{
		const int64_t Value = ParseDateJsImpl(pValue);
		if(Value != -1)
		{
			m_ResultDate = Value;
		}
	}
	else if(str_comp_nocase(pName, "Last-Modified") == 0)
	{
		const int64_t Value = ParseDateJsImpl(pValue);
		if(Value != -1)
		{
			m_ResultLastModified = Value;
		}
	}
}

void CHttpRequestEmscripten::OnCompletionInternal(EHttpState State, const char *pErrorDetail)
{
	m_CallbackFinished = true;
	if(IsAbortRequested())
		State = EHttpState::ABORTED;
	if(m_pFetch != nullptr)
	{
		const size_t HeadersLength = emscripten_fetch_get_response_headers_length(m_pFetch);
		if(!ReadHttpResponseHeaders(HeadersLength, [this](char *pHeaders, size_t Capacity) { return emscripten_fetch_get_response_headers(m_pFetch, pHeaders, Capacity); }, [this](const char *pName, const char *pValue) { OnHeader(pName, pValue); }) && State != EHttpState::ABORTED)
		{
			State = EHttpState::ERROR;
			pErrorDetail = "Failed to read response headers";
		}

		m_StatusCode = m_pFetch->status;
	}

	if(State == EHttpState::DONE)
	{
		if(g_Config.m_DbgHttp || m_LogProgress >= HTTPLOG::ALL)
		{
			log_info("http", "task done: %s", m_aUrl);
		}
	}
	else
	{
		if(g_Config.m_DbgHttp || m_LogProgress >= HTTPLOG::FAILURE)
		{
			const char *pError =
				pErrorDetail != nullptr                                  ? pErrorDetail :
				State == EHttpState::ABORTED                             ? "Request aborted" :
				(m_pFetch != nullptr && m_pFetch->statusText[0] != '\0') ? m_pFetch->statusText :
											   "Unknown (check the web browser console)";
			log_error("http", "%s failed. fetch error: %s", m_aUrl, pError);
		}
	}

	if(m_pFetch != nullptr)
	{
		// Unset member variable before closing handle so completion callbacks are not called again.
		emscripten_fetch_t *pFetch = m_pFetch;
		m_pFetch = nullptr;
		dbg_assert(emscripten_fetch_close(pFetch) == EMSCRIPTEN_RESULT_SUCCESS, "emscripten_fetch_close failure");
	}

	IHttpRequest::OnCompletionInternal(State);
}

void CHttpRequestEmscripten::FetchCallbackSuccess(emscripten_fetch_t *pFetch)
{
	static_cast<CHttpRequestEmscripten *>(pFetch->userData)->OnSuccess(pFetch);
}

void CHttpRequestEmscripten::FetchCallbackFailure(emscripten_fetch_t *pFetch)
{
	static_cast<CHttpRequestEmscripten *>(pFetch->userData)->OnFailure(pFetch);
}

void CHttpRequestEmscripten::FetchCallbackProgress(emscripten_fetch_t *pFetch)
{
	static_cast<CHttpRequestEmscripten *>(pFetch->userData)->OnProgress(pFetch);
}

std::unique_ptr<IHttpRequest> CreateHttpRequest(const char *pUrl)
{
	return std::make_unique<CHttpRequestEmscripten>(pUrl);
}

bool CHttpEmscripten::Init(std::chrono::milliseconds ShutdownDelay)
{
	std::unique_lock Lock(m_Lock);
	if(m_pThread)
		return m_Initialized && !m_Shutdown;
	m_ShutdownDelay = ShutdownDelay;
	m_Shutdown = false;

	m_pThread = thread_try_init(CHttpEmscripten::ThreadMain, this, "http");
	if(!m_pThread)
	{
		m_Shutdown = true;
		m_ConditionVariableInit.notify_all();
		return false;
	}
	m_ConditionVariableInit.wait(Lock, [this]() { return m_Initialized.load(std::memory_order_seq_cst); });

	return true;
}

void CHttpEmscripten::Shutdown()
{
	{
		std::unique_lock Lock(m_Lock);
		if(m_Shutdown || !m_Initialized)
		{
			return;
		}
		m_Shutdown = true;
	}
	m_ConditionVariableLoop.notify_all();
}

CHttpEmscripten::~CHttpEmscripten()
{
	if(!m_pThread)
	{
		return;
	}

	Shutdown();
	thread_wait(m_pThread);
}

void CHttpEmscripten::Run(std::shared_ptr<IHttpRequest> pRequest)
{
	auto pRequestImpl = std::static_pointer_cast<CHttpRequestEmscripten>(pRequest);
	bool ShuttingDown;
	bool QueueFailed = false;
	{
		std::unique_lock Lock(m_Lock);
		m_ConditionVariableInit.wait(Lock, [this]() { return m_Initialized || m_Shutdown; });
		if(pRequestImpl->m_Submitted)
			return;
		pRequestImpl->m_Submitted = true;
		pRequestImpl->m_pHttp = this;
		ShuttingDown = m_Shutdown;
		if(!ShuttingDown)
		{
			try
			{
				m_PendingRequests.emplace_back(pRequestImpl);
			}
			catch(...)
			{
				QueueFailed = true;
			}
		}
	}
	// 完成消费者可以重新提交别的请求，不能在持有队列锁时调用。
	if(ShuttingDown || QueueFailed)
		pRequestImpl->OnCompletionInternal(ShuttingDown ? EHttpState::ABORTED : EHttpState::ERROR,
			ShuttingDown ? "Shutting down" : "Failed to queue request");
	m_ConditionVariableLoop.notify_all();
}

EM_JS(void, EscapeUrlJsImpl, (char *pBuf, size_t Size, const char *pStr), {
	const escapedString = encodeURIComponent(UTF8ToString(pStr));
	stringToUTF8(escapedString, pBuf, Size);
});

void EscapeUrl(char *pBuf, size_t Size, const char *pStr)
{
	EscapeUrlJsImpl(pBuf, Size, pStr);
}

bool CHttpEmscripten::HasIpresolveBug() const
{
	return false;
}

void CHttpEmscripten::ThreadMain(void *pUser)
{
	static_cast<CHttpEmscripten *>(pUser)->RunLoop();
}

void CHttpEmscripten::RunLoop()
{
	{
		std::unique_lock Lock(m_Lock);
		m_Initialized = true;
	}
	m_ConditionVariableInit.notify_all();

	while(true)
	{
		{
			std::unique_lock Lock(m_Lock);
			if(m_Shutdown)
			{
				if(m_RunningRequests.empty() && m_PendingRequests.empty())
					break;

				const auto Now = std::chrono::steady_clock::now();
				if(!m_ShutdownTime.has_value())
				{
					m_ShutdownTime = Now + m_ShutdownDelay;
				}
				else if(m_ShutdownTime < Now)
				{
					if(m_StartedShutdown)
					{
						break;
					}
					else
					{
						for(auto &[_, pRequest] : m_RunningRequests)
						{
							pRequest->m_Completion.Publish(EHttpState::ABORTED);
						}
						m_HasPendingStateChanges = true;
						m_StartedShutdown = true;
						m_ShutdownTime = Now + m_ShutdownDelay;
					}
				}
			}
		}

		decltype(m_PendingRequests) PendingRequests = {};
		{
			std::unique_lock Lock(m_Lock);
			std::swap(m_PendingRequests, PendingRequests);
			m_HasPendingStateChanges = false;
		}

		for(const auto &pRequest : PendingRequests)
		{
			try
			{
				if(g_Config.m_DbgHttp)
				{
					log_debug("http", "task: %s %s", CHttpRequestEmscripten::GetRequestType(pRequest->m_Type), pRequest->m_aUrl);
				}
				if(pRequest->ShouldSkipRequest())
				{
					pRequest->PublishCompletion(EHttpState::DONE);
					continue;
				}
				if(m_StartedShutdown || pRequest->IsAbortRequested())
				{
					pRequest->OnCompletionInternal(EHttpState::ABORTED, m_StartedShutdown ? "Shutting down" : "Request aborted");
					continue;
				}
				// 先登记稳定的请求身份，再创建 SDK 句柄；同步回调也已经有 owner。
				const auto [_, Inserted] = m_RunningRequests.emplace(pRequest.get(), pRequest);
				if(!Inserted)
					continue;
				if(!pRequest->ConfigureAndRun())
				{
					pRequest->OnCompletionInternal(EHttpState::ABORTED, "Failed to initialize request");
					m_RunningRequests.erase(pRequest.get());
				}
			}
			catch(...)
			{
				pRequest->OnCompletionInternal(EHttpState::ERROR, "Failed to prepare request");
				m_RunningRequests.erase(pRequest.get());
			}
		}

		for(auto It = m_RunningRequests.begin(); It != m_RunningRequests.end();)
		{
			const auto NewState = It->second->m_Completion.State();
			if(NewState == EHttpState::QUEUED)
			{
				++It;
			}
			else
			{
				It->second->OnCompletionInternal(NewState, nullptr);
				It = m_RunningRequests.erase(It);
			}
		}

		// Return control to the browser so the created fetch handles are serviced.
		// This will cause the success, failure and progress callbacks to be called.
		emscripten_sleep(0);

		// Wait a bit for state changes, but also wake up periodically because we
		// need to call emscripten_sleep to service the handles.
		std::unique_lock Lock(m_Lock);
		const auto &&WaitPredicate = [this]() { return m_Shutdown || !m_PendingRequests.empty() || m_HasPendingStateChanges; };
		const auto WaitTime = std::chrono::milliseconds(100);
		const auto Now = std::chrono::steady_clock::now();
		if(m_ShutdownTime.has_value() && m_ShutdownTime.value() - Now < WaitTime)
		{
			m_ConditionVariableLoop.wait_until(Lock, m_ShutdownTime.value(), WaitPredicate);
		}
		else
		{
			m_ConditionVariableLoop.wait_for(Lock, WaitTime, WaitPredicate);
		}
	}

	decltype(m_PendingRequests) PendingRequests;
	{
		std::unique_lock Lock(m_Lock);
		PendingRequests.swap(m_PendingRequests);
	}
	for(auto &pRequest : PendingRequests)
	{
		pRequest->OnCompletionInternal(EHttpState::ABORTED, "Shutting down");
	}
	PendingRequests.clear();

	for(auto &[_, pRequest] : m_RunningRequests)
	{
		pRequest->OnCompletionInternal(EHttpState::ABORTED, "Shutting down");
	}
	m_RunningRequests.clear();
}

void CHttpEmscripten::AddPendingStateChange(CHttpRequestEmscripten &Request, EHttpState State)
{
	Request.m_Completion.Publish(State);
	{
		std::unique_lock Lock(m_Lock);
		m_HasPendingStateChanges = true;
	}
	m_ConditionVariableLoop.notify_all();
}

IEngineHttp *CreateEngineHttp()
{
	return new CHttpEmscripten;
}

#endif // CONF_PLATFORM_EMSCRIPTEN
