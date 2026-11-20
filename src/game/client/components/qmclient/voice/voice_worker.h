#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_WORKER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_WORKER_H

#include <base/str.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <thread>
#include <utility>

class CVoiceWorker
{
public:
	enum class EFailure
	{
		NONE,
		START,
		RUN,
	};

private:
	std::thread m_Thread;
	std::atomic<bool> m_Stop{false};
	std::atomic<EFailure> m_Failure{EFailure::NONE};
	char m_aError[256] = {};
	int64_t m_RetryAfter = 0;

	void Fail(EFailure Failure, const char *pError)
	{
		// 固定缓冲避免处理分配失败时再次分配；写完消息后再发布失败状态。
		str_copy(m_aError, pError);
		m_Failure.store(Failure);
	}

public:
	~CVoiceWorker() { Stop(); }

	bool StopRequested() const { return m_Stop.load(); }
	bool HasFailed() const { return m_Failure.load() != EFailure::NONE; }
	// 除 StopRequested/HasFailed 外，生命周期接口仅由 owner 线程调用。
	bool HasThread() const { return m_Thread.joinable(); }
	bool CanStart(int64_t Now) const { return !HasFailed() && Now >= m_RetryAfter; }

	template<typename TRun, typename TLaunch>
	bool Start(int64_t Now, TRun Run, TLaunch Launch)
	{
		if(!CanStart(Now))
			return false;
		if(HasThread())
			return true;
		m_Stop.store(false);
		try
		{
			m_Thread = Launch([this, Run = std::move(Run)] {
				try
				{
					Run();
				}
				catch(const std::exception &Error)
				{
					Fail(EFailure::RUN, Error.what());
				}
				catch(...)
				{
					Fail(EFailure::RUN, "Unknown exception");
				}
			});
			return true;
		}
		catch(const std::exception &Error)
		{
			Fail(EFailure::START, Error.what());
		}
		catch(...)
		{
			Fail(EFailure::START, "Unknown exception");
		}
		return false;
	}

	template<typename TRun>
	bool Start(int64_t Now, TRun Run)
	{
		return Start(Now, std::move(Run), [](auto Body) { return std::thread(std::move(Body)); });
	}

	void Stop()
	{
		m_Stop.store(true);
		if(HasThread())
			m_Thread.join();
		m_Stop.store(false);
	}

	template<typename TCleanup>
	bool HandleFailure(int64_t Now, int64_t RetryDelay, TCleanup Cleanup)
	{
		const EFailure Failure = m_Failure.load();
		if(Failure == EFailure::NONE)
			return false;
		// 先等待旧线程退出，再由 owner 清理设备、编码器和连接。
		Stop();
		Cleanup(Failure, m_aError);
		m_RetryAfter = Now + RetryDelay;
		m_Failure.store(EFailure::NONE);
		return true;
	}
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_WORKER_H
