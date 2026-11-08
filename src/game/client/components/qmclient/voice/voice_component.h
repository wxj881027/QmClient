// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_COMPONENT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_COMPONENT_H

#include "voice_core.h"

#include <engine/shared/config.h>

#include <game/client/component.h>

class CVoiceComponent : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnConsoleInit() override;

	void RenderOverlay();
	const CRClientVoice &Voice() const { return m_Voice; }

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
	CVoiceOverlayState m_OverlayState;
	// 首次启用语音时是否已放行冷启动；关闭语音后重新复位，让下一次启用重新等卡片动画。
	bool m_ColdStartReleased = false;
	// 等卡片动画的上限时刻：动画标记异常时到点直接启动，避免语音一直起不来。
	int64_t m_ColdStartDeadline = 0;
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_COMPONENT_H
