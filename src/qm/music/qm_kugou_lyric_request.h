#ifndef QM_MUSIC_QM_KUGOU_LYRIC_REQUEST_H
#define QM_MUSIC_QM_KUGOU_LYRIC_REQUEST_H

#include "qm_music_source.h"

#include <chrono>
#include <future>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace QmMusicHook
{
	struct SKugouLyricResult
	{
		std::string m_Type;
		std::string m_Text;
		bool m_Success = false;
	};

	// 采样线程持有当前歌和唯一的在途请求。切歌时继续等待原请求自然结束。
	class CKugouLyricRequest
	{
	public:
		using TClock = std::chrono::steady_clock;

		template<typename TStart>
		void Update(SPlayback &Playback, TClock::time_point Now, TStart &&Start)
		{
			if(m_CurrentId != Playback.m_MediaId)
			{
				m_CurrentId = Playback.m_MediaId;
				// 即使稍后切回同一首歌，之前的请求也已失效。
				m_PendingIsCurrent = false;
				m_Lyrics = {};
				m_RequestFailed = false;
				m_RetryAt = {};
			}
			ConsumeReady(Now);
			if(!Playback.m_HasSong)
				return;
			if(!m_Pending.valid() && !m_Lyrics.m_Success && Now >= m_RetryAt && !TryStart(Playback.m_DurationMs, Start))
				m_RequestFailed = true;
			// 已接纳的歌词留在缓存中，输出准备失败时下一轮仍可复制。
			Playback.m_LyricType = m_Lyrics.m_Type;
			Playback.m_LyricContent = m_Lyrics.m_Text;
			Playback.m_Loading = !m_Lyrics.m_Success;
			if(m_RequestFailed)
				Playback.m_Error = "Kugou lyric request failed; retrying";
		}

	private:
		std::string m_CurrentId;
		std::future<SKugouLyricResult> m_Pending;
		bool m_PendingIsCurrent = false;
		SKugouLyricResult m_Lyrics;
		TClock::time_point m_RetryAt{};
		bool m_RequestFailed = false;

		void ConsumeReady(TClock::time_point Now)
		{
			if(!m_Pending.valid() || m_Pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
				return;

			if(!m_PendingIsCurrent)
			{
				m_Pending = {};
				return;
			}

			SKugouLyricResult Result;
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
			m_PendingIsCurrent = false;
			m_Lyrics = std::move(Result);
			m_RequestFailed = !m_Lyrics.m_Success;
			if(m_RequestFailed)
				m_RetryAt = Now + std::chrono::seconds(10);
		}

		template<typename TStart>
		bool TryStart(int64_t DurationMs, TStart &Start)
		try
		{
			m_Pending = Start(m_CurrentId, DurationMs);
			m_PendingIsCurrent = true;
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
		catch(const std::system_error &Error)
		{
			if(Error.code() != std::errc::resource_unavailable_try_again)
				throw;
			return false;
		}
	};
}

#endif
