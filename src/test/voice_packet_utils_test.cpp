// VoiceUtils voice_packet_utils_test.cpp 行为测试。
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

TEST(VoiceUtils, WriteReadVoicePacketHeader)
{
	SVoicePacketHeader Header;
	Header.m_Version = 3;
	Header.m_Type = VOICE_TYPE_AUDIO;
	Header.m_PayloadSize = 123;
	Header.m_ContextHash = 0x12345678u;
	Header.m_TokenHash = 0xAABBCCDDu;
	Header.m_Flags = VOICE_FLAG_VAD | VOICE_FLAG_LOOPBACK;
	Header.m_SenderId = 42;
	Header.m_Sequence = 65530;
	Header.m_PosX = 321.5f;
	Header.m_PosY = -654.25f;

	uint8_t aBuf[VOICE_PACKET_HEADER_SIZE + 123] = {};
	ASSERT_TRUE(WriteVoicePacketHeader(aBuf, sizeof(aBuf), Header));

	SVoicePacketHeader Parsed;
	ASSERT_TRUE(ReadVoicePacketHeader(aBuf, sizeof(aBuf), Parsed));
	EXPECT_EQ(Parsed.m_Version, Header.m_Version);
	EXPECT_EQ(Parsed.m_Type, Header.m_Type);
	EXPECT_EQ(Parsed.m_PayloadSize, Header.m_PayloadSize);
	EXPECT_EQ(Parsed.m_ContextHash, Header.m_ContextHash);
	EXPECT_EQ(Parsed.m_TokenHash, Header.m_TokenHash);
	EXPECT_EQ(Parsed.m_Flags, Header.m_Flags);
	EXPECT_EQ(Parsed.m_SenderId, Header.m_SenderId);
	EXPECT_EQ(Parsed.m_Sequence, Header.m_Sequence);
	EXPECT_FLOAT_EQ(Parsed.m_PosX, Header.m_PosX);
	EXPECT_FLOAT_EQ(Parsed.m_PosY, Header.m_PosY);
}

TEST(VoiceUtils, WriteVoicePacketHeaderMatchesExactAudioVector)
{
	SVoicePacketHeader Header;
	Header.m_Version = 3;
	Header.m_Type = VOICE_TYPE_AUDIO;
	Header.m_PayloadSize = 0x1234;
	Header.m_ContextHash = 0x78563412u;
	Header.m_TokenHash = 0xDDCCBBAAu;
	Header.m_Flags = VOICE_FLAG_VAD | VOICE_FLAG_LOOPBACK;
	Header.m_SenderId = 0x2244;
	Header.m_Sequence = 0x6688;
	Header.m_PosX = 1.5f;
	Header.m_PosY = -2.25f;

	uint8_t aBuf[VOICE_PACKET_HEADER_SIZE];
	ASSERT_TRUE(WriteVoicePacketHeader(aBuf, sizeof(aBuf), Header));

	const uint8_t aExpected[VOICE_PACKET_HEADER_SIZE] = {
		'R',
		'V',
		'0',
		'1',
		0x03,
		0x01,
		0x34,
		0x12,
		0x12,
		0x34,
		0x56,
		0x78,
		0xAA,
		0xBB,
		0xCC,
		0xDD,
		0x03,
		0x44,
		0x22,
		0x88,
		0x66,
		0x00,
		0x00,
		0xC0,
		0x3F,
		0x00,
		0x00,
		0x10,
		0xC0,
	};
	EXPECT_EQ(mem_comp(aBuf, aExpected, sizeof(aExpected)), 0);
}

TEST(VoiceUtils, WriteVoicePacketHeaderMatchesExactPingVector)
{
	SVoicePacketHeader Header;
	Header.m_Version = 7;
	Header.m_Type = VOICE_TYPE_PING;
	Header.m_PayloadSize = 0;
	Header.m_ContextHash = 0x01020304u;
	Header.m_TokenHash = 0;
	Header.m_Flags = 0;
	Header.m_SenderId = 9;
	Header.m_Sequence = 10;
	Header.m_PosX = 0.0f;
	Header.m_PosY = 0.0f;

	uint8_t aBuf[VOICE_PACKET_HEADER_SIZE];
	ASSERT_TRUE(WriteVoicePacketHeader(aBuf, sizeof(aBuf), Header));

	const uint8_t aExpected[VOICE_PACKET_HEADER_SIZE] = {
		'R',
		'V',
		'0',
		'1',
		0x07,
		0x02,
		0x00,
		0x00,
		0x04,
		0x03,
		0x02,
		0x01,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x09,
		0x00,
		0x0A,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};
	EXPECT_EQ(mem_comp(aBuf, aExpected, sizeof(aExpected)), 0);
}

TEST(VoiceUtils, WriteVoicePacketHeaderMatchesExactPongVector)
{
	SVoicePacketHeader Header;
	Header.m_Version = 5;
	Header.m_Type = VOICE_TYPE_PONG;
	Header.m_PayloadSize = 0x0004;
	Header.m_ContextHash = 0x44332211u;
	Header.m_TokenHash = 0x04030201u;
	Header.m_Flags = VOICE_FLAG_LOOPBACK;
	Header.m_SenderId = 0x1234;
	Header.m_Sequence = 0xABCD;
	Header.m_PosX = -3.5f;
	Header.m_PosY = 9.25f;

	uint8_t aBuf[VOICE_PACKET_HEADER_SIZE];
	ASSERT_TRUE(WriteVoicePacketHeader(aBuf, sizeof(aBuf), Header));

	const uint8_t aExpected[VOICE_PACKET_HEADER_SIZE] = {
		'R',
		'V',
		'0',
		'1',
		0x05,
		0x03,
		0x04,
		0x00,
		0x11,
		0x22,
		0x33,
		0x44,
		0x01,
		0x02,
		0x03,
		0x04,
		0x02,
		0x34,
		0x12,
		0xCD,
		0xAB,
		0x00,
		0x00,
		0x60,
		0xC0,
		0x00,
		0x00,
		0x14,
		0x41,
	};
	EXPECT_EQ(mem_comp(aBuf, aExpected, sizeof(aExpected)), 0);
}

TEST(VoiceUtils, WriteReadVoicePacketHeaderKeepsContextTokenAndSender)
{
	SVoicePacketHeader Header;
	Header.m_Version = VOICE_VERSION;
	Header.m_Type = VOICE_TYPE_AUDIO;
	Header.m_PayloadSize = 32;
	Header.m_ContextHash = 0xCAFEBABEu;
	Header.m_TokenHash = 0x0BADF00Du;
	Header.m_Flags = VOICE_FLAG_VAD;
	Header.m_SenderId = 63;
	Header.m_Sequence = 777;
	Header.m_PosX = 64.0f;
	Header.m_PosY = -48.0f;

	uint8_t aBuf[VOICE_PACKET_HEADER_SIZE + 32] = {};
	ASSERT_TRUE(WriteVoicePacketHeader(aBuf, sizeof(aBuf), Header));

	SVoicePacketHeader Parsed;
	ASSERT_TRUE(ReadVoicePacketHeader(aBuf, sizeof(aBuf), Parsed));
	EXPECT_EQ(Parsed.m_ContextHash, Header.m_ContextHash);
	EXPECT_EQ(Parsed.m_TokenHash, Header.m_TokenHash);
	EXPECT_EQ(Parsed.m_SenderId, Header.m_SenderId);
	EXPECT_EQ(Parsed.m_Sequence, Header.m_Sequence);
}

TEST(VoiceUtils, VoicePacketTypeNameReturnsExpectedNames)
{
	EXPECT_STREQ(VoicePacketTypeName(VOICE_TYPE_AUDIO), "audio");
	EXPECT_STREQ(VoicePacketTypeName(VOICE_TYPE_PING), "ping");
	EXPECT_STREQ(VoicePacketTypeName(VOICE_TYPE_PONG), "pong");
	EXPECT_STREQ(VoicePacketTypeName(99), "unknown");
}

TEST(VoiceUtils, ReadVoicePacketHeaderRejectsBadMagic)
{
	uint8_t aBuf[VOICE_PACKET_HEADER_SIZE];
	mem_zero(aBuf, sizeof(aBuf));
	aBuf[0] = 'N';
	aBuf[1] = 'O';
	aBuf[2] = 'P';
	aBuf[3] = 'E';

	SVoicePacketHeader Parsed;
	EXPECT_FALSE(ReadVoicePacketHeader(aBuf, sizeof(aBuf), Parsed));
}

TEST(VoiceUtils, ReadVoicePacketHeaderRejectsTruncatedBuffer)
{
	uint8_t aBuf[VOICE_PACKET_HEADER_SIZE];
	mem_zero(aBuf, sizeof(aBuf));

	SVoicePacketHeader Header;
	Header.m_Version = 3;
	Header.m_Type = VOICE_TYPE_PING;
	ASSERT_TRUE(WriteVoicePacketHeader(aBuf, sizeof(aBuf), Header));

	SVoicePacketHeader Parsed;
	EXPECT_FALSE(ReadVoicePacketHeader(aBuf, VOICE_PACKET_HEADER_SIZE - 1, Parsed));
}

TEST(VoiceUtils, ReadVoicePacketHeaderRejectsNullBuffer)
{
	SVoicePacketHeader Parsed;
	EXPECT_FALSE(ReadVoicePacketHeader(nullptr, VOICE_PACKET_HEADER_SIZE, Parsed));
}

TEST(VoiceUtils, ReadVoicePacketHeaderRejectsZeroSize)
{
	uint8_t aBuf[VOICE_PACKET_HEADER_SIZE];
	mem_zero(aBuf, sizeof(aBuf));

	SVoicePacketHeader Parsed;
	EXPECT_FALSE(ReadVoicePacketHeader(aBuf, 0, Parsed));
}

TEST(VoiceUtils, WriteVoicePacketHeaderRejectsNullBuffer)
{
	SVoicePacketHeader Header;
	Header.m_Version = 3;
	Header.m_Type = VOICE_TYPE_AUDIO;
	EXPECT_FALSE(WriteVoicePacketHeader(nullptr, VOICE_PACKET_HEADER_SIZE, Header));
}

TEST(VoiceUtils, WriteVoicePacketHeaderRejectsInsufficientSize)
{
	SVoicePacketHeader Header;
	Header.m_Version = 3;
	Header.m_Type = VOICE_TYPE_AUDIO;
	uint8_t aBuf[VOICE_PACKET_HEADER_SIZE];
	EXPECT_FALSE(WriteVoicePacketHeader(aBuf, VOICE_PACKET_HEADER_SIZE - 1, Header));
}
