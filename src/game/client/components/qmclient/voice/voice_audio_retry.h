#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_AUDIO_RETRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_AUDIO_RETRY_H

#include <base/str.h>

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

// 子系统初始化只由主线程推进；Shutdown 必须在音频工作线程 join 后调用。
class CVoiceAudioSubsystem
{
	bool m_Initialized = false;
	bool m_Attempted = false;
	int64_t m_NextAttempt = 0;
	char m_aBackend[64] = {};

public:
	bool IsInitialized() const { return m_Initialized; }

	template<typename TGetTime, typename TInitialize>
	bool Ensure(const char *pBackend, int64_t RetryInterval, TGetTime GetTime, TInitialize Initialize)
	{
		if(m_Initialized)
			return true;
		if(m_Attempted && str_comp(m_aBackend, pBackend) == 0 && GetTime() < m_NextAttempt)
			return false;
		m_Attempted = true;
		str_copy(m_aBackend, pBackend);
		m_Initialized = Initialize();
		// 驱动初始化本身可能很慢，等待间隔从失败返回后开始。
		if(!m_Initialized)
			m_NextAttempt = GetTime() + RetryInterval;
		return m_Initialized;
	}

	template<typename TShutdown>
	void Shutdown(TShutdown Release)
	{
		if(m_Initialized)
			Release();
		m_Initialized = false;
		m_Attempted = false;
		m_NextAttempt = 0;
		m_aBackend[0] = '\0';
	}
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_AUDIO_RETRY_H
