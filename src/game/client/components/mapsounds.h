#ifndef GAME_CLIENT_COMPONENTS_MAPSOUNDS_H
#define GAME_CLIENT_COMPONENTS_MAPSOUNDS_H

#include "map_sound_source_queue.h"

#include <game/client/component.h>
#include <game/mapitems.h>

#include <cstdint>
#include <memory>
#include <vector>

class CQmMapSoundDecodeJob;

class CMapSounds : public CComponent
{
	int m_aSounds[MAX_MAPSOUNDS];
	int m_Count;

	std::vector<SMapSoundSource> m_vSourceQueue;
	std::shared_ptr<CQmMapSoundDecodeJob> m_pDecodeJob;
	std::vector<std::shared_ptr<CQmMapSoundDecodeJob>> m_vRetiredDecodeJobs;
	bool m_DecodeReady = true;
	bool m_DecodeWarning = false;
	void Clear();
	bool SoundEnabled() const;

public:
	CMapSounds();
	~CMapSounds() override;
	int Sizeof() const override { return sizeof(*this); }

	void Play(int Channel, int SoundId);
	void PlayAt(int Channel, int SoundId, vec2 Position);
	void StopAll();

	void OnMapLoad() override;
	bool PollDecode();
	bool DecodeReady() const { return m_DecodeReady; }
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;
};

#endif // GAME_CLIENT_COMPONENTS_MAPSOUNDS_H
