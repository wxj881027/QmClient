#include "qm_music_publication.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace QmMusicHook
{
	namespace
	{
		std::string JsonString(std::string_view Value)
		{
			std::string Result = "\"";
			for(const unsigned char Byte : Value)
			{
				if(Byte == '"' || Byte == '\\')
				{
					Result += '\\';
					Result += (char)Byte;
				}
				else if(Byte < 0x20)
				{
					char aEscape[7];
					std::snprintf(aEscape, sizeof(aEscape), "\\u%04x", Byte);
					Result += aEscape;
				}
				else
					Result += (char)Byte;
			}
			return Result + '"';
		}

		template<size_t N>
		void Copy(char (&aDestination)[N], const std::string &Source)
		{
			QmSodaHook::CopyUtf8Truncated(aDestination, N, Source.data(), Source.size());
		}
	}

	bool CPublication::Update(const SPlayback &Playback)
	{
		const bool Changed = m_Playback.m_ProcessId != Playback.m_ProcessId ||
				     m_Playback.m_HasSong != Playback.m_HasSong || m_Playback.m_MediaId != Playback.m_MediaId ||
				     m_Playback.m_LyricType != Playback.m_LyricType || m_Playback.m_LyricContent != Playback.m_LyricContent ||
				     m_Playback.m_TranslationLrc != Playback.m_TranslationLrc || m_Playback.m_Title != Playback.m_Title ||
				     m_Playback.m_Artist != Playback.m_Artist || m_Playback.m_Album != Playback.m_Album;
		m_Playback = Playback;
		if(Changed)
			++m_Generation;
		return Changed;
	}

	bool CPublication::HasLyrics() const
	{
		return m_Playback.m_HasSong && !m_Playback.m_MediaId.empty() && !m_Playback.m_LyricContent.empty();
	}

	std::string CPublication::LyricJson() const
	{
		return "{\"mediaId\":" + JsonString(m_Playback.m_MediaId) +
		       ",\"title\":" + JsonString(m_Playback.m_Title) +
		       ",\"artist\":" + JsonString(m_Playback.m_Artist) +
		       ",\"album\":" + JsonString(m_Playback.m_Album) +
		       ",\"coverUrl\":" + JsonString(m_Playback.m_CoverUrl) +
		       ",\"durationMs\":" + std::to_string(m_Playback.m_DurationMs) +
		       ",\"lyricType\":" + JsonString(m_Playback.m_LyricType) +
		       ",\"lyricContent\":" + JsonString(m_Playback.m_LyricContent) +
		       ",\"translationLrc\":" + JsonString(m_Playback.m_TranslationLrc) + "}";
	}

	QmSodaHook::SSnapshot CPublication::Snapshot(uint64_t TickMs, const std::string &LyricPath) const
	{
		QmSodaHook::SSnapshot Result;
		Result.m_UpdatedAtTick = TickMs;
		Result.m_SodaMusicPid = m_Playback.m_ProcessId;
		Result.m_Generation = m_Generation;
		Result.m_PositionMs = std::max<int64_t>(0, m_Playback.m_PositionMs);
		Result.m_DurationMs = std::max<int64_t>(0, m_Playback.m_DurationMs);
		Copy(Result.m_aError, m_Playback.m_Error);
		if(m_Playback.m_Loading)
			Result.m_Flags |= QmSodaHook::FLAG_LOADING;
		if(!m_Playback.m_HasSong || m_Playback.m_MediaId.empty())
			return Result;
		Result.m_Flags |= QmSodaHook::FLAG_HAS_SONG;
		if(m_Playback.m_Playing)
			Result.m_Flags |= QmSodaHook::FLAG_PLAYING;
		if(m_Playback.m_PositionValid && m_Playback.m_PositionMs >= 0)
			Result.m_Flags |= QmSodaHook::FLAG_POSITION_VALID;
		Copy(Result.m_aMediaId, m_Playback.m_MediaId);
		Copy(Result.m_aTitle, m_Playback.m_Title);
		Copy(Result.m_aArtist, m_Playback.m_Artist);
		Copy(Result.m_aAlbum, m_Playback.m_Album);
		Copy(Result.m_aCoverUrl, m_Playback.m_CoverUrl);
		if(!m_Playback.m_CoverUrl.empty())
			Result.m_Flags |= QmSodaHook::FLAG_HAS_COVER;
		if(HasLyrics() && !LyricPath.empty() && LyricPath.size() < sizeof(Result.m_aLyricFilePath))
		{
			Result.m_Flags |= QmSodaHook::FLAG_HAS_LYRIC_FILE;
			Copy(Result.m_aLyricFilePath, LyricPath);
		}
		return Result;
	}
}
