#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_PRESENTATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_PRESENTATION_H

#include "music_humming_state.h"
#include "music_lyrics_community.h"
#include "music_lyrics_display.h"
#include "music_singing_state.h"
#include "music_world_lyrics.h"

#include <engine/console.h>

#include <game/client/component.h>

class CQmLyricsPresentation : public CComponent
{
public:
	struct SFrame
	{
		bool m_Active = false;
		int m_RowCount = 0;
		float m_Progress = -1.0f;
		char m_aaRows[2][1024] = {};
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnReset() override;
	void OnShutdown() override;
	void OnWindowResize() override;
	void OnMapLoad() override;
	void OnStateChange(int NewState, int OldState) override;
	const SFrame &Frame() const { return m_Frame; }

private:
	static void ConSing(IConsole::IResult *pResult, void *pUserData);
	QmMusicLyrics::SSourceSnapshot RawSource(int &SourceId) const;
	QmMusicLyrics::SSourceSnapshot CurrentSource(int &SourceId) const;
	void SynchronizeSinging(const QmMusicLyrics::SSourceSnapshot &Source, int SourceId);

	SFrame m_Frame;
	QmMusicLyrics::CCommunityLyrics m_CommunityLyrics;
	QmMusicLyrics::CHummingState m_Humming;
	QmMusicLyrics::CSingingState m_Singing;
	QmMusicLyrics::CWorldLyricsState m_WorldLyrics;
	QmMusicLyrics::CWorldLyricsRenderer m_WorldLyricsRenderer;
};

#endif
