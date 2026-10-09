#include "netease_shared_memory.h"

#include <atomic>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace NeteaseLyrics
{
	namespace
	{
		uint64_t LoadSequence(const volatile uint64_t *pSequence)
		{
			if(pSequence == nullptr)
				return 0;
#if defined(_WIN32)
			// 映射只读，禁止用带写入语义的 compare-exchange；序列、载荷序列和校验和共同拒绝不稳定快照。
			const uint64_t Value = *pSequence;
			MemoryBarrier();
			return Value;
#else
			const uint64_t Value = *pSequence;
			std::atomic_thread_fence(std::memory_order_acquire);
			return Value;
#endif
		}

		void ReadPayload(const volatile QmNeteaseHook::SSnapshotV5 *pSource, QmNeteaseHook::SSnapshotV5 *pDestination)
		{
			std::memcpy(pDestination, (const void *)pSource, sizeof(*pDestination));
#if defined(_WIN32)
			MemoryBarrier();
#else
			std::atomic_thread_fence(std::memory_order_acquire);
#endif
		}
	}

	bool ReadStableV5(const volatile QmNeteaseHook::SSharedBlockV5 &Shared, QmNeteaseHook::SSnapshotV5 *pOut, int MaxAttempts)
	{
		if(pOut == nullptr)
			return false;
		*pOut = {};
		for(int Attempt = 0; Attempt < (MaxAttempts > 0 ? MaxAttempts : 1); ++Attempt)
		{
			const uint64_t Begin = LoadSequence(&Shared.m_Sequence);
			if(Begin == 0 || (Begin & 1) != 0)
				continue;
			QmNeteaseHook::SSnapshotV5 Candidate{};
			ReadPayload(&Shared.m_Snapshot, &Candidate);
			const uint64_t End = LoadSequence(&Shared.m_Sequence);
			if(!QmNeteaseHook::IsStableSequenceV5(Begin, End) || Candidate.m_Sequence != End)
				continue;
			if(!QmNeteaseHook::ValidateSnapshotV5(Candidate))
				continue;
			*pOut = Candidate;
			return true;
		}
		return false;
	}

	struct CV5SharedMemoryReader::SImpl
	{
#if defined(_WIN32)
		HANDLE m_hMapping = nullptr;
		volatile QmNeteaseHook::SSharedBlockV5 *m_pView = nullptr;
#endif
		uint64_t m_LastSequence = 0;
		uint32_t m_LastPid = 0;
	};

	CV5SharedMemoryReader::CV5SharedMemoryReader() :
		m_pImpl(std::make_unique<SImpl>()) {}

	CV5SharedMemoryReader::~CV5SharedMemoryReader()
	{
		Close();
	}

	bool CV5SharedMemoryReader::Open()
	{
		if(!m_pImpl)
			return false;
#if defined(_WIN32)
		if(m_pImpl->m_pView != nullptr)
			return true;
		const std::wstring Name = [] {
			std::wstring Result;
			const char *pName = QmNeteaseHook::PROTOCOL_MAPPING_NAME_V5;
			while(*pName != '\0')
				Result.push_back((wchar_t)(unsigned char)*pName++);
			return Result;
		}();
		m_pImpl->m_hMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, Name.c_str());
		if(m_pImpl->m_hMapping == nullptr)
			return false;
		m_pImpl->m_pView = static_cast<volatile QmNeteaseHook::SSharedBlockV5 *>(MapViewOfFile(m_pImpl->m_hMapping, FILE_MAP_READ, 0, 0, sizeof(QmNeteaseHook::SSharedBlockV5)));
		if(m_pImpl->m_pView == nullptr)
		{
			CloseHandle(m_pImpl->m_hMapping);
			m_pImpl->m_hMapping = nullptr;
			return false;
		}
		return true;
#else
		return false;
#endif
	}

	void CV5SharedMemoryReader::Close()
	{
		if(!m_pImpl)
			return;
#if defined(_WIN32)
		if(m_pImpl->m_pView != nullptr)
		{
			UnmapViewOfFile((const void *)m_pImpl->m_pView);
			m_pImpl->m_pView = nullptr;
		}
		if(m_pImpl->m_hMapping != nullptr)
		{
			CloseHandle(m_pImpl->m_hMapping);
			m_pImpl->m_hMapping = nullptr;
		}
#endif
		m_pImpl->m_LastSequence = 0;
		m_pImpl->m_LastPid = 0;
	}

	bool CV5SharedMemoryReader::Read(QmNeteaseHook::SSnapshotV5 *pOut, uint64_t NowTick, uint64_t TimeoutMs)
	{
		if(pOut == nullptr || !m_pImpl || !Open())
			return false;
#if defined(_WIN32)
		QmNeteaseHook::SSnapshotV5 Candidate{};
		if(!ReadStableV5(*m_pImpl->m_pView, &Candidate))
			return false;
		if(QmNeteaseHook::IsStaleV5(Candidate, NowTick, TimeoutMs))
			return false;
		if(m_pImpl->m_LastPid != 0 && Candidate.m_CloudMusicPid != m_pImpl->m_LastPid)
			m_pImpl->m_LastSequence = 0;
		if(m_pImpl->m_LastSequence != 0 && Candidate.m_Sequence < m_pImpl->m_LastSequence)
			m_pImpl->m_LastSequence = 0;
		m_pImpl->m_LastPid = Candidate.m_CloudMusicPid;
		m_pImpl->m_LastSequence = Candidate.m_Sequence;
		*pOut = Candidate;
		return true;
#else
		(void)NowTick;
		(void)TimeoutMs;
		return false;
#endif
	}

	bool CV5SharedMemoryReader::IsOpen() const
	{
#if defined(_WIN32)
		return m_pImpl != nullptr && m_pImpl->m_pView != nullptr;
#else
		return false;
#endif
	}
} // namespace NeteaseLyrics
