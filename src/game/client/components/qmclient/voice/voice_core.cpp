// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "voice_core.h"

#include "voice_capture_pipeline.h"
#include "voice_utils.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/textrender.h>

#include <game/client/components/qmclient/qmclient.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon_manager.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

#include <opus/opus.h>

#if defined(CONF_RNNOISE)
#include <rnnoise.h>
#endif

#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

static constexpr int VOICE_CLIENT_SNAPSHOT_INTERVAL_MS = 10;
static constexpr int VOICE_MAX_CAPTURE_FRAMES_PER_UPDATE = 3;
static constexpr int VOICE_CONFIG_SNAPSHOT_INTERVAL_MS = 50;
static constexpr int VOICE_OVERLAY_VISIBLE_MS = 180;
static constexpr int VOICE_OVERLAY_MAX_SPEAKERS = 5;
static constexpr const char *s_pVoiceOverlayMicIcon = "\xEF\x84\xB0";

void CVoiceOverlayState::Reset()
{
	m_aLastHeard.fill(0);
	m_aOrder.fill(0);
	m_aIsLocal.fill(false);
	m_aLevels.fill(0.0f);
	for(auto &aName : m_aaNames)
		aName[0] = '\0';
	m_NextOrder = 1;
}

void CVoiceOverlayState::NoteSpeaker(int ClientId, const char *pName, bool IsLocal, int64_t Timestamp, float Level)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || pName == nullptr || pName[0] == '\0')
		return;

	m_aLastHeard[ClientId] = Timestamp;
	m_aIsLocal[ClientId] = IsLocal;
	m_aLevels[ClientId] = std::clamp(Level, 0.0f, 1.0f);
	str_copy(m_aaNames[ClientId].data(), pName, m_aaNames[ClientId].size());
	if(m_aOrder[ClientId] == 0)
	{
		m_aOrder[ClientId] = m_NextOrder++;
		if(m_NextOrder == 0)
			m_NextOrder = 1;
	}
}

int CVoiceOverlayState::CollectVisible(int64_t Now, int64_t VisibleWindow, bool ShowLocalWhenActive, size_t MaxEntries, std::array<SVoiceOverlayEntry, MAX_CLIENTS> &aEntries)
{
	int EntryCount = 0;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const int64_t LastHeard = m_aLastHeard[ClientId];
		if(LastHeard <= 0 || Now - LastHeard >= VisibleWindow)
		{
			m_aOrder[ClientId] = 0;
			m_aLevels[ClientId] = 0.0f;
			continue;
		}

		if(m_aaNames[ClientId][0] == '\0')
		{
			m_aOrder[ClientId] = 0;
			m_aLevels[ClientId] = 0.0f;
			continue;
		}

		if(m_aIsLocal[ClientId] && !ShowLocalWhenActive)
		{
			m_aOrder[ClientId] = 0;
			m_aLevels[ClientId] = 0.0f;
			continue;
		}

		if(m_aOrder[ClientId] == 0)
		{
			m_aOrder[ClientId] = m_NextOrder++;
			if(m_NextOrder == 0)
				m_NextOrder = 1;
		}

		SVoiceOverlayEntry &Entry = aEntries[EntryCount++];
		Entry.m_ClientId = ClientId;
		Entry.m_Order = m_aOrder[ClientId];
		Entry.m_IsLocal = m_aIsLocal[ClientId];
		const float AgeAlpha = VisibleWindow > 0 ? std::clamp(1.0f - (Now - LastHeard) / (float)VisibleWindow, 0.0f, 1.0f) : 1.0f;
		Entry.m_Level = std::clamp(m_aLevels[ClientId] * AgeAlpha, 0.0f, 1.0f);
		str_copy(Entry.m_aName, m_aaNames[ClientId].data(), sizeof(Entry.m_aName));
	}

	if(EntryCount > 1)
	{
		std::sort(aEntries.begin(), aEntries.begin() + EntryCount, [](const SVoiceOverlayEntry &Left, const SVoiceOverlayEntry &Right) {
			if(Left.m_Order != Right.m_Order)
				return Left.m_Order < Right.m_Order;
			return Left.m_ClientId < Right.m_ClientId;
		});
	}

	const int MaxEntryCount = MaxEntries > MAX_CLIENTS ? MAX_CLIENTS : (int)MaxEntries;
	if(EntryCount > MaxEntryCount)
		EntryCount = MaxEntryCount;

	return EntryCount;
}

static constexpr int VOICE_CHANNELS = 1;
static constexpr int VOICE_FRAME_BYTES = VOICE_FRAME_SAMPLES * sizeof(int16_t);

static uint8_t VoiceProtocolVersion(const SRClientVoiceConfigSnapshot &Config)
{
	const int ProtocolVersion = Config.m_QmVoiceProtocolVersion > 0 ? Config.m_QmVoiceProtocolVersion : VOICE_VERSION;
	return (uint8_t)std::clamp(ProtocolVersion, 1, 255);
}

bool CRClientVoice::FindMinLiveQueuedSeq(const SVoicePeer &Peer, uint16_t &OutSeq) const
{
	std::array<uint8_t, SVoicePeer::MAX_JITTER_PACKETS> aValid = {};
	std::array<uint16_t, SVoicePeer::MAX_JITTER_PACKETS> aSeq = {};
	for(size_t i = 0; i < Peer.MAX_JITTER_PACKETS; i++)
	{
		aValid[i] = Peer.m_aPackets[i].m_Valid ? 1 : 0;
		aSeq[i] = Peer.m_aPackets[i].m_Seq;
	}
	return VoiceUtils::FindMinLiveVoiceSeq(aValid.data(), aSeq.data(), aValid.size(), OutSeq);
}

bool CRClientVoice::SeedPeerNextSeq(SVoicePeer &Peer)
{
	std::array<uint8_t, SVoicePeer::MAX_JITTER_PACKETS> aValid = {};
	std::array<uint16_t, SVoicePeer::MAX_JITTER_PACKETS> aSeq = {};
	for(size_t i = 0; i < Peer.MAX_JITTER_PACKETS; i++)
	{
		aValid[i] = Peer.m_aPackets[i].m_Valid ? 1 : 0;
		aSeq[i] = Peer.m_aPackets[i].m_Seq;
	}

	bool HasNextSeq = Peer.m_HasNextSeq;
	uint16_t NextSeq = Peer.m_NextSeq;
	if(!VoiceUtils::SeedVoiceJitterStartSeq(Peer.m_QueuedPackets, Peer.m_TargetFrames, HasNextSeq, NextSeq, aValid.data(), aSeq.data(), aValid.size(), HasNextSeq, NextSeq))
		return false;

	Peer.m_HasNextSeq = HasNextSeq;
	Peer.m_NextSeq = NextSeq;
	Peer.m_MinQueuedSeq = NextSeq;
	Peer.m_HasMinQueuedSeq = true;
	return true;
}

void CRClientVoice::SDLAudioCallback(void *pUserData, Uint8 *pStream, int Len)
{
	auto *pThis = static_cast<CRClientVoice *>(pUserData);
	if(!pThis || Len <= 0)
		return;

	const int OutputChannels = std::max(1, pThis->m_OutputChannels.load());
	const int Samples = Len / (int)(sizeof(int16_t) * OutputChannels);
	if(Samples <= 0)
	{
		mem_zero(pStream, Len);
		return;
	}

	pThis->MixAudio(reinterpret_cast<int16_t *>(pStream), Samples, OutputChannels);
}

void CRClientVoice::Init(CGameClient *pGameClient, IClient *pClient, IConsole *pConsole)
{
	SetInterfaces(pGameClient, pClient, pConsole);
	Init();
}

void CRClientVoice::SetInterfaces(CGameClient *pGameClient, IClient *pClient, IConsole *pConsole)
{
	m_pGameClient = pGameClient;
	m_pClient = pClient;
	m_pConsole = pConsole;
	m_pGraphics = m_pGameClient ? m_pGameClient->Kernel()->RequestInterface<IEngineGraphics>() : nullptr;
}

bool CRClientVoice::Init()
{
	if(!m_pGameClient || !m_pClient || !m_pConsole)
		return false;

	m_pPeers = std::make_unique<std::array<SVoicePeer, MAX_CLIENTS>>();
	m_MixBuffer.resize(VOICE_FRAME_SAMPLES * 2);
	m_ShutdownDone = false;
	m_LastConfigSnapshotUpdate = 0;
	m_LastClientSnapshotUpdate = 0;
	UpdateConfigSnapshot(true);
	VoiceUtils::SVoiceAudioDeviceConfig RequestedAudioConfig;
	BuildRequestedAudioDeviceConfig(RequestedAudioConfig);
	SetRequestedAudioDeviceConfig(RequestedAudioConfig);
	return true;
}

void CRClientVoice::OnShutdown()
{
	Shutdown();
}

void CRClientVoice::OnFrame()
{
	OnRender();
}

void CRClientVoice::SetPttActive(bool Active)
{
	const bool WasActive = m_PttActive.exchange(Active);
	if(Active)
	{
		m_PttReleaseDeadline.store(0);
		return;
	}

	if(WasActive)
	{
		UpdateConfigSnapshot(true);
		SRClientVoiceConfigSnapshot Config;
		GetConfigSnapshot(Config);
		const int DelayMs = std::clamp(Config.m_QmVoicePttReleaseDelayMs, 0, 1000);
		if(DelayMs > 0)
			m_PttReleaseDeadline.store(time_get() + (int64_t)time_freq() * DelayMs / 1000);
		else
			m_PttReleaseDeadline.store(0);
	}
}

void CRClientVoice::BuildRequestedAudioDeviceConfig(VoiceUtils::SVoiceAudioDeviceConfig &Out) const
{
	str_copy(Out.m_aBackend, g_Config.m_QmVoiceAudioBackend, sizeof(Out.m_aBackend));
	str_copy(Out.m_aInputDevice, g_Config.m_QmVoiceInputDevice, sizeof(Out.m_aInputDevice));
	str_copy(Out.m_aOutputDevice, g_Config.m_QmVoiceOutputDevice, sizeof(Out.m_aOutputDevice));
	Out.m_OutputStereo = g_Config.m_QmVoiceStereo != 0;
}

void CRClientVoice::SetRequestedAudioDeviceConfig(const VoiceUtils::SVoiceAudioDeviceConfig &Config) NO_THREAD_SAFETY_ANALYSIS
{
	const CLockScope Guard(m_RequestedAudioDeviceConfigMutex);
	m_RequestedAudioDeviceConfigMain = Config;
}

void CRClientVoice::GetRequestedAudioDeviceConfig(VoiceUtils::SVoiceAudioDeviceConfig &Out) const NO_THREAD_SAFETY_ANALYSIS
{
	const CLockScope Guard(m_RequestedAudioDeviceConfigMutex);
	Out = m_RequestedAudioDeviceConfigMain;
}

bool CRClientVoice::RememberDiagnosticLogMessage(char *pField, size_t FieldSize, const char *pMessage) NO_THREAD_SAFETY_ANALYSIS
{
	const CLockScope Guard(m_DiagnosticLogMutex);
	if(str_comp(pField, pMessage) == 0)
		return false;

	str_copy(pField, pMessage, FieldSize);
	return true;
}

void CRClientVoice::LogDiagnosticErrorOnce(char *pField, size_t FieldSize, const char *pMessage) NO_THREAD_SAFETY_ANALYSIS
{
	if(!RememberDiagnosticLogMessage(pField, FieldSize, pMessage))
		return;

	log_error("voice", "%s", pMessage);
}

void CRClientVoice::ClearDiagnosticLogMessage(char *pField) NO_THREAD_SAFETY_ANALYSIS
{
	const CLockScope Guard(m_DiagnosticLogMutex);
	pField[0] = '\0';
}

void CRClientVoice::CopyDiagnosticLogMessage(const char *pField, char *pBuf, size_t BufSize) const NO_THREAD_SAFETY_ANALYSIS
{
	const CLockScope Guard(m_DiagnosticLogMutex);
	str_copy(pBuf, pField, BufSize);
}

void CRClientVoice::UpdateVoiceTransport() NO_THREAD_SAFETY_ANALYSIS
{
	if(!m_pVoiceTransport)
		m_pVoiceTransport = std::make_unique<VoiceUtils::CVoiceWebSocketTransport>();
	char aServerUrl[sizeof(m_aServerAddrStr)];
	{
		const CLockScope Guard(m_ServerAddrMutex);
		str_copy(aServerUrl, m_aServerAddrStr, sizeof(aServerUrl));
	}
	SRClientVoiceConfigSnapshot Config;
	GetConfigSnapshot(Config);
	bool Online = false;
	{
		const CLockScope Guard(m_SnapshotMutex);
		Online = m_OnlineSnap;
	}
	const bool NeedNetwork = Online && std::clamp(Config.m_QmVoiceTestMode, 0, 2) != 1;
	const bool Reset = m_pVoiceTransport->Update(aServerUrl, NeedNetwork, m_ContextHash.load(), Config.m_QmVoiceTokenHash, VoiceProtocolVersion(Config));
	m_ServerAddrValid.store(m_pVoiceTransport->UrlValid());
	m_TransportReady.store(m_pVoiceTransport->Connected());
	m_TransportConnecting.store(m_pVoiceTransport->Connecting());
	if(Reset)
		ResetRuntimeState(VoiceUtils::VOICE_RUNTIME_RESET_CONNECTION | VoiceUtils::VOICE_RUNTIME_RESET_PEERS, Config.m_QmVoiceTokenHash);
	const char *pError = m_pVoiceTransport->LastError();
	if(NeedNetwork && pError[0] != '\0')
	{
		char *pField = m_pVoiceTransport->UrlValid() ? m_aSocketErrorLog : m_aServerAddrErrorLog;
		LogDiagnosticErrorOnce(pField, sizeof(m_aSocketErrorLog), pError);
	}
	if(m_pVoiceTransport->UrlValid())
		ClearDiagnosticLogMessage(m_aServerAddrErrorLog);
	if(!NeedNetwork || m_pVoiceTransport->Connected())
		ClearDiagnosticLogMessage(m_aSocketErrorLog);
}

bool CRClientVoice::SendVoicePacket(const uint8_t *pData, size_t Size) NO_THREAD_SAFETY_ANALYSIS
{
	return m_pVoiceTransport && m_pVoiceTransport->SendPacket(pData, Size);
}

bool CRClientVoice::EnsureAudio()
{
	VoiceUtils::SVoiceAudioDeviceConfig RequestedAudioConfig;
	GetRequestedAudioDeviceConfig(RequestedAudioConfig);

	SDL_AudioSpec WantCapture = {};
	WantCapture.freq = VOICE_SAMPLE_RATE;
	WantCapture.format = AUDIO_S16;
	WantCapture.channels = VOICE_CHANNELS;
	WantCapture.samples = VOICE_FRAME_SAMPLES;
	WantCapture.callback = nullptr;

	const bool WantStereo = RequestedAudioConfig.m_OutputStereo;
	const int DesiredOutputChannels = VoiceUtils::VoiceDesiredOutputChannels(RequestedAudioConfig);

	SDL_AudioSpec WantOutput = {};
	WantOutput.freq = VOICE_SAMPLE_RATE;
	WantOutput.format = AUDIO_S16;
	WantOutput.channels = DesiredOutputChannels;
	WantOutput.samples = VOICE_FRAME_SAMPLES;
	WantOutput.callback = SDLAudioCallback;
	WantOutput.userdata = this;

	const bool BackendChanged = str_comp(m_aAudioBackend, RequestedAudioConfig.m_aBackend) != 0;
	if(BackendChanged)
	{
		if(m_CaptureDevice)
		{
			SDL_CloseAudioDevice(m_CaptureDevice);
			m_CaptureDevice = 0;
			m_CaptureReady.store(false);
		}
		if(m_OutputDevice)
		{
			SDL_CloseAudioDevice(m_OutputDevice);
			m_OutputDevice = 0;
			m_OutputReady.store(false);
		}
		m_CaptureSpec = {};
		m_OutputSpec = {};
		m_OutputChannels.store(0);
		ClearPeerFrames();
		str_copy(m_aAudioBackend, RequestedAudioConfig.m_aBackend, sizeof(m_aAudioBackend));
		m_aAudioBackendMismatchReq[0] = '\0';
		m_aAudioBackendMismatchCur[0] = '\0';
		m_aAudioInitLoggedBackend[0] = '\0';
		{
			const CLockScope DeviceRouteGuard(m_DeviceRouteMutex);
			m_aResolvedInputDeviceName[0] = '\0';
			m_aResolvedOutputDeviceName[0] = '\0';
		}
		ClearDiagnosticLogMessage(m_aAudioErrorLog);
		ClearDiagnosticLogMessage(m_aEncoderErrorLog);
		m_LogDeviceChange = true;
		m_CaptureUnavailable = false;
		m_OutputUnavailable = false;
		m_LastAudioRetryAttempt = 0;
	}

	const char *pRequestedBackend = RequestedAudioConfig.m_aBackend[0] ? RequestedAudioConfig.m_aBackend : nullptr;
	if((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0)
	{
		if(SDL_AudioInit(pRequestedBackend) < 0)
		{
			if(pRequestedBackend)
			{
				char aError[256];
				str_format(aError, sizeof(aError), "Failed to init audio backend '%s': %s", pRequestedBackend, SDL_GetError());
				LogDiagnosticErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
			}
			else
			{
				char aError[256];
				str_format(aError, sizeof(aError), "Failed to init audio: %s", SDL_GetError());
				LogDiagnosticErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
			}
			return false;
		}
		m_AudioSubsystemInitializedByVoice = true;
		const char *pDriver = SDL_GetCurrentAudioDriver();
		if(pDriver && pDriver[0] != '\0')
		{
			if(str_comp_nocase(m_aAudioInitLoggedBackend, pDriver) != 0)
			{
				log_info("voice", "audio initialized using backend '%s'", pDriver);
				str_copy(m_aAudioInitLoggedBackend, pDriver, sizeof(m_aAudioInitLoggedBackend));
			}
		}
		else if(m_aAudioInitLoggedBackend[0] == '\0')
		{
			log_info("voice", "audio initialized");
			str_copy(m_aAudioInitLoggedBackend, "<unknown>", sizeof(m_aAudioInitLoggedBackend));
		}
	}
	else if(pRequestedBackend && pRequestedBackend[0] != '\0')
	{
		const char *pDriver = SDL_GetCurrentAudioDriver();
		if(pDriver && str_comp_nocase(pDriver, pRequestedBackend) != 0)
		{
			const bool ReqChanged = str_comp_nocase(m_aAudioBackendMismatchReq, pRequestedBackend) != 0;
			const bool CurChanged = str_comp_nocase(m_aAudioBackendMismatchCur, pDriver) != 0;
			if(ReqChanged || CurChanged)
			{
				log_info("voice", "audio backend already initialized as '%s' (requested '%s')", pDriver, pRequestedBackend);
				str_copy(m_aAudioBackendMismatchReq, pRequestedBackend, sizeof(m_aAudioBackendMismatchReq));
				str_copy(m_aAudioBackendMismatchCur, pDriver, sizeof(m_aAudioBackendMismatchCur));
			}
		}
		else
		{
			m_aAudioBackendMismatchReq[0] = '\0';
			m_aAudioBackendMismatchCur[0] = '\0';
		}
	}

	const bool HadCapture = m_CaptureDevice != 0;
	const bool HadOutput = m_OutputDevice != 0;
	const bool HadEncoder = m_pEncoder != nullptr;

	if(str_comp(m_aInputDeviceName, RequestedAudioConfig.m_aInputDevice) != 0)
	{
		if(m_CaptureDevice)
		{
			SDL_CloseAudioDevice(m_CaptureDevice);
			m_CaptureDevice = 0;
			m_CaptureReady.store(false);
		}
		str_copy(m_aInputDeviceName, RequestedAudioConfig.m_aInputDevice, sizeof(m_aInputDeviceName));
		{
			const CLockScope DeviceRouteGuard(m_DeviceRouteMutex);
			m_aResolvedInputDeviceName[0] = '\0';
		}
		m_LogDeviceChange = true;
		m_CaptureUnavailable = false;
		m_LastAudioRetryAttempt = 0;
	}

	if(str_comp(m_aOutputDeviceName, RequestedAudioConfig.m_aOutputDevice) != 0)
	{
		if(m_OutputDevice)
		{
			SDL_CloseAudioDevice(m_OutputDevice);
			m_OutputDevice = 0;
			m_OutputReady.store(false);
		}
		str_copy(m_aOutputDeviceName, RequestedAudioConfig.m_aOutputDevice, sizeof(m_aOutputDeviceName));
		{
			const CLockScope DeviceRouteGuard(m_DeviceRouteMutex);
			m_aResolvedOutputDeviceName[0] = '\0';
		}
		m_LogDeviceChange = true;
		m_OutputUnavailable = false;
		m_LastAudioRetryAttempt = 0;
	}

	if(m_OutputStereo != WantStereo)
	{
		if(m_OutputDevice)
		{
			SDL_CloseAudioDevice(m_OutputDevice);
			m_OutputDevice = 0;
			m_OutputReady.store(false);
		}
		m_OutputStereo = WantStereo;
		{
			const CLockScope DeviceRouteGuard(m_DeviceRouteMutex);
			m_aResolvedOutputDeviceName[0] = '\0';
		}
		m_LogDeviceChange = true;
		m_OutputUnavailable = false;
		m_LastAudioRetryAttempt = 0;
	}

	if(HadCapture && HadOutput && HadEncoder && m_CaptureDevice && m_OutputDevice && m_pEncoder)
	{
		return true;
	}

	const char *pInputName = FindDeviceName(true, RequestedAudioConfig.m_aInputDevice);
	const char *pOutputName = FindDeviceName(false, RequestedAudioConfig.m_aOutputDevice);

	if(!m_pEncoder)
	{
		int Error = 0;
		m_pEncoder = opus_encoder_create(VOICE_SAMPLE_RATE, VOICE_CHANNELS, OPUS_APPLICATION_VOIP, &Error);
		if(!m_pEncoder || Error != OPUS_OK)
		{
			m_EncoderReady.store(false);
			char aError[256];
			str_format(aError, sizeof(aError), "Failed to create Opus encoder: %d", Error);
			LogDiagnosticErrorOnce(m_aEncoderErrorLog, sizeof(m_aEncoderErrorLog), aError);
			return false;
		}
		m_EncoderReady.store(true);
		ClearDiagnosticLogMessage(m_aEncoderErrorLog);
		VoiceUtils::ComputeVoiceEncoderTargetsWithComplexity(0, 0.0f, g_Config.m_QmVoiceBitrateProfile, &m_EncBitrate, &m_EncLossPerc, &m_EncFec, &m_EncComplexity);
		m_LastEncUpdate = 0;
		opus_encoder_ctl(m_pEncoder, OPUS_SET_BITRATE(m_EncBitrate));
		opus_encoder_ctl(m_pEncoder, OPUS_SET_PACKET_LOSS_PERC(m_EncLossPerc));
		opus_encoder_ctl(m_pEncoder, OPUS_SET_INBAND_FEC(m_EncFec ? 1 : 0));
		const int ComplexityResult = opus_encoder_ctl(m_pEncoder, OPUS_SET_COMPLEXITY(m_EncComplexity));
		if(ComplexityResult != OPUS_OK)
		{
			log_warn("voice", "OPUS_SET_COMPLEXITY(%d) failed during encoder init with error %d", m_EncComplexity, ComplexityResult);
			opus_encoder_ctl(m_pEncoder, OPUS_GET_COMPLEXITY(&m_EncComplexity));
		}
		opus_encoder_ctl(m_pEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	}

	if(!m_pPeers)
		m_pPeers = std::make_unique<std::array<SVoicePeer, MAX_CLIENTS>>();

	if(!m_OutputDevice)
	{
		const bool OutputMissing = RequestedAudioConfig.m_aOutputDevice[0] != '\0' && pOutputName == nullptr;
		const bool NoOutputDevices = SDL_GetNumAudioDevices(0) <= 0;

		if(OutputMissing)
		{
			if(!m_OutputUnavailable.load())
			{
				char aError[256];
				str_format(aError, sizeof(aError), "Output device not found: '%s'", RequestedAudioConfig.m_aOutputDevice);
				LogDiagnosticErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
			}
			m_OutputReady.store(false);
			m_OutputUnavailable = true;
		}
		else if(NoOutputDevices)
		{
			if(!m_OutputUnavailable.load())
				LogDiagnosticErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), "No output devices available");
			m_OutputReady.store(false);
			m_OutputUnavailable = true;
		}
		else
		{
			if(!m_OutputUnavailable.load())
				log_info("voice", "attempting to open output device '%s'", pOutputName ? pOutputName : "<default>");
			m_OutputDevice = SDL_OpenAudioDevice(pOutputName, 0, &WantOutput, &m_OutputSpec, 0);
			if(!m_OutputDevice)
			{
				if(!m_OutputUnavailable.load())
				{
					char aError[256];
					str_format(aError, sizeof(aError), "Failed to open output device: %s", SDL_GetError());
					LogDiagnosticErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
				}
				m_OutputReady.store(false);
				m_OutputUnavailable = true;
			}
			else
			{
				const int Channels = m_OutputSpec.channels > 0 ? m_OutputSpec.channels : (WantStereo ? 2 : 1);
				log_info("voice", "output device opened '%s' %dch@%d",
					pOutputName ? pOutputName : "<default>",
					Channels,
					m_OutputSpec.freq);
				{
					const CLockScope DeviceRouteGuard(m_DeviceRouteMutex);
					if(pOutputName && pOutputName[0] != '\0')
						str_copy(m_aResolvedOutputDeviceName, pOutputName, sizeof(m_aResolvedOutputDeviceName));
					else
						m_aResolvedOutputDeviceName[0] = '\0';
				}
				m_OutputChannels.store(Channels);
				SDL_PauseAudioDevice(m_OutputDevice, 0);
				ClearPeerFrames();
				m_OutputReady.store(true);
				m_OutputUnavailable = false;
			}
		}
	}
	else
	{
		m_OutputReady.store(true);
		m_OutputUnavailable = false;
	}

	if(!m_CaptureDevice)
	{
#if defined(CONF_PLATFORM_ANDROID)
		if(m_AndroidRecordPermissionKnown && !m_AndroidRecordPermissionGranted)
		{
			m_CaptureReady.store(false);
			m_CaptureUnavailable = true;
		}
		else
#endif
		{
			const bool InputMissing = RequestedAudioConfig.m_aInputDevice[0] != '\0' && pInputName == nullptr;
			const bool NoCaptureDevices = SDL_GetNumAudioDevices(1) <= 0;

			if(InputMissing)
			{
				if(!m_CaptureUnavailable.load())
				{
					char aError[256];
					str_format(aError, sizeof(aError), "Input device not found: '%s'", RequestedAudioConfig.m_aInputDevice);
					LogDiagnosticErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
				}
				m_CaptureReady.store(false);
				m_CaptureUnavailable = true;
			}
			else if(NoCaptureDevices)
			{
				if(!m_CaptureUnavailable.load())
					LogDiagnosticErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), "No capture devices available");
				m_CaptureReady.store(false);
				m_CaptureUnavailable = true;
			}
			else
			{
				if(!m_CaptureUnavailable.load())
					log_info("voice", "attempting to open capture device '%s'", pInputName ? pInputName : "<default>");
				m_CaptureDevice = SDL_OpenAudioDevice(pInputName, 1, &WantCapture, &m_CaptureSpec, 0);
				if(!m_CaptureDevice)
				{
					if(!m_CaptureUnavailable.load())
					{
						char aError[256];
						str_format(aError, sizeof(aError), "Failed to open capture device: %s", SDL_GetError());
						LogDiagnosticErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), aError);
					}
					m_CaptureReady.store(false);
					m_CaptureUnavailable = true;
				}
				else
				{
					log_info("voice", "capture device opened '%s' %dch@%d",
						pInputName ? pInputName : "<default>",
						m_CaptureSpec.channels,
						m_CaptureSpec.freq);
					{
						const CLockScope DeviceRouteGuard(m_DeviceRouteMutex);
						if(pInputName && pInputName[0] != '\0')
							str_copy(m_aResolvedInputDeviceName, pInputName, sizeof(m_aResolvedInputDeviceName));
						else
							m_aResolvedInputDeviceName[0] = '\0';
					}
					SDL_PauseAudioDevice(m_CaptureDevice, 0);
					m_CaptureReady.store(true);
					m_CaptureUnavailable = false;
				}
			}
		}
	}
	else
	{
		m_CaptureReady.store(true);
		m_CaptureUnavailable = false;
	}

	if(m_LogDeviceChange)
	{
		const char *pInputReq = m_aInputDeviceName[0] ? m_aInputDeviceName : "<default>";
		const char *pOutputReq = m_aOutputDeviceName[0] ? m_aOutputDeviceName : "<default>";
		const char *pInputResolved = pInputName ? pInputName : "<default>";
		const char *pOutputResolved = pOutputName ? pOutputName : "<default>";
		log_info("voice", "audio devices set input='%s' resolved='%s' output='%s' resolved='%s' capture=%dch@%d output=%dch@%d",
			pInputReq, pInputResolved, pOutputReq, pOutputResolved,
			m_CaptureSpec.channels, m_CaptureSpec.freq,
			m_OutputSpec.channels, m_OutputSpec.freq);
		m_LogDeviceChange = false;
	}

	if(m_CaptureUnavailable.load() || m_OutputUnavailable.load())
	{
		m_LastAudioRetryAttempt = time_get();
	}
	else
	{
		m_LastAudioRetryAttempt = 0;
		ClearDiagnosticLogMessage(m_aAudioErrorLog);
	}
	ClearDiagnosticLogMessage(m_aEncoderErrorLog);
	return true;
}

void CRClientVoice::PushPeerFrame(int PeerId, const int16_t *pPcm, int Samples, float LeftGain, float RightGain)
{
	if(PeerId < 0 || PeerId >= MAX_CLIENTS)
		return;
	if(Samples <= 0)
		return;

	SVoicePeer &Peer = (*m_pPeers)[PeerId];
	if(Peer.m_FrameCount >= SVoicePeer::MAX_FRAMES)
	{
		Peer.m_FrameHead = (Peer.m_FrameHead + 1) % SVoicePeer::MAX_FRAMES;
		Peer.m_FrameCount--;
		Peer.m_FrameReadPos = 0;
	}

	SVoicePeer::SVoiceFrame &Frame = Peer.m_aFrames[Peer.m_FrameTail];
	const int CopySamples = std::min(Samples, VOICE_FRAME_SAMPLES);
	mem_copy(Frame.m_aPcm, pPcm, CopySamples * sizeof(int16_t));
	Frame.m_Samples = CopySamples;
	Frame.m_LeftGain = LeftGain;
	Frame.m_RightGain = RightGain;
	Peer.m_FrameTail = (Peer.m_FrameTail + 1) % SVoicePeer::MAX_FRAMES;
	Peer.m_FrameCount++;
}

void CRClientVoice::MixAudio(int16_t *pOut, int Samples, int OutputChannels)
{
	if(Samples <= 0 || OutputChannels <= 0)
		return;

	const int Needed = Samples * OutputChannels;
	if(m_MixBuffer.size() < (size_t)Needed)
		m_MixBuffer.resize(Needed);
	std::fill(m_MixBuffer.begin(), m_MixBuffer.begin() + Needed, 0);

	for(auto &Peer : *m_pPeers)
	{
		int FrameIdx = Peer.m_FrameHead;
		int FrameCount = Peer.m_FrameCount;
		int ReadPos = Peer.m_FrameReadPos;
		if(FrameCount <= 0)
			continue;

		for(int i = 0; i < Samples; i++)
		{
			if(FrameCount <= 0)
				break;

			SVoicePeer::SVoiceFrame &Frame = Peer.m_aFrames[FrameIdx];
			const int16_t Pcm = Frame.m_aPcm[ReadPos];
			const float LeftGain = Frame.m_LeftGain;
			const float RightGain = Frame.m_RightGain;

			const int Base = i * OutputChannels;
			if(OutputChannels == 1)
			{
				const float MonoGain = 0.5f * (LeftGain + RightGain);
				m_MixBuffer[Base] += (int32_t)(Pcm * MonoGain);
			}
			else
			{
				m_MixBuffer[Base] += (int32_t)(Pcm * LeftGain);
				m_MixBuffer[Base + 1] += (int32_t)(Pcm * RightGain);
				if(OutputChannels > 2)
				{
					const int32_t Center = (int32_t)(Pcm * 0.5f * (LeftGain + RightGain));
					for(int Channel = 2; Channel < OutputChannels; Channel++)
						m_MixBuffer[Base + Channel] += Center;
				}
			}

			ReadPos++;
			if(ReadPos >= Frame.m_Samples)
			{
				ReadPos = 0;
				FrameIdx = (FrameIdx + 1) % SVoicePeer::MAX_FRAMES;
				FrameCount--;
			}
		}

		Peer.m_FrameHead = FrameIdx;
		Peer.m_FrameCount = FrameCount;
		Peer.m_FrameReadPos = ReadPos;
	}

	for(int i = 0; i < Needed; i++)
	{
		pOut[i] = (int16_t)std::clamp(m_MixBuffer[i], -32768, 32767);
	}
}

void CRClientVoice::ClearPeerFrames()
{
	if(!m_pPeers)
		return;
	if(m_OutputDevice)
		SDL_LockAudioDevice(m_OutputDevice);
	for(auto &Peer : *m_pPeers)
	{
		for(auto &Pkt : Peer.m_aPackets)
		{
			Pkt.m_Valid = false;
			Pkt.m_Size = 0;
			Pkt.m_Seq = 0;
			Pkt.m_LeftGain = 1.0f;
			Pkt.m_RightGain = 1.0f;
		}
		Peer.m_QueuedPackets = 0;
		Peer.m_LastSeq = 0;
		Peer.m_HasSeq = false;
		Peer.m_HasNextSeq = false;
		Peer.m_NextSeq = 0;
		Peer.m_HasMinQueuedSeq = false;
		Peer.m_MinQueuedSeq = 0;
		Peer.m_HasLastRecvSeq = false;
		Peer.m_LastRecvSeq = 0;
		Peer.m_LastRecvTime = 0;
		Peer.m_JitterMs = 0.0f;
		Peer.m_TargetFrames = 3;
		Peer.m_LastGainLeft = 1.0f;
		Peer.m_LastGainRight = 1.0f;
		Peer.m_LossEwma = 0.0f;
		Peer.m_DecoderFailed = false;
		if(Peer.m_pDecoder)
			opus_decoder_ctl(Peer.m_pDecoder, OPUS_RESET_STATE);
		Peer.m_FrameHead = 0;
		Peer.m_FrameTail = 0;
		Peer.m_FrameCount = 0;
		Peer.m_FrameReadPos = 0;
	}
	if(m_OutputDevice)
		SDL_UnlockAudioDevice(m_OutputDevice);
}

void CRClientVoice::ResetPeer(SVoicePeer &Peer)
{
	if(m_OutputDevice)
		SDL_LockAudioDevice(m_OutputDevice);
	Peer.m_FrameHead = 0;
	Peer.m_FrameTail = 0;
	Peer.m_FrameCount = 0;
	Peer.m_FrameReadPos = 0;
	for(auto &Pkt : Peer.m_aPackets)
	{
		Pkt.m_Valid = false;
		Pkt.m_Size = 0;
		Pkt.m_Seq = 0;
		Pkt.m_LeftGain = 1.0f;
		Pkt.m_RightGain = 1.0f;
	}
	Peer.m_QueuedPackets = 0;
	Peer.m_LastSeq = 0;
	Peer.m_HasSeq = false;
	Peer.m_HasNextSeq = false;
	Peer.m_NextSeq = 0;
	Peer.m_HasMinQueuedSeq = false;
	Peer.m_MinQueuedSeq = 0;
	Peer.m_HasLastRecvSeq = false;
	Peer.m_LastRecvSeq = 0;
	Peer.m_LastRecvTime = 0;
	Peer.m_JitterMs = 0.0f;
	Peer.m_TargetFrames = 3;
	Peer.m_LastGainLeft = 1.0f;
	Peer.m_LastGainRight = 1.0f;
	Peer.m_LossEwma = 0.0f;
	Peer.m_DecoderFailed = false;
	if(Peer.m_pDecoder)
		opus_decoder_ctl(Peer.m_pDecoder, OPUS_RESET_STATE);
	if(m_OutputDevice)
		SDL_UnlockAudioDevice(m_OutputDevice);
}

const char *CRClientVoice::FindDeviceName(bool Capture, const char *pDesired) const
{
	if(!pDesired || pDesired[0] == '\0')
		return nullptr;

	const int Num = SDL_GetNumAudioDevices(Capture ? 1 : 0);
	for(int i = 0; i < Num; i++)
	{
		const char *pName = SDL_GetAudioDeviceName(i, Capture ? 1 : 0);
		if(pName && str_comp_nocase(pName, pDesired) == 0)
			return pName;
	}
	return nullptr;
}

void CRClientVoice::ListDevices()
{
	if(!m_pConsole)
		return;

	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "voice", "Input devices:");
	const int NumInputs = SDL_GetNumAudioDevices(1);
	for(int i = 0; i < NumInputs; i++)
	{
		const char *pName = SDL_GetAudioDeviceName(i, 1);
		if(pName)
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "voice", pName);
	}

	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "voice", "Output devices:");
	const int NumOutputs = SDL_GetNumAudioDevices(0);
	for(int i = 0; i < NumOutputs; i++)
	{
		const char *pName = SDL_GetAudioDeviceName(i, 0);
		if(pName)
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "voice", pName);
	}
}

void CRClientVoice::ExportOverlayState(CVoiceOverlayState &Overlay) const NO_THREAD_SAFETY_ANALYSIS
{
	std::array<std::array<char, MAX_NAME_LENGTH>, MAX_CLIENTS> aaClientNames{};
	std::array<int, 2> aLocalClientIds{};
	aLocalClientIds.fill(-1);
	int PreferredLocalId = -1;
	bool Online = false;
	{
		const CLockScope Guard(m_SnapshotMutex);
		Online = m_OnlineSnap;
		if(!Online)
			return;

		PreferredLocalId = m_LocalClientIdSnap;
		aLocalClientIds = m_aLocalClientIdsSnap;
		aaClientNames = m_aClientNameSnap;
	}

	const int64_t Now = time_get();
	const bool LocalTxActive = m_TxWasActive.load();
	bool LocalEntryAdded = false;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		int64_t LastSeen = m_aLastHeard[ClientId].load();
		if(aaClientNames[ClientId][0] == '\0')
			continue;

		bool IsLocalSpeaker = false;
		for(const int LocalId : aLocalClientIds)
		{
			if(LocalId == ClientId)
			{
				IsLocalSpeaker = true;
				break;
			}
		}

		if(IsLocalSpeaker)
		{
			if(!LocalTxActive)
				continue;
			LastSeen = Now;
			if(PreferredLocalId >= 0 && ClientId != PreferredLocalId)
				continue;
			if(PreferredLocalId < 0 && LocalEntryAdded)
				continue;
			LocalEntryAdded = true;
		}
		else if(LastSeen <= 0)
		{
			continue;
		}

		const float Level = IsLocalSpeaker ?
					    (LocalTxActive ? m_MicLevel.load() : 0.0f) :
					    m_aSpeakerLevel[ClientId].load();
		Overlay.NoteSpeaker(ClientId, aaClientNames[ClientId].data(), IsLocalSpeaker, LastSeen, Level);
	}
}

void CRClientVoice::ExportUiStatus(VoiceUtils::SVoiceUiStatus &Out) const NO_THREAD_SAFETY_ANALYSIS
{
	Out = {};
	Out.m_Enabled = g_Config.m_QmVoiceEnable != 0;

	SRClientVoiceConfigSnapshot Config;
	GetConfigSnapshot(Config);
	Out.m_NeedNetwork = std::clamp(Config.m_QmVoiceTestMode, 0, 2) != 1;
	Out.m_AudioRefreshPending = m_AudioRefreshRequested.load();
	Out.m_ServerAddrValid = m_ServerAddrValid.load();
	Out.m_HaveSocket = m_TransportReady.load();
	Out.m_Connecting = m_TransportConnecting.load();
	Out.m_CaptureReady = m_CaptureReady.load();
	Out.m_CaptureUnavailable = m_CaptureUnavailable.load();
	Out.m_OutputReady = m_OutputReady.load();
	Out.m_OutputUnavailable = m_OutputUnavailable.load();
	Out.m_EncoderReady = m_EncoderReady.load();
	Out.m_MicMuted = Config.m_QmVoiceMicMute != 0;
	Out.m_TxActive = m_TxWasActive.load();
	Out.m_PingMs = m_PingMs.load();
	Out.m_MicLevel = m_MicLevel.load();
	VoiceUtils::SVoiceAudioDeviceConfig RequestedAudioConfig;
	GetRequestedAudioDeviceConfig(RequestedAudioConfig);
	str_copy(Out.m_aRequestedInputDevice, RequestedAudioConfig.m_aInputDevice, sizeof(Out.m_aRequestedInputDevice));
	str_copy(Out.m_aRequestedOutputDevice, RequestedAudioConfig.m_aOutputDevice, sizeof(Out.m_aRequestedOutputDevice));
	{
		const CLockScope DeviceRouteGuard(m_DeviceRouteMutex);
		str_copy(Out.m_aResolvedInputDevice, m_aResolvedInputDeviceName, sizeof(Out.m_aResolvedInputDevice));
		str_copy(Out.m_aResolvedOutputDevice, m_aResolvedOutputDeviceName, sizeof(Out.m_aResolvedOutputDevice));
	}

	const int64_t Now = time_get();
	const int64_t RecentWindow = time_freq() * 3;
	const int64_t LastTxPacketTime = m_LastTxPacketTime.load();
	const int64_t LastMediaRxPacketTime = m_LastMediaRxPacketTime.load();
	Out.m_TxAgeMs = LastTxPacketTime > 0 ? (int)std::clamp((Now - LastTxPacketTime) * 1000 / time_freq(), (int64_t)0, (int64_t)999999) : -1;
	Out.m_RxAgeMs = LastMediaRxPacketTime > 0 ? (int)std::clamp((Now - LastMediaRxPacketTime) * 1000 / time_freq(), (int64_t)0, (int64_t)999999) : -1;
	Out.m_HaveRecentRx = LastMediaRxPacketTime > 0 && Now - LastMediaRxPacketTime <= RecentWindow;

	int aLocalClientIds[2] = {-1, -1};
	{
		const CLockScope Guard(m_SnapshotMutex);
		Out.m_Online = m_OnlineSnap;
		aLocalClientIds[0] = m_aLocalClientIdsSnap[0];
		aLocalClientIds[1] = m_aLocalClientIdsSnap[1];
	}

	int ActivePeerCount = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(ClientId == aLocalClientIds[0] || ClientId == aLocalClientIds[1])
			continue;

		const int64_t Seen = m_aLastHeard[ClientId].load();
		if(Seen > 0 && Now - Seen <= RecentWindow)
			ActivePeerCount++;
	}
	Out.m_ActivePeerCount = ActivePeerCount;
	Out.m_HaveRecentPeers = ActivePeerCount > 0;
	CopyDiagnosticLogMessage(m_aAudioErrorLog, Out.m_aAudioError, sizeof(Out.m_aAudioError));
	CopyDiagnosticLogMessage(m_aServerAddrErrorLog, Out.m_aNetworkError, sizeof(Out.m_aNetworkError));
	if(Out.m_aNetworkError[0] == '\0')
		CopyDiagnosticLogMessage(m_aSocketErrorLog, Out.m_aNetworkError, sizeof(Out.m_aNetworkError));
	CopyDiagnosticLogMessage(m_aEncoderErrorLog, Out.m_aCodecError, sizeof(Out.m_aCodecError));
	if(Out.m_aCodecError[0] == '\0')
		CopyDiagnosticLogMessage(m_aDecoderErrorLog, Out.m_aCodecError, sizeof(Out.m_aCodecError));
}

void CRClientVoice::UpdateMicLevel(float Peak)
{
	const float Prev = m_MicLevel.load();
	if(Peak < 0.0f)
	{
		m_MicLevel.store(Prev * 0.97f);
		return;
	}
	Peak = std::clamp(Peak, 0.0f, 1.0f);
	const float Next = Peak >= Prev ? Peak : (Prev * 0.9f);
	m_MicLevel.store(Next);
}

void CRClientVoice::Shutdown()
{
	if(m_ShutdownDone)
		return;
	m_ShutdownDone = true;

	StopWorker();

	if(m_CaptureDevice)
	{
		SDL_CloseAudioDevice(m_CaptureDevice);
		m_CaptureDevice = 0;
	}
	m_CaptureReady.store(false);
	if(m_OutputDevice)
	{
		SDL_CloseAudioDevice(m_OutputDevice);
		m_OutputDevice = 0;
	}
	m_OutputReady.store(false);
	m_OutputChannels.store(0);
	{
		const CLockScope DeviceRouteGuard(m_DeviceRouteMutex);
		m_aResolvedInputDeviceName[0] = '\0';
		m_aResolvedOutputDeviceName[0] = '\0';
	}
	m_CaptureUnavailable = false;
	m_OutputUnavailable = false;
#if defined(CONF_PLATFORM_ANDROID)
	m_AndroidRecordPermissionKnown = false;
	m_AndroidRecordPermissionGranted = false;
#endif
	m_LastAudioRetryAttempt = 0;
	m_AudioRefreshRequested.store(true);
	if(m_pEncoder)
	{
		opus_encoder_destroy(m_pEncoder);
		m_pEncoder = nullptr;
	}
	m_EncoderReady.store(false);
	ResetRuntimeState(VoiceUtils::VOICE_RUNTIME_RESET_CONNECTION | VoiceUtils::VOICE_RUNTIME_RESET_PEERS, 0);
	for(auto &Peer : *m_pPeers)
	{
		if(Peer.m_pDecoder)
		{
			opus_decoder_destroy(Peer.m_pDecoder);
			Peer.m_pDecoder = nullptr;
		}
	}
	m_pPeers.reset();
	if(m_pVoiceTransport)
	{
		m_pVoiceTransport->Disconnect();
		m_pVoiceTransport.reset();
	}
	m_TransportReady.store(false);
	m_TransportConnecting.store(false);
	m_ServerAddrValid.store(false);
	m_aServerAddrStr[0] = '\0';
	m_HpfPrevIn = 0.0f;
	m_HpfPrevOut = 0.0f;
	m_CompEnv = 0.0f;
	// Reset DSP state to factory defaults. Also reset in ResetTransmitState()
	// when the transmit chain needs to restart without full shutdown.
	m_AgcGain = 1.0f;
	m_NsNoiseFloor = 0.0f;
	m_NsGain = 1.0f;
	m_AudioPausedForInactive = false;
	m_NoiseSuppressFallbackLogged = false;
#if defined(CONF_RNNOISE)
	if(m_pNoiseSuppress)
	{
		rnnoise_destroy(m_pNoiseSuppress);
		m_pNoiseSuppress = nullptr;
	}
#endif
	m_aAudioBackend[0] = '\0';
	m_aAudioBackendMismatchReq[0] = '\0';
	m_aAudioBackendMismatchCur[0] = '\0';
	m_aAudioInitLoggedBackend[0] = '\0';
	ClearDiagnosticLogMessage(m_aSocketErrorLog);
	ClearDiagnosticLogMessage(m_aAudioErrorLog);
	ClearDiagnosticLogMessage(m_aEncoderErrorLog);
	ClearDiagnosticLogMessage(m_aServerAddrErrorLog);
	ClearDiagnosticLogMessage(m_aDecoderErrorLog);
	m_AudioSubsystemInitializedByVoice = false;
	m_MicLevel.store(0.0f);
	m_LastConfigSnapshotUpdate = 0;
	m_LastClientSnapshotUpdate = 0;
}

void CRClientVoice::UpdateServerAddrConfig() NO_THREAD_SAFETY_ANALYSIS
{
	const CLockScope Guard(m_ServerAddrMutex);
	str_copy(m_aServerAddrStr, VoiceUtils::EffectiveVoiceWebSocketUrl(g_Config.m_QmVoiceServer), sizeof(m_aServerAddrStr));
}

bool CRClientVoice::UpdateContext()
{
	const uint32_t Old = m_ContextHash.load();
	if(!m_pClient || m_pClient->State() != IClient::STATE_ONLINE)
	{
		m_ContextHash.store(0);
		return Old != 0;
	}
	char aAddr[NETADDR_MAXSTRSIZE];
	const NETADDR *pServerAddr = m_pClient->ServerAddress();
	if(!pServerAddr)
	{
		m_ContextHash.store(0);
		return Old != 0;
	}
	net_addr_str(pServerAddr, aAddr, sizeof(aAddr), true);
	const uint32_t NewHash = str_quickhash(aAddr);
	m_ContextHash.store(NewHash);
	return NewHash != Old;
}

void CRClientVoice::UpdateClientSnapshot(bool Force) NO_THREAD_SAFETY_ANALYSIS
{
	const bool Online = m_pClient && m_pGameClient && m_pClient->State() == IClient::STATE_ONLINE;
	if(!Online)
	{
		const CLockScope Guard(m_SnapshotMutex);
		if(!m_OnlineSnap && m_LocalClientIdSnap == -1 && !m_SpecActiveSnap)
			return;

		m_OnlineSnap = false;
		m_LocalClientIdSnap = -1;
		m_aLocalClientIdsSnap.fill(-1);
		m_SpecActiveSnap = false;
		m_SpecPosSnap = vec2(0.0f, 0.0f);
		m_LastClientSnapshotUpdate = 0;
		return;
	}

	const int64_t Now = time_get();
	const int64_t RefreshInterval = (int64_t)time_freq() * VOICE_CLIENT_SNAPSHOT_INTERVAL_MS / 1000;
	if(!Force && m_LastClientSnapshotUpdate != 0 && Now - m_LastClientSnapshotUpdate < RefreshInterval)
		return;

	m_LastClientSnapshotUpdate = Now;
	const CLockScope Guard(m_SnapshotMutex);
	m_OnlineSnap = true;
	m_LocalClientIdSnap = m_pGameClient->m_Snap.m_LocalClientId;
	m_aLocalClientIdsSnap.fill(-1);
	for(size_t Dummy = 0; Dummy < m_aLocalClientIdsSnap.size(); ++Dummy)
		m_aLocalClientIdsSnap[Dummy] = m_pGameClient->m_aLocalIds[Dummy];
	m_SpecActiveSnap = m_pGameClient->m_Snap.m_SpecInfo.m_Active;
	if(m_SpecActiveSnap)
		m_SpecPosSnap = m_pGameClient->m_Camera.m_Center;
	if(m_LocalClientIdSnap < 0 || m_LocalClientIdSnap >= MAX_CLIENTS)
	{
		m_OnlineSnap = false;
		m_LocalClientIdSnap = -1;
		m_aLocalClientIdsSnap.fill(-1);
		m_SpecActiveSnap = false;
		m_SpecPosSnap = vec2(0.0f, 0.0f);
		m_LastClientSnapshotUpdate = 0;
		return;
	}
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aClientPosSnap[i] = m_pGameClient->m_aClients[i].m_RenderPos;
		str_copy(m_aClientNameSnap[i].data(), m_pGameClient->m_aClients[i].m_aName, MAX_NAME_LENGTH);
		m_aClientOtherTeamSnap[i] = m_pGameClient->m_Teams.Team(i) != m_pGameClient->m_Teams.Team(m_LocalClientIdSnap);
		m_aClientActiveSnap[i] = m_pGameClient->m_Snap.m_aCharacters[i].m_Active;
		m_aClientSpecSnap[i] = m_pGameClient->m_aClients[i].m_Spec;
	}
}

void CRClientVoice::ResetTransmitState(bool ClearQueuedCapture) NO_THREAD_SAFETY_ANALYSIS
{
	UpdateMicLevel(0.0f);
	// Reset transmit-side DSP state. Mirrored in Shutdown() for full cleanup.
	m_AgcGain = 1.0f;
	m_VadActive = false;
	m_VadReleaseDeadline = 0;
	m_PttReleaseDeadline.store(0);
	m_TxWasActive = false;
	if(ClearQueuedCapture && m_CaptureDevice)
		SDL_ClearQueuedAudio(m_CaptureDevice);
}

bool CRClientVoice::HasConnectionRuntimeState() const
{
	return m_PingMs.load() >= 0 ||
	       m_LastTxPacketTime.load() > 0 ||
	       m_LastRxPacketTime.load() > 0 ||
	       m_LastMediaRxPacketTime.load() > 0 ||
	       m_LastPingSentTime != 0 ||
	       m_LastPingSeq != 0 ||
	       m_LastTokenHashSent != 0 ||
	       m_TxWasActive.load();
}

bool CRClientVoice::HasPeerRuntimeState(uint32_t RoomTokenHash) const
{
	if(m_RoomMemberTokenHash.load() != RoomTokenHash)
		return true;
	for(const auto &RoomMemberSeen : m_aRoomMemberSeen)
	{
		if(RoomMemberSeen.load() > 0)
			return true;
	}
	return false;
}

void CRClientVoice::ResetRuntimeState(uint32_t Flags, uint32_t RoomTokenHash) NO_THREAD_SAFETY_ANALYSIS
{
	if((Flags & VoiceUtils::VOICE_RUNTIME_RESET_CONNECTION) != 0)
	{
		ResetTransmitState(true);
		m_PingMs.store(-1);
		m_LastTxPacketTime.store(0);
		m_LastRxPacketTime.store(0);
		m_LastMediaRxPacketTime.store(0);
		m_LastPingSentTime = 0;
		m_LastPingSeq = 0;
		m_LastKeepalive = 0;
		m_LastTokenHashSent = 0;
	}

	if((Flags & VoiceUtils::VOICE_RUNTIME_RESET_PEERS) != 0)
	{
		ClearPeerFrames();
		for(auto &RoomMemberSeen : m_aRoomMemberSeen)
			RoomMemberSeen.store(0);
		for(auto &SpeakerLevel : m_aSpeakerLevel)
			SpeakerLevel.store(0.0f);
		m_RoomMemberTokenHash.store(RoomTokenHash);
	}
}

void CRClientVoice::ProcessCapture() NO_THREAD_SAFETY_ANALYSIS
{
	SRClientVoiceConfigSnapshot Config;
	GetConfigSnapshot(Config);
	const bool HaveCaptureDevice = m_CaptureDevice != 0;
	const int TestMode = std::clamp(Config.m_QmVoiceTestMode, 0, 2);
	const bool TestLocal = TestMode == 1;
	const bool NeedNetwork = !TestLocal;
	const bool ShowMicLevel = true;
	const bool MicMuted = Config.m_QmVoiceMicMute != 0;
	const float TestGain = std::clamp(Config.m_QmVoiceVolume / 100.0f, 0.0f, 4.0f);

	int LocalClientId = -1;
	std::array<int, 2> aLocalClientIds = {};
	aLocalClientIds.fill(-1);
	vec2 LocalPos = vec2(0.0f, 0.0f);
	bool Online = false;
	{
		const CLockScope Guard(m_SnapshotMutex);
		Online = m_OnlineSnap;
		LocalClientId = m_LocalClientIdSnap;
		aLocalClientIds = m_aLocalClientIdsSnap;
		if(LocalClientId >= 0 && LocalClientId < MAX_CLIENTS)
			LocalPos = m_aClientPosSnap[LocalClientId];
	}
	VoiceUtils::SVoiceTransmitPreconditions Preconditions;
	Preconditions.m_NeedNetwork = NeedNetwork;
	Preconditions.m_ServerAddrValid = m_ServerAddrValid.load();
	Preconditions.m_HaveSocket = m_TransportReady.load();
	Preconditions.m_Online = Online && LocalClientId >= 0 && LocalClientId < MAX_CLIENTS;
	Preconditions.m_HaveCaptureDevice = HaveCaptureDevice;
	Preconditions.m_HaveEncoder = m_pEncoder != nullptr;
	Preconditions.m_MicMuted = MicMuted;

	const uint32_t NetworkBlockers = VoiceUtils::VoiceTransmitBlockers(Preconditions) &
					 (VoiceUtils::VOICE_TX_BLOCK_SERVER_ADDR | VoiceUtils::VOICE_TX_BLOCK_SOCKET | VoiceUtils::VOICE_TX_BLOCK_ONLINE);
	if(NetworkBlockers != 0)
	{
		ResetTransmitState(true);
		return;
	}

	if((!Online || LocalClientId < 0 || LocalClientId >= MAX_CLIENTS) && !TestLocal)
	{
		ResetTransmitState(false);
		return;
	}
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		LocalClientId = 0;

	const auto MarkLocalVoiceActive = [&](int64_t Timestamp) {
		for(const int Id : aLocalClientIds)
		{
			if(Id >= 0 && Id < MAX_CLIENTS)
			{
				m_aLastHeard[Id].store(Timestamp);
				m_aRoomMemberSeen[Id].store(Timestamp);
			}
		}
	};

	const auto MarkLocalRoomMemberSeen = [&](int64_t Timestamp) {
		for(const int Id : aLocalClientIds)
		{
			if(Id >= 0 && Id < MAX_CLIENTS)
				m_aRoomMemberSeen[Id].store(Timestamp);
		}
	};

	const int64_t Now = time_get();
	const bool UseVad = Config.m_QmVoiceVadEnable != 0;
	if(!UseVad)
	{
		m_VadActive = false;
		m_VadReleaseDeadline = 0;
	}
	int64_t ReleaseDeadline = 0;
	bool PttHeld = false;
	if(!UseVad)
	{
		ReleaseDeadline = m_PttReleaseDeadline.load();
		PttHeld = m_PttActive.load() || (ReleaseDeadline != 0 && Now < ReleaseDeadline);
		if(TestLocal)
			PttHeld = true;
	}
	else if(m_VadActive && m_VadReleaseDeadline != 0 && Now >= m_VadReleaseDeadline)
	{
		m_VadActive = false;
		m_VadReleaseDeadline = 0;
	}
	const bool TokenChanged = Config.m_QmVoiceTokenHash != m_LastTokenHashSent;
	const bool NeedKeepalive = m_LastKeepalive == 0 || Now - m_LastKeepalive > time_freq() * 2;
	const bool TxActiveSnapshot = UseVad ? m_VadActive.load() : PttHeld;
	const uint8_t ProtocolVersion = VoiceProtocolVersion(Config);
	uint8_t TxFlags = UseVad ? VOICE_FLAG_VAD : 0;
	if(TestMode == 2)
		TxFlags |= VOICE_FLAG_LOOPBACK;

	if(NeedNetwork && (TokenChanged || (!TxActiveSnapshot && NeedKeepalive)))
	{
		uint8_t aPacket[VOICE_MAX_PACKET];
		const uint16_t PingSequence = m_Sequence++;
		VoiceUtils::SVoicePacketHeader Header;
		Header.m_Version = ProtocolVersion;
		Header.m_Type = VOICE_TYPE_PING;
		Header.m_ContextHash = m_ContextHash.load();
		Header.m_TokenHash = Config.m_QmVoiceTokenHash;
		Header.m_Flags = TxFlags;
		Header.m_SenderId = (uint16_t)LocalClientId;
		Header.m_Sequence = PingSequence;
		if(VoiceUtils::WriteVoicePacketHeader(aPacket, sizeof(aPacket), Header) &&
			SendVoicePacket(aPacket, VOICE_PACKET_HEADER_SIZE))
		{
			m_LastTxPacketTime.store(Now);
			MarkLocalRoomMemberSeen(Now);
			m_LastPingSentTime = Now;
			m_LastPingSeq = PingSequence;
			m_LastKeepalive = Now;
			m_LastTokenHashSent = Config.m_QmVoiceTokenHash;
		}
	}

	if(!HaveCaptureDevice)
	{
		ResetTransmitState(false);
		return;
	}

	if(MicMuted)
	{
		ResetTransmitState(true);
		return;
	}

	if(!UseVad && !PttHeld)
	{
		if(ShowMicLevel)
		{
			bool UpdatedMicLevel = false;
			int FramesProcessed = 0;
			while(SDL_GetQueuedAudioSize(m_CaptureDevice) >= VOICE_FRAME_BYTES && FramesProcessed < VOICE_MAX_CAPTURE_FRAMES_PER_UPDATE)
			{
				int16_t aPcm[VOICE_FRAME_SAMPLES];
				SDL_DequeueAudio(m_CaptureDevice, aPcm, VOICE_FRAME_BYTES);
				FramesProcessed++;
				VoiceUtils::ProcessVoiceCaptureFrame(Config, aPcm, VOICE_FRAME_SAMPLES, m_AgcGain, m_NsNoiseFloor, m_NsGain, m_pNoiseSuppress, m_NoiseSuppressFallbackLogged, m_HpfPrevIn, m_HpfPrevOut, m_CompEnv);
				const float Peak = VoiceUtils::VoiceFramePeak(aPcm, VOICE_FRAME_SAMPLES);
				UpdateMicLevel(Peak);
				UpdatedMicLevel = true;
			}
			if(SDL_GetQueuedAudioSize(m_CaptureDevice) >= VOICE_FRAME_BYTES)
				SDL_ClearQueuedAudio(m_CaptureDevice);
			if(!UpdatedMicLevel)
				UpdateMicLevel(-1.0f);
		}
		else
		{
			UpdateMicLevel(0.0f);
		}
		if(ReleaseDeadline != 0 && Now >= ReleaseDeadline)
			m_PttReleaseDeadline.store(0);
		m_TxWasActive = false;
		SDL_ClearQueuedAudio(m_CaptureDevice);
		return;
	}

	const int ClientId = LocalClientId;
	const vec2 Pos = LocalPos;
	if(!m_pEncoder)
	{
		ResetTransmitState(false);
		return;
	}
	const float VadThreshold = std::clamp(Config.m_QmVoiceVadThreshold / 100.0f, 0.0f, 1.0f);
	const int VadReleaseMs = std::clamp(Config.m_QmVoiceVadReleaseDelayMs, 0, 1000);
	const int64_t VadReleaseTicks = (int64_t)time_freq() * VadReleaseMs / 1000;
	if(!TxActiveSnapshot)
		m_TxWasActive = false;

	uint8_t aPacket[VOICE_MAX_PACKET];
	uint8_t aPayload[VOICE_MAX_PAYLOAD];

	bool UpdatedMicLevel = false;
	int FramesProcessed = 0;
	while(SDL_GetQueuedAudioSize(m_CaptureDevice) >= VOICE_FRAME_BYTES && FramesProcessed < VOICE_MAX_CAPTURE_FRAMES_PER_UPDATE)
	{
		int16_t aPcm[VOICE_FRAME_SAMPLES];
		SDL_DequeueAudio(m_CaptureDevice, aPcm, VOICE_FRAME_BYTES);
		FramesProcessed++;
		VoiceUtils::ProcessVoiceCaptureFrame(Config, aPcm, VOICE_FRAME_SAMPLES, m_AgcGain, m_NsNoiseFloor, m_NsGain, m_pNoiseSuppress, m_NoiseSuppressFallbackLogged, m_HpfPrevIn, m_HpfPrevOut, m_CompEnv);

		const float Peak = VoiceUtils::VoiceFramePeak(aPcm, VOICE_FRAME_SAMPLES);
		if(ShowMicLevel)
		{
			UpdateMicLevel(Peak);
			UpdatedMicLevel = true;
		}

		if(UseVad)
		{
			const bool Trigger = VadThreshold <= 0.0f || Peak >= VadThreshold;
			const int64_t FrameNow = time_get();
			if(Trigger)
			{
				m_VadActive = true;
				if(VadReleaseTicks > 0)
					m_VadReleaseDeadline = FrameNow + VadReleaseTicks;
				else
					m_VadReleaseDeadline = 0;
			}
			else if(m_VadActive)
			{
				if(m_VadReleaseDeadline == 0 || FrameNow >= m_VadReleaseDeadline)
				{
					m_VadActive = false;
					m_VadReleaseDeadline = 0;
				}
			}
		}

		const bool TxActive = UseVad ? m_VadActive.load() : PttHeld;
		if(!TxActive)
		{
			m_TxWasActive = false;
			continue;
		}
		if(!m_TxWasActive)
		{
			if(m_pEncoder)
				opus_encoder_ctl(m_pEncoder, OPUS_RESET_STATE);
			m_HpfPrevIn = 0.0f;
			m_HpfPrevOut = 0.0f;
			m_CompEnv = 0.0f;
			m_TxWasActive = true;
		}

		if(TestLocal)
		{
			if(m_OutputDevice && TestGain > 0.0f)
			{
				SDL_LockAudioDevice(m_OutputDevice);
				PushPeerFrame(LocalClientId, aPcm, VOICE_FRAME_SAMPLES, TestGain, TestGain);
				SDL_UnlockAudioDevice(m_OutputDevice);
			}
			MarkLocalVoiceActive(Now);
			continue;
		}

		const int EncSize = opus_encode(m_pEncoder, aPcm, VOICE_FRAME_SAMPLES, aPayload, (int)sizeof(aPayload));
		if(EncSize <= 0)
			continue;
		if(EncSize > VOICE_MAX_PAYLOAD)
			continue;

		VoiceUtils::SVoicePacketHeader Header;
		Header.m_Version = ProtocolVersion;
		Header.m_Type = VOICE_TYPE_AUDIO;
		Header.m_PayloadSize = (uint16_t)EncSize;
		Header.m_ContextHash = m_ContextHash.load();
		Header.m_TokenHash = Config.m_QmVoiceTokenHash;
		Header.m_Flags = TxFlags;
		Header.m_SenderId = (uint16_t)ClientId;
		Header.m_Sequence = m_Sequence++;
		Header.m_PosX = Pos.x;
		Header.m_PosY = Pos.y;
		if(!VoiceUtils::WriteVoicePacketHeader(aPacket, sizeof(aPacket), Header))
			continue;

		size_t Offset = VOICE_PACKET_HEADER_SIZE;
		mem_copy(aPacket + Offset, aPayload, EncSize);
		Offset += EncSize;

		if(!SendVoicePacket(aPacket, Offset))
		{
			ResetTransmitState(true);
			break;
		}
		m_LastTxPacketTime.store(Now);
		MarkLocalVoiceActive(Now);
		if(Config.m_QmVoiceDebug)
		{
			m_TxPackets++;
			if(Now - m_TxLastLog > time_freq())
			{
				log_info("voice", "tx packets=%d ctx=0x%08x", m_TxPackets, m_ContextHash.load());
				m_TxLastLog = Now;
				m_TxPackets = 0;
			}
		}
	}
	if(SDL_GetQueuedAudioSize(m_CaptureDevice) >= VOICE_FRAME_BYTES)
		SDL_ClearQueuedAudio(m_CaptureDevice);

	if(ShowMicLevel)
	{
		if(!UpdatedMicLevel)
			UpdateMicLevel(-1.0f);
	}
	else
	{
		UpdateMicLevel(0.0f);
	}
}

void CRClientVoice::ProcessIncoming() NO_THREAD_SAFETY_ANALYSIS
{
	if(!m_pVoiceTransport || !m_pVoiceTransport->Connected())
		return;

	SRClientVoiceConfigSnapshot Config;
	GetConfigSnapshot(Config);
	const int TestMode = std::clamp(Config.m_QmVoiceTestMode, 0, 2);
	const bool TestServer = TestMode == 2;
	const uint8_t ProtocolVersion = VoiceProtocolVersion(Config);

	while(true)
	{
		SQmWebSocketMessage WebSocketMessage;
		if(!m_pVoiceTransport->PollPacket(WebSocketMessage))
			break;
		auto *pData = reinterpret_cast<unsigned char *>(WebSocketMessage.m_Data.data());
		const int Bytes = static_cast<int>(WebSocketMessage.m_Data.size());

		if(Bytes < VOICE_PACKET_HEADER_SIZE)
		{
			m_RxDropHeader++;
			continue;
		}

		VoiceUtils::SVoicePacketHeader Header;
		if(!VoiceUtils::ReadVoicePacketHeader(pData, Bytes, Header))
		{
			m_RxDropHeader++;
			continue;
		}

		const uint8_t Version = Header.m_Version;
		const uint8_t Type = Header.m_Type;
		if(Version != ProtocolVersion)
		{
			m_RxDropVersion++;
			continue;
		}
		if(Type != VOICE_TYPE_AUDIO && Type != VOICE_TYPE_PING && Type != VOICE_TYPE_PONG)
		{
			m_RxDropType++;
			continue;
		}
		const uint16_t PayloadSize = Header.m_PayloadSize;
		const uint32_t TokenHash = Header.m_TokenHash;
		const uint8_t Flags = Header.m_Flags;
		const uint16_t SenderId = Header.m_SenderId;
		const uint16_t Sequence = Header.m_Sequence;
		const float PosX = VoiceUtils::SanitizeFloat(Header.m_PosX);
		const float PosY = VoiceUtils::SanitizeFloat(Header.m_PosY);

		VoiceUtils::SVoiceIncomingPacketContext PacketContext;
		PacketContext.m_ProtocolVersion = ProtocolVersion;
		PacketContext.m_LocalContextHash = m_ContextHash.load();
		PacketContext.m_LocalTokenHash = Config.m_QmVoiceTokenHash;
		PacketContext.m_MaxClients = MAX_CLIENTS;
		const auto PacketDecision = VoiceUtils::ClassifyVoiceIncomingPacket(Header, (size_t)Bytes, PacketContext);
		switch(PacketDecision)
		{
		case VoiceUtils::EVoiceIncomingPacketDecision::DROP_HEADER:
			m_RxDropHeader++;
			continue;
		case VoiceUtils::EVoiceIncomingPacketDecision::DROP_VERSION:
			m_RxDropVersion++;
			continue;
		case VoiceUtils::EVoiceIncomingPacketDecision::DROP_TYPE:
			m_RxDropType++;
			continue;
		case VoiceUtils::EVoiceIncomingPacketDecision::DROP_FLAGS:
			m_RxDropHeader++;
			continue;
		case VoiceUtils::EVoiceIncomingPacketDecision::DROP_CONTEXT:
			m_RxDropContext++;
			continue;
		case VoiceUtils::EVoiceIncomingPacketDecision::DROP_GROUP:
		case VoiceUtils::EVoiceIncomingPacketDecision::DROP_KEEPALIVE_TOKEN:
			m_RxDropGroup++;
			continue;
		case VoiceUtils::EVoiceIncomingPacketDecision::DROP_SENDER:
			m_RxDropSender++;
			continue;
		case VoiceUtils::EVoiceIncomingPacketDecision::DROP_PAYLOAD:
			m_RxDropPayload++;
			continue;
		case VoiceUtils::EVoiceIncomingPacketDecision::HANDLE_AUDIO:
		case VoiceUtils::EVoiceIncomingPacketDecision::HANDLE_PING:
		case VoiceUtils::EVoiceIncomingPacketDecision::HANDLE_PONG:
			break;
		}

		const uint32_t LocalToken = Config.m_QmVoiceTokenHash;

		const int64_t PacketNow = time_get();
		if(SenderId >= MAX_CLIENTS)
		{
			m_RxDropSender++;
			continue;
		}
		m_aRoomMemberSeen[SenderId].store(PacketNow);

		if(PacketDecision == VoiceUtils::EVoiceIncomingPacketDecision::HANDLE_PING || PacketDecision == VoiceUtils::EVoiceIncomingPacketDecision::HANDLE_PONG)
		{
			m_LastRxPacketTime.store(PacketNow);
			if(VoiceUtils::VoiceShouldUpdatePingRtt(Sequence, m_LastPingSeq, m_LastPingSentTime))
			{
				const int RttMs = (int)std::clamp((PacketNow - m_LastPingSentTime) * 1000 / time_freq(), (int64_t)0, (int64_t)9999);
				m_PingMs.store(RttMs);
			}
			continue;
		}

		if(!m_OutputDevice)
			continue;

		int LocalId = -1;
		vec2 LocalPos = vec2(0.0f, 0.0f);
		bool SpecActive = false;
		vec2 SpecPos = vec2(0.0f, 0.0f);
		char aSenderName[MAX_NAME_LENGTH];
		bool SenderOtherTeam = false;
		bool SenderActive = false;
		bool SenderSpec = false;
		{
			const CLockScope Guard(m_SnapshotMutex);
			if(!m_OnlineSnap)
				continue;
			LocalId = m_LocalClientIdSnap;
			if(LocalId < 0 || LocalId >= MAX_CLIENTS)
				continue;
			LocalPos = m_aClientPosSnap[LocalId];
			SpecActive = m_SpecActiveSnap;
			SpecPos = m_SpecPosSnap;
			str_copy(aSenderName, m_aClientNameSnap[SenderId].data(), sizeof(aSenderName));
			SenderOtherTeam = m_aClientOtherTeamSnap[SenderId] != 0;
			SenderActive = m_aClientActiveSnap[SenderId] != 0;
			SenderSpec = m_aClientSpecSnap[SenderId] != 0;
		}

		LocalPos = VoiceUtils::VoiceResolveListenerPosition(LocalPos, SpecActive, SpecPos, Config.m_QmVoiceHearOnSpecPos != 0);

		const bool IsSelf = SenderId == LocalId;
		const bool IgnoreDistance = VoiceUtils::VoiceShouldIgnoreDistance(Config.m_QmVoiceIgnoreDistance != 0, Config.m_QmVoiceGroupGlobal != 0, LocalToken, TokenHash);
		const char *pSenderName = aSenderName;
		VoiceUtils::SVoiceReceiveAudibilityContext AudibilityContext;
		AudibilityContext.m_IsSelf = IsSelf;
		AudibilityContext.m_TestServer = TestServer;
		AudibilityContext.m_IgnoreDistance = IgnoreDistance;
		AudibilityContext.m_VisibilityMode = Config.m_QmVoiceVisibilityMode;
		AudibilityContext.m_HearPeoplesInSpectate = Config.m_QmVoiceHearPeoplesInSpectate != 0;
		AudibilityContext.m_SenderOtherTeam = SenderOtherTeam;
		AudibilityContext.m_SenderActive = SenderActive;
		AudibilityContext.m_SenderSpec = SenderSpec;
		AudibilityContext.m_ListMode = Config.m_QmVoiceListMode;
		AudibilityContext.m_HearVad = Config.m_QmVoiceHearVad != 0;
		AudibilityContext.m_SenderUsesVad = (Flags & VOICE_FLAG_VAD) != 0;
		AudibilityContext.m_pMuteList = Config.m_aQmVoiceMute;
		AudibilityContext.m_pWhitelist = Config.m_aQmVoiceWhitelist;
		AudibilityContext.m_pBlacklist = Config.m_aQmVoiceBlacklist;
		AudibilityContext.m_pVadAllow = Config.m_aQmVoiceVadAllow;
		if(VoiceUtils::EvaluateVoiceReceiveAudibility(AudibilityContext, pSenderName) != VoiceUtils::EVoiceReceiveAudibility::ALLOW)
			continue;
		m_aLastHeard[SenderId].store(PacketNow);

		if(PayloadSize > (uint16_t)VOICE_MAX_PAYLOAD)
		{
			m_RxDropPayload++;
			continue;
		}
		if((size_t)VOICE_PACKET_HEADER_SIZE + PayloadSize > (size_t)Bytes)
		{
			m_RxDropPayload++;
			continue;
		}
		if(PayloadSize == 0)
		{
			m_RxDropPayload++;
			continue;
		}

		const vec2 SenderPos = vec2(PosX, PosY);
		const float Radius = std::max(1, Config.m_QmVoiceRadius) * 32.0f;
		if(!VoiceUtils::VoiceIsPacketWithinAudibleRadius(LocalPos, SenderPos, Radius, IgnoreDistance))
		{
			m_RxDropRadius++;
			continue;
		}

		const float Dist = distance(LocalPos, SenderPos);
		const float RadiusFactor = IgnoreDistance ? 1.0f : (1.0f - (Dist / Radius));
		float Volume = std::clamp(RadiusFactor * (Config.m_QmVoiceVolume / 100.0f), 0.0f, 4.0f);
		if(Volume <= 0.0f)
			continue;

		int NameVolume = 100;
		if(VoiceUtils::VoiceNameVolume(Config.m_aQmVoiceNameVolumes, pSenderName, NameVolume))
		{
			Volume *= (NameVolume / 100.0f);
			if(Volume <= 0.0f)
				continue;
		}

		const bool StereoEnabled = Config.m_QmVoiceStereo != 0;
		const float StereoWidth = std::clamp(Config.m_QmVoiceStereoWidth / 100.0f, 0.0f, 2.0f);
		const float Pan = StereoEnabled ? std::clamp(((SenderPos.x - LocalPos.x) / Radius) * StereoWidth, -1.0f, 1.0f) : 0.0f;
		const float LeftGain = Volume * (Pan <= 0.0f ? 1.0f : (1.0f - Pan));
		const float RightGain = Volume * (Pan >= 0.0f ? 1.0f : (1.0f + Pan));

		SVoicePeer &Peer = (*m_pPeers)[SenderId];
		const int64_t Now = time_get();
		bool ResetStream = false;
		if(Peer.m_LastRecvTime != 0)
		{
			const int64_t Gap = Now - Peer.m_LastRecvTime;
			if(Gap > time_freq() * 2)
			{
				ResetStream = true;
			}
			else if(Peer.m_HasLastRecvSeq)
			{
				const int Delta = VoiceUtils::VoiceSeqDelta(Sequence, Peer.m_LastRecvSeq);
				if(Delta > SVoicePeer::MAX_JITTER_PACKETS * 8)
					ResetStream = true;
			}
		}
		if(ResetStream)
			ResetPeer(Peer);

		if(Peer.m_LastRecvTime != 0)
		{
			const float DeltaMs = (float)((Now - Peer.m_LastRecvTime) * 1000.0 / (double)time_freq());
			const float Deviation = std::fabs(DeltaMs - 20.0f);
			Peer.m_JitterMs = 0.9f * Peer.m_JitterMs + 0.1f * Deviation;
		}
		Peer.m_LastRecvTime = Now;

		int Target = VoiceUtils::VoiceClampJitterTarget(Peer.m_JitterMs);
		if(Peer.m_HasLastRecvSeq)
		{
			const uint16_t Expected = (uint16_t)(Peer.m_LastRecvSeq + 1);
			if(Sequence != Expected)
				Target = std::min(Target + 1, 6);
		}
		Peer.m_TargetFrames = Target;
		if(Peer.m_HasLastRecvSeq)
		{
			const int Delta = VoiceUtils::VoiceSeqDelta(Sequence, Peer.m_LastRecvSeq);
			if(Delta > 0 && Delta < 1000)
			{
				const int Lost = std::max(0, Delta - 1);
				const float LossRatio = std::clamp(Lost / (float)Delta, 0.0f, 1.0f);
				Peer.m_LossEwma = 0.9f * Peer.m_LossEwma + 0.1f * LossRatio;
			}
		}
		if(!Peer.m_HasLastRecvSeq || VoiceUtils::VoiceSeqLess(Peer.m_LastRecvSeq, Sequence))
			Peer.m_LastRecvSeq = Sequence;
		Peer.m_HasLastRecvSeq = true;
		Peer.m_LastGainLeft = LeftGain;
		Peer.m_LastGainRight = RightGain;

		const int Slot = Sequence % SVoicePeer::MAX_JITTER_PACKETS;
		SVoicePeer::SJitterPacket &Pkt = Peer.m_aPackets[Slot];
		if(Pkt.m_Valid && Pkt.m_Seq != Sequence)
			Peer.m_QueuedPackets = std::max(0, Peer.m_QueuedPackets - 1);
		if(!Pkt.m_Valid || Pkt.m_Seq != Sequence)
			Peer.m_QueuedPackets = std::min(Peer.m_QueuedPackets + 1, SVoicePeer::MAX_JITTER_PACKETS);
		Pkt.m_Valid = true;
		Pkt.m_Seq = Sequence;
		Pkt.m_Size = PayloadSize;
		Pkt.m_LeftGain = LeftGain;
		Pkt.m_RightGain = RightGain;
		mem_copy(Pkt.m_aData, pData + VOICE_PACKET_HEADER_SIZE, PayloadSize);
		if(!Peer.m_HasMinQueuedSeq || VoiceUtils::VoiceSeqLess(Sequence, Peer.m_MinQueuedSeq))
		{
			Peer.m_MinQueuedSeq = Sequence;
			Peer.m_HasMinQueuedSeq = true;
		}
		m_LastRxPacketTime.store(PacketNow);
		m_LastMediaRxPacketTime.store(PacketNow);

		if(Config.m_QmVoiceDebug)
		{
			m_RxPackets++;
			if(Now - m_RxLastLog > time_freq())
			{
				log_info("voice", "rx packets=%d drop_header=%d drop_version=%d drop_type=%d drop_ctx=%d drop_group=%d drop_sender=%d drop_payload=%d drop_radius=%d",
					m_RxPackets,
					m_RxDropHeader,
					m_RxDropVersion,
					m_RxDropType,
					m_RxDropContext,
					m_RxDropGroup,
					m_RxDropSender,
					m_RxDropPayload,
					m_RxDropRadius);
				m_RxLastLog = Now;
				m_RxPackets = 0;
				m_RxDropHeader = 0;
				m_RxDropVersion = 0;
				m_RxDropType = 0;
				m_RxDropContext = 0;
				m_RxDropGroup = 0;
				m_RxDropSender = 0;
				m_RxDropPayload = 0;
				m_RxDropRadius = 0;
			}
		}
	}
}

void CRClientVoice::UpdateConfigSnapshot(bool Force) NO_THREAD_SAFETY_ANALYSIS
{
	const int64_t Now = time_get();
	const int64_t RefreshInterval = (int64_t)time_freq() * VOICE_CONFIG_SNAPSHOT_INTERVAL_MS / 1000;
	if(!Force && m_LastConfigSnapshotUpdate != 0 && Now - m_LastConfigSnapshotUpdate < RefreshInterval)
		return;

	m_LastConfigSnapshotUpdate = Now;
	const CLockScope Guard(m_ConfigMutex);
	m_ConfigSnapshot.m_QmVoiceFilterEnable = g_Config.m_QmVoiceFilterEnable;
	m_ConfigSnapshot.m_QmVoiceAgcEnable = g_Config.m_QmVoiceAgcEnable;
	m_ConfigSnapshot.m_QmVoiceBitrateProfile = g_Config.m_QmVoiceBitrateProfile;
	m_ConfigSnapshot.m_QmVoiceProtocolVersion = g_Config.m_QmVoiceProtocolVersion;
	m_ConfigSnapshot.m_QmVoiceNoiseSuppressEnable = g_Config.m_QmVoiceNoiseSuppressEnable;
	m_ConfigSnapshot.m_QmVoiceNoiseSuppressStrength = g_Config.m_QmVoiceNoiseSuppressStrength;
	m_ConfigSnapshot.m_QmVoicePttReleaseDelayMs = g_Config.m_QmVoicePttReleaseDelayMs;
	m_ConfigSnapshot.m_QmVoiceCompThreshold = g_Config.m_QmVoiceCompThreshold;
	m_ConfigSnapshot.m_QmVoiceCompRatio = g_Config.m_QmVoiceCompRatio;
	m_ConfigSnapshot.m_QmVoiceCompAttackMs = g_Config.m_QmVoiceCompAttackMs;
	m_ConfigSnapshot.m_QmVoiceCompReleaseMs = g_Config.m_QmVoiceCompReleaseMs;
	m_ConfigSnapshot.m_QmVoiceCompMakeup = g_Config.m_QmVoiceCompMakeup;
	m_ConfigSnapshot.m_QmVoiceLimiter = g_Config.m_QmVoiceLimiter;
	m_ConfigSnapshot.m_QmVoiceStereo = g_Config.m_QmVoiceStereo;
	m_ConfigSnapshot.m_QmVoiceStereoWidth = g_Config.m_QmVoiceStereoWidth;
	m_ConfigSnapshot.m_QmVoiceRadius = g_Config.m_QmVoiceRadius;
	m_ConfigSnapshot.m_QmVoiceVolume = g_Config.m_QmVoiceVolume;
	m_ConfigSnapshot.m_QmVoiceMicVolume = g_Config.m_QmVoiceMicVolume;
	m_ConfigSnapshot.m_QmVoiceMicMute = g_Config.m_QmVoiceMicMute;
	m_ConfigSnapshot.m_QmVoiceTestMode = g_Config.m_QmVoiceTestMode;
	m_ConfigSnapshot.m_QmVoiceVadEnable = g_Config.m_QmVoiceVadEnable;
	m_ConfigSnapshot.m_QmVoiceVadThreshold = g_Config.m_QmVoiceVadThreshold;
	m_ConfigSnapshot.m_QmVoiceVadReleaseDelayMs = g_Config.m_QmVoiceVadReleaseDelayMs;
	m_ConfigSnapshot.m_QmVoiceIgnoreDistance = g_Config.m_QmVoiceIgnoreDistance;
	m_ConfigSnapshot.m_QmVoiceGroupGlobal = g_Config.m_QmVoiceGroupGlobal;
	m_ConfigSnapshot.m_QmVoiceVisibilityMode = g_Config.m_QmVoiceVisibilityMode;
	m_ConfigSnapshot.m_QmVoiceListMode = g_Config.m_QmVoiceListMode;
	m_ConfigSnapshot.m_QmVoiceDebug = g_Config.m_QmVoiceDebug;
	m_ConfigSnapshot.m_QmVoiceGroupMode = g_Config.m_QmVoiceGroupMode;
	m_ConfigSnapshot.m_QmVoiceHearOnSpecPos = g_Config.m_QmVoiceHearOnSpecPos;
	m_ConfigSnapshot.m_QmVoiceHearPeoplesInSpectate = g_Config.m_QmVoiceHearPeoplesInSpectate;
	m_ConfigSnapshot.m_QmVoiceHearVad = g_Config.m_QmVoiceHearVad;
	m_ConfigSnapshot.m_ClShowOthers = g_Config.m_ClShowOthers;
	m_ConfigSnapshot.m_QmVoiceTokenHash = VoiceUtils::BuildLegacyVoiceTokenHash(g_Config.m_QmVoiceToken);
	str_copy(m_ConfigSnapshot.m_aQmVoiceWhitelist, g_Config.m_QmVoiceWhitelist, sizeof(m_ConfigSnapshot.m_aQmVoiceWhitelist));
	str_copy(m_ConfigSnapshot.m_aQmVoiceBlacklist, g_Config.m_QmVoiceBlacklist, sizeof(m_ConfigSnapshot.m_aQmVoiceBlacklist));
	str_copy(m_ConfigSnapshot.m_aQmVoiceMute, g_Config.m_QmVoiceMute, sizeof(m_ConfigSnapshot.m_aQmVoiceMute));
	str_copy(m_ConfigSnapshot.m_aQmVoiceVadAllow, g_Config.m_QmVoiceVadAllow, sizeof(m_ConfigSnapshot.m_aQmVoiceVadAllow));
	str_copy(m_ConfigSnapshot.m_aQmVoiceNameVolumes, g_Config.m_QmVoiceNameVolumes, sizeof(m_ConfigSnapshot.m_aQmVoiceNameVolumes));
}

void CRClientVoice::GetConfigSnapshot(SRClientVoiceConfigSnapshot &Out) const NO_THREAD_SAFETY_ANALYSIS
{
	const CLockScope Guard(m_ConfigMutex);
	Out = m_ConfigSnapshot;
}

void CRClientVoice::UpdateEncoderParams()
{
	if(!m_pEncoder)
		return;

	const int64_t Now = time_get();
	if(m_LastEncUpdate != 0 && Now - m_LastEncUpdate < time_freq())
		return;

	SRClientVoiceConfigSnapshot Config;
	GetConfigSnapshot(Config);

	float LossAvg = 0.0f;
	float JitterMax = 0.0f;
	int Count = 0;
	for(const auto &Peer : *m_pPeers)
	{
		if(Peer.m_LastRecvTime == 0)
			continue;
		if(Now - Peer.m_LastRecvTime > time_freq() * 5)
			continue;
		LossAvg += Peer.m_LossEwma;
		JitterMax = std::max(JitterMax, Peer.m_JitterMs);
		Count++;
	}
	if(Count > 0)
		LossAvg /= (float)Count;

	const int LossPerc = (int)std::clamp(LossAvg * 100.0f, 0.0f, 30.0f);

	int TargetBitrate = m_EncBitrate;
	int TargetLoss = 0;
	bool TargetFec = false;
	int TargetComplexity = m_EncComplexity;
	// Share one target table with startup so healthy links get the quality bump
	// while moderate loss/jitter step down more conservatively.
	VoiceUtils::ComputeVoiceEncoderTargetsWithComplexity(LossPerc, JitterMax, Config.m_QmVoiceBitrateProfile, &TargetBitrate, &TargetLoss, &TargetFec, &TargetComplexity);

	if(TargetBitrate != m_EncBitrate)
	{
		opus_encoder_ctl(m_pEncoder, OPUS_SET_BITRATE(TargetBitrate));
		m_EncBitrate = TargetBitrate;
	}
	if(TargetLoss != m_EncLossPerc)
	{
		opus_encoder_ctl(m_pEncoder, OPUS_SET_PACKET_LOSS_PERC(TargetLoss));
		m_EncLossPerc = TargetLoss;
	}
	if(TargetFec != m_EncFec)
	{
		opus_encoder_ctl(m_pEncoder, OPUS_SET_INBAND_FEC(TargetFec ? 1 : 0));
		m_EncFec = TargetFec;
	}
	if(TargetComplexity != m_EncComplexity)
	{
		const int Result = opus_encoder_ctl(m_pEncoder, OPUS_SET_COMPLEXITY(TargetComplexity));
		if(Result == OPUS_OK)
		{
			m_EncComplexity = TargetComplexity;
		}
		else
		{
			log_warn("voice", "OPUS_SET_COMPLEXITY(%d) failed with error %d, keeping previous complexity %d", TargetComplexity, Result, m_EncComplexity);
			opus_encoder_ctl(m_pEncoder, OPUS_GET_COMPLEXITY(&m_EncComplexity));
		}
	}

	m_LastEncUpdate = Now;
}

void CRClientVoice::DecodeJitter()
{
	if(!m_OutputDevice)
		return;

	for(int PeerId = 0; PeerId < MAX_CLIENTS; PeerId++)
	{
		SVoicePeer &Peer = (*m_pPeers)[PeerId];
		if(Peer.m_QueuedPackets <= 0)
			continue;

		if(!Peer.m_HasNextSeq)
		{
			if(!SeedPeerNextSeq(Peer))
				continue;
		}

		int FrameCount = 0;
		SDL_LockAudioDevice(m_OutputDevice);
		FrameCount = Peer.m_FrameCount;
		SDL_UnlockAudioDevice(m_OutputDevice);
		if(FrameCount >= SVoicePeer::MAX_FRAMES)
			continue;

		const int Slot = Peer.m_NextSeq % SVoicePeer::MAX_JITTER_PACKETS;
		SVoicePeer::SJitterPacket *pPkt = nullptr;
		if(Peer.m_aPackets[Slot].m_Valid && Peer.m_aPackets[Slot].m_Seq == Peer.m_NextSeq)
			pPkt = &Peer.m_aPackets[Slot];
		const int NextSlot = (uint16_t)(Peer.m_NextSeq + 1) % SVoicePeer::MAX_JITTER_PACKETS;
		SVoicePeer::SJitterPacket *pNextPkt = nullptr;
		if(Peer.m_aPackets[NextSlot].m_Valid && Peer.m_aPackets[NextSlot].m_Seq == (uint16_t)(Peer.m_NextSeq + 1))
			pNextPkt = &Peer.m_aPackets[NextSlot];

		if(!Peer.m_pDecoder)
		{
			if(Peer.m_DecoderFailed)
				continue;
			int Error = 0;
			Peer.m_pDecoder = opus_decoder_create(VOICE_SAMPLE_RATE, VOICE_CHANNELS, &Error);
			if(!Peer.m_pDecoder || Error != OPUS_OK)
			{
				char aError[256];
				str_format(aError, sizeof(aError), "Failed to create Opus decoder: %d", Error);
				LogDiagnosticErrorOnce(m_aDecoderErrorLog, sizeof(m_aDecoderErrorLog), aError);
				Peer.m_DecoderFailed = true;
				continue;
			}
			ClearDiagnosticLogMessage(m_aDecoderErrorLog);
			Peer.m_HasSeq = false;
		}

		int16_t aPcm[VOICE_FRAME_SAMPLES];
		int Samples = 0;
		float LeftGain = Peer.m_LastGainLeft;
		float RightGain = Peer.m_LastGainRight;
		if(pPkt)
		{
			Samples = opus_decode(Peer.m_pDecoder, pPkt->m_aData, pPkt->m_Size, aPcm, VOICE_FRAME_SAMPLES, 0);
			if(Samples > 0)
			{
				LeftGain = pPkt->m_LeftGain;
				RightGain = pPkt->m_RightGain;
			}
			pPkt->m_Valid = false;
			Peer.m_QueuedPackets = std::max(0, Peer.m_QueuedPackets - 1);
		}
		else if(pNextPkt && Peer.m_LossEwma > 0.02f)
		{
			Samples = opus_decode(Peer.m_pDecoder, pNextPkt->m_aData, pNextPkt->m_Size, aPcm, VOICE_FRAME_SAMPLES, 1);
		}
		else if(Peer.m_HasSeq)
		{
			Samples = opus_decode(Peer.m_pDecoder, nullptr, 0, aPcm, VOICE_FRAME_SAMPLES, 1);
		}

		if(Samples > 0)
		{
			const float Peak = std::clamp(VoiceUtils::VoiceFramePeak(aPcm, Samples) * std::max(LeftGain, RightGain), 0.0f, 1.0f);
			const float PrevLevel = m_aSpeakerLevel[PeerId].load();
			const float NextLevel = Peak >= PrevLevel ? Peak : (PrevLevel * 0.9f);
			m_aSpeakerLevel[PeerId].store(NextLevel);

			SDL_LockAudioDevice(m_OutputDevice);
			PushPeerFrame(PeerId, aPcm, Samples, LeftGain, RightGain);
			SDL_UnlockAudioDevice(m_OutputDevice);
		}
		else
		{
			m_aSpeakerLevel[PeerId].store(m_aSpeakerLevel[PeerId].load() * 0.92f);
		}

		Peer.m_LastSeq = Peer.m_NextSeq;
		Peer.m_HasSeq = true;
		Peer.m_NextSeq = (uint16_t)(Peer.m_NextSeq + 1);
	}
}

void CRClientVoice::StartWorker()
{
	if(m_Worker.joinable())
		return;
	m_WorkerStop.store(false);
	m_WorkerEnabled.store(true);
	m_Worker = std::thread(&CRClientVoice::WorkerLoop, this);
}

void CRClientVoice::StopWorker()
{
	m_WorkerEnabled.store(false);
	if(m_Worker.joinable())
	{
		m_WorkerStop.store(true);
		m_Worker.join();
	}
	m_WorkerStop.store(false);
}

void CRClientVoice::WorkerLoop() NO_THREAD_SAFETY_ANALYSIS
{
	using namespace std::chrono_literals;
	while(!m_WorkerStop.load())
	{
		if(!m_WorkerEnabled.load())
		{
			std::this_thread::sleep_for(10ms);
			continue;
		}

		bool ShouldEnsureAudio = m_AudioRefreshRequested.exchange(false);
		bool CaptureNeedsRetry = m_CaptureUnavailable.load();
#if defined(CONF_PLATFORM_ANDROID)
		if(m_AndroidRecordPermissionKnown && !m_AndroidRecordPermissionGranted)
			CaptureNeedsRetry = false;
#endif
		if(!ShouldEnsureAudio && (CaptureNeedsRetry || m_OutputUnavailable.load()))
		{
			const int64_t RetryInterval = time_freq();
			const int64_t Now = time_get();
			if(m_LastAudioRetryAttempt == 0 || Now - m_LastAudioRetryAttempt >= RetryInterval)
				ShouldEnsureAudio = true;
		}
		if(ShouldEnsureAudio)
			EnsureAudio();

		UpdateVoiceTransport();
		ProcessIncoming();
		DecodeJitter();
		UpdateEncoderParams();
		ProcessCapture();

		SRClientVoiceConfigSnapshot Config;
		GetConfigSnapshot(Config);
		if(Config.m_QmVoiceDebug)
		{
			const int64_t Now = time_get();
			if(m_DebugStateLastLog == 0 || Now - m_DebugStateLastLog >= time_freq())
			{
				bool OnlineSnap = false;
				{
					const CLockScope Guard(m_SnapshotMutex);
					OnlineSnap = m_OnlineSnap;
				}

				int ActivePeers = 0;
				int QueuedPackets = 0;
				int QueuedFrames = 0;
				if(m_pPeers)
				{
					for(const auto &Peer : *m_pPeers)
					{
						if(Peer.m_QueuedPackets > 0 || Peer.m_FrameCount > 0)
							ActivePeers++;
						QueuedPackets += Peer.m_QueuedPackets;
						QueuedFrames += Peer.m_FrameCount;
					}
				}

				SRClientVoiceConfigSnapshot StateConfig;
				GetConfigSnapshot(StateConfig);
				const bool NeedNetwork = std::clamp(StateConfig.m_QmVoiceTestMode, 0, 2) != 1;
				VoiceUtils::SVoiceTransmitPreconditions Preconditions;
				Preconditions.m_NeedNetwork = NeedNetwork;
				Preconditions.m_ServerAddrValid = m_ServerAddrValid.load();
				Preconditions.m_HaveSocket = m_TransportReady.load();
				Preconditions.m_Online = OnlineSnap;
				Preconditions.m_HaveCaptureDevice = m_CaptureDevice != 0;
				Preconditions.m_HaveEncoder = m_pEncoder != nullptr;
				Preconditions.m_MicMuted = StateConfig.m_QmVoiceMicMute != 0;
				const uint32_t TxBlockers = VoiceUtils::VoiceTransmitBlockers(Preconditions);
				char aTxBlockers[128];
				VoiceUtils::FormatVoiceTransmitBlockers(TxBlockers, aTxBlockers, (int)sizeof(aTxBlockers));

				const int TxAgeMs = m_LastTxPacketTime.load() > 0 ? (int)std::clamp((Now - m_LastTxPacketTime.load()) * 1000 / time_freq(), (int64_t)0, (int64_t)999999) : -1;
				const int RxAgeMs = m_LastRxPacketTime.load() > 0 ? (int)std::clamp((Now - m_LastRxPacketTime.load()) * 1000 / time_freq(), (int64_t)0, (int64_t)999999) : -1;

				log_info("voice", "state server_valid=%d ping=%d socket=%d online=%d out=%d cap=%d enc=%d mute=%d tx_block=0x%x tx_blockers=%s tx=%d tx_age=%d mic=%.2f rx_age=%d drop_header=%d drop_version=%d drop_type=%d drop_ctx=%d drop_group=%d drop_sender=%d drop_payload=%d drop_radius=%d peers=%d queued_packets=%d queued_frames=%d",
					m_ServerAddrValid.load() ? 1 : 0,
					m_PingMs.load(),
					m_TransportReady.load() ? 1 : 0,
					OnlineSnap ? 1 : 0,
					m_OutputDevice != 0 ? 1 : 0,
					m_CaptureDevice != 0 ? 1 : 0,
					m_pEncoder != nullptr ? 1 : 0,
					StateConfig.m_QmVoiceMicMute != 0 ? 1 : 0,
					(unsigned)TxBlockers,
					aTxBlockers,
					m_TxWasActive.load() ? 1 : 0,
					TxAgeMs,
					(double)m_MicLevel.load(),
					RxAgeMs,
					m_RxDropHeader,
					m_RxDropVersion,
					m_RxDropType,
					m_RxDropContext,
					m_RxDropGroup,
					m_RxDropSender,
					m_RxDropPayload,
					m_RxDropRadius,
					ActivePeers,
					QueuedPackets,
					QueuedFrames);
				m_DebugStateLastLog = Now;
			}
		}

		std::this_thread::sleep_for(5ms);
	}
}

void CRClientVoice::OnRender() NO_THREAD_SAFETY_ANALYSIS
{
	if(!g_Config.m_QmVoiceEnable || !m_pGameClient || !m_pClient)
	{
		Shutdown();
		return;
	}
	m_ShutdownDone = false;

#if defined(CONF_PLATFORM_EMSCRIPTEN)
	if(!m_UnsupportedPlatformLogged)
	{
		log_info("voice", "voice runtime is unavailable on emscripten, skipping voice initialization");
		m_UnsupportedPlatformLogged = true;
	}
	Shutdown();
	return;
#endif

	if(g_Config.m_QmVoiceOffNonActive && m_pGraphics && !m_pGraphics->WindowActive())
	{
		StopWorker();
		if(m_pVoiceTransport)
			m_pVoiceTransport->Disconnect();
		m_TransportReady.store(false);
		m_TransportConnecting.store(false);
		ResetRuntimeState(VoiceUtils::VOICE_RUNTIME_RESET_CONNECTION | VoiceUtils::VOICE_RUNTIME_RESET_PEERS, m_RoomMemberTokenHash.load());
		if(!m_AudioPausedForInactive)
		{
			if(m_CaptureDevice)
			{
				SDL_ClearQueuedAudio(m_CaptureDevice);
				SDL_PauseAudioDevice(m_CaptureDevice, 1);
			}
			if(m_OutputDevice)
				SDL_PauseAudioDevice(m_OutputDevice, 1);
			m_AudioPausedForInactive = true;
		}
		ClearPeerFrames();
		return;
	}
	if(m_AudioPausedForInactive)
	{
		if(m_CaptureDevice)
			SDL_PauseAudioDevice(m_CaptureDevice, 0);
		if(m_OutputDevice)
			SDL_PauseAudioDevice(m_OutputDevice, 0);
		m_AudioPausedForInactive = false;
	}

	UpdateServerAddrConfig();
	const bool ContextChanged = UpdateContext();
	UpdateClientSnapshot(ContextChanged);
	UpdateConfigSnapshot();
	SRClientVoiceConfigSnapshot RuntimeConfig;
	GetConfigSnapshot(RuntimeConfig);

	bool OnlineForReset = false;
	{
		const CLockScope Guard(m_SnapshotMutex);
		OnlineForReset = m_OnlineSnap;
	}
	const uint32_t RuntimeResetFlags = VoiceUtils::VoiceRuntimeResetFlags(
		ContextChanged,
		OnlineForReset,
		m_RoomMemberTokenHash.load(),
		RuntimeConfig.m_QmVoiceTokenHash);

#if defined(CONF_PLATFORM_ANDROID)
	if(!m_AndroidRecordPermissionKnown)
	{
		m_AndroidRecordPermissionGranted = RequestAndroidAudioRecordPermission();
		m_AndroidRecordPermissionKnown = true;
		if(!m_AndroidRecordPermissionGranted)
		{
			LogDiagnosticErrorOnce(m_aAudioErrorLog, sizeof(m_aAudioErrorLog), "Microphone permission denied on Android");
			m_CaptureUnavailable = true;
			m_CaptureReady.store(false);
		}
	}
#endif

	VoiceUtils::SVoiceAudioDeviceConfig RequestedAudioConfig;
	BuildRequestedAudioDeviceConfig(RequestedAudioConfig);
	VoiceUtils::SVoiceAudioDeviceConfig AppliedRequestedAudioConfig;
	GetRequestedAudioDeviceConfig(AppliedRequestedAudioConfig);
	const int DesiredChannels = VoiceUtils::VoiceDesiredOutputChannels(RequestedAudioConfig);
	VoiceUtils::SVoiceAudioRefreshState RefreshState;
	RefreshState.m_BackendChanged = str_comp(AppliedRequestedAudioConfig.m_aBackend, RequestedAudioConfig.m_aBackend) != 0;
	RefreshState.m_InputDeviceChanged = str_comp(AppliedRequestedAudioConfig.m_aInputDevice, RequestedAudioConfig.m_aInputDevice) != 0;
	RefreshState.m_OutputDeviceChanged = str_comp(AppliedRequestedAudioConfig.m_aOutputDevice, RequestedAudioConfig.m_aOutputDevice) != 0;
	RefreshState.m_StereoChanged = AppliedRequestedAudioConfig.m_OutputStereo != RequestedAudioConfig.m_OutputStereo;
	if(RefreshState.m_BackendChanged || RefreshState.m_InputDeviceChanged || RefreshState.m_OutputDeviceChanged || RefreshState.m_StereoChanged)
		SetRequestedAudioDeviceConfig(RequestedAudioConfig);
	RefreshState.m_EncoderReady = m_EncoderReady.load();
	RefreshState.m_OutputReady = m_OutputReady.load();
	RefreshState.m_CaptureReady = m_CaptureReady.load();
	RefreshState.m_OutputUnavailable = m_OutputUnavailable.load();
	RefreshState.m_CaptureUnavailable = m_CaptureUnavailable.load();
	RefreshState.m_CurrentOutputChannels = m_OutputChannels.load();
	RefreshState.m_DesiredOutputChannels = DesiredChannels;
#if defined(CONF_PLATFORM_ANDROID)
	if(m_AndroidRecordPermissionKnown && !m_AndroidRecordPermissionGranted)
		RefreshState.m_CaptureUnavailable = true;
#endif
	const bool NeedReinit = VoiceUtils::VoiceNeedsAudioRefresh(RefreshState);
	bool WorkerStopped = false;
	bool NeedRuntimeReset = false;
	if((RuntimeResetFlags & VoiceUtils::VOICE_RUNTIME_RESET_CONNECTION) != 0)
		NeedRuntimeReset = NeedRuntimeReset || HasConnectionRuntimeState();
	if((RuntimeResetFlags & VoiceUtils::VOICE_RUNTIME_RESET_PEERS) != 0)
		NeedRuntimeReset = NeedRuntimeReset || HasPeerRuntimeState(RuntimeConfig.m_QmVoiceTokenHash);
	if(NeedRuntimeReset)
	{
		StopWorker();
		WorkerStopped = true;
		ResetRuntimeState(RuntimeResetFlags, RuntimeConfig.m_QmVoiceTokenHash);
	}

	if(NeedReinit)
	{
		if(!WorkerStopped)
			StopWorker();
		m_AudioRefreshRequested.store(true);
	}
	if(!m_Worker.joinable())
		m_AudioRefreshRequested.store(true);

	StartWorker();
}

void CRClientVoice::RenderSpeakerOverlay() NO_THREAD_SAFETY_ANALYSIS
{
	if(!m_pGameClient || !m_pGraphics || !g_Config.m_QmVoiceEnable)
		return;

	const bool HudEditorPreview = m_pGameClient->m_HudEditor.IsActive();

	ITextRender *pTextRender = m_pGameClient->TextRender();
	if(!pTextRender)
		return;

	struct SSpeakerEntry
	{
		int m_ClientId = -1;
		uint64_t m_OverlayOrder = 0;
		bool m_IsLocal = false;
		char m_aName[MAX_NAME_LENGTH] = {};
		float m_FullNameWidth = 0.0f;
		float m_NameWidth = 0.0f;
		float m_RowWidth = 0.0f;
	};

	std::array<std::array<char, MAX_NAME_LENGTH>, MAX_CLIENTS> aaClientNames{};
	std::array<int, 2> aLocalClientIds{};
	aLocalClientIds.fill(-1);
	int PreferredLocalId = -1;
	bool Online = false;
	{
		const CLockScope Guard(m_SnapshotMutex);
		Online = m_OnlineSnap;
		if(!Online)
			return;

		PreferredLocalId = m_LocalClientIdSnap;
		aLocalClientIds = m_aLocalClientIdsSnap;
		aaClientNames = m_aClientNameSnap;
	}

	const int64_t Now = time_get();
	const int64_t VisibleWindow = (int64_t)time_freq() * VOICE_OVERLAY_VISIBLE_MS / 1000;
	std::array<bool, MAX_CLIENTS> aVisibleNow{};
	aVisibleNow.fill(false);
	std::array<SSpeakerEntry, MAX_CLIENTS> aEntries{};
	int EntryCount = 0;

	bool LocalEntryAdded = false;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const int64_t LastHeard = m_aLastHeard[ClientId].load();
		if(LastHeard <= 0)
			continue;
		if(Now - LastHeard >= VisibleWindow)
			continue;

		if(aaClientNames[ClientId][0] == '\0')
			continue;

		bool IsLocalSpeaker = false;
		for(const int LocalId : aLocalClientIds)
		{
			if(LocalId == ClientId)
			{
				IsLocalSpeaker = true;
				break;
			}
		}

		if(IsLocalSpeaker)
		{
			if(PreferredLocalId >= 0 && ClientId != PreferredLocalId)
				continue;
			if(PreferredLocalId < 0 && LocalEntryAdded)
				continue;
			LocalEntryAdded = true;
		}

		aVisibleNow[ClientId] = true;
		if(m_aOverlayOrder[ClientId] == 0)
		{
			m_aOverlayOrder[ClientId] = m_NextOverlayOrder++;
			if(m_NextOverlayOrder == 0)
				m_NextOverlayOrder = 1;
		}

		SSpeakerEntry &Entry = aEntries[EntryCount++];
		Entry.m_ClientId = ClientId;
		Entry.m_OverlayOrder = m_aOverlayOrder[ClientId];
		Entry.m_IsLocal = IsLocalSpeaker;
		str_copy(Entry.m_aName, aaClientNames[ClientId].data(), sizeof(Entry.m_aName));
	}

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!aVisibleNow[ClientId])
			m_aOverlayOrder[ClientId] = 0;
	}

	if(EntryCount == 0 && !HudEditorPreview)
		return;

	if(EntryCount == 0 && HudEditorPreview)
	{
		SSpeakerEntry &LocalEntry = aEntries[EntryCount++];
		LocalEntry.m_ClientId = 0;
		LocalEntry.m_OverlayOrder = 1;
		LocalEntry.m_IsLocal = true;
		str_copy(LocalEntry.m_aName, "You", sizeof(LocalEntry.m_aName));

		SSpeakerEntry &TeammateEntry = aEntries[EntryCount++];
		TeammateEntry.m_ClientId = 1;
		TeammateEntry.m_OverlayOrder = 2;
		TeammateEntry.m_IsLocal = false;
		str_copy(TeammateEntry.m_aName, "Teammate", sizeof(TeammateEntry.m_aName));
	}

	if(EntryCount > 1)
	{
		std::sort(aEntries.begin(), aEntries.begin() + EntryCount, [](const SSpeakerEntry &Left, const SSpeakerEntry &Right) {
			if(Left.m_OverlayOrder != Right.m_OverlayOrder)
				return Left.m_OverlayOrder < Right.m_OverlayOrder;
			return Left.m_ClientId < Right.m_ClientId;
		});
	}

	if(EntryCount > VOICE_OVERLAY_MAX_SPEAKERS)
		EntryCount = VOICE_OVERLAY_MAX_SPEAKERS;

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	m_pGraphics->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	const float HudWidth = 300.0f * m_pGraphics->ScreenAspect();
	const float HudHeight = 300.0f;
	m_pGraphics->MapScreen(0.0f, 0.0f, HudWidth, HudHeight);

	constexpr float PanelX = 6.0f;
	constexpr float PanelY = 74.0f;
	constexpr float RowHeight = 12.0f;
	constexpr float RowGap = 2.0f;
	constexpr float RowRadius = 5.0f;
	constexpr float RowPaddingX = 3.0f;
	constexpr float UserBoxWidth = 11.0f;
	constexpr float UserToNameGap = 2.5f;
	constexpr float NameToMicGap = 3.0f;
	constexpr float NameFontSize = 5.5f;
	constexpr float IconFontSize = 5.4f;
	constexpr float UserIconFontSize = 5.1f;
	constexpr float MaxNameWidth = 52.0f;

	const unsigned int PrevFlags = pTextRender->GetRenderFlags();
	const ColorRGBA PrevTextColor = pTextRender->GetTextColor();
	const ColorRGBA PrevOutlineColor = pTextRender->GetTextOutlineColor();
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
	pTextRender->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.40f);

	pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
	const float UserIconWidth = UserIconFontSize;
	const float MicIconWidth = pTextRender->TextWidth(IconFontSize, s_pVoiceOverlayMicIcon);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	float PanelWidth = 0.0f;
	for(int EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		SSpeakerEntry &Entry = aEntries[EntryIndex];
		Entry.m_FullNameWidth = std::round(pTextRender->TextBoundingBox(NameFontSize, Entry.m_aName).m_W);
		Entry.m_NameWidth = std::min(Entry.m_FullNameWidth, MaxNameWidth);
		Entry.m_RowWidth = RowPaddingX + UserBoxWidth + UserToNameGap + Entry.m_NameWidth + NameToMicGap + MicIconWidth + RowPaddingX;
		PanelWidth = std::max(PanelWidth, Entry.m_RowWidth);
	}
	const float PanelHeight = EntryCount == 0 ? 0.0f : EntryCount * RowHeight + (EntryCount - 1) * RowGap;
	const CUIRect PanelRect = {PanelX, PanelY, PanelWidth, PanelHeight};
	const auto HudEditorScope = m_pGameClient->m_HudEditor.BeginTransform(EHudEditorElement::VoiceOverlay, PanelRect);

	for(int Index = 0; Index < EntryCount; ++Index)
	{
		const SSpeakerEntry &Entry = aEntries[Index];
		const float NameWidth = Entry.m_NameWidth;
		const float RowWidth = Entry.m_RowWidth;
		const float RowY = PanelY + Index * (RowHeight + RowGap);
		const float RowX = PanelX;

		ColorRGBA RowColor(0.10f, 0.11f, 0.14f, 0.82f);
		if(Entry.m_IsLocal)
			RowColor = ColorRGBA(0.12f, 0.13f, 0.17f, 0.88f);
		m_pGraphics->DrawRect(RowX, RowY, RowWidth, RowHeight, RowColor, IGraphics::CORNER_ALL, RowRadius);

		const ColorRGBA UserBoxColor(1.0f, 1.0f, 1.0f, 0.10f);
		m_pGraphics->DrawRect(RowX + 1.0f, RowY + 1.0f, UserBoxWidth, RowHeight - 2.0f, UserBoxColor, IGraphics::CORNER_ALL, RowRadius - 1.0f);

		const float UserIconX = RowX + 1.0f + (UserBoxWidth - UserIconWidth) * 0.5f;
		const float UserIconY = RowY + (RowHeight - UserIconFontSize) * 0.5f - 0.5f;
		m_pGameClient->Ui()->DrawQmIconAt(UserIconX, UserIconY, UserIconFontSize, EQmIcon::USERS, FontIcons::FONT_ICON_USERS, ColorRGBA(1.0f, 1.0f, 1.0f, 0.82f));

		const float MicIconX = RowX + RowWidth - RowPaddingX - MicIconWidth;
		const float MicIconY = RowY + (RowHeight - IconFontSize) * 0.5f - 0.5f;
		pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
		pTextRender->TextColor(1.0f, 1.0f, 1.0f, 0.90f);
		pTextRender->Text(MicIconX, MicIconY, IconFontSize, s_pVoiceOverlayMicIcon, -1.0f);

		const float NameX = RowX + RowPaddingX + UserBoxWidth + UserToNameGap;
		const float NameY = RowY + (RowHeight - NameFontSize) * 0.5f - 0.5f;
		pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
		pTextRender->TextColor(0.97f, 0.98f, 1.0f, 0.94f);
		if(NameWidth + 0.01f < Entry.m_FullNameWidth)
		{
			CTextCursor Cursor;
			Cursor.m_FontSize = NameFontSize;
			Cursor.m_LineWidth = MaxNameWidth;
			Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END;
			Cursor.SetPosition(vec2(NameX, NameY));
			pTextRender->TextEx(&Cursor, Entry.m_aName);
		}
		else
		{
			pTextRender->Text(NameX, NameY, NameFontSize, Entry.m_aName, -1.0f);
		}
	}

	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	pTextRender->TextColor(PrevTextColor);
	pTextRender->TextOutlineColor(PrevOutlineColor);
	pTextRender->SetRenderFlags(PrevFlags);
	m_pGameClient->m_HudEditor.UpdateVisibleRect(EHudEditorElement::VoiceOverlay, PanelRect);
	m_pGameClient->m_HudEditor.EndTransform(HudEditorScope);
	m_pGraphics->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
}
