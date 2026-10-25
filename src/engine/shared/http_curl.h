#ifndef ENGINE_SHARED_HTTP_CURL_H
#define ENGINE_SHARED_HTTP_CURL_H

#include <base/detect.h>
#include <base/thread.h>
#if !defined(CONF_PLATFORM_EMSCRIPTEN)

#include <engine/http.h>

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

class CHttpRequestCurl : public IHttpRequest
{
	friend class CHttpCurl;
	friend class CHttpCurlTestPeer;
	friend class CHttpRequestCurlTestPeer;

public:
	CHttpRequestCurl(const char *pUrl);
	~CHttpRequestCurl() override;

	void Header(const char *pNameColonValue) override;
	static std::optional<int64_t> ParseRetryAfter(std::string_view Value, int64_t Now);
	// 标准地址与域名匹配、原生旁路证据和最终连接判定分别处理。
	static bool ProxyBypassed(std::string_view Host, std::string_view List);
	static bool ProxyBypassEvidence(std::string_view Host, std::string_view List, unsigned int RuntimeVersion);
	static bool ProxyFallbackBypassed(int StatusCode, std::optional<bool> UsedProxy, bool BypassEvidence);

protected:
	virtual bool ConfigureHandle(CURL *pHandle);

private:
	curl_slist *m_pRequestHeaders = nullptr;
	curl_slist *m_pDnsBootstrap = nullptr;
	CURLSH *m_pDnsShare = nullptr;

	char m_aErr[CURL_ERROR_SIZE] = "";

	bool m_ResponseHeadersEnded = false;
	bool m_Submitted = false;
	std::string m_HostKey;

	// Abort the request if `OnHeader()` returns something other than
	// `DataSize`. `pHeader` is NOT null-terminated.
	size_t OnHeader(char *pHeader, size_t HeaderSize);

	// `pHandle` can be nullptr if no handle was ever created for this request.
	void OnCompletionInternal(CURL *pHandle, CURLcode Code);

	static int ProgressCallback(void *pUser, double DlTotal, double DlCurr, double UlTotal, double UlCurr);
	static size_t HeaderCallback(char *pData, size_t Size, size_t Number, void *pUser);
	static size_t WriteCallback(char *pData, size_t Size, size_t Number, void *pUser);
};

class CHttpCurl : public IEngineHttp
{
public:
	// Lifecycle
	explicit CHttpCurl(THREAD_CREATE pfnCreate = thread_try_init) :
		m_pfnCreate(pfnCreate) {}
	bool Init(std::chrono::milliseconds ShutdownDelay) override;
	void Shutdown() override;
	~CHttpCurl() override;

	// User
	void Run(std::shared_ptr<IHttpRequest> pRequest) override;
	bool HasIpresolveBug() const override;

private:
	friend class CHttpCurlTestPeer;
	friend class CHttpCurlSchedulerTestPeer;
	enum EState
	{
		UNINITIALIZED,
		RUNNING,
		ERROR,
	};
	enum class EPrepareResult
	{
		RUNNING,
		DEFERRED,
		CACHED,
		ABORTED,
		LOCAL_ERROR,
		SYSTEM_ERROR,
	};

	void *m_pThread = nullptr;
	THREAD_CREATE m_pfnCreate;

	std::mutex m_Lock;
	std::condition_variable m_ConditionVariableInit;
	std::atomic<EState> m_State = UNINITIALIZED;
	std::list<std::shared_ptr<CHttpRequestCurl>> m_PendingRequests;
	// 工作线程复用批次容器，避免循环内构造MSVC链表哨兵时再分配。
	std::list<std::shared_ptr<CHttpRequestCurl>> m_WorkRequests;
	std::list<std::shared_ptr<CHttpRequestCurl>> m_DeferredRequests;
	std::unordered_map<CURL *, std::shared_ptr<CHttpRequestCurl>> m_RunningRequests;
	std::chrono::milliseconds m_ShutdownDelay{};
	std::optional<std::chrono::time_point<std::chrono::steady_clock>> m_ShutdownTime;
	std::atomic<bool> m_Shutdown = false;

	// Only to be used with curl_multi_wakeup
	CURLM *m_pMultiH = nullptr;
	int m_NextTimeout;

	static void ThreadMain(void *pUser);
	void RunLoop();
	size_t RunningForHost(std::string_view Host) const;
	EPrepareResult PrepareRequest(const std::shared_ptr<CHttpRequestCurl> &pRequest);
	bool ProcessPendingRequests();
	void DrainRequests(bool Cleanup);
	void CompleteRunningRequest(CURL *pHandle, CURLcode Code, bool Cleanup = true);
	void CancelAbortedRequests();
};

#endif // !CONF_PLATFORM_EMSCRIPTEN
#endif // ENGINE_SHARED_HTTP_CURL_H
