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

	uint8_t aBuf[VOICE_PACKET_HEADER_SIZE];
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

	uint8_t aBuf[VOICE_PACKET_HEADER_SIZE];
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

TEST(VoiceUtils, VoiceTransmitBlockersNetworkAndDevice)
{
	SVoiceTransmitPreconditions Preconditions;
	Preconditions.m_NeedNetwork = true;

	const uint32_t Blockers = VoiceTransmitBlockers(Preconditions);
	EXPECT_NE(Blockers & VOICE_TX_BLOCK_SERVER_ADDR, 0u);
	EXPECT_NE(Blockers & VOICE_TX_BLOCK_SOCKET, 0u);
	EXPECT_NE(Blockers & VOICE_TX_BLOCK_ONLINE, 0u);
	EXPECT_NE(Blockers & VOICE_TX_BLOCK_CAPTURE, 0u);
	EXPECT_NE(Blockers & VOICE_TX_BLOCK_ENCODER, 0u);
}

TEST(VoiceUtils, VoiceTransmitBlockersLocalTestIgnoresNetwork)
{
	SVoiceTransmitPreconditions Preconditions;
	Preconditions.m_NeedNetwork = false;
	Preconditions.m_HaveCaptureDevice = true;
	Preconditions.m_HaveEncoder = true;

	const uint32_t Blockers = VoiceTransmitBlockers(Preconditions);
	EXPECT_EQ(Blockers & VOICE_TX_BLOCK_SERVER_ADDR, 0u);
	EXPECT_EQ(Blockers & VOICE_TX_BLOCK_SOCKET, 0u);
	EXPECT_EQ(Blockers & VOICE_TX_BLOCK_ONLINE, 0u);
	EXPECT_EQ(Blockers, 0u);
}

TEST(VoiceUtils, VoiceTransmitBlockersMicMutedIsReportedSeparately)
{
	SVoiceTransmitPreconditions Preconditions;
	Preconditions.m_NeedNetwork = true;
	Preconditions.m_ServerAddrValid = true;
	Preconditions.m_HaveSocket = true;
	Preconditions.m_Online = true;
	Preconditions.m_HaveCaptureDevice = true;
	Preconditions.m_HaveEncoder = true;
	Preconditions.m_MicMuted = true;

	const uint32_t Blockers = VoiceTransmitBlockers(Preconditions);
	EXPECT_EQ(Blockers, VOICE_TX_BLOCK_MIC_MUTED);
}

TEST(VoiceUtils, FormatVoiceTransmitBlockersEmpty)
{
	char aBuf[64];
	FormatVoiceTransmitBlockers(0, aBuf, (int)sizeof(aBuf));
	EXPECT_STREQ(aBuf, "none");
}

TEST(VoiceUtils, FormatVoiceTransmitBlockersListsReasonsInStableOrder)
{
	char aBuf[128];
	const uint32_t Blockers =
		VOICE_TX_BLOCK_SERVER_ADDR |
		VOICE_TX_BLOCK_SOCKET |
		VOICE_TX_BLOCK_CAPTURE |
		VOICE_TX_BLOCK_MIC_MUTED;
	FormatVoiceTransmitBlockers(Blockers, aBuf, (int)sizeof(aBuf));
	EXPECT_STREQ(aBuf, "server_addr,socket,capture,mic_muted");
}

TEST(VoiceUtils, VoiceNeedsAudioRefreshWhenStereoLayoutChanges)
{
	SVoiceAudioRefreshState State;
	State.m_EncoderReady = true;
	State.m_OutputReady = true;
	State.m_CaptureReady = true;
	State.m_CurrentOutputChannels = 1;
	State.m_DesiredOutputChannels = 2;

	EXPECT_TRUE(VoiceNeedsAudioRefresh(State));
}

TEST(VoiceUtils, VoiceNeedsAudioRefreshWhenUnavailableDeviceCanRetry)
{
	SVoiceAudioRefreshState State;
	State.m_EncoderReady = true;
	State.m_OutputReady = false;
	State.m_CaptureReady = true;
	State.m_OutputUnavailable = false;

	EXPECT_TRUE(VoiceNeedsAudioRefresh(State));
}

TEST(VoiceUtils, VoiceNeedsAudioRefreshStaysIdleWhenEverythingIsReady)
{
	SVoiceAudioRefreshState State;
	State.m_EncoderReady = true;
	State.m_OutputReady = true;
	State.m_CaptureReady = true;
	State.m_CurrentOutputChannels = 2;
	State.m_DesiredOutputChannels = 2;

	EXPECT_FALSE(VoiceNeedsAudioRefresh(State));
}

TEST(VoiceUtils, VoiceRuntimeResetFlagsStayIdleWhenContextAndTokenStaySame)
{
	EXPECT_EQ(VoiceRuntimeResetFlags(false, true, 0x11u, 0x11u), 0u);
}

TEST(VoiceUtils, VoiceRuntimeResetFlagsResetPeersWhenRoomTokenChanges)
{
	EXPECT_EQ(VoiceRuntimeResetFlags(false, true, 0x11u, 0x22u), VOICE_RUNTIME_RESET_PEERS);
}

TEST(VoiceUtils, VoiceRuntimeResetFlagsResetConnectionAndPeersWhenOfflineOrContextChanges)
{
	EXPECT_EQ(VoiceRuntimeResetFlags(true, true, 0x11u, 0x11u), VOICE_RUNTIME_RESET_CONNECTION | VOICE_RUNTIME_RESET_PEERS);
	EXPECT_EQ(VoiceRuntimeResetFlags(false, false, 0x11u, 0x11u), VOICE_RUNTIME_RESET_CONNECTION | VOICE_RUNTIME_RESET_PEERS);
}

TEST(VoiceUtils, VoiceUiMicStatusReportsMutedAndUnavailable)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_MicMuted = true;
	EXPECT_STREQ(VoiceUiMicStatus(Status), "muted");

	Status.m_MicMuted = false;
	Status.m_CaptureUnavailable = true;
	EXPECT_STREQ(VoiceUiMicStatus(Status), "unavailable");
}

TEST(VoiceUtils, VoiceUiServerStatusDistinguishesLocalOfflineAndConnected)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_NeedNetwork = false;
	EXPECT_STREQ(VoiceUiServerStatus(Status), "local_test");

	Status.m_NeedNetwork = true;
	EXPECT_STREQ(VoiceUiServerStatus(Status), "offline");

	Status.m_Online = true;
	Status.m_ServerAddrValid = true;
	Status.m_HaveSocket = true;
	Status.m_PingMs = 42;
	EXPECT_STREQ(VoiceUiServerStatus(Status), "connected");
}

TEST(VoiceUtils, VoiceUiRoomAndTransportStatusReflectPeerAndTraffic)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_NeedNetwork = true;
	Status.m_Online = true;
	EXPECT_STREQ(VoiceUiRoomStatus(Status), "waiting_peer");
	EXPECT_STREQ(VoiceUiTransportStatus(Status), "idle_no_peer");

	Status.m_HaveRecentPeers = true;
	EXPECT_STREQ(VoiceUiRoomStatus(Status), "matched");
	EXPECT_STREQ(VoiceUiTransportStatus(Status), "idle_with_peer");

	Status.m_TxActive = true;
	EXPECT_STREQ(VoiceUiTransportStatus(Status), "tx_active");

	Status.m_HaveRecentRx = true;
	EXPECT_STREQ(VoiceUiTransportStatus(Status), "tx_rx_active");
}

TEST(VoiceUtils, VoiceUiActionHintPointsToNextCheck)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_CaptureUnavailable = true;
	EXPECT_STREQ(VoiceUiActionHint(Status), "check_input");

	Status.m_CaptureUnavailable = false;
	Status.m_NeedNetwork = true;
	Status.m_Online = true;
	Status.m_ServerAddrValid = false;
	EXPECT_STREQ(VoiceUiActionHint(Status), "check_server");

	Status.m_ServerAddrValid = true;
	Status.m_HaveSocket = true;
	Status.m_HaveRecentPeers = false;
	EXPECT_STREQ(VoiceUiActionHint(Status), "check_room");

	Status.m_HaveRecentPeers = true;
	Status.m_TxActive = true;
	EXPECT_STREQ(VoiceUiActionHint(Status), "wait_peer");
}

TEST(VoiceUtils, VoiceUiActionHintPrefersSpecificAudioFailureGuidance)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_CaptureUnavailable = true;
	str_copy(Status.m_aAudioError, "Failed to open capture device: kAudioHardwareNotPermittedError", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiActionHint(Status), "grant_mic_permission");

	str_copy(Status.m_aAudioError, "Input device not found: 'USB Mic'", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiActionHint(Status), "select_input_device");

	Status.m_CaptureUnavailable = false;
	Status.m_OutputUnavailable = true;
	str_copy(Status.m_aAudioError, "Output device not found: 'USB DAC'", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiActionHint(Status), "select_output_device");
}

TEST(VoiceUtils, VoiceUiRouteStatusShowsSwitchingAndSelectedDeviceResults)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_AudioRefreshPending = true;
	str_copy(Status.m_aRequestedInputDevice, "USB Mic", sizeof(Status.m_aRequestedInputDevice));
	str_copy(Status.m_aRequestedOutputDevice, "USB DAC", sizeof(Status.m_aRequestedOutputDevice));
	EXPECT_STREQ(VoiceUiInputRouteStatus(Status), "switching_selected");
	EXPECT_STREQ(VoiceUiOutputRouteStatus(Status), "switching_selected");

	Status.m_AudioRefreshPending = false;
	Status.m_CaptureReady = true;
	Status.m_OutputReady = true;
	EXPECT_STREQ(VoiceUiInputRouteStatus(Status), "using_selected");
	EXPECT_STREQ(VoiceUiOutputRouteStatus(Status), "using_selected");

	Status = {};
	Status.m_Enabled = true;
	Status.m_CaptureReady = true;
	Status.m_OutputReady = true;
	EXPECT_STREQ(VoiceUiInputRouteStatus(Status), "using_default");
	EXPECT_STREQ(VoiceUiOutputRouteStatus(Status), "using_default");
}

TEST(VoiceUtils, VoiceUiRouteStatusDistinguishesPermissionAndFailure)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_CaptureUnavailable = true;
	str_copy(Status.m_aRequestedInputDevice, "USB Mic", sizeof(Status.m_aRequestedInputDevice));
	str_copy(Status.m_aAudioError, "Failed to open capture device: kAudioHardwareNotPermittedError", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiInputRouteStatus(Status), "permission_denied");
	EXPECT_STREQ(VoiceUiAudioIssueKey(Status), "permission_denied");

	str_copy(Status.m_aAudioError, "Input device not found: 'USB Mic'", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiInputRouteStatus(Status), "selected_failed");
	EXPECT_STREQ(VoiceUiAudioIssueKey(Status), "input_device_not_found");

	Status = {};
	Status.m_Enabled = true;
	Status.m_OutputUnavailable = true;
	str_copy(Status.m_aRequestedOutputDevice, "USB DAC", sizeof(Status.m_aRequestedOutputDevice));
	str_copy(Status.m_aAudioError, "Failed to open output device: device busy", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiOutputRouteStatus(Status), "selected_failed");
	EXPECT_STREQ(VoiceUiAudioIssueKey(Status), "open_output_failed");
}

TEST(VoiceUtils, VoiceUiPrimaryErrorPrefersAudioThenNetworkThenCodec)
{
	SVoiceUiStatus Status;
	str_copy(Status.m_aCodecError, "codec", sizeof(Status.m_aCodecError));
	EXPECT_STREQ(VoiceUiPrimaryError(Status), "codec");

	str_copy(Status.m_aNetworkError, "network", sizeof(Status.m_aNetworkError));
	EXPECT_STREQ(VoiceUiPrimaryError(Status), "network");

	str_copy(Status.m_aAudioError, "audio", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiPrimaryError(Status), "audio");
}

TEST(VoiceUtils, VoiceAudioErrorLooksLikeMacPermissionDenied)
{
	EXPECT_TRUE(VoiceAudioErrorLooksLikePermissionDenied("Failed to open capture device: kAudioHardwareNotPermittedError"));
	EXPECT_TRUE(VoiceAudioErrorLooksLikePermissionDenied("Failed to open capture device: microphone access not authorized"));
	EXPECT_FALSE(VoiceAudioErrorLooksLikePermissionDenied("Failed to open capture device: device busy"));
}

TEST(VoiceUtils, ClassifyVoiceAudioIssueRecognizesDeviceFailurePaths)
{
	SVoiceUiStatus Status;

	str_copy(Status.m_aAudioError, "Input device not found: 'USB Mic'", sizeof(Status.m_aAudioError));
	EXPECT_EQ(ClassifyVoiceAudioIssue(Status), EVoiceAudioIssue::INPUT_DEVICE_NOT_FOUND);
	EXPECT_STREQ(VoiceUiAudioFailureHint(Status), "select_input_device");

	str_copy(Status.m_aAudioError, "No output devices available", sizeof(Status.m_aAudioError));
	EXPECT_EQ(ClassifyVoiceAudioIssue(Status), EVoiceAudioIssue::NO_OUTPUT_DEVICES);
	EXPECT_STREQ(VoiceUiAudioFailureHint(Status), "select_output_device");

	str_copy(Status.m_aAudioError, "Failed to open capture device: device busy", sizeof(Status.m_aAudioError));
	EXPECT_EQ(ClassifyVoiceAudioIssue(Status), EVoiceAudioIssue::OPEN_CAPTURE_FAILED);
	EXPECT_STREQ(VoiceUiAudioFailureHint(Status), "retry_input_open");

	str_copy(Status.m_aAudioError, "Failed to init audio backend 'coreaudio': unavailable", sizeof(Status.m_aAudioError));
	EXPECT_EQ(ClassifyVoiceAudioIssue(Status), EVoiceAudioIssue::BACKEND_INIT_FAILED);
	EXPECT_STREQ(VoiceUiAudioFailureHint(Status), "check_audio_backend");
}

TEST(VoiceUtils, ClassifyVoiceAudioIssueMapsMacPermissionToHint)
{
	SVoiceUiStatus Status;
	str_copy(Status.m_aAudioError, "Failed to open capture device: kAudioHardwareNotPermittedError", sizeof(Status.m_aAudioError));

	EXPECT_EQ(ClassifyVoiceAudioIssue(Status), EVoiceAudioIssue::PERMISSION_DENIED);
	EXPECT_STREQ(VoiceUiAudioFailureHint(Status), "grant_mic_permission");
}

TEST(VoiceUtils, VoiceShouldIgnoreDistanceRespectsConfigAndSharedGroup)
{
	EXPECT_TRUE(VoiceShouldIgnoreDistance(true, false, 0x11u, 0x22u));
	EXPECT_FALSE(VoiceShouldIgnoreDistance(false, false, 0x11u, 0x11u));
	EXPECT_TRUE(VoiceShouldIgnoreDistance(false, true, 0x11u, 0x11u));
	EXPECT_FALSE(VoiceShouldIgnoreDistance(false, true, 0x00u, 0x00u));
	EXPECT_FALSE(VoiceShouldIgnoreDistance(false, true, 0x11u, 0x22u));
	EXPECT_TRUE(VoiceShouldIgnoreDistance(false, true, 0x40000011u, 0x00000011u));
}

TEST(VoiceUtils, VoiceResolveListenerPositionUsesSpecPositionOnlyWhenEnabled)
{
	const vec2 LocalPos(10.0f, 20.0f);
	const vec2 SpecPos(30.0f, 40.0f);

	EXPECT_EQ(VoiceResolveListenerPosition(LocalPos, false, SpecPos, true), LocalPos);
	EXPECT_EQ(VoiceResolveListenerPosition(LocalPos, true, SpecPos, false), LocalPos);
	EXPECT_EQ(VoiceResolveListenerPosition(LocalPos, true, SpecPos, true), SpecPos);
}

TEST(VoiceUtils, EvaluateVoiceReceiveAudibilityBlocksSelfUnlessTestServer)
{
	SVoiceReceiveAudibilityContext Context;
	Context.m_IsSelf = true;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "self"), EVoiceReceiveAudibility::DROP_SELF);

	Context.m_TestServer = true;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "self"), EVoiceReceiveAudibility::ALLOW);
}

TEST(VoiceUtils, EvaluateVoiceReceiveAudibilityAppliesVisibilityRules)
{
	SVoiceReceiveAudibilityContext Context;
	Context.m_VisibilityMode = 0;
	Context.m_SenderActive = false;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_INACTIVE);

	Context.m_IgnoreDistance = true;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::ALLOW);

	Context = {};
	Context.m_VisibilityMode = 1;
	Context.m_SenderOtherTeam = true;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_OTHER_TEAM);

	Context.m_HearPeoplesInSpectate = true;
	Context.m_SenderActive = false;
	Context.m_SenderSpec = false;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::ALLOW);
}

TEST(VoiceUtils, EvaluateVoiceReceiveAudibilityAppliesMuteListsAndVad)
{
	SVoiceReceiveAudibilityContext Context;
	Context.m_SenderActive = true;
	Context.m_pMuteList = "peer";
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_MUTED);

	Context = {};
	Context.m_SenderActive = true;
	Context.m_ListMode = 1;
	Context.m_pWhitelist = "allowed";
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_NOT_WHITELISTED);
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "allowed"), EVoiceReceiveAudibility::ALLOW);

	Context = {};
	Context.m_SenderActive = true;
	Context.m_ListMode = 2;
	Context.m_pBlacklist = "peer";
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_BLACKLISTED);

	Context = {};
	Context.m_SenderActive = true;
	Context.m_SenderUsesVad = true;
	Context.m_HearVad = false;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_VAD_BLOCKED);

	Context.m_pVadAllow = "peer";
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::ALLOW);
}

TEST(VoiceUtils, VoiceIsPacketWithinAudibleRadiusRespectsDistanceAndOverride)
{
	const vec2 LocalPos(0.0f, 0.0f);
	const vec2 NearPos(16.0f, 0.0f);
	const vec2 FarPos(128.0f, 0.0f);

	EXPECT_TRUE(VoiceIsPacketWithinAudibleRadius(LocalPos, NearPos, 32.0f, false));
	EXPECT_FALSE(VoiceIsPacketWithinAudibleRadius(LocalPos, FarPos, 32.0f, false));
	EXPECT_TRUE(VoiceIsPacketWithinAudibleRadius(LocalPos, FarPos, 32.0f, true));
}

TEST(VoiceUtils, VoiceAudioDeviceConfigEqualsForIdenticalRequests)
{
	SVoiceAudioDeviceConfig Left;
	str_copy(Left.m_aBackend, "pipewire", sizeof(Left.m_aBackend));
	str_copy(Left.m_aInputDevice, "Mic A", sizeof(Left.m_aInputDevice));
	str_copy(Left.m_aOutputDevice, "Headset B", sizeof(Left.m_aOutputDevice));
	Left.m_OutputStereo = true;

	SVoiceAudioDeviceConfig Right = Left;
	EXPECT_TRUE(VoiceAudioDeviceConfigEquals(Left, Right));
	EXPECT_EQ(VoiceDesiredOutputChannels(Left), 2);
}

TEST(VoiceUtils, VoiceAudioDeviceConfigEqualsDetectsAnyFieldChange)
{
	SVoiceAudioDeviceConfig Base;
	str_copy(Base.m_aBackend, "coreaudio", sizeof(Base.m_aBackend));
	str_copy(Base.m_aInputDevice, "Built-in Microphone", sizeof(Base.m_aInputDevice));
	str_copy(Base.m_aOutputDevice, "Built-in Output", sizeof(Base.m_aOutputDevice));
	Base.m_OutputStereo = false;

	SVoiceAudioDeviceConfig Changed = Base;
	str_copy(Changed.m_aBackend, "dummy", sizeof(Changed.m_aBackend));
	EXPECT_FALSE(VoiceAudioDeviceConfigEquals(Base, Changed));

	Changed = Base;
	str_copy(Changed.m_aInputDevice, "USB Mic", sizeof(Changed.m_aInputDevice));
	EXPECT_FALSE(VoiceAudioDeviceConfigEquals(Base, Changed));

	Changed = Base;
	str_copy(Changed.m_aOutputDevice, "USB DAC", sizeof(Changed.m_aOutputDevice));
	EXPECT_FALSE(VoiceAudioDeviceConfigEquals(Base, Changed));

	Changed = Base;
	Changed.m_OutputStereo = true;
	EXPECT_FALSE(VoiceAudioDeviceConfigEquals(Base, Changed));
	EXPECT_EQ(VoiceDesiredOutputChannels(Base), 1);
}

TEST(VoiceUtils, BuildVoiceDeviceDropdownEntriesKeepsDefaultAndDeduplicatesDevices)
{
	std::vector<std::string> vDetectedDeviceNames = {"Built-in Microphone", "USB Mic", "usb mic", "", "Line In"};
	std::vector<SVoiceDeviceDropdownEntry> vEntries;

	BuildVoiceDeviceDropdownEntries(vDetectedDeviceNames, "", "Default", "Disconnected", vEntries);

	ASSERT_EQ(vEntries.size(), 4u);
	EXPECT_EQ(vEntries[0].m_DisplayName, "Default");
	EXPECT_EQ(vEntries[0].m_ConfigValue, "");
	EXPECT_EQ(vEntries[1].m_ConfigValue, "Built-in Microphone");
	EXPECT_EQ(vEntries[2].m_ConfigValue, "USB Mic");
	EXPECT_EQ(vEntries[3].m_ConfigValue, "Line In");
	EXPECT_EQ(VoiceFindSelectedDeviceIndex(vEntries, ""), 0);
	EXPECT_EQ(VoiceFindSelectedDeviceIndex(vEntries, "usb mic"), 2);
}

TEST(VoiceUtils, BuildVoiceDeviceDropdownEntriesPreservesDisconnectedCurrentDevice)
{
	std::vector<std::string> vDetectedDeviceNames = {"Built-in Output", "Headset"};
	std::vector<SVoiceDeviceDropdownEntry> vEntries;

	BuildVoiceDeviceDropdownEntries(vDetectedDeviceNames, "USB DAC", "Default", "Disconnected", vEntries);

	ASSERT_EQ(vEntries.size(), 4u);
	EXPECT_EQ(vEntries.back().m_DisplayName, "USB DAC (Disconnected)");
	EXPECT_EQ(vEntries.back().m_ConfigValue, "USB DAC");
	EXPECT_TRUE(vEntries.back().m_Disconnected);
	EXPECT_EQ(VoiceFindSelectedDeviceIndex(vEntries, "USB DAC"), 3);
}

TEST(VoiceUtils, BuildVoiceDeviceDropdownEntriesDoesNotDuplicateCurrentDeviceWhenCaseDiffers)
{
	std::vector<std::string> vDetectedDeviceNames = {"USB DAC", "Built-in Output"};
	std::vector<SVoiceDeviceDropdownEntry> vEntries;

	BuildVoiceDeviceDropdownEntries(vDetectedDeviceNames, "usb dac", "Default", "Disconnected", vEntries);

	ASSERT_EQ(vEntries.size(), 3u);
	EXPECT_FALSE(vEntries[1].m_Disconnected);
	EXPECT_EQ(vEntries[1].m_ConfigValue, "USB DAC");
	EXPECT_EQ(VoiceFindSelectedDeviceIndex(vEntries, "usb dac"), 1);
}

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

TEST(VoiceCore, SeqDeltaNormal)
{
	EXPECT_EQ(VoiceSeqDelta(10, 5), 5);
	EXPECT_EQ(VoiceSeqDelta(100, 0), 100);
	EXPECT_EQ(VoiceSeqDelta(5, 10), -5);
	EXPECT_EQ(VoiceSeqDelta(0, 100), -100);
}

TEST(VoiceCore, SeqDeltaSame)
{
	EXPECT_EQ(VoiceSeqDelta(42, 42), 0);
	EXPECT_EQ(VoiceSeqDelta(0, 0), 0);
	EXPECT_EQ(VoiceSeqDelta(65535, 65535), 0);
}

TEST(VoiceCore, SeqDeltaWrapForward)
{
	EXPECT_EQ(VoiceSeqDelta(5, 65530), 11);
	EXPECT_EQ(VoiceSeqDelta(0, 65535), 1);
	EXPECT_EQ(VoiceSeqDelta(100, 65436), 200);
}

TEST(VoiceCore, SeqDeltaWrapBackward)
{
	EXPECT_EQ(VoiceSeqDelta(65530, 5), -11);
	EXPECT_EQ(VoiceSeqDelta(65535, 0), -1);
	EXPECT_EQ(VoiceSeqDelta(65436, 100), -200);
}

TEST(VoiceCore, SeqLessNormal)
{
	EXPECT_TRUE(VoiceSeqLess(5, 10));
	EXPECT_TRUE(VoiceSeqLess(0, 1));
	EXPECT_FALSE(VoiceSeqLess(10, 5));
	EXPECT_FALSE(VoiceSeqLess(1, 0));
}

TEST(VoiceCore, SeqLessEqual)
{
	EXPECT_FALSE(VoiceSeqLess(42, 42));
	EXPECT_FALSE(VoiceSeqLess(0, 0));
	EXPECT_FALSE(VoiceSeqLess(65535, 65535));
}

TEST(VoiceCore, SeqLessWrap)
{
	EXPECT_TRUE(VoiceSeqLess(65530, 5));
	EXPECT_TRUE(VoiceSeqLess(65535, 1));
	EXPECT_FALSE(VoiceSeqLess(5, 65530));
	EXPECT_FALSE(VoiceSeqLess(1, 65535));
}

TEST(VoiceCore, SeqLessHalfWrap)
{
	// 32768 = 0x8000 wraps to -32768 as int16_t
	// Both directions return true at the exact halfway point (ambiguous)
	EXPECT_TRUE(VoiceSeqLess(0, 32768));
	EXPECT_TRUE(VoiceSeqLess(32768, 0));
	// Just past halfway: 32769 = 0x8001 wraps to -32767
	EXPECT_FALSE(VoiceSeqLess(0, 32769));
	EXPECT_TRUE(VoiceSeqLess(32769, 0));
}

TEST(VoiceCore, ClampJitterTargetLow)
{
	EXPECT_EQ(VoiceClampJitterTarget(0.0f), 2);
	EXPECT_EQ(VoiceClampJitterTarget(5.0f), 2);
	EXPECT_EQ(VoiceClampJitterTarget(8.0f), 2);
}

TEST(VoiceCore, ClampJitterTargetMid)
{
	EXPECT_EQ(VoiceClampJitterTarget(10.0f), 3);
	EXPECT_EQ(VoiceClampJitterTarget(14.0f), 3);
	EXPECT_EQ(VoiceClampJitterTarget(18.0f), 4);
	EXPECT_EQ(VoiceClampJitterTarget(22.0f), 4);
	EXPECT_EQ(VoiceClampJitterTarget(28.0f), 5);
	EXPECT_EQ(VoiceClampJitterTarget(32.0f), 5);
}

TEST(VoiceCore, ClampJitterTargetHigh)
{
	EXPECT_EQ(VoiceClampJitterTarget(33.0f), 6);
	EXPECT_EQ(VoiceClampJitterTarget(100.0f), 6);
	EXPECT_EQ(VoiceClampJitterTarget(1000.0f), 6);
}

TEST(VoiceCore, ComputeVoiceEncoderTargetsAutoProfileUsesAggressiveTable)
{
	int Bitrate = 0;
	int Loss = 0;
	bool Fec = false;

	ComputeVoiceEncoderTargets(0, 0.0f, 0, &Bitrate, &Loss, &Fec);
	EXPECT_EQ(Bitrate, 64000);
	EXPECT_EQ(Loss, 0);
	EXPECT_FALSE(Fec);

	ComputeVoiceEncoderTargets(5, 10.0f, 0, &Bitrate, &Loss, &Fec);
	EXPECT_EQ(Bitrate, 48000);
	EXPECT_EQ(Loss, 5);
	EXPECT_TRUE(Fec);

	ComputeVoiceEncoderTargets(10, 20.0f, 0, &Bitrate, &Loss, &Fec);
	EXPECT_EQ(Bitrate, 32000);
	EXPECT_EQ(Loss, 10);
	EXPECT_TRUE(Fec);

	ComputeVoiceEncoderTargets(20, 40.0f, 0, &Bitrate, &Loss, &Fec);
	EXPECT_EQ(Bitrate, 24000);
	EXPECT_EQ(Loss, 20);
	EXPECT_TRUE(Fec);
}

TEST(VoiceCore, ComputeVoiceEncoderTargetsWithComplexityManualProfileUsesStableComplexity)
{
	int Bitrate = 0;
	int Loss = 0;
	bool Fec = false;
	int Complexity = 0;
	ComputeVoiceEncoderTargetsWithComplexity(20, 40.0f, 4, &Bitrate, &Loss, &Fec, &Complexity);
	EXPECT_EQ(Bitrate, 64000);
	EXPECT_EQ(Loss, 0);
	EXPECT_FALSE(Fec);
	EXPECT_EQ(Complexity, 8);
}

TEST(VoiceCore, ComputeVoiceEncoderTargetsWithComplexityReducesComplexityOnPoorNetwork)
{
	int Bitrate1 = 0;
	int Loss1 = 0;
	bool Fec1 = false;
	int Complexity1 = 0;
	ComputeVoiceEncoderTargetsWithComplexity(0, 0.0f, 0, &Bitrate1, &Loss1, &Fec1, &Complexity1);
	EXPECT_EQ(Complexity1, 8);

	int Bitrate2 = 0;
	int Loss2 = 0;
	bool Fec2 = false;
	int Complexity2 = 0;
	ComputeVoiceEncoderTargetsWithComplexity(15, 35.0f, 0, &Bitrate2, &Loss2, &Fec2, &Complexity2);
	EXPECT_EQ(Complexity2, 6);
	EXPECT_LT(Complexity2, Complexity1);
}

TEST(VoiceCore, VoiceProcessingFactoryDefaultsMatchRoadmapDefaults)
{
	const auto Defaults = VoiceProcessingFactoryDefaults();

	EXPECT_EQ(Defaults.m_NoiseSuppressMode, VOICE_NOISE_SUPPRESS_OFF);
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

TEST(VoiceCore, ConfigDefaultsMatchFactoryDefaults)
{
	const auto Defaults = VoiceProcessingFactoryDefaults();

	EXPECT_EQ(DefaultConfig::QmVoiceNoiseSuppressEnable, Defaults.m_NoiseSuppressMode);
	EXPECT_EQ(DefaultConfig::QmVoiceNoiseSuppressStrength, Defaults.m_NoiseSuppressStrength);
	EXPECT_NEAR(DefaultConfig::QmVoiceCompThreshold / 100.0f, Defaults.m_CompressorThreshold, 0.001f);
	EXPECT_NEAR(DefaultConfig::QmVoiceCompRatio / 10.0f, Defaults.m_CompressorRatio, 0.001f);
	EXPECT_NEAR(DefaultConfig::QmVoiceCompAttackMs / 1000.0f, Defaults.m_CompressorAttackSec, 0.001f);
	EXPECT_NEAR(DefaultConfig::QmVoiceCompReleaseMs / 1000.0f, Defaults.m_CompressorReleaseSec, 0.001f);
	EXPECT_NEAR(DefaultConfig::QmVoiceCompMakeup / 100.0f, Defaults.m_CompressorMakeupGain, 0.001f);
	EXPECT_NEAR(DefaultConfig::QmVoiceLimiter / 100.0f, Defaults.m_Limiter, 0.001f);
	EXPECT_EQ(DefaultConfig::QmVoiceAgcEnable, 0);
}

TEST(VoiceCore, VoiceProcessTraceCallbackRecordsStagesInOrder)
{
	std::vector<EVoiceProcessStage> vStages;
	SetVoiceProcessTraceCallback(
		[](EVoiceProcessStage Stage, void *pUserData) {
			auto *pStages = static_cast<std::vector<EVoiceProcessStage> *>(pUserData);
			pStages->push_back(Stage);
		},
		&vStages);

	TraceVoiceProcessStage(EVoiceProcessStage::AGC_GAIN);
	TraceVoiceProcessStage(EVoiceProcessStage::MIC_GAIN);
	TraceVoiceProcessStage(EVoiceProcessStage::DENOISE);
	TraceVoiceProcessStage(EVoiceProcessStage::HPF_COMPRESSOR);

	SetVoiceProcessTraceCallback(nullptr, nullptr);

	ASSERT_EQ(vStages.size(), 4u);
	EXPECT_EQ(vStages[0], EVoiceProcessStage::AGC_GAIN);
	EXPECT_EQ(vStages[1], EVoiceProcessStage::MIC_GAIN);
	EXPECT_EQ(vStages[2], EVoiceProcessStage::DENOISE);
	EXPECT_EQ(vStages[3], EVoiceProcessStage::HPF_COMPRESSOR);
}

TEST(VoiceCore, CaptureProcessOrderIsAgcThenMicGainThenDenoiseThenDynamics)
{
	CRClientVoice Voice;
	SRClientVoiceConfigSnapshot Config;
	Config.m_QmVoiceAgcEnable = 1;
	Config.m_QmVoiceMicVolume = 100;
	Config.m_QmVoiceNoiseSuppressEnable = TEST_VOICE_NOISE_SUPPRESS_OFF;
	Config.m_QmVoiceFilterEnable = 0;

	std::vector<EVoiceProcessStage> vStages;
	SetVoiceProcessTraceCallback(
		[](EVoiceProcessStage Stage, void *pUserData) {
			auto *pStages = static_cast<std::vector<EVoiceProcessStage> *>(pUserData);
			pStages->push_back(Stage);
		},
		&vStages);

	int16_t aSamples[VOICE_FRAME_SAMPLES] = {};
	aSamples[0] = 1000;
	float AgcGain = 1.0f;
	float NoiseFloor = 0.0f;
	float NoiseGate = 1.0f;
	DenoiseState *pNoiseState = nullptr;
	bool NoiseFallbackLogged = false;
	float HpfPrevIn = 0.0f;
	float HpfPrevOut = 0.0f;
	float CompEnv = 0.0f;

	VoiceUtils::ProcessVoiceCaptureFrame(Config, aSamples, VOICE_FRAME_SAMPLES, AgcGain, NoiseFloor, NoiseGate, pNoiseState, NoiseFallbackLogged, HpfPrevIn, HpfPrevOut, CompEnv);

	SetVoiceProcessTraceCallback(nullptr, nullptr);

	ASSERT_EQ(vStages.size(), 4u);
	EXPECT_EQ(vStages[0], EVoiceProcessStage::AGC_GAIN);
	EXPECT_EQ(vStages[1], EVoiceProcessStage::MIC_GAIN);
	EXPECT_EQ(vStages[2], EVoiceProcessStage::DENOISE);
	EXPECT_EQ(vStages[3], EVoiceProcessStage::HPF_COMPRESSOR);
	EXPECT_GT(AgcGain, 0.0f);
}

TEST(VoiceCore, ComputeVoiceEncoderTargetsManualProfilesOverrideAdaptiveTable)
{
	int Bitrate = 0;
	int Loss = 0;
	bool Fec = true;

	ComputeVoiceEncoderTargets(20, 40.0f, 1, &Bitrate, &Loss, &Fec);
	EXPECT_EQ(Bitrate, 24000);
	EXPECT_EQ(Loss, 0);
	EXPECT_FALSE(Fec);

	ComputeVoiceEncoderTargets(20, 40.0f, 2, &Bitrate, &Loss, &Fec);
	EXPECT_EQ(Bitrate, 32000);
	EXPECT_EQ(Loss, 0);
	EXPECT_FALSE(Fec);

	ComputeVoiceEncoderTargets(20, 40.0f, 3, &Bitrate, &Loss, &Fec);
	EXPECT_EQ(Bitrate, 48000);
	EXPECT_EQ(Loss, 0);
	EXPECT_FALSE(Fec);

	ComputeVoiceEncoderTargets(20, 40.0f, 4, &Bitrate, &Loss, &Fec);
	EXPECT_EQ(Bitrate, 64000);
	EXPECT_EQ(Loss, 0);
	EXPECT_FALSE(Fec);
}

// ---------------------------------------------------------------------------
// ProcessIncoming PayloadSize=0 regression test
// ---------------------------------------------------------------------------

static size_t BuildVoicePacket(uint8_t *pBuf, uint8_t Version, uint8_t Type, uint16_t PayloadSize,
	uint32_t ContextHash, uint32_t TokenHash, uint8_t Flags, uint16_t SenderId, uint16_t Sequence,
	float PosX, float PosY, const uint8_t *pPayload = nullptr)
{
	SVoicePacketHeader Header;
	Header.m_Version = Version;
	Header.m_Type = Type;
	Header.m_PayloadSize = PayloadSize;
	Header.m_ContextHash = ContextHash;
	Header.m_TokenHash = TokenHash;
	Header.m_Flags = Flags;
	Header.m_SenderId = SenderId;
	Header.m_Sequence = Sequence;
	Header.m_PosX = PosX;
	Header.m_PosY = PosY;
	if(!WriteVoicePacketHeader(pBuf, VOICE_MAX_PACKET, Header))
		return 0;

	size_t Offset = VOICE_PACKET_HEADER_SIZE;
	if(PayloadSize > 0 && pPayload)
	{
		mem_copy(pBuf + Offset, pPayload, PayloadSize);
		Offset += PayloadSize;
	}
	return Offset;
}

static bool ParseVoicePacketPayloadSize(const uint8_t *pData, int Bytes, uint16_t &OutPayloadSize)
{
	if(!pData || Bytes < VOICE_PACKET_HEADER_SIZE)
		return false;

	SVoicePacketHeader Header;
	if(!ReadVoicePacketHeader(pData, Bytes, Header))
		return false;
	OutPayloadSize = Header.m_PayloadSize;
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

static EVoiceIncomingPacketDecision ClassifyTestPacket(uint8_t Version, uint8_t Type, uint16_t PayloadSize,
	uint32_t ContextHash, uint32_t TokenHash, uint16_t SenderId, size_t PacketSize)
{
	SVoicePacketHeader Header;
	Header.m_Version = Version;
	Header.m_Type = Type;
	Header.m_PayloadSize = PayloadSize;
	Header.m_ContextHash = ContextHash;
	Header.m_TokenHash = TokenHash;
	Header.m_SenderId = SenderId;

	SVoiceIncomingPacketContext Context;
	Context.m_ProtocolVersion = VOICE_VERSION;
	Context.m_LocalContextHash = 0x12345678u;
	Context.m_LocalTokenHash = 0x00000011u;
	Context.m_MaxClients = MAX_CLIENTS;
	return ClassifyVoiceIncomingPacket(Header, PacketSize, Context);
}

static EVoiceIncomingPacketDecision ClassifyBuiltVoicePacket(const uint8_t *pPacket, size_t PacketSize, uint32_t LocalContextHash, uint32_t LocalTokenHash)
{
	SVoicePacketHeader Header;
	if(!ReadVoicePacketHeader(pPacket, PacketSize, Header))
		return EVoiceIncomingPacketDecision::DROP_HEADER;

	SVoiceIncomingPacketContext Context;
	Context.m_ProtocolVersion = VOICE_VERSION;
	Context.m_LocalContextHash = LocalContextHash;
	Context.m_LocalTokenHash = LocalTokenHash;
	Context.m_MaxClients = MAX_CLIENTS;
	return ClassifyVoiceIncomingPacket(Header, PacketSize, Context);
}

TEST(VoiceCore, ProcessIncomingZeroPayload)
{
	uint8_t aPacket[1200];
	const size_t PacketSize = BuildVoicePacket(aPacket, 3, VOICE_TYPE_AUDIO,
		0, 0x12345678u, 0u, 0, 1, 100, 50.0f, 50.0f, nullptr);

	uint16_t PayloadSize = 0;
	ASSERT_TRUE(ParseVoicePacketPayloadSize(aPacket, (int)PacketSize, PayloadSize));
	EXPECT_EQ(PayloadSize, 0);

	EXPECT_FALSE(ShouldProcessPayload(PayloadSize, VOICE_PACKET_HEADER_SIZE, (int)PacketSize));
}

TEST(VoiceCore, ProcessIncomingNormalPayload)
{
	uint8_t aPayload[64];
	mem_zero(aPayload, sizeof(aPayload));
	aPayload[0] = 0xFF;

	uint8_t aPacket[1200];
	const size_t PacketSize = BuildVoicePacket(aPacket, 3, VOICE_TYPE_AUDIO,
		64, 0x12345678u, 0u, 0, 1, 100, 50.0f, 50.0f, aPayload);

	uint16_t PayloadSize = 0;
	ASSERT_TRUE(ParseVoicePacketPayloadSize(aPacket, (int)PacketSize, PayloadSize));
	EXPECT_EQ(PayloadSize, 64);

	EXPECT_TRUE(ShouldProcessPayload(PayloadSize, VOICE_PACKET_HEADER_SIZE, (int)PacketSize));
}

TEST(VoiceCore, ProcessIncomingTruncatedPayload)
{
	uint8_t aPacket[1200];
	const size_t PacketSize = BuildVoicePacket(aPacket, 3, VOICE_TYPE_AUDIO,
		200, 0x12345678u, 0u, 0, 1, 100, 50.0f, 50.0f, nullptr);

	uint16_t PayloadSize = 0;
	ASSERT_TRUE(ParseVoicePacketPayloadSize(aPacket, (int)PacketSize, PayloadSize));
	EXPECT_EQ(PayloadSize, 200);

	EXPECT_FALSE(ShouldProcessPayload(PayloadSize, VOICE_PACKET_HEADER_SIZE, (int)PacketSize));
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

TEST(VoiceCore, ProcessIncomingClassifiesVersionTypeAndContextDrops)
{
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION + 1, VOICE_TYPE_AUDIO, 8, 0x12345678u, 0x11u, 1, VOICE_PACKET_HEADER_SIZE + 8),
		EVoiceIncomingPacketDecision::DROP_VERSION);
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, 99, 8, 0x12345678u, 0x11u, 1, VOICE_PACKET_HEADER_SIZE + 8),
		EVoiceIncomingPacketDecision::DROP_TYPE);
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_AUDIO, 8, 0, 0x11u, 1, VOICE_PACKET_HEADER_SIZE + 8),
		EVoiceIncomingPacketDecision::DROP_CONTEXT);
}

TEST(VoiceCore, ProcessIncomingClassifiesGroupSenderAndPayloadDrops)
{
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_AUDIO, 8, 0x12345678u, 0x22u, 1, VOICE_PACKET_HEADER_SIZE + 8),
		EVoiceIncomingPacketDecision::DROP_GROUP);
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_AUDIO, 8, 0x12345678u, 0x40000011u, 1, VOICE_PACKET_HEADER_SIZE + 8),
		EVoiceIncomingPacketDecision::HANDLE_AUDIO);
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_AUDIO, 8, 0x12345678u, 0x11u, MAX_CLIENTS, VOICE_PACKET_HEADER_SIZE + 8),
		EVoiceIncomingPacketDecision::DROP_SENDER);
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_AUDIO, 0, 0x12345678u, 0x11u, 1, VOICE_PACKET_HEADER_SIZE),
		EVoiceIncomingPacketDecision::DROP_PAYLOAD);
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_AUDIO, 32, 0x12345678u, 0x11u, 1, VOICE_PACKET_HEADER_SIZE + 8),
		EVoiceIncomingPacketDecision::DROP_PAYLOAD);
}

TEST(VoiceCore, ProcessIncomingClassifiesAudioPingAndPongPaths)
{
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_AUDIO, 8, 0x12345678u, 0x11u, 1, VOICE_PACKET_HEADER_SIZE + 8),
		EVoiceIncomingPacketDecision::HANDLE_AUDIO);
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_PING, 0, 0x12345678u, 0x11u, 1, VOICE_PACKET_HEADER_SIZE),
		EVoiceIncomingPacketDecision::HANDLE_PING);
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_PONG, 0, 0x12345678u, 0x11u, 1, VOICE_PACKET_HEADER_SIZE),
		EVoiceIncomingPacketDecision::HANDLE_PONG);
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_PONG, 0, 0x12345678u, 0x40000011u, 1, VOICE_PACKET_HEADER_SIZE),
		EVoiceIncomingPacketDecision::HANDLE_PONG);
}

TEST(VoiceCore, ProcessIncomingAllowsSameGroupAcrossLegacyAndModePackedTokens)
{
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_AUDIO, 8, 0x12345678u, 0x80000011u, 1, VOICE_PACKET_HEADER_SIZE + 8),
		EVoiceIncomingPacketDecision::HANDLE_AUDIO);
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_PING, 0, 0x12345678u, 0x80000011u, 1, VOICE_PACKET_HEADER_SIZE),
		EVoiceIncomingPacketDecision::HANDLE_PING);
}

TEST(VoiceCore, ProcessIncomingRejectsKeepaliveFromDifferentGroupEvenWhenModeBitsDiffer)
{
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_PING, 0, 0x12345678u, 0x80000022u, 1, VOICE_PACKET_HEADER_SIZE),
		EVoiceIncomingPacketDecision::DROP_KEEPALIVE_TOKEN);
	EXPECT_EQ(ClassifyTestPacket(VOICE_VERSION, VOICE_TYPE_PONG, 0, 0x12345678u, 0x40000022u, 1, VOICE_PACKET_HEADER_SIZE),
		EVoiceIncomingPacketDecision::DROP_KEEPALIVE_TOKEN);
}

TEST(VoiceCore, JitterStartSeqWaitsForTargetFramesAndUsesMinLiveSequence)
{
	std::array<uint8_t, 32> aValid = {};
	std::array<uint16_t, 32> aSeq = {};
	aValid[5] = 1;
	aSeq[5] = 37;
	aValid[9] = 1;
	aSeq[9] = 41;
	aValid[12] = 1;
	aSeq[12] = 44;

	bool HasNextSeq = false;
	uint16_t NextSeq = 999;
	EXPECT_FALSE(SeedVoiceJitterStartSeq(2, 3, false, NextSeq, aValid.data(), aSeq.data(), aValid.size(), HasNextSeq, NextSeq));
	EXPECT_FALSE(HasNextSeq);

	ASSERT_TRUE(SeedVoiceJitterStartSeq(3, 3, false, 0, aValid.data(), aSeq.data(), aValid.size(), HasNextSeq, NextSeq));
	EXPECT_TRUE(HasNextSeq);
	EXPECT_EQ(NextSeq, 37);
}

TEST(VoiceCore, JitterStartSeqSkipsAllInvalidBuffersAndHandlesWrapAround)
{
	std::array<uint8_t, 32> aValid = {};
	std::array<uint16_t, 32> aSeq = {};
	bool HasNextSeq = false;
	uint16_t NextSeq = 0;

	EXPECT_FALSE(SeedVoiceJitterStartSeq(3, 3, false, 0, aValid.data(), aSeq.data(), aValid.size(), HasNextSeq, NextSeq));
	EXPECT_FALSE(HasNextSeq);

	aValid[1] = 1;
	aSeq[1] = 65535;
	aValid[2] = 1;
	aSeq[2] = 0;
	aValid[3] = 1;
	aSeq[3] = 1;

	ASSERT_TRUE(SeedVoiceJitterStartSeq(3, 3, false, 0, aValid.data(), aSeq.data(), aValid.size(), HasNextSeq, NextSeq));
	EXPECT_TRUE(HasNextSeq);
	EXPECT_EQ(NextSeq, 65535);
}

TEST(VoiceCore, BuiltPacketsFollowPositiveProtocolPaths)
{
	uint8_t aPayload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	uint8_t aPacket[VOICE_MAX_PACKET];

	size_t PacketSize = BuildVoicePacket(aPacket, VOICE_VERSION, VOICE_TYPE_AUDIO,
		sizeof(aPayload), 0x10203040u, 0x11u, 0, 3, 77, 10.0f, 20.0f, aPayload);
	ASSERT_GT(PacketSize, (size_t)0);
	EXPECT_EQ(ClassifyBuiltVoicePacket(aPacket, PacketSize, 0x10203040u, 0x11u),
		EVoiceIncomingPacketDecision::HANDLE_AUDIO);

	PacketSize = BuildVoicePacket(aPacket, VOICE_VERSION, VOICE_TYPE_PING,
		0, 0x10203040u, 0u, 0, 3, 78, 10.0f, 20.0f, nullptr);
	ASSERT_GT(PacketSize, (size_t)0);
	EXPECT_EQ(ClassifyBuiltVoicePacket(aPacket, PacketSize, 0x10203040u, 0u),
		EVoiceIncomingPacketDecision::HANDLE_PING);

	PacketSize = BuildVoicePacket(aPacket, VOICE_VERSION, VOICE_TYPE_PONG,
		0, 0x10203040u, 0x11u, 0, 3, 79, 10.0f, 20.0f, nullptr);
	ASSERT_GT(PacketSize, (size_t)0);
	EXPECT_EQ(ClassifyBuiltVoicePacket(aPacket, PacketSize, 0x10203040u, 0x11u),
		EVoiceIncomingPacketDecision::HANDLE_PONG);
}

TEST(QmClient, ParseQmClientUsersJsonSupportsNameFieldsAndLocalMarks)
{
	const char *pJsonText =
		"{\"users\":["
		"{\"server_address\":\"addr-b\",\"player_name\":\"RemoteDummy\",\"dummy\":true},"
		"{\"server\":\"addr-a\",\"name\":\"QmSeven\",\"client_type\":\"arg\",\"qid\":\"qm-7\",\"foot_particles_enabled\":1,"
		"\"remote_particles_enabled\":true,\"voice_supported\":0},"
		"{\"server_address\":\"addr-a\",\"player_name\":\"QmEight\",\"dummy\":false,\"type\":\"qm\"}"
		"]}";

	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmClientUsersParseResult Result;
	EXPECT_TRUE(ParseQmClientUsersJson(pJson, "addr-a", Result));
	EXPECT_TRUE(Result.m_Parsed);
	ASSERT_EQ(Result.m_vServerDistribution.size(), 2u);
	EXPECT_EQ(Result.m_vServerDistribution[0].m_ServerAddress, "addr-a");
	EXPECT_EQ(Result.m_vServerDistribution[0].m_UserCount, 2);
	EXPECT_EQ(Result.m_vServerDistribution[0].m_DummyCount, 0);
	EXPECT_EQ(Result.m_vServerDistribution[1].m_ServerAddress, "addr-b");
	EXPECT_EQ(Result.m_vServerDistribution[1].m_UserCount, 0);
	EXPECT_EQ(Result.m_vServerDistribution[1].m_DummyCount, 1);
	EXPECT_EQ(Result.m_OnlineUserCount, 2);
	EXPECT_EQ(Result.m_OnlineDummyCount, 1);

	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 2u);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Name, "QmSeven");
	EXPECT_TRUE(Result.m_vLocalServerMarks[0].m_FootParticlesEnabled);
	EXPECT_TRUE(Result.m_vLocalServerMarks[0].m_RemoteParticlesEnabled);
	EXPECT_FALSE(Result.m_vLocalServerMarks[0].m_VoiceSupported);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_ClientBrand, EClientBrand::ARG);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Qid, "qm-7");

	EXPECT_EQ(Result.m_vLocalServerMarks[1].m_Name, "QmEight");
	EXPECT_TRUE(Result.m_vLocalServerMarks[1].m_VoiceSupported);
	EXPECT_EQ(Result.m_vLocalServerMarks[1].m_ClientBrand, EClientBrand::QM);

	json_value_free(pJson);
}

TEST(QmClient, ParseQmClientUsersJsonRejectsMissingUsersArrayAndSkipsBrokenEntries)
{
	const char *pJsonText =
		"["
		"{\"server_address\":\"addr-a\",\"player_name\":\"QmThree\",\"dummy\":0},"
		"{\"server_address\":17,\"player_name\":\"QmFour\"},"
		"{\"server_address\":\"addr-a\"},"
		"{\"server_address\":\"addr-a\",\"player_id\":5,\"voice_supported\":true},"
		"{\"server_address\":\"addr-a\",\"name\":\"QmFive\",\"voice_supported\":true}"
		"]";

	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmClientUsersParseResult Result;
	EXPECT_TRUE(ParseQmClientUsersJson(pJson, "addr-a", Result));
	EXPECT_TRUE(Result.m_Parsed);
	EXPECT_EQ(Result.m_OnlineUserCount, 2);
	EXPECT_EQ(Result.m_OnlineDummyCount, 0);
	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 2u);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Name, "QmThree");
	EXPECT_TRUE(Result.m_vLocalServerMarks[0].m_VoiceSupported);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_ClientBrand, EClientBrand::QM);
	EXPECT_EQ(Result.m_vLocalServerMarks[1].m_Name, "QmFive");
	EXPECT_TRUE(Result.m_vLocalServerMarks[1].m_VoiceSupported);
	EXPECT_EQ(Result.m_vLocalServerMarks[1].m_ClientBrand, EClientBrand::QM);

	json_value_free(pJson);

	const char *pBadRootText = "{\"not_users\":{}}";
	pJson = json_parse(pBadRootText, str_length(pBadRootText));
	ASSERT_NE(pJson, nullptr);
	EXPECT_FALSE(ParseQmClientUsersJson(pJson, "addr-a", Result));
	EXPECT_FALSE(Result.m_Parsed);
	EXPECT_TRUE(Result.m_vServerDistribution.empty());
	EXPECT_TRUE(Result.m_vLocalServerMarks.empty());
	json_value_free(pJson);
}

TEST(QmClient, ParseQmDeveloperPresencesSupportsArbitraryNamesAndDummy)
{
	const char *pJsonText =
		"{\"server_time\":1000,\"presences\":["
		"{\"developer_id\":\"qimen-main\",\"server_address\":\"addr-a\",\"player_id\":7,\"player_name\":\"Any nickname\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":19},"
		"{\"developer_id\":\"qimen-dummy\",\"server_address\":\"addr-a\",\"player_id\":8,\"player_name\":\"完全不同的昵称\",\"dummy\":true,\"issued_at\":950,\"expires_at\":1050,\"style_bucket\":20}"
		"]}";

	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmDeveloperPresenceParseResult Result;
	ASSERT_TRUE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	EXPECT_TRUE(Result.m_Parsed);
	EXPECT_EQ(Result.m_ServerTime, 1000);
	ASSERT_EQ(Result.m_vPresences.size(), 2u);

	const SQmDeveloperPresence *pMain = FindQmDeveloperPresence(Result.m_vPresences, "addr-a", 7, "Any nickname", 1000);
	ASSERT_NE(pMain, nullptr);
	EXPECT_EQ(pMain->m_DeveloperId, "qimen-main");
	EXPECT_FALSE(pMain->m_Dummy);
	EXPECT_EQ(QmDeveloperBadgeStyleFromBucket(pMain->m_StyleBucket), EQmDeveloperBadgeStyle::RAINBOW);

	const SQmDeveloperPresence *pDummy = FindQmDeveloperPresence(Result.m_vPresences, "addr-a", 8, "完全不同的昵称", 1000);
	ASSERT_NE(pDummy, nullptr);
	EXPECT_EQ(pDummy->m_DeveloperId, "qimen-dummy");
	EXPECT_TRUE(pDummy->m_Dummy);
	EXPECT_EQ(QmDeveloperBadgeStyleFromBucket(pDummy->m_StyleBucket), EQmDeveloperBadgeStyle::BLACK);

	json_value_free(pJson);
}

TEST(QmClient, FindQmDeveloperPresenceRequiresExactServerIdAndName)
{
	std::vector<SQmDeveloperPresence> vPresences = {
		{"developer-one", "addr-a", 4, "SameName", false, 900, 1100, 1},
		{"developer-two", "addr-a", 5, "SameName", true, 900, 1100, 2},
	};

	const SQmDeveloperPresence *pFound = FindQmDeveloperPresence(vPresences, "addr-a", 5, "SameName", 1000);
	ASSERT_NE(pFound, nullptr);
	EXPECT_EQ(pFound->m_DeveloperId, "developer-two");

	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-a", 4, "Renamed", 1000), nullptr);
	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-b", 4, "SameName", 1000), nullptr);
	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-a", 6, "SameName", 1000), nullptr);
	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-a", 4, "samename", 1000), nullptr);
}

TEST(QmClient, ParseQmDeveloperPresencesKeepsOnlyValidLocalEntries)
{
	const char *pJsonText =
		"{\"server_time\":1000,\"presences\":["
		"{\"developer_id\":\"valid\",\"server_address\":\"addr-a\",\"player_id\":3,\"player_name\":\"Valid Name\",\"dummy\":false,\"issued_at\":1000,\"expires_at\":1001,\"style_bucket\":99},"
		"{\"developer_id\":\"remote\",\"server_address\":\"addr-b\",\"player_id\":3,\"player_name\":\"Valid Name\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"expired\",\"server_address\":\"addr-a\",\"player_id\":4,\"player_name\":\"Expired\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1000,\"style_bucket\":0},"
		"{\"developer_id\":\"future\",\"server_address\":\"addr-a\",\"player_id\":5,\"player_name\":\"Future\",\"dummy\":false,\"issued_at\":1001,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"\",\"server_address\":\"addr-a\",\"player_id\":6,\"player_name\":\"No developer\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"no-name\",\"server_address\":\"addr-a\",\"player_id\":7,\"player_name\":\"\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"negative-id\",\"server_address\":\"addr-a\",\"player_id\":-1,\"player_name\":\"Bad id\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"large-id\",\"server_address\":\"addr-a\",\"player_id\":128,\"player_name\":\"Bad id\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"negative-style\",\"server_address\":\"addr-a\",\"player_id\":8,\"player_name\":\"Bad style\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":-1},"
		"{\"developer_id\":\"large-style\",\"server_address\":\"addr-a\",\"player_id\":9,\"player_name\":\"Bad style\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":100},"
		"{\"developer_id\":\"bad-dummy\",\"server_address\":\"addr-a\",\"player_id\":10,\"player_name\":\"Bad dummy\",\"dummy\":1,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"bad-time\",\"server_address\":\"addr-a\",\"player_id\":11,\"player_name\":\"Bad time\",\"dummy\":false,\"issued_at\":\"900\",\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"missing-style\",\"server_address\":\"addr-a\",\"player_id\":12,\"player_name\":\"Missing style\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100}"
		"]}";

	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmDeveloperPresenceParseResult Result;
	ASSERT_TRUE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	ASSERT_EQ(Result.m_vPresences.size(), 1u);
	EXPECT_EQ(Result.m_vPresences[0].m_DeveloperId, "valid");
	EXPECT_EQ(Result.m_vPresences[0].m_PlayerId, 3);
	EXPECT_EQ(Result.m_vPresences[0].m_StyleBucket, 99);

	json_value_free(pJson);
}

TEST(QmClient, FindQmDeveloperPresenceRechecksLifetime)
{
	const std::vector<SQmDeveloperPresence> vPresences = {
		{"developer", "addr-a", 1, "Name", false, 100, 200, 0},
	};

	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-a", 1, "Name", 99), nullptr);
	EXPECT_NE(FindQmDeveloperPresence(vPresences, "addr-a", 1, "Name", 100), nullptr);
	EXPECT_NE(FindQmDeveloperPresence(vPresences, "addr-a", 1, "Name", 199), nullptr);
	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-a", 1, "Name", 200), nullptr);
}

TEST(QmClient, QmDeveloperBadgeStyleUsesExactlyTwentyRainbowBuckets)
{
	int RainbowCount = 0;
	for(int Bucket = 0; Bucket < 100; ++Bucket)
	{
		if(QmDeveloperBadgeStyleFromBucket(Bucket) == EQmDeveloperBadgeStyle::RAINBOW)
			++RainbowCount;
	}

	EXPECT_EQ(RainbowCount, 20);
	EXPECT_EQ(QmDeveloperBadgeStyleFromBucket(-1), EQmDeveloperBadgeStyle::BLACK);
	EXPECT_EQ(QmDeveloperBadgeStyleFromBucket(100), EQmDeveloperBadgeStyle::BLACK);
}

TEST(QmClient, DeveloperBadgeVisibilityRequiresAuthenticationAndVisibleIdentity)
{
	EXPECT_TRUE(ShouldShowQmDeveloperBadge(true, true, false));
	EXPECT_FALSE(ShouldShowQmDeveloperBadge(false, true, false));
	EXPECT_FALSE(ShouldShowQmDeveloperBadge(true, false, false));
	EXPECT_FALSE(ShouldShowQmDeveloperBadge(true, true, true));
	EXPECT_FALSE(ShouldShowQmDeveloperBadge(false, false, true));
}

TEST(QmClient, DeveloperMarkRequiresTheSameActivePlayerNameAndUnexpiredLease)
{
	EXPECT_TRUE(IsQmDeveloperMarkCurrent(true, "Authenticated Name", "Authenticated Name", 101, 100));
	EXPECT_FALSE(IsQmDeveloperMarkCurrent(false, "Authenticated Name", "Authenticated Name", 101, 100));
	EXPECT_FALSE(IsQmDeveloperMarkCurrent(true, "Authenticated Name", "Renamed", 101, 100));
	EXPECT_FALSE(IsQmDeveloperMarkCurrent(true, "Authenticated Name", "Authenticated Name", 100, 100));
	EXPECT_FALSE(IsQmDeveloperMarkCurrent(true, "", "Authenticated Name", 101, 100));
}

TEST(QmClient, ParseQmDeveloperPresencesResetsOutputAndKeepsOrder)
{
	const char *pJsonText =
		"{\"server_time\":1000,\"presences\":["
		"{\"developer_id\":\"first\",\"server_address\":\"addr-a\",\"player_id\":1,\"player_name\":\"First\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":1},"
		"{\"developer_id\":\"second\",\"server_address\":\"addr-a\",\"player_id\":2,\"player_name\":\"Second\",\"dummy\":true,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":2}"
		"]}";

	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmDeveloperPresenceParseResult Result;
	ASSERT_TRUE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	ASSERT_EQ(Result.m_vPresences.size(), 2u);
	EXPECT_EQ(Result.m_vPresences[0].m_DeveloperId, "first");
	EXPECT_EQ(Result.m_vPresences[1].m_DeveloperId, "second");

	ASSERT_TRUE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	ASSERT_EQ(Result.m_vPresences.size(), 2u);
	EXPECT_EQ(Result.m_vPresences[0].m_DeveloperId, "first");
	EXPECT_EQ(Result.m_vPresences[1].m_DeveloperId, "second");

	json_value_free(pJson);
}

TEST(QmClient, ParseQmDeveloperPresencesRejectsMalformedRoot)
{
	const char *pJsonText = "{\"server_time\":1000,\"presences\":{}}";
	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmDeveloperPresenceParseResult Result;
	EXPECT_FALSE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	EXPECT_FALSE(Result.m_Parsed);
	EXPECT_TRUE(Result.m_vPresences.empty());

	json_value_free(pJson);

	pJsonText = "{\"presences\":[]}";
	pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);
	EXPECT_FALSE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	EXPECT_FALSE(Result.m_Parsed);
	EXPECT_EQ(Result.m_ServerTime, 0);
	json_value_free(pJson);
}

TEST(QmClient, DistributionSuccessLogsAreLatchedUntilFailureOrReset)
{
	std::ifstream HeaderFile(TestSourcePath("src/game/client/components/qmclient/qmclient.h"));
	ASSERT_TRUE(HeaderFile.good());
	std::stringstream HeaderBuffer;
	HeaderBuffer << HeaderFile.rdbuf();
	const std::string Header = HeaderBuffer.str();
	EXPECT_NE(Header.find("bool m_QmClientDistributionSuccessLatched = false;"), std::string::npos);

	std::ifstream SourceFile(TestSourcePath("src/game/client/components/qmclient/qmclient.cpp"));
	ASSERT_TRUE(SourceFile.good());
	std::stringstream SourceBuffer;
	SourceBuffer << SourceFile.rdbuf();
	const std::string Source = SourceBuffer.str();

	EXPECT_NE(Source.find("if(!m_QmClientDistributionSuccessLatched)\n\t\tLogQmClientDistributionRequestEvent(\"request\", aUrl);"), std::string::npos);
	EXPECT_NE(Source.find("m_QmClientDistributionSuccessLatched = true;"), std::string::npos);
	EXPECT_NE(Source.find("LogQmClientDistributionEvent(\"parse_ok\", Result.m_OnlineUserCount, Result.m_OnlineDummyCount, (int)Result.m_vLocalServerMarks.size());"), std::string::npos);
	EXPECT_NE(Source.find("m_QmClientDistributionSuccessLatched = false;\n\t\t\tLogQmClientDistributionFailureEvent(\"parse_failed\", \"users payload could not be parsed\");"), std::string::npos);
	EXPECT_NE(Source.find("m_QmClientDistributionSuccessLatched = false;\n\t\tLogQmClientDistributionFailureEvent(\"request_failed\", aFailure);"), std::string::npos);
	EXPECT_NE(Source.find("m_QmClientDistributionSuccessLatched = false;\n\tm_aQmClientAuthToken[0] = '\\0';"), std::string::npos);
}

TEST(VoiceCore, ProcessIncomingPingRttRequiresMatchingOutstandingPing)
{
	EXPECT_TRUE(VoiceShouldUpdatePingRtt(15, 15, 1234));
	EXPECT_FALSE(VoiceShouldUpdatePingRtt(14, 15, 1234));
	EXPECT_FALSE(VoiceShouldUpdatePingRtt(15, 15, 0));
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

// 意图：服务器头衔有严格的长度上限，并且只接受当前服务器的有效声明。
TEST(QmClient, CustomTitleLengthAndPresenceValidation)
{
	EXPECT_TRUE(IsValidQmTitle("一二三四五六"));
	EXPECT_TRUE(IsValidQmTitle("abcdefghijkl"));
	EXPECT_FALSE(IsValidQmTitle("一二三四五六七"));
	EXPECT_FALSE(IsValidQmTitle("abcdefghijklm"));
	EXPECT_FALSE(IsValidQmTitle("[开发者]"));
	EXPECT_FALSE(IsValidQmTitle("a\nb"));
	EXPECT_FALSE(IsValidQmTitle(""));
	const char *pJsonText = R"({"server_time":1000,"presences":[{"server_address":"a","player_id":3,"player_name":"Twen","title":"小猫","issued_at":1000,"expires_at":1015},{"server_address":"b","player_id":4,"player_name":"Twen","title":"小猫","issued_at":1000,"expires_at":1015},{"server_address":"a","player_id":5,"player_name":"Twen","title":"小猫","issued_at":900,"expires_at":999}]})";
	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);
	const auto Presences = ParseQmTitlePresences(pJson, "a");
	ASSERT_EQ(Presences.size(), 1u);
	EXPECT_EQ(Presences[0].m_PlayerId, 3);
	EXPECT_EQ(Presences[0].m_Title, "小猫");
	EXPECT_EQ(Presences[0].m_RemainingSeconds, 15);
	json_value_free(pJson);
}
