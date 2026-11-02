#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_PRESENTATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_MUSIC_LYRICS_PRESENTATION_H

#include "music_humming_state.h"

#include <game/client/component.h>

class CQmLyricsPresentation : public CComponent
{
public:
	struct SFrame
	{
		bool m_Active = false;
		int m_RowCount = 0;
		char m_aaRows[2][1024] = {};
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnUpdate() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	const SFrame &Frame() const { return m_Frame; }

private:
	SFrame m_Frame;
	QmMusicLyrics::CHummingState m_Humming;
};

#endif
