// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1
#include "test.h"

#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/websocket_client.h>

#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/voice/voice_capture_pipeline.h>
#include <game/client/components/qmclient/voice/voice_core.h>
#include <game/client/components/qmclient/voice/voice_utils.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <limits>
#include <memory>
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

TEST(VoiceUtils, VoiceWebSocketUrlMigratesOnlyOfficialUdpDefault)
{
	EXPECT_STREQ(VoiceUtils::EffectiveVoiceWebSocketUrl(nullptr), "wss://qmclient.icu/ws/voice");
	EXPECT_STREQ(VoiceUtils::EffectiveVoiceWebSocketUrl(""), "wss://qmclient.icu/ws/voice");
	EXPECT_STREQ(VoiceUtils::EffectiveVoiceWebSocketUrl("42.194.185.210:9987"), "wss://qmclient.icu/ws/voice");
	EXPECT_STREQ(VoiceUtils::EffectiveVoiceWebSocketUrl("custom.example:9987"), "custom.example:9987");
	EXPECT_STREQ(VoiceUtils::EffectiveVoiceWebSocketUrl("wss://voice.example/ws"), "wss://voice.example/ws");
}

namespace
{
	class CVoiceTransportTestClient final : public IQmWebSocketClient
	{
	public:
		STuning m_Tuning;
		EQmWebSocketState m_State = EQmWebSocketState::IDLE;
		bool m_Desired = false;
		bool m_SendAccepted = true;
		int64_t m_ConnectedTick = 0;
		std::deque<SQmWebSocketMessage> m_Incoming;

		bool Available() const override { return true; }
		const char *UnavailableReason() const override { return ""; }
		bool Connect(const SQmWebSocketConnectConfig &, std::string &) override
		{
			m_Desired = true;
			m_State = EQmWebSocketState::CONNECTING;
			return true;
		}
		void Disconnect() override
		{
			m_Desired = false;
			m_State = EQmWebSocketState::IDLE;
			m_Incoming.clear();
		}
		bool Desired() const override { return m_Desired; }
		EQmWebSocketState State() const override { return m_State; }
		const char *StateName() const override { return "test"; }
		bool SendText(const char *, size_t) override { return false; }
		bool SendBinary(const char *, size_t) override { return m_SendAccepted; }
		void SetTuning(const STuning &Tuning) override { m_Tuning = Tuning; }
		bool PollMessage(SQmWebSocketMessage &Out) override
		{
			if(m_Incoming.empty())
				return false;
			Out = std::move(m_Incoming.front());
			m_Incoming.pop_front();
			return true;
		}
		size_t PendingMessages() const override { return m_Incoming.size(); }
		int64_t LastConnectedTick() const override { return m_ConnectedTick; }
		int64_t LastMessageTick() const override { return 0; }
		int64_t SendCount() const override { return 0; }
		int64_t RecvCount() const override { return 0; }
		int64_t DroppedIncomingCount() const override { return 0; }
		int64_t DroppedOutgoingCount() const override { return 0; }
		int64_t ReconnectCount() const override { return 0; }
		int LastPingRttMs() const override { return -1; }
		const char *LastError() const override { return ""; }
	};
}

TEST(VoiceUtils, WebSocketVoiceBoundsOutgoingQueueAndResetsAfterReconnect)
{
	auto pClient = std::make_unique<CVoiceTransportTestClient>();
	auto *pMock = pClient.get();
	CVoiceWebSocketTransport Transport(std::move(pClient));
	EXPECT_EQ(pMock->m_Tuning.m_OutgoingQueueCapacity, 8u);
	EXPECT_TRUE(Transport.Update("wss://voice.example/ws", true, 10, 20, VOICE_VERSION));
	EXPECT_TRUE(Transport.Connecting());
	pMock->m_State = EQmWebSocketState::CONNECTED;
	++pMock->m_ConnectedTick;
	EXPECT_TRUE(Transport.Update("wss://voice.example/ws", true, 10, 20, VOICE_VERSION));
	uint8_t aPacket[VOICE_PACKET_HEADER_SIZE] = {};
	EXPECT_TRUE(Transport.SendPacket(aPacket, sizeof(aPacket)));
	pMock->m_SendAccepted = false;
	EXPECT_FALSE(Transport.SendPacket(aPacket, sizeof(aPacket)));
	pMock->m_State = EQmWebSocketState::RECONNECTING;
	EXPECT_TRUE(Transport.Update("wss://voice.example/ws", true, 10, 20, VOICE_VERSION));
	EXPECT_FALSE(Transport.SendPacket(aPacket, sizeof(aPacket)));
	pMock->m_State = EQmWebSocketState::CONNECTED;
	++pMock->m_ConnectedTick;
	EXPECT_FALSE(Transport.SendPacket(aPacket, sizeof(aPacket)));
	EXPECT_TRUE(Transport.Update("wss://voice.example/ws", true, 10, 20, VOICE_VERSION));
	pMock->m_SendAccepted = true;
	EXPECT_TRUE(Transport.SendPacket(aPacket, sizeof(aPacket)));
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

TEST(VoiceCore, CaptureProcessKeepsOnlyMicGain)
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

	ASSERT_EQ(vStages.size(), 1u);
	EXPECT_EQ(vStages[0], EVoiceProcessStage::MIC_GAIN);
	EXPECT_FLOAT_EQ(AgcGain, 1.0f);
	EXPECT_FLOAT_EQ(NoiseFloor, 0.0f);
	EXPECT_FLOAT_EQ(NoiseGate, 1.0f);
	EXPECT_FLOAT_EQ(HpfPrevIn, 0.0f);
	EXPECT_FLOAT_EQ(HpfPrevOut, 0.0f);
	EXPECT_FLOAT_EQ(CompEnv, 0.0f);
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
	uint8_t aPayload[200] = {};
	uint8_t aPacket[1200];
	const size_t PacketSize = BuildVoicePacket(aPacket, 3, VOICE_TYPE_AUDIO,
		sizeof(aPayload), 0x12345678u, 0u, 0, 1, 100, 50.0f, 50.0f, aPayload);
	ASSERT_EQ(PacketSize, VOICE_PACKET_HEADER_SIZE + sizeof(aPayload));

	uint16_t PayloadSize = 0;
	EXPECT_FALSE(ParseVoicePacketPayloadSize(aPacket, (int)PacketSize - 1, PayloadSize));
	EXPECT_EQ(ClassifyBuiltVoicePacket(aPacket, PacketSize - 1, 0x12345678u, 0u),
		EVoiceIncomingPacketDecision::DROP_HEADER);
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
	int64_t ServerTime = 0;
	const auto Presences = ParseQmTitlePresences(pJson, "a", &ServerTime);
	ASSERT_EQ(Presences.size(), 1u);
	EXPECT_EQ(ServerTime, 1000);
	EXPECT_EQ(Presences[0].m_PlayerId, 3);
	EXPECT_EQ(Presences[0].m_Title, "小猫");
	EXPECT_EQ(Presences[0].m_RemainingSeconds, 15);
	json_value_free(pJson);
}
