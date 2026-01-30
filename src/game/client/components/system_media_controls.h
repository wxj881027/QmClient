#ifndef GAME_CLIENT_COMPONENTS_SYSTEM_MEDIA_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_SYSTEM_MEDIA_CONTROLS_H

#include <game/client/component.h>

class CSystemMediaControls : public CComponent
{
public:
	struct SState
	{
		bool m_CanPlay = false;
		bool m_CanPause = false;
		bool m_CanPrev = false;
		bool m_CanNext = false;
		bool m_Playing = false;
		char m_aTitle[128] = {};
		char m_aArtist[128] = {};
	};

	int Sizeof() const override { return sizeof(*this); }

	bool GetStateSnapshot(SState &State) const;
	void Previous();
	void PlayPause();
	void Next();
};

#endif
