#ifndef ENGINE_SHARED_HTTP_EMSCRIPTEN_H
#define ENGINE_SHARED_HTTP_EMSCRIPTEN_H

#include <base/detect.h>
#if defined(CONF_PLATFORM_EMSCRIPTEN)

#include <engine/http.h>
#include <engine/shared/http_request_completion.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

typedef struct emscripten_fetch_t emscripten_fetch_t;

class CHttpEmscripten;

class CHttpRequestEmscripten : public IHttpRequest
{
	friend CHttpEmscripten;

public:
	CHttpRequestEmscripten(const char *pUrl);
	~CHttpRequestEmscripten() override;

	void Header(const char *pNameColonValue) override;

	void Abort() override;

private:
	std::atomic<CHttpEmscripten *> m_pHttp = nullptr;
	bool m_Submitted = false;
	CHttpRequestCompletion m_Completion;

	std::vector<std::pair<std::string, std::string>> m_vRequestHeaders;

	emscripten_fetch_t *m_pFetch = nullptr;
	bool m_CallbackFinished = false;

	bool ConfigureAndRun();
	void OnSuccess(emscripten_fetch_t *pFetch);
	void OnFailure(emscripten_fetch_t *pFetch);
	void OnProgress(emscripten_fetch_t *pFetch);
	void OnHeader(const char *pName, const char *pValue);
	void OnCompletionInternal(EHttpState State, const char *pErrorDetail);

	static void FetchCallbackSuccess(emscripten_fetch_t *pFetch);
	static void FetchCallbackFailure(emscripten_fetch_t *pFetch);
	static void FetchCallbackProgress(emscripten_fetch_t *pFetch);
};

class CHttpEmscripten : public IEngineHttp
{
	friend CHttpRequestEmscripten;

public:
	// Lifecycle
	bool Init(std::chrono::milliseconds ShutdownDelay) override;
	void Shutdown() override;
	~CHttpEmscripten();

	// User
	void Run(std::shared_ptr<IHttpRequest> pRequest) override;
	bool HasIpresolveBug() const override;

private:
	void *m_pThread = nullptr;

	std::mutex m_Lock;
	std::condition_variable m_ConditionVariableInit;
	std::condition_variable m_ConditionVariableLoop;
	std::atomic<bool> m_Initialized = false;
	std::vector<std::shared_ptr<CHttpRequestEmscripten>> m_PendingRequests;
	bool m_HasPendingStateChanges = false;
	std::unordered_map<CHttpRequestEmscripten *, std::shared_ptr<CHttpRequestEmscripten>> m_RunningRequests;
	std::chrono::milliseconds m_ShutdownDelay{};
	std::optional<std::chrono::time_point<std::chrono::steady_clock>> m_ShutdownTime;
	std::atomic<bool> m_Shutdown = false;
	std::atomic<bool> m_StartedShutdown = false;

	static void ThreadMain(void *pUser);
	void RunLoop();
	void AddPendingStateChange(CHttpRequestEmscripten &Request, EHttpState State);
};

#endif // CONF_PLATFORM_EMSCRIPTEN
#endif // ENGINE_SHARED_HTTP_EMSCRIPTEN_H
