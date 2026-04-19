#include "voice_component.h"

#include <base/system.h>

#include <engine/shared/config.h>

void CVoiceComponent::ConVoicePtt(IConsole::IResult *pResult, void *pUserData)
{
	CVoiceComponent *pThis = static_cast<CVoiceComponent *>(pUserData);
	if(pThis->UseRustVoice()) {
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
	str_copy(g_Config.m_RiVoiceInputDevice, pResult->GetString(0), sizeof(g_Config.m_RiVoiceInputDevice));
}

void CVoiceComponent::ConVoiceSetOutputDevice(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;
	str_copy(g_Config.m_RiVoiceOutputDevice, pResult->GetString(0), sizeof(g_Config.m_RiVoiceOutputDevice));
}

void CVoiceComponent::ConVoiceClearInputDevice(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	g_Config.m_RiVoiceInputDevice[0] = '\0';
}

void CVoiceComponent::ConVoiceClearOutputDevice(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	g_Config.m_RiVoiceOutputDevice[0] = '\0';
}

void CVoiceComponent::ConVoiceToggleMicMute(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	g_Config.m_RiVoiceMicMute = g_Config.m_RiVoiceMicMute ? 0 : 1;
}

void CVoiceComponent::ConVoiceSetMicMute(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;
	g_Config.m_RiVoiceMicMute = pResult->GetInteger(0) != 0 ? 1 : 0;
}

void CVoiceComponent::OnInit()
{
	if(UseRustVoice()) {
		// Rust 语音系统初始化
		voice::Config config;
		config.mic_volume = g_Config.m_RiVoiceMicVolume;
		config.noise_suppress = g_Config.m_RiVoiceNoiseSuppressEnable != 0;
		config.noise_suppress_strength = g_Config.m_RiVoiceNoiseSuppressStrength;
		config.comp_threshold = g_Config.m_RiVoiceCompThreshold;
		config.comp_ratio = g_Config.m_RiVoiceCompRatio;
		config.comp_attack_ms = g_Config.m_RiVoiceCompAttackMs;
		config.comp_release_ms = g_Config.m_RiVoiceCompReleaseMs;
		config.comp_makeup = g_Config.m_RiVoiceCompMakeup;
		config.vad_enable = g_Config.m_RiVoiceVadEnable != 0;
		config.vad_threshold = g_Config.m_RiVoiceVadThreshold;
		config.vad_release_delay_ms = g_Config.m_RiVoiceVadReleaseDelayMs;
		config.stereo = g_Config.m_RiVoiceStereo != 0;
		config.stereo_width = g_Config.m_RiVoiceStereoWidth;
		config.volume = g_Config.m_RiVoiceVolume;
		config.radius = g_Config.m_RiVoiceRadius;
		
		m_RustVoice.setConfig(config);
	} else {
		m_Voice.Init(GameClient(), Client(), Console());
	}
}

void CVoiceComponent::OnShutdown()
{
	if(!UseRustVoice()) {
		m_Voice.OnShutdown();
	}
}

void CVoiceComponent::OnRender()
{
	if(!UseRustVoice()) {
		m_Voice.OnRender();
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
