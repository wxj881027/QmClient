#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_WORLD_LYRICS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_WORLD_LYRICS_H

#include "music_world_lyrics_state.h"

#include <engine/textrender.h>

namespace QmMusicLyrics
{
	class CWorldLyricsRenderer
	{
		struct SCachedRow
		{
			STextContainerIndex m_Container;
			std::string m_Text;
			vec2 m_Size = vec2(0.0f, 0.0f);
			uint64_t m_AtlasRevision = 0;
			int m_VisibleCharacters = -1;
			bool m_Initialized = false;
		};
		// 只保留当前句与上一句，双语各占一行缓存。
		std::array<std::array<SCachedRow, 2>, 2> m_aaRows;

	public:
		void Reset(ITextRender &TextRender);
		void Render(ITextRender &TextRender, IGraphics &Graphics, const CWorldLyricsState &State, vec2 Camera, float Zoom, double Now);
	};
}

#endif
