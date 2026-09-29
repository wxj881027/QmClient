#ifndef QM_MUSIC_QM_QQMUSIC_SOURCE_H
#define QM_MUSIC_QM_QQMUSIC_SOURCE_H

#include "qm_music_source.h"

#include <memory>

namespace QmMusicHook
{
	class CQQMusicSource
	{
		class CImpl;
		std::unique_ptr<CImpl> m_pImpl;

	public:
		CQQMusicSource();
		~CQQMusicSource();
		bool Poll(SPlayback &Out);
	};
}

#endif
