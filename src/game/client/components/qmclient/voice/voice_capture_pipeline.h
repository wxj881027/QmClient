// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_CAPTURE_PIPELINE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_CAPTURE_PIPELINE_H

#include "voice_core.h"

namespace VoiceUtils
{
	void ProcessVoiceCaptureFrame(
		const SRClientVoiceConfigSnapshot &Config,
		int16_t *pSamples,
		int Count,
		float &AgcGain,
		float &NoiseFloor,
		float &NoiseGate,
		float &HpfPrevIn,
		float &HpfPrevOut,
		float &CompEnv);
}

#endif
