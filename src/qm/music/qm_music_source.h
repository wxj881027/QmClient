#ifndef QM_MUSIC_QM_MUSIC_SOURCE_H
#define QM_MUSIC_QM_MUSIC_SOURCE_H

#include <cstdint>
#include <string>

namespace QmMusicHook
{
	// 两家采集器只在独立 helper 内运行，歌曲身份直接来自正在播放的应用。
	struct SPlayback
	{
		uint32_t m_ProcessId = 0;
		bool m_HasSong = false;
		bool m_Playing = false;
		bool m_Loading = false;
		bool m_PositionValid = false;
		int64_t m_PositionMs = 0;
		int64_t m_DurationMs = 0;
		std::string m_MediaId;
		std::string m_Title;
		std::string m_Artist;
		std::string m_Album;
		std::string m_CoverUrl;
		// 已解密的 krc、qrc（逐字文本）或 lrc；暂缺时留空，后续轮询补齐。
		std::string m_LyricType;
		std::string m_LyricContent;
		std::string m_TranslationLrc;
		std::string m_Error;
	};
}

#endif
