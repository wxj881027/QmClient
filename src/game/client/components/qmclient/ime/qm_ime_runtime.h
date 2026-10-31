#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_RUNTIME_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_RUNTIME_H

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <thread>

// Rime 外部边界。通知状态属于 runtime，不能借用 engine 或输入框。
class CQmImeRuntime
{
public:
	virtual ~CQmImeRuntime() = default;
	void OnNotification(const char *pType, const char *pValue);
	bool DeploymentFailed() const { return m_DeployFailed.load(); }
	virtual bool Initialized() const = 0;
	virtual void JoinMaintenance() = 0;
	virtual void DestroySession() = 0;
	virtual void ClearNotificationHandler() = 0;
	virtual void Finalize() = 0;
	virtual void UnloadLibrary() = 0;

private:
	std::atomic<bool> m_DeployFailed{false};
};

// 在 initialize/start_maintenance 之前准备清理线程；Close 只移交完整所有权。
// 同一进程只有一个 Rime service，旧 runtime 回收完成前不能重新初始化。
class CQmImeRuntimeShutdown
{
public:
	enum class EResult
	{
		CLOSED,
		FAILED,
	};

	// 替换线程创建边界；失败时应抛出异常或返回不可 join 的线程，不得启动隐藏任务。
	using FStartWorker = std::thread (*)(std::function<void()> Task);
	static std::unique_ptr<CQmImeRuntimeShutdown> Prepare(FStartWorker StartWorker = nullptr);
	~CQmImeRuntimeShutdown();
	CQmImeRuntimeShutdown(const CQmImeRuntimeShutdown &) = delete;
	CQmImeRuntimeShutdown &operator=(const CQmImeRuntimeShutdown &) = delete;

	void Close(std::unique_ptr<CQmImeRuntime> pRuntime = nullptr);
	// 观察票据不持有 runtime；前台 owner 销毁后仍可有界等待回收结果。
	std::shared_future<EResult> Completion() const;

private:
	struct SState;
	explicit CQmImeRuntimeShutdown(std::shared_ptr<SState> pState);
	static void Run(const std::shared_ptr<SState> &pState);
	std::shared_ptr<SState> m_pState;
};

#endif
