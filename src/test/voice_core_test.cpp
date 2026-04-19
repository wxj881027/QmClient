#include "test.h"

#include <game/client/components/qmclient/voice_utils.h>
#include <base/vmath.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

using namespace VoiceUtils;

TEST(VoiceUtils, WriteReadU16)
{
	uint8_t aBuf[2];

	WriteU16(aBuf, 0x0000);
	EXPECT_EQ(ReadU16(aBuf), 0x0000);

	WriteU16(aBuf, 0x00FF);
	EXPECT_EQ(ReadU16(aBuf), 0x00FF);

	WriteU16(aBuf, 0xFF00);
	EXPECT_EQ(ReadU16(aBuf), 0xFF00);

	WriteU16(aBuf, 0x1234);
	EXPECT_EQ(ReadU16(aBuf), 0x1234);

	WriteU16(aBuf, 0xFFFF);
	EXPECT_EQ(ReadU16(aBuf), 0xFFFF);
}

TEST(VoiceUtils, WriteReadU32)
{
	uint8_t aBuf[4];

	WriteU32(aBuf, 0x00000000);
	EXPECT_EQ(ReadU32(aBuf), 0x00000000u);

	WriteU32(aBuf, 0x000000FF);
	EXPECT_EQ(ReadU32(aBuf), 0x000000FFu);

	WriteU32(aBuf, 0xFF000000);
	EXPECT_EQ(ReadU32(aBuf), 0xFF000000u);

	WriteU32(aBuf, 0x12345678);
	EXPECT_EQ(ReadU32(aBuf), 0x12345678u);

	WriteU32(aBuf, 0xFFFFFFFF);
	EXPECT_EQ(ReadU32(aBuf), 0xFFFFFFFFu);
}

TEST(VoiceUtils, WriteReadFloat)
{
	uint8_t aBuf[4];

	WriteFloat(aBuf, 0.0f);
	EXPECT_FLOAT_EQ(ReadFloat(aBuf), 0.0f);

	WriteFloat(aBuf, 1.0f);
	EXPECT_FLOAT_EQ(ReadFloat(aBuf), 1.0f);

	WriteFloat(aBuf, -1.0f);
	EXPECT_FLOAT_EQ(ReadFloat(aBuf), -1.0f);

	WriteFloat(aBuf, 3.14159f);
	EXPECT_NEAR(ReadFloat(aBuf), 3.14159f, 0.00001f);

	WriteFloat(aBuf, 12345.6789f);
	EXPECT_NEAR(ReadFloat(aBuf), 12345.6789f, 0.001f);
}

TEST(VoiceUtils, SanitizeFloatNormalValues)
{
	EXPECT_FLOAT_EQ(SanitizeFloat(0.0f), 0.0f);
	EXPECT_FLOAT_EQ(SanitizeFloat(1.0f), 1.0f);
	EXPECT_FLOAT_EQ(SanitizeFloat(-1.0f), -1.0f);
	EXPECT_FLOAT_EQ(SanitizeFloat(100.0f), 100.0f);
	EXPECT_FLOAT_EQ(SanitizeFloat(-100.0f), -100.0f);
}

TEST(VoiceUtils, SanitizeFloatInfinity)
{
	EXPECT_FLOAT_EQ(SanitizeFloat(std::numeric_limits<float>::infinity()), 0.0f);
	EXPECT_FLOAT_EQ(SanitizeFloat(-std::numeric_limits<float>::infinity()), 0.0f);
}

TEST(VoiceUtils, SanitizeFloatNaN)
{
	EXPECT_FLOAT_EQ(SanitizeFloat(std::numeric_limits<float>::quiet_NaN()), 0.0f);
}

TEST(VoiceUtils, SanitizeFloatClamp)
{
	EXPECT_FLOAT_EQ(SanitizeFloat(2000000.0f), 1000000.0f);
	EXPECT_FLOAT_EQ(SanitizeFloat(-2000000.0f), -1000000.0f);
	EXPECT_FLOAT_EQ(SanitizeFloat(1000000.0f), 1000000.0f);
	EXPECT_FLOAT_EQ(SanitizeFloat(-1000000.0f), -1000000.0f);
}

TEST(VoiceUtils, VoiceFramePeakSilence)
{
	int16_t aSamples[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	EXPECT_FLOAT_EQ(VoiceFramePeak(aSamples, 10), 0.0f);
}

TEST(VoiceUtils, VoiceFramePeakPositive)
{
	int16_t aSamples[4] = {10000, 20000, 15000, 5000};
	EXPECT_FLOAT_EQ(VoiceFramePeak(aSamples, 4), 20000 / 32768.0f);
}

TEST(VoiceUtils, VoiceFramePeakNegative)
{
	int16_t aSamples[4] = {-10000, -20000, -15000, -5000};
	EXPECT_FLOAT_EQ(VoiceFramePeak(aSamples, 4), 20000 / 32768.0f);
}

TEST(VoiceUtils, VoiceFramePeakMixed)
{
	int16_t aSamples[4] = {10000, -20000, 15000, -30000};
	EXPECT_FLOAT_EQ(VoiceFramePeak(aSamples, 4), 30000 / 32768.0f);
}

TEST(VoiceUtils, VoiceFramePeakMaxNegative)
{
	int16_t aSamples[1] = {-32768};
	EXPECT_FLOAT_EQ(VoiceFramePeak(aSamples, 1), 1.0f);
}

TEST(VoiceUtils, VoiceFrameRmsSilence)
{
	int16_t aSamples[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	EXPECT_FLOAT_EQ(VoiceFrameRms(aSamples, 10), 0.0f);
}

TEST(VoiceUtils, VoiceFrameRmsConstant)
{
	int16_t aSamples[4] = {16384, 16384, 16384, 16384};
	EXPECT_NEAR(VoiceFrameRms(aSamples, 4), 0.5f, 0.001f);
}

TEST(VoiceUtils, VoiceFrameRmsZeroCount)
{
	int16_t aSamples[4] = {1000, 2000, 3000, 4000};
	EXPECT_FLOAT_EQ(VoiceFrameRms(aSamples, 0), 0.0f);
}

TEST(VoiceUtils, VoiceListMatchEmpty)
{
	EXPECT_FALSE(VoiceListMatch("", "test"));
	EXPECT_FALSE(VoiceListMatch(nullptr, "test"));
}

TEST(VoiceUtils, VoiceListMatchSingle)
{
	EXPECT_TRUE(VoiceListMatch("player1", "player1"));
	EXPECT_FALSE(VoiceListMatch("player1", "player2"));
}

TEST(VoiceUtils, VoiceListMatchMultiple)
{
	EXPECT_TRUE(VoiceListMatch("player1,player2,player3", "player2"));
	EXPECT_FALSE(VoiceListMatch("player1,player2,player3", "player4"));
}

TEST(VoiceUtils, VoiceListMatchWithSpaces)
{
	EXPECT_TRUE(VoiceListMatch("player1, player2, player3", "player2"));
	EXPECT_TRUE(VoiceListMatch("  player1  ,  player2  ", "player1"));
}

TEST(VoiceUtils, VoiceListMatchCaseInsensitive)
{
	EXPECT_TRUE(VoiceListMatch("Player1", "player1"));
	EXPECT_TRUE(VoiceListMatch("PLAYER1", "player1"));
	EXPECT_TRUE(VoiceListMatch("player1", "PLAYER1"));
}

TEST(VoiceUtils, VoiceNameVolumeEmpty)
{
	int OutPercent = 0;
	EXPECT_FALSE(VoiceNameVolume("", "test", OutPercent));
	EXPECT_FALSE(VoiceNameVolume(nullptr, "test", OutPercent));
}

TEST(VoiceUtils, VoiceNameVolumeSingle)
{
	int OutPercent = 0;
	EXPECT_TRUE(VoiceNameVolume("player1=50", "player1", OutPercent));
	EXPECT_EQ(OutPercent, 50);
}

TEST(VoiceUtils, VoiceNameVolumeMultiple)
{
	int OutPercent = 0;
	EXPECT_TRUE(VoiceNameVolume("player1=50,player2=75,player3=100", "player2", OutPercent));
	EXPECT_EQ(OutPercent, 75);
}

TEST(VoiceUtils, VoiceNameVolumeWithColon)
{
	int OutPercent = 0;
	EXPECT_TRUE(VoiceNameVolume("player1:50", "player1", OutPercent));
	EXPECT_EQ(OutPercent, 50);
}

TEST(VoiceUtils, VoiceNameVolumeNotFound)
{
	int OutPercent = 0;
	EXPECT_FALSE(VoiceNameVolume("player1=50,player2=75", "player3", OutPercent));
}

TEST(VoiceUtils, VoiceNameVolumeClampHigh)
{
	int OutPercent = 0;
	EXPECT_TRUE(VoiceNameVolume("player1=300", "player1", OutPercent));
	EXPECT_EQ(OutPercent, 200);
}

TEST(VoiceUtils, VoiceNameVolumeClampLow)
{
	int OutPercent = 0;
	EXPECT_TRUE(VoiceNameVolume("player1=-50", "player1", OutPercent));
	EXPECT_EQ(OutPercent, 0);
}

TEST(VoiceUtils, ApplyMicGainUnity)
{
	int16_t aSamples[4] = {1000, 2000, -1000, -2000};
	ApplyMicGain(1.0f, aSamples, 4);
	EXPECT_EQ(aSamples[0], 1000);
	EXPECT_EQ(aSamples[1], 2000);
	EXPECT_EQ(aSamples[2], -1000);
	EXPECT_EQ(aSamples[3], -2000);
}

TEST(VoiceUtils, ApplyMicGainDouble)
{
	int16_t aSamples[4] = {1000, 2000, -1000, -2000};
	ApplyMicGain(2.0f, aSamples, 4);
	EXPECT_EQ(aSamples[0], 2000);
	EXPECT_EQ(aSamples[1], 4000);
	EXPECT_EQ(aSamples[2], -2000);
	EXPECT_EQ(aSamples[3], -4000);
}

TEST(VoiceUtils, ApplyMicGainClamp)
{
	int16_t aSamples[2] = {20000, -20000};
	ApplyMicGain(2.0f, aSamples, 2);
	EXPECT_EQ(aSamples[0], 32767);
	EXPECT_EQ(aSamples[1], -32768);
}

TEST(VoiceUtils, Compute3DAudioSamePosition)
{
	vec2 Pos(100.0f, 100.0f);
	S3DAudioResult Result = Compute3DAudio(Pos, Pos, 100.0f, 1.0f, 1.0f, true, false);

	EXPECT_FLOAT_EQ(Result.m_Volume, 1.0f);
	EXPECT_FLOAT_EQ(Result.m_LeftGain, 1.0f);
	EXPECT_FLOAT_EQ(Result.m_RightGain, 1.0f);
}

TEST(VoiceUtils, Compute3DAudioLeftSide)
{
	vec2 LocalPos(0.0f, 0.0f);
	vec2 SenderPos(-50.0f, 0.0f);
	S3DAudioResult Result = Compute3DAudio(LocalPos, SenderPos, 100.0f, 1.0f, 1.0f, true, false);

	EXPECT_GT(Result.m_LeftGain, Result.m_RightGain);
}

TEST(VoiceUtils, Compute3DAudioRightSide)
{
	vec2 LocalPos(0.0f, 0.0f);
	vec2 SenderPos(50.0f, 0.0f);
	S3DAudioResult Result = Compute3DAudio(LocalPos, SenderPos, 100.0f, 1.0f, 1.0f, true, false);

	EXPECT_LT(Result.m_LeftGain, Result.m_RightGain);
}

TEST(VoiceUtils, Compute3DAudioOutsideRadius)
{
	vec2 LocalPos(0.0f, 0.0f);
	vec2 SenderPos(200.0f, 0.0f);
	S3DAudioResult Result = Compute3DAudio(LocalPos, SenderPos, 100.0f, 1.0f, 1.0f, true, false);

	EXPECT_FLOAT_EQ(Result.m_Volume, 0.0f);
	EXPECT_FLOAT_EQ(Result.m_LeftGain, 0.0f);
	EXPECT_FLOAT_EQ(Result.m_RightGain, 0.0f);
}

TEST(VoiceUtils, Compute3DAudioIgnoreDistance)
{
	vec2 LocalPos(0.0f, 0.0f);
	vec2 SenderPos(200.0f, 0.0f);
	S3DAudioResult Result = Compute3DAudio(LocalPos, SenderPos, 100.0f, 1.0f, 1.0f, true, true);

	EXPECT_FLOAT_EQ(Result.m_Volume, 1.0f);
}

TEST(VoiceUtils, Compute3DAudioMono)
{
	vec2 LocalPos(0.0f, 0.0f);
	vec2 SenderPos(50.0f, 0.0f);
	S3DAudioResult Result = Compute3DAudio(LocalPos, SenderPos, 100.0f, 1.0f, 1.0f, false, false);

	EXPECT_FLOAT_EQ(Result.m_LeftGain, Result.m_RightGain);
}

TEST(VoiceUtils, Compute3DAudioDistanceAttenuation)
{
	vec2 LocalPos(0.0f, 0.0f);
	vec2 SenderPos1(25.0f, 0.0f);
	vec2 SenderPos2(50.0f, 0.0f);

	S3DAudioResult Result1 = Compute3DAudio(LocalPos, SenderPos1, 100.0f, 1.0f, 1.0f, false, false);
	S3DAudioResult Result2 = Compute3DAudio(LocalPos, SenderPos2, 100.0f, 1.0f, 1.0f, false, false);

	EXPECT_GT(Result1.m_Volume, Result2.m_Volume);
}

TEST(VoiceUtils, HpfCompressorDisabled)
{
	SCompressorConfig Config;
	Config.m_Enable = false;
	SHpfCompressorState State;

	int16_t aSamples[4] = {10000, 20000, -10000, -20000};
	int16_t aExpected[4] = {10000, 20000, -10000, -20000};

	ApplyHpfCompressor(Config, aSamples, 4, State);

	for(int i = 0; i < 4; i++)
		EXPECT_EQ(aSamples[i], aExpected[i]);
}

TEST(VoiceUtils, HpfCompressorEnabled)
{
	SCompressorConfig Config;
	Config.m_Enable = true;
	Config.m_Threshold = 0.5f;
	Config.m_Ratio = 4.0f;
	Config.m_Limiter = 0.9f;
	SHpfCompressorState State;

	int16_t aSamples[100];
	for(int i = 0; i < 100; i++)
		aSamples[i] = 30000;

	ApplyHpfCompressor(Config, aSamples, 100, State);

	bool AnyChanged = false;
	for(int i = 0; i < 100; i++)
	{
		if(aSamples[i] != 30000)
		{
			AnyChanged = true;
			break;
		}
	}
	EXPECT_TRUE(AnyChanged);
}
