#include "qm_netease_hook_v5_writer.h"

#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace QmNeteaseHook
{
	namespace
	{
#if defined(_WIN32)
		bool AcquireWriterMutex(HANDLE hMutex)
		{
			if(hMutex == nullptr)
				return false;
			const DWORD Result = WaitForSingleObject(hMutex, 1000);
			// A crashed writer leaves the named mutex abandoned. The mapping is
			// still valid; the next writer must be allowed to repair its sequence.
			return Result == WAIT_OBJECT_0 || Result == WAIT_ABANDONED;
		}

		// 只拥有本次取得的锁，不关闭 writer 持有的原生句柄。
		class CWriterMutexLock
		{
			HANDLE m_hMutex;

		public:
			explicit CWriterMutexLock(HANDLE hMutex) :
				m_hMutex(AcquireWriterMutex(hMutex) ? hMutex : nullptr) {}
			~CWriterMutexLock()
			{
				if(m_hMutex)
					ReleaseMutex(m_hMutex);
			}
			explicit operator bool() const { return m_hMutex != nullptr; }
			CWriterMutexLock(const CWriterMutexLock &) = delete;
			CWriterMutexLock &operator=(const CWriterMutexLock &) = delete;
		};
#endif
	}

	struct CV5Writer::SImpl
	{
#if defined(_WIN32)
		HANDLE m_hMapping = nullptr;
		HANDLE m_hWriterMutex = nullptr;
		SSharedBlockV5 *m_pBlock = nullptr;
#endif
		uint64_t m_NextSequence = 2;
		bool m_MappingOwner = false;
	};

	CV5Writer::CV5Writer() :
		m_pImpl(std::make_unique<SImpl>())
	{
	}

	CV5Writer::~CV5Writer()
	{
		Close();
	}

	bool CV5Writer::Open(bool PreferExisting)
	{
		if(!m_pImpl)
			return false;
		if(IsOpen())
			return true;
#if defined(_WIN32)
		bool CreatedMapping = false;
		if(PreferExisting)
		{
			m_pImpl->m_hMapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, PROTOCOL_MAPPING_NAME_V5_W);
			if(m_pImpl->m_hMapping == nullptr)
			{
				m_pImpl->m_hMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)sizeof(SSharedBlockV5), PROTOCOL_MAPPING_NAME_V5_W);
				CreatedMapping = m_pImpl->m_hMapping != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
			}
		}
		else
		{
			m_pImpl->m_hMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)sizeof(SSharedBlockV5), PROTOCOL_MAPPING_NAME_V5_W);
			CreatedMapping = m_pImpl->m_hMapping != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
		}
		if(m_pImpl->m_hMapping == nullptr)
			return false;
		m_pImpl->m_pBlock = static_cast<SSharedBlockV5 *>(MapViewOfFile(m_pImpl->m_hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SSharedBlockV5)));
		if(m_pImpl->m_pBlock == nullptr)
		{
			CloseHandle(m_pImpl->m_hMapping);
			m_pImpl->m_hMapping = nullptr;
			return false;
		}
		m_pImpl->m_hWriterMutex = CreateMutexW(nullptr, FALSE, PROTOCOL_WRITER_MUTEX_NAME_V5_W);
		if(m_pImpl->m_hWriterMutex == nullptr)
		{
			UnmapViewOfFile(m_pImpl->m_pBlock);
			m_pImpl->m_pBlock = nullptr;
			CloseHandle(m_pImpl->m_hMapping);
			m_pImpl->m_hMapping = nullptr;
			return false;
		}
		m_pImpl->m_MappingOwner = CreatedMapping;
		// 新建页面文件映射已由系统清零，不能再覆盖其他 writer 可能已发布的载荷。
		const uint64_t ExistingSequence = m_pImpl->m_pBlock->m_Sequence;
		m_pImpl->m_NextSequence = ExistingSequence >= 2 && (ExistingSequence % 2) == 0 ? ExistingSequence + 2 : 2;
		return true;
#else
		(void)PreferExisting;
		return false;
#endif
	}

	void CV5Writer::Close()
	{
		if(!m_pImpl)
			return;
#if defined(_WIN32)
		if(m_pImpl->m_pBlock != nullptr)
		{
			UnmapViewOfFile(m_pImpl->m_pBlock);
			m_pImpl->m_pBlock = nullptr;
		}
		if(m_pImpl->m_hMapping != nullptr)
		{
			CloseHandle(m_pImpl->m_hMapping);
			m_pImpl->m_hMapping = nullptr;
		}
		if(m_pImpl->m_hWriterMutex != nullptr)
		{
			CloseHandle(m_pImpl->m_hWriterMutex);
			m_pImpl->m_hWriterMutex = nullptr;
		}
#endif
		m_pImpl->m_MappingOwner = false;
		m_pImpl->m_NextSequence = 2;
	}

	bool CV5Writer::IsOpen() const
	{
#if defined(_WIN32)
		return m_pImpl != nullptr && m_pImpl->m_pBlock != nullptr;
#else
		return false;
#endif
	}

	bool CV5Writer::IsMappingOwner() const
	{
		return m_pImpl != nullptr && m_pImpl->m_MappingOwner;
	}

	bool CV5Writer::Publish(SSnapshotV5 Snapshot)
	{
		if(!m_pImpl || !IsOpen())
			return false;
#if defined(_WIN32)
		const CWriterMutexLock Lock(m_pImpl->m_hWriterMutex);
		if(!Lock)
			return false;
		const uint64_t CurrentSequence = m_pImpl->m_pBlock->m_Sequence;
		const uint64_t Sequence = CurrentSequence >= 2 && (CurrentSequence & 1) == 0 && CurrentSequence < UINT64_MAX - 2 ? CurrentSequence + 2 : 2;
		m_pImpl->m_NextSequence = Sequence + 2;
		Snapshot.m_Sequence = Sequence;
		FinalizeSnapshotV5(&Snapshot);
		InterlockedExchange64(reinterpret_cast<volatile LONG64 *>(&m_pImpl->m_pBlock->m_Sequence), (LONG64)(Sequence - 1));
		MemoryBarrier();
		std::memcpy(&m_pImpl->m_pBlock->m_Snapshot, &Snapshot, sizeof(Snapshot));
		MemoryBarrier();
		InterlockedExchange64(reinterpret_cast<volatile LONG64 *>(&m_pImpl->m_pBlock->m_Sequence), (LONG64)Sequence);
		return true;
#else
		(void)Snapshot;
		return false;
#endif
	}

	bool CV5Writer::PublishFallback(SSnapshotV5 Snapshot, uint64_t NowTick, uint64_t TimeoutMs)
	{
		if(!m_pImpl || !IsOpen())
			return false;
#if defined(_WIN32)
		const CWriterMutexLock Lock(m_pImpl->m_hWriterMutex);
		if(!Lock)
			return false;

		// Keep the source check and the write under one mutex. Seqlock protects
		// readers, while this mutex serializes the Helper and GDI writers.
		SSnapshotV5 Existing{};
		const uint64_t ExistingSequence = m_pImpl->m_pBlock->m_Sequence;
		MemoryBarrier();
		const bool ExistingStable = ExistingSequence != 0 && (ExistingSequence & 1) == 0;
		if(ExistingStable)
		{
			std::memcpy(&Existing, &m_pImpl->m_pBlock->m_Snapshot, sizeof(Existing));
			MemoryBarrier();
		}
		const bool ExistingFresh = ExistingStable && Existing.m_Sequence == ExistingSequence && ValidateSnapshotV5(Existing) && !IsStaleV5(Existing, NowTick, TimeoutMs);
		if(ExistingFresh && !CanPublishDesktopFallbackV5(Existing, Snapshot, NowTick, TimeoutMs))
		{
			return false;
		}

		const uint64_t CurrentSequence = m_pImpl->m_pBlock->m_Sequence;
		const uint64_t Sequence = CurrentSequence >= 2 && (CurrentSequence & 1) == 0 && CurrentSequence < UINT64_MAX - 2 ? CurrentSequence + 2 : 2;
		m_pImpl->m_NextSequence = Sequence + 2;
		Snapshot.m_Sequence = Sequence;
		FinalizeSnapshotV5(&Snapshot);
		InterlockedExchange64(reinterpret_cast<volatile LONG64 *>(&m_pImpl->m_pBlock->m_Sequence), (LONG64)(Sequence - 1));
		MemoryBarrier();
		std::memcpy(&m_pImpl->m_pBlock->m_Snapshot, &Snapshot, sizeof(Snapshot));
		MemoryBarrier();
		InterlockedExchange64(reinterpret_cast<volatile LONG64 *>(&m_pImpl->m_pBlock->m_Sequence), (LONG64)Sequence);
		return true;
#else
		(void)Snapshot;
		(void)NowTick;
		(void)TimeoutMs;
		return false;
#endif
	}

	bool CV5Writer::Read(SSnapshotV5 *pSnapshot) const
	{
		if(!m_pImpl || pSnapshot == nullptr || !IsOpen())
			return false;
#if defined(_WIN32)
		const CWriterMutexLock Lock(m_pImpl->m_hWriterMutex);
		if(!Lock)
			return false;
		for(int Attempt = 0; Attempt < 3; ++Attempt)
		{
			const uint64_t Begin = m_pImpl->m_pBlock->m_Sequence;
			MemoryBarrier();
			if(Begin == 0 || (Begin & 1) != 0)
				continue;
			SSnapshotV5 Candidate{};
			std::memcpy(&Candidate, &m_pImpl->m_pBlock->m_Snapshot, sizeof(Candidate));
			MemoryBarrier();
			const uint64_t End = m_pImpl->m_pBlock->m_Sequence;
			if(!IsStableSequenceV5(Begin, End) || Candidate.m_Sequence != End || !ValidateSnapshotV5(Candidate))
				continue;
			*pSnapshot = Candidate;
			return true;
		}
		return false;
#else
		return false;
#endif
	}
}
