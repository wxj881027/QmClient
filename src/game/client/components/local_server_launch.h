#ifndef GAME_CLIENT_COMPONENTS_LOCAL_SERVER_LAUNCH_H
#define GAME_CLIENT_COMPONENTS_LOCAL_SERVER_LAUNCH_H

#include <base/borrowed_allocator.h>
#include <base/types.h>

#include <game/client/components/qmclient/bounded_file_reader.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <string>
#include <vector>

inline int ReadLocalServerReportedPort(IStorage &Storage, const char *pFilename)
{
	// 服务端原子发布实际端口，只写十进制数字。
	CQmCheckedFileData Data;
	if(!Data.Read(Storage, pFilename, IStorage::TYPE_SAVE, sizeof("65535") - 1))
		return 0;

	const char *pBegin = static_cast<const char *>(Data.Data());
	const char *pEnd = pBegin + Data.Size();
	int Port = 0;
	const auto Parsed = std::from_chars(pBegin, pEnd, Port);
	if(Parsed.ec != std::errc{} || Parsed.ptr != pEnd || Port < 1 || Port > 65535)
		return 0;
	return Port;
}

// 启动请求在权限等待与异步绑定期间拥有完整参数，指针仅借用自身存储。
class CLocalServerLaunch
{
	using CString = std::basic_string<char, std::char_traits<char>, CBorrowedAllocator<char>>;
	IAllocationResource *m_pResource;
	std::vector<CString, CBorrowedAllocator<CString>> m_vArguments;
	std::vector<const char *, CBorrowedAllocator<const char *>> m_vPointers;
	enum class EState
	{
		IDLE,
		READY,
		BINDING
	};
	EState m_State = EState::IDLE;
	bool m_ConnectWhenReady = false;
	int64_t m_BindingStarted = 0;

public:
	static constexpr uint64_t BIND_TIMEOUT_NANOS = 15ULL * 1000000000;
	enum class EBindingResult
	{
		WAITING,
		CONNECTED,
		CONNECT,
		FAILED
	};
	explicit CLocalServerLaunch(IAllocationResource *pResource = nullptr) noexcept :
		m_pResource(pResource), m_vArguments(pResource), m_vPointers(pResource) {}
	CLocalServerLaunch(const CLocalServerLaunch &) = delete;
	CLocalServerLaunch &operator=(const CLocalServerLaunch &) = delete;

	bool TryPrepare(const char *const *ppArguments, size_t Count, const char *pAuthCommand, bool ConnectWhenReady, const char *pPortCommand = nullptr) noexcept
	{
		const size_t Extra = pPortCommand ? 2 : 1;
		const size_t Max = std::min(m_vArguments.max_size(), m_vPointers.max_size());
		if(IsStarting() || !pAuthCommand || (Count && !ppArguments) || Max < Extra || Count > Max - Extra)
			return false;
		try
		{
			CLocalServerLaunch Candidate(m_pResource);
			Candidate.m_vArguments.reserve(Count + Extra);
			for(size_t i = 0; i < Count; ++i)
			{
				if(!ppArguments[i])
					return false;
				Candidate.m_vArguments.emplace_back(ppArguments[i], CBorrowedAllocator<char>(m_pResource));
			}
			Candidate.m_vArguments.emplace_back(pAuthCommand, CBorrowedAllocator<char>(m_pResource));
			if(pPortCommand)
				Candidate.m_vArguments.emplace_back(pPortCommand, CBorrowedAllocator<char>(m_pResource));
			Candidate.m_vPointers.reserve(Count + Extra);
			for(const auto &Argument : Candidate.m_vArguments)
				Candidate.m_vPointers.push_back(Argument.c_str());
			m_vArguments.swap(Candidate.m_vArguments);
			m_vPointers.swap(Candidate.m_vPointers);
			m_ConnectWhenReady = ConnectWhenReady;
			m_State = EState::READY;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
	bool IsStarting() const noexcept { return m_State != EState::IDLE; }
	bool IsReady() const noexcept { return m_State == EState::READY; }
	bool IsBinding() const noexcept { return m_State == EState::BINDING; }
	bool ConnectWhenReady() const noexcept { return m_ConnectWhenReady; }
	size_t Count() const noexcept { return m_vPointers.size(); }
	const char **Data() noexcept { return m_vPointers.data(); }
	const char *const *Data() const noexcept { return m_vPointers.data(); }
	bool MarkBinding(int64_t Now) noexcept
	{
		if(!IsReady())
			return false;
		m_State = EState::BINDING;
		m_BindingStarted = Now;
		return true;
	}
	bool BindingTimedOut(int64_t Now) const noexcept
	{
		return IsBinding() && Now >= m_BindingStarted && static_cast<uint64_t>(Now) - static_cast<uint64_t>(m_BindingStarted) >= BIND_TIMEOUT_NANOS;
	}
	EBindingResult PollBinding(bool Running, int64_t Now) noexcept
	{
		if(!IsBinding())
			return EBindingResult::WAITING;
		if(Running)
		{
			const bool Connect = m_ConnectWhenReady;
			Reset();
			return Connect ? EBindingResult::CONNECT : EBindingResult::CONNECTED;
		}
		if(BindingTimedOut(Now))
		{
			Reset();
			return EBindingResult::FAILED;
		}
		return EBindingResult::WAITING;
	}
	void Reset() noexcept
	{
		m_vPointers.clear();
		m_vArguments.clear();
		m_State = EState::IDLE;
		m_ConnectWhenReady = false;
		m_BindingStarted = 0;
	}
};

// Unix 子进程在回收前独占本地服的启动位置。
class CLocalServerUnixProcess
{
public:
#if defined(CONF_FAMILY_UNIX)
	using TPid = PROCESS;
#else
	using TPid = int;
#endif

	struct SProbeResult
	{
		bool m_Exited = false;
		int m_Error = 0;
	};
	struct SPollResult
	{
		bool m_Released = false;
		int m_ProbeError = 0;
		int m_SignalError = 0;
	};

	CLocalServerUnixProcess() = default;
	CLocalServerUnixProcess(const CLocalServerUnixProcess &) = delete;
	CLocalServerUnixProcess &operator=(const CLocalServerUnixProcess &) = delete;

	TPid Pid() const { return m_Pid; }
	bool HasProcess() const { return m_Pid > 0; }
	bool IsStopping() const { return m_State != EState::RUNNING; }

	template<typename FSpawn>
	bool TryStart(FSpawn &&Spawn)
	{
		if(HasProcess())
			return false;
		const TPid Pid = Spawn();
		if(Pid <= 0)
			return false;
		m_Pid = Pid;
		return true;
	}

	void RequestStop()
	{
		if(HasProcess() && m_State != EState::WAITING_FOR_REAP)
			m_State = EState::STOP_REQUESTED;
	}

	template<typename FProbe, typename FTerminate>
	SPollResult Poll(FProbe &&Probe, FTerminate &&Terminate)
	{
		if(!HasProcess())
			return {};

		const SProbeResult Result = Probe(m_Pid);
		if(Result.m_Exited || Result.m_Error == ECHILD)
		{
			m_Pid = 0;
			m_State = EState::RUNNING;
			m_LastProbeError = 0;
			return {.m_Released = true};
		}
		if(Result.m_Error != 0)
		{
			// 探测失败时保留 PID，等下次轮询确认子进程归属后再发信号。
			if(Result.m_Error == EINTR || Result.m_Error == m_LastProbeError)
				return {};
			m_LastProbeError = Result.m_Error;
			return {.m_ProbeError = Result.m_Error};
		}

		m_LastProbeError = 0;
		if(m_State != EState::STOP_REQUESTED)
			return {};

		const int SignalError = Terminate(m_Pid);
		if(SignalError == 0 || SignalError == ESRCH)
		{
			// 服务端收到第二次 SIGTERM 会直接退出。ESRCH 也要等 waitpid 确认。
			m_State = EState::WAITING_FOR_REAP;
			return {};
		}

		// 保持停止中。下一次显式停服可以重试，轮询只负责回收进程。
		m_State = EState::SIGNAL_FAILED;
		return {.m_SignalError = SignalError};
	}

private:
	enum class EState
	{
		RUNNING,
		STOP_REQUESTED,
		WAITING_FOR_REAP,
		SIGNAL_FAILED,
	};

	TPid m_Pid = 0;
	EState m_State = EState::RUNNING;
	int m_LastProbeError = 0;
};

#endif
