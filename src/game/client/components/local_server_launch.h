#ifndef GAME_CLIENT_COMPONENTS_LOCAL_SERVER_LAUNCH_H
#define GAME_CLIENT_COMPONENTS_LOCAL_SERVER_LAUNCH_H

#include <base/borrowed_allocator.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

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

	bool TryPrepare(const char *const *ppArguments, size_t Count, const char *pAuthCommand, bool ConnectWhenReady) noexcept
	{
		if(IsStarting() || !pAuthCommand || (Count && !ppArguments) || Count >= std::min(m_vArguments.max_size(), m_vPointers.max_size()))
			return false;
		try
		{
			CLocalServerLaunch Candidate(m_pResource);
			Candidate.m_vArguments.reserve(Count + 1);
			for(size_t i = 0; i < Count; ++i)
			{
				if(!ppArguments[i])
					return false;
				Candidate.m_vArguments.emplace_back(ppArguments[i], CBorrowedAllocator<char>(m_pResource));
			}
			Candidate.m_vArguments.emplace_back(pAuthCommand, CBorrowedAllocator<char>(m_pResource));
			Candidate.m_vPointers.reserve(Count + 1);
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

#endif
