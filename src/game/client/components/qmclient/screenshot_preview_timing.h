#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_PREVIEW_TIMING_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCREENSHOT_PREVIEW_TIMING_H

#include <algorithm>
#include <chrono>

class CQmScreenshotPreviewTiming
{
	static constexpr std::chrono::milliseconds FADE_DURATION{220};
	std::chrono::nanoseconds m_Start{};
	std::chrono::nanoseconds m_End{};

public:
	void Start(std::chrono::nanoseconds Now)
	{
		m_Start = Now;
		m_End = Now + std::chrono::seconds(1);
	}

	void Dismiss(std::chrono::nanoseconds Now)
	{
		m_End = std::min(m_End, Now + FADE_DURATION);
	}

	float ElapsedSeconds(std::chrono::nanoseconds Now) const
	{
		return std::chrono::duration<float>(Now - m_Start).count();
	}

	float Alpha(std::chrono::nanoseconds Now) const
	{
		const std::chrono::duration<float> Remaining = m_End - Now;
		return std::clamp(Remaining / FADE_DURATION, 0.0f, 1.0f);
	}
};

#endif
