#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_VOICE_COMPONENT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_VOICE_COMPONENT_H

#include <engine/shared/config.h>
#include <game/client/component.h>

#include "voice_core.h"
#include "voice_rust.h"

class CVoiceComponent : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnConsoleInit() override;
	
	/// 是否使用 Rust 重构版语音系统
	bool UseRustVoice() const { return g_Config.m_RiVoiceUseRust != 0; }
	
	void RenderOverlay() { 
		if(UseRustVoice()) {
			// Rust 版本暂无覆盖层渲染
		} else {
			m_Voice.RenderSpeakerOverlay();
		}
	}

private:
	static void ConVoicePtt(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceListDevices(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceSetInputDevice(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceSetOutputDevice(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceClearInputDevice(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceClearOutputDevice(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceToggleMicMute(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceSetMicMute(IConsole::IResult *pResult, void *pUserData);

	CRClientVoice m_Voice;
	voice::VoiceSystem m_RustVoice;
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_VOICE_COMPONENT_H
