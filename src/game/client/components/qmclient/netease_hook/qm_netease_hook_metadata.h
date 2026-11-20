#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_HOOK_QM_NETEASE_HOOK_METADATA_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_HOOK_QM_NETEASE_HOOK_METADATA_H

#include <game/client/components/qmclient/netease_hook/qm_netease_hook_protocol.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <string>
#include <vector>

namespace QmNeteaseHook
{
	constexpr uint32_t WAVE_OUT_TIME_MS = 0x0001;
	constexpr uint32_t WAVE_OUT_TIME_SAMPLES = 0x0002;
	constexpr uint32_t WAVE_OUT_TIME_BYTES = 0x0004;

	struct SLocalTrackMetadata
	{
		std::string m_SongId;
		std::string m_Title;
		std::string m_Artist;
		std::string m_Album;
		std::string m_CoverUrl;
		int64_t m_DurationMs = 0;
	};

	inline bool IsUsableLocalTrack(const SLocalTrackMetadata &Track)
	{
		return !Track.m_Title.empty();
	}

	inline std::string NormalizeWindowTitleField(const std::string &Value)
	{
		std::string Result;
		bool PendingSpace = false;
		for(const unsigned char Character : Value)
		{
			if(std::isspace(Character) != 0)
			{
				PendingSpace = !Result.empty();
				continue;
			}
			if(PendingSpace)
			{
				Result.push_back(' ');
				PendingSpace = false;
			}
			Result.push_back(Character <= 0x7f ? (char)std::tolower(Character) : (char)Character);
		}
		return Result;
	}

	inline std::string NormalizeWindowTitleArtists(const std::string &Value)
	{
		std::string Result;
		bool PendingSpace = false;
		for(const unsigned char Character : Value)
		{
			if(Character == ',' || Character == '/')
			{
				while(!Result.empty() && Result.back() == ' ')
					Result.pop_back();
				if(!Result.empty() && Result.back() != ',')
					Result.push_back(',');
				PendingSpace = false;
				continue;
			}
			if(std::isspace(Character) != 0)
			{
				PendingSpace = !Result.empty() && Result.back() != ',';
				continue;
			}
			if(PendingSpace)
			{
				Result.push_back(' ');
				PendingSpace = false;
			}
			Result.push_back(Character <= 0x7f ? (char)std::tolower(Character) : (char)Character);
		}
		while(!Result.empty() && (Result.back() == ' ' || Result.back() == ','))
			Result.pop_back();
		return Result;
	}

	inline std::string NormalizeWindowTitleLabel(const std::string &Value)
	{
		return NormalizeWindowTitleArtists(Value);
	}

	inline std::string LocalTrackWindowTitle(const SLocalTrackMetadata &Track)
	{
		const std::string Title = NormalizeWindowTitleField(Track.m_Title);
		const std::string Artists = NormalizeWindowTitleArtists(Track.m_Artist);
		if(Title.empty() || Artists.empty())
			return {};
		return NormalizeWindowTitleLabel(Track.m_Title + " - " + Track.m_Artist);
	}

	// 网易云主窗口标题由播放器维护，是播放列表中唯一可验证的当前曲目线索。
	// 多条记录命中同一个标题时，需要相同的非空歌曲 ID 才能确认是同一首歌。
	inline const SLocalTrackMetadata *FindLocalTrackByWindowTitle(const std::vector<SLocalTrackMetadata> &Tracks, const std::string &WindowTitle)
	{
		const std::string NormalizedWindowTitle = NormalizeWindowTitleLabel(WindowTitle);
		if(NormalizedWindowTitle.empty())
			return nullptr;

		const SLocalTrackMetadata *pMatch = nullptr;
		for(const SLocalTrackMetadata &Track : Tracks)
		{
			if(!IsUsableLocalTrack(Track) || LocalTrackWindowTitle(Track) != NormalizedWindowTitle)
				continue;
			if(pMatch != nullptr && (Track.m_SongId.empty() || pMatch->m_SongId != Track.m_SongId))
				return nullptr;
			pMatch = &Track;
		}
		return pMatch;
	}

	inline void CopySnapshotField(char *pDestination, size_t DestinationSize, const std::string &Source)
	{
		CopyUtf8Truncated(pDestination, DestinationSize, Source.data(), Source.size());
	}

	inline uint64_t ParseLocalTrackSongId(const std::string &Value)
	{
		if(Value.empty())
			return 0;
		uint64_t SongId = 0;
		const auto Result = std::from_chars(Value.data(), Value.data() + Value.size(), SongId);
		return Result.ec == std::errc{} && Result.ptr == Value.data() + Value.size() ? SongId : 0;
	}

	inline std::string LocalTrackMediaKey(const SLocalTrackMetadata &Track)
	{
		return Track.m_SongId + '\n' + Track.m_Title + '\n' + Track.m_Artist + '\n' + Track.m_Album;
	}

	inline bool PopulateSnapshotFromLocalTrack(SSnapshot *pSnapshot, const SLocalTrackMetadata &Track)
	{
		if(pSnapshot == nullptr || !IsUsableLocalTrack(Track))
			return false;
		CopySnapshotField(pSnapshot->m_aSongId, sizeof(pSnapshot->m_aSongId), Track.m_SongId);
		CopySnapshotField(pSnapshot->m_aTitle, sizeof(pSnapshot->m_aTitle), Track.m_Title);
		CopySnapshotField(pSnapshot->m_aArtist, sizeof(pSnapshot->m_aArtist), Track.m_Artist);
		CopySnapshotField(pSnapshot->m_aAlbum, sizeof(pSnapshot->m_aAlbum), Track.m_Album);
		CopySnapshotField(pSnapshot->m_aCoverUrl, sizeof(pSnapshot->m_aCoverUrl), Track.m_CoverUrl);
		pSnapshot->m_SongId = ParseLocalTrackSongId(Track.m_SongId);
		pSnapshot->m_DurationMs = std::max<int64_t>(0, Track.m_DurationMs);
		if(pSnapshot->m_DurationMs > 0 && pSnapshot->m_PositionMs > pSnapshot->m_DurationMs)
			pSnapshot->m_PositionMs = pSnapshot->m_DurationMs;
		pSnapshot->m_Status |= STATUS_HAS_MEDIA;
		if(pSnapshot->m_aCoverUrl[0] != '\0')
			pSnapshot->m_Status |= STATUS_HAS_COVER;
		return true;
	}

	inline bool ConvertWaveOutPositionToMs(uint32_t TimeType, uint32_t Value, uint32_t SampleRate, uint32_t AverageBytesPerSecond, int64_t *pPositionMs)
	{
		if(pPositionMs == nullptr)
			return false;
		uint64_t PositionMs = 0;
		if(TimeType == WAVE_OUT_TIME_MS)
			PositionMs = Value;
		else if(TimeType == WAVE_OUT_TIME_SAMPLES && SampleRate > 0)
			PositionMs = ((uint64_t)Value * 1000) / SampleRate;
		else if(TimeType == WAVE_OUT_TIME_BYTES && AverageBytesPerSecond > 0)
			PositionMs = ((uint64_t)Value * 1000) / AverageBytesPerSecond;
		else
			return false;
		if(PositionMs > 24ULL * 60 * 60 * 1000)
			return false;
		*pPositionMs = (int64_t)PositionMs;
		return true;
	}

} // namespace QmNeteaseHook

#endif
