#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_UTILS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_UTILS_H

#include <base/types.h>
#include <base/vmath.h>
#include <cstdint>

constexpr int VOICE_SAMPLE_RATE = 48000;
constexpr int VOICE_FRAME_SAMPLES = 960;

namespace VoiceUtils
{
void WriteU16(uint8_t *pBuf, uint16_t Value);
void WriteU32(uint8_t *pBuf, uint32_t Value);
void WriteFloat(uint8_t *pBuf, float Value);
uint16_t ReadU16(const uint8_t *pBuf);
uint32_t ReadU32(const uint8_t *pBuf);
float ReadFloat(const uint8_t *pBuf);

float SanitizeFloat(float Value);

float VoiceFramePeak(const int16_t *pSamples, int Count);
float VoiceFrameRms(const int16_t *pSamples, int Count);

bool VoiceListMatch(const char *pList, const char *pName);
bool VoiceNameVolume(const char *pList, const char *pName, int &OutPercent);

void ApplyMicGain(float Gain, int16_t *pSamples, int Count);

struct SHpfCompressorState
{
	float m_PrevIn = 0.0f;
	float m_PrevOut = 0.0f;
	float m_Env = 0.0f;
};

struct SCompressorConfig
{
	bool m_Enable = false;
	float m_Threshold = 0.5f;
	float m_Ratio = 4.0f;
	float m_AttackSec = 0.01f;
	float m_ReleaseSec = 0.1f;
	float m_MakeupGain = 1.0f;
	float m_Limiter = 0.9f;
};

void ApplyHpfCompressor(const SCompressorConfig &Config, int16_t *pSamples, int Count, SHpfCompressorState &State);

struct S3DAudioResult
{
	float m_LeftGain;
	float m_RightGain;
	float m_Volume;
};

S3DAudioResult Compute3DAudio(
	vec2 LocalPos,
	vec2 SenderPos,
	float Radius,
	float Volume,
	float StereoWidth,
	bool StereoEnabled,
	bool IgnoreDistance);

}
#endif
