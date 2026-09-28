#ifndef QM_MUSIC_QM_MUSIC_PUBLICATION_H
#define QM_MUSIC_QM_MUSIC_PUBLICATION_H

#include "qm_music_source.h"

#include <qm/hooks/soda/qm_soda_protocol.h>

namespace QmMusicHook
{
	// 完整身份和歌词内容决定版本；进度、暂停变化不触发歌词文件重载。
	class CPublication
	{
		SPlayback m_Playback;
		uint64_t m_Generation = 0;

	public:
		explicit CPublication(uint64_t InitialGeneration = 0) :
			m_Generation(InitialGeneration) {}
		bool Update(const SPlayback &Playback);
		uint64_t Generation() const { return m_Generation; }
		bool HasLyrics() const;
		std::string LyricJson() const;
		QmSodaHook::SSnapshot Snapshot(uint64_t TickMs, const std::string &LyricPath) const;
	};
}

#endif
