// VoiceUtils voice_spatial_utils_test.cpp 行为测试。
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

TEST(VoiceUtils, VoiceProcessingFactoryDefaultsMatchConfigDefaults)
{
	const auto Defaults = VoiceProcessingFactoryDefaults();

	EXPECT_EQ(Defaults.m_NoiseSuppressMode, 0);
	EXPECT_EQ(Defaults.m_NoiseSuppressStrength, 35);
	EXPECT_NEAR(Defaults.m_HpfCutoffHz, VOICE_HPF_CUTOFF_HZ, 0.001f);
	EXPECT_NEAR(Defaults.m_CompressorThreshold, 0.24f, 0.001f);
	EXPECT_NEAR(Defaults.m_CompressorRatio, 2.0f, 0.001f);
	EXPECT_NEAR(Defaults.m_CompressorAttackSec, 0.012f, 0.001f);
	EXPECT_NEAR(Defaults.m_CompressorReleaseSec, 0.140f, 0.001f);
	EXPECT_NEAR(Defaults.m_CompressorMakeupGain, 1.25f, 0.001f);
	EXPECT_NEAR(Defaults.m_Limiter, 0.92f, 0.001f);
	EXPECT_EQ(Defaults.m_EncoderComplexity, 8);
}
