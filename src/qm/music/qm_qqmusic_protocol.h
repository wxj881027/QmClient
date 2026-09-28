#ifndef QM_MUSIC_QM_QQMUSIC_PROTOCOL_H
#define QM_MUSIC_QM_QQMUSIC_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace QmMusicHook::QQMusic
{
	struct SOffsets
	{
		int m_Major;
		int m_Minor;
		uint32_t m_Struct;
		uint32_t m_Name;
		uint32_t m_Singer;
		uint32_t m_Album;
		uint32_t m_SongId;
		uint32_t m_Duration;
		uint32_t m_Progress;
		uint32_t m_SessionDuration;
		uint32_t m_TimerPointer;
		uint32_t m_TimerField;
		uint32_t m_AbsoluteProgress;
		bool m_Wide;
		bool m_DurationSeconds;
	};
	const SOffsets *FindOffsets(int Major, int Minor);
	std::string ExtractSongMid(std::string_view StreamUrl, std::string_view Parameters);

	struct SSsoLayout
	{
		uint32_t m_Pointer = 0;
		uint32_t m_Length = 0;
	};
	bool DecodeSsoLayout(const unsigned char *pData, size_t Size, SSsoLayout &Out);

	struct SLyrics
	{
		std::string m_Type;
		std::string m_Content;
		std::string m_Translation;
	};
	bool ParseLyricsResponse(std::string_view Json, uint32_t ExpectedSongId, bool MidRequest, SLyrics &Out);

	class CPlaybackClock
	{
		std::string m_Identity;
		int64_t m_LastPosition = 0;
		int64_t m_LastObservedAt = 0;
		int64_t m_LastAdvanceAt = 0;
		bool m_Playing = false;

	public:
		bool Update(std::string_view Identity, int64_t PositionMs, int64_t NowMs);
	};

	class CSongIdentity
	{
		std::string m_Title;
		std::string m_Artist;
		uint32_t m_DurationMs = 0;
		std::string m_Candidate;
		std::string m_LastConfirmedKey;
		std::string m_RejectedKey;
		std::string m_Accepted;
		int64_t m_CandidateSince = 0;

	public:
		// 显示元数据先变化时，不把尚未更新的上一首 ID 交给取词任务。
		std::string Update(std::string_view Title, std::string_view Artist, uint32_t DurationMs, std::string_view Key, int64_t NowMs);
		// 空播放态只清除本次候选，保留最后确认的身份以防切歌过程中的旧 ID 回流。
		void Suspend();
	};
}

#endif
