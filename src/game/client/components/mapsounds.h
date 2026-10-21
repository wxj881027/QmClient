#ifndef GAME_CLIENT_COMPONENTS_MAPSOUNDS_H
#define GAME_CLIENT_COMPONENTS_MAPSOUNDS_H

#include "map_sound_source_queue.h"

#include <game/client/component.h>
#include <game/mapitems.h>

#include <vector>

class CMapSounds : public CComponent
{
	int m_aSounds[MAX_MAPSOUNDS];
	int m_Count;

	std::vector<SMapSoundSource> m_vSourceQueue;
	void Clear();
	bool SoundEnabled() const;

public:
	CMapSounds();
	int Sizeof() const override { return sizeof(*this); }

	void Play(int Channel, int SoundId);
	void PlayAt(int Channel, int SoundId, vec2 Position);
	void StopAll();

	void OnMapLoad() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;
};

#endif // GAME_CLIENT_COMPONENTS_MAPSOUNDS_H
