#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_VAD_STATE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_VAD_STATE_H

#include <atomic>
#include <cstdint>

class CVoiceVadState
{
	std::atomic<bool> m_Active = false;
	std::atomic<int64_t> m_ReleaseDeadline = 0;

public:
	static bool ShouldActivate(float Peak, float Threshold) noexcept
	{
		return Threshold <= 0.0f || Peak >= Threshold;
	}

	bool IsActive() const noexcept { return m_Active.load(); }

	void Reset() noexcept
	{
		m_Active.store(false);
		m_ReleaseDeadline.store(0);
	}

	void PrepareUpdate(bool UseVad, int64_t Now) noexcept
	{
		// 零释放延迟没有计时截止点，收到静音帧时才释放。
		if(!UseVad || (m_Active.load() && m_ReleaseDeadline.load() != 0 && Now >= m_ReleaseDeadline.load()))
			Reset();
	}

	void UpdateFrame(bool Trigger, int64_t FrameNow, int64_t ReleaseTicks) noexcept
	{
		if(Trigger)
		{
			m_Active.store(true);
			m_ReleaseDeadline.store(ReleaseTicks > 0 ? FrameNow + ReleaseTicks : 0);
		}
		else if(m_Active.load() && (m_ReleaseDeadline.load() == 0 || FrameNow >= m_ReleaseDeadline.load()))
		{
			Reset();
		}
	}
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_VOICE_VAD_STATE_H
