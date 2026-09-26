// VoiceUtils voice_processing_utils_test.cpp 行为测试。
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

TEST(VoiceUtils, ResolveNoiseSuppressModeDisabled)
{
	bool FallbackUsed = true;
	const int Mode = ResolveNoiseSuppressMode(TEST_VOICE_NOISE_SUPPRESS_OFF, false, &FallbackUsed);
	EXPECT_EQ(Mode, TEST_VOICE_NOISE_SUPPRESS_OFF);
	EXPECT_FALSE(FallbackUsed);
}

TEST(VoiceUtils, ResolveNoiseSuppressModeSimple)
{
	bool FallbackUsed = true;
	const int Mode = ResolveNoiseSuppressMode(TEST_VOICE_NOISE_SUPPRESS_SIMPLE, false, &FallbackUsed);
	EXPECT_EQ(Mode, TEST_VOICE_NOISE_SUPPRESS_SIMPLE);
	EXPECT_FALSE(FallbackUsed);
}

TEST(VoiceUtils, ResolveNoiseSuppressModeRnnoiseWhenAvailable)
{
	bool FallbackUsed = false;
	const int Mode = ResolveNoiseSuppressMode(TEST_VOICE_NOISE_SUPPRESS_RNNOISE, true, &FallbackUsed);
	EXPECT_EQ(Mode, TEST_VOICE_NOISE_SUPPRESS_RNNOISE);
	EXPECT_FALSE(FallbackUsed);
}

TEST(VoiceUtils, ResolveNoiseSuppressModeFallbackToSimpleWhenRnnoiseUnavailable)
{
	bool FallbackUsed = false;
	const int Mode = ResolveNoiseSuppressMode(TEST_VOICE_NOISE_SUPPRESS_RNNOISE, false, &FallbackUsed);
	EXPECT_EQ(Mode, TEST_VOICE_NOISE_SUPPRESS_SIMPLE);
	EXPECT_TRUE(FallbackUsed);
}

TEST(VoiceUtils, ResolveNoiseSuppressModeInvalidValue)
{
	bool FallbackUsed = false;
	const int Mode = ResolveNoiseSuppressMode(99, true, &FallbackUsed);
	EXPECT_EQ(Mode, TEST_VOICE_NOISE_SUPPRESS_RNNOISE);
	EXPECT_FALSE(FallbackUsed);
}

TEST(VoiceUtils, ResolveNoiseSuppressModeNegativeValue)
{
	bool FallbackUsed = false;
	const int Mode = ResolveNoiseSuppressMode(-1, true, &FallbackUsed);
	EXPECT_EQ(Mode, TEST_VOICE_NOISE_SUPPRESS_OFF);
	EXPECT_FALSE(FallbackUsed);
}

TEST(VoiceUtils, ResolveNoiseSuppressModeNullFallbackPointer)
{
	const int Mode = ResolveNoiseSuppressMode(TEST_VOICE_NOISE_SUPPRESS_RNNOISE, false, nullptr);
	EXPECT_EQ(Mode, TEST_VOICE_NOISE_SUPPRESS_SIMPLE);
}

TEST(VoiceUtils, RnnoiseIsCompiledIn)
{
	EXPECT_TRUE(IsRnnoiseCompiledIn());
}

#if defined(CONF_RNNOISE)

TEST(VoiceUtils, RnnoiseProcessesSilenceFrame)
{
	DenoiseState *pState = rnnoise_create(nullptr);
	ASSERT_NE(pState, nullptr);

	const int FrameSize = rnnoise_get_frame_size();
	ASSERT_GT(FrameSize, 0);

	std::vector<float> vInput(FrameSize, 0.0f);
	std::vector<float> vOutput(FrameSize, 1.0f);
	const float VadProbability = rnnoise_process_frame(pState, vOutput.data(), vInput.data());

	EXPECT_TRUE(std::isfinite(VadProbability));
	for(float Sample : vOutput)
		EXPECT_TRUE(std::isfinite(Sample));

	rnnoise_destroy(pState);
}
#endif

TEST(VoiceUtils, ComputeVoiceEncoderTargetsHealthyNetwork)
{
	int TargetBitrate = 0;
	int TargetLoss = 0;
	bool TargetFec = true;
	ComputeVoiceEncoderTargets(0, 0.0f, 0, &TargetBitrate, &TargetLoss, &TargetFec);
	EXPECT_EQ(TargetBitrate, 64000);
	EXPECT_EQ(TargetLoss, 0);
	EXPECT_FALSE(TargetFec);
}

TEST(VoiceUtils, VoiceProcessingFactoryDefaultsDisableNoiseSuppressByDefault)
{
	const auto Defaults = VoiceProcessingFactoryDefaults();
	EXPECT_EQ(Defaults.m_NoiseSuppressMode, VOICE_NOISE_SUPPRESS_OFF);
	EXPECT_EQ(Defaults.m_NoiseSuppressStrength, 35);
	EXPECT_EQ(Defaults.m_EncoderComplexity, 8);
}

TEST(VoiceUtils, ComputeVoiceEncoderTargetsWithComplexityHealthyNetworkKeepsHighQuality)
{
	int TargetBitrate = 0;
	int TargetLoss = 0;
	bool TargetFec = true;
	int TargetComplexity = 0;
	ComputeVoiceEncoderTargetsWithComplexity(0, 0.0f, 0, &TargetBitrate, &TargetLoss, &TargetFec, &TargetComplexity);
	EXPECT_EQ(TargetBitrate, 64000);
	EXPECT_EQ(TargetLoss, 0);
	EXPECT_FALSE(TargetFec);
	EXPECT_EQ(TargetComplexity, 8);
}

TEST(VoiceUtils, ComputeVoiceAutoGainRaisesQuietFramesButHonorsMaxGain)
{
	const auto Config = VoiceAgcConfigFromRuntime(true);
	const float Next = ComputeVoiceAutoGain(1.0f, 0.05f, Config);
	EXPECT_GT(Next, 1.0f);
	EXPECT_LE(Next, Config.m_MaxGain);
}

TEST(VoiceUtils, ComputeVoiceEncoderTargetsWithComplexityBackwardCompatibleWithOldFunction)
{
	int TargetBitrateOld = 0;
	int TargetLossOld = 0;
	bool TargetFecOld = false;
	ComputeVoiceEncoderTargets(5, 10.0f, 0, &TargetBitrateOld, &TargetLossOld, &TargetFecOld);

	int TargetBitrateNew = 0;
	int TargetLossNew = 0;
	bool TargetFecNew = false;
	int TargetComplexityNew = 0;
	ComputeVoiceEncoderTargetsWithComplexity(5, 10.0f, 0, &TargetBitrateNew, &TargetLossNew, &TargetFecNew, &TargetComplexityNew);

	EXPECT_EQ(TargetBitrateOld, TargetBitrateNew);
	EXPECT_EQ(TargetLossOld, TargetLossNew);
	EXPECT_EQ(TargetFecOld, TargetFecNew);
}

TEST(VoiceUtils, ComputeVoiceAutoGainFallsBackTowardUnityForLoudFrames)
{
	const auto Config = VoiceAgcConfigFromRuntime(true);
	const float Next = ComputeVoiceAutoGain(1.8f, 0.35f, Config);
	EXPECT_LT(Next, 1.8f);
	EXPECT_GE(Next, Config.m_MinGain);
}

TEST(VoiceUtils, ComputeVoiceAutoGainDisabledReturnsUnity)
{
	const auto Config = VoiceAgcConfigFromRuntime(false);
	const float Next = ComputeVoiceAutoGain(1.5f, 0.05f, Config);
	EXPECT_FLOAT_EQ(Next, 1.0f);
}

TEST(VoiceUtils, ComputeVoiceAutoGainAttackAndReleaseAffectSlewRate)
{
	SVoiceAgcConfig FastConfig = VoiceAgcConfigFromRuntime(true);
	FastConfig.m_AttackSec = 0.02f;
	FastConfig.m_ReleaseSec = 0.10f;

	SVoiceAgcConfig SlowConfig = VoiceAgcConfigFromRuntime(true);
	SlowConfig.m_AttackSec = 0.20f;
	SlowConfig.m_ReleaseSec = 0.80f;

	const float FastRaise = ComputeVoiceAutoGain(1.0f, 0.05f, FastConfig);
	const float SlowRaise = ComputeVoiceAutoGain(1.0f, 0.05f, SlowConfig);
	EXPECT_GT(FastRaise, SlowRaise);

	const float FastFall = ComputeVoiceAutoGain(1.8f, 0.35f, FastConfig);
	const float SlowFall = ComputeVoiceAutoGain(1.8f, 0.35f, SlowConfig);
	EXPECT_LT(FastFall, SlowFall);
}

TEST(VoiceUtils, ComputeVoiceEncoderTargetsKeepsMoreBitrateBeforeWeakNetwork)
{
	int TargetBitrate = 0;
	int TargetLoss = 0;
	bool TargetFec = false;
	ComputeVoiceEncoderTargets(5, 12.0f, 0, &TargetBitrate, &TargetLoss, &TargetFec);
	EXPECT_EQ(TargetBitrate, 48000);
	EXPECT_EQ(TargetLoss, 5);
	EXPECT_TRUE(TargetFec);
}

TEST(VoiceUtils, ComputeVoiceEncoderTargetsWeakNetwork)
{
	int TargetBitrate = 0;
	int TargetLoss = 0;
	bool TargetFec = false;
	ComputeVoiceEncoderTargets(10, 20.0f, 0, &TargetBitrate, &TargetLoss, &TargetFec);
	EXPECT_EQ(TargetBitrate, 32000);
	EXPECT_EQ(TargetLoss, 10);
	EXPECT_TRUE(TargetFec);
}

TEST(VoiceUtils, ComputeVoiceEncoderTargetsPoorNetwork)
{
	int TargetBitrate = 0;
	int TargetLoss = 0;
	bool TargetFec = false;
	ComputeVoiceEncoderTargets(15, 35.0f, 0, &TargetBitrate, &TargetLoss, &TargetFec);
	EXPECT_EQ(TargetBitrate, 24000);
	EXPECT_EQ(TargetLoss, 20);
	EXPECT_TRUE(TargetFec);
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
