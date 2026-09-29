#include "qm_soda_writer.h"

#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace QmSodaHook
{
	namespace
	{
#if defined(_WIN32)
		bool AcquireWriterMutex(HANDLE hMutex)
		{
			if(hMutex == nullptr)
				return false;
			const DWORD Result = WaitForSingleObject(hMutex, 1000);
			// 崩溃的 writer 会留下 abandoned 命名互斥体;映射仍然有效,下一个 writer 必须允许修复序列号。
			return Result == WAIT_OBJECT_0 || Result == WAIT_ABANDONED;
		}
#endif
	}

	struct CSodaWriter::SImpl
	{
#if defined(_WIN32)
		HANDLE m_hMapping = nullptr;
		HANDLE m_hWriterMutex = nullptr;
		SSharedBlock *m_pBlock = nullptr;
#endif
		bool m_MappingOwner = false;
	};

	CSodaWriter::CSodaWriter() :
		m_pImpl(std::make_unique<SImpl>()) {}

	CSodaWriter::~CSodaWriter() { Close(); }

	bool CSodaWriter::Open(bool PreferExisting, const wchar_t *pMappingName, const wchar_t *pWriterMutexName)
	{
		if(!m_pImpl)
			return false;
		if(IsOpen())
			return true;
#if defined(_WIN32)
		bool CreatedMapping = false;
		if(PreferExisting)
		{
			m_pImpl->m_hMapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, pMappingName);
			if(m_pImpl->m_hMapping == nullptr)
			{
				m_pImpl->m_hMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)sizeof(SSharedBlock), pMappingName);
				CreatedMapping = m_pImpl->m_hMapping != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
			}
		}
		else
		{
			m_pImpl->m_hMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)sizeof(SSharedBlock), pMappingName);
			CreatedMapping = m_pImpl->m_hMapping != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
		}
		if(m_pImpl->m_hMapping == nullptr)
			return false;
		m_pImpl->m_pBlock = static_cast<SSharedBlock *>(MapViewOfFile(m_pImpl->m_hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SSharedBlock)));
		if(m_pImpl->m_pBlock == nullptr)
		{
			CloseHandle(m_pImpl->m_hMapping);
			m_pImpl->m_hMapping = nullptr;
			return false;
		}
		m_pImpl->m_hWriterMutex = CreateMutexW(nullptr, FALSE, pWriterMutexName);
		if(m_pImpl->m_hWriterMutex == nullptr)
		{
			UnmapViewOfFile(m_pImpl->m_pBlock);
			m_pImpl->m_pBlock = nullptr;
			CloseHandle(m_pImpl->m_hMapping);
			m_pImpl->m_hMapping = nullptr;
			return false;
		}
		m_pImpl->m_MappingOwner = CreatedMapping;
		if(m_pImpl->m_MappingOwner)
			std::memset(m_pImpl->m_pBlock, 0, sizeof(*m_pImpl->m_pBlock));
		return true;
#else
		(void)PreferExisting;
		(void)pMappingName;
		(void)pWriterMutexName;
		return false;
#endif
	}

	void CSodaWriter::Close()
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
	}

	bool CSodaWriter::IsOpen() const
	{
#if defined(_WIN32)
		return m_pImpl != nullptr && m_pImpl->m_pBlock != nullptr;
#else
		return false;
#endif
	}

	bool CSodaWriter::Publish(SSnapshot Snapshot)
	{
		if(!m_pImpl || !IsOpen())
			return false;
#if defined(_WIN32)
		if(!AcquireWriterMutex(m_pImpl->m_hWriterMutex))
			return false;
		const uint64_t CurrentSequence = m_pImpl->m_pBlock->m_Sequence;
		const uint64_t Sequence = CurrentSequence >= 2 && (CurrentSequence & 1) == 0 && CurrentSequence < UINT64_MAX - 2 ? CurrentSequence + 2 : 2;
		Snapshot.m_Sequence = Sequence;
		FinalizeSnapshot(&Snapshot);
		InterlockedExchange64(reinterpret_cast<volatile LONG64 *>(&m_pImpl->m_pBlock->m_Sequence), (LONG64)(Sequence - 1));
		MemoryBarrier();
		std::memcpy(&m_pImpl->m_pBlock->m_Snapshot, &Snapshot, sizeof(Snapshot));
		MemoryBarrier();
		InterlockedExchange64(reinterpret_cast<volatile LONG64 *>(&m_pImpl->m_pBlock->m_Sequence), (LONG64)Sequence);
		ReleaseMutex(m_pImpl->m_hWriterMutex);
		return true;
#else
		(void)Snapshot;
		return false;
#endif
	}
} // namespace QmSodaHook
