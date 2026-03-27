#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_VOICE_COMPONENT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_VOICE_COMPONENT_H

#include <engine/shared/console.h>

#include <game/client/component.h>

#include "voice_core.h"

class CVoiceComponent : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnConsoleInit() override;
	bool IsVoiceActive(int ClientId) const { return m_Voice.IsVoiceActive(ClientId); }
	float MicLevel() const { return m_Voice.MicLevel(); }

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
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_VOICE_COMPONENT_H
