// VoiceUtils voice_audio_utils_test.cpp 行为测试。
// VoiceUtils 协议、音频算法和设备选择行为测试。
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1
#include "test.h"

#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/voice/voice_capture_pipeline.h>
#include <game/client/components/qmclient/voice/voice_core.h>
#include <game/client/components/qmclient/voice/voice_utils.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

#if defined(CONF_RNNOISE)
#include <rnnoise.h>
#endif

using namespace VoiceUtils;

namespace VoiceUtils
{
	int ResolveNoiseSuppressMode(int ConfigValue, bool RnnoiseRuntimeAvailable, bool *pFallbackUsed);
}

static constexpr int TEST_VOICE_NOISE_SUPPRESS_OFF = 0;
static constexpr int TEST_VOICE_NOISE_SUPPRESS_SIMPLE = 1;
static constexpr int TEST_VOICE_NOISE_SUPPRESS_RNNOISE = 2;

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

TEST(VoiceUtils, VoiceFramePeakNullPointer)
{
	EXPECT_FLOAT_EQ(VoiceFramePeak(nullptr, 10), 0.0f);
}

TEST(VoiceUtils, VoiceFramePeakZeroCount)
{
	int16_t aSamples[4] = {1000, 2000, 3000, 4000};
	EXPECT_FLOAT_EQ(VoiceFramePeak(aSamples, 0), 0.0f);
}

TEST(VoiceUtils, VoiceFramePeakNegativeCount)
{
	int16_t aSamples[4] = {1000, 2000, 3000, 4000};
	EXPECT_FLOAT_EQ(VoiceFramePeak(aSamples, -5), 0.0f);
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

TEST(VoiceUtils, BlendDenoisedFrameKeepsDryWhenWetMixZero)
{
	const int16_t aDry[4] = {1000, -2000, 3000, -4000};
	int16_t aWet[4] = {9000, -9000, 9000, -9000};

	BlendDenoisedFrame(aDry, aWet, 4, 0.0f);

	EXPECT_EQ(aWet[0], aDry[0]);
	EXPECT_EQ(aWet[1], aDry[1]);
	EXPECT_EQ(aWet[2], aDry[2]);
	EXPECT_EQ(aWet[3], aDry[3]);
}

TEST(VoiceUtils, BlendDenoisedFrameKeepsWetWhenWetMixOne)
{
	const int16_t aDry[4] = {1000, -2000, 3000, -4000};
	int16_t aWet[4] = {9000, -9000, 9000, -9000};
	const int16_t aExpected[4] = {9000, -9000, 9000, -9000};

	BlendDenoisedFrame(aDry, aWet, 4, 1.0f);

	EXPECT_EQ(aWet[0], aExpected[0]);
	EXPECT_EQ(aWet[1], aExpected[1]);
	EXPECT_EQ(aWet[2], aExpected[2]);
	EXPECT_EQ(aWet[3], aExpected[3]);
}

TEST(VoiceUtils, BlendDenoisedFrameInterpolatesSamples)
{
	const int16_t aDry[2] = {1000, -1000};
	int16_t aWet[2] = {3000, -3000};

	BlendDenoisedFrame(aDry, aWet, 2, 0.25f);

	EXPECT_EQ(aWet[0], 1500);
	EXPECT_EQ(aWet[1], -1500);
}
