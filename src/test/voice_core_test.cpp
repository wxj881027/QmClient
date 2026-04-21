#include "test.h"

#include <game/client/components/qmclient/voice_utils.h>
#include <base/system.h>
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

// ---------------------------------------------------------------------------
// SeqDelta / SeqLess  (re-implement static logic from voice_core.cpp)
// ---------------------------------------------------------------------------

static int TestSeqDelta(uint16_t NewSeq, uint16_t OldSeq)
{
	return (int)(int16_t)(NewSeq - OldSeq);
}

static bool TestSeqLess(uint16_t A, uint16_t B)
{
	return (int16_t)(A - B) < 0;
}

TEST(VoiceCore, SeqDeltaNormal)
{
	EXPECT_EQ(TestSeqDelta(10, 5), 5);
	EXPECT_EQ(TestSeqDelta(100, 0), 100);
	EXPECT_EQ(TestSeqDelta(5, 10), -5);
	EXPECT_EQ(TestSeqDelta(0, 100), -100);
}

TEST(VoiceCore, SeqDeltaSame)
{
	EXPECT_EQ(TestSeqDelta(42, 42), 0);
	EXPECT_EQ(TestSeqDelta(0, 0), 0);
	EXPECT_EQ(TestSeqDelta(65535, 65535), 0);
}

TEST(VoiceCore, SeqDeltaWrapForward)
{
	EXPECT_EQ(TestSeqDelta(5, 65530), 11);
	EXPECT_EQ(TestSeqDelta(0, 65535), 1);
	EXPECT_EQ(TestSeqDelta(100, 65436), 200);
}

TEST(VoiceCore, SeqDeltaWrapBackward)
{
	EXPECT_EQ(TestSeqDelta(65530, 5), -11);
	EXPECT_EQ(TestSeqDelta(65535, 0), -1);
	EXPECT_EQ(TestSeqDelta(65436, 100), -200);
}

TEST(VoiceCore, SeqLessNormal)
{
	EXPECT_TRUE(TestSeqLess(5, 10));
	EXPECT_TRUE(TestSeqLess(0, 1));
	EXPECT_FALSE(TestSeqLess(10, 5));
	EXPECT_FALSE(TestSeqLess(1, 0));
}

TEST(VoiceCore, SeqLessEqual)
{
	EXPECT_FALSE(TestSeqLess(42, 42));
	EXPECT_FALSE(TestSeqLess(0, 0));
	EXPECT_FALSE(TestSeqLess(65535, 65535));
}

TEST(VoiceCore, SeqLessWrap)
{
	EXPECT_TRUE(TestSeqLess(65530, 5));
	EXPECT_TRUE(TestSeqLess(65535, 1));
	EXPECT_FALSE(TestSeqLess(5, 65530));
	EXPECT_FALSE(TestSeqLess(1, 65535));
}

TEST(VoiceCore, SeqLessHalfWrap)
{
	// 32768 = 0x8000 wraps to -32768 as int16_t
	// Both directions return true at the exact halfway point (ambiguous)
	EXPECT_TRUE(TestSeqLess(0, 32768));
	EXPECT_TRUE(TestSeqLess(32768, 0));
	// Just past halfway: 32769 = 0x8001 wraps to -32767
	EXPECT_FALSE(TestSeqLess(0, 32769));
	EXPECT_TRUE(TestSeqLess(32769, 0));
}

// ---------------------------------------------------------------------------
// ClampJitterTarget  (re-implement static logic from voice_core.cpp)
// ---------------------------------------------------------------------------

static int TestClampJitterTarget(float JitterMs)
{
	if(JitterMs <= 8.0f)
		return 2;
	if(JitterMs <= 14.0f)
		return 3;
	if(JitterMs <= 22.0f)
		return 4;
	if(JitterMs <= 32.0f)
		return 5;
	return 6;
}

TEST(VoiceCore, ClampJitterTargetLow)
{
	EXPECT_EQ(TestClampJitterTarget(0.0f), 2);
	EXPECT_EQ(TestClampJitterTarget(5.0f), 2);
	EXPECT_EQ(TestClampJitterTarget(8.0f), 2);
}

TEST(VoiceCore, ClampJitterTargetMid)
{
	EXPECT_EQ(TestClampJitterTarget(10.0f), 3);
	EXPECT_EQ(TestClampJitterTarget(14.0f), 3);
	EXPECT_EQ(TestClampJitterTarget(18.0f), 4);
	EXPECT_EQ(TestClampJitterTarget(22.0f), 4);
	EXPECT_EQ(TestClampJitterTarget(28.0f), 5);
	EXPECT_EQ(TestClampJitterTarget(32.0f), 5);
}

TEST(VoiceCore, ClampJitterTargetHigh)
{
	EXPECT_EQ(TestClampJitterTarget(33.0f), 6);
	EXPECT_EQ(TestClampJitterTarget(100.0f), 6);
	EXPECT_EQ(TestClampJitterTarget(1000.0f), 6);
}

// ---------------------------------------------------------------------------
// ProcessIncoming PayloadSize=0 regression test
// ---------------------------------------------------------------------------

static constexpr char TestVoiceMagic[4] = {'R', 'V', '0', '1'};
static constexpr uint8_t TestVoiceTypeAudio = 1;
static constexpr int TestVoiceHeaderSize = sizeof(TestVoiceMagic) + 1 + 1 + 2 + 4 + 4 + 1 + 2 + 2 + 4 + 4;

static size_t BuildVoicePacket(uint8_t *pBuf, uint8_t Version, uint8_t Type, uint16_t PayloadSize,
	uint32_t ContextHash, uint32_t TokenHash, uint8_t Flags, uint16_t SenderId, uint16_t Sequence,
	float PosX, float PosY, const uint8_t *pPayload = nullptr)
{
	size_t Offset = 0;
	mem_copy(pBuf + Offset, TestVoiceMagic, sizeof(TestVoiceMagic));
	Offset += sizeof(TestVoiceMagic);
	pBuf[Offset++] = Version;
	pBuf[Offset++] = Type;
	VoiceUtils::WriteU16(pBuf + Offset, PayloadSize);
	Offset += sizeof(uint16_t);
	VoiceUtils::WriteU32(pBuf + Offset, ContextHash);
	Offset += sizeof(uint32_t);
	VoiceUtils::WriteU32(pBuf + Offset, TokenHash);
	Offset += sizeof(uint32_t);
	pBuf[Offset++] = Flags;
	VoiceUtils::WriteU16(pBuf + Offset, SenderId);
	Offset += sizeof(uint16_t);
	VoiceUtils::WriteU16(pBuf + Offset, Sequence);
	Offset += sizeof(uint16_t);
	VoiceUtils::WriteFloat(pBuf + Offset, PosX);
	Offset += sizeof(float);
	VoiceUtils::WriteFloat(pBuf + Offset, PosY);
	Offset += sizeof(float);
	if(PayloadSize > 0 && pPayload)
	{
		mem_copy(pBuf + Offset, pPayload, PayloadSize);
		Offset += PayloadSize;
	}
	return Offset;
}

static bool ParseVoicePacketPayloadSize(const uint8_t *pData, int Bytes, uint16_t &OutPayloadSize)
{
	if(!pData || Bytes < TestVoiceHeaderSize)
		return false;

	size_t Offset = 0;
	if(mem_comp(pData, TestVoiceMagic, sizeof(TestVoiceMagic)) != 0)
		return false;
	Offset += sizeof(TestVoiceMagic);

	Offset++; // Version
	Offset++; // Type

	OutPayloadSize = VoiceUtils::ReadU16(pData + Offset);
	return true;
}

static bool ShouldProcessPayload(uint16_t PayloadSize, size_t Offset, int Bytes)
{
	if(PayloadSize == 0)
		return false;
	if(Offset + PayloadSize > (size_t)Bytes)
		return false;
	return true;
}

TEST(VoiceCore, ProcessIncomingZeroPayload)
{
	uint8_t aPacket[1200];
	const size_t PacketSize = BuildVoicePacket(aPacket, 3, TestVoiceTypeAudio,
		0, 0x12345678u, 0u, 0, 1, 100, 50.0f, 50.0f, nullptr);

	uint16_t PayloadSize = 0;
	ASSERT_TRUE(ParseVoicePacketPayloadSize(aPacket, (int)PacketSize, PayloadSize));
	EXPECT_EQ(PayloadSize, 0);

	EXPECT_FALSE(ShouldProcessPayload(PayloadSize, TestVoiceHeaderSize, (int)PacketSize));
}

TEST(VoiceCore, ProcessIncomingNormalPayload)
{
	uint8_t aPayload[64];
	mem_zero(aPayload, sizeof(aPayload));
	aPayload[0] = 0xFF;

	uint8_t aPacket[1200];
	const size_t PacketSize = BuildVoicePacket(aPacket, 3, TestVoiceTypeAudio,
		64, 0x12345678u, 0u, 0, 1, 100, 50.0f, 50.0f, aPayload);

	uint16_t PayloadSize = 0;
	ASSERT_TRUE(ParseVoicePacketPayloadSize(aPacket, (int)PacketSize, PayloadSize));
	EXPECT_EQ(PayloadSize, 64);

	EXPECT_TRUE(ShouldProcessPayload(PayloadSize, TestVoiceHeaderSize, (int)PacketSize));
}

TEST(VoiceCore, ProcessIncomingTruncatedPayload)
{
	uint8_t aPacket[1200];
	const size_t PacketSize = BuildVoicePacket(aPacket, 3, TestVoiceTypeAudio,
		200, 0x12345678u, 0u, 0, 1, 100, 50.0f, 50.0f, nullptr);

	uint16_t PayloadSize = 0;
	ASSERT_TRUE(ParseVoicePacketPayloadSize(aPacket, (int)PacketSize, PayloadSize));
	EXPECT_EQ(PayloadSize, 200);

	EXPECT_FALSE(ShouldProcessPayload(PayloadSize, TestVoiceHeaderSize, (int)PacketSize));
}

TEST(VoiceCore, ProcessIncomingBadMagic)
{
	uint8_t aPacket[1200];
	mem_zero(aPacket, sizeof(aPacket));
	aPacket[0] = 'X';
	aPacket[1] = 'Y';
	aPacket[2] = 'Z';
	aPacket[3] = '!';

	uint16_t PayloadSize = 999;
	EXPECT_FALSE(ParseVoicePacketPayloadSize(aPacket, (int)sizeof(aPacket), PayloadSize));
}

TEST(VoiceCore, ProcessIncomingTooSmall)
{
	uint8_t aPacket[4];
	mem_zero(aPacket, sizeof(aPacket));

	uint16_t PayloadSize = 999;
	EXPECT_FALSE(ParseVoicePacketPayloadSize(aPacket, (int)sizeof(aPacket), PayloadSize));
}

// ---------------------------------------------------------------------------
// VAD state machine test
// ---------------------------------------------------------------------------

struct SVadState
{
	bool m_Active = false;
	int64_t m_ReleaseDeadline = 0;
};

static void VadUpdate(SVadState &State, bool Trigger, int64_t FrameNow, int64_t ReleaseTicks)
{
	if(Trigger)
	{
		State.m_Active = true;
		if(ReleaseTicks > 0)
			State.m_ReleaseDeadline = FrameNow + ReleaseTicks;
		else
			State.m_ReleaseDeadline = 0;
	}
	else if(State.m_Active)
	{
		if(State.m_ReleaseDeadline == 0 || FrameNow >= State.m_ReleaseDeadline)
		{
			State.m_Active = false;
			State.m_ReleaseDeadline = 0;
		}
	}
}

TEST(VoiceCore, VadTriggerActivates)
{
	SVadState State;
	EXPECT_FALSE(State.m_Active);

	VadUpdate(State, true, 1000, 500);
	EXPECT_TRUE(State.m_Active);
	EXPECT_EQ(State.m_ReleaseDeadline, 1500);
}

TEST(VoiceCore, VadTriggerWhileActiveExtendsDeadline)
{
	SVadState State;
	VadUpdate(State, true, 1000, 500);
	EXPECT_EQ(State.m_ReleaseDeadline, 1500);

	VadUpdate(State, true, 2000, 500);
	EXPECT_TRUE(State.m_Active);
	EXPECT_EQ(State.m_ReleaseDeadline, 2500);
}

TEST(VoiceCore, VadNoTriggerStaysInactive)
{
	SVadState State;
	VadUpdate(State, false, 1000, 500);
	EXPECT_FALSE(State.m_Active);
}

TEST(VoiceCore, VadReleaseDelayKeepsActive)
{
	SVadState State;
	VadUpdate(State, true, 1000, 500);
	EXPECT_TRUE(State.m_Active);

	VadUpdate(State, false, 1200, 500);
	EXPECT_TRUE(State.m_Active);
}

TEST(VoiceCore, VadReleaseDelayExpires)
{
	SVadState State;
	VadUpdate(State, true, 1000, 500);
	EXPECT_TRUE(State.m_Active);

	VadUpdate(State, false, 1499, 500);
	EXPECT_TRUE(State.m_Active);

	VadUpdate(State, false, 1500, 500);
	EXPECT_FALSE(State.m_Active);
	EXPECT_EQ(State.m_ReleaseDeadline, 0);
}

TEST(VoiceCore, VadZeroReleaseDelayDeactivatesImmediately)
{
	SVadState State;
	VadUpdate(State, true, 1000, 0);
	EXPECT_TRUE(State.m_Active);
	EXPECT_EQ(State.m_ReleaseDeadline, 0);

	VadUpdate(State, false, 1001, 0);
	EXPECT_FALSE(State.m_Active);
}

TEST(VoiceCore, VadReactivatesAfterRelease)
{
	SVadState State;
	VadUpdate(State, true, 1000, 500);
	VadUpdate(State, false, 1500, 500);
	EXPECT_FALSE(State.m_Active);

	VadUpdate(State, true, 2000, 500);
	EXPECT_TRUE(State.m_Active);
	EXPECT_EQ(State.m_ReleaseDeadline, 2500);
}

TEST(VoiceCore, VadTriggerWithZeroThreshold)
{
	SVadState State;
	const bool Trigger = true;
	VadUpdate(State, Trigger, 1000, 500);
	EXPECT_TRUE(State.m_Active);
}
