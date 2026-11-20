#ifndef QM_MUSIC_QM_QQMUSIC_LYRIC_REQUEST_H
#define QM_MUSIC_QM_QQMUSIC_LYRIC_REQUEST_H

#include "qm_qqmusic_protocol.h"

#include <chrono>
#include <cstdint>
#include <future>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace QmMusicHook::QQMusic
{
	struct SLyricFetchResult
	{
		bool m_Success = false;
		SLyrics m_Lyrics;
	};

	class CLyricRequest
	{
		static constexpr int MAX_ATTEMPTS = 3;
		static constexpr int RETRY_STEP_MS = 2000;

		std::future<SLyricFetchResult> m_Pending;
		uint64_t m_Generation = 0;
		uint64_t m_PendingGeneration = 0;
		int m_Attempts = 0;
		int64_t m_NextFetch = 0;
		SLyricFetchResult m_Result;

		template<typename TStart>
		void StartRequest(TStart &Start)
		try
		{
			m_Pending = Start();
			if(m_Pending.valid())
			{
				m_PendingGeneration = m_Generation;
				++m_Attempts;
			}
		}
		catch(const std::bad_alloc &)
		{
		}
		catch(const std::length_error &)
		{
		}
		catch(const std::system_error &Error)
		{
			if(Error.code() != std::errc::resource_unavailable_try_again)
				throw;
		}

	public:
		void Reset(int64_t Now) noexcept
		{
			// 切歌时保留原请求，待它结束后再启动新请求。
			++m_Generation;
			m_Attempts = 0;
			m_NextFetch = Now;
			m_Result = {};
		}

		void ConsumeReady(int64_t Now)
		{
			if(!m_Pending.valid() || m_Pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
				return;
			if(m_PendingGeneration != m_Generation)
			{
				// 旧请求已结束，直接释放结果和保存在 future 中的异常。
				m_Pending = {};
				return;
			}

			SLyricFetchResult Result;
			try
			{
				Result = m_Pending.get();
			}
			catch(const std::bad_alloc &)
			{
			}
			catch(const std::length_error &)
			{
			}
			if(Result.m_Success)
				m_Result = std::move(Result);
			else
				m_NextFetch = Now + RETRY_STEP_MS * m_Attempts;
		}

		template<typename TStart>
		void Update(int64_t Now, bool HasIdentity, TStart &&Start)
		{
			ConsumeReady(Now);
			if(HasIdentity && !m_Pending.valid() && !m_Result.m_Success && m_Attempts < MAX_ATTEMPTS && Now >= m_NextFetch)
				StartRequest(Start);
		}

		bool Exhausted() const { return !m_Result.m_Success && m_Attempts >= MAX_ATTEMPTS && !m_Pending.valid(); }
		bool Loading() const { return !m_Result.m_Success && !Exhausted(); }
		const SLyrics &Lyrics() const { return m_Result.m_Lyrics; }
	};
}

#endif
