#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_AUDIO_RETRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_AUDIO_RETRY_H

#include <cstdint>

// 音频初始化与设备重试由工作线程推进；主线程只在 join 后重置。
class CVoiceAudioRetry
{
	int64_t m_NextAttempt = 0;
	bool m_InitializationFailed = false;

public:
	void Reset()
	{
		m_NextAttempt = 0;
		m_InitializationFailed = false;
	}

	template<typename TInitialize>
	void Update(bool RefreshRequested, bool DeviceUnavailable, int64_t Now, int64_t RetryInterval, TInitialize Initialize)
	{
		if(!RefreshRequested && (!(m_InitializationFailed || DeviceUnavailable) || Now < m_NextAttempt))
			return;
		// 编码器失败可能先于设备打开，不能仅依赖设备不可用标志重试。
		m_InitializationFailed = !Initialize();
		m_NextAttempt = Now + RetryInterval;
	}
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_AUDIO_RETRY_H
