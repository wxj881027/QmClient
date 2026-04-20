#include "voice_utils.h"

#include <base/system.h>
#include <base/str.h>
#include <engine/shared/protocol.h>

#include <algorithm>
#include <cmath>
#include <cctype>

namespace VoiceUtils
{

void WriteU16(uint8_t *pBuf, uint16_t Value)
{
	pBuf[0] = Value & 0xff;
	pBuf[1] = (Value >> 8) & 0xff;
}

void WriteU32(uint8_t *pBuf, uint32_t Value)
{
	pBuf[0] = Value & 0xff;
	pBuf[1] = (Value >> 8) & 0xff;
	pBuf[2] = (Value >> 16) & 0xff;
	pBuf[3] = (Value >> 24) & 0xff;
}

void WriteFloat(uint8_t *pBuf, float Value)
{
	static_assert(sizeof(float) == 4, "float must be 4 bytes");
	uint32_t Bits = 0;
	mem_copy(&Bits, &Value, sizeof(Bits));
	WriteU32(pBuf, Bits);
}

uint16_t ReadU16(const uint8_t *pBuf)
{
	return (uint16_t)pBuf[0] | ((uint16_t)pBuf[1] << 8);
}

uint32_t ReadU32(const uint8_t *pBuf)
{
	return (uint32_t)pBuf[0] | ((uint32_t)pBuf[1] << 8) | ((uint32_t)pBuf[2] << 16) | ((uint32_t)pBuf[3] << 24);
}

float ReadFloat(const uint8_t *pBuf)
{
	uint32_t Bits = ReadU32(pBuf);
	float Value = 0.0f;
	mem_copy(&Value, &Bits, sizeof(Value));
	return Value;
}

float SanitizeFloat(float Value)
{
	if(!std::isfinite(Value))
		return 0.0f;
	if(Value > 1000000.0f)
		return 1000000.0f;
	if(Value < -1000000.0f)
		return -1000000.0f;
	return Value;
}

float VoiceFramePeak(const int16_t *pSamples, int Count)
{
	int Peak = 0;
	for(int i = 0; i < Count; i++)
	{
		const int Sample = pSamples[i];
		const int Abs = Sample == -32768 ? 32768 : (Sample < 0 ? -Sample : Sample);
		if(Abs > Peak)
			Peak = Abs;
	}
	return Peak / 32768.0f;
}

float VoiceFrameRms(const int16_t *pSamples, int Count)
{
	if(Count <= 0)
		return 0.0f;
	double Sum = 0.0;
	for(int i = 0; i < Count; i++)
	{
		const float x = pSamples[i] / 32768.0f;
		Sum += x * x;
	}
	return (float)std::sqrt(Sum / (double)Count);
}

bool VoiceListMatch(const char *pList, const char *pName)
{
	if(!pList || pList[0] == '\0')
		return false;

	const char *p = pList;
	while(*p)
	{
		while(*p == ',' || *p == ' ' || *p == '\t')
			p++;
		if(*p == '\0')
			break;

		const char *pStart = p;
		while(*p && *p != ',')
			p++;
		int Len = (int)(p - pStart);
		while(Len > 0 && std::isspace((unsigned char)pStart[Len - 1]))
			Len--;
		if(Len <= 0)
			continue;

		char aToken[MAX_NAME_LENGTH];
		int CopyLen = Len < MAX_NAME_LENGTH - 1 ? Len : MAX_NAME_LENGTH - 1;
		for(int i = 0; i < CopyLen; i++)
			aToken[i] = pStart[i];
		aToken[CopyLen] = '\0';

		if(str_comp_nocase(aToken, pName) == 0)
			return true;
	}

	return false;
}

bool VoiceNameVolume(const char *pList, const char *pName, int &OutPercent)
{
	if(!pList || pList[0] == '\0' || !pName || pName[0] == '\0')
		return false;

	const char *p = pList;
	while(*p)
	{
		while(*p == ',' || *p == ' ' || *p == '\t')
			p++;
		if(*p == '\0')
			break;

		const char *pStart = p;
		while(*p && *p != ',')
			p++;
		const char *pEnd = p;
		while(pEnd > pStart && std::isspace((unsigned char)pEnd[-1]))
			pEnd--;
		if(pEnd <= pStart)
			continue;

		const char *pSep = nullptr;
		for(const char *q = pStart; q < pEnd; q++)
		{
			if(*q == '=' || *q == ':')
			{
				pSep = q;
				break;
			}
		}
		if(!pSep)
			continue;

		const char *pNameEnd = pSep;
		while(pNameEnd > pStart && std::isspace((unsigned char)pNameEnd[-1]))
			pNameEnd--;
		const char *pValueStart = pSep + 1;
		while(pValueStart < pEnd && std::isspace((unsigned char)*pValueStart))
			pValueStart++;

		const int NameLen = (int)(pNameEnd - pStart);
		const int ValueLen = (int)(pEnd - pValueStart);
		if(NameLen <= 0 || ValueLen <= 0)
			continue;

		char aToken[MAX_NAME_LENGTH];
		int CopyLen = NameLen < MAX_NAME_LENGTH - 1 ? NameLen : MAX_NAME_LENGTH - 1;
		for(int i = 0; i < CopyLen; i++)
			aToken[i] = pStart[i];
		aToken[CopyLen] = '\0';

		if(str_comp_nocase(aToken, pName) != 0)
			continue;

		char aValue[16];
		int ValueCopyLen = ValueLen < 15 ? ValueLen : 15;
		for(int i = 0; i < ValueCopyLen; i++)
			aValue[i] = pValueStart[i];
		aValue[ValueCopyLen] = '\0';

		int Percent = str_toint(aValue);
		Percent = std::clamp(Percent, 0, 200);
		OutPercent = Percent;
		return true;
	}

	return false;
}

void ApplyMicGain(float Gain, int16_t *pSamples, int Count)
{
	if(Gain == 1.0f)
		return;

	for(int i = 0; i < Count; i++)
	{
		const float Out = pSamples[i] * Gain;
		const int Sample = (int)std::clamp(Out, -32768.0f, 32767.0f);
		pSamples[i] = (int16_t)Sample;
	}
}

void ApplyHpfCompressor(const SCompressorConfig &Config, int16_t *pSamples, int Count, SHpfCompressorState &State)
{
	if(!Config.m_Enable)
		return;

	const float CutoffHz = 120.0f;
	const float Rc = 1.0f / (2.0f * 3.14159265f * CutoffHz);
	const float Dt = 1.0f / VOICE_SAMPLE_RATE;
	const float Alpha = Rc / (Rc + Dt);

	const float NoiseFloor = 0.02f;
	const float AttackCoeff = 1.0f - std::exp(-1.0f / (Config.m_AttackSec * VOICE_SAMPLE_RATE));
	const float ReleaseCoeff = 1.0f - std::exp(-1.0f / (Config.m_ReleaseSec * VOICE_SAMPLE_RATE));

	for(int i = 0; i < Count; i++)
	{
		const float x = pSamples[i] / 32768.0f;
		const float y = Alpha * (State.m_PrevOut + x - State.m_PrevIn);
		State.m_PrevIn = x;
		State.m_PrevOut = SanitizeFloat(y);

		const float AbsY = std::fabs(State.m_PrevOut);
		if(AbsY > State.m_Env)
			State.m_Env += (AbsY - State.m_Env) * AttackCoeff;
		else
			State.m_Env += (AbsY - State.m_Env) * ReleaseCoeff;

		float Gain = 1.0f;
		if(State.m_Env > Config.m_Threshold)
			Gain = (Config.m_Threshold + (State.m_Env - Config.m_Threshold) / Config.m_Ratio) / State.m_Env;
		if(State.m_Env > NoiseFloor)
			Gain *= Config.m_MakeupGain;

		const float Out = std::clamp(State.m_PrevOut * Gain, -Config.m_Limiter, Config.m_Limiter);
		const int Sample = (int)std::clamp(Out * 32767.0f, -32768.0f, 32767.0f);
		pSamples[i] = (int16_t)Sample;
	}
}

S3DAudioResult Compute3DAudio(
	vec2 LocalPos,
	vec2 SenderPos,
	float Radius,
	float Volume,
	float StereoWidth,
	bool StereoEnabled,
	bool IgnoreDistance)
{
	S3DAudioResult Result = {1.0f, 1.0f, 1.0f};

	const float Dist = distance(LocalPos, SenderPos);
	if(!IgnoreDistance && Dist > Radius)
	{
		Result.m_Volume = 0.0f;
		Result.m_LeftGain = 0.0f;
		Result.m_RightGain = 0.0f;
		return Result;
	}

	const float RadiusFactor = IgnoreDistance ? 1.0f : (1.0f - (Dist / Radius));
	Result.m_Volume = std::clamp(RadiusFactor * Volume, 0.0f, 4.0f);

	const float Pan = StereoEnabled ? std::clamp(((SenderPos.x - LocalPos.x) / Radius) * StereoWidth, -1.0f, 1.0f) : 0.0f;
	Result.m_LeftGain = Result.m_Volume * (Pan <= 0.0f ? 1.0f : (1.0f - Pan));
	Result.m_RightGain = Result.m_Volume * (Pan >= 0.0f ? 1.0f : (1.0f + Pan));

	return Result;
}

}
