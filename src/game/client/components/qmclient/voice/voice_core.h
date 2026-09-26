// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_CORE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_CORE_H

#include "voice_utils.h"

#include <base/lock.h>
#include <base/system.h>
#include <base/types.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <SDL_audio.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class CGameClient;
class IClient;
class IConsole;
class IEngineGraphics;
struct OpusDecoder;
struct OpusEncoder;
struct DenoiseState;

struct SVoiceOverlayEntry
{
	int m_ClientId = -1;
	uint64_t m_Order = 0;
	bool m_IsLocal = false;
	float m_Level = 0.0f;
	char m_aName[MAX_NAME_LENGTH] = {};
};

class CVoiceOverlayState
{
public:
	void Reset();
	void NoteSpeaker(int ClientId, const char *pName, bool IsLocal, int64_t Timestamp, float Level);
	int CollectVisible(int64_t Now, int64_t VisibleWindow, bool ShowLocalWhenActive, size_t MaxEntries, std::array<SVoiceOverlayEntry, MAX_CLIENTS> &aEntries);

private:
	std::array<int64_t, MAX_CLIENTS> m_aLastHeard = {};
	std::array<uint64_t, MAX_CLIENTS> m_aOrder = {};
	std::array<bool, MAX_CLIENTS> m_aIsLocal = {};
	std::array<float, MAX_CLIENTS> m_aLevels = {};
	std::array<std::array<char, MAX_NAME_LENGTH>, MAX_CLIENTS> m_aaNames = {};
	uint64_t m_NextOrder = 1;
};

struct SRClientVoiceConfigSnapshot
{
	int m_QmVoiceFilterEnable = 0;
	int m_QmVoiceAgcEnable = 0;
	int m_QmVoiceBitrateProfile = 0;
	int m_QmVoiceProtocolVersion = 0;
	int m_QmVoiceNoiseSuppressEnable = 0;
	int m_QmVoiceNoiseSuppressStrength = 0;
	int m_QmVoicePttReleaseDelayMs = 0;
	int m_QmVoiceCompThreshold = 0;
	int m_QmVoiceCompRatio = 0;
	int m_QmVoiceCompAttackMs = 0;
	int m_QmVoiceCompReleaseMs = 0;
	int m_QmVoiceCompMakeup = 0;
	int m_QmVoiceLimiter = 0;
	int m_QmVoiceStereo = 0;
	int m_QmVoiceStereoWidth = 0;
	int m_QmVoiceRadius = 0;
	int m_QmVoiceVolume = 0;
	int m_QmVoiceMicVolume = 0;
	int m_QmVoiceMicMute = 0;
	int m_QmVoiceTestMode = 0;
	int m_QmVoiceVadEnable = 0;
	int m_QmVoiceVadThreshold = 0;
	int m_QmVoiceVadReleaseDelayMs = 0;
	int m_QmVoiceIgnoreDistance = 0;
	int m_QmVoiceGroupGlobal = 0;
	int m_QmVoiceVisibilityMode = 0;
	int m_QmVoiceListMode = 0;
	int m_QmVoiceDebug = 0;
	int m_QmVoiceGroupMode = 0;
	int m_QmVoiceHearOnSpecPos = 0;
	int m_QmVoiceHearPeoplesInSpectate = 0;
	int m_QmVoiceHearVad = 0;
	int m_ClShowOthers = 0;
	uint32_t m_QmVoiceTokenHash = 0;
	char m_aQmVoiceWhitelist[512] = {};
	char m_aQmVoiceBlacklist[512] = {};
	char m_aQmVoiceMute[512] = {};
	char m_aQmVoiceVadAllow[512] = {};
	char m_aQmVoiceNameVolumes[512] = {};
};

class CRClientVoice
{
	struct SVoicePeer
	{
		static constexpr int MAX_JITTER_PACKETS = 32;
		static constexpr int MAX_PACKET_BYTES = 1500;
		static constexpr int MAX_FRAMES = 8;
		struct SVoiceFrame
		{
			int16_t m_aPcm[960] = {};
			int m_Samples = 0;
			float m_LeftGain = 1.0f;
			float m_RightGain = 1.0f;
		};
		struct SJitterPacket
		{
			bool m_Valid = false;
			uint16_t m_Seq = 0;
			int m_Size = 0;
			float m_LeftGain = 1.0f;
			float m_RightGain = 1.0f;
			uint8_t m_aData[MAX_PACKET_BYTES] = {};
		};

		OpusDecoder *m_pDecoder = nullptr;
		bool m_DecoderFailed = false;
		uint16_t m_LastSeq = 0;
		bool m_HasSeq = false;
		uint16_t m_LastRecvSeq = 0;
		bool m_HasLastRecvSeq = false;
		float m_LossEwma = 0.0f;
		uint16_t m_NextSeq = 0;
		bool m_HasNextSeq = false;
		bool m_HasMinQueuedSeq = false;
		uint16_t m_MinQueuedSeq = 0;
		float m_LastGainLeft = 1.0f;
		float m_LastGainRight = 1.0f;
		int64_t m_LastRecvTime = 0;
		float m_JitterMs = 0.0f;
		int m_TargetFrames = 3;
		int m_QueuedPackets = 0;
		SJitterPacket m_aPackets[MAX_JITTER_PACKETS] = {};
		SVoiceFrame m_aFrames[MAX_FRAMES] = {};
		int m_FrameHead = 0;
		int m_FrameTail = 0;
		int m_FrameCount = 0;
		int m_FrameReadPos = 0;
	};

	CGameClient *m_pGameClient = nullptr;
	IClient *m_pClient = nullptr;
	IConsole *m_pConsole = nullptr;
	IEngineGraphics *m_pGraphics = nullptr;

	std::unique_ptr<VoiceUtils::CVoiceWebSocketTransport> m_pVoiceTransport;
	std::atomic<bool> m_ServerAddrValid = false;
	char m_aServerAddrStr[256] = {0};

	SDL_AudioDeviceID m_CaptureDevice = 0;
	SDL_AudioDeviceID m_OutputDevice = 0;
	SDL_AudioSpec m_CaptureSpec = {};
	SDL_AudioSpec m_OutputSpec = {};
	char m_aAudioBackend[64] = {0};
	char m_aAudioBackendMismatchReq[64] = {0};
	char m_aAudioBackendMismatchCur[64] = {0};
	char m_aAudioInitLoggedBackend[64] = {0};
	char m_aSocketErrorLog[256] = {0};
	char m_aAudioErrorLog[256] = {0};
	char m_aEncoderErrorLog[256] = {0};
	char m_aServerAddrErrorLog[256] = {0};
	char m_aDecoderErrorLog[256] = {0};
	bool m_AudioSubsystemInitializedByVoice = false;
#if defined(CONF_PLATFORM_EMSCRIPTEN)
	bool m_UnsupportedPlatformLogged = false;
#endif
	char m_aInputDeviceName[128] = {0};
	char m_aOutputDeviceName[128] = {0};
	char m_aResolvedInputDeviceName[128] = {0};
	char m_aResolvedOutputDeviceName[128] = {0};
	int64_t m_LastAudioRetryAttempt = 0;
	bool m_OutputStereo = true;
	bool m_AudioPausedForInactive = false;
	bool m_LogDeviceChange = false;
	std::atomic<bool> m_CaptureUnavailable = false;
	std::atomic<bool> m_OutputUnavailable = false;
#if defined(CONF_PLATFORM_ANDROID)
	bool m_AndroidRecordPermissionKnown = false;
	bool m_AndroidRecordPermissionGranted = false;
#endif
	float m_HpfPrevIn = 0.0f;
	float m_HpfPrevOut = 0.0f;
	float m_CompEnv = 0.0f;
	float m_AgcGain = 1.0f;
	float m_NsNoiseFloor = 0.0f;
	float m_NsGain = 1.0f;
	DenoiseState *m_pNoiseSuppress = nullptr;
	bool m_NoiseSuppressFallbackLogged = false;
	std::atomic<int> m_OutputChannels = 0;

	OpusEncoder *m_pEncoder = nullptr;
	int m_EncBitrate = 36000;
	int m_EncLossPerc = 0;
	bool m_EncFec = false;
	int m_EncComplexity = 8;
	int64_t m_LastEncUpdate = 0;
	std::atomic<int> m_PingMs = -1;
	std::atomic<float> m_MicLevel = 0.0f;
	std::atomic<int64_t> m_LastTxPacketTime = 0;
	std::atomic<int64_t> m_LastRxPacketTime = 0;
	std::atomic<int64_t> m_LastMediaRxPacketTime = 0;
	std::atomic<bool> m_CaptureReady = false;
	std::atomic<bool> m_OutputReady = false;
	std::atomic<bool> m_EncoderReady = false;
	std::atomic<bool> m_TransportReady = false;
	std::atomic<bool> m_TransportConnecting = false;
	// Connection runtime state: last successful transport activity and keepalive.
	int64_t m_LastPingSentTime = 0;
	uint16_t m_LastPingSeq = 0;
	std::unique_ptr<std::array<SVoicePeer, MAX_CLIENTS>> m_pPeers;
	std::vector<int32_t> m_MixBuffer;
	std::array<std::atomic<int64_t>, MAX_CLIENTS> m_aLastHeard = {};
	// Room/peer runtime state: "who is present / speaking" for the current token.
	std::array<std::atomic<int64_t>, MAX_CLIENTS> m_aRoomMemberSeen = {};
	std::array<std::atomic<float>, MAX_CLIENTS> m_aSpeakerLevel = {};
	std::atomic<uint32_t> m_RoomMemberTokenHash = 0;
	std::array<uint64_t, MAX_CLIENTS> m_aOverlayOrder = {};
	uint64_t m_NextOverlayOrder = 1;

	std::atomic<bool> m_PttActive = false;
	std::atomic<int64_t> m_PttReleaseDeadline = 0;
	std::atomic<bool> m_VadActive = false;
	std::atomic<int64_t> m_VadReleaseDeadline = 0;
	std::atomic<bool> m_TxWasActive = false;
	std::atomic<uint16_t> m_Sequence = 0;
	std::atomic<uint32_t> m_ContextHash = 0;
	int64_t m_LastKeepalive = 0;
	uint32_t m_LastTokenHashSent = 0;

	// Debug counters (worker thread only)
	int64_t m_TxLastLog = 0;
	int m_TxPackets = 0;
	int64_t m_RxLastLog = 0;
	int m_RxPackets = 0;
	int m_RxDropHeader = 0;
	int m_RxDropVersion = 0;
	int m_RxDropType = 0;
	int m_RxDropContext = 0;
	int m_RxDropGroup = 0;
	int m_RxDropSender = 0;
	int m_RxDropPayload = 0;
	int m_RxDropRadius = 0;
	int64_t m_DebugStateLastLog = 0;

	std::thread m_Worker;
	std::atomic<bool> m_WorkerStop = false;
	std::atomic<bool> m_WorkerEnabled = false;
	std::atomic<bool> m_AudioRefreshRequested = true;
	std::atomic<bool> m_ShutdownDone = true;
	int64_t m_LastConfigSnapshotUpdate = 0;
	int64_t m_LastClientSnapshotUpdate = 0;
	CLock m_ServerAddrMutex;

	mutable CLock m_ConfigMutex;
	SRClientVoiceConfigSnapshot m_ConfigSnapshot = {};
	mutable CLock m_RequestedAudioDeviceConfigMutex;
	mutable CLock m_DeviceRouteMutex;
	mutable CLock m_DiagnosticLogMutex;
	// Main thread owns desired audio selection. Worker threads only consume the
	// latched copy through GetRequestedAudioDeviceConfig() and never read live
	// g_Config audio-device fields directly.
	VoiceUtils::SVoiceAudioDeviceConfig m_RequestedAudioDeviceConfigMain = {};

	mutable CLock m_SnapshotMutex;
	int m_LocalClientIdSnap = -1;
	std::array<int, 2> m_aLocalClientIdsSnap = {};
	bool m_OnlineSnap = false;
	bool m_SpecActiveSnap = false;
	vec2 m_SpecPosSnap = vec2(0.0f, 0.0f);
	std::array<vec2, MAX_CLIENTS> m_aClientPosSnap = {};
	std::array<std::array<char, MAX_NAME_LENGTH>, MAX_CLIENTS> m_aClientNameSnap = {};
	std::array<uint8_t, MAX_CLIENTS> m_aClientOtherTeamSnap = {};
	std::array<uint8_t, MAX_CLIENTS> m_aClientActiveSnap = {};
	std::array<uint8_t, MAX_CLIENTS> m_aClientSpecSnap = {};

	void UpdateVoiceTransport() NO_THREAD_SAFETY_ANALYSIS;
	bool SendVoicePacket(const uint8_t *pData, size_t Size) NO_THREAD_SAFETY_ANALYSIS;
	bool EnsureAudio() NO_THREAD_SAFETY_ANALYSIS;
	void UpdateServerAddrConfig();
	bool UpdateContext();
	void UpdateClientSnapshot(bool Force = false);
	void UpdateConfigSnapshot(bool Force = false);
	void GetConfigSnapshot(SRClientVoiceConfigSnapshot &Out) const;
	void BuildRequestedAudioDeviceConfig(VoiceUtils::SVoiceAudioDeviceConfig &Out) const;
	void SetRequestedAudioDeviceConfig(const VoiceUtils::SVoiceAudioDeviceConfig &Config) NO_THREAD_SAFETY_ANALYSIS;
	void GetRequestedAudioDeviceConfig(VoiceUtils::SVoiceAudioDeviceConfig &Out) const NO_THREAD_SAFETY_ANALYSIS;
	bool RememberDiagnosticLogMessage(char *pField, size_t FieldSize, const char *pMessage) NO_THREAD_SAFETY_ANALYSIS;
	void LogDiagnosticErrorOnce(char *pField, size_t FieldSize, const char *pMessage) NO_THREAD_SAFETY_ANALYSIS;
	void ClearDiagnosticLogMessage(char *pField) NO_THREAD_SAFETY_ANALYSIS;
	void CopyDiagnosticLogMessage(const char *pField, char *pBuf, size_t BufSize) const NO_THREAD_SAFETY_ANALYSIS;
	bool HasConnectionRuntimeState() const;
	bool HasPeerRuntimeState(uint32_t RoomTokenHash) const;
	void ResetRuntimeState(uint32_t Flags, uint32_t RoomTokenHash) NO_THREAD_SAFETY_ANALYSIS;
	void ResetTransmitState(bool ClearQueuedCapture) NO_THREAD_SAFETY_ANALYSIS;
	void ProcessCapture() NO_THREAD_SAFETY_ANALYSIS;
	void ProcessIncoming() NO_THREAD_SAFETY_ANALYSIS;
	void DecodeJitter();
	void UpdateEncoderParams();
	void UpdateMicLevel(float Peak);
	void PushPeerFrame(int PeerId, const int16_t *pPcm, int Samples, float LeftGain, float RightGain);
	void MixAudio(int16_t *pOut, int Samples, int OutputChannels);
	void ClearPeerFrames();
	void ResetPeer(SVoicePeer &Peer);
	bool FindMinLiveQueuedSeq(const SVoicePeer &Peer, uint16_t &OutSeq) const;
	bool SeedPeerNextSeq(SVoicePeer &Peer);
	static void SDLAudioCallback(void *pUserData, Uint8 *pStream, int Len);
	const char *FindDeviceName(bool Capture, const char *pDesired) const;
	void StartWorker();
	void StopWorker();
	void WorkerLoop() NO_THREAD_SAFETY_ANALYSIS;

public:
	void SetInterfaces(CGameClient *pGameClient, IClient *pClient, IConsole *pConsole);
	bool Init();
	void Init(CGameClient *pGameClient, IClient *pClient, IConsole *pConsole);
	void Shutdown() NO_THREAD_SAFETY_ANALYSIS;
	void OnShutdown();
	void OnFrame();
	void OnRender() NO_THREAD_SAFETY_ANALYSIS;
	void RenderSpeakerOverlay();
	void SetPttActive(bool Active);
	void ListDevices();
	void ExportOverlayState(CVoiceOverlayState &Overlay) const;
	void ExportUiStatus(VoiceUtils::SVoiceUiStatus &Out) const NO_THREAD_SAFETY_ANALYSIS;
	int PingMs() const { return m_PingMs.load(); }
	float MicLevel() const { return m_MicLevel.load(); }
	bool IsSpeaking() const { return m_TxWasActive.load(); }
	bool IsCaptureUnavailable() const { return m_CaptureUnavailable.load(); }
	bool IsOutputUnavailable() const { return m_OutputUnavailable.load(); }
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_CORE_H
