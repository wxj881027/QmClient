#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_COMPONENT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_COMPONENT_H

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
	
	/// 是否配置使用 Rust 语音系统
	bool UseRustVoice() const { return g_Config.m_RiVoiceEnable && g_Config.m_RiVoiceUseRust; }
	
	/// Rust 语音系统是否实际可用 (配置启用且初始化成功)
	bool IsRustVoiceActive() const { return UseRustVoice() && !m_RustInitFailed; }
	
	void RenderOverlay() { 
		// 始终调用 C++ 的 RenderSpeakerOverlay
		// 因为音频捕获由 C++ 系统处理，电平数据在 C++ 系统中
		m_Voice.RenderSpeakerOverlay();
	}
	
	/// 更新配置
	void UpdateConfig();
	
	/// 更新玩家快照
	void UpdatePlayers(const std::vector<voice::PlayerSnapshot> &players);
	
	/// 设置本地客户端 ID
	void SetLocalClientId(int clientId);
	
	/// 设置上下文哈希
	void SetContextHash(uint32_t hash);
	
	/// 获取麦克风电平 (0-100)
	int GetMicLevel() const;
	
	/// 是否正在说话
	bool IsSpeaking() const;
	
	/// 处理音频帧 (捕获 -> DSP -> 编码)
	/// @param pcm 输入/输出 PCM 数据 (960 samples)
	/// @param output 输出缓冲区 (用于存储编码后的网络包)
	/// @return 编码后的数据长度，如果不需要发送则返回 0
	int ProcessFrame(int16_t *pcm, size_t pcmLen, uint8_t *output, size_t outputLen);
	
	/// 接收网络数据
	void ReceivePacket(const uint8_t *data, size_t len);
	
	/// 解码抖动缓冲
	void DecodeJitter();
	
	/// 混音输出
	void MixAudio(int16_t *output, size_t samples, size_t channels);
	
	/// 更新编码器参数
	void UpdateEncoderParams();

private:
	static void ConVoicePtt(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceListDevices(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceSetInputDevice(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceSetOutputDevice(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceClearInputDevice(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceClearOutputDevice(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceToggleMicMute(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceSetMicMute(IConsole::IResult *pResult, void *pUserData);

	CRClientVoice m_Voice;        // C++ 语音实现
	voice::VoiceSystem m_RustVoice;  // Rust 语音实现
	bool m_RustInitFailed = false;    // Rust 初始化是否失败
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_COMPONENT_H
