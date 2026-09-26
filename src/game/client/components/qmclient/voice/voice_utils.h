// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_UTILS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_UTILS_H

#include <base/types.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>
#include <engine/shared/websocket_client.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

constexpr int VOICE_SAMPLE_RATE = 48000;
constexpr int VOICE_FRAME_SAMPLES = 960;
constexpr uint8_t VOICE_VERSION = 3;
constexpr uint8_t VOICE_TYPE_AUDIO = 1;
constexpr uint8_t VOICE_TYPE_PING = 2;
constexpr uint8_t VOICE_TYPE_PONG = 3;
constexpr int VOICE_MAX_PACKET = 1200;
constexpr int VOICE_PACKET_HEADER_SIZE = 4 + 1 + 1 + 2 + 4 + 4 + 1 + 2 + 2 + 4 + 4;
constexpr int VOICE_MAX_PAYLOAD = VOICE_MAX_PACKET - VOICE_PACKET_HEADER_SIZE;
constexpr uint8_t VOICE_FLAG_VAD = 1 << 0;
constexpr uint8_t VOICE_FLAG_LOOPBACK = 1 << 1;
constexpr uint8_t VOICE_ALLOWED_FLAGS = VOICE_FLAG_VAD | VOICE_FLAG_LOOPBACK;
constexpr int VOICE_NOISE_SUPPRESS_OFF = 0;
constexpr int VOICE_NOISE_SUPPRESS_SIMPLE = 1;
constexpr int VOICE_NOISE_SUPPRESS_RNNOISE = 2;
inline constexpr float VOICE_HPF_CUTOFF_HZ = 120.0f;

namespace VoiceUtils
{
	const char *EffectiveVoiceWebSocketUrl(const char *pUrl);

	// 语音 worker 独占传输对象，停止 worker 后才从外部销毁。
	class CVoiceWebSocketTransport
	{
		std::unique_ptr<IQmWebSocketClient> m_pClient;
		std::string m_Url;
		std::string m_Error;
		uint32_t m_ContextHash = 0;
		uint32_t m_TokenHash = 0;
		uint8_t m_ProtocolVersion = 0;
		bool m_Enabled = false;
		bool m_UrlValid = false;
		bool m_Connected = false;
		int64_t m_LastConnectedTick = 0;

	public:
		CVoiceWebSocketTransport();
		explicit CVoiceWebSocketTransport(std::unique_ptr<IQmWebSocketClient> pClient);
		bool Update(const char *pUrl, bool Enabled, uint32_t ContextHash, uint32_t TokenHash, uint8_t ProtocolVersion);
		void Disconnect();
		bool Connected() const;
		bool Connecting() const;
		bool UrlValid() const { return m_UrlValid; }
		const char *LastError() const;
		bool SendPacket(const uint8_t *pData, size_t Size);
		bool PollPacket(SQmWebSocketMessage &Out);
	};
	struct SVoicePacketHeader
	{
		// Keep this layout in sync with WriteVoicePacketHeader/ReadVoicePacketHeader.
		uint8_t m_Version = VOICE_VERSION;
		// AUDIO carries Opus payload, PING/PONG are keepalive/RTT probes.
		uint8_t m_Type = VOICE_TYPE_AUDIO;
		// Payload starts immediately after the fixed-size header.
		uint16_t m_PayloadSize = 0;
		// Filters packets to the current gameplay session/server context.
		uint32_t m_ContextHash = 0;
		// Encodes room/group identity used for hearability checks.
		uint32_t m_TokenHash = 0;
		uint8_t m_Flags = 0;
		uint16_t m_SenderId = 0;
		uint16_t m_Sequence = 0;
		float m_PosX = 0.0f;
		float m_PosY = 0.0f;
	};

	struct SVoiceAudioDeviceConfig
	{
		char m_aBackend[64] = {};
		char m_aInputDevice[128] = {};
		char m_aOutputDevice[128] = {};
		bool m_OutputStereo = false;
	};

	struct SVoiceProcessingFactoryDefaults
	{
		int m_NoiseSuppressMode = VOICE_NOISE_SUPPRESS_OFF;
		int m_NoiseSuppressStrength = 35;
		float m_HpfCutoffHz = VOICE_HPF_CUTOFF_HZ;
		float m_CompressorThreshold = 0.24f;
		float m_CompressorRatio = 2.0f;
		float m_CompressorAttackSec = 0.012f;
		float m_CompressorReleaseSec = 0.140f;
		float m_CompressorMakeupGain = 1.25f;
		float m_Limiter = 0.92f;
		int m_EncoderComplexity = 8;
	};

	struct SVoiceAgcConfig
	{
		bool m_Enable = false;
		float m_TargetRms = 0.18f;
		float m_MaxGain = 2.0f;
		float m_MinGain = 0.75f;
		float m_AttackSec = 0.050f;
		float m_ReleaseSec = 0.350f;
	};

	struct SVoiceDeviceDropdownEntry
	{
		std::string m_DisplayName;
		std::string m_ConfigValue;
		bool m_Disconnected = false;
	};

	void WriteU16(uint8_t *pBuf, uint16_t Value);
	void WriteU32(uint8_t *pBuf, uint32_t Value);
	void WriteFloat(uint8_t *pBuf, float Value);
	uint16_t ReadU16(const uint8_t *pBuf);
	uint32_t ReadU32(const uint8_t *pBuf);
	float ReadFloat(const uint8_t *pBuf);
	bool WriteVoicePacketHeader(uint8_t *pBuf, size_t BufSize, const SVoicePacketHeader &Header);
	bool ReadVoicePacketHeader(const uint8_t *pBuf, size_t BufSize, SVoicePacketHeader &OutHeader);
	const char *VoicePacketTypeName(uint8_t Type);
	bool VoiceAudioDeviceConfigEquals(const SVoiceAudioDeviceConfig &Left, const SVoiceAudioDeviceConfig &Right);
	int VoiceDesiredOutputChannels(const SVoiceAudioDeviceConfig &Config);
	SVoiceProcessingFactoryDefaults VoiceProcessingFactoryDefaults();
	SVoiceAgcConfig VoiceAgcConfigFromRuntime(bool EnableAgc);
	int VoiceClampJitterTarget(float JitterMs);
	int VoiceSeqDelta(uint16_t NewSeq, uint16_t OldSeq);
	bool VoiceSeqLess(uint16_t A, uint16_t B);
	enum class EVoiceProcessStage
	{
		AGC_GAIN,
		MIC_GAIN,
		DENOISE,
		HPF_COMPRESSOR,
	};

	using VoiceProcessTraceCallback = void (*)(EVoiceProcessStage, void *pUserData);

	// Test/debug hook for observing the capture processing order. The callback may
	// be installed from tests while the voice runtime thread is active.
	void SetVoiceProcessTraceCallback(VoiceProcessTraceCallback pCallback, void *pUserData);
	void TraceVoiceProcessStage(EVoiceProcessStage Stage);
	void BuildVoiceDeviceDropdownEntries(
		const std::vector<std::string> &vDetectedDeviceNames,
		const char *pCurrentDevice,
		const char *pDefaultLabel,
		const char *pDisconnectedSuffix,
		std::vector<SVoiceDeviceDropdownEntry> &vEntries);
	int VoiceFindSelectedDeviceIndex(const std::vector<SVoiceDeviceDropdownEntry> &vEntries, const char *pCurrentDevice);
	int ResolveNoiseSuppressMode(int ConfigValue, bool RnnoiseRuntimeAvailable, bool *pFallbackUsed);
	bool IsRnnoiseCompiledIn();

	enum class EVoiceIncomingPacketDecision
	{
		DROP_HEADER,
		DROP_VERSION,
		DROP_TYPE,
		DROP_FLAGS,
		DROP_CONTEXT,
		DROP_GROUP,
		DROP_SENDER,
		DROP_PAYLOAD,
		DROP_KEEPALIVE_TOKEN,
		HANDLE_AUDIO,
		HANDLE_PING,
		HANDLE_PONG,
	};

	struct SVoiceIncomingPacketContext
	{
		uint8_t m_ProtocolVersion = VOICE_VERSION;
		uint32_t m_LocalContextHash = 0;
		uint32_t m_LocalTokenHash = 0;
		int m_MaxClients = MAX_CLIENTS;
	};

	EVoiceIncomingPacketDecision ClassifyVoiceIncomingPacket(const SVoicePacketHeader &Header, size_t PacketSize, const SVoiceIncomingPacketContext &Context);
	bool VoiceShouldUpdatePingRtt(uint16_t Sequence, uint16_t LastPingSeq, int64_t LastPingSentTime);
	uint32_t VoiceTokenGroupHash(uint32_t TokenHash);
	uint32_t BuildLegacyVoiceTokenHash(const char *pToken);
	bool FindMinLiveVoiceSeq(const uint8_t *pValid, const uint16_t *pSeq, size_t Count, uint16_t &OutSeq);
	bool SeedVoiceJitterStartSeq(int QueuedPackets, int TargetFrames, bool HasNextSeq, uint16_t InitialNextSeq, const uint8_t *pValid, const uint16_t *pSeq, size_t Count, bool &OutHasNextSeq, uint16_t &OutNextSeq);
	bool VoiceShouldIgnoreDistance(bool IgnoreDistanceConfig, bool GroupGlobal, uint32_t LocalTokenHash, uint32_t SenderTokenHash);
	vec2 VoiceResolveListenerPosition(vec2 LocalPos, bool SpecActive, vec2 SpecPos, bool HearOnSpecPos);

	enum class EVoiceReceiveAudibility
	{
		ALLOW,
		DROP_SELF,
		DROP_INACTIVE,
		DROP_OTHER_TEAM,
		DROP_MUTED,
		DROP_NOT_WHITELISTED,
		DROP_BLACKLISTED,
		DROP_VAD_BLOCKED,
	};

	struct SVoiceReceiveAudibilityContext
	{
		bool m_IsSelf = false;
		bool m_TestServer = false;
		bool m_IgnoreDistance = false;
		int m_VisibilityMode = 0;
		bool m_HearPeoplesInSpectate = false;
		bool m_SenderOtherTeam = false;
		bool m_SenderActive = false;
		bool m_SenderSpec = false;
		int m_ListMode = 0;
		bool m_HearVad = false;
		bool m_SenderUsesVad = false;
		const char *m_pMuteList = "";
		const char *m_pWhitelist = "";
		const char *m_pBlacklist = "";
		const char *m_pVadAllow = "";
	};

	EVoiceReceiveAudibility EvaluateVoiceReceiveAudibility(const SVoiceReceiveAudibilityContext &Context, const char *pSenderName);
	bool VoiceIsPacketWithinAudibleRadius(vec2 LocalPos, vec2 SenderPos, float Radius, bool IgnoreDistance);

	struct SVoiceTransmitPreconditions
	{
		bool m_NeedNetwork = false;
		bool m_ServerAddrValid = false;
		bool m_HaveSocket = false;
		bool m_Online = false;
		bool m_HaveCaptureDevice = false;
		bool m_HaveEncoder = false;
		bool m_MicMuted = false;
	};

	struct SVoiceAudioRefreshState
	{
		bool m_BackendChanged = false;
		bool m_InputDeviceChanged = false;
		bool m_OutputDeviceChanged = false;
		bool m_StereoChanged = false;
		bool m_EncoderReady = false;
		bool m_OutputReady = false;
		bool m_CaptureReady = false;
		bool m_OutputUnavailable = false;
		bool m_CaptureUnavailable = false;
		int m_CurrentOutputChannels = 0;
		int m_DesiredOutputChannels = 0;
	};

	struct SVoiceUiStatus
	{
		bool m_Enabled = false;
		bool m_NeedNetwork = true;
		bool m_AudioRefreshPending = false;
		bool m_ServerAddrValid = false;
		bool m_HaveSocket = false;
		bool m_Connecting = false;
		bool m_Online = false;
		bool m_CaptureReady = false;
		bool m_CaptureUnavailable = false;
		bool m_OutputReady = false;
		bool m_OutputUnavailable = false;
		bool m_EncoderReady = false;
		bool m_MicMuted = false;
		bool m_TxActive = false;
		bool m_HaveRecentRx = false;
		bool m_HaveRecentPeers = false;
		int m_PingMs = -1;
		int m_ActivePeerCount = 0;
		int m_TxAgeMs = -1;
		int m_RxAgeMs = -1;
		float m_MicLevel = 0.0f;
		char m_aRequestedInputDevice[128] = {};
		char m_aResolvedInputDevice[128] = {};
		char m_aRequestedOutputDevice[128] = {};
		char m_aResolvedOutputDevice[128] = {};
		char m_aAudioError[256] = {};
		char m_aNetworkError[256] = {};
		char m_aCodecError[256] = {};
	};

	enum class EVoiceAudioIssue
	{
		NONE,
		INPUT_DEVICE_NOT_FOUND,
		OUTPUT_DEVICE_NOT_FOUND,
		NO_CAPTURE_DEVICES,
		NO_OUTPUT_DEVICES,
		OPEN_CAPTURE_FAILED,
		OPEN_OUTPUT_FAILED,
		PERMISSION_DENIED,
		BACKEND_INIT_FAILED,
		UNKNOWN,
	};

	inline constexpr uint32_t VOICE_TX_BLOCK_SERVER_ADDR = 1u << 0;
	inline constexpr uint32_t VOICE_TX_BLOCK_SOCKET = 1u << 1;
	inline constexpr uint32_t VOICE_TX_BLOCK_ONLINE = 1u << 2;
	inline constexpr uint32_t VOICE_TX_BLOCK_CAPTURE = 1u << 3;
	inline constexpr uint32_t VOICE_TX_BLOCK_ENCODER = 1u << 4;
	inline constexpr uint32_t VOICE_TX_BLOCK_MIC_MUTED = 1u << 5;
	inline constexpr uint32_t VOICE_RUNTIME_RESET_PEERS = 1u << 0;
	inline constexpr uint32_t VOICE_RUNTIME_RESET_CONNECTION = 1u << 1;

	uint32_t VoiceTransmitBlockers(const SVoiceTransmitPreconditions &Preconditions);
	void FormatVoiceTransmitBlockers(uint32_t Blockers, char *pBuf, int BufSize);
	bool VoiceNeedsAudioRefresh(const SVoiceAudioRefreshState &State);
	void ComputeVoiceEncoderTargets(int LossPerc, float JitterMax, int BitrateProfile, int *pTargetBitrate, int *pTargetLoss, bool *pTargetFec);
	void ComputeVoiceEncoderTargetsWithComplexity(int LossPerc, float JitterMax, int BitrateProfile, int *pTargetBitrate, int *pTargetLoss, bool *pTargetFec, int *pTargetComplexity);
	float ComputeVoiceAutoGain(float CurrentGain, float FrameRms, const SVoiceAgcConfig &Config);
	uint32_t VoiceRuntimeResetFlags(bool ContextChanged, bool Online, uint32_t PreviousRoomTokenHash, uint32_t CurrentRoomTokenHash);
	const char *VoiceUiMicStatus(const SVoiceUiStatus &Status);
	const char *VoiceUiOutputStatus(const SVoiceUiStatus &Status);
	const char *VoiceUiServerStatus(const SVoiceUiStatus &Status);
	const char *VoiceUiRoomStatus(const SVoiceUiStatus &Status);
	const char *VoiceUiTransportStatus(const SVoiceUiStatus &Status);
	const char *VoiceUiActionHint(const SVoiceUiStatus &Status);
	const char *VoiceUiPrimaryError(const SVoiceUiStatus &Status);
	const char *VoiceUiInputRouteStatus(const SVoiceUiStatus &Status);
	const char *VoiceUiOutputRouteStatus(const SVoiceUiStatus &Status);
	bool VoiceAudioErrorLooksLikePermissionDenied(const char *pError);
	EVoiceAudioIssue ClassifyVoiceAudioIssue(const SVoiceUiStatus &Status);
	const char *VoiceUiAudioFailureHint(const SVoiceUiStatus &Status);
	const char *VoiceUiAudioIssueKey(const SVoiceUiStatus &Status);

	float SanitizeFloat(float Value);

	float VoiceFramePeak(const int16_t *pSamples, int Count);
	float VoiceFrameRms(const int16_t *pSamples, int Count);

	bool VoiceListMatch(const char *pList, const char *pName);
	bool VoiceNameVolume(const char *pList, const char *pName, int &OutPercent);

	void ApplyMicGain(float Gain, int16_t *pSamples, int Count);
	void BlendDenoisedFrame(const int16_t *pDrySamples, int16_t *pWetSamples, int Count, float WetMix);

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
