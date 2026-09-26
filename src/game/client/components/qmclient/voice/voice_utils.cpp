// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "voice_utils.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/shared/protocol.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>
#include <utility>

namespace VoiceUtils
{
	const char *EffectiveVoiceWebSocketUrl(const char *pUrl)
	{
		if(!pUrl || pUrl[0] == '\0' || str_comp(pUrl, "42.194.185.210:9987") == 0)
			return "wss://qmclient.icu/ws/voice";
		return pUrl;
	}

	CVoiceWebSocketTransport::CVoiceWebSocketTransport() :
		CVoiceWebSocketTransport(CreateQmWebSocketClient({}))
	{
	}

	CVoiceWebSocketTransport::CVoiceWebSocketTransport(std::unique_ptr<IQmWebSocketClient> pClient) :
		m_pClient(std::move(pClient))
	{
		IQmWebSocketClient::STuning Tuning;
		Tuning.m_OutgoingQueueCapacity = 8;
		m_pClient->SetTuning(Tuning);
	}

	bool CVoiceWebSocketTransport::Update(const char *pUrl, bool Enabled, uint32_t ContextHash, uint32_t TokenHash, uint8_t ProtocolVersion)
	{
		const std::string Url = EffectiveVoiceWebSocketUrl(pUrl);
		const bool ConfigChanged = m_Url != Url || m_ContextHash != ContextHash || m_TokenHash != TokenHash || m_ProtocolVersion != ProtocolVersion;
		bool ResetRuntime = ConfigChanged || m_Enabled != Enabled;
		if(ResetRuntime)
		{
			Disconnect();
			m_Url = Url;
			m_ContextHash = ContextHash;
			m_TokenHash = TokenHash;
			m_ProtocolVersion = ProtocolVersion;
			m_Enabled = Enabled;
			m_Error.clear();
			SQmWebSocketConnectConfig Config;
			m_Error = ParseQmWebSocketUrl(m_Url.c_str(), Config);
			m_UrlValid = m_Error.empty();
			if(m_Enabled && m_UrlValid)
			{
				Config.m_MaxMessageSize = 128 * 1024;
				if(!m_pClient->Available())
					m_Error = m_pClient->UnavailableReason();
				else if(!m_pClient->Connect(Config, m_Error) && m_Error.empty())
					m_Error = "voice WebSocket connection failed";
			}
		}
		const bool IsConnected = m_Enabled && m_UrlValid && m_pClient->Desired() && m_pClient->State() == EQmWebSocketState::CONNECTED;
		const int64_t ConnectedTick = IsConnected ? m_pClient->LastConnectedTick() : 0;
		ResetRuntime = ResetRuntime || m_Connected != IsConnected || (IsConnected && m_LastConnectedTick != ConnectedTick);
		m_Connected = IsConnected;
		m_LastConnectedTick = ConnectedTick;
		if(IsConnected)
			m_Error.clear();
		return ResetRuntime;
	}

	void CVoiceWebSocketTransport::Disconnect()
	{
		m_pClient->Disconnect();
		m_Enabled = false;
		m_Connected = false;
		m_LastConnectedTick = 0;
	}

	bool CVoiceWebSocketTransport::Connected() const
	{
		return m_Connected && m_pClient->Desired() && m_pClient->State() == EQmWebSocketState::CONNECTED && m_pClient->LastConnectedTick() == m_LastConnectedTick;
	}

	bool CVoiceWebSocketTransport::Connecting() const
	{
		return m_Enabled && m_UrlValid && m_pClient->Desired() && !Connected();
	}

	const char *CVoiceWebSocketTransport::LastError() const
	{
		if(!m_Error.empty())
			return m_Error.c_str();
		return m_Enabled && !Connected() ? m_pClient->LastError() : "";
	}

	bool CVoiceWebSocketTransport::SendPacket(const uint8_t *pData, size_t Size)
	{
		if(!pData || Size < VOICE_PACKET_HEADER_SIZE || Size > VOICE_MAX_PACKET || !Connected())
			return false;
		return m_pClient->SendBinary(reinterpret_cast<const char *>(pData), Size);
	}

	bool CVoiceWebSocketTransport::PollPacket(SQmWebSocketMessage &Out)
	{
		SQmWebSocketMessage Message;
		while(Connected() && m_pClient->PollMessage(Message))
		{
			if(Message.m_Type != EQmWebSocketMessageType::BINARY || Message.m_Data.size() < VOICE_PACKET_HEADER_SIZE || Message.m_Data.size() > VOICE_MAX_PACKET)
				continue;
			Out = std::move(Message);
			return true;
		}
		return false;
	}

	static constexpr char VOICE_MAGIC[4] = {'R', 'V', '0', '1'};
	static constexpr uint32_t VOICE_GROUP_MASK = 0x3fffffff;

	static std::mutex s_VoiceProcessTraceMutex;
	static VoiceProcessTraceCallback s_pVoiceProcessTraceCallback = nullptr;
	static void *s_pVoiceProcessTraceUserData = nullptr;

	static bool VoiceContainsNoCase(const char *pHaystack, const char *pNeedle)
	{
		return pHaystack && pNeedle && pNeedle[0] != '\0' && str_find_nocase(pHaystack, pNeedle) != nullptr;
	}

	uint32_t VoiceTokenGroupHash(uint32_t TokenHash)
	{
		return TokenHash & VOICE_GROUP_MASK;
	}

	uint32_t BuildLegacyVoiceTokenHash(const char *pToken)
	{
		if(!pToken || pToken[0] == '\0')
			return 0;

		return str_quickhash(pToken);
	}

	bool FindMinLiveVoiceSeq(const uint8_t *pValid, const uint16_t *pSeq, size_t Count, uint16_t &OutSeq)
	{
		if(!pValid || !pSeq || Count == 0)
			return false;

		bool Found = false;
		uint16_t MinSeq = 0;
		for(size_t i = 0; i < Count; i++)
		{
			if(pValid[i] == 0)
				continue;
			if(!Found || VoiceSeqLess(pSeq[i], MinSeq))
			{
				MinSeq = pSeq[i];
				Found = true;
			}
		}

		if(Found)
			OutSeq = MinSeq;
		return Found;
	}

	bool SeedVoiceJitterStartSeq(int QueuedPackets, int TargetFrames, bool HasNextSeq, uint16_t InitialNextSeq, const uint8_t *pValid, const uint16_t *pSeq, size_t Count, bool &OutHasNextSeq, uint16_t &OutNextSeq)
	{
		OutHasNextSeq = HasNextSeq;
		OutNextSeq = InitialNextSeq;
		if(OutHasNextSeq)
			return true;
		if(QueuedPackets < TargetFrames)
			return false;

		uint16_t StartSeq = 0;
		if(!FindMinLiveVoiceSeq(pValid, pSeq, Count, StartSeq))
			return false;

		OutHasNextSeq = true;
		OutNextSeq = StartSeq;
		return true;
	}

	void WriteU16(uint8_t *pBuf, uint16_t Value)
	{
		pBuf[0] = Value & 0xff;
		pBuf[1] = (Value >> 8) & 0xff;
	}

	void WriteU32(uint8_t *pBuf, uint32_t Value)
	{
		pBuf[0] = Value & 0xff;
		pBuf[1] = (Value >> 8) & 0xff;
		pBuf[2] = (Value >> 16) & 0xff;
		pBuf[3] = (Value >> 24) & 0xff;
	}

	void WriteFloat(uint8_t *pBuf, float Value)
	{
		static_assert(sizeof(float) == 4, "float must be 4 bytes");
		uint32_t Bits = 0;
		mem_copy(&Bits, &Value, sizeof(Bits));
		WriteU32(pBuf, Bits);
	}

	uint16_t ReadU16(const uint8_t *pBuf)
	{
		return (uint16_t)pBuf[0] | ((uint16_t)pBuf[1] << 8);
	}

	uint32_t ReadU32(const uint8_t *pBuf)
	{
		return (uint32_t)pBuf[0] | ((uint32_t)pBuf[1] << 8) | ((uint32_t)pBuf[2] << 16) | ((uint32_t)pBuf[3] << 24);
	}

	float ReadFloat(const uint8_t *pBuf)
	{
		uint32_t Bits = ReadU32(pBuf);
		float Value = 0.0f;
		mem_copy(&Value, &Bits, sizeof(Value));
		return Value;
	}

	bool WriteVoicePacketHeader(uint8_t *pBuf, size_t BufSize, const SVoicePacketHeader &Header)
	{
		if(!pBuf || BufSize < VOICE_PACKET_HEADER_SIZE)
			return false;

		size_t Offset = 0;
		mem_copy(pBuf + Offset, VOICE_MAGIC, sizeof(VOICE_MAGIC));
		Offset += sizeof(VOICE_MAGIC);
		pBuf[Offset++] = Header.m_Version;
		pBuf[Offset++] = Header.m_Type;
		WriteU16(pBuf + Offset, Header.m_PayloadSize);
		Offset += sizeof(uint16_t);
		WriteU32(pBuf + Offset, Header.m_ContextHash);
		Offset += sizeof(uint32_t);
		WriteU32(pBuf + Offset, Header.m_TokenHash);
		Offset += sizeof(uint32_t);
		pBuf[Offset++] = Header.m_Flags;
		WriteU16(pBuf + Offset, Header.m_SenderId);
		Offset += sizeof(uint16_t);
		WriteU16(pBuf + Offset, Header.m_Sequence);
		Offset += sizeof(uint16_t);
		WriteFloat(pBuf + Offset, Header.m_PosX);
		Offset += sizeof(float);
		WriteFloat(pBuf + Offset, Header.m_PosY);
		return true;
	}

	bool ReadVoicePacketHeader(const uint8_t *pBuf, size_t BufSize, SVoicePacketHeader &OutHeader)
	{
		if(!pBuf || BufSize < VOICE_PACKET_HEADER_SIZE)
			return false;
		if(mem_comp(pBuf, VOICE_MAGIC, sizeof(VOICE_MAGIC)) != 0)
			return false;

		size_t Offset = sizeof(VOICE_MAGIC);
		OutHeader.m_Version = pBuf[Offset++];
		OutHeader.m_Type = pBuf[Offset++];
		OutHeader.m_PayloadSize = ReadU16(pBuf + Offset);
		Offset += sizeof(uint16_t);
		OutHeader.m_ContextHash = ReadU32(pBuf + Offset);
		Offset += sizeof(uint32_t);
		OutHeader.m_TokenHash = ReadU32(pBuf + Offset);
		Offset += sizeof(uint32_t);
		OutHeader.m_Flags = pBuf[Offset++];
		OutHeader.m_SenderId = ReadU16(pBuf + Offset);
		Offset += sizeof(uint16_t);
		OutHeader.m_Sequence = ReadU16(pBuf + Offset);
		Offset += sizeof(uint16_t);
		OutHeader.m_PosX = ReadFloat(pBuf + Offset);
		Offset += sizeof(float);
		OutHeader.m_PosY = ReadFloat(pBuf + Offset);
		if(OutHeader.m_PayloadSize > VOICE_MAX_PAYLOAD ||
			(size_t)VOICE_PACKET_HEADER_SIZE + OutHeader.m_PayloadSize != BufSize ||
			!std::isfinite(OutHeader.m_PosX) || !std::isfinite(OutHeader.m_PosY))
			return false;
		return true;
	}

	const char *VoicePacketTypeName(uint8_t Type)
	{
		switch(Type)
		{
		case VOICE_TYPE_AUDIO:
			return "audio";
		case VOICE_TYPE_PING:
			return "ping";
		case VOICE_TYPE_PONG:
			return "pong";
		default:
			return "unknown";
		}
	}

	bool VoiceAudioDeviceConfigEquals(const SVoiceAudioDeviceConfig &Left, const SVoiceAudioDeviceConfig &Right)
	{
		return str_comp(Left.m_aBackend, Right.m_aBackend) == 0 &&
		       str_comp(Left.m_aInputDevice, Right.m_aInputDevice) == 0 &&
		       str_comp(Left.m_aOutputDevice, Right.m_aOutputDevice) == 0 &&
		       Left.m_OutputStereo == Right.m_OutputStereo;
	}

	int VoiceDesiredOutputChannels(const SVoiceAudioDeviceConfig &Config)
	{
		return Config.m_OutputStereo ? 2 : 1;
	}

	SVoiceProcessingFactoryDefaults VoiceProcessingFactoryDefaults()
	{
		return {};
	}

	SVoiceAgcConfig VoiceAgcConfigFromRuntime(bool EnableAgc)
	{
		SVoiceAgcConfig Config;
		Config.m_Enable = EnableAgc;
		Config.m_TargetRms = 0.18f;
		Config.m_MaxGain = 2.0f;
		Config.m_MinGain = 0.75f;
		Config.m_AttackSec = 0.050f;
		Config.m_ReleaseSec = 0.350f;
		return Config;
	}

	int VoiceClampJitterTarget(float JitterMs)
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

	int VoiceSeqDelta(uint16_t NewSeq, uint16_t OldSeq)
	{
		return (int)(int16_t)(NewSeq - OldSeq);
	}

	bool VoiceSeqLess(uint16_t A, uint16_t B)
	{
		return (int16_t)(A - B) < 0;
	}

	void SetVoiceProcessTraceCallback(VoiceProcessTraceCallback pCallback, void *pUserData)
	{
		std::lock_guard<std::mutex> Guard(s_VoiceProcessTraceMutex);
		s_pVoiceProcessTraceCallback = pCallback;
		s_pVoiceProcessTraceUserData = pUserData;
	}

	void TraceVoiceProcessStage(EVoiceProcessStage Stage)
	{
		VoiceProcessTraceCallback pCallback = nullptr;
		void *pUserData = nullptr;
		{
			std::lock_guard<std::mutex> Guard(s_VoiceProcessTraceMutex);
			pCallback = s_pVoiceProcessTraceCallback;
			pUserData = s_pVoiceProcessTraceUserData;
		}
		if(pCallback)
			pCallback(Stage, pUserData);
	}

	void BuildVoiceDeviceDropdownEntries(
		const std::vector<std::string> &vDetectedDeviceNames,
		const char *pCurrentDevice,
		const char *pDefaultLabel,
		const char *pDisconnectedSuffix,
		std::vector<SVoiceDeviceDropdownEntry> &vEntries)
	{
		vEntries.clear();
		vEntries.push_back({pDefaultLabel ? pDefaultLabel : "", "", false});

		for(const auto &DeviceName : vDetectedDeviceNames)
		{
			if(DeviceName.empty())
				continue;

			bool Exists = false;
			for(const auto &Entry : vEntries)
			{
				if(str_comp_nocase(Entry.m_ConfigValue.c_str(), DeviceName.c_str()) == 0)
				{
					Exists = true;
					break;
				}
			}
			if(Exists)
				continue;

			vEntries.push_back({DeviceName, DeviceName, false});
		}

		if(!pCurrentDevice || pCurrentDevice[0] == '\0')
			return;

		for(const auto &Entry : vEntries)
		{
			if(str_comp_nocase(Entry.m_ConfigValue.c_str(), pCurrentDevice) == 0)
				return;
		}

		char aDisplay[160];
		if(pDisconnectedSuffix && pDisconnectedSuffix[0] != '\0')
			str_format(aDisplay, sizeof(aDisplay), "%s (%s)", pCurrentDevice, pDisconnectedSuffix);
		else
			str_copy(aDisplay, pCurrentDevice, sizeof(aDisplay));
		vEntries.push_back({aDisplay, pCurrentDevice, true});
	}

	int VoiceFindSelectedDeviceIndex(const std::vector<SVoiceDeviceDropdownEntry> &vEntries, const char *pCurrentDevice)
	{
		if(!pCurrentDevice || pCurrentDevice[0] == '\0')
			return 0;

		for(size_t i = 1; i < vEntries.size(); i++)
		{
			if(str_comp_nocase(vEntries[i].m_ConfigValue.c_str(), pCurrentDevice) == 0)
				return (int)i;
		}
		return 0;
	}

	int ResolveNoiseSuppressMode(int ConfigValue, bool RnnoiseRuntimeAvailable, bool *pFallbackUsed)
	{
		if(pFallbackUsed)
			*pFallbackUsed = false;

		const int Mode = std::clamp(ConfigValue, VOICE_NOISE_SUPPRESS_OFF, VOICE_NOISE_SUPPRESS_RNNOISE);
		if(Mode != VOICE_NOISE_SUPPRESS_RNNOISE || RnnoiseRuntimeAvailable)
			return Mode;

		if(pFallbackUsed)
			*pFallbackUsed = true;
		return VOICE_NOISE_SUPPRESS_SIMPLE;
	}

	bool IsRnnoiseCompiledIn()
	{
#if defined(CONF_RNNOISE)
		return true;
#else
		return false;
#endif
	}

	EVoiceIncomingPacketDecision ClassifyVoiceIncomingPacket(const SVoicePacketHeader &Header, size_t PacketSize, const SVoiceIncomingPacketContext &Context)
	{
		if(PacketSize < VOICE_PACKET_HEADER_SIZE)
			return EVoiceIncomingPacketDecision::DROP_HEADER;
		if(Header.m_Version != Context.m_ProtocolVersion)
			return EVoiceIncomingPacketDecision::DROP_VERSION;
		if(Header.m_Type != VOICE_TYPE_AUDIO && Header.m_Type != VOICE_TYPE_PING && Header.m_Type != VOICE_TYPE_PONG)
			return EVoiceIncomingPacketDecision::DROP_TYPE;
		if((Header.m_Flags & ~VOICE_ALLOWED_FLAGS) != 0)
			return EVoiceIncomingPacketDecision::DROP_FLAGS;
		if(Header.m_ContextHash == 0 || Header.m_ContextHash != Context.m_LocalContextHash)
			return EVoiceIncomingPacketDecision::DROP_CONTEXT;

		if(Header.m_Type == VOICE_TYPE_PING || Header.m_Type == VOICE_TYPE_PONG)
		{
			if(Header.m_PayloadSize != 0)
				return EVoiceIncomingPacketDecision::DROP_PAYLOAD;
			const uint32_t HeaderGroup = VoiceTokenGroupHash(Header.m_TokenHash);
			const uint32_t LocalGroup = VoiceTokenGroupHash(Context.m_LocalTokenHash);
			if(HeaderGroup != 0 && HeaderGroup != LocalGroup)
				return EVoiceIncomingPacketDecision::DROP_KEEPALIVE_TOKEN;
			return Header.m_Type == VOICE_TYPE_PING ? EVoiceIncomingPacketDecision::HANDLE_PING : EVoiceIncomingPacketDecision::HANDLE_PONG;
		}

		if(VoiceTokenGroupHash(Header.m_TokenHash) != VoiceTokenGroupHash(Context.m_LocalTokenHash))
			return EVoiceIncomingPacketDecision::DROP_GROUP;
		if(Header.m_SenderId >= Context.m_MaxClients)
			return EVoiceIncomingPacketDecision::DROP_SENDER;

		if(Header.m_PayloadSize > (uint16_t)VOICE_MAX_PAYLOAD)
			return EVoiceIncomingPacketDecision::DROP_PAYLOAD;
		if((size_t)VOICE_PACKET_HEADER_SIZE + Header.m_PayloadSize != PacketSize)
			return EVoiceIncomingPacketDecision::DROP_PAYLOAD;
		if(Header.m_PayloadSize == 0)
			return EVoiceIncomingPacketDecision::DROP_PAYLOAD;

		return EVoiceIncomingPacketDecision::HANDLE_AUDIO;
	}

	bool VoiceShouldUpdatePingRtt(uint16_t Sequence, uint16_t LastPingSeq, int64_t LastPingSentTime)
	{
		return LastPingSentTime != 0 && Sequence == LastPingSeq;
	}

	bool VoiceShouldIgnoreDistance(bool IgnoreDistanceConfig, bool GroupGlobal, uint32_t LocalTokenHash, uint32_t SenderTokenHash)
	{
		if(IgnoreDistanceConfig)
			return true;

		const uint32_t LocalGroup = VoiceTokenGroupHash(LocalTokenHash);
		const uint32_t SenderGroup = VoiceTokenGroupHash(SenderTokenHash);
		const bool SameRoom = LocalGroup != 0 && SenderGroup == LocalGroup;
		return GroupGlobal && SameRoom;
	}

	vec2 VoiceResolveListenerPosition(vec2 LocalPos, bool SpecActive, vec2 SpecPos, bool HearOnSpecPos)
	{
		if(SpecActive && HearOnSpecPos)
			return SpecPos;
		return LocalPos;
	}

	EVoiceReceiveAudibility EvaluateVoiceReceiveAudibility(const SVoiceReceiveAudibilityContext &Context, const char *pSenderName)
	{
		if(Context.m_IsSelf)
			return Context.m_TestServer ? EVoiceReceiveAudibility::ALLOW : EVoiceReceiveAudibility::DROP_SELF;

		const bool AllowObserver = Context.m_HearPeoplesInSpectate && !Context.m_SenderActive && !Context.m_SenderSpec;
		if(Context.m_VisibilityMode == 0)
		{
			if(!Context.m_IgnoreDistance && !Context.m_SenderActive && !AllowObserver)
				return EVoiceReceiveAudibility::DROP_INACTIVE;
		}
		else if(Context.m_VisibilityMode == 1)
		{
			if(Context.m_SenderOtherTeam && !AllowObserver)
				return EVoiceReceiveAudibility::DROP_OTHER_TEAM;
		}

		if(VoiceListMatch(Context.m_pMuteList, pSenderName))
			return EVoiceReceiveAudibility::DROP_MUTED;
		if(Context.m_ListMode == 1 && !VoiceListMatch(Context.m_pWhitelist, pSenderName))
			return EVoiceReceiveAudibility::DROP_NOT_WHITELISTED;
		if(Context.m_ListMode == 2 && VoiceListMatch(Context.m_pBlacklist, pSenderName))
			return EVoiceReceiveAudibility::DROP_BLACKLISTED;
		if(Context.m_SenderUsesVad && !Context.m_HearVad && !VoiceListMatch(Context.m_pVadAllow, pSenderName))
			return EVoiceReceiveAudibility::DROP_VAD_BLOCKED;
		return EVoiceReceiveAudibility::ALLOW;
	}

	bool VoiceIsPacketWithinAudibleRadius(vec2 LocalPos, vec2 SenderPos, float Radius, bool IgnoreDistance)
	{
		if(IgnoreDistance)
			return true;
		return distance(LocalPos, SenderPos) <= Radius;
	}

	uint32_t VoiceTransmitBlockers(const SVoiceTransmitPreconditions &Preconditions)
	{
		uint32_t Blockers = 0;
		if(Preconditions.m_NeedNetwork)
		{
			if(!Preconditions.m_ServerAddrValid)
				Blockers |= VOICE_TX_BLOCK_SERVER_ADDR;
			if(!Preconditions.m_HaveSocket)
				Blockers |= VOICE_TX_BLOCK_SOCKET;
			if(!Preconditions.m_Online)
				Blockers |= VOICE_TX_BLOCK_ONLINE;
		}
		if(!Preconditions.m_HaveCaptureDevice)
			Blockers |= VOICE_TX_BLOCK_CAPTURE;
		if(!Preconditions.m_HaveEncoder)
			Blockers |= VOICE_TX_BLOCK_ENCODER;
		if(Preconditions.m_MicMuted)
			Blockers |= VOICE_TX_BLOCK_MIC_MUTED;
		return Blockers;
	}

	void FormatVoiceTransmitBlockers(uint32_t Blockers, char *pBuf, int BufSize)
	{
		if(!pBuf || BufSize == 0)
			return;

		if(Blockers == 0)
		{
			str_copy(pBuf, "none", BufSize);
			return;
		}

		struct SBlockerName
		{
			uint32_t m_Bit;
			const char *m_pName;
		};
		static constexpr SBlockerName s_aNames[] = {
			{VOICE_TX_BLOCK_SERVER_ADDR, "server_addr"},
			{VOICE_TX_BLOCK_SOCKET, "socket"},
			{VOICE_TX_BLOCK_ONLINE, "online"},
			{VOICE_TX_BLOCK_CAPTURE, "capture"},
			{VOICE_TX_BLOCK_ENCODER, "encoder"},
			{VOICE_TX_BLOCK_MIC_MUTED, "mic_muted"},
		};

		pBuf[0] = '\0';
		bool First = true;
		for(const auto &Name : s_aNames)
		{
			if((Blockers & Name.m_Bit) == 0)
				continue;

			if(!First)
				str_append(pBuf, ",", BufSize);
			str_append(pBuf, Name.m_pName, BufSize);
			First = false;
		}

		if(pBuf[0] == '\0')
			str_copy(pBuf, "unknown", BufSize);
	}

	bool VoiceNeedsAudioRefresh(const SVoiceAudioRefreshState &State)
	{
		if(State.m_BackendChanged || State.m_InputDeviceChanged || State.m_OutputDeviceChanged || State.m_StereoChanged)
			return true;
		if(State.m_CurrentOutputChannels > 0 && State.m_DesiredOutputChannels > 0 && State.m_CurrentOutputChannels != State.m_DesiredOutputChannels)
			return true;
		if(!State.m_EncoderReady)
			return true;
		if(!State.m_OutputReady && !State.m_OutputUnavailable)
			return true;
		if(!State.m_CaptureReady && !State.m_CaptureUnavailable)
			return true;
		return false;
	}

	void ComputeVoiceEncoderTargets(int LossPerc, float JitterMax, int BitrateProfile, int *pTargetBitrate, int *pTargetLoss, bool *pTargetFec)
	{
		int Complexity = 0;
		ComputeVoiceEncoderTargetsWithComplexity(LossPerc, JitterMax, BitrateProfile, pTargetBitrate, pTargetLoss, pTargetFec, &Complexity);
	}

	void ComputeVoiceEncoderTargetsWithComplexity(int LossPerc, float JitterMax, int BitrateProfile, int *pTargetBitrate, int *pTargetLoss, bool *pTargetFec, int *pTargetComplexity)
	{
		if(!pTargetBitrate || !pTargetLoss || !pTargetFec || !pTargetComplexity)
			return;

		static constexpr int s_aManualBitrates[] = {
			0,
			24000,
			32000,
			48000,
			64000,
		};

		const int ClampedProfile = std::clamp(BitrateProfile, 0, 4);
		if(ClampedProfile != 0)
		{
			*pTargetBitrate = s_aManualBitrates[ClampedProfile];
			*pTargetLoss = 0;
			*pTargetFec = false;
			*pTargetComplexity = 8;
			return;
		}

		const int ClampedLoss = std::clamp(LossPerc, 0, 30);
		const float ClampedJitter = std::isfinite(JitterMax) ? std::max(0.0f, JitterMax) : 0.0f;

		if(ClampedLoss <= 2 && ClampedJitter < 8.0f)
		{
			*pTargetBitrate = 64000;
			*pTargetLoss = 0;
			*pTargetFec = false;
			*pTargetComplexity = 8;
		}
		else if(ClampedLoss <= 5 && ClampedJitter < 16.0f)
		{
			*pTargetBitrate = 48000;
			*pTargetLoss = 5;
			*pTargetFec = true;
			*pTargetComplexity = 8;
		}
		else if(ClampedLoss <= 10 && ClampedJitter < 28.0f)
		{
			*pTargetBitrate = 32000;
			*pTargetLoss = 10;
			*pTargetFec = true;
			*pTargetComplexity = 7;
		}
		else
		{
			*pTargetBitrate = 24000;
			*pTargetLoss = 20;
			*pTargetFec = true;
			*pTargetComplexity = 6;
		}
	}

	float ComputeVoiceAutoGain(float CurrentGain, float FrameRms, const SVoiceAgcConfig &Config)
	{
		if(!Config.m_Enable)
			return 1.0f;

		const float SafeRms = std::clamp(FrameRms, 0.0001f, 1.0f);
		const float TargetGain = std::clamp(Config.m_TargetRms / SafeRms, Config.m_MinGain, Config.m_MaxGain);
		const bool Raising = TargetGain > CurrentGain;
		const float FrameSec = VOICE_FRAME_SAMPLES / (float)VOICE_SAMPLE_RATE;
		const float AttackSec = std::max(Config.m_AttackSec, FrameSec);
		const float ReleaseSec = std::max(Config.m_ReleaseSec, FrameSec);
		const float Slew = Raising ?
					   (1.0f - std::exp(-FrameSec / AttackSec)) :
					   (1.0f - std::exp(-FrameSec / ReleaseSec));
		const float NextGain = CurrentGain + (TargetGain - CurrentGain) * Slew;
		const float TowardUnity = NextGain + (1.0f - NextGain) * 0.01f;
		return std::clamp(TowardUnity, Config.m_MinGain, Config.m_MaxGain);
	}

	uint32_t VoiceRuntimeResetFlags(bool ContextChanged, bool Online, uint32_t PreviousRoomTokenHash, uint32_t CurrentRoomTokenHash)
	{
		uint32_t Flags = 0;
		if(ContextChanged || !Online)
			Flags |= VOICE_RUNTIME_RESET_CONNECTION;
		if((Flags & VOICE_RUNTIME_RESET_CONNECTION) != 0 || PreviousRoomTokenHash != CurrentRoomTokenHash)
			Flags |= VOICE_RUNTIME_RESET_PEERS;
		return Flags;
	}

	const char *VoiceUiMicStatus(const SVoiceUiStatus &Status)
	{
		if(!Status.m_Enabled)
			return "disabled";
		if(Status.m_MicMuted)
			return "muted";
		if(Status.m_CaptureReady)
			return "ready";
		if(Status.m_CaptureUnavailable)
			return "unavailable";
		return "waiting";
	}

	const char *VoiceUiOutputStatus(const SVoiceUiStatus &Status)
	{
		if(!Status.m_Enabled)
			return "disabled";
		if(Status.m_OutputReady)
			return "ready";
		if(Status.m_OutputUnavailable)
			return "unavailable";
		return "waiting";
	}

	const char *VoiceUiServerStatus(const SVoiceUiStatus &Status)
	{
		if(!Status.m_Enabled)
			return "disabled";
		if(!Status.m_NeedNetwork)
			return "local_test";
		if(!Status.m_Online)
			return "offline";
		if(!Status.m_ServerAddrValid)
			return "resolving";
		if(Status.m_Connecting)
			return "connecting";
		if(!Status.m_HaveSocket)
			return "socket_error";
		if(Status.m_PingMs >= 0)
			return "connected";
		return "connected_no_ping";
	}

	const char *VoiceUiRoomStatus(const SVoiceUiStatus &Status)
	{
		if(!Status.m_Enabled)
			return "disabled";
		if(!Status.m_NeedNetwork)
			return "local_test";
		if(!Status.m_Online)
			return "offline";
		if(Status.m_HaveRecentPeers)
			return "matched";
		return "waiting_peer";
	}

	const char *VoiceUiTransportStatus(const SVoiceUiStatus &Status)
	{
		if(!Status.m_Enabled)
			return "disabled";
		if(Status.m_TxActive && Status.m_HaveRecentRx)
			return "tx_rx_active";
		if(Status.m_TxActive)
			return "tx_active";
		if(Status.m_HaveRecentRx)
			return "rx_active";
		if(Status.m_HaveRecentPeers)
			return "idle_with_peer";
		return "idle_no_peer";
	}

	const char *VoiceUiActionHint(const SVoiceUiStatus &Status)
	{
		if(!Status.m_Enabled)
			return "enable_voice";
		const char *pAudioHint = VoiceUiAudioFailureHint(Status);
		if(pAudioHint[0] != '\0')
			return pAudioHint;
		if(Status.m_CaptureUnavailable)
			return "check_input";
		if(Status.m_OutputUnavailable)
			return "check_output";
		if(Status.m_NeedNetwork && !Status.m_Online)
			return "join_server";
		if(Status.m_NeedNetwork && !Status.m_ServerAddrValid)
			return "check_server";
		if(Status.m_NeedNetwork && Status.m_Connecting)
			return "wait_connection";
		if(Status.m_NeedNetwork && !Status.m_HaveSocket)
			return "retry_socket";
		if(Status.m_NeedNetwork && !Status.m_HaveRecentPeers)
			return "check_room";
		if(!Status.m_HaveRecentRx && Status.m_TxActive)
			return "wait_peer";
		return "ok";
	}

	const char *VoiceUiPrimaryError(const SVoiceUiStatus &Status)
	{
		if(Status.m_aAudioError[0] != '\0')
			return Status.m_aAudioError;
		if(Status.m_aNetworkError[0] != '\0')
			return Status.m_aNetworkError;
		if(Status.m_aCodecError[0] != '\0')
			return Status.m_aCodecError;
		return "";
	}

	const char *VoiceUiInputRouteStatus(const SVoiceUiStatus &Status)
	{
		if(!Status.m_Enabled)
			return "disabled";
		if(Status.m_CaptureUnavailable)
		{
			if(ClassifyVoiceAudioIssue(Status) == EVoiceAudioIssue::PERMISSION_DENIED)
				return "permission_denied";
			return Status.m_aRequestedInputDevice[0] != '\0' ? "selected_failed" : "default_failed";
		}
		if(Status.m_CaptureReady)
			return Status.m_aRequestedInputDevice[0] != '\0' ? "using_selected" : "using_default";
		if(Status.m_AudioRefreshPending)
			return Status.m_aRequestedInputDevice[0] != '\0' ? "switching_selected" : "switching_default";
		return "waiting";
	}

	const char *VoiceUiOutputRouteStatus(const SVoiceUiStatus &Status)
	{
		if(!Status.m_Enabled)
			return "disabled";
		if(Status.m_OutputUnavailable)
			return Status.m_aRequestedOutputDevice[0] != '\0' ? "selected_failed" : "default_failed";
		if(Status.m_OutputReady)
			return Status.m_aRequestedOutputDevice[0] != '\0' ? "using_selected" : "using_default";
		if(Status.m_AudioRefreshPending)
			return Status.m_aRequestedOutputDevice[0] != '\0' ? "switching_selected" : "switching_default";
		return "waiting";
	}

	bool VoiceAudioErrorLooksLikePermissionDenied(const char *pError)
	{
		return VoiceContainsNoCase(pError, "permission denied") ||
		       VoiceContainsNoCase(pError, "not permitted") ||
		       VoiceContainsNoCase(pError, "not authorized") ||
		       VoiceContainsNoCase(pError, "microphone access") ||
		       VoiceContainsNoCase(pError, "privacy") ||
		       VoiceContainsNoCase(pError, "kAudioHardwareNotPermittedError");
	}

	EVoiceAudioIssue ClassifyVoiceAudioIssue(const SVoiceUiStatus &Status)
	{
		if(Status.m_aAudioError[0] == '\0')
			return EVoiceAudioIssue::NONE;
		if(VoiceAudioErrorLooksLikePermissionDenied(Status.m_aAudioError))
			return EVoiceAudioIssue::PERMISSION_DENIED;
		if(str_startswith_nocase(Status.m_aAudioError, "Input device not found:"))
			return EVoiceAudioIssue::INPUT_DEVICE_NOT_FOUND;
		if(str_startswith_nocase(Status.m_aAudioError, "Output device not found:"))
			return EVoiceAudioIssue::OUTPUT_DEVICE_NOT_FOUND;
		if(str_comp_nocase(Status.m_aAudioError, "No capture devices available") == 0)
			return EVoiceAudioIssue::NO_CAPTURE_DEVICES;
		if(str_comp_nocase(Status.m_aAudioError, "No output devices available") == 0)
			return EVoiceAudioIssue::NO_OUTPUT_DEVICES;
		if(str_startswith_nocase(Status.m_aAudioError, "Failed to open capture device:"))
			return EVoiceAudioIssue::OPEN_CAPTURE_FAILED;
		if(str_startswith_nocase(Status.m_aAudioError, "Failed to open output device:"))
			return EVoiceAudioIssue::OPEN_OUTPUT_FAILED;
		if(str_startswith_nocase(Status.m_aAudioError, "Failed to init audio"))
			return EVoiceAudioIssue::BACKEND_INIT_FAILED;
		return EVoiceAudioIssue::UNKNOWN;
	}

	const char *VoiceUiAudioFailureHint(const SVoiceUiStatus &Status)
	{
		switch(ClassifyVoiceAudioIssue(Status))
		{
		case EVoiceAudioIssue::NONE:
			return "";
		case EVoiceAudioIssue::INPUT_DEVICE_NOT_FOUND:
		case EVoiceAudioIssue::NO_CAPTURE_DEVICES:
			return "select_input_device";
		case EVoiceAudioIssue::OUTPUT_DEVICE_NOT_FOUND:
		case EVoiceAudioIssue::NO_OUTPUT_DEVICES:
			return "select_output_device";
		case EVoiceAudioIssue::OPEN_CAPTURE_FAILED:
			return "retry_input_open";
		case EVoiceAudioIssue::OPEN_OUTPUT_FAILED:
			return "retry_output_open";
		case EVoiceAudioIssue::PERMISSION_DENIED:
			return "grant_mic_permission";
		case EVoiceAudioIssue::BACKEND_INIT_FAILED:
			return "check_audio_backend";
		case EVoiceAudioIssue::UNKNOWN:
			return "inspect_audio_log";
		}
		return "inspect_audio_log";
	}

	const char *VoiceUiAudioIssueKey(const SVoiceUiStatus &Status)
	{
		switch(ClassifyVoiceAudioIssue(Status))
		{
		case EVoiceAudioIssue::NONE:
			return "none";
		case EVoiceAudioIssue::INPUT_DEVICE_NOT_FOUND:
			return "input_device_not_found";
		case EVoiceAudioIssue::OUTPUT_DEVICE_NOT_FOUND:
			return "output_device_not_found";
		case EVoiceAudioIssue::NO_CAPTURE_DEVICES:
			return "no_capture_devices";
		case EVoiceAudioIssue::NO_OUTPUT_DEVICES:
			return "no_output_devices";
		case EVoiceAudioIssue::OPEN_CAPTURE_FAILED:
			return "open_capture_failed";
		case EVoiceAudioIssue::OPEN_OUTPUT_FAILED:
			return "open_output_failed";
		case EVoiceAudioIssue::PERMISSION_DENIED:
			return "permission_denied";
		case EVoiceAudioIssue::BACKEND_INIT_FAILED:
			return "backend_init_failed";
		case EVoiceAudioIssue::UNKNOWN:
			return "unknown";
		}
		return "unknown";
	}

	float SanitizeFloat(float Value)
	{
		if(!std::isfinite(Value))
			return 0.0f;
		if(Value > 1000000.0f)
			return 1000000.0f;
		if(Value < -1000000.0f)
			return -1000000.0f;
		return Value;
	}

	float VoiceFramePeak(const int16_t *pSamples, int Count)
	{
		if(!pSamples || Count <= 0)
			return 0.0f;

		int Peak = 0;
		for(int i = 0; i < Count; i++)
		{
			const int Sample = pSamples[i];
			const int Abs = Sample == -32768 ? 32768 : (Sample < 0 ? -Sample : Sample);
			if(Abs > Peak)
				Peak = Abs;
		}
		return Peak / 32768.0f;
	}

	float VoiceFrameRms(const int16_t *pSamples, int Count)
	{
		if(Count <= 0)
			return 0.0f;
		double Sum = 0.0;
		for(int i = 0; i < Count; i++)
		{
			const double X = static_cast<double>(pSamples[i]) / 32768.0;
			Sum += X * X;
		}
		return (float)std::sqrt(Sum / (double)Count);
	}

	bool VoiceListMatch(const char *pList, const char *pName)
	{
		if(!pList || pList[0] == '\0')
			return false;

		const char *p = pList;
		while(*p)
		{
			while(*p == ',' || *p == ' ' || *p == '\t')
				p++;
			if(*p == '\0')
				break;

			const char *pStart = p;
			while(*p && *p != ',')
				p++;
			int Len = (int)(p - pStart);
			while(Len > 0 && std::isspace((unsigned char)pStart[Len - 1]))
				Len--;
			if(Len <= 0)
				continue;

			char aToken[MAX_NAME_LENGTH];
			int CopyLen = Len < MAX_NAME_LENGTH - 1 ? Len : MAX_NAME_LENGTH - 1;
			for(int i = 0; i < CopyLen; i++)
				aToken[i] = pStart[i];
			aToken[CopyLen] = '\0';

			if(str_comp_nocase(aToken, pName) == 0)
				return true;
		}

		return false;
	}

	bool VoiceNameVolume(const char *pList, const char *pName, int &OutPercent)
	{
		if(!pList || pList[0] == '\0' || !pName || pName[0] == '\0')
			return false;

		const char *p = pList;
		while(*p)
		{
			while(*p == ',' || *p == ' ' || *p == '\t')
				p++;
			if(*p == '\0')
				break;

			const char *pStart = p;
			while(*p && *p != ',')
				p++;
			const char *pEnd = p;
			while(pEnd > pStart && std::isspace((unsigned char)pEnd[-1]))
				pEnd--;
			if(pEnd <= pStart)
				continue;

			const char *pSep = nullptr;
			for(const char *q = pStart; q < pEnd; q++)
			{
				if(*q == '=' || *q == ':')
				{
					pSep = q;
					break;
				}
			}
			if(!pSep)
				continue;

			const char *pNameEnd = pSep;
			while(pNameEnd > pStart && std::isspace((unsigned char)pNameEnd[-1]))
				pNameEnd--;
			const char *pValueStart = pSep + 1;
			while(pValueStart < pEnd && std::isspace((unsigned char)*pValueStart))
				pValueStart++;

			const int NameLen = (int)(pNameEnd - pStart);
			const int ValueLen = (int)(pEnd - pValueStart);
			if(NameLen <= 0 || ValueLen <= 0)
				continue;

			char aToken[MAX_NAME_LENGTH];
			int CopyLen = NameLen < MAX_NAME_LENGTH - 1 ? NameLen : MAX_NAME_LENGTH - 1;
			for(int i = 0; i < CopyLen; i++)
				aToken[i] = pStart[i];
			aToken[CopyLen] = '\0';

			if(str_comp_nocase(aToken, pName) != 0)
				continue;

			char aValue[16];
			int ValueCopyLen = ValueLen < 15 ? ValueLen : 15;
			for(int i = 0; i < ValueCopyLen; i++)
				aValue[i] = pValueStart[i];
			aValue[ValueCopyLen] = '\0';

			int Percent = str_toint(aValue);
			Percent = std::clamp(Percent, 0, 200);
			OutPercent = Percent;
			return true;
		}

		return false;
	}

	void ApplyMicGain(float Gain, int16_t *pSamples, int Count)
	{
		if(Gain == 1.0f)
			return;

		for(int i = 0; i < Count; i++)
		{
			const float Out = pSamples[i] * Gain;
			const int Sample = (int)std::clamp(Out, -32768.0f, 32767.0f);
			pSamples[i] = (int16_t)Sample;
		}
	}

	void BlendDenoisedFrame(const int16_t *pDrySamples, int16_t *pWetSamples, int Count, float WetMix)
	{
		if(!pDrySamples || !pWetSamples || Count <= 0)
			return;

		const float ClampedWetMix = std::clamp(WetMix, 0.0f, 1.0f);
		if(ClampedWetMix >= 0.999f)
			return;
		if(ClampedWetMix <= 0.001f)
		{
			mem_copy(pWetSamples, pDrySamples, Count * sizeof(int16_t));
			return;
		}

		const float DryMix = 1.0f - ClampedWetMix;
		for(int i = 0; i < Count; i++)
		{
			const float Out = pDrySamples[i] * DryMix + pWetSamples[i] * ClampedWetMix;
			pWetSamples[i] = (int16_t)std::clamp(Out, -32768.0f, 32767.0f);
		}
	}

	void ApplyHpfCompressor(const SCompressorConfig &Config, int16_t *pSamples, int Count, SHpfCompressorState &State)
	{
		if(!Config.m_Enable)
			return;

		const float CutoffHz = VOICE_HPF_CUTOFF_HZ;
		const float Rc = 1.0f / (2.0f * 3.14159265f * CutoffHz);
		const float Dt = 1.0f / VOICE_SAMPLE_RATE;
		const float Alpha = Rc / (Rc + Dt);

		const float NoiseFloor = 0.02f;
		const float AttackCoeff = 1.0f - std::exp(-1.0f / (Config.m_AttackSec * VOICE_SAMPLE_RATE));
		const float ReleaseCoeff = 1.0f - std::exp(-1.0f / (Config.m_ReleaseSec * VOICE_SAMPLE_RATE));

		for(int i = 0; i < Count; i++)
		{
			const float X = pSamples[i] / 32768.0f;
			const float Y = Alpha * (State.m_PrevOut + X - State.m_PrevIn);
			State.m_PrevIn = X;
			State.m_PrevOut = SanitizeFloat(Y);

			const float AbsY = std::fabs(State.m_PrevOut);
			if(AbsY > State.m_Env)
				State.m_Env += (AbsY - State.m_Env) * AttackCoeff;
			else
				State.m_Env += (AbsY - State.m_Env) * ReleaseCoeff;

			float Gain = 1.0f;
			if(State.m_Env > Config.m_Threshold)
				Gain = (Config.m_Threshold + (State.m_Env - Config.m_Threshold) / Config.m_Ratio) / State.m_Env;
			if(State.m_Env > NoiseFloor)
				Gain *= Config.m_MakeupGain;

			const float Out = std::clamp(State.m_PrevOut * Gain, -Config.m_Limiter, Config.m_Limiter);
			const int Sample = (int)std::clamp(Out * 32767.0f, -32768.0f, 32767.0f);
			pSamples[i] = (int16_t)Sample;
		}
	}

	S3DAudioResult Compute3DAudio(
		vec2 LocalPos,
		vec2 SenderPos,
		float Radius,
		float Volume,
		float StereoWidth,
		bool StereoEnabled,
		bool IgnoreDistance)
	{
		S3DAudioResult Result = {1.0f, 1.0f, 1.0f};

		const float Dist = distance(LocalPos, SenderPos);
		if(!IgnoreDistance && Dist > Radius)
		{
			Result.m_Volume = 0.0f;
			Result.m_LeftGain = 0.0f;
			Result.m_RightGain = 0.0f;
			return Result;
		}

		const float RadiusFactor = IgnoreDistance ? 1.0f : (1.0f - (Dist / Radius));
		Result.m_Volume = std::clamp(RadiusFactor * Volume, 0.0f, 4.0f);

		const float Pan = StereoEnabled ? std::clamp(((SenderPos.x - LocalPos.x) / Radius) * StereoWidth, -1.0f, 1.0f) : 0.0f;
		Result.m_LeftGain = Result.m_Volume * (Pan <= 0.0f ? 1.0f : (1.0f - Pan));
		Result.m_RightGain = Result.m_Volume * (Pan >= 0.0f ? 1.0f : (1.0f + Pan));

		return Result;
	}

}
