#include "voice_component.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/shared/config.h>

void CVoiceComponent::ConVoicePtt(IConsole::IResult *pResult, void *pUserData)
{
	CVoiceComponent *pThis = static_cast<CVoiceComponent *>(pUserData);
	if(pThis->IsRustVoiceActive()) {
		pThis->m_RustVoice.setPTT(pResult->GetInteger(0) != 0);
	} else {
		pThis->m_Voice.SetPttActive(pResult->GetInteger(0) != 0);
	}
}

void CVoiceComponent::ConVoiceListDevices(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CVoiceComponent *pThis = static_cast<CVoiceComponent *>(pUserData);
	pThis->m_Voice.ListDevices();
}

void CVoiceComponent::ConVoiceSetInputDevice(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;
	str_copy(g_Config.m_QmVoiceInputDevice, pResult->GetString(0), sizeof(g_Config.m_QmVoiceInputDevice));
}

void CVoiceComponent::ConVoiceSetOutputDevice(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;
	str_copy(g_Config.m_QmVoiceOutputDevice, pResult->GetString(0), sizeof(g_Config.m_QmVoiceOutputDevice));
}

void CVoiceComponent::ConVoiceClearInputDevice(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	g_Config.m_QmVoiceInputDevice[0] = '\0';
}

void CVoiceComponent::ConVoiceClearOutputDevice(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	g_Config.m_QmVoiceOutputDevice[0] = '\0';
}

void CVoiceComponent::ConVoiceToggleMicMute(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	g_Config.m_QmVoiceMicMute = g_Config.m_QmVoiceMicMute ? 0 : 1;
}

void CVoiceComponent::ConVoiceSetMicMute(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;
	g_Config.m_QmVoiceMicMute = pResult->GetInteger(0) != 0 ? 1 : 0;
}

void CVoiceComponent::OnInit()
{
	m_RustInitFailed = false;

	if(UseRustVoice()) {
		voice::Config config;
		config.mic_volume = g_Config.m_QmVoiceMicVolume;
		config.noise_suppress = g_Config.m_QmVoiceNoiseSuppressEnable != 0;
		config.noise_suppress_strength = g_Config.m_QmVoiceNoiseSuppressStrength;
		config.comp_threshold = g_Config.m_QmVoiceCompThreshold;
		config.comp_ratio = g_Config.m_QmVoiceCompRatio;
		config.comp_attack_ms = g_Config.m_QmVoiceCompAttackMs;
		config.comp_release_ms = g_Config.m_QmVoiceCompReleaseMs;
		config.comp_makeup = g_Config.m_QmVoiceCompMakeup;
		config.vad_enable = g_Config.m_QmVoiceVadEnable != 0;
		config.vad_threshold = g_Config.m_QmVoiceVadThreshold;
		config.vad_release_delay_ms = g_Config.m_QmVoiceVadReleaseDelayMs;
		config.stereo = g_Config.m_QmVoiceStereo != 0;
		config.stereo_width = g_Config.m_QmVoiceStereoWidth;
		config.volume = g_Config.m_QmVoiceVolume;
		config.radius = g_Config.m_QmVoiceRadius;
		config.mic_mute = g_Config.m_QmVoiceMicMute != 0;
		config.test_mode = g_Config.m_QmVoiceTestMode;
		config.ignore_distance = g_Config.m_QmVoiceIgnoreDistance != 0;
		config.group_global = g_Config.m_QmVoiceGroupGlobal != 0;
		config.token_hash = 0;
		config.context_hash = 0;
		
		m_RustVoice.setConfig(config);

		// 检查 Rust 初始化是否成功，失败则回退到 C++ 模式
		if(!m_RustVoice.init()) {
			log_error("voice", "Rust voice system init failed, falling back to C++ mode");
			m_RustInitFailed = true;
		}
		
		// 同时初始化 C++ 语音系统用于音频捕获
		m_Voice.Init(GameClient(), Client(), Console());
	} else {
		m_Voice.Init(GameClient(), Client(), Console());
	}
}

void CVoiceComponent::OnShutdown()
{
	if(IsRustVoiceActive()) {
		m_RustVoice.shutdown();
	}
	m_Voice.OnShutdown();
}

void CVoiceComponent::OnRender()
{
	// 始终调用 C++ 的 OnRender 来驱动音频捕获
	// C++ 版本会处理音频设备和网络
	m_Voice.OnRender();
	
	// 如果 Rust 语音系统实际可用，同步状态
	if(IsRustVoiceActive()) {
		// 更新 Rust 配置
		UpdateConfig();
	}
}

void CVoiceComponent::OnConsoleInit()
{
	Console()->Register("+qm_voice_ptt", "", CFGFLAG_CLIENT, ConVoicePtt, this, "Push-to-talk for voice chat");
	Console()->Register("qm_voice_list_devices", "", CFGFLAG_CLIENT, ConVoiceListDevices, this, "List available voice devices");
	Console()->Register("qm_voice_set_input_device", "s[name]", CFGFLAG_CLIENT, ConVoiceSetInputDevice, this, "Set voice input device");
	Console()->Register("qm_voice_set_output_device", "s[name]", CFGFLAG_CLIENT, ConVoiceSetOutputDevice, this, "Set voice output device");
	Console()->Register("qm_voice_clear_input_device", "", CFGFLAG_CLIENT, ConVoiceClearInputDevice, this, "Use default voice input device");
	Console()->Register("qm_voice_clear_output_device", "", CFGFLAG_CLIENT, ConVoiceClearOutputDevice, this, "Use default voice output device");
	Console()->Register("qm_voice_toggle_mic", "", CFGFLAG_CLIENT, ConVoiceToggleMicMute, this, "Toggle microphone mute");
	Console()->Register("qm_voice_set_mic", "i[state]", CFGFLAG_CLIENT, ConVoiceSetMicMute, this, "Set microphone mute state (0=on, 1=mute)");
}

void CVoiceComponent::UpdateConfig()
{
	if(!IsRustVoiceActive()) return;
	
	voice::Config config;
	config.mic_volume = g_Config.m_QmVoiceMicVolume;
	config.noise_suppress = g_Config.m_QmVoiceNoiseSuppressEnable != 0;
	config.noise_suppress_strength = g_Config.m_QmVoiceNoiseSuppressStrength;
	config.comp_threshold = g_Config.m_QmVoiceCompThreshold;
	config.comp_ratio = g_Config.m_QmVoiceCompRatio;
	config.comp_attack_ms = g_Config.m_QmVoiceCompAttackMs;
	config.comp_release_ms = g_Config.m_QmVoiceCompReleaseMs;
	config.comp_makeup = g_Config.m_QmVoiceCompMakeup;
	config.vad_enable = g_Config.m_QmVoiceVadEnable != 0;
	config.vad_threshold = g_Config.m_QmVoiceVadThreshold;
	config.vad_release_delay_ms = g_Config.m_QmVoiceVadReleaseDelayMs;
	config.stereo = g_Config.m_QmVoiceStereo != 0;
	config.stereo_width = g_Config.m_QmVoiceStereoWidth;
	config.volume = g_Config.m_QmVoiceVolume;
	config.radius = g_Config.m_QmVoiceRadius;
	config.mic_mute = g_Config.m_QmVoiceMicMute != 0;
	config.test_mode = g_Config.m_QmVoiceTestMode;
	config.ignore_distance = g_Config.m_QmVoiceIgnoreDistance != 0;
	config.group_global = g_Config.m_QmVoiceGroupGlobal != 0;
	config.token_hash = 0;
	config.context_hash = 0;
	
	m_RustVoice.setConfig(config);
}

void CVoiceComponent::UpdatePlayers(const std::vector<voice::PlayerSnapshot> &players)
{
	if(!IsRustVoiceActive()) return;
	m_RustVoice.updatePlayers(players);
}

void CVoiceComponent::SetLocalClientId(int clientId)
{
	if(!IsRustVoiceActive()) return;
	m_RustVoice.setLocalClientId(clientId);
}

void CVoiceComponent::SetContextHash(uint32_t hash)
{
	if(!IsRustVoiceActive()) return;
	m_RustVoice.setContextHash(hash);
}

int CVoiceComponent::GetMicLevel() const
{
	// Rust 模式下优先使用 Rust 端的电平数据
	// Rust 端的 processFrame 会根据 DSP 处理后的音频计算电平
	if(IsRustVoiceActive()) {
		return m_RustVoice.getMicLevel();
	}
	return static_cast<int>(m_Voice.MicLevel());
}

bool CVoiceComponent::IsSpeaking() const
{
	// Rust 模式下优先使用 Rust 端的说话状态
	// Rust 端的 processFrame 会根据 VAD/PTT 状态更新说话状态
	if(IsRustVoiceActive()) {
		return m_RustVoice.isSpeaking();
	}
	return m_Voice.IsSpeaking();
}

int CVoiceComponent::ProcessFrame(int16_t *pcm, size_t pcmLen, uint8_t *output, size_t outputLen)
{
	if(!IsRustVoiceActive()) return 0;
	return m_RustVoice.processFrame(pcm, pcmLen, output, outputLen);
}

void CVoiceComponent::ReceivePacket(const uint8_t *data, size_t len)
{
	if(!IsRustVoiceActive()) return;
	m_RustVoice.receivePacket(data, len);
}

void CVoiceComponent::DecodeJitter()
{
	if(!IsRustVoiceActive()) return;
	m_RustVoice.decodeJitter();
}

void CVoiceComponent::MixAudio(int16_t *output, size_t samples, size_t channels)
{
	if(!IsRustVoiceActive()) return;
	m_RustVoice.mixAudio(output, samples, channels);
}

void CVoiceComponent::UpdateEncoderParams()
{
	if(!IsRustVoiceActive()) return;
	m_RustVoice.updateEncoderParams();
}
