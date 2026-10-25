#include "qm_netease_lyrics_channel.h"

#include <cstring>
#include <limits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace QmNeteaseHook
{
	namespace
	{
#if defined(_WIN32)
		struct CMutexRelease
		{
			HANDLE m_Handle;
			~CMutexRelease() { ReleaseMutex(m_Handle); }
		};
		constexpr uint32_t MAGIC = 0x514d4c31;
		constexpr size_t MAX_BYTES = 512 * 1024;
		struct SLyricsBlock
		{
			uint32_t m_Magic;
			uint32_t m_Pid;
			uint64_t m_SongId;
			uint64_t m_Generation;
			uint64_t m_Revision;
			uint32_t m_LrcBytes;
			uint32_t m_YrcBytes;
			uint32_t m_TranslationBytes;
			char m_aData[MAX_BYTES];
		};
#endif
	}

	struct CLyricsChannel::SImpl
	{
#if defined(_WIN32)
		HANDLE m_hMapping = nullptr;
		HANDLE m_hMutex = nullptr;
		SLyricsBlock *m_pBlock = nullptr;
#endif
		bool m_Writer = false;
		std::wstring m_Endpoint;
		std::wstring m_MutexName;
	};

	CLyricsChannel::CLyricsChannel(std::wstring Endpoint) :
		m_pImpl(std::make_unique<SImpl>())
	{
		m_pImpl->m_Endpoint = std::move(Endpoint);
		m_pImpl->m_MutexName = m_pImpl->m_Endpoint + L".Lock";
	}
	CLyricsChannel::~CLyricsChannel() { Close(); }

	void CLyricsChannel::Close()
	{
#if defined(_WIN32)
		if(m_pImpl->m_pBlock)
			UnmapViewOfFile(m_pImpl->m_pBlock);
		if(m_pImpl->m_hMapping)
			CloseHandle(m_pImpl->m_hMapping);
		if(m_pImpl->m_hMutex)
			CloseHandle(m_pImpl->m_hMutex);
		m_pImpl->m_hMapping = nullptr;
		m_pImpl->m_hMutex = nullptr;
		m_pImpl->m_pBlock = nullptr;
#endif
		m_pImpl->m_Writer = false;
	}

	bool CLyricsChannel::Open(bool Writer)
	{
#if defined(_WIN32)
		if(m_pImpl->m_pBlock && m_pImpl->m_Writer == Writer)
			return true;
		Close();
		m_pImpl->m_Writer = Writer;
		m_pImpl->m_hMapping = Writer ?
					      CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(SLyricsBlock)), m_pImpl->m_Endpoint.c_str()) :
					      OpenFileMappingW(FILE_MAP_READ, FALSE, m_pImpl->m_Endpoint.c_str());
		if(m_pImpl->m_hMapping)
			m_pImpl->m_pBlock = static_cast<SLyricsBlock *>(MapViewOfFile(m_pImpl->m_hMapping,
				Writer ? FILE_MAP_WRITE | FILE_MAP_READ : FILE_MAP_READ, 0, 0, sizeof(SLyricsBlock)));
		m_pImpl->m_hMutex = Writer ? CreateMutexW(nullptr, FALSE, m_pImpl->m_MutexName.c_str()) :
					     OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, m_pImpl->m_MutexName.c_str());
		if(m_pImpl->m_pBlock && m_pImpl->m_hMutex)
			return true;
		Close();
#else
		(void)Writer;
#endif
		return false;
	}

	bool CLyricsChannel::Publish(uint32_t Pid, uint64_t SongId, uint64_t Generation, const NeteaseLyrics::SRawLyrics &Raw)
	{
#if defined(_WIN32)
		if(Raw.m_Lrc.size() > MAX_BYTES || Raw.m_Yrc.size() > MAX_BYTES || Raw.m_TranslationLrc.size() > MAX_BYTES ||
			Raw.m_Lrc.size() + Raw.m_Yrc.size() + Raw.m_TranslationLrc.size() > MAX_BYTES || !Open(true))
			return false;
		const DWORD Wait = WaitForSingleObject(m_pImpl->m_hMutex, 0);
		if(Wait != WAIT_OBJECT_0 && Wait != WAIT_ABANDONED)
			return false;
		const CMutexRelease Release{m_pImpl->m_hMutex};
		SLyricsBlock &Block = *m_pImpl->m_pBlock;
		const uint64_t Revision = Block.m_Magic == MAGIC && Block.m_Revision < std::numeric_limits<uint64_t>::max() ? Block.m_Revision + 1 : 1;
		Block.m_Revision = 0;
		Block.m_Magic = MAGIC;
		Block.m_Pid = Pid;
		Block.m_SongId = SongId;
		Block.m_Generation = Generation;
		Block.m_LrcBytes = static_cast<uint32_t>(Raw.m_Lrc.size());
		Block.m_YrcBytes = static_cast<uint32_t>(Raw.m_Yrc.size());
		Block.m_TranslationBytes = static_cast<uint32_t>(Raw.m_TranslationLrc.size());
		std::memcpy(Block.m_aData, Raw.m_Lrc.data(), Raw.m_Lrc.size());
		std::memcpy(Block.m_aData + Block.m_LrcBytes, Raw.m_Yrc.data(), Raw.m_Yrc.size());
		std::memcpy(Block.m_aData + Block.m_LrcBytes + Block.m_YrcBytes, Raw.m_TranslationLrc.data(), Raw.m_TranslationLrc.size());
		Block.m_Revision = Revision;
		return true;
#else
		(void)Pid;
		(void)SongId;
		(void)Generation;
		(void)Raw;
		return false;
#endif
	}

	bool CLyricsChannel::Read(uint32_t Pid, uint64_t SongId, uint64_t Generation, uint64_t &Revision, NeteaseLyrics::SRawLyrics &Raw)
	{
#if defined(_WIN32)
		if(!Open(false))
			return false;
		const DWORD Wait = WaitForSingleObject(m_pImpl->m_hMutex, 0);
		if(Wait != WAIT_OBJECT_0)
		{
			if(Wait == WAIT_ABANDONED)
				ReleaseMutex(m_pImpl->m_hMutex);
			return false;
		}
		const CMutexRelease Release{m_pImpl->m_hMutex};
		const SLyricsBlock &Block = *m_pImpl->m_pBlock;
		const uint64_t Total = uint64_t(Block.m_LrcBytes) + Block.m_YrcBytes + Block.m_TranslationBytes;
		const bool Changed = Block.m_Magic == MAGIC && Block.m_Revision != 0 && Block.m_Revision != Revision &&
				     Block.m_Pid == Pid && Block.m_SongId == SongId && Block.m_Generation == Generation && Total <= MAX_BYTES;
		if(Changed)
		{
			Raw.m_Lrc.assign(Block.m_aData, Block.m_LrcBytes);
			Raw.m_Yrc.assign(Block.m_aData + Block.m_LrcBytes, Block.m_YrcBytes);
			Raw.m_TranslationLrc.assign(Block.m_aData + Block.m_LrcBytes + Block.m_YrcBytes, Block.m_TranslationBytes);
			Revision = Block.m_Revision;
		}
		return Changed;
#else
		(void)Pid;
		(void)SongId;
		(void)Generation;
		(void)Revision;
		(void)Raw;
		return false;
#endif
	}
}
