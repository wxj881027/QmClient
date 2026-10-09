// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "voice_capture_pipeline.h"

#include "voice_utils.h"

#include <algorithm>

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
		float &CompEnv)
	{
		// 语音核心只保留稳定、可预测的麦克风增益。旧的 AGC、RNNoise、
		// 高通、压缩器和限幅器不再进入实时链路，兼容参数仍由配置层读取。
		AgcGain = 1.0f;
		NoiseFloor = 0.0f;
		NoiseGate = 1.0f;
		HpfPrevIn = 0.0f;
		HpfPrevOut = 0.0f;
		CompEnv = 0.0f;

		if(!pSamples || Count <= 0)
			return;

		const float Gain = std::clamp(Config.m_QmVoiceMicVolume / 100.0f, 0.0f, 3.0f);
		ApplyMicGain(Gain, pSamples, Count);
		TraceVoiceProcessStage(EVoiceProcessStage::MIC_GAIN);
	}
}
